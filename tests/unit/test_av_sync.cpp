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
#include <span>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "core/model.h"
#include "core/rational.h"
#include "probe/packet_scan.h"

using mediadiff::PacketRecord;
using mediadiff::PrimingResult;
using mediadiff::PtsSpan;
using mediadiff::Rational;
using mediadiff::resolve_priming;
using mediadiff::Scope;
using mediadiff::detail::primary_video_stream;
using mediadiff::detail::priming_samples_to_ticks;
using mediadiff::detail::sorted_pts_with_span;
using mediadiff::detail::span_ticks_for_basis;
using mediadiff::detail::SpanBasisCandidates;

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

// --- detail::priming_samples_to_ticks (05-14-PLAN.md Task 1, Gap 3) -------
// Every expected value below is transcribed verbatim from this task's own
// <action> item 4 list, never captured from the implementation.

TEST_CASE("av_sync - priming_samples_to_ticks(1024, 44100, {1,44100}) returns 1024 (identity timebase)", "[unit]") {
  const std::optional<std::int64_t> result = priming_samples_to_ticks(1024, 44100, Rational{1, 44100});
  REQUIRE(result.has_value());
  REQUIRE(*result == 1024);
}

TEST_CASE("av_sync - priming_samples_to_ticks(1024, 44100, {1,1000}) returns 23 (Matroska 1ms timebase)", "[unit]") {
  const std::optional<std::int64_t> result = priming_samples_to_ticks(1024, 44100, Rational{1, 1000});
  REQUIRE(result.has_value());
  REQUIRE(*result == 23);
}

TEST_CASE("av_sync - priming_samples_to_ticks(1024, 44100, {1,90000}) returns 2090 (MPEG-TS 90kHz timebase)",
          "[unit]") {
  const std::optional<std::int64_t> result = priming_samples_to_ticks(1024, 44100, Rational{1, 90000});
  REQUIRE(result.has_value());
  REQUIRE(*result == 2090);
}

TEST_CASE("av_sync - priming_samples_to_ticks(1024, 48000, {1,1000}) returns 21 (a different sample rate)",
          "[unit]") {
  const std::optional<std::int64_t> result = priming_samples_to_ticks(1024, 48000, Rational{1, 1000});
  REQUIRE(result.has_value());
  REQUIRE(*result == 21);
}

TEST_CASE("av_sync - priming_samples_to_ticks(1, 2, {1,1}) returns 1 (an exact tie rounds away from zero)",
          "[unit]") {
  const std::optional<std::int64_t> result = priming_samples_to_ticks(1, 2, Rational{1, 1});
  REQUIRE(result.has_value());
  REQUIRE(*result == 1);
}

TEST_CASE("av_sync - priming_samples_to_ticks(0, 44100, {1,1000}) returns 0", "[unit]") {
  const std::optional<std::int64_t> result = priming_samples_to_ticks(0, 44100, Rational{1, 1000});
  REQUIRE(result.has_value());
  REQUIRE(*result == 0);
}

TEST_CASE("av_sync - priming_samples_to_ticks returns nullopt when sample_rate is 0", "[unit]") {
  REQUIRE_FALSE(priming_samples_to_ticks(1024, 0, Rational{1, 1000}).has_value());
}

TEST_CASE("av_sync - priming_samples_to_ticks returns nullopt when tb.num is 0", "[unit]") {
  REQUIRE_FALSE(priming_samples_to_ticks(1024, 44100, Rational{0, 1000}).has_value());
}

TEST_CASE("av_sync - priming_samples_to_ticks returns nullopt on an overflowing product", "[unit]") {
  REQUIRE_FALSE(priming_samples_to_ticks(INT64_MAX - 1, 1, Rational{1, 1000}).has_value());
}

// --- detail::sorted_pts_with_span (05-14-PLAN.md Task 3, Gap 6, CR-01/
// WR-01) --------------------------------------------------------------
// Every expected value below is transcribed verbatim from this task's own
// <behavior> block, never captured from the implementation. The CR-01
// heap-underflow fix is specifically what the single-entry cases below
// prove: previously, a one-entry stream with a non-positive declared
// duration read 16 bytes before `entries.data()`.

TEST_CASE("av_sync - sorted_pts_with_span over 0 entries returns an empty, spanless result", "[unit]") {
  const std::vector<PacketRecord> packets;
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.pts.empty());
  REQUIRE(result.durations.empty());
  REQUIRE_FALSE(result.has_span);
  REQUIRE(result.nominal_duration_ticks == 0);
}

