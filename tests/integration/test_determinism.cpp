// D-13/TRUST-05 determinism harness (02-03-PLAN.md Task 3): running the
// same compare twice must produce byte-identical --json output.
//
// This re-verifies TRUST-05 against the full test registry Task 1 built
// (ten `t.*` checks across all seven semantics) rather than only the three
// shipped `meta.*` checks 02-01's tracer proved it on -- so the fail-first
// infrastructure this plan adds (the second generated registry, the
// coverage gate's fixture enumeration, the canary, the golden harness) is
// shown to be transparent to the guarantee rather than perturbing it. This
// deliberately does NOT go through the mediadiff binary/cli_harness.h: the
// shipped binary only knows the production registry (three `meta.*`
// checks), which cannot exercise the test registry's ten checks. Instead
// it calls compare_fingerprints()/render_json() directly, linked from
// libmediadiff, against Fingerprints built by tests/support/stub_analyzer.h.
//
// (The compare-path guarantee itself was established in 02-01; the
// envelope-field exclusions are 02-07's job. This test adds neither -- it
// proves the test infrastructure this plan adds is transparent to both.)

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/snapshot.h"
#include "report/json.h"
#include "support/fixture_paths.h"
#include "support/stub_analyzer.h"
#include "test/test_check_id.h"

namespace {

std::string snap_path(const std::string& name) { return mediadiff::test::snapshot_dir() + "/" + name; }

std::string render_compare(const mediadiff::CheckRegistry& registry, const mediadiff::Fingerprint& baseline,
                            const mediadiff::Fingerprint& candidate) {
  const mediadiff::Policy policy{mediadiff::ProfileId::sw_encoder};
  auto findings = mediadiff::compare_fingerprints(baseline, candidate, policy, registry);
  REQUIRE(findings.has_value());
  return mediadiff::render_json(*findings, baseline.envelope, registry);
}

}  // namespace

TEST_CASE("determinism - D-13/TRUST-05: two runs over the full test registry produce byte-identical --json",
          "[integration]") {
  const mediadiff::CheckRegistry& registry = mediadiff::test_registry();

  auto baseline = mediadiff::read_snapshot(snap_path("t.exact_string__pass.a.snap.json"), registry);
  auto candidate = mediadiff::read_snapshot(snap_path("t.exact_string__pass.b.snap.json"), registry);
  REQUIRE(baseline.has_value());
  REQUIRE(candidate.has_value());

  const std::string first = render_compare(registry, *baseline, *candidate);
  const std::string second = render_compare(registry, *baseline, *candidate);
  REQUIRE(first == second);
}

TEST_CASE(
    "determinism - D-13/TRUST-05: registry declaration order governs report output, not measurement "
    "insertion order",
    "[integration]") {
  // The coverage gate (Task 2) walks tests/fixtures/snapshots/ in whatever
  // order std::filesystem::directory_iterator happens to return -- this
  // proves that enumeration order never leaks into report output: the same
  // set of measurements, inserted in two different orders via
  // make_stub_fingerprint, must still render byte-identical JSON
  // (compare/engine.cpp's own TRUST-05 ordering contract, from 02-01).
  const mediadiff::CheckRegistry& registry = mediadiff::test_registry();

  const std::vector<mediadiff::test::StubMeasurement> forward = {
      {"t.exact_string", mediadiff::Scope{mediadiff::Scope::Kind::global, 0}, mediadiff::Value{std::string("same")}},
      {"t.aliased_new", mediadiff::Scope{mediadiff::Scope::Kind::global, 0}, mediadiff::Value{std::string("same")}},
  };
  const std::vector<mediadiff::test::StubMeasurement> reversed(forward.rbegin(), forward.rend());

  const mediadiff::Fingerprint baseline_forward = mediadiff::test::make_stub_fingerprint(registry, forward);
  const mediadiff::Fingerprint candidate_forward = mediadiff::test::make_stub_fingerprint(registry, forward);
  const mediadiff::Fingerprint baseline_reversed = mediadiff::test::make_stub_fingerprint(registry, reversed);
  const mediadiff::Fingerprint candidate_reversed = mediadiff::test::make_stub_fingerprint(registry, reversed);

  const std::string json_forward = render_compare(registry, baseline_forward, candidate_forward);
  const std::string json_reversed = render_compare(registry, baseline_reversed, candidate_reversed);
  REQUIRE(json_forward == json_reversed);
}
