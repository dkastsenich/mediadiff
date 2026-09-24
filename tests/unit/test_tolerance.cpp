// core/tolerance.cpp's grammar parser (02-04-PLAN.md Task 1, ENG-05,
// CLI-10): table-driven over every valid suffix, both sign spellings, an
// integer and a fractional magnitude, the two-threshold form, and the
// rejection cases doc 01 section 3 and this plan's own action text list.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include "analyzers/audio/analyzers.h"
#include "analyzers/timeline/analyzers.h"
#include "compare/semantics.h"
#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/rational.h"
#include "core/registry.h"
#include "core/tolerance.h"
#include "core/value.h"
#include "probe/audio_decode.h"

using mediadiff::CheckDef;
using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::Finding;
using mediadiff::Measurement;
using mediadiff::parse_tolerance;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::Rational;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::Semantic;
using mediadiff::Severity;
using mediadiff::Status;
using mediadiff::Tolerance;
using mediadiff::Unit;
using mediadiff::ValueKind;
using mediadiff::builtin_registry;
using mediadiff::compare_tol;
using mediadiff::CheckRegistry;
using mediadiff::kCeilingCrossingDeadbandDen;
using mediadiff::kCeilingCrossingDeadbandNum;
using mediadiff::kLoudnessQuantiserDen;

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

// D-03 (03-CONTEXT.md): compare_tol widens the effective threshold by
// kEstimatedToleranceFactor (3x) when either side of a comparison carries
// Measurement::estimated. compare_tol is called directly here, with a
// hand-built CheckDef, rather than through a registered check id -- lets
// Test 5 (below) exercise a tolerance magnitude no real registered check
// declares, without adding a synthetic check solely to reach it.

namespace {

RationalValue ms(std::int64_t value) { return RationalValue{value, 1, Rational{1, 1}}; }

Measurement measurement_at(std::int64_t value_ms, bool estimated) {
  Measurement m;
  m.check_index = 0;
  m.scope = Scope{Scope::Kind::global, 0};
  m.value = mediadiff::Value{ms(value_ms)};
  m.estimated = estimated;
  return m;
}

// A single-threshold "tol" check: unit=ms, tolerance text supplied by the
// caller, no profile overrides. `tolerance_text` must be a string literal
// (static storage duration) -- the returned CheckDef's default_tolerance
// is a non-owning string_view over it, matching tests/unit/test_glob.cpp's
// own make_def convention for a hand-built CheckDef.
CheckDef make_tol_check(std::string_view tolerance_text, Severity severity = Severity::info) {
  return CheckDef{
      .id = "t.synthetic_tol_widen",
      .group = "t",
      .semantic = Semantic::tol,
      .unit = Unit::ms,
      .value_kind = ValueKind::rational,
      .default_severity = severity,
      .default_tolerance = tolerance_text,
      .is_volatile = false,
      .requires_pass = false,
      .profile_severity_overrides = nullptr,
      .profile_severity_override_count = 0,
      .profile_tolerance_overrides = nullptr,
      .profile_tolerance_override_count = 0,
  };
}

const Policy kWidenPolicy{ProfileId::sw_encoder};

}  // namespace

TEST_CASE("tolerance widening: a non-estimated comparison uses the declared tolerance unchanged", "[tolerance]") {
  const CheckDef check = make_tol_check("5ms");
  const Measurement baseline = measurement_at(0, /*estimated=*/false);
  const Measurement candidate = measurement_at(6, /*estimated=*/false);

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::info);  // 6ms exceeds the unwidened 5ms threshold
  CHECK(finding->message.find("estimated") == std::string::npos);
}

TEST_CASE("tolerance widening: an estimated measurement passes at a delta beyond the declared tolerance but within "
          "3x it, and the message names the widening",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms");
  const Measurement baseline = measurement_at(0, /*estimated=*/true);
  const Measurement candidate = measurement_at(12, /*estimated=*/false);  // beyond 5ms, within 15ms (3x)

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
  CHECK(finding->message.find("estimated") != std::string::npos);
}

