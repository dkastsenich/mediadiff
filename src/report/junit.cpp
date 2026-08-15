#include "report/junit.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "core/serializer.h"

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
  }
  return "none";
}

// Escapes text for both an XML attribute value (double-quoted throughout
// this renderer) and an XML text body -- the same four substitutions cover
// both contexts, since `"` only matters inside an attribute and escaping
// it inside a text body is harmless.
std::string xml_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
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
        out += c;
    }
  }
  return out;
}

std::string baseline_candidate_detail(const Finding& finding) {
  return fmt::format("baseline: {} | candidate: {}", value_to_json(finding.baseline).dump(),
                      value_to_json(finding.candidate).dump());
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

}  // namespace mediadiff
