#include "cli/tty_render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/color.h>
#include <fmt/format.h>

#include "core/serializer.h"

namespace mediadiff {

namespace {

// The Unicode symbol / ASCII word pair for each Status (doc 01 section 9:
// "--ascii swaps <checkmark><warn><cross><info> for OK WARN FAIL INFO").
// `skipped`/`error` are not named by that doc sentence -- it only lists
// the four semantics common to every comparison outcome -- but this
// renderer still needs a glyph for them, so SKIP/ERROR extend the same
// convention rather than falling back to a shared word that would make a
// skip and an error visually indistinguishable in `--ascii` mode.
std::string_view status_word(Status status, bool ascii) {
  switch (status) {
    case Status::pass:
      return ascii ? "OK" : "✓";
    case Status::info:
      return ascii ? "INFO" : "ℹ";
    case Status::warn:
      return ascii ? "WARN" : "⚠";
    case Status::fail:
      return ascii ? "FAIL" : "✗";
    case Status::skipped:
      return ascii ? "SKIP" : "○";
    case Status::error:
      return ascii ? "ERROR" : "‼";
  }
  // Unreachable for any valid Status -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return ascii ? "FAIL" : "✗";
}

fmt::color status_color(Status status) {
  switch (status) {
    case Status::pass:
      return fmt::color::green;
    case Status::info:
      return fmt::color::cyan;
    case Status::warn:
      return fmt::color::yellow;
    case Status::fail:
      return fmt::color::red;
    case Status::skipped:
      return fmt::color::gray;
    case Status::error:
      return fmt::color::magenta;
  }
  return fmt::color::red;
}

// The plain (unstyled) glyph text, used for BOTH width accounting and the
// no-colour render path, alongside the styled variant used only when
// `color.color_enabled` is true -- kept as two separate strings rather
// than measuring the styled string's own length, since an embedded ANSI
// escape sequence is invisible on a real terminal but would otherwise
// count toward this renderer's width budget, corrupting every elision
// decision downstream of it.
struct Glyph {
  std::string plain;
  std::string rendered;
};

Glyph make_glyph(Status status, const ColorDecision& color) {
  Glyph glyph;
  glyph.plain = std::string(status_word(status, color.ascii_glyphs));
  glyph.rendered =
      color.color_enabled ? fmt::format(fmt::fg(status_color(status)), "{}", glyph.plain) : glyph.plain;
  return glyph;
}

// Elides `value` to at most `max_width` bytes, appending a trailing "..."
// marker when truncation actually occurred -- REPORT-02's "elided with a
// marker rather than broken across lines" contract for the finding row's
// own value column. Byte-length, not a display-column count, matching
// src/report/markdown.cpp's own UTF-8-byte-budget convention elsewhere in
// this project (kMarkdownBudgetBytes) rather than a locale-aware display
// width this project has no library for.
std::string elide_value(std::string_view value, std::size_t max_width) {
  if (value.size() <= max_width) {
    return std::string(value);
  }
  if (max_width == 0) {
    return "";
  }
  if (max_width <= 3) {
    return std::string(value.substr(0, max_width));
  }
  return std::string(value.substr(0, max_width - 3)) + "...";
}

// Greedily word-wraps ONE paragraph (no embedded newline) to at most
// `width` bytes per line. A single token longer than `width` is hard-split
// -- rare in practice (the accept/tune/silence prose this function wraps
// is ordinary sentences), but without it one long token (a URL, an
// unbroken identifier) could exceed `width` on its own, independent of
// wrapping, which would silently break this renderer's own
// "no rendered line exceeds terminal_width" contract.
std::vector<std::string> wrap_paragraph(std::string_view para, std::size_t width) {
  if (width == 0) {
    width = 1;
  }
  std::vector<std::string> lines;
  std::string current;
  std::size_t pos = 0;
  while (pos <= para.size()) {
    const std::size_t space = para.find(' ', pos);
    std::string_view word = (space == std::string_view::npos) ? para.substr(pos) : para.substr(pos, space - pos);
    pos = (space == std::string_view::npos) ? para.size() + 1 : space + 1;

    if (word.empty()) {
      continue;
    }

    while (word.size() > width) {
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }
      lines.push_back(std::string(word.substr(0, width)));
      word = word.substr(width);
    }

    const std::size_t candidate_len = current.empty() ? word.size() : current.size() + 1 + word.size();
    if (candidate_len > width && !current.empty()) {
      lines.push_back(current);
      current.clear();
    }
    if (!current.empty()) {
      current += ' ';
    }
    current += std::string(word);
  }
  if (!current.empty() || lines.empty()) {
    lines.push_back(current);
  }
  return lines;
}

// Splits `text` on existing newlines (preserving each as its own
// paragraph -- a doc's own multi-sentence body may already carry
// intentional line breaks) and word-wraps each paragraph independently to
// `width` bytes.
std::vector<std::string> wrap_text(std::string_view text, std::size_t width) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      const std::vector<std::string> wrapped = wrap_paragraph(text.substr(start, i - start), width);
      out.insert(out.end(), wrapped.begin(), wrapped.end());
      start = i + 1;
    }
  }
  if (out.empty()) {
    out.push_back("");
  }
  return out;
}

