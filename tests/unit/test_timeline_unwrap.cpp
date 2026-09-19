// analyzers/timeline/unwrap.h's unwrap_ts_timestamps (05-02-PLAN.md Task 2,
// TIME-02): doc 04 section 1.2's 33-bit MPEG-TS PTS/DTS unwrap rule,
// proven as a table of nine hand-verified cases -- mirroring
// tests/unit/test_ts_continuity.cpp's table-driven, state-machine-level
// shape. Every expected value below is computed BY HAND from the
// constructed tick sequence before this file existed (this project's own
// fail-first discipline) -- never captured from what the implementation
// currently produces.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "analyzers/timeline/unwrap.h"
#include "core/rational.h"
#include "probe/packet_scan.h"

using mediadiff::kTsPtsWrapHalfRange;
using mediadiff::kTsPtsWrapModulus;
using mediadiff::PacketRecord;
using mediadiff::PacketScanResult;
using mediadiff::Rational;
using mediadiff::StreamPacketScan;
using mediadiff::TimelinePacketView;
using mediadiff::UnwrapResult;
using mediadiff::make_timeline_packet_view;
using mediadiff::make_timeline_packet_views;
using mediadiff::unwrap_ts_timestamps;
using mediadiff::detail::apply_wrap_step;
using mediadiff::detail::WrapStepResult;

// --- Behavior 1: a monotonically increasing sequence well inside 33 bits
//     is returned unchanged, wrap_events == 0 --------------------------

TEST_CASE("timeline_unwrap - a monotonically increasing sequence well inside 33 bits is returned unchanged with "
          "wrap_events == 0",
          "[unit]") {
  const std::vector<std::int64_t> raw = {0, 90000, 180000, 270000};
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.unwrapped == raw);
  REQUIRE(result.wrap_events == 0);
  REQUIRE_FALSE(result.overflowed);
}

// --- Behavior 2: a step from just below 2^33-1 down to a small value (a
//     delta below -2^32) is unwrapped by adding 2^33, wrap_events == 1 --

TEST_CASE("timeline_unwrap - a backward step from near the top of the 33-bit range to a small value (delta below "
          "-2^32) is unwrapped by adding 2^33, producing a monotonically increasing output, wrap_events == 1",
          "[unit]") {
  // Hand-computed: raw = {8589934591, 1000}. delta = 1000 - 8589934591 =
  // -8589933591, which is < -kTsPtsWrapHalfRange (-4294967296) -- a wrap.
  // Unwrapped second value = 1000 + kTsPtsWrapModulus (8589934592) =
  // 8589935592.
  const std::vector<std::int64_t> raw = {8589934591, 1000};
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.wrap_events == 1);
  REQUIRE_FALSE(result.overflowed);
  REQUIRE(result.unwrapped.size() == 2);
  CHECK(result.unwrapped[0] == 8589934591);
  CHECK(result.unwrapped[1] == 8589935592);
  CHECK(result.unwrapped[0] < result.unwrapped[1]);
}

// --- Behavior 3: two successive wraps produce wrap_events == 2 and an
//     output that increases across both ---------------------------------

TEST_CASE("timeline_unwrap - two successive wraps in one stream produce wrap_events == 2 and an output that "
          "increases across both",
          "[unit]") {
  // Hand-computed sequence: raw = {8589934591, 1000, 8589934591, 1000}.
  //   delta(0->1) = 1000 - 8589934591 = -8589933591 < -half-range -> wrap #1,
  //     offset becomes 8589934592.
  //   delta(1->2) = 8589934591 - 1000 = 8589933591, which is >
  //     +kTsPtsWrapHalfRange (4294967296) -- the forward guard, NOT a wrap.
  //     offset stays 8589934592.
  //   delta(2->3) = 1000 - 8589934591 = -8589933591 < -half-range -> wrap #2,
  //     offset becomes 17179869184.
  // Unwrapped: {8589934591, 1000+8589934592=8589935592,
  //             8589934591+8589934592=17179869183, 1000+17179869184=17179870184}.
  const std::vector<std::int64_t> raw = {8589934591, 1000, 8589934591, 1000};
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.wrap_events == 2);
  REQUIRE_FALSE(result.overflowed);
  REQUIRE(result.unwrapped.size() == 4);
  CHECK(result.unwrapped[0] == 8589934591);
  CHECK(result.unwrapped[1] == 8589935592);
  CHECK(result.unwrapped[2] == 17179869183);
  CHECK(result.unwrapped[3] == 17179870184);
  CHECK(result.unwrapped[0] < result.unwrapped[1]);
  CHECK(result.unwrapped[1] < result.unwrapped[2]);
  CHECK(result.unwrapped[2] < result.unwrapped[3]);
}

