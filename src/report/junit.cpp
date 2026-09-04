#include "report/junit.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "core/serializer.h"

// T-2-33: this file deliberately does NOT call
// mediadiff::sanitize_for_display (src/util/sanitize.h) anywhere. XML
// escaping (xml_escape below) is a different context with different rules
// -- routing through the display escaper would double-escape the four XML
// metacharacters and silently change every committed JUnit golden for no
// security benefit. sanitize_for_display's own job is the terminal/
// Markdown display surface (src/cli/tty_render.cpp, src/cli/
// provenance_render.cpp, src/report/markdown.cpp), not this one.
//
// The four XML metacharacter substitutions alone do NOT make this file's
// output legal XML 1.0: a raw C0 control byte (other than tab/LF/CR) or
// DEL is not legal XML 1.0 character data under ANY escaping mechanism --
// not even an XML numeric character reference (`&#x1B;` is exactly as
// illegal as the literal byte, so emitting one would move the problem
// rather than fix it). xml_escape's default branch below therefore emits
// such a byte as a visible "\xHH" numeric escape IN THE TEXT ITSELF,
// matching the escape form src/util/sanitize.cpp's sanitize_for_display
// already uses for the same class of byte, so a reader who has seen one
// escaped control byte in this codebase recognizes the other. This is
// this file's own, XML-context-local fix for T-2-33 -- not a call into
// sanitize_for_display, which would be the wrong context entirely (see
// above).

