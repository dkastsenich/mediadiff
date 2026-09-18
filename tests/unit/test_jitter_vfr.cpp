// 05-08-PLAN.md Task 2 (TIME-05): timeline.jitter/timeline.vfr_profile's own
// pure-function core -- detail::classify_vfr_bin's D-06 six-bucket cascade
// and detail::compute_jitter_sigma's fixed-point sigma, plus
// detail::compute_sorted_axis_intervals' own axis-sorted interval walk --
// driven directly against hand-computed expected values (mirrors
// tests/unit/test_cadence.cpp's own "no fixture on disk, hand-verified
// every expected value" discipline), never captured from what the
// implementation currently produces.
//
// 05-19-PLAN.md (UD-2, WINDOWS #28) rewrites classify_vfr_bin's on_grid/
// one_tick boundary to be quantization-aware (a deviation strictly below
// one tick of the stream's own timebase is representational rounding, not
// jitter) and re-references compute_jitter_sigma from the stream's exact
// ideal interval (ideal_num/ideal_den) rather than the timebase-bound mode
// -- every compute_jitter_sigma call below therefore takes the new
// (ideal_num, ideal_den, intervals) signature, and JitterSigmaResult's
// max_abs_deviation_fixed field is now a FIXED-POINT magnitude (scale
// 2^kJitterSigmaFixedShift), not a bare tick count.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "core/rational.h"
#include "probe/cadence.h"
#include "probe/packet_scan.h"

using mediadiff::CadenceAxis;
using mediadiff::PacketRecord;
using mediadiff::Rational;
using mediadiff::detail::classify_vfr_bin;
using mediadiff::detail::compute_jitter_sigma;
using mediadiff::detail::compute_sorted_axis_intervals;
using mediadiff::detail::JitterSigmaResult;

namespace {

// Mirrors test_cadence.cpp's own pts_only/dts_only helpers verbatim (this
// project's per-file-copy convention for hand-built PacketRecord test
// fixtures).
PacketRecord pts_only(std::int64_t pts) {
  PacketRecord record;
  record.pts = pts;
  record.dts = INT64_MIN;
  return record;
}

PacketRecord dts_only(std::int64_t dts) {
  PacketRecord record;
  record.pts = INT64_MIN;
  record.dts = dts;
  return record;
}

}  // namespace

// --- classify_vfr_bin: the D-06 six-bucket cascade, every named boundary,
// hand-computed against ideal_num=100000, ideal_den=1 (a plain-integer
// "ideal interval of 100000 ticks") -- unaffected by 05-19-PLAN.md's UD-2
// quantization rule, since every one of these ideals is already an exact
// integer number of ticks (ideal_den=1) -----------------------------------

TEST_CASE("classify_vfr_bin - exactly on the grid returns on_grid", "[unit]") {
  const std::optional<std::string> bin = classify_vfr_bin(100000, 100000, 1);
  REQUIRE(bin.has_value());
  REQUIRE(*bin == "on_grid");
}

TEST_CASE("classify_vfr_bin - within one tick (both directions) returns one_tick, boundary INCLUSIVE", "[unit]") {
  // |diff| == 1 on both sides -- the tightest non-zero bucket.
  REQUIRE(*classify_vfr_bin(100001, 100000, 1) == "one_tick");
  REQUIRE(*classify_vfr_bin(99999, 100000, 1) == "one_tick");
}

TEST_CASE("classify_vfr_bin - one_tick/one_percent boundary is exactly at |diff| == 1 versus |diff| == 2",
          "[unit]") {
  // One tick past one_tick's own inclusive boundary immediately enters
  // one_percent (D-06's own six-bucket vocabulary has no bucket between
  // the two) -- hand-computed: |diff|=2 is within the one_percent
  // threshold (|diff| <= 1000 for a 100000-tick ideal), so it lands there,
  // not in a nonexistent seventh bucket.
  REQUIRE(*classify_vfr_bin(100002, 100000, 1) == "one_percent");
  REQUIRE(*classify_vfr_bin(99998, 100000, 1) == "one_percent");
}

