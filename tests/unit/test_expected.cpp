#include <catch2/catch_test_macros.hpp>

#include <string>

#include "util/expected.h"

namespace {

mediadiff::expected<int, std::string> parse_positive(int value) {
  if (value > 0) {
    return value;
  }
  return mediadiff::unexpected<std::string>("value must be positive");
}

}  // namespace

TEST_CASE("mediadiff::expected holding a value is truthy and yields it", "[expected]") {
  mediadiff::expected<int, std::string> e = 42;
  REQUIRE(static_cast<bool>(e));
  REQUIRE(e.value() == 42);
}

TEST_CASE("mediadiff::expected holding an error is falsy and yields it", "[expected]") {
  mediadiff::expected<int, std::string> e = mediadiff::unexpected<std::string>("boom");
  REQUIRE_FALSE(static_cast<bool>(e));
  REQUIRE(e.error() == "boom");
}

TEST_CASE("and_then on a value propagates and transforms", "[expected]") {
  auto result = parse_positive(5).and_then([](int v) -> mediadiff::expected<int, std::string> {
    return v * 2;
  });
  REQUIRE(static_cast<bool>(result));
  REQUIRE(result.value() == 10);
}

TEST_CASE("and_then on an error short-circuits and preserves the original error", "[expected]") {
  auto result = parse_positive(-1).and_then([](int v) -> mediadiff::expected<int, std::string> {
    return v * 2;
  });
  REQUIRE_FALSE(static_cast<bool>(result));
  REQUIRE(result.error() == "value must be positive");
}

TEST_CASE("mediadiff::unexpected constructs an error-state expected", "[expected]") {
  mediadiff::expected<int, std::string> e = mediadiff::unexpected<std::string>("explicit error");
  REQUIRE_FALSE(static_cast<bool>(e));
  REQUIRE(e.error() == "explicit error");
}