namespace mediadiff {

namespace {

std::string_view skip_reason_text(SkipReason reason) {
  switch (reason) {
    case SkipReason::none:
      return "none";
    case SkipReason::not_applicable_container:
      return "not_applicable_container";
    case SkipReason::requires_decode:
      return "requires_decode";
    case SkipReason::cross_container:
      return "cross_container";
    case SkipReason::sampling_mismatch:
      return "sampling_mismatch";
    case SkipReason::hash_incomparable:
      return "hash_incomparable";
    case SkipReason::no_parser:
      return "no_parser";
    case SkipReason::unparsed_mechanism:
      return "unparsed_mechanism";
    case SkipReason::vfr:
      return "vfr";
    case SkipReason::requires_media:
      return "requires_media";
    case SkipReason::no_prior_release:
      return "no_prior_release";
    case SkipReason::partial_scan:
      return "partial_scan";
    case SkipReason::insufficient_data:
      return "insufficient_data";
    case SkipReason::no_timing_data:
      return "no_timing_data";
  }
  return "none";
}

constexpr char kHexDigits[] = "0123456789abcdef";

// Appends "\xHH" (lowercase hex, exactly two digits) to `out` for a byte
// that is illegal as raw XML 1.0 character data -- matches the escape
// form src/util/sanitize.cpp's sanitize_for_display uses for the same
// class of byte (see this file's own top-of-file comment for why this is
// a local fix, not a call into that function).
void append_control_byte_escape(std::string& out, unsigned char value) {
  out += "\\x";
  out += kHexDigits[(value >> 4) & 0xF];
  out += kHexDigits[value & 0xF];
}

// Escapes text for both an XML attribute value (double-quoted throughout
// this renderer) and an XML text body -- the same four metacharacter
// substitutions cover both contexts, since `"` only matters inside an
// attribute and escaping it inside a text body is harmless. Beyond the
// four metacharacters, any C0 control byte other than tab (0x09), line
// feed (0x0A) and carriage return (0x0D), plus DEL (0x7F), is not legal
// XML 1.0 character data in any form -- those bytes are escaped as a
// visible "\xHH" sequence in the text itself (see append_control_byte_escape
// and this file's own top comment for why NOT an XML numeric character
// reference). Every other byte -- ordinary printable ASCII, tab/LF/CR, and
// valid multi-byte UTF-8 -- passes through unchanged, byte-for-byte, so a
// committed golden with no such bytes is unaffected.
std::string xml_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        if (c == 0x7F || (c < 0x20 && c != '\t' && c != '\n' && c != '\r')) {
          append_control_byte_escape(out, c);
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

std::string baseline_candidate_detail(const Finding& finding) {
  // CR-02: serialize_value_compact, not nlohmann's own .dump() -- a
  // rational/real-valued finding's embedded double must be formatted by
  // the same canonical std::to_chars writer core/serializer.cpp owns
  // (D-08), not a second independent formatter. Compact (single-line)
  // rather than serialize_document because this text is embedded inline in
  // an XML <failure>/<error> element body alongside "baseline: "/" |
  // candidate: " -- serialize_document's own one-scalar-per-line layout
  // would splice a literal newline into that line.
  return fmt::format("baseline: {} | candidate: {}", serialize_value_compact(value_to_json(finding.baseline)),
                      serialize_value_compact(value_to_json(finding.candidate)));
}

enum class ElementShape { pass, failure, error, skipped };

ElementShape shape_for(const Finding& finding, bool strict) {
  switch (finding.status) {
    case Status::skipped:
      return ElementShape::skipped;
    case Status::fail:
      return ElementShape::failure;
    case Status::warn:
      return strict ? ElementShape::failure : ElementShape::skipped;
    case Status::error:
      return ElementShape::error;
    case Status::pass:
    case Status::info:
      // Status::info is only reachable here for a gating-capable finding
      // (severity warn/fail, is_gating) whose comparator nonetheless
      // classified this particular result as informational -- treated the
      // same as a clean pass: a bare <testcase> with no child element,
      // since there is no regression to report.
      return ElementShape::pass;
  }
  return ElementShape::pass;
}

struct Counts {
  std::size_t tests = 0;
  std::size_t failures = 0;
  std::size_t errors = 0;
  std::size_t skipped = 0;
};

std::string render_testcase(const Finding& finding, std::string_view classname, bool strict, Counts& counts) {
  const ElementShape shape = shape_for(finding, strict);
  ++counts.tests;

  const std::string name = fmt::format("{}[{}]", finding.id, scope_to_text(finding.scope));
  std::string out =
      fmt::format("    <testcase classname=\"{}\" name=\"{}\">", xml_escape(classname), xml_escape(name));

  switch (shape) {
    case ElementShape::pass:
      break;
    case ElementShape::failure:
      ++counts.failures;
      out += fmt::format("<failure message=\"{}\">{}</failure>", xml_escape(finding.message),
                          xml_escape(baseline_candidate_detail(finding)));
      break;
    case ElementShape::error:
      ++counts.errors;
      out += fmt::format("<error message=\"{}\">{}</error>", xml_escape(finding.message),
                          xml_escape(baseline_candidate_detail(finding)));
      break;
    case ElementShape::skipped:
      ++counts.skipped;
      out += fmt::format("<skipped message=\"{}\"/>", xml_escape(std::string(skip_reason_text(finding.skip_reason)) +
                                                                   (finding.message.empty() ? "" : ": ") +
                                                                   finding.message));
      break;
  }

  out += "</testcase>\n";
  return out;
}

}  // namespace

std::string render_junit(const ReportModel& model, const CheckRegistry& /*registry*/, bool strict) {
  Counts total;
  std::string suites_body;

  for (const GroupBlock& block : model.groups) {
    std::vector<const Finding*> gating;
    for (const Finding& finding : block.findings) {
      if (is_gating(finding.severity)) {
        gating.push_back(&finding);
      }
    }
    if (gating.empty()) {
      continue;
    }

    Counts suite;
    std::string testcases;
    const std::string classname = std::string(group_to_string(block.group));
    for (const Finding* finding : gating) {
      testcases += render_testcase(*finding, classname, strict, suite);
    }

    suites_body += fmt::format(
        "  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\">\n{}  </testsuite>\n",
        xml_escape(classname), suite.tests, suite.failures, suite.errors, suite.skipped, testcases);

    total.tests += suite.tests;
    total.failures += suite.failures;
    total.errors += suite.errors;
    total.skipped += suite.skipped;
  }

  std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out += fmt::format("<testsuites tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\">\n", total.tests,
                      total.failures, total.errors, total.skipped);
  out += suites_body;
  out += "</testsuites>\n";
  return out;
}

std::string render_junit(const CorpusModel& model, const CheckRegistry& /*registry*/, bool strict) {
  Counts total;
  std::string suites_body;

  for (const FileBlock& block : model.files) {
    std::vector<const Finding*> gating;
    for (const Finding& finding : block.findings) {
      if (is_gating(finding.severity)) {
        gating.push_back(&finding);
      }
    }
    if (gating.empty()) {
      continue;
    }

    Counts suite;
    std::string testcases;
    for (const Finding* finding : gating) {
      // `classname` stays the finding's own GROUP (container/video/.../meta),
      // matching the single-file renderer's own rule -- one file's own
      // findings can span multiple groups, so the file itself is not a
      // meaningful classname. The file identity lives on the enclosing
      // `<testsuite name=...>` below instead (this overload's own
      // "one testsuite per file" contract).
      const std::string classname = std::string(group_to_string(group_for(finding->id)));
      testcases += render_testcase(*finding, classname, strict, suite);
    }

    suites_body += fmt::format(
        "  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\">\n{}  </testsuite>\n",
        xml_escape(block.relative_path), suite.tests, suite.failures, suite.errors, suite.skipped, testcases);

    total.tests += suite.tests;
    total.failures += suite.failures;
    total.errors += suite.errors;
    total.skipped += suite.skipped;
  }

  std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out += fmt::format("<testsuites tests=\"{}\" failures=\"{}\" errors=\"{}\" skipped=\"{}\">\n", total.tests,
                      total.failures, total.errors, total.skipped);
  out += suites_body;
  out += "</testsuites>\n";
  return out;
}

}  // namespace mediadiff
