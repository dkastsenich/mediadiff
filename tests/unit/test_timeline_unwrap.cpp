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
#include <vector>

#include "analyzers/timeline/unwrap.h"

using mediadiff::kTsPtsWrapHalfRange;
using mediadiff::kTsPtsWrapModulus;
using mediadiff::UnwrapResult;
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
