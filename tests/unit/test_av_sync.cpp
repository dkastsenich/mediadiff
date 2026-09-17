// 05-09-PLAN.md Task 2 (TIME-06/TIME-09/TIME-10, D-09): resolve_priming's
// own pure-function core, driven directly against plain integers -- every
// expected value below is transcribed from this task's own <behavior>
// block (Test 1-4), which is itself the roster's approved resolver
// ordering, never captured from the implementation. Also covers
// detail::primary_video_stream's first-video-scoped-stream selection over
// hand-built scope vectors, no fixture on disk (mirrors
// test_timeline_start_duration.cpp's own established shape for this class
// of problem).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "core/model.h"

using mediadiff::PrimingResult;
using mediadiff::resolve_priming;
using mediadiff::Scope;
using mediadiff::detail::primary_video_stream;

// --- Test 1: MP4's own case -- skip_samples present and nonzero,
// initial_padding zero -----------------------------------------------------
TEST_CASE("av_sync - resolve_priming(skip_samples=1024, initial_padding=0) returns source skip_samples with 1024",
          "[unit]") {
  const PrimingResult result = resolve_priming(1024, 0);
  REQUIRE(result.source == PrimingResult::Source::skip_samples);
  REQUIRE(result.samples == 1024);
}

// --- Test 2: Matroska's own case -- BOTH populated, packet-level still
// wins ------------------------------------------------------------------
TEST_CASE("av_sync - resolve_priming(skip_samples=1024, initial_padding=1024) still returns source skip_samples",
          "[unit]") {
  const PrimingResult result = resolve_priming(1024, 1024);
  REQUIRE(result.source == PrimingResult::Source::skip_samples);
  REQUIRE(result.samples == 1024);
}

// --- Test 3: only the codecpar-level field carries a value -----------------
TEST_CASE("av_sync - resolve_priming(skip_samples=0/absent, initial_padding=1024) returns source initial_padding",
          "[unit]") {
  const PrimingResult result = resolve_priming(0, 1024);
  REQUIRE(result.source == PrimingResult::Source::initial_padding);
  REQUIRE(result.samples == 1024);
}

// --- Test 4: MPEG-TS's own case -- neither signal present ------------------
TEST_CASE("av_sync - resolve_priming(skip_samples=0/absent, initial_padding=0) returns source unknown with zero "
          "samples",
          "[unit]") {
  const PrimingResult result = resolve_priming(0, 0);
  REQUIRE(result.source == PrimingResult::Source::unknown);
  REQUIRE(result.samples == 0);
}

// --- detail::primary_video_stream -------------------------------------

TEST_CASE("av_sync - primary_video_stream returns the first video-scoped stream's own array index", "[unit]") {
  const std::vector<std::optional<Scope>> scopes = {
      Scope{Scope::Kind::audio, 0},
      Scope{Scope::Kind::video, 0},
      Scope{Scope::Kind::video, 1},
  };
  const std::optional<std::size_t> result = primary_video_stream(scopes);
  REQUIRE(result.has_value());
  REQUIRE(*result == 1);
}

TEST_CASE("av_sync - primary_video_stream returns nullopt when no stream is video-scoped", "[unit]") {
  const std::vector<std::optional<Scope>> scopes = {
      Scope{Scope::Kind::audio, 0},
      std::nullopt,
      Scope{Scope::Kind::subtitle, 0},
  };
  REQUIRE_FALSE(primary_video_stream(scopes).has_value());
}

TEST_CASE("av_sync - primary_video_stream returns nullopt over an empty scope list", "[unit]") {
  const std::vector<std::optional<Scope>> scopes;
  REQUIRE_FALSE(primary_video_stream(scopes).has_value());
}