// Appends one labelled, word-wrapped hint line ("    accept: <text>",
// continuation lines aligned under the label) to `out`. `label` plus
// ": " forms the first line's prefix; every subsequent wrapped line is
// indented to the same column so the whole block reads as one hanging
// paragraph.
void append_hint(std::string& out, std::string_view label, std::string_view text, int terminal_width) {
  const std::string prefix = "    " + std::string(label) + ": ";
  const std::size_t indent = prefix.size();
  const std::size_t budget = terminal_width > 0 ? static_cast<std::size_t>(terminal_width) : 0;
  const std::size_t width = budget > indent ? budget - indent : 1;

  const std::vector<std::string> lines = wrap_text(text, width);
  bool first = true;
  for (const std::string& line : lines) {
    if (first) {
      out += prefix + line + "\n";
      first = false;
    } else {
      out += std::string(indent, ' ') + line + "\n";
    }
  }
}

// The accept/tune/silence triple, in this fixed order, under ONE gating
// finding -- REPORT-03's own "under every gating finding individually,
// never once per group" contract. Looked up by Finding::id through
// `registry` alone (CheckDef::explain_accept/explain_tune/explain_silence,
// src/core/registry.h), which is what lets this same function work
// unchanged against either the production registry or a
// `--symbol-prefix test_` test registry -- neither of which this renderer
// ever names by its own generated CheckId enum. A Finding whose id the
// registry does not recognize (only ever a hand-built unit-test fixture
// that was never registered anywhere, per src/report/model.h's own
// sort-key comment) prints no triple: there is no compiled-in explain
// text to show for an id that was never a real check.
void append_triple(std::string& out, const Finding& finding, const CheckRegistry& registry, int terminal_width) {
  const std::optional<std::uint32_t> index = registry.find(finding.id);
  if (!index.has_value()) {
    return;
  }
  const CheckDef& def = registry.at(*index);
  append_hint(out, "accept", def.explain_accept, terminal_width);
  append_hint(out, "tune", def.explain_tune, terminal_width);
  append_hint(out, "silence", def.explain_silence, terminal_width);
}

// Word-wrapped (never elided -- this is a line of prose, not a value
// column) to `terminal_width` so a narrow terminal never receives a
// summary line wider than it asked for.
std::string render_summary_line(const Summary& summary, int terminal_width) {
  const std::string text =
      fmt::format("pass:{} info:{} warn:{} fail:{} skipped:{} error:{}  (worst: {})", summary.pass, summary.info,
                  summary.warn, summary.fail, summary.skipped, summary.error, severity_to_string(summary.worst_gating));
  const std::size_t width = terminal_width > 0 ? static_cast<std::size_t>(terminal_width) : 1;
  std::string out;
  for (const std::string& line : wrap_text(text, width)) {
    out += line + "\n";
  }
  return out;
}

// One finding's own row: status glyph, id, scope, then the (possibly
// elided) value column. Only the value column is ever elided -- id and
// scope always render in full, matching this renderer's own "no wrapping
// inside value columns, everything else stays intact" contract.
std::string render_finding_row(const Finding& finding, const ColorDecision& color, int terminal_width) {
  const Glyph glyph = make_glyph(finding.status, color);
  const std::string scope_text = scope_to_text(finding.scope);

  const std::string plain_prefix = fmt::format("  {} {} {}  ", glyph.plain, finding.id, scope_text);
  const std::string rendered_prefix = fmt::format("  {} {} {}  ", glyph.rendered, finding.id, scope_text);

  const std::string value_text = fmt::format("{} (baseline={}, candidate={})", finding.message,
                                              value_to_json(finding.baseline).dump(),
                                              value_to_json(finding.candidate).dump());

  const std::size_t prefix_len = plain_prefix.size();
  const std::size_t width_budget = terminal_width > 0 ? static_cast<std::size_t>(terminal_width) : 0;
  const std::size_t remaining = prefix_len < width_budget ? width_budget - prefix_len : 0;

  return rendered_prefix + elide_value(value_text, remaining) + "\n";
}

}  // namespace

std::string render_tty(const ReportModel& model, const CheckRegistry& registry, const ColorDecision& color,
                        int terminal_width) {
  std::string out = render_summary_line(model.summary, terminal_width);
  out += "\n";

  for (const GroupBlock& block : model.groups) {
    if (block.findings.empty()) {
      continue;
    }
    out += fmt::format("{} ({})\n", group_to_string(block.group), block.findings.size());
    for (const Finding& finding : block.findings) {
      out += render_finding_row(finding, color, terminal_width);
      if (is_gating(finding.severity)) {
        append_triple(out, finding, registry, terminal_width);
      }
    }
    out += "\n";
  }

  return out;
}

}  // namespace mediadiff
