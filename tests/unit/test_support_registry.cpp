// D-15/02-03-PLAN.md Task 1: proves tests/support/test_checks.def declares
// every Semantic enumerator at least once. A fixture registry that claims
// to "cover all seven comparison semantics" but quietly loses one during a
// later edit is exactly the kind of check-cannot-observe-its-own-subject
// failure D-14 exists to prevent.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>

#include "core/registry.h"
#include "test/test_check_id.h"

TEST_CASE("test_registry: every Semantic enumerator is declared by at least one check", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::test_registry();

  std::set<mediadiff::Semantic> seen;
  for (const mediadiff::CheckDef& def : registry) {
    seen.insert(def.semantic);
  }

  static const std::set<mediadiff::Semantic> kAllSemantics = {
      mediadiff::Semantic::exact, mediadiff::Semantic::tol,      mediadiff::Semantic::set,
      mediadiff::Semantic::presence, mediadiff::Semantic::hash,  mediadiff::Semantic::dist,
      mediadiff::Semantic::span,
  };
  REQUIRE(seen == kAllSemantics);
}

TEST_CASE("test_registry: size matches the number of records in test_checks.def", "[registry]") {
  REQUIRE(mediadiff::test_registry().size() == static_cast<std::size_t>(mediadiff::TestCheckId::kCount));
}

TEST_CASE("test_registry: find resolves each declared test check id", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::test_registry();
  for (const char* id : {"t.exact_string", "t.tol_ms", "t.set_tags", "t.presence_track", "t.hash_chain",
                          "t.dist_bins", "t.span_runs", "t.volatile_tag", "t.requires_pass_gate", "t.aliased_new"}) {
    REQUIRE(registry.find(id).has_value());
  }
}

TEST_CASE("test_registry: flags and alias round-trip through the generated CheckDef table", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::test_registry();

  const mediadiff::CheckDef& volatile_check = registry.at(*registry.find("t.volatile_tag"));
  REQUIRE(volatile_check.is_volatile);
  REQUIRE_FALSE(volatile_check.requires_pass);

  const mediadiff::CheckDef& gated_check = registry.at(*registry.find("t.requires_pass_gate"));
  REQUIRE(gated_check.requires_pass);
  REQUIRE_FALSE(gated_check.is_volatile);

  bool was_aliased = false;
  auto idx = registry.resolve_alias("t.aliased_old", &was_aliased);
  REQUIRE(idx.has_value());
  REQUIRE(was_aliased);
  REQUIRE(registry.at(*idx).id == "t.aliased_new");
}
