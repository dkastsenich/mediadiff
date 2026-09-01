#include "compare/semantics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>

#include "core/rational.h"
#include "core/tolerance.h"

namespace mediadiff {

namespace {

Status escalate(Severity severity) {
  switch (severity) {
    case Severity::fail:
      return Status::fail;
    case Severity::warn:
      return Status::warn;
    case Severity::info:
    case Severity::ignore:
      return Status::info;
  }
  return Status::info;
}

struct Magnitude {
  std::int64_t num;
  std::int64_t den;
  Rational tb;
};

std::optional<Magnitude> extract_magnitude(const Value& value) {
  if (const auto* r = std::get_if<RationalValue>(&value)) {
    return Magnitude{r->num, r->den, r->tb};
  }
  if (const auto* i = std::get_if<std::int64_t>(&value)) {
    return Magnitude{*i, 1, Rational{1, 1}};
  }
  return std::nullopt;
}

}  // namespace

// compare_tol: doc 01 section 3's `±tol` semantic. This engine layer has
// no real analyzer feeding it yet (02-CONTEXT.md D-10 -- Phase 2 proves
// the engine without media), so this comparator's own contract for a
// RationalValue is: `num` is the measured quantity already expressed in
// the check's declared unit (`den` divides it exactly -- e.g. den=10 for
// one fractional digit), and `tb` is used only to order the two sides via
// core/rational.h's compare_ticks, which renders the delta's sign -- it is
// not used to rescale the magnitude itself. A future analyzer whose two
// sides genuinely record differing timebases will need this comparator
// extended to cross-multiply through both `tb`s before the magnitude
// check; flagged as a follow-up in 02-04-SUMMARY.md, not required by this
// plan's own test registry (every `tol` check declares unit=ms with
// identity-tb fixtures). No code path here ever converts a tick count (or
// any magnitude) to double or float (ENG-05) -- every comparison below is
// integer cross-multiplication.
mediadiff::expected<Finding, Error> compare_tol(const CheckDef& check, const Measurement& baseline,
                                                  const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;
  finding.severity = resolve_severity(check, policy);

  auto tolerance = parse_tolerance(check.tolerance_for(policy.profile), check.unit);
  if (!tolerance) {
    return mediadiff::unexpected(tolerance.error());
  }

  const auto baseline_mag = extract_magnitude(baseline.value);
  const auto candidate_mag = extract_magnitude(candidate.value);
  if (!baseline_mag.has_value() || !candidate_mag.has_value()) {
    // D-09 already guarantees both sides hold the check's declared
    // value_kind (or Absent) -- reaching here means a tol check declared a
    // value_kind this comparator does not support (only rational and
    // int64 are), which is a registry-authoring bug, not a runtime input.
    return mediadiff::unexpected(Error{ErrorKind::internal, "tol comparator received an unsupported value kind"});
  }

  // Sign only, purely for rendering "+"/"-" on the delta -- the magnitude
  // comparison below is entirely separate integer cross-multiplication.
  const int order =
      compare_ticks(Ticks{candidate_mag->num, candidate_mag->tb}, Ticks{baseline_mag->num, baseline_mag->tb});
  const std::string_view sign = order > 0 ? "+" : (order < 0 ? "-" : "");

  // CR-03: baseline_mag/candidate_mag's num/den are int64 magnitudes read
  // directly from an untrusted snapshot (core/serializer.cpp's
  // value_from_json validates TYPE, never MAGNITUDE) -- a crafted
  // near-INT64_MAX num/den previously triggered signed integer overflow
  // (UB) in a plain `*`/`-`, which can produce an arbitrary pass/warn/fail
  // verdict depending on optimization. Every cross-multiplication and
  // subtraction below is routed through core/rational.h's
  // detail::checked_mul/checked_sub/checked_negate -- the SAME
  // overflow-checked primitives compare_ticks above already uses -- and on
  // overflow this comparator returns a real Finding carrying
  // Status::error, mirroring compare/engine.cpp's value_kind_mismatch
  // "never a coercion, never a fabricated verdict" contract (D-09) rather
  // than computing a UB-tainted result.
  const auto overflow_finding = [&](std::string_view what) {
    finding.status = Status::error;
    finding.skip_reason = SkipReason::none;
    finding.message =
        fmt::format("tol comparator: {} overflowed int64_t during cross-multiplication; cannot determine a verdict",
                     what);
    return finding;
  };

  // delta = candidate - baseline, as an exact rational over
  // baseline_den*candidate_den -- cross-multiplication, never a division.
  std::int64_t delta_den = 0;
  if (!detail::checked_mul(baseline_mag->den, candidate_mag->den, &delta_den)) {
    return overflow_finding("delta_den (baseline_den * candidate_den)");
  }
  std::int64_t delta_num_lhs = 0;
  std::int64_t delta_num_rhs = 0;
  if (!detail::checked_mul(candidate_mag->num, baseline_mag->den, &delta_num_lhs) ||
      !detail::checked_mul(baseline_mag->num, candidate_mag->den, &delta_num_rhs)) {
    return overflow_finding("delta_num (num * den cross-products)");
  }
  std::int64_t delta_num = 0;
  if (!detail::checked_sub(delta_num_lhs, delta_num_rhs, &delta_num)) {
    return overflow_finding("delta_num (cross-product subtraction)");
  }
  std::int64_t abs_delta_num = 0;
  if (delta_num < 0) {
    if (!detail::checked_negate(delta_num, &abs_delta_num)) {
      return overflow_finding("abs_delta_num");
    }
  } else {
    abs_delta_num = delta_num;
  }

  bool within_fail = false;
  bool within_warn = false;
  if (tolerance->is_relative) {
    // |delta| * 100 * baseline_den <= percent_num * |baseline| (doc 01
    // section 3's own formula), generalized with an extra candidate_den
    // factor so it stays exact even when the two sides' denominators
    // differ -- derivation recorded in 02-04-SUMMARY.md.
    std::int64_t abs_baseline_num = 0;
    if (baseline_mag->num < 0) {
      if (!detail::checked_negate(baseline_mag->num, &abs_baseline_num)) {
        return overflow_finding("abs_baseline_num");
      }
    } else {
      abs_baseline_num = baseline_mag->num;
    }

    std::int64_t lhs = 0;
    if (!detail::checked_mul(abs_delta_num, tolerance->den, &lhs) || !detail::checked_mul(lhs, 100, &lhs)) {
      return overflow_finding("relative-tolerance lhs (|delta| * tolerance_den * 100)");
    }
    std::int64_t rhs = 0;
    if (!detail::checked_mul(tolerance->num, abs_baseline_num, &rhs) ||
        !detail::checked_mul(rhs, candidate_mag->den, &rhs)) {
      return overflow_finding("relative-tolerance rhs (tolerance_num * |baseline| * candidate_den)");
    }
    within_fail = lhs <= rhs;
    if (tolerance->warn_num.has_value()) {
      std::int64_t rhs_warn = 0;
      if (!detail::checked_mul(*tolerance->warn_num, abs_baseline_num, &rhs_warn) ||
          !detail::checked_mul(rhs_warn, candidate_mag->den, &rhs_warn)) {
        return overflow_finding("relative-tolerance warn rhs (warn_num * |baseline| * candidate_den)");
      }
      within_warn = lhs <= rhs_warn;
    }
  } else {
    std::int64_t lhs = 0;
    if (!detail::checked_mul(abs_delta_num, tolerance->den, &lhs)) {
      return overflow_finding("absolute-tolerance lhs (|delta| * tolerance_den)");
    }
    std::int64_t rhs = 0;
    if (!detail::checked_mul(tolerance->num, delta_den, &rhs)) {
      return overflow_finding("absolute-tolerance rhs (tolerance_num * delta_den)");
    }
    within_fail = lhs <= rhs;
    if (tolerance->warn_num.has_value()) {
      std::int64_t rhs_warn = 0;
      if (!detail::checked_mul(*tolerance->warn_num, delta_den, &rhs_warn)) {
        return overflow_finding("absolute-tolerance warn rhs (warn_num * delta_den)");
      }
      within_warn = lhs <= rhs_warn;
    }
  }

  const std::string_view unit_text = unit_suffix(check.unit);

  if (tolerance->warn_num.has_value()) {
    // Two-threshold form: the zone the delta falls in decides the status,
    // independent of the check's own severity (doc 01 section 3).
    if (within_warn) {
      finding.status = Status::pass;
      finding.message = fmt::format("delta {}{}/{}{} within warn threshold", sign, abs_delta_num, delta_den,
                                     tolerance->is_relative ? "%" : std::string(unit_text));
    } else if (within_fail) {
      finding.status = Status::warn;
      finding.message = fmt::format("delta {}{}/{}{} between warn and fail thresholds", sign, abs_delta_num,
                                     delta_den, tolerance->is_relative ? "%" : std::string(unit_text));
    } else {
      finding.status = Status::fail;
      finding.message = fmt::format("delta {}{}/{}{} beyond fail threshold", sign, abs_delta_num, delta_den,
                                     tolerance->is_relative ? "%" : std::string(unit_text));
    }
    return finding;
  }

  if (within_fail) {
    finding.status = Status::pass;
    finding.message = "delta within tolerance";
    return finding;
  }

  finding.status = escalate(finding.severity);
  finding.message = fmt::format("delta {}{}/{}{} exceeds tolerance", sign, abs_delta_num, delta_den,
                                 tolerance->is_relative ? "%" : std::string(unit_text));
  return finding;
}

}  // namespace mediadiff