TEST_CASE("tolerance widening: a delta beyond 3x the declared tolerance still fails even when both sides are "
          "estimated -- widening is bounded, not a bypass",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms");
  const Measurement baseline = measurement_at(0, /*estimated=*/true);
  const Measurement candidate = measurement_at(20, /*estimated=*/true);  // beyond 15ms (3x)

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::info);  // escalate(Severity::info) for this check's severity
  CHECK(finding->message.find("estimated") != std::string::npos);
}

TEST_CASE("tolerance widening: a tolerance magnitude whose 3x widening exceeds int64 is widened exactly -- the "
          "threshold is neither wrapped nor refused",
          "[tolerance]") {
  // 4e18 comfortably fits int64_t alone (max is ~9.223e18), but 4e18 * 3 =
  // 1.2e19 does not. Until debug session test-898-ci-nonreproducible the
  // comparator returned Status::error here; it now widens exactly
  // (core/exact_int.h). A WRAPPED threshold (1.2e19 - 2^64 < 0) would fail
  // every delta below, and a refused one would be `error` -- the boundary
  // neighbors prove neither happens. -3e18 -> 9e18 is a delta of exactly
  // 1.2e19ms, the widened line itself.
  const CheckDef check = make_tol_check("4000000000000000000ms");
  const Measurement baseline = measurement_at(-3000000000000000000, /*estimated=*/true);

  {
    const Measurement candidate = measurement_at(9000000000000000000, /*estimated=*/true);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);  // exactly at the widened 1.2e19ms line
    CHECK(finding->message.find("estimated") != std::string::npos);
  }
  {
    const Measurement candidate = measurement_at(9000000000000000001, /*estimated=*/true);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::info);  // one past it: escalate(Severity::info)
    CHECK(finding->message.find("12000000000000000001/1ms") != std::string::npos);
  }
  {
    // Unwidened (neither side estimated): the same 1.2e19ms delta is far
    // beyond the declared 4e18ms.
    const Measurement plain_baseline = measurement_at(-3000000000000000000, /*estimated=*/false);
    const Measurement plain_candidate = measurement_at(9000000000000000000, /*estimated=*/false);
    auto finding = compare_tol(check, plain_baseline, plain_candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::info);
  }
}

// Debug session test-898-ci-nonreproducible's own regression: the REAL
// timeline.av_drift rates of tests/fixtures/timeline_ts_nowrap.ts vs
// timeline_ts_jump.ts (reduced rationals, ms/min) -- 270183060000 *
// 36327640 ~= 9.8e18 overflowed int64_t, so a real five-second splice
// reported `error` ("cannot determine a verdict") instead of a verdict.
// Exact delta, computed independently (Python fractions):
// 270183060000/47612048 - 24030060000/36327640
//   == 8670992567615520000/1729633339406720 ms/min ~= 5013.197 ms/min.
TEST_CASE("tolerance: timeline.av_drift's real splice rates, whose cross-products exceed int64, get the exact verdict",
          "[tolerance]") {
  const CheckDef check = make_tol_check("0.2ms", Severity::fail);
  const auto rate = [](std::int64_t num, std::int64_t den) {
    Measurement m;
    m.check_index = 0;
    m.scope = Scope{Scope::Kind::global, 0};
    m.value = mediadiff::Value{RationalValue{num, den, Rational{1, 1}}};
    return m;
  };
  const Measurement baseline = rate(24030060000, 36327640);

  {
    auto finding = compare_tol(check, baseline, rate(270183060000, 47612048), kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::fail);
    CHECK(finding->message.find("8670992567615520000/1729633339406720") != std::string::npos);
  }
  {
    // Identical wide rates on both sides (the flagged/unflagged split pair's
    // own situation): delta exactly zero -> pass, not `error`.
    auto finding = compare_tol(check, rate(270183060000, 47612048), rate(270183060000, 47612048), kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);
  }
  {
    // Boundary neighbors around the 0.2 line on the same baseline:
    // baseline + 2/10 == 240373255280/363276400 exactly -> pass; one unit
    // more in the numerator -> fail.
    auto at_line = compare_tol(check, baseline, rate(240373255280, 363276400), kWidenPolicy);
    REQUIRE(at_line.has_value());
    CHECK(at_line->status == Status::pass);
    auto past_line = compare_tol(check, baseline, rate(240373255281, 363276400), kWidenPolicy);
    REQUIRE(past_line.has_value());
    CHECK(past_line->status == Status::fail);
  }
}

