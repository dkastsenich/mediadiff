// 03-13-PLAN.md Task 2 (CONT-05, PROBE-09; CR-01/CR-02 in 03-REVIEW.md): the
// eight behaviors of mediadiff::detail::compute_median_fragment_duration,
// driven directly against hand-built extreme-DTS arrays -- no committed
// fixture can exercise this path at all. A crafted MP4 whose adjacent
// keyframe DTS values sit at opposite ends of the int64 range (CR-01) or
// under a tick/timebase combination extreme enough to overflow a
// cross-multiplied comparator (CR-02) cannot be produced by this project's
// bitexact-only fixture discipline (D-08: every fixture is synthesized by
// `-flags +bitexact -fflags +bitexact` ffmpeg output), and media binaries
// are not committed to this repository either way. This file is
// tests/unit/test_size_windowing.cpp's sibling for the identical
// "test-only extraction point" shape (see that file's own header comment
// and src/analyzers/size/analyzers.h's detail::compute_peak_window). Every
// expected median below is a literal the test author computed by hand and
// wrote down -- never a value recomputed by calling the function under
// test a second time.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "analyzers/container/analyzers.h"
#include "core/rational.h"

using mediadiff::Rational;
using mediadiff::detail::compute_median_fragment_duration;
using mediadiff::detail::MedianDurationStatus;

// --- Test 1 (CR-01 reproduction): adjacent DTS at INT64_MIN/INT64_MAX -----

TEST_CASE("mp4_fragment_duration - CR-01 reproduction: adjacent keyframe DTS at INT64_MIN and INT64_MAX cannot "
          "determine, never a wrapped duration or a crash",
          "[unit]") {
  // The delta INT64_MAX - INT64_MIN does not fit in int64_t -- exactly the
  // signed-overflow UB CR-01 reported on a raw `a - b` subtraction. The
  // fixed seam must refuse via detail::checked_sub instead of wrapping.
  const std::vector<std::int64_t> keyframe_dts = {INT64_MIN, INT64_MAX};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::cannot_determine);
}

// --- Test 2: one overflowing pair amid otherwise-ordinary deltas ----------

TEST_CASE("mp4_fragment_duration - one adjacent pair overflowing amid otherwise-ordinary deltas cannot determine "
          "for the WHOLE computation, never a median from the subset that happened to work",
          "[unit]") {
  // Unsorted input; internal sort yields INT64_MIN, 0, 100, 200. The first
  // adjacent delta (0 - INT64_MIN) overflows int64_t; the remaining two
  // deltas (100, 100) would be perfectly ordinary on their own. A median
  // silently computed from just those two would be a fabricated answer --
  // the whole computation must refuse instead.
  const std::vector<std::int64_t> keyframe_dts = {200, INT64_MIN, 0, 100};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::cannot_determine);
}

// --- Test 3 (CR-02 reproduction): a comparator-overflowing timebase, fixed
// median still deterministic ------------------------------------------------

TEST_CASE("mp4_fragment_duration - CR-02 reproduction: a timebase extreme enough to overflow a cross-multiplied "
          "comparison still returns the hand-computed lower median deterministically",
          "[unit]") {
  // kHugeNum chosen so that `duration_value * tb.num` overflows int64_t for
  // every delta here (the smallest, 600, times kHugeNum is already ~40x
  // INT64_MAX) -- exactly the cross-multiplication compare_ticks_checked
  // performs internally (core/rational.h). Before this plan's fix, sorting
  // these durations by compare_ticks_checked would have folded every
  // comparison's overflow into "equivalent", which is not a strict weak
  // order -- calling std::stable_sort with it is undefined behavior during
  // the sort call itself (CR-02). The fixed seam orders by raw
  // std::int64_t tick value instead (same shared timebase, proven
  // positive, so tick order IS duration order), which cannot overflow
  // regardless of how large tb.num is.
  constexpr std::int64_t kHugeNum = 922337203685477581;  // INT64_MAX/10 + 1
  // Unsorted input; internal sort yields 0, 500, 1500, 3000, 3600. Deltas:
  // 500, 1000, 1500, 600 -- sorted deltas: 500, 600, 1000, 1500. Even
  // count (4): the LOWER of the two central values (600, 1000) is 600.
  const std::vector<std::int64_t> keyframe_dts = {3000, 0, 3600, 500, 1500};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{kHugeNum, 1});
  REQUIRE(result.status == MedianDurationStatus::ok);
  REQUIRE(result.median.value == 600);
  REQUIRE(result.median.tb == Rational{kHugeNum, 1});
}

