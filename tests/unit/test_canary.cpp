// D-16 permanent canary (02-03-PLAN.md Task 2). tests/fixtures/snapshots/
// canary_{a,b}.snap.json are DELIBERATELY, permanently divergent and must
// NEVER be edited to match -- if this test ever reports the pair clean,
// the test harness itself is broken, not the fixture. This is exactly the
// class of failure that let Phase 1 ship six checks structurally incapable
// of observing their own subject while `ctest` reported green
// (01-VERIFICATION.md's Resolution section): a base case this cheap,
// permanent and loud would have caught it immediately.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/snapshot.h"
#include "support/fixture_paths.h"
#include "test/test_check_id.h"

namespace {

std::string canary_path(const std::string& name) { return mediadiff::test::snapshot_dir() + "/" + name; }

}  // namespace

TEST_CASE("canary: the permanent divergent fixture pair always reports exactly one fail finding", "[canary]") {
  const mediadiff::CheckRegistry& registry = mediadiff::test_registry();

  auto baseline = mediadiff::read_snapshot(canary_path("canary_a.snap.json"), registry);
  auto candidate = mediadiff::read_snapshot(canary_path("canary_b.snap.json"), registry);
  REQUIRE(baseline.has_value());
  REQUIRE(candidate.has_value());

  const mediadiff::Policy policy{mediadiff::ProfileId::sw_encoder};
  auto findings = mediadiff::compare_fingerprints(*baseline, *candidate, policy, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);

  const mediadiff::Finding& finding = findings->front();
  REQUIRE(finding.id == "t.aliased_new");
  REQUIRE(finding.status == mediadiff::Status::fail);
}
