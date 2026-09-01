// The `volatile` rule (02-05-PLAN.md Task 2, doc 01 section 4): a
// volatile-flagged check is computed on every run, resolves to
// Severity::ignore in every one of the five profiles regardless of any
// profile override, never gates the exit code, and is overridable only by
// an explicit later-layer (config/CLI) provenance entry.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/value.h"
#include "support/stub_analyzer.h"
#include "test/test_check_id.h"

using mediadiff::CheckDef;
using mediadiff::CheckRegistry;
using mediadiff::Finding;
using mediadiff::Fingerprint;
using mediadiff::Policy;
using mediadiff::PolicyProvenance;
using mediadiff::ProfileId;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::Status;
using mediadiff::Value;
using mediadiff::apply_severity_override;
using mediadiff::compare_fingerprints;
using mediadiff::resolve_policy;
using mediadiff::resolve_severity;
using mediadiff::test_registry;
using mediadiff::test::make_stub_fingerprint;
using mediadiff::test::StubMeasurement;

namespace {

Scope global0() { return Scope{Scope::Kind::global, 0}; }

const Policy kPolicy{ProfileId::sw_encoder};

Finding single_finding(const std::string& check_id, const Value& baseline_value, const Value& candidate_value) {
  const CheckRegistry& registry = test_registry();
  const Fingerprint baseline =
      make_stub_fingerprint(registry, std::vector<StubMeasurement>{{check_id, global0(), baseline_value}});
  const Fingerprint candidate =
      make_stub_fingerprint(registry, std::vector<StubMeasurement>{{check_id, global0(), candidate_value}});
  auto findings = compare_fingerprints(baseline, candidate, kPolicy, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);
  return (*findings)[0];
}

}  // namespace

TEST_CASE("volatile: equal values under t.volatile_tag report pass", "[volatile]") {
  const Finding finding = single_finding("t.volatile_tag", Value{std::string{"same"}}, Value{std::string{"same"}});
  CHECK(finding.status == Status::pass);
  CHECK(finding.severity == Severity::ignore);
}

TEST_CASE("volatile: t.volatile_tag resolves to Severity::ignore under every shipped profile", "[volatile]") {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.volatile_tag");
  REQUIRE(index.has_value());
  const CheckDef& check = registry.at(*index);

  for (ProfileId profile :
       {ProfileId::strict_bitexact, ProfileId::sw_encoder, ProfileId::hw_encoder, ProfileId::remux,
        ProfileId::transform}) {
    auto policy = resolve_policy(registry, profile);
    REQUIRE(policy.has_value());
    CHECK(resolve_severity(check, *policy) == Severity::ignore);
    CHECK(policy->per_check[*index].severity == Severity::ignore);
  }
}

TEST_CASE("volatile: a profile declaring no override for the volatile check does not promote it", "[volatile]") {
  const CheckRegistry& registry = test_registry();
  const auto index = registry.find("t.volatile_tag");
  REQUIRE(index.has_value());

  // t.volatile_tag declares no [check.profile_severity] table at all -- an
  // empty override table must not promote it either.
  auto policy = resolve_policy(registry, ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  CHECK(policy->per_check[*index].severity == Severity::ignore);
}

TEST_CASE(
    "volatile: a differing value under t.volatile_tag is still computed, adds a finding, and never gates",
    "[volatile]") {
  const CheckRegistry& registry = test_registry();

  const Fingerprint baseline_without = make_stub_fingerprint(
      registry, std::vector<StubMeasurement>{{"t.exact_string", global0(), Value{std::string{"same"}}}});
  const Fingerprint candidate_without = make_stub_fingerprint(
      registry, std::vector<StubMeasurement>{{"t.exact_string", global0(), Value{std::string{"same"}}}});
  auto findings_without = compare_fingerprints(baseline_without, candidate_without, kPolicy, registry);
  REQUIRE(findings_without.has_value());
  REQUIRE(findings_without->size() == 1);

  const Fingerprint baseline_with = make_stub_fingerprint(
      registry, std::vector<StubMeasurement>{{"t.exact_string", global0(), Value{std::string{"same"}}},
                                              {"t.volatile_tag", global0(), Value{std::string{"a"}}}});
  const Fingerprint candidate_with = make_stub_fingerprint(
      registry, std::vector<StubMeasurement>{{"t.exact_string", global0(), Value{std::string{"same"}}},
                                              {"t.volatile_tag", global0(), Value{std::string{"b"}}}});
  auto findings_with = compare_fingerprints(baseline_with, candidate_with, kPolicy, registry);
  REQUIRE(findings_with.has_value());
  // The volatile check's differing value is computed and produces its own
  // finding -- never dropped -- so the total grows by exactly one.
  CHECK(findings_with->size() == findings_without->size() + 1);

  bool any_gating = false;
  for (const Finding& f : *findings_with) {
    if (f.status == Status::fail || f.status == Status::warn) {
      any_gating = true;
    }
  }
  CHECK_FALSE(any_gating);

  const auto it = std::find_if(findings_with->begin(), findings_with->end(),
                                [](const Finding& f) { return f.id == "t.volatile_tag"; });
  REQUIRE(it != findings_with->end());
  CHECK(it->severity == Severity::ignore);
  CHECK(it->status != Status::pass);
  CHECK(it->status != Status::fail);
  CHECK(it->status != Status::warn);
}

TEST_CASE("volatile: an explicit later-layer override to fail makes t.volatile_tag gate", "[volatile]") {
  const CheckRegistry& registry = test_registry();
  auto policy = resolve_policy(registry, ProfileId::sw_encoder);
  REQUIRE(policy.has_value());

  const auto index = registry.find("t.volatile_tag");
  REQUIRE(index.has_value());
  apply_severity_override(*policy, *index, Severity::fail, PolicyProvenance::Layer::cli,
                           "--set t.volatile_tag=fail");
  CHECK(policy->per_check[*index].severity == Severity::fail);
  CHECK(policy->per_check[*index].chain.back().layer == PolicyProvenance::Layer::cli);

  const Fingerprint baseline =
      make_stub_fingerprint(registry, std::vector<StubMeasurement>{{"t.volatile_tag", global0(), Value{std::string{"a"}}}});
  const Fingerprint candidate =
      make_stub_fingerprint(registry, std::vector<StubMeasurement>{{"t.volatile_tag", global0(), Value{std::string{"b"}}}});
  auto findings = compare_fingerprints(baseline, candidate, *policy, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);
  CHECK((*findings)[0].status == Status::fail);
  CHECK((*findings)[0].severity == Severity::fail);
}
