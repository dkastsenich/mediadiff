// core/rational.h's detail::Int128Accum (05-02-PLAN.md Task 1,
// TIME-01/TIME-02): a genuine 128-bit-safe ACCUMULATOR for plan 05-09's
// av_drift least-squares sums, not merely overflow detection on a single
// multiplication (that is what checked_mul already does, tested in
// test_rational.cpp). Every expected value below is computed BY HAND
// before this file existed -- never captured from what the implementation
// currently produces (this project's own fail-first discipline).
//
// The magnitude used throughout (a == b == 648'000'000) is
// 05-RESEARCH.md's own worked example: a two-hour file at a 90 kHz
// timebase gives x_max ~= 6.48e8 ticks. 32 * 648000000^2 =
// 13,436,928,000,000,000,000, which exceeds INT64_MAX
// (9,223,372,036,854,775,807) by roughly 1.46x -- already enough to prove
// naive int64_t accumulation fails well before the full K*Sigma(x^2) term
// RESEARCH.md computes (~4.3e20, ~46x INT64_MAX) is reached.
//
// Int128Accum's own surface is deliberately minimal (add_product/add/
// try_narrow, nothing else -- rational.h's own comment), so a WIDE value
// that itself exceeds int64_t range cannot be read back directly. Where a
// test needs to prove an exact wide value, it narrows a KNOWN, hand-
// computed remainder after subtracting an int64_t-representable amount
// via `add()` -- exercising only the public API, on every toolchain.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "core/rational.h"

using mediadiff::detail::checked_add;
using mediadiff::detail::checked_mul;
using mediadiff::detail::Int128Accum;

namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

}  // namespace

// --- Test 1: 32 terms of a==b==648000000, plus the 64-bit sibling failure
//     that proves this test would not pass without the widening ----------

TEST_CASE(
    "rational_wide - add_product over thirty-two terms of 648000000*648000000 accumulates the exact "
    "hand-computed 128-bit sum, narrowed via a known int64_t-representable remainder",
    "[unit]") {
  Int128Accum accum;
  for (int i = 0; i < 32; ++i) {
    accum.add_product(648000000, 648000000);
  }

  // Hand-computed: 32 * 648000000^2 = 13,436,928,000,000,000,000.
  // Subtracting INT64_MAX (a value add() can represent) brings the result
  // back into int64_t range: 13,436,928,000,000,000,000 -
  // 9,223,372,036,854,775,807 = 4,213,555,963,145,224,193 (hand-computed
  // via long subtraction before this test was written).
  accum.add(-kInt64Max);

  std::int64_t out = 0;
  REQUIRE(accum.try_narrow(&out));
  CHECK(out == 4213555963145224193LL);
}

TEST_CASE(
    "rational_wide - the SAME thirty-two-term sum accumulated in int64_t via checked_mul/checked_add fails, "
    "proving the 128-bit widening is what makes the sibling test above pass",
    "[unit]") {
  std::int64_t sum = 0;
  bool overflowed = false;
  for (int i = 0; i < 32; ++i) {
    std::int64_t product = 0;
    REQUIRE(checked_mul(648000000, 648000000, &product));  // a single product never overflows int64_t here
    std::int64_t next_sum = 0;
    if (!checked_add(sum, product, &next_sum)) {
      overflowed = true;
      break;
    }
    sum = next_sum;
  }
  CHECK(overflowed);
}

// --- Test 2: a sum that fits in int64_t narrows successfully and matches
//     an independent int64_t accumulation ----------------------------------

TEST_CASE(
    "rational_wide - a sum whose true value fits in int64_t narrows successfully and equals an int64_t "
    "accumulation of the same terms",
    "[unit]") {
  Int128Accum accum;
  accum.add_product(1000, 2000);  // 2,000,000
  accum.add_product(-500, 300);   // -150,000
  accum.add(42);                  // +42

  std::int64_t out = 0;
  REQUIRE(accum.try_narrow(&out));

  // Hand-computed: 2,000,000 - 150,000 + 42 = 1,850,042.
  CHECK(out == 1850042);

  // Independent int64_t accumulation of the identical terms agrees.
  std::int64_t p1 = 0;
  std::int64_t p2 = 0;
  REQUIRE(checked_mul(1000, 2000, &p1));
  REQUIRE(checked_mul(-500, 300, &p2));
  std::int64_t sum = 0;
  REQUIRE(checked_add(p1, p2, &sum));
  REQUIRE(checked_add(sum, 42, &sum));
  CHECK(out == sum);
}

// --- Test 3: a sum exceeding INT64_MAX refuses to narrow, never a
//     truncated low word ---------------------------------------------------

TEST_CASE(
    "rational_wide - a sum whose true value exceeds INT64_MAX returns false from try_narrow and leaves the "
    "out-parameter untouched",
    "[unit]") {
  Int128Accum accum;
  for (int i = 0; i < 32; ++i) {
    accum.add_product(648000000, 648000000);  // sum = 13,436,928,000,000,000,000 > INT64_MAX, no offset applied
  }

  const std::int64_t sentinel = -777777777777LL;
  std::int64_t out = sentinel;
  CHECK_FALSE(accum.try_narrow(&out));
  // Never a truncated low word -- the out-parameter is left exactly as
  // the caller set it.
  CHECK(out == sentinel);
}

// --- Test 4: negative products, including a sum crossing zero from
//     positive to negative and back (the MSVC carry path's sign handling) -

TEST_CASE(
    "rational_wide - negative products accumulate correctly, including a running sum that crosses zero from "
    "positive to negative and back to positive",
    "[unit]") {
  Int128Accum accum;
  accum.add_product(1000000000000LL, 1);   // +1,000,000,000,000 -- positive
  accum.add_product(-3000000000000LL, 1);  // -3,000,000,000,000 -- running total goes negative
  accum.add_product(5000000000000LL, 1);   // +5,000,000,000,000 -- running total goes positive again

  std::int64_t out = 0;
  REQUIRE(accum.try_narrow(&out));
  // Hand-computed: 1e12 - 3e12 + 5e12 = 3e12.
  CHECK(out == 3000000000000LL);
}

// --- Test 5: INT64_MIN * INT64_MIN, the extreme single product ------------

TEST_CASE(
    "rational_wide - INT64_MIN * INT64_MIN is representable in the accumulator (its 128-bit magnitude, "
    "~8.5e37, is well inside the ~1.7e38 signed 128-bit range) and try_narrow reports false for it",
    "[unit]") {
  Int128Accum accum;
  accum.add_product(kInt64Min, kInt64Min);

  const std::int64_t sentinel = 424242;
  std::int64_t out = sentinel;
  CHECK_FALSE(accum.try_narrow(&out));
  CHECK(out == sentinel);
}

// --- Test 6: an accumulator with nothing added narrows to exactly 0 -------

TEST_CASE("rational_wide - an accumulator that has had nothing added narrows to exactly 0", "[unit]") {
  Int128Accum accum;
  std::int64_t out = 999;
  REQUIRE(accum.try_narrow(&out));
  CHECK(out == 0);
}