// --- 05-09-PLAN.md Task 2 (TIME-06, D-10): timeline.av_offset's boundary
// behavior and the generic priming-basis override -----------------------
//
// Boundary behavior needs no check-specific machinery at all -- it is the
// shared two-threshold `tol` comparator's own generic behavior, exercised
// here at exactly timeline.av_offset's own registered "5ms,20ms" tolerance
// (Test 9 in this task's own <behavior> block).

namespace {

CheckDef make_av_offset_check() { return make_tol_check("5ms,20ms", Severity::fail); }

}  // namespace

TEST_CASE("timeline.av_offset boundary: a delta exactly at the 5ms warn threshold passes; one tick past it warns",
          "[tolerance]") {
  const CheckDef check = make_av_offset_check();
  const Measurement baseline = measurement_at(0, /*estimated=*/false);

  {
    const Measurement candidate = measurement_at(5, /*estimated=*/false);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);
  }
  {
    const Measurement candidate = measurement_at(6, /*estimated=*/false);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::warn);
  }
}

TEST_CASE("timeline.av_offset boundary: a delta exactly at the 20ms fail threshold warns; one tick past it fails",
          "[tolerance]") {
  const CheckDef check = make_av_offset_check();
  const Measurement baseline = measurement_at(0, /*estimated=*/false);

  {
    const Measurement candidate = measurement_at(20, /*estimated=*/false);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::warn);
  }
  {
    const Measurement candidate = measurement_at(21, /*estimated=*/false);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::fail);
  }
}

namespace {

// A Measurement carrying D-10's own evidence shape -- raw value as
// Measurement::value (always `raw_ms`), `adjusted_offset_ms`/
// `comparison_basis` in evidence, mirroring av_sync.cpp's own construction
// exactly.
Measurement priming_measurement(std::int64_t raw_ms, std::int64_t adjusted_ms, const std::string& basis) {
  Measurement m = measurement_at(raw_ms, /*estimated=*/false);
  m.evidence = nlohmann::ordered_json{
      {"raw_offset_ms", raw_ms},
      {"adjusted_offset_ms", adjusted_ms},
      {"comparison_basis", basis},
  };
  return m;
}

}  // namespace

TEST_CASE("compare_tol D-10 override: both sides declaring comparison_basis=adjusted swaps the compared magnitude "
          "to adjusted_offset_ms on both sides",
          "[tolerance]") {
  const CheckDef check = make_av_offset_check();
  // Raw values differ by 23ms (would fail); adjusted values are identical
  // (0ms delta) -- proving the OVERRIDE, not the raw value, decided the
  // verdict.
  const Measurement baseline = priming_measurement(/*raw=*/-23, /*adjusted=*/0, "adjusted");
  const Measurement candidate = priming_measurement(/*raw=*/0, /*adjusted=*/0, "adjusted");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
}

TEST_CASE("compare_tol D-10 override: EITHER side declaring comparison_basis=raw falls back to the raw magnitude "
          "on BOTH sides, even when the other side prefers adjusted",
          "[tolerance]") {
  const CheckDef check = make_av_offset_check();
  // Raw values are identical (0ms delta, would pass); adjusted values
  // differ by 23ms -- proving the candidate's own "raw" preference forced
  // the WHOLE comparison to raw-to-raw, per D-10's own rule.
  const Measurement baseline = priming_measurement(/*raw=*/0, /*adjusted=*/0, "adjusted");
  const Measurement candidate = priming_measurement(/*raw=*/0, /*adjusted=*/23, "raw");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
}