TEST_CASE("av_sync - sorted_pts_with_span over 1 declared entry (duration 1024) has no span", "[unit]") {
  const std::vector<PacketRecord> packets = {PacketRecord{.pts = 0, .duration = 1024}};
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.pts == std::vector<std::int64_t>{0});
  REQUIRE(result.durations == std::vector<std::int64_t>{1024});
  REQUIRE_FALSE(result.has_span);
  REQUIRE(result.nominal_duration_ticks == 1024);
}

TEST_CASE("av_sync - sorted_pts_with_span over 1 undeclared entry (duration 0) never reads out of bounds",
          "[unit]") {
  // CR-01's own regression case: exactly one valid-pts entry, non-positive
  // declared duration -- the OLD code underflowed `std::size_t neighbor`
  // to SIZE_MAX and read `entries[SIZE_MAX]`. The fixed code has no
  // neighbour to fall back to, so the effective duration is 0.
  const std::vector<PacketRecord> packets = {PacketRecord{.pts = 0, .duration = 0}};
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.pts == std::vector<std::int64_t>{0});
  REQUIRE(result.durations == std::vector<std::int64_t>{0});
  REQUIRE_FALSE(result.has_span);
  REQUIRE(result.nominal_duration_ticks == 0);
}

TEST_CASE("av_sync - sorted_pts_with_span over 1 INT64_MIN-sentinel entry treats it as 0 valid entries",
          "[unit]") {
  const std::vector<PacketRecord> packets = {PacketRecord{.pts = INT64_MIN, .duration = 1024}};
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.pts.empty());
  REQUIRE(result.durations.empty());
  REQUIRE_FALSE(result.has_span);
  REQUIRE(result.nominal_duration_ticks == 0);
}

TEST_CASE("av_sync - sorted_pts_with_span over 2 declared entries computes span 2048", "[unit]") {
  const std::vector<PacketRecord> packets = {
      PacketRecord{.pts = 0, .duration = 1024},
      PacketRecord{.pts = 1024, .duration = 1024},
  };
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.pts == (std::vector<std::int64_t>{0, 1024}));
  REQUIRE(result.durations == (std::vector<std::int64_t>{1024, 1024}));
  REQUIRE(result.has_span);
  REQUIRE(result.span_ticks == 2048);
}

TEST_CASE("av_sync - sorted_pts_with_span with the LAST entry undeclared fills it from the preceding interval",
          "[unit]") {
  const std::vector<PacketRecord> packets = {
      PacketRecord{.pts = 0, .duration = 1024},
      PacketRecord{.pts = 1024, .duration = 0},
  };
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.durations == (std::vector<std::int64_t>{1024, 1024}));
  REQUIRE(result.has_span);
  REQUIRE(result.span_ticks == 2048);
}

TEST_CASE("av_sync - sorted_pts_with_span with the FIRST entry undeclared fills it from the interval to next",
          "[unit]") {
  const std::vector<PacketRecord> packets = {
      PacketRecord{.pts = 0, .duration = 0},
      PacketRecord{.pts = 1024, .duration = 1024},
  };
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.durations == (std::vector<std::int64_t>{1024, 1024}));
  REQUIRE(result.has_span);
  REQUIRE(result.span_ticks == 2048);
}

TEST_CASE("av_sync - sorted_pts_with_span sorts unsorted input into ascending pts order", "[unit]") {
  const std::vector<PacketRecord> packets = {
      PacketRecord{.pts = 1024, .duration = 1024},
      PacketRecord{.pts = 0, .duration = 1024},
  };
  const PtsSpan result = sorted_pts_with_span(std::span<const PacketRecord>(packets));
  REQUIRE(result.pts == (std::vector<std::int64_t>{0, 1024}));
}

// --- detail::span_ticks_for_basis (06-07-PLAN.md Task 1, D-16, WINDOWS.md
// #32) -- Test 1/Test 2 from this task's own <behavior> block, transcribed
// verbatim, never captured from the implementation. A hand-built PtsSpan
// with `has_span == true` stands in for a real packet-derived span --
// `sorted_pts_with_span`'s own contract is already covered above, so these
// cases only need a PLAUSIBLE raw span to exercise the basis decision.
// -------------------------------------------------------------------------

