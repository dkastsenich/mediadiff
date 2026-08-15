#include "report/markdown.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

namespace mediadiff {

// GitHub's real PR-comment limit is 65,536 CHARACTERS, not an ambiguous
// "60 KB" figure (02-RESEARCH.md Pitfall 3, REPORT-05) -- this constant is
// counted in UTF-8 BYTES, deliberately below that character limit, because
// multi-byte UTF-8 content (any non-ASCII finding message, filename or
// check id) inflates a byte count above the equivalent character count,
// and this renderer has no way to know in advance how much of a report's
// content will be non-ASCII. 60000 bytes carries roughly 5,500 bytes of
// headroom under the 65,536-character ceiling even in the worst case
// (every character 1 byte), and far more headroom in the common case
// (mostly ASCII with occasional multi-byte content) -- comfortably
// conservative without being so tight that ordinary reports trip the fold
// needlessly.
const std::size_t kMarkdownBudgetBytes = 60000;

namespace {

std::string_view status_text(Status status) {
  switch (status) {
    case Status::pass:
      return "pass";
    case Status::info:
      return "info";
    case Status::warn:
      return "warn";
    case Status::fail:
      return "fail";
    case Status::skipped:
      return "skipped";
    case Status::error:
      return "error";
  }
  return "error";
}

// Escapes text for use inside a single Markdown table cell: a literal `|`
// would otherwise be read as a column delimiter, and an embedded newline
// would break the table's one-row-per-line structure -- both are plausible
// in a finding message (a filename, a comma-separated tag list) or a check
// id's `--explain` text.
std::string escape_cell(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c == '|') {
      out += "\\|";
    } else if (c == '\n' || c == '\r') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

std::string render_summary_table(const Summary& summary) {
  return fmt::format(
      "| Status | Count |\n"
      "| --- | --- |\n"
      "| pass | {} |\n"
      "| info | {} |\n"
      "| warn | {} |\n"
      "| fail | {} |\n"
      "| skipped | {} |\n"
      "| error | {} |\n",
      summary.pass, summary.info, summary.warn, summary.fail, summary.skipped, summary.error);
}

std::string render_group_details(const GroupBlock& block) {
  std::string out = fmt::format("<details>\n<summary>{} ({})</summary>\n\n", group_to_string(block.group),
                                 block.findings.size());
  out += "| ID | Scope | Status | Severity | Message |\n";
  out += "| --- | --- | --- | --- | --- |\n";
  for (const Finding& finding : block.findings) {
    out += fmt::format("| {} | {} | {} | {} | {} |\n", escape_cell(finding.id), scope_to_text(finding.scope),
                        status_text(finding.status), severity_to_string(finding.severity),
                        escape_cell(finding.message));
  }
  out += "\n</details>\n";
  return out;
}

// The document with `excluded` findings omitted from their own group's
// table (and that group's own <details> block omitted entirely if it
// becomes empty as a result) -- no fold line included; the caller appends
// that separately once the withheld count is known. `extra_section`
// (empty for a single-file document) is inserted immediately after the
// summary table and before the first group's own `<details>` block --
// `dir` mode's own per-file summary table (DIR-03) is exactly this: it
// must never be subject to the fold below, matching the top-level summary
// table's own "always present, even at zero" contract, so it is written
// here, ahead of any group content the fold can still drop.
std::string render_body(const ReportModel& model, const std::vector<std::vector<bool>>& excluded,
                         std::string_view extra_section = "") {
  std::string out = "# mediadiff report\n\n## Summary\n\n";
  out += render_summary_table(model.summary);
  out += "\n";
  out += extra_section;
  for (std::size_t g = 0; g < model.groups.size(); ++g) {
    const GroupBlock& block = model.groups[g];
    if (block.findings.empty()) {
      continue;
    }
    GroupBlock visible{block.group, block.counts, {}};
    for (std::size_t f = 0; f < block.findings.size(); ++f) {
      if (!excluded[g][f]) {
        visible.findings.push_back(block.findings[f]);
      }
    }
    if (visible.findings.empty()) {
      continue;
    }
    out += render_group_details(visible);
    out += "\n";
  }
  return out;
}

// A byte is a UTF-8 CONTINUATION byte (10xxxxxx) -- never a valid position
// to cut a string, since doing so would split a multi-byte code point.
bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

// Truncates `text` to at most `max_bytes`, walking backward from that byte
// offset until landing on a byte that is NOT a UTF-8 continuation byte --
// never mid-code-point, per this renderer's own contract.
std::string truncate_utf8(const std::string& text, std::size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return text;
  }
  std::size_t cut = max_bytes;
  while (cut > 0 && is_utf8_continuation(static_cast<unsigned char>(text[cut]))) {
    --cut;
  }
  return text.substr(0, cut);
}

std::string fold_line(std::size_t withheld_count) {
  return fmt::format("_{} more finding{}, see JSON artifact_\n", withheld_count, withheld_count == 1 ? "" : "s");
}

// Shared by both render_markdown overloads below: single-file passes an
// empty `extra_section`, `dir` mode's corpus overload passes its own
// per-file summary table (report/markdown.h's own render_body doc
// comment). Every fold candidate here is drawn from `model.groups`, so
// `extra_section` -- never subject to the fold -- is threaded through
// every render_body call, including the pathological truncation path.
std::string render_markdown_impl(const ReportModel& model, bool strict, std::string_view extra_section) {
  // Nothing excluded yet -- the unfolded body is the common case and the
  // only one most reports ever need.
  std::vector<std::vector<bool>> excluded;
  excluded.reserve(model.groups.size());
  for (const GroupBlock& block : model.groups) {
    excluded.emplace_back(block.findings.size(), false);
  }

  std::string unfolded = render_body(model, excluded, extra_section);
  if (unfolded.size() <= kMarkdownBudgetBytes) {
    return unfolded;
  }

  // Two removal tiers, each in canonical (group order, then registry
  // order) sequence: tier 1 is every finding this run's fold rule
  // considers non-gating (never Severity::fail; Severity::warn only when
  // NOT strict); tier 2 is Severity::warn findings when strict IS in
  // effect -- droppable only once tier 1 is fully exhausted. A
  // Severity::fail finding is never a member of either tier, matching this
  // renderer's own "every fail finding survives folding" contract.
  struct Ref {
    std::size_t group;
    std::size_t finding;
  };
  std::vector<Ref> tier1;
  std::vector<Ref> tier2;
  for (std::size_t g = 0; g < model.groups.size(); ++g) {
    const GroupBlock& block = model.groups[g];
    for (std::size_t f = 0; f < block.findings.size(); ++f) {
      const Severity severity = block.findings[f].severity;
      if (severity == Severity::fail) {
        continue;  // never droppable
      }
      if (severity == Severity::warn && strict) {
        tier2.push_back(Ref{g, f});
      } else {
        tier1.push_back(Ref{g, f});
      }
    }
  }

  // "From the end": each tier is walked last-to-first, and tier 1 is fully
  // exhausted (in that order) before tier 2 is touched at all.
  std::vector<Ref> candidates;
  candidates.insert(candidates.end(), tier1.rbegin(), tier1.rend());
  candidates.insert(candidates.end(), tier2.rbegin(), tier2.rend());

  std::string folded;
  std::size_t dropped = 0;
  bool fits = false;
  for (std::size_t k = 1; k <= candidates.size(); ++k) {
    excluded[candidates[k - 1].group][candidates[k - 1].finding] = true;
    dropped = k;
    folded = render_body(model, excluded, extra_section) + fold_line(dropped);
    if (folded.size() <= kMarkdownBudgetBytes) {
      fits = true;
      break;
    }
  }

  if (fits) {
    return folded;
  }

  // Pathological case (doc's own "requires a pathological group count"):
  // every droppable finding is gone and the document -- summary table plus
  // whatever Severity::fail findings remain -- still exceeds the budget.
  // Truncate on a UTF-8 character boundary and still append the fold line
  // naming the real count.
  const std::string line = fold_line(dropped);
  const std::size_t body_budget = line.size() < kMarkdownBudgetBytes ? kMarkdownBudgetBytes - line.size() : 0;
  return truncate_utf8(render_body(model, excluded, extra_section), body_budget) + line;
}

}  // namespace