TEST_CASE("classify_vfr_bin - one_percent/two_x boundary, hand-computed at |diff| == 1000 versus |diff| == 1001",
          "[unit]") {
  // 1% of a 100000-tick ideal is exactly 1000 ticks -- |diff| == 1000
  // (interval 101000) sits AT that boundary and is INCLUSIVE (one_percent,
  // the tighter bucket); |diff| == 1001 (interval 101001) is one tick past
  // it and falls into two_x (the interval is genuinely longer than ideal,
  // and 101001 <= 2 * 100000).
  REQUIRE(*classify_vfr_bin(101000, 100000, 1) == "one_percent");
  REQUIRE(*classify_vfr_bin(101001, 100000, 1) == "two_x");
  // The identical boundary on the SHORT side: |diff| == 1000 (interval
  // 99000) is still one_percent; nothing past it on the short side is a
  // "two_x" of anything (an interval that short is never "longer than
  // ideal") -- it instead falls straight through to the longer catch-all,
  // proven by the dedicated short-interval test below.
  REQUIRE(*classify_vfr_bin(99000, 100000, 1) == "one_percent");
}

TEST_CASE("classify_vfr_bin - two_x/three_x boundary, hand-computed at exactly 2x versus one tick past 2x",
          "[unit]") {
  // interval == 2 * ideal_num exactly (200000) is INCLUSIVE in two_x (the
  // cascade's own `<=` comparison); one tick past it (200001) is the first
  // value to enter three_x.
  REQUIRE(*classify_vfr_bin(200000, 100000, 1) == "two_x");
  REQUIRE(*classify_vfr_bin(200001, 100000, 1) == "three_x");
}

TEST_CASE("classify_vfr_bin - three_x/longer boundary, hand-computed at exactly 3x versus one tick past 3x",
          "[unit]") {
  REQUIRE(*classify_vfr_bin(300000, 100000, 1) == "three_x");
  REQUIRE(*classify_vfr_bin(300001, 100000, 1) == "longer");
}

TEST_CASE("classify_vfr_bin - an interval shorter than the ideal by more than one percent falls through to "
          "longer, the fixed six-label vocabulary's own catch-all with no dedicated shorter bucket",
          "[unit]") {
  // 50000 is HALF the 100000-tick ideal -- nowhere near two_x/three_x
  // (which only ever apply to a LONGER interval), so this must land in
  // the longer catch-all, not silently misclassify as a lengthened
  // interval.
  REQUIRE(*classify_vfr_bin(50000, 100000, 1) == "longer");
}

TEST_CASE("classify_vfr_bin - a crafted ideal/interval pair that overflows the cross-multiplication returns "
          "std::nullopt rather than a fabricated bucket",
          "[unit]") {
  // interval * ideal_den overflows int64_t: 5e18 * 3 ~= 1.5e19, comfortably
  // past INT64_MAX (~9.22e18).
  const std::optional<std::string> bin = classify_vfr_bin(5000000000000000000LL, 100000, 3);
  REQUIRE_FALSE(bin.has_value());
}

// --- classify_vfr_bin: 05-19-PLAN.md's UD-2 quantization rule, hand-
// computed against real NON-integer ideals (a deviation strictly below one
// tick of the stream's own timebase is representational rounding -- on_grid,
// not one_tick) -----------------------------------------------------------

TEST_CASE("classify_vfr_bin - sub-tick: NTSC's own Matroska-1ms ideal (3971/119) lands both real tick values "
          "on_grid, never one_tick",
          "[unit]") {
  // Q = interval*119 - 3971. interval=33: Q = 3927-3971 = -44, |Q|=44 < 119
  // (ideal_den) -> on_grid. interval=34: Q = 4046-3971 = 75, |Q|=75 < 119
  // -> on_grid. WINDOWS.md #28's exact fixture case: both of Matroska's own
  // 33/34ms alternation values are sub-tick rounding of the true
  // 3971/119ms ideal, not real jitter.
  REQUIRE(*classify_vfr_bin(33, 3971, 119) == "on_grid");
  REQUIRE(*classify_vfr_bin(34, 3971, 119) == "on_grid");
}

TEST_CASE("classify_vfr_bin - sub-tick: one tick past NTSC's own 3971/119 ideal (both directions) is one_tick, "
          "never on_grid",
          "[unit]") {
  // interval=35: Q = 4165-3971 = 194, |Q|=194 -- den(119) <= 194 < 2*den
  // (238) -> one_tick. interval=32: Q = 3808-3971 = -163, |Q|=163 -- same
  // range -> one_tick. TIME-05/adjacency: a deviation of ONE full tick or
  // more is real, never zeroed as rounding.
  REQUIRE(*classify_vfr_bin(35, 3971, 119) == "one_tick");
  REQUIRE(*classify_vfr_bin(32, 3971, 119) == "one_tick");
}