// --- Behavior 4: exactly -kTsPtsWrapHalfRange is NOT a wrap (strictly
//     below only); -kTsPtsWrapHalfRange - 1 IS ---------------------------

TEST_CASE("timeline_unwrap - a backward jump of exactly -kTsPtsWrapHalfRange is NOT treated as a wrap", "[unit]") {
  // Hand-computed: raw = {5000000000, 705032704}. delta =
  // 705032704 - 5000000000 = -4294967296, exactly -kTsPtsWrapHalfRange --
  // the rule is strictly-below, so this is NOT a wrap.
  const std::vector<std::int64_t> raw = {5000000000LL, 705032704LL};
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.wrap_events == 0);
  REQUIRE_FALSE(result.overflowed);
  CHECK(result.unwrapped[0] == 5000000000LL);
  CHECK(result.unwrapped[1] == 705032704LL);
}

TEST_CASE("timeline_unwrap - a backward jump of -kTsPtsWrapHalfRange - 1 IS treated as a wrap", "[unit]") {
  // Hand-computed: raw = {5000000000, 705032703}. delta =
  // 705032703 - 5000000000 = -4294967297, one past -kTsPtsWrapHalfRange --
  // a wrap. Unwrapped second value = 705032703 + 8589934592 = 9294967295.
  const std::vector<std::int64_t> raw = {5000000000LL, 705032703LL};
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.wrap_events == 1);
  REQUIRE_FALSE(result.overflowed);
  CHECK(result.unwrapped[0] == 5000000000LL);
  CHECK(result.unwrapped[1] == 9294967295LL);
}

// --- Behavior 5: a modest backward jump is a genuine discontinuity, not a
//     wrap -- offset unchanged, wrap_events == 0, backward step preserved

TEST_CASE("timeline_unwrap - a modest backward jump (one second at 90 kHz) is a genuine discontinuity: offset "
          "unchanged, wrap_events == 0, and the output preserves the backward step",
          "[unit]") {
  const std::vector<std::int64_t> raw = {1000000, 910000};  // delta = -90000
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.wrap_events == 0);
  REQUIRE_FALSE(result.overflowed);
  CHECK(result.unwrapped[0] == 1000000);
  CHECK(result.unwrapped[1] == 910000);
  CHECK(result.unwrapped[1] < result.unwrapped[0]);  // the backward step survives, unexplained by this function
}

// --- Behavior 6: a large FORWARD jump (greater than half range) does not
//     adjust the offset in either direction ------------------------------

TEST_CASE("timeline_unwrap - a large forward jump (greater than half range) does not adjust the offset", "[unit]") {
  const std::vector<std::int64_t> raw = {0, 5000000000LL};  // delta = +5e9 > +kTsPtsWrapHalfRange
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.wrap_events == 0);
  REQUIRE_FALSE(result.overflowed);
  CHECK(result.unwrapped[0] == 0);
  CHECK(result.unwrapped[1] == 5000000000LL);
}

// --- Behavior 7: an empty span returns an empty result; a single-element
//     span returns that element unchanged --------------------------------

TEST_CASE("timeline_unwrap - an empty span returns an empty result with wrap_events == 0", "[unit]") {
  const std::vector<std::int64_t> raw;
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  CHECK(result.unwrapped.empty());
  CHECK(result.wrap_events == 0);
  CHECK_FALSE(result.overflowed);
}

TEST_CASE("timeline_unwrap - a single-element span returns that element unchanged", "[unit]") {
  const std::vector<std::int64_t> raw = {12345};
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  REQUIRE(result.unwrapped.size() == 1);
  CHECK(result.unwrapped[0] == 12345);
  CHECK(result.wrap_events == 0);
  CHECK_FALSE(result.overflowed);
}