TEST_CASE("compare_tol D-10 override: a check with no comparison_basis/adjusted_offset_ms evidence shape at all is "
          "entirely unaffected (the override is opt-in via evidence shape, never gated on check.id)",
          "[tolerance]") {
  const CheckDef check = make_av_offset_check();
  const Measurement baseline = measurement_at(0, /*estimated=*/false);
  const Measurement candidate = measurement_at(6, /*estimated=*/false);  // no evidence at all

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::warn);  // ordinary raw-value comparison, unaffected
}

// --- 06-07-PLAN.md Task 1 (D-16, WINDOWS.md #32): the GENERALISED override
// -- Test 8 and Test 9 from this task's own <behavior> block, exercised
// against `make_tol_check`'s own SYNTHETIC "t.synthetic_tol_widen" id
// (neither `timeline.av_drift` nor `timeline.av_offset`), which is what
// makes "never gated on check.id" testable rather than merely asserted. ---

namespace {

// A Measurement carrying the SECOND (generic) evidence shape the
// generalised override reads -- raw value as Measurement::value (always
// `raw_ms`), `span_basis`/`adjusted_magnitude` in evidence, mirroring
// av_sync.cpp's own `timeline.av_drift` construction exactly, but declared
// against a synthetic, non-`av_drift` check id. `adjusted_magnitude` is
// passed as a raw JSON value so Test 9 can supply a non-numeric one
// without this helper coercing it.
Measurement span_basis_measurement(std::int64_t raw_ms, const nlohmann::ordered_json& adjusted_magnitude,
                                    const std::string& basis) {
  Measurement m = measurement_at(raw_ms, /*estimated=*/false);
  m.evidence = nlohmann::ordered_json{
      {"span_basis", basis},
      {"adjusted_magnitude", adjusted_magnitude},
  };
  return m;
}

}  // namespace

TEST_CASE("compare_tol D-16 generalised override: both sides declaring span_basis=adjusted on a SYNTHETIC "
          "non-av_drift, non-av_offset check id swaps the compared magnitude to adjusted_magnitude on both sides "
          "(Test 8)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  // Raw values differ by 20ms (would fail the declared 5ms tolerance);
  // adjusted_magnitude values, both expressed over the SAME
  // kDriftAdjustedMagnitudeDen convention av_sync.cpp writes, are
  // identical -- proving the OVERRIDE, not the raw value, decided the
  // verdict, on a check id that is neither `timeline.av_drift` nor
  // `timeline.av_offset`.
  const Measurement baseline =
      span_basis_measurement(/*raw_ms=*/-20, mediadiff::kDriftAdjustedMagnitudeDen * 5, "adjusted");
  const Measurement candidate =
      span_basis_measurement(/*raw_ms=*/0, mediadiff::kDriftAdjustedMagnitudeDen * 5, "adjusted");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
}

TEST_CASE("compare_tol D-16 generalised override: EITHER side declaring span_basis=raw (or omitting the keys "
          "entirely) falls back to the raw magnitude on BOTH sides, on the SAME synthetic check id (Test 8's "
          "negative half)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  // Raw values are identical (0ms delta, would pass); adjusted_magnitude
  // values differ hugely -- proving the candidate's own "raw" preference
  // forced the WHOLE comparison to raw-to-raw, exactly D-16's own
  // extension of D-10's rule.
  const Measurement baseline = span_basis_measurement(/*raw_ms=*/0, mediadiff::kDriftAdjustedMagnitudeDen * 5,
                                                       "adjusted");
  const Measurement candidate =
      span_basis_measurement(/*raw_ms=*/0, mediadiff::kDriftAdjustedMagnitudeDen * 500, "raw");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);

  // No evidence at all, on the same synthetic id: unaffected, ordinary
  // raw-value comparison (mirrors D-10's own "opt-in via evidence shape"
  // test immediately above, repeated here against the generic key's own
  // check id to prove the override is not somehow keyed on `av_offset`
  // specifically).
  const Measurement plain_baseline = measurement_at(0, /*estimated=*/false);
  const Measurement plain_candidate = measurement_at(6, /*estimated=*/false);
  auto plain_finding = compare_tol(check, plain_baseline, plain_candidate, kWidenPolicy);
  REQUIRE(plain_finding.has_value());
  CHECK(plain_finding->status == Status::fail);  // 6ms past the declared 5ms, unwidened (neither side estimated)
}

