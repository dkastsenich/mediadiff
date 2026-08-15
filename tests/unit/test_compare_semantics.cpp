// Table-driven edge-value coverage for the six semantics plan 02-04
// implements (doc 01 section 12's testing strategy), distinct from
// tests/unit/test_fail_first_coverage.cpp's (semantic, status) gate: this
// file exercises boundary values, empty inputs, ordering independence, the
// hash precondition path, and D-09's value_kind mismatch -- the specific
// edge cases 02-04-PLAN.md Task 3 names, each backed by a real assertion
// rather than a fixture file alone.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

#include "compare/engine.h"
#include "compare/semantics.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/rational.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "support/fixture_paths.h"
#include "support/stub_analyzer.h"
#include "test/test_check_id.h"

using mediadiff::Absent;
using mediadiff::CheckRegistry;
using mediadiff::Finding;
using mediadiff::Fingerprint;
using mediadiff::Histogram;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::Span;
using mediadiff::SpanList;
using mediadiff::Status;
using mediadiff::StringSet;
using mediadiff::compare_fingerprints;
using mediadiff::read_snapshot;
using mediadiff::test_registry;
using mediadiff::test::make_stub_fingerprint;
using mediadiff::test::StubMeasurement;
using mediadiff::test::snapshot_dir;

namespace {

RationalValue ms(std::int64_t value) { return RationalValue{value, 1, mediadiff::Rational{1, 1}}; }

Scope global0() { return Scope{Scope::Kind::global, 0}; }

const Policy kPolicy{ProfileId::sw_encoder};

// Every helper below builds ONE baseline fingerprint and ONE candidate
// fingerprint from a single StubMeasurement each, so a single-Finding
// result can be asserted directly.
Finding single_finding(const std::string& check_id, const mediadiff::Value& baseline_value,
                        const mediadiff::Value& candidate_value) {
  const CheckRegistry& registry = test_registry();
  const Fingerprint baseline = make_stub_fingerprint(registry, std::vector<StubMeasurement>{
                                                                     {check_id, global0(), baseline_value}});
  const Fingerprint candidate = make_stub_fingerprint(registry, std::vector<StubMeasurement>{
                                                                     {check_id, global0(), candidate_value}});
  auto findings = compare_fingerprints(baseline, candidate, kPolicy, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);
  return (*findings)[0];
}

}  // namespace

TEST_CASE("semantics: tol boundary -- delta exactly at the threshold passes, one tick beyond does not",
          "[semantics]") {
  // t.tol_info: single threshold 5ms, severity=info.
  const Finding at_boundary = single_finding("t.tol_info", mediadiff::Value{ms(0)}, mediadiff::Value{ms(5)});
  CHECK(at_boundary.status == Status::pass);

  const Finding beyond = single_finding("t.tol_info", mediadiff::Value{ms(0)}, mediadiff::Value{ms(6)});
  CHECK(beyond.status == Status::info);
}

TEST_CASE("semantics: dist boundary -- worst-bin delta exactly at tolerance passes, one point beyond warns",
          "[semantics]") {
  // t.dist_bins: tolerance 5%, severity=warn.
  Histogram baseline;
  baseline.bins = {{"a", 50}, {"b", 50}};

  Histogram at_boundary;
  at_boundary.bins = {{"a", 55}, {"b", 45}};  // |55/100 - 50/100| = 5%
  const Finding at = single_finding("t.dist_bins", mediadiff::Value{baseline}, mediadiff::Value{at_boundary});
  CHECK(at.status == Status::pass);

  Histogram beyond;
  beyond.bins = {{"a", 56}, {"b", 44}};  // 6%
  const Finding over = single_finding("t.dist_bins", mediadiff::Value{baseline}, mediadiff::Value{beyond});
  CHECK(over.status == Status::warn);
}

// CR-03 regression: baseline_mag/candidate_mag's num/den are int64
// magnitudes read straight off an (in principle untrusted) snapshot --
// CR-01 validates their TYPE at read time, never their MAGNITUDE. A
// near-INT64_MAX num crossed with a den > 1 must be REJECTED (Status::error,
// "cannot determine a verdict") rather than silently overflowing into an
// arbitrary pass/warn/fail via UB.
TEST_CASE("semantics: CR-03 tol comparator returns Status::error on cross-multiplication overflow, never a "
          "fabricated verdict",
          "[semantics]") {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  // den=2/den=3 guarantees the very first cross-multiplication
  // (candidate.num * baseline.den) overflows: kMax * 2 cannot fit in
  // int64_t.
  const RationalValue baseline{kMax, 2, mediadiff::Rational{1, 1}};
  const RationalValue candidate{kMax, 3, mediadiff::Rational{1, 1}};
  const Finding f = single_finding("t.tol_ms", mediadiff::Value{baseline}, mediadiff::Value{candidate});
  CHECK(f.status == Status::error);
}