// --- Test 4: a non-positive timebase numerator, and separately denominator,
// each cannot determine -----------------------------------------------------

TEST_CASE("mp4_fragment_duration - a non-positive timebase numerator cannot determine rather than ordering that "
          "silently reverses",
          "[unit]") {
  const std::vector<std::int64_t> keyframe_dts = {0, 100, 250};
  const auto zero_num = compute_median_fragment_duration(keyframe_dts, Rational{0, 1000});
  REQUIRE(zero_num.status == MedianDurationStatus::cannot_determine);
  const auto negative_num = compute_median_fragment_duration(keyframe_dts, Rational{-1, 1000});
  REQUIRE(negative_num.status == MedianDurationStatus::cannot_determine);
}

TEST_CASE("mp4_fragment_duration - a non-positive timebase denominator cannot determine rather than ordering that "
          "silently reverses",
          "[unit]") {
  const std::vector<std::int64_t> keyframe_dts = {0, 100, 250};
  const auto zero_den = compute_median_fragment_duration(keyframe_dts, Rational{1, 0});
  REQUIRE(zero_den.status == MedianDurationStatus::cannot_determine);
  const auto negative_den = compute_median_fragment_duration(keyframe_dts, Rational{1, -1000});
  REQUIRE(negative_den.status == MedianDurationStatus::cannot_determine);
}

// --- Test 5: zero values and one value each cannot determine --------------

TEST_CASE("mp4_fragment_duration - zero keyframe DTS values cannot determine", "[unit]") {
  const std::vector<std::int64_t> keyframe_dts;
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::cannot_determine);
}

TEST_CASE("mp4_fragment_duration - a single keyframe DTS value cannot determine (no delta to measure)", "[unit]") {
  const std::vector<std::int64_t> keyframe_dts = {42};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::cannot_determine);
}

// --- Test 6: odd-count and even-count ordinary cases, even returns the
// LOWER central value --------------------------------------------------------

TEST_CASE("mp4_fragment_duration - an odd count of durations returns the exact middle observed duration",
          "[unit]") {
  // Unsorted input; internal sort yields 0, 100, 300, 650. Deltas: 100,
  // 200, 350 -- three (odd) durations, already sorted ascending. Middle =
  // 200.
  const std::vector<std::int64_t> keyframe_dts = {650, 0, 300, 100};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::ok);
  REQUIRE(result.median.value == 200);
}

TEST_CASE("mp4_fragment_duration - an even count of durations returns the LOWER of the two central values, no "
          "mean, no division",
          "[unit]") {
  // Unsorted input; internal sort yields 0, 50, 150, 300, 550. Deltas: 50,
  // 100, 150, 250 -- four (even) durations, already sorted ascending. The
  // two central values are 100 and 150; the LOWER is 100 (never their mean
  // of 125).
  const std::vector<std::int64_t> keyframe_dts = {550, 0, 300, 50, 150};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::ok);
  REQUIRE(result.median.value == 100);
}

// --- Test 7: the caller's input span is not reordered by the call ---------

TEST_CASE("mp4_fragment_duration - the caller's own vector is not reordered by the call", "[unit]") {
  std::vector<std::int64_t> keyframe_dts = {300, 0, 150};
  const auto result = compute_median_fragment_duration(keyframe_dts, Rational{1, 1000});
  REQUIRE(result.status == MedianDurationStatus::ok);
  // The seam sorts a LOCAL copy internally (mirrors
  // detail::compute_peak_window's own contract); the caller's own vector
  // must be untouched, still in its original unsorted order.
  REQUIRE(keyframe_dts == std::vector<std::int64_t>{300, 0, 150});
}

// --- Test 8: shuffled input and sorted input of the same values produce
// identical results ----------------------------------------------------------

TEST_CASE("mp4_fragment_duration - shuffled input and sorted input of the same values produce identical results",
          "[unit]") {
  const std::vector<std::int64_t> sorted_dts = {0, 100, 250, 400};
  const std::vector<std::int64_t> shuffled_dts = {400, 0, 250, 100};
  const auto sorted_result = compute_median_fragment_duration(sorted_dts, Rational{1, 1000});
  const auto shuffled_result = compute_median_fragment_duration(shuffled_dts, Rational{1, 1000});
  REQUIRE(sorted_result.status == shuffled_result.status);
  REQUIRE(sorted_result.status == MedianDurationStatus::ok);
  REQUIRE(sorted_result.median.value == shuffled_result.median.value);
  REQUIRE(sorted_result.median.tb == shuffled_result.median.tb);
}
