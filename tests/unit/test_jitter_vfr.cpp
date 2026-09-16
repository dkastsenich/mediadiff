// 05-08-PLAN.md Task 2 (TIME-05): timeline.jitter/timeline.vfr_profile's own
// pure-function core -- detail::classify_vfr_bin's D-06 six-bucket cascade
// and detail::compute_jitter_sigma's fixed-point sigma, plus
// detail::compute_sorted_axis_intervals' own axis-sorted interval walk --
// driven directly against hand-computed expected values (mirrors
// tests/unit/test_cadence.cpp's own "no fixture on disk, hand-verified
// every expected value" discipline), never captured from what the
// implementation currently produces.

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
// "ideal interval of 100000 ticks") ------------------------------------

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

// --- compute_jitter_sigma: A1's fixed-point sigma, computed with ONLY
// checked-integer and Int128Accum-wide arithmetic ------------------------

TEST_CASE("compute_jitter_sigma - every interval exactly matching the mode reports sigma zero and max deviation "
          "zero",
          "[unit]") {
  const std::vector<std::int64_t> intervals = {1000, 1000, 1000, 1000};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(1000, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 0);
  REQUIRE(result->max_abs_deviation_ticks == 0);
  REQUIRE(result->considered_intervals == 4);
}

TEST_CASE("compute_jitter_sigma - a hand-computed +-1-tick perturbation reports the exact fixed-point sigma and "
          "max deviation",
          "[unit]") {
  // By hand (mode=1000): deviations = [0, 0, 1, -1, 0]. Scaled by 2^16 =
  // 65536: [0, 0, 65536, -65536, 0]. Sum of squares =
  // 2 * 65536^2 = 8589934592. Mean (floor divide by 5) = 1717986918.
  // floor(sqrt(1717986918)): 41448^2 = 1717936704 <= 1717986918 <
  // 1718019601 = 41449^2, so the floor root is exactly 41448.
  const std::vector<std::int64_t> intervals = {1000, 1000, 1001, 999, 1000};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(1000, intervals);
  REQUIRE(result.has_value());
  REQUIRE(result->sigma_fixed_numerator == 41448);
  REQUIRE(result->max_abs_deviation_ticks == 1);
  REQUIRE(result->considered_intervals == 5);
}

TEST_CASE("compute_jitter_sigma - an empty interval list returns std::nullopt, never a sigma over nothing",
          "[unit]") {
  const std::vector<std::int64_t> intervals;
  REQUIRE_FALSE(compute_jitter_sigma(1000, intervals).has_value());
}

TEST_CASE("compute_jitter_sigma - a crafted interval magnitude whose accumulated sum of squared scaled "
          "deviations cannot narrow to int64_t returns std::nullopt rather than a wrapped sigma (T-05-34)",
          "[unit]") {
  // deviation = 10,000,000,000 (1e10) against a mode of 0: deviation_scaled
  // = 1e10 * 65536 = 655,360,000,000,000 (fits int64_t comfortably), but
  // its OWN square (~4.295e29) is far beyond int64_t's ~9.22e18 range --
  // Int128Accum accumulates it losslessly (well inside its own 127-bit
  // capacity) but try_narrow must refuse, which is exactly the case this
  // test proves.
  const std::vector<std::int64_t> intervals = {10000000000LL};
  const std::optional<JitterSigmaResult> result = compute_jitter_sigma(0, intervals);
  REQUIRE_FALSE(result.has_value());
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