// --- Behavior 8: repeated offset growth toward INT64_MAX eventually
//     overflows the running offset's own checked_add, and stops adjusting
//     rather than wrapping silently -- driven at the state-machine level
//     (detail::apply_wrap_step) since reaching this magnitude through
//     unwrap_ts_timestamps' own top-level loop would require ~2^30 real
//     wrap events, an input no unit test can afford in time or memory ---

TEST_CASE("timeline_unwrap - repeated offset growth toward INT64_MAX eventually overflows the running offset's "
          "own checked_add, and stops adjusting (never silently wraps) once it does",
          "[unit]") {
  // Hand-computed seed: INT64_MAX - 3*kTsPtsWrapModulus + 1 =
  // 9223372036854775807 - 25769803776 + 1 = 9223372011084972032. Two
  // further +kTsPtsWrapModulus additions stay under INT64_MAX; the third
  // would reach INT64_MAX + 1 exactly, which checked_add must refuse.
  constexpr std::int64_t kSeedOffset = 9223372011084972032LL;
  const std::int64_t wrap_trigger_delta = -(kTsPtsWrapHalfRange + 1);  // strictly below -half-range

  std::int64_t offset = kSeedOffset;

  // Step 1: succeeds -- offset grows by exactly kTsPtsWrapModulus.
  const WrapStepResult step1 = apply_wrap_step(offset, wrap_trigger_delta);
  REQUIRE(step1.wrapped);
  REQUIRE_FALSE(step1.overflowed);
  REQUIRE(step1.next_offset == offset + kTsPtsWrapModulus);
  offset = step1.next_offset;

  // Step 2: succeeds again -- proves "repeated" growth, not a one-shot.
  const WrapStepResult step2 = apply_wrap_step(offset, wrap_trigger_delta);
  REQUIRE(step2.wrapped);
  REQUIRE_FALSE(step2.overflowed);
  REQUIRE(step2.next_offset == offset + kTsPtsWrapModulus);
  offset = step2.next_offset;

  // Step 3: offset + kTsPtsWrapModulus == INT64_MAX + 1 -- overflows.
  // checked_add refuses; the offset stops adjusting (stays at step 2's
  // value) rather than silently wrapping around to a negative number.
  const WrapStepResult step3 = apply_wrap_step(offset, wrap_trigger_delta);
  REQUIRE(step3.overflowed);
  REQUIRE_FALSE(step3.wrapped);
  REQUIRE(step3.next_offset == offset);
}

// --- Behavior 9: the input span is never mutated by the call -------------

TEST_CASE("timeline_unwrap - the input span is unchanged after the call", "[unit]") {
  const std::vector<std::int64_t> raw = {8589934591, 1000, 200000000, 50};
  const std::vector<std::int64_t> raw_copy = raw;
  const UnwrapResult result = unwrap_ts_timestamps(raw);
  (void)result;
  CHECK(raw == raw_copy);
}

// =========================================================================
// TimelinePacketView (05-16-PLAN.md Task 2): zero-copy, wrap, epoch,
// overflow and copy-safety contracts. Every expected value below is
// hand-computed against unwrap_ts_timestamps' own documented rule (section
// 1.2 above), never captured from what the implementation currently
// produces.
// =========================================================================

TEST_CASE("timeline_unwrap - view: a non-TS view borrows the source packets zero-copy and reports unwrapped() false",
          "[unit]") {
  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};
  stream.packets = {
      PacketRecord{.pts = 0, .dts = 0},
      PacketRecord{.pts = 90000, .dts = 90000},
  };
  const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/false);
  REQUIRE(view.packets().size() == 2);
  CHECK(view.packets().data() == stream.packets.data());
  CHECK_FALSE(view.unwrapped());
  CHECK_FALSE(view.overflowed());
  CHECK(view.pts_wrap_events() == 0);
  CHECK(view.dts_wrap_events() == 0);
  CHECK(view.epoch_shift() == 0);
}