TEST_CASE("compare_tol D-16 generalised override: a non-numeric adjusted_magnitude leaves the raw magnitude in "
          "place rather than throwing or coercing (Test 9)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  // Both sides declare span_basis=adjusted, but the candidate's own
  // adjusted_magnitude is a STRING, not a number -- CR-03's own "never a
  // fabricated verdict" discipline, applied to a crafted/malformed
  // snapshot's evidence rather than its Measurement::value.
  const Measurement baseline =
      span_basis_measurement(/*raw_ms=*/0, mediadiff::kDriftAdjustedMagnitudeDen * 5, "adjusted");
  const Measurement candidate = span_basis_measurement(/*raw_ms=*/6, nlohmann::ordered_json("not-a-number"), "adjusted");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  // The override never fires (candidate's own preference does not parse),
  // so this is an ordinary raw-value comparison: 6ms past the declared
  // 5ms, unwidened -- never a crash, never a coerced 0.
  CHECK(finding->status == Status::fail);
}

// --- 06-08-PLAN.md Task 2 (AUDIO-06): the THIRD generic, evidence-shape-
// driven override in this same family -- the asymmetric ceiling escalation
// (`ceiling_state` == "under"/"above"), exercised here against the SAME
// SYNTHETIC "t.synthetic_tol_widen" id the D-16 block above uses (neither
// `audio.loudness.true_peak` nor any other registered id), which is what
// makes "driven by evidence shape rather than check.id" testable rather
// than merely asserted (Test 8 from that task's own <behavior> block). The
// real end-to-end proof against audio_peak_under.flac/audio_peak_over.flac
// lives in tests/integration/test_audio_loudness.cpp; what a hand-built
// Measurement pair proves that a real fixture pair cannot is Test 5's own
// claim that the escalation fires "regardless of whether the magnitude
// delta fit the tolerance" -- the real fixture pair's ~1.5dB delta already
// exceeds its own 0.3dB tolerance on magnitude alone, so it cannot isolate
// the escalation's own effect from the delta's. ---

namespace {

// A Measurement carrying ONLY the ceiling_state evidence key (never
// comparison_basis/span_basis) -- the raw value as Measurement::value
// (always `raw_ms`, mirroring `measurement_at` above), so a test can pick a
// delta that would OTHERWISE pass or fail the declared tolerance
// independent of the escalation, and observe the escalation's own effect
// in isolation.
Measurement ceiling_state_measurement(std::int64_t raw_ms, const std::string& ceiling_state) {
  Measurement m = measurement_at(raw_ms, /*estimated=*/false);
  m.evidence = nlohmann::ordered_json{{"ceiling_state", ceiling_state}};
  return m;
}

}  // namespace

TEST_CASE("compare_tol ceiling escalation: baseline under, candidate above escalates to fail even when the delta "
          "alone is WITHIN the declared tolerance, on a SYNTHETIC non-loudness check id (Test 5, Test 8)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  REQUIRE(check.id == "t.synthetic_tol_widen");  // never audio.loudness.true_peak -- proves evidence-shape-driven
  // A 2ms delta is comfortably WITHIN the declared 5ms tolerance -- absent
  // the escalation, this pair would report Status::pass. Only the
  // ceiling_state transition decides the verdict here.
  const Measurement baseline = ceiling_state_measurement(/*raw_ms=*/0, "under");
  const Measurement candidate = ceiling_state_measurement(/*raw_ms=*/2, "above");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::fail);
  CHECK(finding->message.find("asymmetric ceiling crossing") != std::string::npos);
}

TEST_CASE("compare_tol ceiling escalation: baseline above, candidate under does NOT escalate -- headroom gained is "
          "not a regression (Test 6)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  // Same 2ms delta, WITHIN tolerance either way -- the reverse transition
  // must leave the ordinary tolerance verdict (pass) untouched.
  const Measurement baseline = ceiling_state_measurement(/*raw_ms=*/0, "above");
  const Measurement candidate = ceiling_state_measurement(/*raw_ms=*/2, "under");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
  CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
}

