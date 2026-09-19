// 05-01-PLAN.md Task 2 (TIME-01/TIME-03, D-03): timeline.start's own
// detail:: helpers, driven directly against hand-built PacketRecord arrays
// and StreamOriginCandidate lists -- no fixture on disk, mirroring
// tests/unit/test_cadence.cpp's own established shape for the identical
// "pure function over a packet array, tested at the seam" problem. Every
// expected value below is computed BY HAND from the constructed input,
// never captured from what the implementation currently produces (this
// project's own fail-first discipline).
//
// Covers this task's own <behavior> scenarios that a CLI-level fixture
// cannot reliably reach: the AV_NOPTS_VALUE skip (Test 6), the checked-
// arithmetic overflow-refuses-a-wrapped-value behavior (part of Test 3/
// Test 7's own "insufficient_data" reasoning), and the stable-tie-on-
// identical-real-time-value determinism (Test 8) -- ties never replace the
// running champion in global_origin_ticks, matching first_presented_pts'
// own strict `<` comparison.
//
// Added retroactively as a Rule 2 deviation (see 05-01-SUMMARY.md): these
// four detail:: functions were implemented and verified empirically
// against the real binary via the integration-level tracer pair first;
// this file pins that same behavior as a permanent, fixture-free
// regression test.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "core/model.h"
#include "core/rational.h"
#include "core/value.h"
#include "probe/packet_scan.h"

using mediadiff::PacketRecord;
using mediadiff::Rational;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::detail::StreamOriginCandidate;

namespace {

// A packet carrying only a PTS -- the shape first_presented_pts reads.
PacketRecord pts_only(std::int64_t pts) {
  PacketRecord record;
  record.pts = pts;
  record.dts = INT64_MIN;
  return record;
}

// A packet whose PTS is the AV_NOPTS_VALUE sentinel -- must never be
// treated as a real candidate (TIME-01).
PacketRecord no_pts() {
  PacketRecord record;
  record.pts = INT64_MIN;
  record.dts = INT64_MIN;
  return record;
}

// A packet carrying both a PTS and a declared duration -- the shape
// reconstruct_packet_durations reads (05-04-PLAN.md, TIME-03).
PacketRecord pts_and_duration(std::int64_t pts, std::int64_t duration) {
  PacketRecord record;
  record.pts = pts;
  record.dts = INT64_MIN;
  record.duration = duration;
  return record;
}

StreamOriginCandidate candidate(Scope::Kind kind, int index, std::int64_t pts_ticks, Rational tb) {
  StreamOriginCandidate c;
  c.scope = Scope{kind, index};
  c.first_pts_ticks = pts_ticks;
  c.tb = tb;
  return c;
}

}  // namespace

// --- first_presented_pts ----------------------------------------------------

TEST_CASE("timeline_start_duration - first_presented_pts returns the minimum non-sentinel PTS, in array order, "
          "ignoring AV_NOPTS_VALUE entirely") {
  const std::vector<PacketRecord> packets = {pts_only(500), no_pts(), pts_only(100), pts_only(300)};
  const auto result = mediadiff::detail::first_presented_pts(packets);
  REQUIRE(result.has_value());
  REQUIRE(*result == 100);
}

TEST_CASE("timeline_start_duration - first_presented_pts returns std::nullopt when every packet carries the "
          "AV_NOPTS_VALUE sentinel (Test 6's own no_timing_data trigger)") {
  const std::vector<PacketRecord> packets = {no_pts(), no_pts()};
  REQUIRE_FALSE(mediadiff::detail::first_presented_pts(packets).has_value());
}

TEST_CASE("timeline_start_duration - first_presented_pts returns std::nullopt on an empty packet array") {
  const std::vector<PacketRecord> packets;
  REQUIRE_FALSE(mediadiff::detail::first_presented_pts(packets).has_value());
}

TEST_CASE("timeline_start_duration - first_presented_pts never coerces the AV_NOPTS_VALUE sentinel to 0 -- a "
          "negative real PTS still wins over a sentinel-only companion stream") {
  // A negative PTS is a legitimate presentation-order value (e.g. B-frame
  // reordering against a stream-relative origin) -- the sentinel
  // (INT64_MIN) must never be mistaken for "very early" and win instead.
  const std::vector<PacketRecord> packets = {no_pts(), pts_only(-5)};
  const auto result = mediadiff::detail::first_presented_pts(packets);
  REQUIRE(result.has_value());
  REQUIRE(*result == -5);
}