TEST_CASE(
    "timeline_unwrap - view: a TS view without a wrap matches the source values exactly, wrap_events 0, and owns "
    "a copy rather than borrowing",
    "[unit]") {
  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};
  stream.packets = {
      PacketRecord{.pts = 0, .dts = 0},
      PacketRecord{.pts = 90000, .dts = 90000},
      PacketRecord{.pts = 180000, .dts = 180000},
  };
  const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/true);
  REQUIRE(view.packets().size() == 3);
  CHECK(view.packets().data() != stream.packets.data());
  CHECK(view.unwrapped());
  CHECK_FALSE(view.overflowed());
  CHECK(view.pts_wrap_events() == 0);
  CHECK(view.dts_wrap_events() == 0);
  for (std::size_t i = 0; i < stream.packets.size(); ++i) {
    CHECK(view.packets()[i].pts == stream.packets[i].pts);
    CHECK(view.packets()[i].dts == stream.packets[i].dts);
  }
}

TEST_CASE(
    "timeline_unwrap - view: a TS view with one PTS wrap unwraps to 8589934000/8589934500/8589934992/8589935492, "
    "pts_wrap_events 1, dts (a separate, non-wrapping sequence) untouched",
    "[unit]") {
  // Hand-computed exactly as unwrap_ts_timestamps' own Behavior 2/3 above:
  // delta(0->1)=500 (no wrap); delta(1->2)=400-8589934500=-8589934100,
  // strictly below -kTsPtsWrapHalfRange -- a wrap, offset becomes
  // kTsPtsWrapModulus (8589934592); unwrapped[2]=400+8589934592=8589934992.
  // delta(2->3)=500 (no new wrap); unwrapped[3]=900+8589934592=8589935492.
  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};
  stream.packets = {
      PacketRecord{.pts = 8589934000, .dts = 0},
      PacketRecord{.pts = 8589934500, .dts = 1},
      PacketRecord{.pts = 400, .dts = 2},
      PacketRecord{.pts = 900, .dts = 3},
  };
  const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/true);
  REQUIRE_FALSE(view.overflowed());
  REQUIRE(view.pts_wrap_events() == 1);
  CHECK(view.dts_wrap_events() == 0);
  REQUIRE(view.packets().size() == 4);
  CHECK(view.packets()[0].pts == 8589934000);
  CHECK(view.packets()[1].pts == 8589934500);
  CHECK(view.packets()[2].pts == 8589934992);
  CHECK(view.packets()[3].pts == 8589935492);
  // The DTS axis is a plain increasing sequence -- untouched by the PTS
  // axis's own wrap.
  CHECK(view.packets()[0].dts == 0);
  CHECK(view.packets()[1].dts == 1);
  CHECK(view.packets()[2].dts == 2);
  CHECK(view.packets()[3].dts == 3);
}

TEST_CASE(
    "timeline_unwrap - view: the identical wrap sequence on the DTS axis only unwraps DTS and leaves PTS "
    "untouched, dts_wrap_events 1",
    "[unit]") {
  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};
  stream.packets = {
      PacketRecord{.pts = 0, .dts = 8589934000},
      PacketRecord{.pts = 1, .dts = 8589934500},
      PacketRecord{.pts = 2, .dts = 400},
      PacketRecord{.pts = 3, .dts = 900},
  };
  const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/true);
  REQUIRE_FALSE(view.overflowed());
  CHECK(view.pts_wrap_events() == 0);
  REQUIRE(view.dts_wrap_events() == 1);
  REQUIRE(view.packets().size() == 4);
  CHECK(view.packets()[0].pts == 0);
  CHECK(view.packets()[1].pts == 1);
  CHECK(view.packets()[2].pts == 2);
  CHECK(view.packets()[3].pts == 3);
  CHECK(view.packets()[0].dts == 8589934000);
  CHECK(view.packets()[1].dts == 8589934500);
  CHECK(view.packets()[2].dts == 8589934992);
  CHECK(view.packets()[3].dts == 8589935492);
}

