// core/rational.h's detail::checked_div (03-01-PLAN.md Task 3, RESEARCH.md
// Open Question 2): the two UB inputs (zero divisor, INT64_MIN / -1) must
// be rejected before any `/` executes, and every ordinary division must
// still produce the exact quotient.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "core/rational.h"

using mediadiff::detail::checked_div;

TEST_CASE("checked_div: an ordinary division returns true and the exact truncating quotient", "[rational]") {
  std::int64_t out = 0;
  REQUIRE(checked_div(10, 3, &out));
  CHECK(out == 3);

  REQUIRE(checked_div(-10, 3, &out));
  CHECK(out == -3);

  REQUIRE(checked_div(0, 5, &out));
  CHECK(out == 0);
}

TEST_CASE("checked_div: a zero divisor returns false for any dividend, never executing the division",
          "[rational]") {
  std::int64_t out = 0;
  CHECK_FALSE(checked_div(10, 0, &out));
  CHECK_FALSE(checked_div(0, 0, &out));
  CHECK_FALSE(checked_div(std::numeric_limits<std::int64_t>::min(), 0, &out));
  CHECK_FALSE(checked_div(std::numeric_limits<std::int64_t>::max(), 0, &out));
}

TEST_CASE("checked_div: INT64_MIN / -1 is the one true overflow case and is rejected, never executed",
          "[rational]") {
  std::int64_t out = 0;
  CHECK_FALSE(checked_div(std::numeric_limits<std::int64_t>::min(), -1, &out));

  // The sibling case, -1 / INT64_MIN, does NOT overflow (result is 0) --
  // only the (INT64_MIN, -1) argument order is UB. Asserted here to prove
  // checked_div's guard is precise, not an overbroad rejection of every
  // INT64_MIN-involving division.
  REQUIRE(checked_div(-1, std::numeric_limits<std::int64_t>::min(), &out));
  CHECK(out == 0);
}

TEST_CASE("checked_div: INT64_MIN divided by anything other than -1 or 0 succeeds", "[rational]") {
  std::int64_t out = 0;
  REQUIRE(checked_div(std::numeric_limits<std::int64_t>::min(), 1, &out));
  CHECK(out == std::numeric_limits<std::int64_t>::min());

  REQUIRE(checked_div(std::numeric_limits<std::int64_t>::min(), 2, &out));
  CHECK(out == std::numeric_limits<std::int64_t>::min() / 2);
}