TEST_CASE("av_sync - span_ticks_for_basis prefers the RECONSTRUCTED trimmed span when priming AND padding are "
          "both known (Test 1)",
          "[unit]") {
  // Raw (packet-derived) span 4040 ticks; priming 23 ticks, padding 17
  // ticks both known and convertible -- Test 1's "both sides' priming and
  // padding are known" case reconstructs 4040 - 23 - 17 = 4000 directly
  // from the raw extent, never from `declared_duration_ticks` (here
  // deliberately a DIFFERENT value, 4010, standing in for a container
  // field that would be the wrong answer if trusted).
  const PtsSpan pts_span{.pts = {0, 4040}, .durations = {0, 0}, .span_ticks = 4040, .has_span = true};
  const SpanBasisCandidates result = span_ticks_for_basis(4010, pts_span, 23, 17);
  REQUIRE(result.prefers_declared);
  REQUIRE(result.has_declared_span);
  REQUIRE(result.declared_span_ticks == 4000);
  REQUIRE(result.has_raw_span);
  REQUIRE(result.raw_span_ticks == 4040);
}

TEST_CASE("av_sync - span_ticks_for_basis falls back to raw when priming is unknown, even with padding known "
          "(Test 2, shared-basis rule)",
          "[unit]") {
  const PtsSpan pts_span{.pts = {0, 4040}, .durations = {0, 0}, .span_ticks = 4040, .has_span = true};
  const SpanBasisCandidates result = span_ticks_for_basis(4010, pts_span, std::nullopt, 17);
  REQUIRE_FALSE(result.prefers_declared);
  REQUIRE(result.has_raw_span);
  REQUIRE(result.raw_span_ticks == 4040);
}

TEST_CASE("av_sync - span_ticks_for_basis falls back to raw when padding is unknown, even with priming known "
          "(Test 2, shared-basis rule)",
          "[unit]") {
  const PtsSpan pts_span{.pts = {0, 4040}, .durations = {0, 0}, .span_ticks = 4040, .has_span = true};
  const SpanBasisCandidates result = span_ticks_for_basis(4010, pts_span, 23, std::nullopt);
  REQUIRE_FALSE(result.prefers_declared);
  REQUIRE(result.has_raw_span);
  REQUIRE(result.raw_span_ticks == 4040);
}

TEST_CASE("av_sync - span_ticks_for_basis falls back to the container's declared field when no reconstruction "
          "is possible (the ONLY branch a video call, which never supplies priming/padding, ever reaches)",
          "[unit]") {
  const PtsSpan pts_span{.pts = {0, 4040}, .durations = {0, 0}, .span_ticks = 4040, .has_span = true};
  const SpanBasisCandidates result = span_ticks_for_basis(4010, pts_span, std::nullopt, std::nullopt);
  REQUIRE_FALSE(result.prefers_declared);
  REQUIRE(result.has_declared_span);
  REQUIRE(result.declared_span_ticks == 4010);
}

TEST_CASE("av_sync - span_ticks_for_basis treats a genuinely known, ZERO padding count as known, not absent",
          "[unit]") {
  // The exact bug an earlier draft of this task shipped: gating the
  // padding conversion on `> 0` silently treated every known-and-zero
  // padding side (e.g. `timeline_start_base.mp4`'s own audio stream,
  // 06-06-SUMMARY.md) as "padding unknown", permanently disqualifying it
  // from ever preferring the trimmed basis.
  const PtsSpan pts_span{.pts = {0, 4023}, .durations = {0, 0}, .span_ticks = 4023, .has_span = true};
  const SpanBasisCandidates result = span_ticks_for_basis(4000, pts_span, 23, 0);
  REQUIRE(result.prefers_declared);
  REQUIRE(result.has_declared_span);
  REQUIRE(result.declared_span_ticks == 4000);
}

TEST_CASE("av_sync - span_ticks_for_basis falls back to the container field when reconstruction underflows to "
          "a non-positive span",
          "[unit]") {
  const PtsSpan pts_span{.pts = {0, 100}, .durations = {0, 0}, .span_ticks = 100, .has_span = true};
  const SpanBasisCandidates result = span_ticks_for_basis(90, pts_span, 60, 60);
  REQUIRE(result.prefers_declared);
  REQUIRE(result.has_declared_span);
  REQUIRE(result.declared_span_ticks == 90);
}
