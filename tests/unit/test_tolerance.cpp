// core/tolerance.cpp's grammar parser (02-04-PLAN.md Task 1, ENG-05,
// CLI-10): table-driven over every valid suffix, both sign spellings, an
// integer and a fractional magnitude, the two-threshold form, and the
// rejection cases doc 01 section 3 and this plan's own action text list.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "core/error.h"
#include "core/registry.h"
#include "core/tolerance.h"

using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::parse_tolerance;
using mediadiff::Tolerance;
using mediadiff::Unit;

TEST_CASE("tolerance: every valid suffix parses with the expected unit and integer magnitude", "[tolerance]") {
  struct Case {
    const char* text;
    Unit unit;
    std::int64_t num;
    std::int64_t den;
    bool is_relative;
  };
  const Case cases[] = {
      {"5ms", Unit::ms, 5, 1, false},
      {"3%", Unit::percent, 3, 1, true},
      {"2frames", Unit::frames, 2, 1, false},
      {"0.5LU", Unit::lu, 5, 10, false},
      {"1.0dB", Unit::db, 10, 10, false},
      {"128samples", Unit::samples, 128, 1, false},
      {"1tick", Unit::ticks, 1, 1, false},
      {"64bytes", Unit::count, 64, 1, false},
      {"64", Unit::count, 64, 1, false},  // bare form, doc 01's "±8"
  };
  for (const Case& c : cases) {
    INFO("text: " << c.text);
    auto result = parse_tolerance(c.text, c.unit);
    REQUIRE(result.has_value());
    CHECK(result->unit == c.unit);
    CHECK(result->num == c.num);
    CHECK(result->den == c.den);
    CHECK(result->is_relative == c.is_relative);
    CHECK_FALSE(result->warn_num.has_value());
  }
}

TEST_CASE("tolerance: the plus-minus prefix is accepted in both spellings and does not change the value",
          "[tolerance]") {
  auto ascii = parse_tolerance("+-8", Unit::count);
  REQUIRE(ascii.has_value());
  CHECK(ascii->num == 8);
  CHECK(ascii->den == 1);

  auto unicode = parse_tolerance("\xC2\xB1" "8", Unit::count);  // U+00B1
  REQUIRE(unicode.has_value());
  CHECK(unicode->num == 8);
  CHECK(unicode->den == 1);
}

TEST_CASE("tolerance: a fractional magnitude produces the exact rational, never a rounded one", "[tolerance]") {
  auto result = parse_tolerance("0.2ms/min", Unit::ms_per_min);
  REQUIRE(result.has_value());
  CHECK(result->num == 2);
  CHECK(result->den == 10);
}

TEST_CASE("tolerance: the two-threshold form parses warn and fail onto a shared denominator", "[tolerance]") {
  auto result = parse_tolerance("3ms,5ms", Unit::ms);
  REQUIRE(result.has_value());
  CHECK(result->num == 5);
  CHECK(result->den == 1);
  REQUIRE(result->warn_num.has_value());
  CHECK(*result->warn_num == 3);
}

TEST_CASE("tolerance: the two-threshold form rescales differing fractional precision onto a common denominator",
          "[tolerance]") {
  auto result = parse_tolerance("0.3ms,1.25ms", Unit::ms);
  REQUIRE(result.has_value());
  CHECK(result->den == 100);
  CHECK(result->num == 125);
  REQUIRE(result->warn_num.has_value());
  CHECK(*result->warn_num == 30);
}

TEST_CASE("tolerance: an empty value is rejected as a usage error naming the expected unit, never parsed as zero",
          "[tolerance]") {
  auto result = parse_tolerance("", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("ms") != std::string::npos);
}

TEST_CASE("tolerance: whitespace-only is rejected the same as empty", "[tolerance]") {
  auto result = parse_tolerance("   \t  ", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a magnitude with no digits is rejected", "[tolerance]") {
  auto result = parse_tolerance("ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a suffix not in the grammar is rejected", "[tolerance]") {
  auto result = parse_tolerance("5furlongs", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a suffix whose unit does not match the check's declared unit is rejected naming that unit",
          "[tolerance]") {
  auto result = parse_tolerance("3%", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("ms") != std::string::npos);
}

TEST_CASE("tolerance: a negative magnitude is rejected", "[tolerance]") {
  auto result = parse_tolerance("-5ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

// WR-01 regression: an absurdly long digit run must be rejected as a usage
// error, never silently wrapped via signed integer overflow (UB) into an
// arbitrary tolerance.
TEST_CASE("tolerance: a magnitude too large for int64_t is rejected, never silently wrapped", "[tolerance]") {
  auto result = parse_tolerance("99999999999999999999999999999ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

// WR-01 regression: the same overflow guard applies to the fractional half
// of the magnitude, not just the integer half.
TEST_CASE("tolerance: a fractional magnitude too large for int64_t is rejected, never silently wrapped",
          "[tolerance]") {
  auto result = parse_tolerance("0.99999999999999999999999999999ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a full-width digit is rejected as non-ASCII rather than normalised", "[tolerance]") {
  // U+FF15 FULLWIDTH DIGIT FIVE, UTF-8 encoded, followed by "ms".
  auto result = parse_tolerance("\xEF\xBC\x95ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a Unicode minus sign is rejected as non-ASCII rather than normalised", "[tolerance]") {
  // U+2212 MINUS SIGN, UTF-8 encoded.
  auto result = parse_tolerance("\xE2\x88\x92" "5ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a decimal point with no following digits is rejected", "[tolerance]") {
  auto result = parse_tolerance("5.ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: more than one comma is rejected", "[tolerance]") {
  auto result = parse_tolerance("1ms,2ms,3ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a warn threshold exceeding the fail threshold is rejected", "[tolerance]") {
  auto result = parse_tolerance("5ms,3ms", Unit::ms);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("tolerance: a warn threshold's suffix may be omitted or must match the fail threshold's", "[tolerance]") {
  // Bare warn, suffixed fail -- both accepted spellings for the same value.
  auto bare_warn = parse_tolerance("3,5ms", Unit::ms);
  REQUIRE(bare_warn.has_value());
  auto suffixed_warn = parse_tolerance("3ms,5ms", Unit::ms);
  REQUIRE(suffixed_warn.has_value());
  CHECK(bare_warn->num == suffixed_warn->num);
  CHECK(bare_warn->den == suffixed_warn->den);
  CHECK(bare_warn->warn_num == suffixed_warn->warn_num);

  // A warn suffix that disagrees with the fail suffix is rejected as
  // internally inconsistent, not silently resolved by picking one.
  auto mismatched = parse_tolerance("3%,5ms", Unit::ms);
  REQUIRE_FALSE(mismatched.has_value());
  CHECK(mismatched.error().kind == ErrorKind::usage);
}
