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
using mediadiff::detail::isqrt_i64;

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

// --- isqrt (05-08-PLAN.md Task 1, TIME-05): a portable, floor-exact
//     integer square root over Int128Accum's own wide representation, and
//     its narrowed-int64_t entry point isqrt_i64. Every expected value
//     below was computed by an INDEPENDENT authority (Python's
//     math.isqrt, itself an arbitrary-precision integer square root, run
//     OUTSIDE this codebase before this file's own implementation was
//     written) -- never captured from what src/core/rational.h currently
//     produces, this project's own fail-first discipline. ------------------

// --- isqrt Test 1: perfect squares -- small, near 2^32, and a value that
//     genuinely needs the wide (>int64_t) representation to hold at all -

TEST_CASE(
    "rational_wide - isqrt of a perfect square returns exactly its root, for small values, for values near "
    "2^32, and for a wide value near the accumulator's own single-product range",
    "[unit]") {
  std::int64_t out = 0;

  REQUIRE(isqrt_i64(0, &out));
  CHECK(out == 0);

  REQUIRE(isqrt_i64(1, &out));
  CHECK(out == 1);

  REQUIRE(isqrt_i64(144, &out));
  CHECK(out == 12);

  REQUIRE(isqrt_i64(1000000, &out));
  CHECK(out == 1000);

  // 2^32 == 65536^2 -- exact, narrow (fits int64_t).
  REQUIRE(isqrt_i64(4294967296LL, &out));
  CHECK(out == 65536);

  // 65537^2 == 4295098369 -- one tick past the previous perfect square,
  // still narrow.
  REQUIRE(isqrt_i64(4295098369LL, &out));
  CHECK(out == 65537);

  // 5,000,000,000^2 == 25,000,000,000,000,000,000 -- exceeds INT64_MAX
  // (~9.223e18), so this value genuinely needs the WIDE accumulator to be
  // represented at all (try_narrow on it would return false); its floor
  // root, 5,000,000,000, is itself perfectly ordinary and narrow.
  Int128Accum wide_perfect_square;
  wide_perfect_square.add_product(5000000000LL, 5000000000LL);
  std::int64_t narrow_check = 0;
  CHECK_FALSE(wide_perfect_square.try_narrow(&narrow_check));  // confirms this input truly needs the wide path
  std::int64_t wide_out = 0;
  REQUIRE(wide_perfect_square.try_isqrt(&wide_out));
  CHECK(wide_out == 5000000000LL);
}

// --- isqrt Test 1b: a perfect square whose EXACT root does not fit
//     int64_t (INT64_MIN*INT64_MIN's own root is exactly 2^63, one more
//     than INT64_MAX) -- try_isqrt must refuse, never wrap or truncate --

TEST_CASE(
    "rational_wide - isqrt of INT64_MIN*INT64_MIN (whose exact root, 2^63, is one more than INT64_MAX) returns "
    "false rather than a wrapped or truncated root",
    "[unit]") {
  Int128Accum accum;
  accum.add_product(kInt64Min, kInt64Min);

  const std::int64_t sentinel = -13371337;
  std::int64_t out = sentinel;
  CHECK_FALSE(accum.try_isqrt(&out));
  CHECK(out == sentinel);  // never a truncated/wrapped low word
}

// --- isqrt Test 2: the floor property for non-perfect squares, asserted
//     as an inequality (r*r <= n < (r+1)*(r+1)) rather than against a
//     captured value, for a table of narrow inputs --------------------------

TEST_CASE(
    "rational_wide - isqrt of a non-perfect square returns the FLOOR of the true root, asserted directly via "
    "r*r <= n < (r+1)*(r+1) for a table of inputs",
    "[unit]") {
  const std::int64_t inputs[] = {2, 3, 5, 10, 1000000001LL, 4295098370LL};
  for (const std::int64_t n : inputs) {
    INFO("n = " << n);
    std::int64_t r = 0;
    REQUIRE(isqrt_i64(n, &r));
    CHECK(r * r <= n);
    CHECK(n < (r + 1) * (r + 1));
  }
}

// --- isqrt Test 3: zero and one -- already exercised inline in isqrt
//     Test 1 above (isqrt_i64(0, ...) == 0, isqrt_i64(1, ...) == 1);
//     restated here as its own dedicated case per the plan's own Test 3,
//     so a reader scanning TEST_CASE names alone sees this behavior
//     covered explicitly, not merely incidentally --------------------------

TEST_CASE("rational_wide - isqrt of zero is zero and of one is one", "[unit]") {
  std::int64_t out = 999;
  REQUIRE(isqrt_i64(0, &out));
  CHECK(out == 0);
  REQUIRE(isqrt_i64(1, &out));
  CHECK(out == 1);
}

// --- isqrt Test 4: narrow vs. wide call-path consistency -- the SAME
//     value, arriving via isqrt_i64 (int64_t) and via
//     Int128Accum::try_isqrt (an accumulator holding the identical value,
//     built via add()), must produce IDENTICAL results, since isqrt_i64
//     is structurally implemented AS a call to try_isqrt and cannot drift
//     from it -- this test is the regression proof of that structural
//     guarantee, not a coincidence -------------------------------------

TEST_CASE(
    "rational_wide - isqrt produces the identical result whether the input arrives as a narrowed int64_t or as "
    "the wide accumulator holding the same value, so the two call paths cannot drift",
    "[unit]") {
  const std::int64_t inputs[] = {0, 1, 2, 144, 1000000, 4294967296LL, 9223372036854775807LL};
  for (const std::int64_t n : inputs) {
    INFO("n = " << n);
    std::int64_t narrow_result = 0;
    REQUIRE(isqrt_i64(n, &narrow_result));

    Int128Accum accum;
    accum.add(n);
    std::int64_t wide_result = 0;
    REQUIRE(accum.try_isqrt(&wide_result));

    CHECK(narrow_result == wide_result);
  }
}

// --- isqrt Test 5: repeatability -- the same input produces the same
//     output across repeated calls (no internal state), the property the
//     byte-identical `--json` determinism guarantee ultimately rests on -

TEST_CASE(
    "rational_wide - isqrt is repeatable: the same input produces the same output across repeated calls, proving "
    "no internal state carries between invocations",
    "[unit]") {
  // 6,480,000,000 * 6,480,000,001 == 41,990,400,006,480,000,000 -- a wide,
  // non-perfect square (exceeds INT64_MAX).
  std::int64_t first = 0;
  std::int64_t second = 0;
  std::int64_t third = 0;

  Int128Accum accum_a;
  accum_a.add_product(6480000000LL, 6480000001LL);
  REQUIRE(accum_a.try_isqrt(&first));

  Int128Accum accum_b;
  accum_b.add_product(6480000000LL, 6480000001LL);
  REQUIRE(accum_b.try_isqrt(&second));

  Int128Accum accum_c;
  accum_c.add_product(6480000000LL, 6480000001LL);
  REQUIRE(accum_c.try_isqrt(&third));

  CHECK(first == second);
  CHECK(second == third);
  // Independently confirmed (Python's math.isqrt): floor(sqrt(41,990,400,
  // 006,480,000,000)) == 6,480,000,000.
  CHECK(first == 6480000000LL);
}