// --- global_origin_ticks -----------------------------------------------------

TEST_CASE("timeline_start_duration - global_origin_ticks picks the candidate with the smallest real-time value "
          "across two different timebases, not the smallest raw tick value") {
  // video: 10 ticks @ 1/25 = 400 ms. audio: 22050 ticks @ 1/44100 = 500 ms.
  // video's real-time value is earlier despite neither raw tick value nor
  // array position alone determining the answer.
  const std::vector<StreamOriginCandidate> candidates = {
      candidate(Scope::Kind::audio, 0, 22050, Rational{1, 44100}),
      candidate(Scope::Kind::video, 0, 10, Rational{1, 25}),
  };
  const auto result = mediadiff::detail::global_origin_ticks(candidates);
  REQUIRE(result.has_value());
  REQUIRE(result->scope.kind == Scope::Kind::video);
  REQUIRE(result->first_pts_ticks == 10);
}

TEST_CASE("timeline_start_duration - global_origin_ticks never replaces the running champion on a genuine tie -- "
          "the FIRST candidate in array order wins, matching first_presented_pts' own strict '<' discipline "
          "(Test 8: byte-identical across repeated runs)") {
  // Both candidates represent exactly 1000 ms in real time, via two
  // different timebases -- a genuine tie, not an ordering decision.
  const std::vector<StreamOriginCandidate> candidates = {
      candidate(Scope::Kind::video, 0, 1000, Rational{1, 1000}),
      candidate(Scope::Kind::audio, 0, 44100, Rational{1, 44100}),
  };
  const auto result = mediadiff::detail::global_origin_ticks(candidates);
  REQUIRE(result.has_value());
  // The FIRST candidate (video) must win the tie, never the second.
  REQUIRE(result->scope.kind == Scope::Kind::video);
}

TEST_CASE("timeline_start_duration - global_origin_ticks returns std::nullopt on an empty candidate list") {
  const std::vector<StreamOriginCandidate> candidates;
  REQUIRE_FALSE(mediadiff::detail::global_origin_ticks(candidates).has_value());
}

TEST_CASE("timeline_start_duration - global_origin_ticks returns std::nullopt (never a wrapped or fabricated "
          "origin) when the cross-multiplication comparison overflows int64_t") {
  // The second candidate's own value*tb.num already overflows int64_t
  // inside compare_ticks_checked's cross-multiplication.
  const std::vector<StreamOriginCandidate> candidates = {
      candidate(Scope::Kind::video, 0, 1, Rational{1, 1}),
      candidate(Scope::Kind::audio, 0, INT64_MAX, Rational{2, 1}),
  };
  REQUIRE_FALSE(mediadiff::detail::global_origin_ticks(candidates).has_value());
}

// --- ticks_to_ms --------------------------------------------------------------

TEST_CASE("timeline_start_duration - ticks_to_ms converts native ticks to a millisecond RationalValue via checked "
          "multiply-then-divide, matching container.ts.pcr_interval's own bytes_to_ms convention") {
  // 25 ticks @ 1/25 s/tick == exactly 1 second == 1000 ms.
  const auto result = mediadiff::detail::ticks_to_ms(25, Rational{1, 25});
  REQUIRE(result.has_value());
  REQUIRE(*result == RationalValue{1000, 1, Rational{1, 1}});
}

TEST_CASE("timeline_start_duration - ticks_to_ms truncates toward zero on a non-unit-numerator timebase (NTSC "
          "30000/1001), never rounding, never a floating-point type") {
  // 1 tick @ 1001/30000 s/tick == 1000 * 1001 / 30000 ms == 33.3666... ->
  // truncated to 33, hand-computed.
  const auto result = mediadiff::detail::ticks_to_ms(1, Rational{1001, 30000});
  REQUIRE(result.has_value());
  REQUIRE(*result == RationalValue{33, 1, Rational{1, 1}});
}

