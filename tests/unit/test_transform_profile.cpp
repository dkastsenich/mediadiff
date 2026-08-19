// The `transform` profile's expectation mechanism (02-05-PLAN.md Task 3,
// doc 01 section 5, ENG-10): parse_resolution_expectation's grammar, and
// compare/exact.cpp's derived-expectation comparison for a check carrying
// `transform_affected`.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "compare/semantics.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/value.h"
#include "test/test_check_id.h"

using mediadiff::CheckDef;
using mediadiff::CheckRegistry;
using mediadiff::compare_exact;
using mediadiff::Dimensions;
using mediadiff::ErrorKind;
using mediadiff::Finding;
using mediadiff::Measurement;
using mediadiff::parse_dimensions;
using mediadiff::parse_resolution_expectation;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::ResolutionExpectation;
using mediadiff::Scope;
using mediadiff::Status;
using mediadiff::test_registry;
using mediadiff::Value;

namespace {

Measurement make_measurement(std::uint32_t check_index, Value value) {
  Measurement m;
  m.check_index = check_index;
  m.scope = Scope{Scope::Kind::global, 0};
  m.value = std::move(value);
  return m;
}

const CheckDef& transform_check() {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.transform_resolution");
  REQUIRE(index.has_value());
  return registry.at(*index);
}

std::uint32_t transform_check_index() {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.transform_resolution");
  REQUIRE(index.has_value());
  return *index;
}

Policy transform_policy(std::string resolution) {
  Policy policy;
  policy.profile = ProfileId::transform;
  policy.transform_expectation.resolution = std::move(resolution);
  return policy;
}

}  // namespace

TEST_CASE("transform: parse_resolution_expectation accepts a bare scale factor", "[transform]") {
  auto expectation = parse_resolution_expectation("2x");
  REQUIRE(expectation.has_value());
  CHECK(expectation->is_scale_factor);
  CHECK(expectation->scale_num == 2);
  CHECK(expectation->scale_den == 1);
}

TEST_CASE("transform: parse_resolution_expectation accepts a decimal scale factor exactly", "[transform]") {
  auto expectation = parse_resolution_expectation("1.5x");
  REQUIRE(expectation.has_value());
  CHECK(expectation->is_scale_factor);
  CHECK(expectation->scale_num == 15);
  CHECK(expectation->scale_den == 10);
}

TEST_CASE("transform: parse_resolution_expectation accepts an absolute WIDTHxHEIGHT pair", "[transform]") {
  auto expectation = parse_resolution_expectation("3840x2160");
  REQUIRE(expectation.has_value());
  CHECK_FALSE(expectation->is_scale_factor);
  CHECK(expectation->absolute == Dimensions{3840, 2160});
}

TEST_CASE("transform: parse_resolution_expectation rejects a malformed string naming both accepted forms",
          "[transform]") {
  auto expectation = parse_resolution_expectation("bogus");
  REQUIRE_FALSE(expectation.has_value());
  CHECK(expectation.error().kind == ErrorKind::usage);
  CHECK(expectation.error().message.find("2x") != std::string::npos);
  CHECK(expectation.error().message.find("3840x2160") != std::string::npos);
}

// WR-01 regression: an absurdly long digit run in the scale-factor grammar
// must be rejected as a usage error, never silently wrapped via signed
// integer overflow (UB) into an arbitrary scale factor.
TEST_CASE("transform: parse_resolution_expectation rejects a scale factor too large for int64_t", "[transform]") {
  auto expectation = parse_resolution_expectation("999999999999999999999999999999x");
  REQUIRE_FALSE(expectation.has_value());
  CHECK(expectation.error().kind == ErrorKind::usage);
}

// WR-01 regression: parse_dimensions (the WIDTHxHEIGHT grammar, shared with
// compare/exact.cpp's own resolution comparison) has the identical
// overflow-guard requirement -- proven separately since it has its own
// digit-accumulation loop (parse_leading_int), not the scale-factor one.
TEST_CASE("transform: parse_dimensions rejects a width too large for int64_t rather than wrapping",
          "[transform]") {
  auto dims = parse_dimensions("999999999999999999999999999999x1080");
  CHECK_FALSE(dims.has_value());
}

