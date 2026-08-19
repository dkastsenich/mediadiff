// 02-09-PLAN.md Task 2: the TTY renderer -- fixed group order, only
// non-pass by default, the accept/tune/silence triple under every gating
// finding, colour/ascii gating, and no wrapped value column.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cli/color_policy.h"
#include "cli/tty_render.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "report/model.h"
#include "support/golden.h"
#include "test/test_check_id.h"

namespace {

using mediadiff::Absent;
using mediadiff::ColorDecision;
using mediadiff::Envelope;
using mediadiff::Finding;
using mediadiff::RenderOptions;
using mediadiff::ReportModel;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;

Finding make_finding(std::string_view id, Status status, Severity severity, std::string message,
                      Scope scope = Scope{Scope::Kind::global, 0}) {
  Finding f;
  f.id = id;
  f.scope = scope;
  f.status = status;
  f.severity = severity;
  f.baseline = mediadiff::Value{std::string("before")};
  f.candidate = mediadiff::Value{std::string("after")};
  f.message = std::move(message);
  f.skip_reason = SkipReason::none;
  return f;
}

ReportModel model_from(const std::vector<Finding>& findings, const RenderOptions& options = RenderOptions{}) {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  env.schema_version = "1.0";
  env.tool_version = "test";
  return mediadiff::build_report_model(env, findings, registry, options);
}

const ColorDecision kNoColor{/*color_enabled=*/false, /*ascii_glyphs=*/false};

std::size_t count_occurrences(const std::string& haystack, std::string_view needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

TEST_CASE("tty - groups render in the fixed container/video/timeline/audio/content/size/meta order", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  // Deliberately constructed out of order -- meta first, container last --
  // so a passing test proves the renderer imposes its own order rather
  // than merely preserving encounter order.
  const ReportModel model = model_from({
      make_finding("t.exact_string", Status::fail, Severity::fail, "meta finding"),
      make_finding("size.rate", Status::fail, Severity::fail, "size finding"),
      make_finding("content.frame", Status::fail, Severity::fail, "content finding"),
      make_finding("audio.level", Status::fail, Severity::fail, "audio finding"),
      make_finding("timeline.drift", Status::fail, Severity::fail, "timeline finding"),
      make_finding("video.color", Status::fail, Severity::fail, "video finding"),
      make_finding("container.moov", Status::fail, Severity::fail, "container finding"),
  });
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);

  const std::size_t container_pos = rendered.find("container (");
  const std::size_t video_pos = rendered.find("video (");
  const std::size_t timeline_pos = rendered.find("timeline (");
  const std::size_t audio_pos = rendered.find("audio (");
  const std::size_t content_pos = rendered.find("content (");
  const std::size_t size_pos = rendered.find("size (");
  const std::size_t meta_pos = rendered.find("meta (");

  REQUIRE(container_pos != std::string::npos);
  REQUIRE(video_pos != std::string::npos);
  REQUIRE(timeline_pos != std::string::npos);
  REQUIRE(audio_pos != std::string::npos);
  REQUIRE(content_pos != std::string::npos);
  REQUIRE(size_pos != std::string::npos);
  REQUIRE(meta_pos != std::string::npos);

  CHECK(container_pos < video_pos);
  CHECK(video_pos < timeline_pos);
  CHECK(timeline_pos < audio_pos);
  CHECK(audio_pos < content_pos);
  CHECK(content_pos < size_pos);
  CHECK(size_pos < meta_pos);
}

TEST_CASE("tty - a pass finding is hidden without -v and shown with it", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const std::vector<Finding> findings = {
      make_finding("video.clean", Status::pass, Severity::fail, "no change"),
  };

  const ReportModel hidden_model = model_from(findings, RenderOptions{/*show_pass=*/false,
                                                                       /*show_ignored=*/false,
                                                                       /*ascii=*/false,
                                                                       /*strict=*/false});
  const std::string hidden_rendered = mediadiff::render_tty(hidden_model, registry, kNoColor, 100);
  CHECK(hidden_rendered.find("video.clean") == std::string::npos);

  const ReportModel shown_model = model_from(findings, RenderOptions{/*show_pass=*/true,
                                                                      /*show_ignored=*/true,
                                                                      /*ascii=*/false,
                                                                      /*strict=*/false});
  const std::string shown_rendered = mediadiff::render_tty(shown_model, registry, kNoColor, 100);
  CHECK(shown_rendered.find("video.clean") != std::string::npos);
}

TEST_CASE("tty - a gating finding prints its accept/tune/silence triple and a non-gating one prints none", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("t.exact_string", Status::fail, Severity::fail, "gating finding", Scope{Scope::Kind::global, 0}),
      make_finding("t.tol_ms", Status::info, Severity::ignore, "non-gating finding", Scope{Scope::Kind::global, 1}),
  });
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);

  CHECK(count_occurrences(rendered, "accept:") == 1);
  CHECK(count_occurrences(rendered, "tune:") == 1);
  CHECK(count_occurrences(rendered, "silence:") == 1);
}