TEST_CASE("timeline_start_duration - ticks_to_ms returns std::nullopt (never a wrapped value) when the checked "
          "multiplication overflows int64_t") {
  const auto result = mediadiff::detail::ticks_to_ms(INT64_MAX, Rational{1, 1});
  REQUIRE_FALSE(result.has_value());
}

// --- subtract_ms ---------------------------------------------------------------

TEST_CASE("timeline_start_duration - subtract_ms computes a - b via cross-multiplication, matching "
          "src/compare/tol.cpp's own delta_num/delta_den construction, never a float") {
  const RationalValue a{500, 1, Rational{1, 1}};
  const RationalValue b{400, 1, Rational{1, 1}};
  const auto result = mediadiff::detail::subtract_ms(a, b);
  REQUIRE(result.has_value());
  REQUIRE(*result == RationalValue{100, 1, Rational{1, 1}});
}

TEST_CASE("timeline_start_duration - subtract_ms returns exactly zero for a value subtracted from itself -- the "
          "stream that owns the global origin reports a relative value of zero (Test 3)") {
  const RationalValue a{400, 1, Rational{1, 1}};
  const auto result = mediadiff::detail::subtract_ms(a, a);
  REQUIRE(result.has_value());
  REQUIRE(*result == RationalValue{0, 1, Rational{1, 1}});
}

TEST_CASE("timeline_start_duration - subtract_ms returns std::nullopt (never a wrapped value) when the "
          "denominator cross-multiplication overflows int64_t") {
  const RationalValue a{1, INT64_MAX, Rational{1, 1}};
  const RationalValue b{1, 2, Rational{1, 1}};
  const auto result = mediadiff::detail::subtract_ms(a, b);
  REQUIRE_FALSE(result.has_value());
}

// --- Combined: the full D-03 pipeline over hand-built input, matching this
// task's own Test 1/Test 2/Test 3 exactly --------------------------------

TEST_CASE("timeline_start_duration - the full global-origin-then-per-stream-relative pipeline (Test 1/Test 2/Test "
          "3): the global origin is the minimum real-time value across streams, and each stream's relative value "
          "is its own first PTS minus that origin") {
  // video: first PTS 10 ticks @ 1/25 == 400 ms.
  // audio: first PTS 22050 ticks @ 1/44100 == 500 ms.
  // video owns the earlier real time, so it is the global origin.
  const std::vector<StreamOriginCandidate> candidates = {
      candidate(Scope::Kind::video, 0, 10, Rational{1, 25}),
      candidate(Scope::Kind::audio, 0, 22050, Rational{1, 44100}),
  };
  const auto origin = mediadiff::detail::global_origin_ticks(candidates);
  REQUIRE(origin.has_value());
  REQUIRE(origin->scope.kind == Scope::Kind::video);

  const auto origin_ms = mediadiff::detail::ticks_to_ms(origin->first_pts_ticks, origin->tb);
  REQUIRE(origin_ms.has_value());
  REQUIRE(*origin_ms == RationalValue{400, 1, Rational{1, 1}});

  for (const StreamOriginCandidate& c : candidates) {
    const auto stream_ms = mediadiff::detail::ticks_to_ms(c.first_pts_ticks, c.tb);
    REQUIRE(stream_ms.has_value());
    const auto relative = mediadiff::detail::subtract_ms(*stream_ms, *origin_ms);
    REQUIRE(relative.has_value());
    if (c.scope.kind == Scope::Kind::video) {
      // The origin-owning stream's relative value is exactly zero.
      REQUIRE(*relative == RationalValue{0, 1, Rational{1, 1}});
    } else {
      REQUIRE(*relative == RationalValue{100, 1, Rational{1, 1}});
    }
  }
}

// --- reconstruct_packet_durations (05-04-PLAN.md Task 1, TIME-01/TIME-03,
// doc 04 section 1.3) ---------------------------------------------------

TEST_CASE("timeline_start_duration - reconstruct_packet_durations keeps every declared (>0) duration verbatim and "
          "never marks any entry reconstructed (Test 3's 'declared' arm)") {
  const std::vector<PacketRecord> packets = {pts_and_duration(0, 10), pts_and_duration(10, 10),
                                              pts_and_duration(20, 10)};
  const auto result = mediadiff::detail::reconstruct_packet_durations(packets, Rational{1, 1});
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 3);
  for (const auto& entry : *result) {
    REQUIRE_FALSE(entry.reconstructed);
    REQUIRE(entry.duration_ticks == 10);
  }
}