TEST_CASE("compare_tol ceiling escalation: both sides already above the ceiling compare on tolerance alone -- an "
          "unchanged hot pair is not a finding (Test 7)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  // Identical magnitude (0ms delta) AND identical ceiling_state="above" on
  // both sides -- no transition at all, so this must report Status::pass
  // on tolerance alone, never escalated.
  const Measurement baseline = ceiling_state_measurement(/*raw_ms=*/10, "above");
  const Measurement candidate = ceiling_state_measurement(/*raw_ms=*/10, "above");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
  CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
}

TEST_CASE("compare_tol ceiling escalation: both sides already under the ceiling never escalates, even with a "
          "beyond-tolerance delta",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  // Both "under" -- no transition -- ordinary tolerance verdict applies: a
  // 20ms delta well beyond the declared 5ms reports fail on magnitude
  // alone, with NO escalation suffix (this is not the P0 case the
  // escalation exists to catch).
  const Measurement baseline = ceiling_state_measurement(/*raw_ms=*/0, "under");
  const Measurement candidate = ceiling_state_measurement(/*raw_ms=*/20, "under");

  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::fail);
  CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
}

TEST_CASE("compare_tol ceiling escalation: either side missing or misspelling ceiling_state leaves the ordinary "
          "tolerance verdict untouched (opt-in via evidence shape, never gated on check.id)",
          "[tolerance]") {
  const CheckDef check = make_tol_check("5ms", Severity::fail);
  {
    // Baseline declares no evidence at all; candidate declares "above" --
    // EITHER side missing the key means the pair never qualifies.
    const Measurement baseline = measurement_at(0, /*estimated=*/false);
    const Measurement candidate = ceiling_state_measurement(/*raw_ms=*/2, "above");
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);  // 2ms within the declared 5ms, ordinary verdict
    CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
  }
  {
    // A misspelled/unexpected ceiling_state value on one side ("unknown",
    // never "under"/"above") never satisfies the transition either.
    const Measurement baseline = ceiling_state_measurement(/*raw_ms=*/0, "unknown");
    const Measurement candidate = ceiling_state_measurement(/*raw_ms=*/2, "above");
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);
    CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
  }
}

// --- CR-04 gap closure (06-17-PLAN.md, 06-REVIEW.md/VERIFICATION.md gap
// 3, AUDIO-06): the deadband that gates the asymmetric ceiling escalation
// above so a knife-edge quantiser-noise crossing (the review's own 0.0002
// dB example) does not hard-fail despite the check's own 0.3 dB tolerance,
// while every crossing of 0.010 dB or more -- including one comfortably
// INSIDE that tolerance (SC3) -- still fails. Exercised against the REAL
// `audio.loudness.true_peak` CheckDef from `builtin_registry()` (unit
// `db`, tolerance `0.3dB`, severity `warn`), never a synthetic id -- this
// is the actual production check the gap was found on. Measurements are
// shaped exactly like `emit_true_peak`'s own output
// (`src/analyzers/audio/loudness.cpp`): `RationalValue{milli,
// kLoudnessQuantiserDen, Rational{1,1}}` plus a `ceiling_state` evidence
// key derived the same way (`milli >= -1000` -> "above", else "under"). ---

namespace {

CheckDef true_peak_check() {
  const CheckRegistry& registry = builtin_registry();
  auto index = registry.find("audio.loudness.true_peak");
  REQUIRE(index.has_value());
  return registry.at(*index);
}

// `milli` is the quantised dBTP value at `kLoudnessQuantiserDen` (e.g.
// -1000 == -1.000 dBTP, the named ceiling) -- mirrors `emit_true_peak`'s
// own `RationalValue` shape and its own `ceiling_state` derivation
// (`milli >= -1000` -> "above") exactly, so a hand-built pair here is
// indistinguishable, from `compare_tol`'s point of view, from a pair
// `audio_loudness_analyzer()` actually emitted.
Measurement true_peak_measurement(std::int64_t milli) {
  Measurement m;
  m.check_index = 0;
  m.scope = Scope{Scope::Kind::global, 0};
  m.value = mediadiff::Value{RationalValue{milli, kLoudnessQuantiserDen, Rational{1, 1}}};
  m.evidence = nlohmann::ordered_json{{"ceiling_state", milli >= -1000 ? "above" : "under"}};
  return m;
}

}  // namespace

