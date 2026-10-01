// 07-09-PLAN.md Task 1 (TRUST-04, D-04): compare_tol's generic path
// precondition table (kTolPreconditionKeys = scaler_path, decode_path_signature).
// compare_tol is called directly with a hand-built CheckDef and synthetic
// Measurements, the way tests/unit/test_tolerance.cpp does -- under D-01 both
// sides of a real two-file run come from one build, so no real fixture pair can
// trip the guard today (A19); it is proven here on synthetic evidence.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "compare/semantics.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/rational.h"
#include "core/registry.h"
#include "core/value.h"

using mediadiff::CheckDef;
using mediadiff::Finding;
using mediadiff::Measurement;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::Rational;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::Semantic;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::Unit;
using mediadiff::ValueKind;
using mediadiff::compare_tol;

namespace {

Measurement measurement_with(std::int64_t value, nlohmann::ordered_json evidence) {
  Measurement m;
  m.check_index = 0;
  m.scope = Scope{Scope::Kind::global, 0};
  m.value = mediadiff::Value{RationalValue{value, 1, Rational{1, 1}}};
  m.evidence = std::move(evidence);
  return m;
}

// A single-threshold ms `tol` check; the table under test is evidence-driven, so
// the check's own id and unit are immaterial to it.
CheckDef make_check() {
  return CheckDef{
      .id = "t.synthetic_tol_path",
      .group = "t",
      .semantic = Semantic::tol,
      .unit = Unit::ms,
      .value_kind = ValueKind::rational,
      .default_severity = Severity::fail,
      .default_tolerance = "5ms",
      .is_volatile = false,
      .requires_pass = false,
      .profile_severity_overrides = nullptr,
      .profile_severity_override_count = 0,
      .profile_tolerance_overrides = nullptr,
      .profile_tolerance_override_count = 0,
  };
}

const Policy kPolicy{ProfileId::sw_encoder};

nlohmann::ordered_json paths(std::string_view scaler, std::string_view decode) {
  nlohmann::ordered_json evidence = nlohmann::ordered_json::object();
  evidence["scaler_path"] = std::string(scaler);
  evidence["decode_path_signature"] = std::string(decode);
  return evidence;
}

Finding run(const Measurement& baseline, const Measurement& candidate) {
  const CheckDef check = make_check();
  auto finding = compare_tol(check, baseline, candidate, kPolicy);
  REQUIRE(finding.has_value());
  return *finding;
}

}  // namespace

TEST_CASE("tol_path_signature - scaler differs", "[tol_path_signature]") {
  const Finding finding = run(measurement_with(10, paths("bilinear-a", "sw-1")),
                              measurement_with(10, paths("bicubic-b", "sw-1")));
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::path_incomparable);
  CHECK(finding.message.find("scaler_path") != std::string::npos);
  CHECK(finding.message.find("re-run both sides with the same build and scaler settings") != std::string::npos);
}

TEST_CASE("tol_path_signature - decode path differs", "[tol_path_signature]") {
  const Finding finding = run(measurement_with(10, paths("bilinear-a", "sw-1")),
                              measurement_with(10, paths("bilinear-a", "hwaccel-9")));
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::path_incomparable);
  CHECK(finding.message.find("decode_path_signature") != std::string::npos);
}

TEST_CASE("tol_path_signature - one-sided key", "[tol_path_signature]") {
  nlohmann::ordered_json candidate_only = nlohmann::ordered_json::object();
  candidate_only["scaler_path"] = "bilinear-a";
  const Finding candidate_has =
      run(measurement_with(10, nlohmann::ordered_json::object()), measurement_with(10, candidate_only));
  CHECK(candidate_has.status == Status::skipped);
  CHECK(candidate_has.skip_reason == SkipReason::path_incomparable);
  CHECK(candidate_has.message.find("scaler_path") != std::string::npos);

  // The mirror image: the key on the baseline only is just as incomparable.
  const Finding baseline_has =
      run(measurement_with(10, candidate_only), measurement_with(10, nlohmann::ordered_json::object()));
  CHECK(baseline_has.status == Status::skipped);
  CHECK(baseline_has.skip_reason == SkipReason::path_incomparable);
}

TEST_CASE("tol_path_signature - inside tolerance still skips", "[tol_path_signature]") {
  // Equal magnitudes (delta 0, well within 5ms): a mismatch is still a skip,
  // never a fabricated pass.
  const Finding finding = run(measurement_with(7, paths("bilinear-a", "sw-1")),
                              measurement_with(7, paths("bicubic-b", "sw-1")));
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::path_incomparable);
  CHECK(finding.status != Status::pass);
}

TEST_CASE("tol_path_signature - equal paths", "[tol_path_signature]") {
  const Finding beyond = run(measurement_with(0, paths("bilinear-a", "sw-1")),
                             measurement_with(20, paths("bilinear-a", "sw-1")));
  CHECK(beyond.skip_reason == SkipReason::none);
  CHECK(beyond.status == Status::fail);

  const Finding within = run(measurement_with(0, paths("bilinear-a", "sw-1")),
                             measurement_with(3, paths("bilinear-a", "sw-1")));
  CHECK(within.skip_reason == SkipReason::none);
  CHECK(within.status == Status::pass);
}

TEST_CASE("tol_path_signature - no keys", "[tol_path_signature]") {
  // A timeline-style fixture: object evidence carrying neither key, and a null
  // evidence -- both behave exactly as before the table existed.
  nlohmann::ordered_json other = nlohmann::ordered_json::object();
  other["comparison_basis"] = "raw";
  const Finding pass = run(measurement_with(0, other), measurement_with(3, other));
  CHECK(pass.status == Status::pass);
  CHECK(pass.skip_reason == SkipReason::none);

  const Finding fail = run(measurement_with(0, nlohmann::ordered_json()), measurement_with(20, nlohmann::ordered_json()));
  CHECK(fail.status == Status::fail);
  CHECK(fail.skip_reason == SkipReason::none);
}
