// core/exact_int.h's detail::ExactInt -- the exact 256-bit integer the
// `tol` comparator (src/compare/tol.cpp) cross-multiplies with since debug
// session test-898-ci-nonreproducible. Every expected decimal string below
// was computed independently with Python's arbitrary-precision integers
// (e.g. `python3 -c "print((2**63-1)**2)"`), never captured from what this
// implementation prints.
//
// ExactInt is ONE portable implementation (32-bit limbs, no __int128, no
// _mul128), so these tests exercise on the Linux leg exactly the
// arithmetic the MSVC leg runs.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "core/exact_int.h"

using mediadiff::detail::ExactInt;

namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

ExactInt mul(const ExactInt& a, const ExactInt& b) {
  ExactInt out;
  REQUIRE(ExactInt::try_mul(a, b, &out));
  return out;
}

ExactInt sub(const ExactInt& a, const ExactInt& b) {
  ExactInt out;
  REQUIRE(ExactInt::try_sub(a, b, &out));
  return out;
}

}  // namespace

TEST_CASE("exact_int: int64_t values render exactly as fmt renders int64_t, including both extremes",
          "[exact_int]") {
  CHECK(ExactInt::from_i64(0).to_decimal() == "0");
  CHECK(ExactInt::from_i64(1).to_decimal() == "1");
  CHECK(ExactInt::from_i64(-1).to_decimal() == "-1");
  CHECK(ExactInt::from_i64(4294967296).to_decimal() == "4294967296");  // 2^32, a limb boundary
  CHECK(ExactInt::from_i64(kInt64Max).to_decimal() == "9223372036854775807");
  CHECK(ExactInt::from_i64(kInt64Min).to_decimal() == "-9223372036854775808");
}

TEST_CASE("exact_int: int64_t x int64_t products are exact at both extremes", "[exact_int]") {
  const ExactInt max = ExactInt::from_i64(kInt64Max);
  const ExactInt min = ExactInt::from_i64(kInt64Min);
  // (2^63 - 1)^2
  CHECK(mul(max, max).to_decimal() == "85070591730234615847396907784232501249");
  // (-2^63)^2 == 2^126
  CHECK(mul(min, min).to_decimal() == "85070591730234615865843651857942052864");
  // (-2^63)^3 -- the sign of an odd count of negative factors is negative.
  CHECK(mul(mul(min, min), min).to_decimal() ==
        "-784637716923335095479473677900958302012794430558004314112");
  // Zero times a negative is zero, never "-0".
  CHECK(mul(ExactInt::from_i64(0), min).to_decimal() == "0");
  CHECK_FALSE(mul(ExactInt::from_i64(0), min).is_negative());
}

TEST_CASE("exact_int: four int64_t factors always fit; the first product past 256 bits is refused, never wrapped",
          "[exact_int]") {
  const ExactInt min = ExactInt::from_i64(kInt64Min);
  const ExactInt two_pow_252 = mul(mul(mul(min, min), min), min);  // (-2^63)^4 == 2^252
  CHECK(two_pow_252.to_decimal() ==
        "7237005577332262213973186563042994240829374041602535252466099000494570602496");

  // 15 * 2^252 < 2^256 -- the last in-range neighbor.
  CHECK(mul(two_pow_252, ExactInt::from_i64(15)).to_decimal() ==
        "108555083659983933209597798445644913612440610624038028786991485007418559037440");

  // 16 * 2^252 == 2^256 -- one past the range: refused, and *out untouched.
  ExactInt untouched = ExactInt::from_i64(7);
  CHECK_FALSE(ExactInt::try_mul(two_pow_252, ExactInt::from_i64(16), &untouched));
  CHECK(untouched.to_decimal() == "7");

  // Addition's own range edge: 2^255 + 2^255 == 2^256 is refused.
  const ExactInt two_pow_255 = mul(two_pow_252, ExactInt::from_i64(8));
  ExactInt sum;
  CHECK_FALSE(ExactInt::try_add(two_pow_255, two_pow_255, &sum));
}

TEST_CASE("exact_int: subtraction is exact across int64_t's range and normalizes zero", "[exact_int]") {
  const ExactInt max = ExactInt::from_i64(kInt64Max);
  // kMax*2 - kMax*3: both products overflow int64_t, the difference fits.
  const ExactInt difference = sub(mul(max, ExactInt::from_i64(2)), mul(max, ExactInt::from_i64(3)));
  CHECK(difference.to_decimal() == "-9223372036854775807");
  CHECK(difference.abs().to_decimal() == "9223372036854775807");

  // a - a is zero, never negative zero, for a negative a too.
  const ExactInt negative = mul(ExactInt::from_i64(kInt64Min), ExactInt::from_i64(3));
  const ExactInt zero = sub(negative, negative);
  CHECK(zero.to_decimal() == "0");
  CHECK_FALSE(zero.is_negative());
  CHECK(ExactInt::compare(zero, ExactInt::from_i64(0)) == 0);

  // Opposite signs add magnitudes: 5 - (-3) == 8, -5 - 3 == -8.
  CHECK(sub(ExactInt::from_i64(5), ExactInt::from_i64(-3)).to_decimal() == "8");
  CHECK(sub(ExactInt::from_i64(-5), ExactInt::from_i64(3)).to_decimal() == "-8");
  // Same signs subtract magnitudes, sign follows the larger: 3 - 5 == -2.
  CHECK(sub(ExactInt::from_i64(3), ExactInt::from_i64(5)).to_decimal() == "-2");
}

TEST_CASE("exact_int: compare orders signed values, including values past int64_t", "[exact_int]") {
  const ExactInt max = ExactInt::from_i64(kInt64Max);
  const ExactInt past_max = mul(max, ExactInt::from_i64(2));
  CHECK(ExactInt::compare(past_max, max) > 0);
  CHECK(ExactInt::compare(max, past_max) < 0);
  CHECK(ExactInt::compare(ExactInt::from_i64(-5), ExactInt::from_i64(-3)) < 0);
  CHECK(ExactInt::compare(ExactInt::from_i64(-3), ExactInt::from_i64(-5)) > 0);
  CHECK(ExactInt::compare(ExactInt::from_i64(-1), ExactInt::from_i64(0)) < 0);
  CHECK(ExactInt::compare(ExactInt::from_i64(0), ExactInt::from_i64(1)) < 0);
  CHECK(ExactInt::compare(past_max, mul(max, ExactInt::from_i64(2))) == 0);
  // Adjacent values past int64_t: 2*kMax vs 2*kMax + 1.
  ExactInt past_max_plus_one;
  REQUIRE(ExactInt::try_add(past_max, ExactInt::from_i64(1), &past_max_plus_one));
  CHECK(ExactInt::compare(past_max, past_max_plus_one) < 0);
  CHECK(past_max_plus_one.to_decimal() == "18446744073709551615");
}
