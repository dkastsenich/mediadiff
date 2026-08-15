// 02-08-PLAN.md Task 3: JUnit XML -- gating-capable selection, element
// shape (pass/failure/error/skipped), --strict-sensitive warn handling,
// escaping and honest tests/failures/errors/skipped counts.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "report/junit.h"
#include "report/model.h"
#include "support/golden.h"
#include "test/test_check_id.h"

namespace {

using mediadiff::Absent;
using mediadiff::Envelope;
using mediadiff::Finding;
using mediadiff::RenderOptions;
using mediadiff::ReportModel;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;

Finding make_finding(std::string_view id, Status status, Severity severity, std::string message,
                      Scope scope = Scope{Scope::Kind::global, 0}, SkipReason skip_reason = SkipReason::none) {
  Finding f;
  f.id = id;
  f.scope = scope;
  f.status = status;
  f.severity = severity;
  f.baseline = mediadiff::Value{Absent{}};
  f.candidate = mediadiff::Value{Absent{}};
  f.message = std::move(message);
  f.skip_reason = skip_reason;
  return f;
}

ReportModel model_from(const std::vector<Finding>& findings) {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  env.schema_version = "1.0";
  env.tool_version = "test";
  return mediadiff::build_report_model(env, findings, registry, RenderOptions{});
}

// A lightweight, dependency-free XML well-formedness scanner: this project
// has no XML parsing library (vcpkg.json carries none), so "re-parse the
// output as XML" is proven with a stack-based tag-balance and
// entity-only-ampersand check instead of a full spec parser -- enough to
// prove render_junit's own escaping actually produces a document that
// parses cleanly, and to FAIL on a raw, unescaped '<'/'&' the way a real
// XML parser would.
bool xml_reparses_cleanly(const std::string& xml) {
  std::vector<std::string> stack;
  std::size_t i = 0;
  static const std::vector<std::string> entities = {"amp;", "lt;", "gt;", "quot;", "apos;"};
  while (i < xml.size()) {
    const char c = xml[i];
    if (c == '&') {
      bool matched = false;
      for (const auto& entity : entities) {
        if (xml.compare(i + 1, entity.size(), entity) == 0) {
          matched = true;
          i += 1 + entity.size();
          break;
        }
      }
      if (!matched) {
        return false;
      }
      continue;
    }
    if (c == '<') {
      const std::size_t close = xml.find('>', i);
      if (close == std::string::npos) {
        return false;
      }
      std::string tag = xml.substr(i + 1, close - i - 1);
      i = close + 1;
      if (!tag.empty() && tag.front() == '?') {
        continue;  // <?xml ... ?>
      }
      const bool closing = !tag.empty() && tag.front() == '/';
      const bool self_closing = !tag.empty() && tag.back() == '/';
      if (closing) {
        tag.erase(0, 1);
      }
      if (self_closing) {
        tag.pop_back();
      }
      const std::size_t space = tag.find_first_of(" \t\n");
      const std::string name = space == std::string::npos ? tag : tag.substr(0, space);
      if (closing) {
        if (stack.empty() || stack.back() != name) {
          return false;
        }
        stack.pop_back();
      } else if (!self_closing) {
        stack.push_back(name);
      }
      continue;
    }
    ++i;
  }
  return stack.empty();
}

}  // namespace

TEST_CASE("junit - two findings for one check at two scopes produce two testcases with distinct names",
          "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("video.probe", Status::fail, Severity::fail, "mismatch a", Scope{Scope::Kind::video, 0}),
      make_finding("video.probe", Status::fail, Severity::fail, "mismatch b", Scope{Scope::Kind::video, 1}),
  });
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("name=\"video.probe[video[0]]\"") != std::string::npos);
  CHECK(rendered.find("name=\"video.probe[video[1]]\"") != std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - a run with zero gating-capable findings emits a well-formed document declaring zero tests",
          "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({make_finding("video.clean", Status::pass, Severity::ignore, "no change")});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("tests=\"0\"") != std::string::npos);
  // "<testsuite " (a child element, trailing space before its own
  // attributes) rather than "<testsuite" alone, which would also match the
  // root "<testsuites " element's own opening tag.
  CHECK(rendered.find("<testsuite ") == std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - a warn finding is a failure under --strict and a skipped element otherwise", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({make_finding("video.warn_check", Status::warn, Severity::warn, "drifted")});

  const std::string strict_rendered = mediadiff::render_junit(model, registry, true);
  CHECK(strict_rendered.find("<failure") != std::string::npos);
  CHECK(strict_rendered.find("<skipped") == std::string::npos);

  const std::string lax_rendered = mediadiff::render_junit(model, registry, false);
  CHECK(lax_rendered.find("<skipped") != std::string::npos);
  CHECK(lax_rendered.find("<failure") == std::string::npos);
}

TEST_CASE("junit - a Status::skipped finding renders <skipped> carrying its reason, never a bare testcase",
          "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({make_finding("video.hash_check", Status::skipped, Severity::fail,
                                                       "cannot compare", Scope{Scope::Kind::video, 0},
                                                       SkipReason::hash_incomparable)});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("<skipped") != std::string::npos);
  CHECK(rendered.find("hash_incomparable") != std::string::npos);
  CHECK(rendered.find("<failure") == std::string::npos);
}

TEST_CASE("junit - a finding message containing <, > and & is escaped and re-parses cleanly", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model =
      model_from({make_finding("video.escape_check", Status::fail, Severity::fail, "a <b> & c \"d\"")});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("a &lt;b&gt; &amp; c") != std::string::npos);
  CHECK(rendered.find("a <b>") == std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - a Status::error finding renders as an <error> element", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model =
      model_from({make_finding("video.mismatch_check", Status::error, Severity::fail, "value_kind mismatch")});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("<error") != std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - a passing gating-capable finding renders a bare testcase with no child element", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model =
      model_from({make_finding("video.within_tolerance", Status::pass, Severity::fail, "within tolerance")});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("<testcase") != std::string::npos);
  CHECK(rendered.find("<failure") == std::string::npos);
  CHECK(rendered.find("<skipped") == std::string::npos);
  CHECK(rendered.find("<error") == std::string::npos);
  CHECK(rendered.find("tests=\"1\"") != std::string::npos);
}

TEST_CASE("junit - golden: suites appear in group order and testcases in registry order", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  // t.exact_string is registered before t.tol_ms (tests/support/test_checks.def)
  // -- both map to Group::meta -- constructed here in reverse, plus one
  // video-group finding so two <testsuite> elements exist, in group order
  // (video before meta, per kGroupOrder).
  const ReportModel model = model_from({
      make_finding("t.tol_ms", Status::fail, Severity::fail, "meta b"),
      make_finding("t.exact_string", Status::warn, Severity::warn, "meta a"),
      make_finding("video.frame_hash", Status::fail, Severity::fail, "video finding"),
  });
  const std::string rendered = mediadiff::render_junit(model, registry, false);
  CHECK(xml_reparses_cleanly(rendered));
  mediadiff::test::check_golden("junit_basic", rendered);
}