TEST_CASE("classify_vfr_bin - sub-tick: the 90kHz AAC ideal from timeline_start_shift.ts (361534/173) lands "
          "both real tick values on_grid, one tick past lands one_tick",
          "[unit]") {
  // Q = interval*173 - 361534. interval=2090: Q = 361570-361534 = 36,
  // |Q|=36 < 173 -> on_grid. interval=2089: Q = 361397-361534 = -137,
  // |Q|=137 < 173 -> on_grid. interval=2091: Q = 361743-361534 = 209,
  // |Q|=209 -- 173 <= 209 < 346 -> one_tick.
  REQUIRE(*classify_vfr_bin(2090, 361534, 173) == "on_grid");
  REQUIRE(*classify_vfr_bin(2089, 361534, 173) == "on_grid");
  REQUIRE(*classify_vfr_bin(2091, 361534, 173) == "one_tick");
}

TEST_CASE("classify_vfr_bin - sub-tick: an EXACT INTEGER ideal (119119/119) bins IDENTICALLY to the "
          "pre-05-19-PLAN.md exact-equality rule -- on_grid only at |Q|==0, one_tick only at |Q|==den",
          "[unit]") {
  // Q = interval*119 - 119119. interval=1001: Q=0 -> on_grid (unchanged
  // from the pre-quantization rule: |Q|==0 was always on_grid). interval=
  // 1002: Q = 119238-119119 = 119 == den -> one_tick (unchanged: |Q|==den
  // was always one_tick). interval=1003: Q = 119357-119119 = 238 ==
  // 2*den -> falls past the one_tick upper bound into one_percent
  // (unchanged from the pre-quantization rule, which never had a
  // quantization exemption for an already-integer ideal).
  REQUIRE(*classify_vfr_bin(1001, 119119, 119) == "on_grid");
  REQUIRE(*classify_vfr_bin(1002, 119119, 119) == "one_tick");
  REQUIRE(*classify_vfr_bin(1003, 119119, 119) == "one_percent");
}

// --- compute_jitter_sigma: A1's fixed-point sigma, computed with ONLY
// checked-integer and Int128Accum-wide arithmetic. 05-19-PLAN.md's own
// (ideal_num, ideal_den, intervals) signature -- with ideal_den=1 this is
// algebraically identical to the pre-05-19 mode-based computation (every
// deviation is already an exact integer number of ticks), so the four
// pre-existing cases below are rewritten to the new signature with
// UNCHANGED sigma/considered_intervals expectations, only
// max_abs_deviation_ticks renamed/rescaled to max_abs_deviation_fixed
// (now a fixed-point magnitude, scale 2^kJitterSigmaFixedShift, not a
// bare tick count) and a new sub_tick_intervals assertion added --------

TEST_CASE("compute_jitter_sigma - every interval exactly matching the mode reports sigma zero and max deviation "
          "zero, and every interval counts as sub-tick",
          "[unit]") {
  const std::vector<std::int64_t> intervals = {1000, 1000, 1000, 1000};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(1000, 1, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 0);
  REQUIRE(result->max_abs_deviation_fixed == 0);
  REQUIRE(result->considered_intervals == 4);
  // Every interval sits EXACTLY on its integer ideal (|Q|==0 < den==1) --
  // UD-2's own sub-tick tally counts all four.
  REQUIRE(result->sub_tick_intervals == 4);
}

TEST_CASE("compute_jitter_sigma - a hand-computed +-1-tick perturbation reports the exact fixed-point sigma and "
          "max deviation, with the three exactly-matching intervals counted sub-tick",
          "[unit]") {
  // By hand (ideal_num=1000, ideal_den=1): deviations = [0, 0, 1, -1, 0].
  // The three ZERO deviations (|Q|==0 < den==1) are sub-tick and
  // contribute d_fixed=0 each. The +-1 deviations are NOT sub-tick
  // (|Q|==1, not < den==1): d_fixed = 1*65536 + round_half_even((1%1==0)
  // *65536/1) = 65536 exactly (no fractional remainder since ideal_den=1
  // divides evenly). Scaled deviations: [0, 0, 65536, -65536, 0]. Sum of
  // squares = 2 * 65536^2 = 8589934592. Mean (floor divide by 5) =
  // 1717986918. floor(sqrt(1717986918)): 41448^2 = 1717936704 <=
  // 1717986918 < 1718019601 = 41449^2, so the floor root is exactly
  // 41448.
  const std::vector<std::int64_t> intervals = {1000, 1000, 1001, 999, 1000};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(1000, 1, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 41448);
  REQUIRE(result->max_abs_deviation_fixed == 65536);
  REQUIRE(result->considered_intervals == 5);
  REQUIRE(result->sub_tick_intervals == 3);
}