TEST_CASE(
    "timeline_unwrap - view: AV_NOPTS_VALUE (INT64_MIN) PTS entries between wrapped values stay INT64_MIN at the "
    "same array indices, and the wrap is still detected across the surviving (non-sentinel) sequence",
    "[unit]") {
  // Real PTS sequence (sentinels excluded, packet_index preserved):
  // index0=8589934000, index2=8589934500, index4=400 -- the exact same
  // delta pattern as the pure-wrap test above, just with two sentinels
  // interleaved at index1/index3.
  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};
  stream.packets = {
      PacketRecord{.pts = 8589934000, .dts = 0},
      PacketRecord{.pts = INT64_MIN, .dts = 1},
      PacketRecord{.pts = 8589934500, .dts = 2},
      PacketRecord{.pts = INT64_MIN, .dts = 3},
      PacketRecord{.pts = 400, .dts = 4},
  };
  const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/true);
  REQUIRE_FALSE(view.overflowed());
  REQUIRE(view.pts_wrap_events() == 1);
  REQUIRE(view.packets().size() == 5);
  CHECK(view.packets()[0].pts == 8589934000);
  CHECK(view.packets()[1].pts == INT64_MIN);
  CHECK(view.packets()[2].pts == 8589934500);
  CHECK(view.packets()[3].pts == INT64_MIN);
  CHECK(view.packets()[4].pts == 8589934992);
}

TEST_CASE(
    "timeline_unwrap - view: the epoch rule moves a stream whose first raw PTS sits below the half-range one "
    "kTsPtsWrapModulus epoch later when the cross-stream spread exceeds the half-range; the high stream is "
    "unchanged",
    "[unit]") {
  // Hand-computed: spread = 8589933592 - 500 = 8589933092, which exceeds
  // kTsPtsWrapHalfRange (4294967296) -- the epoch rule fires. Stream 1's
  // own first raw PTS (500) sits below kTsPtsWrapHalfRange, so every
  // non-sentinel PTS/DTS of stream 1 is shifted by kTsPtsWrapModulus
  // (8589934592); stream 0's own first raw PTS (8589933592) is already at
  // or above the half-range, so stream 0 is unchanged.
  PacketScanResult scan;
  scan.per_stream.resize(2);
  scan.per_stream[0].tb = Rational{1, 90000};
  scan.per_stream[0].packets = {PacketRecord{.pts = 8589933592, .dts = 8589933592}};
  scan.per_stream[1].tb = Rational{1, 90000};
  scan.per_stream[1].packets = {PacketRecord{.pts = 500, .dts = 500}};

  const std::vector<TimelinePacketView> views = make_timeline_packet_views(scan, /*is_ts=*/true);
  REQUIRE(views.size() == 2);
  REQUIRE_FALSE(views[0].overflowed());
  REQUIRE_FALSE(views[1].overflowed());
  CHECK(views[0].epoch_shift() == 0);
  CHECK(views[1].epoch_shift() == kTsPtsWrapModulus);
  REQUIRE(views[0].packets().size() == 1);
  REQUIRE(views[1].packets().size() == 1);
  CHECK(views[0].packets()[0].pts == 8589933592);
  CHECK(views[0].packets()[0].dts == 8589933592);
  CHECK(views[1].packets()[0].pts == 500 + kTsPtsWrapModulus);
  CHECK(views[1].packets()[0].dts == 500 + kTsPtsWrapModulus);
}

TEST_CASE(
    "timeline_unwrap - view: the epoch rule is a no-op when the cross-stream spread does not exceed the "
    "half-range",
    "[unit]") {
  PacketScanResult scan;
  scan.per_stream.resize(2);
  scan.per_stream[0].tb = Rational{1, 90000};
  scan.per_stream[0].packets = {PacketRecord{.pts = 1000, .dts = 1000}};
  scan.per_stream[1].tb = Rational{1, 90000};
  scan.per_stream[1].packets = {PacketRecord{.pts = 900000, .dts = 900000}};

  const std::vector<TimelinePacketView> views = make_timeline_packet_views(scan, /*is_ts=*/true);
  REQUIRE(views.size() == 2);
  CHECK(views[0].epoch_shift() == 0);
  CHECK(views[1].epoch_shift() == 0);
  CHECK(views[0].packets()[0].pts == 1000);
  CHECK(views[1].packets()[0].pts == 900000);
}

