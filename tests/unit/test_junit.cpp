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

// CR-03 / T-2-33 (plan 03-15) -- xml_escape's default branch now escapes
// any C0 control byte (other than tab/LF/CR) and DEL as a visible "\xHH"
// sequence rather than passing it through, closing the gap
// 03-VERIFICATION.md's Anti-Patterns table recorded. Behaviors 1-6 below.

namespace {

// Counts bytes below 0x20 in `text` other than tab (0x09), newline (0x0A)
// and carriage return (0x0D) -- the illegal-as-raw-XML-1.0 C0 subset this
// plan's own acceptance criteria checks with
// `LC_ALL=C grep -c $'[\x01-\x08\x0b\x0c\x0e-\x1f]'`.
std::size_t count_illegal_c0_bytes(const std::string& text) {
  std::size_t count = 0;
  for (unsigned char c : text) {
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
      ++count;
    }
  }
  return count;
}

}  // namespace

TEST_CASE("junit - a raw ESC byte in a finding message is escaped, not passed through", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const std::string message_with_esc = std::string("before\x1b" "after");
  const ReportModel model =
      model_from({make_finding("video.esc_check", Status::fail, Severity::fail, message_with_esc)});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  // Test 1: no byte below 0x20 other than tab/newline/CR anywhere in the
  // emitted document.
  CHECK(count_illegal_c0_bytes(rendered) == 0);
  // The escaped form is visible in the message="..." attribute rather
  // than the byte silently vanishing.
  CHECK(rendered.find("message=\"before\\x1bafter\"") != std::string::npos);
  CHECK(rendered.find(message_with_esc) == std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - a control byte is escaped whether it lands in an attribute or an element body",
          "[junit]") {
  const auto& registry = mediadiff::test_registry();
  // finding.message renders into the <failure message="..."> ATTRIBUTE;
  // finding.candidate renders (via baseline_candidate_detail) into the
  // <failure>...</failure> element BODY -- xml_escape is the same
  // function serving both call sites, so a control byte surviving either
  // path would be this same bug reappearing in the other context.
  Finding f = make_finding("video.esc_both_contexts", Status::fail, Severity::fail, "attr\x1b" "context");
  f.candidate = mediadiff::Value{std::string("body\x1b" "context")};
  const ReportModel model = model_from({f});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(count_illegal_c0_bytes(rendered) == 0);
  // The attribute-context escape is visible directly.
  CHECK(rendered.find("message=\"attr\\x1bcontext\"") != std::string::npos);
  // The element-body text (baseline_candidate_detail) carries the
  // candidate value; whether it was JSON-pre-escaped upstream or escaped
  // by xml_escape's own default branch, no raw ESC byte reaches the body.
  CHECK(rendered.find("body\x1b" "context") == std::string::npos);
  CHECK(rendered.find("candidate:") != std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - a NUL byte in Finding::candidate is escaped, not truncating the output", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  Finding f = make_finding("video.nul_check", Status::fail, Severity::fail, "nul in candidate");
  // std::string carries an embedded NUL correctly (size-tracked, not
  // C-string length); this proves the pipeline (JSON escaping,
  // fmt::format, xml_escape and string concatenation) never truncates at
  // that byte the way a naive C-string-based writer would.
  f.candidate = mediadiff::Value{std::string("before\0after", 12)};
  const ReportModel model = model_from({f});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(count_illegal_c0_bytes(rendered) == 0);
  CHECK(rendered.find("before") != std::string::npos);
  // The text after the embedded NUL is present -- nothing was truncated.
  CHECK(rendered.find("after") != std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - the four XML metacharacters still escape exactly as before; ordinary ASCII and UTF-8 pass "
          "through byte-for-byte",
          "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("video.metachar_check", Status::fail, Severity::fail, "a <b> & c \"d\" e'f"),
      make_finding("video.utf8_check", Status::fail, Severity::fail, "caf\xC3\xA9 \xE2\x9C\x93"),
  });
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(rendered.find("a &lt;b&gt; &amp; c &quot;d&quot; e'f") != std::string::npos);
  CHECK(rendered.find("a <b>") == std::string::npos);
  // Valid multi-byte UTF-8 (e-acute, a checkmark) passes through
  // byte-for-byte -- no C1-range escaping applies here (that is
  // sanitize_for_display's job at the display boundary, not this XML
  // renderer's).
  CHECK(rendered.find("caf\xC3\xA9 \xE2\x9C\x93") != std::string::npos);
  CHECK(xml_reparses_cleanly(rendered));
}

TEST_CASE("junit - tab, newline and carriage return are preserved, not escaped", "[junit]") {
  const auto& registry = mediadiff::test_registry();
  const std::string message_with_whitespace = std::string("a\tb\nc\rd");
  const ReportModel model =
      model_from({make_finding("video.whitespace_check", Status::fail, Severity::fail, message_with_whitespace)});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  // Legal XML 1.0 character data -- preserved verbatim, not turned into
  // "\x09"/"\x0a"/"\x0d" (which would alter the existing committed golden's
  // formatting for any message that happens to embed one).
  CHECK(rendered.find(message_with_whitespace) != std::string::npos);
  CHECK(rendered.find("\\x09") == std::string::npos);
  CHECK(rendered.find("\\x0a") == std::string::npos);
  CHECK(rendered.find("\\x0d") == std::string::npos);
}

TEST_CASE("junit - a document with a control byte in a crafted message still re-parses cleanly as XML",
          "[junit]") {
  // Test 6: this project has no XML-parsing library in vcpkg.json (see
  // xml_reparses_cleanly's own comment above), so "the emitted document
  // parses as XML" is proven with this file's dependency-free
  // stack-based tag-balance/entity scanner rather than a full spec
  // parser -- it already asserts every '<'/'&' in the text is either a
  // legal entity or a legal tag delimiter, which is exactly what a
  // control-byte escape must not disturb.
  const auto& registry = mediadiff::test_registry();
  const std::string crafted = std::string("\x00\x01\x02\x1b\x7f", 5) + "<script>&";
  const ReportModel model =
      model_from({make_finding("video.crafted_check", Status::fail, Severity::fail, crafted)});
  const std::string rendered = mediadiff::render_junit(model, registry, false);

  CHECK(count_illegal_c0_bytes(rendered) == 0);
  CHECK(xml_reparses_cleanly(rendered));
}