// CR-03 regression: compare_dist's own bin-total accumulation and
// cross-multiplication are equally reachable from an untrusted histogram's
// int64 bin counts -- same "reject, never wrap" contract as compare_tol
// above.
TEST_CASE("semantics: CR-03 dist comparator returns Status::error on bin-total overflow, never a fabricated "
          "verdict",
          "[semantics]") {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  Histogram baseline;
  baseline.bins = {{"x", kMax}};
  Histogram candidate;
  // (kMax - 1) + 1000 overflows candidate_total's own accumulation.
  candidate.bins = {{"x", kMax - 1}, {"y", 1000}};
  const Finding f = single_finding("t.dist_bins", mediadiff::Value{baseline}, mediadiff::Value{candidate});
  CHECK(f.status == Status::error);
}

TEST_CASE("semantics: two empty fingerprints yield zero findings and no error", "[semantics]") {
  const CheckRegistry& registry = test_registry();
  const Fingerprint baseline = make_stub_fingerprint(registry, std::vector<StubMeasurement>{});
  const Fingerprint candidate = make_stub_fingerprint(registry, std::vector<StubMeasurement>{});
  auto findings = compare_fingerprints(baseline, candidate, kPolicy, registry);
  REQUIRE(findings.has_value());
  CHECK(findings->empty());
}

TEST_CASE("semantics: two empty StringSets pass", "[semantics]") {
  const Finding finding = single_finding("t.set_tags", mediadiff::Value{StringSet{}}, mediadiff::Value{StringSet{}});
  CHECK(finding.status == Status::pass);
}

TEST_CASE("semantics: two empty Histograms pass", "[semantics]") {
  const Finding finding = single_finding("t.dist_bins", mediadiff::Value{Histogram{}}, mediadiff::Value{Histogram{}});
  CHECK(finding.status == Status::pass);
}

TEST_CASE("semantics: two empty SpanLists pass", "[semantics]") {
  const Finding finding = single_finding("t.span_runs", mediadiff::Value{SpanList{}}, mediadiff::Value{SpanList{}});
  CHECK(finding.status == Status::pass);
}

TEST_CASE("semantics: reordering a baseline StringSet's construction produces a byte-identical finding",
          "[semantics]") {
  StringSet order_a;
  order_a.insert("z");
  order_a.insert("a");
  order_a.insert("m");

  StringSet order_b;
  order_b.insert("a");
  order_b.insert("m");
  order_b.insert("z");

  // StringSet is std::set, so both insertion orders already produce an
  // identical container -- this assertion documents that structural
  // property before checking the comparator's own output is identical too.
  REQUIRE(order_a == order_b);

  const StringSet candidate{"a", "m", "z", "extra"};
  const Finding finding_a = single_finding("t.set_tags", mediadiff::Value{order_a}, mediadiff::Value{candidate});
  const Finding finding_b = single_finding("t.set_tags", mediadiff::Value{order_b}, mediadiff::Value{candidate});
  CHECK(finding_a.status == finding_b.status);
  CHECK(finding_a.message == finding_b.message);
}

TEST_CASE("semantics: shuffling a SpanList's element order produces the same merged output", "[semantics]") {
  const Span first{ms(0), ms(10)};
  const Span second{ms(20), ms(30)};

  SpanList unsorted;
  unsorted.spans = {second, first};

  SpanList sorted;
  sorted.spans = {first, second};

  SpanList baseline_list;
  baseline_list.spans = {first};

  const Finding finding_unsorted = single_finding("t.span_runs", mediadiff::Value{baseline_list},
                                                    mediadiff::Value{unsorted});
  const Finding finding_sorted = single_finding("t.span_runs", mediadiff::Value{baseline_list},
                                                  mediadiff::Value{sorted});
  CHECK(finding_unsorted.status == finding_sorted.status);
  CHECK(finding_unsorted.message == finding_sorted.message);
}

TEST_CASE("semantics: hash preconditions differing produces skipped:hash_incomparable, never a fabricated verdict",
          "[semantics]") {
  const CheckRegistry& registry = test_registry();
  const std::string dir = snapshot_dir();
  auto baseline = read_snapshot(dir + "/t.hash_chain__skipped.a.snap.json", registry);
  auto candidate = read_snapshot(dir + "/t.hash_chain__skipped.b.snap.json", registry);
  REQUIRE(baseline.has_value());
  REQUIRE(candidate.has_value());

  auto findings = compare_fingerprints(*baseline, *candidate, kPolicy, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);
  CHECK((*findings)[0].status == Status::skipped);
  CHECK((*findings)[0].skip_reason == SkipReason::hash_incomparable);
}

TEST_CASE("semantics: a measurement holding a std::string for a check whose declared value_kind is int64 "
          "yields status error (D-09)",
          "[semantics]") {
  const Finding finding =
      single_finding("t.int64_count", mediadiff::Value{std::string{"not-an-int"}}, mediadiff::Value{std::int64_t{5}});
  CHECK(finding.status == Status::error);
}