std::string render_markdown(const ReportModel& model, const CheckRegistry& /*registry*/, bool strict) {
  return render_markdown_impl(model, strict, "");
}

std::string render_markdown(const CorpusModel& model, const CheckRegistry& registry, bool strict) {
  // A per-file summary table, one row per FileBlock in `model.files`' own
  // order -- DIR-03's "summary line per file", never subject to the fold
  // below (render_markdown_impl's own extra_section contract).
  std::string per_file_table =
      "## Files\n\n| Relative Path | pass | info | warn | fail | skipped | error | worst |\n"
      "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
  for (const FileBlock& block : model.files) {
    per_file_table += fmt::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n", escape_cell(block.relative_path),
                                   block.summary.pass, block.summary.info, block.summary.warn, block.summary.fail,
                                   block.summary.skipped, block.summary.error,
                                   severity_to_string(block.summary.worst_gating));
  }
  per_file_table += "\n";

  // Every file's own findings, concatenated and re-grouped through the
  // EXACT SAME build_report_model pass the single-file renderer's own
  // model came from -- reusing render_markdown_impl's fold/budget
  // machinery unchanged rather than a second, parallel implementation.
  std::vector<Finding> all_findings;
  for (const FileBlock& block : model.files) {
    all_findings.insert(all_findings.end(), block.findings.begin(), block.findings.end());
  }
  const ReportModel flattened = build_report_model(model.envelope, all_findings, registry, RenderOptions{});

  return render_markdown_impl(flattened, strict, per_file_table);
}

}  // namespace mediadiff