TEST_CASE("tty - two adjacent gating findings each print their own triple", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("t.exact_string", Status::fail, Severity::fail, "first gating finding",
                    Scope{Scope::Kind::global, 0}),
      make_finding("t.tol_ms", Status::warn, Severity::warn, "second gating finding", Scope{Scope::Kind::global, 1}),
  });
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);

  CHECK(count_occurrences(rendered, "accept:") == 2);
  CHECK(count_occurrences(rendered, "tune:") == 2);
  CHECK(count_occurrences(rendered, "silence:") == 2);
}

TEST_CASE("tty - the triple's three labels appear in accept, tune, silence order", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model =
      model_from({make_finding("t.exact_string", Status::fail, Severity::fail, "gating finding")});
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);

  const std::size_t accept_pos = rendered.find("accept:");
  const std::size_t tune_pos = rendered.find("tune:");
  const std::size_t silence_pos = rendered.find("silence:");
  REQUIRE(accept_pos != std::string::npos);
  REQUIRE(tune_pos != std::string::npos);
  REQUIRE(silence_pos != std::string::npos);
  CHECK(accept_pos < tune_pos);
  CHECK(tune_pos < silence_pos);
}

TEST_CASE("tty - a check whose tune sub-section states none applies still prints the tune label", "[tty]") {
  // t.exact_string's own docs/checks doc (tests/support/docs/t.exact_string.md)
  // carries "Not applicable -- `exact` has no tolerance." under its own
  // "### Tune" heading -- the tune label must still render even though the
  // check has nothing to tune.
  const auto& registry = mediadiff::test_registry();
  const ReportModel model =
      model_from({make_finding("t.exact_string", Status::fail, Severity::fail, "gating finding")});
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);

  CHECK(rendered.find("tune:") != std::string::npos);
  CHECK(rendered.find("Not applicable") != std::string::npos);
}

TEST_CASE("tty - a value wider than its column is elided rather than wrapped, and no line exceeds the given width",
          "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const std::string long_message(500, 'x');
  const ReportModel model =
      model_from({make_finding("t.exact_string", Status::fail, Severity::fail, long_message)});
  constexpr int kWidth = 40;
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, kWidth);

  std::size_t start = 0;
  for (std::size_t i = 0; i <= rendered.size(); ++i) {
    if (i == rendered.size() || rendered[i] == '\n') {
      const std::size_t line_length = i - start;
      CHECK(line_length <= static_cast<std::size_t>(kWidth));
      start = i + 1;
    }
  }
  // The long message itself never appears intact -- it was elided.
  CHECK(rendered.find(long_message) == std::string::npos);
}

TEST_CASE("tty - with color_enabled false the output contains no ESC byte", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("video.a", Status::fail, Severity::fail, "a"),
      make_finding("t.exact_string", Status::warn, Severity::warn, "b"),
  });
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);
  CHECK(rendered.find('\x1B') == std::string::npos);
}

TEST_CASE("tty - color_enabled true emits an ESC byte for the status glyph", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({make_finding("video.a", Status::fail, Severity::fail, "a")});
  const ColorDecision color{/*color_enabled=*/true, /*ascii_glyphs=*/false};
  const std::string rendered = mediadiff::render_tty(model, registry, color, 100);
  CHECK(rendered.find('\x1B') != std::string::npos);
}

TEST_CASE("tty - with ascii_glyphs true the output uses ASCII status words and no symbol glyphs", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("video.a", Status::pass, Severity::ignore, "pass finding"),
      make_finding("video.b", Status::info, Severity::ignore, "info finding"),
      make_finding("video.c", Status::warn, Severity::warn, "warn finding"),
      make_finding("video.d", Status::fail, Severity::fail, "fail finding"),
  });
  const ColorDecision ascii_color{/*color_enabled=*/false, /*ascii_glyphs=*/true};
  const std::string rendered = mediadiff::render_tty(model, registry, ascii_color, 100);

  CHECK(rendered.find("OK") != std::string::npos);
  CHECK(rendered.find("INFO") != std::string::npos);
  CHECK(rendered.find("WARN") != std::string::npos);
  CHECK(rendered.find("FAIL") != std::string::npos);
  CHECK(rendered.find("✓") == std::string::npos);  // checkmark
  CHECK(rendered.find("ℹ") == std::string::npos);  // info
  CHECK(rendered.find("⚠") == std::string::npos);  // warning
  CHECK(rendered.find("✗") == std::string::npos);  // cross
}

TEST_CASE("tty - golden: a fixed model at a fixed width matches the recorded golden", "[tty]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("video.color", Status::fail, Severity::fail, "range flipped from limited to full",
                    Scope{Scope::Kind::video, 0}),
      make_finding("t.exact_string", Status::warn, Severity::warn, "tool version skew"),
      make_finding("audio.level", Status::skipped, Severity::fail, "cannot compare across sampling rates",
                    Scope{Scope::Kind::audio, 0}),
  });
  const std::string rendered = mediadiff::render_tty(model, registry, kNoColor, 100);
  mediadiff::test::check_golden("tty_basic", rendered);
}