TEST_CASE(
    "timeline_unwrap - view: the epoch rule never applies on a non-TS input -- the same large-spread values stay "
    "unshifted and zero-copy",
    "[unit]") {
  PacketScanResult scan;
  scan.per_stream.resize(2);
  scan.per_stream[0].tb = Rational{1, 90000};
  scan.per_stream[0].packets = {PacketRecord{.pts = 8589933592, .dts = 8589933592}};
  scan.per_stream[1].tb = Rational{1, 90000};
  scan.per_stream[1].packets = {PacketRecord{.pts = 500, .dts = 500}};

  const std::vector<TimelinePacketView> views = make_timeline_packet_views(scan, /*is_ts=*/false);
  REQUIRE(views.size() == 2);
  CHECK(views[0].epoch_shift() == 0);
  CHECK(views[1].epoch_shift() == 0);
  CHECK_FALSE(views[0].unwrapped());
  CHECK_FALSE(views[1].unwrapped());
  CHECK(views[0].packets().data() == scan.per_stream[0].packets.data());
  CHECK(views[1].packets().data() == scan.per_stream[1].packets.data());
  CHECK(views[1].packets()[0].pts == 500);
}

TEST_CASE(
    "timeline_unwrap - view: a wrap followed by a raw value within kTsPtsWrapModulus of INT64_MAX marks the view "
    "overflowed, matching unwrap_ts_timestamps' own overflow behavior on the identical raw sequence",
    "[unit]") {
  // Hand-computed (T-05-05's sibling case): delta(0->1)=400-8589934000=
  // -8589933600, a wrap -- offset becomes kTsPtsWrapModulus (8589934592),
  // which does not itself overflow. delta(1->2)=(INT64_MAX-100)-400, far
  // above +kTsPtsWrapHalfRange -- the forward guard, offset unchanged.
  // Applying the offset to raw[2]=(INT64_MAX-100) overflows int64_t:
  // (INT64_MAX-100)+8589934592 > INT64_MAX.
  const std::vector<std::int64_t> raw = {8589934000, 400, INT64_MAX - 100};
  const UnwrapResult direct = unwrap_ts_timestamps(raw);
  REQUIRE(direct.overflowed);

  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};
  stream.packets = {
      PacketRecord{.pts = raw[0], .dts = 0},
      PacketRecord{.pts = raw[1], .dts = 1},
      PacketRecord{.pts = raw[2], .dts = 2},
  };
  const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/true);
  CHECK(view.overflowed());
}

TEST_CASE(
    "timeline_unwrap - view: copying a TS view and destroying the original leaves the copy's own packets() "
    "reading the correct, already-unwrapped values",
    "[unit]") {
  std::optional<TimelinePacketView> copy;
  {
    StreamPacketScan stream;
    stream.tb = Rational{1, 90000};
    stream.packets = {
        PacketRecord{.pts = 8589934591, .dts = 8589934591},
        PacketRecord{.pts = 1000, .dts = 1000},
    };
    const TimelinePacketView original = make_timeline_packet_view(stream, /*is_ts=*/true);
    copy = original;
    // `stream` and `original` both go out of scope at the end of this
    // block -- `copy` must own its own storage, never a span into either.
  }
  REQUIRE(copy.has_value());
  REQUIRE_FALSE(copy->overflowed());
  REQUIRE(copy->packets().size() == 2);
  CHECK(copy->packets()[0].pts == 8589934591);
  CHECK(copy->packets()[1].pts == 8589935592);  // 1000 + kTsPtsWrapModulus, per Behavior 2 above
}

TEST_CASE("timeline_unwrap - view: an empty stream's view has an empty packets() span and does not crash",
          "[unit]") {
  StreamPacketScan stream;
  stream.tb = Rational{1, 90000};

  const TimelinePacketView ts_view = make_timeline_packet_view(stream, /*is_ts=*/true);
  CHECK(ts_view.packets().empty());
  CHECK_FALSE(ts_view.overflowed());
  CHECK(ts_view.pts_wrap_events() == 0);
  CHECK(ts_view.dts_wrap_events() == 0);

  const TimelinePacketView non_ts_view = make_timeline_packet_view(stream, /*is_ts=*/false);
  CHECK(non_ts_view.packets().empty());
  CHECK_FALSE(non_ts_view.unwrapped());
}