TEST_CASE("timeline_start_duration - reconstruct_packet_durations substitutes a NON-last packet's missing duration "
          "with the delta to the NEXT pts in presentation order (doc 04 section 1.3)") {
  // Presentation order: 0(declared 10), 10(missing), 25(declared 5). The
  // middle packet's own reconstructed duration is 25 - 10 == 15, never its
  // own declared field (0) and never the mode interval (this is NOT the
  // last packet).
  const std::vector<PacketRecord> packets = {pts_and_duration(0, 10), pts_and_duration(10, 0),
                                              pts_and_duration(25, 5)};
  const auto result = mediadiff::detail::reconstruct_packet_durations(packets, Rational{1, 1});
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 3);
  REQUIRE_FALSE((*result)[0].reconstructed);
  REQUIRE((*result)[0].duration_ticks == 10);
  REQUIRE((*result)[1].reconstructed);
  REQUIRE((*result)[1].duration_ticks == 15);
  REQUIRE_FALSE((*result)[2].reconstructed);
  REQUIRE((*result)[2].duration_ticks == 5);
}

TEST_CASE("timeline_start_duration - reconstruct_packet_durations substitutes the LAST packet's missing duration "
          "with the shared derive_cadence's own mode interval -- NOT zero and NOT the previous frame's own delta "
          "(Test 4)") {
  // Presentation order: 0,10,20,30 (three 10-tick intervals, the mode) then
  // 44 (a 14-tick interval, the PREVIOUS frame's own delta into the last
  // packet) with the LAST packet's own declared duration missing. The mode
  // interval (10) must win, never 14 (the previous delta) and never 0.
  const std::vector<PacketRecord> packets = {pts_and_duration(0, 10), pts_and_duration(10, 10),
                                              pts_and_duration(20, 10), pts_and_duration(30, 10),
                                              pts_and_duration(44, 0)};
  const auto result = mediadiff::detail::reconstruct_packet_durations(packets, Rational{1, 1});
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 5);
  const auto& last = result->back();
  REQUIRE(last.pts_ticks == 44);
  REQUIRE(last.reconstructed);
  REQUIRE(last.duration_ticks == 10);
  REQUIRE(last.duration_ticks != 0);
  REQUIRE(last.duration_ticks != 14);
}

TEST_CASE("timeline_start_duration - reconstruct_packet_durations returns entries in PRESENTATION order (sorted by "
          "pts), not the input span's own read order") {
  const std::vector<PacketRecord> packets = {pts_and_duration(20, 10), pts_and_duration(0, 10),
                                              pts_and_duration(10, 10)};
  const auto result = mediadiff::detail::reconstruct_packet_durations(packets, Rational{1, 1});
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 3);
  REQUIRE((*result)[0].pts_ticks == 0);
  REQUIRE((*result)[1].pts_ticks == 10);
  REQUIRE((*result)[2].pts_ticks == 20);
}

TEST_CASE("timeline_start_duration - reconstruct_packet_durations returns std::nullopt when no packet carries a "
          "valid pts at all (Test 6's own no_timing_data trigger)") {
  const std::vector<PacketRecord> packets = {no_pts(), no_pts()};
  REQUIRE_FALSE(mediadiff::detail::reconstruct_packet_durations(packets, Rational{1, 1}).has_value());
}

TEST_CASE("timeline_start_duration - reconstruct_packet_durations returns std::nullopt (never a wrapped value) "
          "when a non-last packet's own delta-to-next-pts overflows int64_t") {
  // INT64_MIN + 1, not INT64_MIN itself -- a valid (non-AV_NOPTS_VALUE)
  // extreme PTS, so this genuinely exercises checked_sub's own overflow
  // path rather than first_presented_pts' sentinel filter.
  const std::vector<PacketRecord> packets = {pts_and_duration(INT64_MIN + 1, 0), pts_and_duration(INT64_MAX, 10)};
  REQUIRE_FALSE(mediadiff::detail::reconstruct_packet_durations(packets, Rational{1, 1}).has_value());
}