TEST_CASE("compare_tol ceiling escalation: a rise below the 0.010 dB deadband does not escalate on the real "
          "audio.loudness.true_peak check (CR-04)",
          "[tolerance]") {
  const CheckDef check = true_peak_check();
  REQUIRE(check.id == "audio.loudness.true_peak");  // the real check, not a synthetic stand-in

  {
    // A 1 milli-dB rise (-1.001 -> -1.000 dBTP) -- the review's own
    // knife-edge class. Before this plan's fix: Status::fail (RED, this
    // plan's SUMMARY records the pre-fix value).
    const Measurement baseline = true_peak_measurement(-1001);
    const Measurement candidate = true_peak_measurement(-1000);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);
    CHECK(finding->message.find("deadband") != std::string::npos);
    CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
  }
  {
    // A 9 milli-dB rise (-1.005 -> -0.996 dBTP) -- still under the 10
    // milli-dB deadband.
    const Measurement baseline = true_peak_measurement(-1005);
    const Measurement candidate = true_peak_measurement(-996);
    auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
    REQUIRE(finding.has_value());
    CHECK(finding->status == Status::pass);
    CHECK(finding->message.find("deadband") != std::string::npos);
    CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
  }
}

TEST_CASE("compare_tol ceiling escalation: a rise of exactly the deadband escalates on the real "
          "audio.loudness.true_peak check (CR-04)",
          "[tolerance]") {
  const CheckDef check = true_peak_check();
  REQUIRE(kCeilingCrossingDeadbandNum == 10);
  REQUIRE(kCeilingCrossingDeadbandDen == 1000);
  // A 10 milli-dB rise (-1.005 -> -0.995 dBTP) -- exactly the deadband's
  // own boundary (>=), so this still escalates.
  const Measurement baseline = true_peak_measurement(-1005);
  const Measurement candidate = true_peak_measurement(-995);
  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::fail);
  CHECK(finding->message.find("asymmetric ceiling crossing") != std::string::npos);
}

TEST_CASE("compare_tol ceiling escalation: a material crossing inside the 0.3 dB tolerance still fails on the real "
          "audio.loudness.true_peak check (SC3, CR-04)",
          "[tolerance]") {
  const CheckDef check = true_peak_check();
  // A 100 milli-dB (0.1 dB) rise (-1.050 -> -0.950 dBTP) -- well over the
  // 10 milli-dB deadband, and comfortably INSIDE the check's own declared
  // 0.3 dB (300 milli-dB) tolerance. SC3/AUDIO-06's own contract: the
  // escalation fires regardless of the declared tolerance, so this must
  // still fail.
  const Measurement baseline = true_peak_measurement(-1050);
  const Measurement candidate = true_peak_measurement(-950);
  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::fail);
  CHECK(finding->message.find("asymmetric ceiling crossing") != std::string::npos);
}

TEST_CASE("compare_tol ceiling escalation: the reverse transition (above -> under) never escalates and never "
          "carries a deadband suffix, on the real audio.loudness.true_peak check (CR-04)",
          "[tolerance]") {
  const CheckDef check = true_peak_check();
  // -0.995 -> -1.005 dBTP: headroom gained, not lost -- must stay exactly
  // as before this plan (no escalation, no deadband message either, since
  // the deadband gate only exists to soften an upward-crossing escalation
  // that would otherwise have fired).
  const Measurement baseline = true_peak_measurement(-995);
  const Measurement candidate = true_peak_measurement(-1005);
  auto finding = compare_tol(check, baseline, candidate, kWidenPolicy);
  REQUIRE(finding.has_value());
  CHECK(finding->status == Status::pass);
  CHECK(finding->message.find("asymmetric ceiling crossing") == std::string::npos);
  CHECK(finding->message.find("deadband") == std::string::npos);
}
