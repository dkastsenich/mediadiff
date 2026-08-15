// Profile identity and the builtin+profile resolution pass (02-05-PLAN.md
// Task 1): the five canonical spellings, ENG-09's default, resolve_policy's
// single deterministic pass over a registry, and doc 01 section 4's
// provenance chain.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "test/test_check_id.h"

using mediadiff::CheckDef;
using mediadiff::CheckRegistry;
using mediadiff::kDefaultProfile;
using mediadiff::Policy;
using mediadiff::PolicyProvenance;
using mediadiff::ProfileId;
using mediadiff::profile_from_string;
using mediadiff::profile_to_string;
using mediadiff::resolve_policy;
using mediadiff::ResolvedCheck;
using mediadiff::Severity;
using mediadiff::test_registry;

TEST_CASE("profiles: profile_from_string accepts each of the five exact spellings", "[profiles]") {
  CHECK(profile_from_string("strict-bitexact") == ProfileId::strict_bitexact);
  CHECK(profile_from_string("sw-encoder") == ProfileId::sw_encoder);
  CHECK(profile_from_string("hw-encoder") == ProfileId::hw_encoder);
  CHECK(profile_from_string("remux") == ProfileId::remux);
  CHECK(profile_from_string("transform") == ProfileId::transform);
}

TEST_CASE("profiles: profile_from_string rejects anything that is not one of the five exact spellings",
          "[profiles]") {
  CHECK_FALSE(profile_from_string("").has_value());
  CHECK_FALSE(profile_from_string("Strict-Bitexact").has_value());
  CHECK_FALSE(profile_from_string("sw_encoder").has_value());
  CHECK_FALSE(profile_from_string("sw-encoder ").has_value());
  CHECK_FALSE(profile_from_string("bitexact").has_value());
}

TEST_CASE("profiles: profile_to_string round-trips profile_from_string for every profile", "[profiles]") {
  for (ProfileId profile :
       {ProfileId::strict_bitexact, ProfileId::sw_encoder, ProfileId::hw_encoder, ProfileId::remux,
        ProfileId::transform}) {
    const auto text = profile_to_string(profile);
    CHECK(profile_from_string(text) == profile);
  }
}

TEST_CASE("profiles: the default profile absent any selection is sw-encoder (ENG-09)", "[profiles]") {
  CHECK(kDefaultProfile == ProfileId::sw_encoder);
}

TEST_CASE("profiles: a check with no profile_severity table resolves to its checks.def baseline under every profile",
          "[profiles]") {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.set_tags");
  REQUIRE(index.has_value());
  const CheckDef& check = registry.at(*index);

  for (ProfileId profile :
       {ProfileId::strict_bitexact, ProfileId::sw_encoder, ProfileId::hw_encoder, ProfileId::remux,
        ProfileId::transform}) {
    auto policy = resolve_policy(registry, profile);
    REQUIRE(policy.has_value());
    CHECK(policy->per_check[*index].severity == check.default_severity);
  }
}

TEST_CASE("profiles: a check declaring a tolerance has that tolerance parsed into per_check when unresolved by profile",
          "[profiles]") {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.tol_ms");
  REQUIRE(index.has_value());

  auto policy = resolve_policy(registry, ProfileId::remux);
  REQUIRE(policy.has_value());
  REQUIRE(policy->per_check[*index].tolerance.has_value());
  CHECK(policy->per_check[*index].tolerance->unit == mediadiff::Unit::ms);
}

TEST_CASE("profiles: two profiles differing in exactly one check's severity differ at exactly that one per_check "
          "index",
          "[profiles]") {
  const CheckRegistry& registry = test_registry();
  // t.exact_string declares [check.profile_severity] strict_bitexact="fail"
  // over a baseline of "warn" -- the same demonstrative shape as
  // src/core/checks.def's meta.tool_version (02-02-PLAN.md).
  auto baseline_policy = resolve_policy(registry, ProfileId::sw_encoder);
  auto override_policy = resolve_policy(registry, ProfileId::strict_bitexact);
  REQUIRE(baseline_policy.has_value());
  REQUIRE(override_policy.has_value());
  REQUIRE(baseline_policy->per_check.size() == override_policy->per_check.size());

  std::optional<std::size_t> differing_index;
  std::size_t differing_count = 0;
  for (std::size_t i = 0; i < baseline_policy->per_check.size(); ++i) {
    if (baseline_policy->per_check[i].severity != override_policy->per_check[i].severity) {
      ++differing_count;
      differing_index = i;
    }
  }

  REQUIRE(differing_count == 1);
  const auto index = registry.find("t.exact_string");
  REQUIRE(index.has_value());
  CHECK(*differing_index == *index);
  CHECK(baseline_policy->per_check[*index].severity == Severity::warn);
  CHECK(override_policy->per_check[*index].severity == Severity::fail);
}

TEST_CASE("profiles: resolve_policy called twice with the same arguments produces equal results", "[profiles]") {
  const CheckRegistry& registry = test_registry();
  auto first = resolve_policy(registry, ProfileId::hw_encoder);
  auto second = resolve_policy(registry, ProfileId::hw_encoder);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(first->profile == second->profile);
  CHECK(first->per_check.size() == second->per_check.size());
  CHECK(first->per_check == second->per_check);
}

TEST_CASE("profiles: every resolved chain begins with a builtin entry", "[profiles]") {
  const CheckRegistry& registry = test_registry();
  auto policy = resolve_policy(registry, ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  REQUIRE_FALSE(policy->per_check.empty());
  for (const ResolvedCheck& resolved : policy->per_check) {
    REQUIRE_FALSE(resolved.chain.empty());
    CHECK(resolved.chain.front().layer == PolicyProvenance::Layer::builtin);
  }
}