TEST_CASE("transform: parse_dimensions parses a plain WIDTHxHEIGHT pair", "[transform]") {
  auto dims = parse_dimensions("1920x1080");
  REQUIRE(dims.has_value());
  CHECK(*dims == Dimensions{1920, 1080});
  CHECK_FALSE(parse_dimensions("1920").has_value());
  CHECK_FALSE(parse_dimensions("1920x").has_value());
  CHECK_FALSE(parse_dimensions("x1080").has_value());
}

TEST_CASE("transform: a 2x scale factor against a 1920x1080 baseline expects 3840x2160", "[transform]") {
  const CheckDef& check = transform_check();
  const Policy policy = transform_policy("2x");
  const Measurement baseline = make_measurement(transform_check_index(), Value{std::string{"1920x1080"}});

  const Measurement candidate_match = make_measurement(transform_check_index(), Value{std::string{"3840x2160"}});
  auto match = compare_exact(check, baseline, candidate_match, policy);
  REQUIRE(match.has_value());
  CHECK(match->status == Status::pass);

  const Measurement candidate_mismatch = make_measurement(transform_check_index(), Value{std::string{"3840x2159"}});
  auto mismatch = compare_exact(check, baseline, candidate_mismatch, policy);
  REQUIRE(mismatch.has_value());
  CHECK(mismatch->status != Status::pass);
  CHECK(mismatch->message.find("3840x2160") != std::string::npos);
}

TEST_CASE("transform: an absolute expectation compares the candidate directly, ignoring the baseline value",
          "[transform]") {
  const CheckDef& check = transform_check();
  const Policy policy = transform_policy("3840x2160");
  const Measurement baseline = make_measurement(transform_check_index(), Value{std::string{"640x480"}});
  const Measurement candidate = make_measurement(transform_check_index(), Value{std::string{"3840x2160"}});

  auto finding = compare_exact(check, baseline, candidate, policy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
}

TEST_CASE("transform: a scale factor of 1.5x against 1920x1080 derives exactly 2880x1620", "[transform]") {
  const CheckDef& check = transform_check();
  const Policy policy = transform_policy("1.5x");
  const Measurement baseline = make_measurement(transform_check_index(), Value{std::string{"1920x1080"}});
  const Measurement candidate = make_measurement(transform_check_index(), Value{std::string{"2880x1620"}});

  auto finding = compare_exact(check, baseline, candidate, policy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
}

TEST_CASE("transform: a scale factor deriving a fractional dimension is a usage error, not a rounded pass",
          "[transform]") {
  const CheckDef& check = transform_check();
  const Policy policy = transform_policy("1.5x");
  // 101 * 1.5 = 151.5 -- not an exact integer.
  const Measurement baseline = make_measurement(transform_check_index(), Value{std::string{"100x101"}});
  const Measurement candidate = make_measurement(transform_check_index(), Value{std::string{"150x152"}});

  auto finding = compare_exact(check, baseline, candidate, policy);
  REQUIRE_FALSE(finding.has_value());
  CHECK(finding.error().kind == ErrorKind::usage);
}

TEST_CASE("transform: a check without transform_affected compares by baseline equality even under transform",
          "[transform]") {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.exact_string");
  REQUIRE(index.has_value());
  const CheckDef& check = registry.at(*index);

  const Policy policy = transform_policy("2x");
  const Measurement baseline = make_measurement(*index, Value{std::string{"1920x1080"}});

  const Measurement candidate_same = make_measurement(*index, Value{std::string{"1920x1080"}});
  auto same = compare_exact(check, baseline, candidate_same, policy);
  REQUIRE(same.has_value());
  CHECK(same->status == Status::pass);

  // Under baseline equality, "3840x2160" (what a transform-affected check
  // would expect at 2x) is simply a different string -- not a match.
  const Measurement candidate_different = make_measurement(*index, Value{std::string{"3840x2160"}});
  auto different = compare_exact(check, baseline, candidate_different, policy);
  REQUIRE(different.has_value());
  CHECK(different->status != Status::pass);
}