TEST_CASE("compute_jitter_sigma - an empty interval list returns std::nullopt, never a sigma over nothing",
          "[unit]") {
  const std::vector<std::int64_t> intervals;
  REQUIRE_FALSE(compute_jitter_sigma(1000, 1, intervals).has_value());
}

TEST_CASE("compute_jitter_sigma - a crafted interval magnitude whose accumulated sum of squared scaled "
          "deviations cannot narrow to int64_t returns std::nullopt rather than a wrapped sigma (T-05-34)",
          "[unit]") {
  // deviation = 10,000,000,000 (1e10) against an ideal of 0/1: |Q| =
  // 1e10, not sub-tick (den=1). d_fixed = 1e10 * 65536 =
  // 655,360,000,000,000 (fits int64_t comfortably), but its OWN square
  // (~4.295e29) is far beyond int64_t's ~9.22e18 range -- Int128Accum
  // accumulates it losslessly (well inside its own 127-bit capacity) but
  // try_narrow must refuse, which is exactly the case this test proves.
  const std::vector<std::int64_t> intervals = {10000000000LL};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(0, 1, intervals);
  REQUIRE_FALSE(result.has_value());
}

// --- compute_jitter_sigma: 05-19-PLAN.md's UD-2 quantization rule against
// real NON-integer ideals -- sub-tick deviations contribute exactly zero,
// deviations at or past one tick enter at their FULL fixed-point
// magnitude (A1: never re-zeroed, never truncated toward the sub-tick
// boundary) -----------------------------------------------------------

TEST_CASE("compute_jitter_sigma - sub-tick: NTSC's own 3971/119 ideal against a genuine 33/34ms alternation "
          "reports sigma zero, max deviation zero, and every interval counted sub-tick",
          "[unit]") {
  // WINDOWS.md #28's own fixture shape: 60 intervals of 33, 60 of 34 --
  // both sub-tick per the classify_vfr_bin test above (|Q|=44 and |Q|=75,
  // both < den=119), so every term contributes d_fixed=0.
  std::vector<std::int64_t> intervals;
  intervals.reserve(120);
  for (int i = 0; i < 60; ++i) {
    intervals.push_back(33);
    intervals.push_back(34);
  }
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(3971, 119, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 0);
  REQUIRE(result->max_abs_deviation_fixed == 0);
  REQUIRE(result->considered_intervals == 120);
  REQUIRE(result->sub_tick_intervals == 120);
}

TEST_CASE("compute_jitter_sigma - sub-tick: an integer ideal (40/1) against a +-4-tick perturbation reports the "
          "SAME sigma the pre-05-19-PLAN.md mode-based function returned, with max deviation now expressed in "
          "fixed-point scale",
          "[unit]") {
  // By hand (ideal_num=40, ideal_den=1): deviations = [0, 0, 4, -4]. The
  // two exact 40s are sub-tick (|Q|==0 < den==1). d_fixed for the +-4
  // terms: whole_ticks=4, remainder=0 (ideal_den=1 divides evenly) ->
  // d_fixed = 4*65536 = 262144 each. Sum of squares = 2 * 262144^2 =
  // 137438953472. Mean (divide by 4) = 34359738368 = 8 * 65536^2 exactly
  // -- floor(sqrt(8) * 65536) = 185363 (hand-computed: sqrt(8) =
  // 2.8284271..., * 65536 = 185363.033..., floor 185363), the IDENTICAL
  // value the pre-05-19-PLAN.md mode-based compute_jitter_sigma(40, {40,
  // 40, 44, 36}) returned -- an integer ideal's sigma is unaffected by
  // this plan's re-referencing.
  const std::vector<std::int64_t> intervals = {40, 40, 44, 36};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(40, 1, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 185363);
  REQUIRE(result->max_abs_deviation_fixed == 4 * 65536);
  REQUIRE(result->considered_intervals == 4);
  REQUIRE(result->sub_tick_intervals == 2);
}

TEST_CASE("compute_jitter_sigma - sub-tick: NTSC's own 3971/119 ideal, one sub-tick interval and one real "
          "one-tick-plus deviation, with the round-half-even fractional term hand-computed",
          "[unit]") {
  // interval=33: Q = 3927-3971 = -44, |Q|=44 < 119 -> sub-tick, d_fixed=0.
  // interval=35: Q = 4165-3971 = 194, |Q|=194 -- NOT sub-tick (194 >=
  // 119). whole_ticks = 194 / 119 = 1, remainder = 194 % 119 = 75.
  // remainder_scaled = 75 * 65536 = 4,915,200. frac_floor = 4,915,200 /
  // 119 = 41304 (119 * 41304 = 4,915,176, frac_remainder = 24).
  // 2*frac_remainder = 48 < 119 (ideal_den) -> no rounding up, frac_
  // rounded stays 41304. d_fixed = 1*65536 + 41304 = 106840.
  //
  // sum of squares = 0^2 + 106840^2 = 11,414,785,600 (106840^2 =
  // (100000+6840)^2 = 10,000,000,000 + 1,368,000,000 + 46,785,600).
  // mean (divide by 2) = 5,707,392,800. floor(sqrt(5,707,392,800)):
  // 75547^2 = 5,707,349,209 <= 5,707,392,800 < 5,707,500,304 = 75548^2,
  // so the floor root is exactly 75547.
  const std::vector<std::int64_t> intervals = {33, 35};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(3971, 119, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 75547);
  REQUIRE(result->max_abs_deviation_fixed == 106840);
  REQUIRE(result->considered_intervals == 2);
  REQUIRE(result->sub_tick_intervals == 1);
}

// --- compute_sorted_axis_intervals: the shared axis-sorted interval walk
// both timeline.jitter and timeline.vfr_profile consume ------------------

TEST_CASE("compute_sorted_axis_intervals - sorts a shuffled read-order PTS array before walking intervals, "
          "never trusting array position",
          "[unit]") {
  // Deliberately NOT in ascending order -- packet_scan.h's own documented
  // contract is that PacketRecord arrays are in av_read_frame READ order,
  // not timestamp order. By hand, sorted PTS order is 0, 1000, 2000, 3000
  // -- three consecutive intervals of 1000 each.
  const std::vector<PacketRecord> packets = {pts_only(2000), pts_only(0), pts_only(3000), pts_only(1000)};
  const std::optional<std::vector<std::int64_t>> intervals = compute_sorted_axis_intervals(packets, CadenceAxis::pts);
  REQUIRE(intervals.has_value());
  REQUIRE(*intervals == std::vector<std::int64_t>{1000, 1000, 1000});
}

TEST_CASE("compute_sorted_axis_intervals - reads the DTS axis when CadenceAxis::dts is requested, never the PTS "
          "field",
          "[unit]") {
  std::vector<PacketRecord> packets = {dts_only(0), dts_only(500), dts_only(1500)};
  const std::optional<std::vector<std::int64_t>> intervals = compute_sorted_axis_intervals(packets, CadenceAxis::dts);
  REQUIRE(intervals.has_value());
  REQUIRE(*intervals == std::vector<std::int64_t>{500, 1000});
}

TEST_CASE("compute_sorted_axis_intervals - fewer than two usable timestamps reports an empty interval list, not "
          "an error",
          "[unit]") {
  const std::vector<PacketRecord> packets = {pts_only(0)};
  const std::optional<std::vector<std::int64_t>> intervals = compute_sorted_axis_intervals(packets, CadenceAxis::pts);
  REQUIRE(intervals.has_value());
  REQUIRE(intervals->empty());
}

TEST_CASE("compute_sorted_axis_intervals - AV_NOPTS_VALUE sentinel packets are excluded from the axis view "
          "entirely, never treated as a zero timestamp",
          "[unit]") {
  PacketRecord sentinel;
  sentinel.pts = INT64_MIN;
  sentinel.dts = INT64_MIN;
  const std::vector<PacketRecord> packets = {pts_only(0), sentinel, pts_only(1000), pts_only(2000)};
  const std::optional<std::vector<std::int64_t>> intervals = compute_sorted_axis_intervals(packets, CadenceAxis::pts);
  REQUIRE(intervals.has_value());
  REQUIRE(*intervals == std::vector<std::int64_t>{1000, 1000});
}
