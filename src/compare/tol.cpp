#include "compare/semantics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>

#include "analyzers/timeline/analyzers.h"
#include "core/exact_int.h"
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

// D-03 (03-CONTEXT.md): TS interval measurements derived from a mux-rate
// estimate compare under a wider tolerance than a directly measured value,
// so estimation noise in e.g. container.ts.pcr_interval's byte-offset ->
// time conversion cannot fabricate a false regression (false positives are
// P0). 3x on pcr_interval's 100ms default yields 300ms, still well under
// psi_interval's own 500ms bound -- a real spacing regression still fires
// while estimation noise does not. A single named constant, never a
// double: the multiply below is exact integer arithmetic
// (core/exact_int.h's detail::ExactInt).
constexpr std::int64_t kEstimatedToleranceFactor = 3;

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

  auto baseline_mag = extract_magnitude(baseline.value);
  auto candidate_mag = extract_magnitude(candidate.value);
  if (!baseline_mag.has_value() || !candidate_mag.has_value()) {
    // D-09 already guarantees both sides hold the check's declared
    // value_kind (or Absent) -- reaching here means a tol check declared a
    // value_kind this comparator does not support (only rational and
    // int64 are), which is a registry-authoring bug, not a runtime input.
    return mediadiff::unexpected(Error{ErrorKind::internal, "tol comparator received an unsupported value kind"});
  }

  // D-10 (05-09-PLAN.md, timeline.av_offset) -- Rule 2 addition, not named
  // in that plan's own `files_modified`: D-10's "adjusted only when both
  // sides know their own priming, raw-to-raw when either does not" rule is
  // inherently a cross-Measurement decision (which side's magnitude to
  // compare depends on BOTH sides' own evidence, not on either
  // Measurement's value alone), so it cannot live entirely inside a
  // per-file analyzer the way every other check in this project is
  // written. This is a GENERIC, evidence-shape-driven override -- never
  // gated on `check.id` -- mirroring the `estimated` flag's own precedent
  // just below (D-03: a Measurement-level flag this SAME comparator
  // already reads from both sides to change how it compares). A check's
  // Measurement::value always holds the RAW/unadjusted magnitude -- the
  // well-defined, single-side-computable default every other `tol` check's
  // own evidence shape leaves untouched (a check with no `comparison_basis`/
  // `adjusted_offset_ms` evidence keys never triggers this branch at all).
  // Only when BOTH sides declare `"comparison_basis": "adjusted"` (each
  // side's OWN preference, set by the analyzer that populated it) AND both
  // carry a numeric `"adjusted_offset_ms"` does the compared magnitude swap
  // to that adjusted value on BOTH sides -- any other combination (either
  // side missing the keys, or either side reporting `"raw"`) leaves the RAW
  // magnitude from Measurement::value in place, which is exactly
  // raw-to-raw.
  const auto side_prefers_adjusted_magnitude = [](const Measurement& side) {
    return side.evidence.is_object() && side.evidence.value("comparison_basis", std::string()) == "adjusted" &&
           side.evidence.contains("adjusted_offset_ms") &&
           side.evidence.at("adjusted_offset_ms").is_number_integer();
  };
  if (side_prefers_adjusted_magnitude(baseline) && side_prefers_adjusted_magnitude(candidate)) {
    baseline_mag->num = baseline.evidence.at("adjusted_offset_ms").get<std::int64_t>();
    candidate_mag->num = candidate.evidence.at("adjusted_offset_ms").get<std::int64_t>();
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
  // verdict depending on optimization. The D-07 gate's int64_t
  // subtraction below goes through core/rational.h's overflow-checked
  // detail::checked_sub/checked_negate, and on overflow this comparator
  // returns a real Finding carrying Status::error, mirroring
  // compare/engine.cpp's value_kind_mismatch "never a coercion, never a
  // fabricated verdict" contract (D-09) rather than computing a UB-tainted
  // result. The magnitude cross-multiplications further down no longer
  // need that escape hatch: they are exact (detail::ExactInt, see the
  // comment at the delta computation), so no int64_t input can make them
  // wrap or overflow.
  const auto overflow_finding = [&](std::string_view what) {
    finding.status = Status::error;
    finding.skip_reason = SkipReason::none;
    finding.message =
        fmt::format("tol comparator: {} overflowed int64_t during cross-multiplication; cannot determine a verdict",
                     what);
    return finding;
  };

  // D-07 (05-10-PLAN.md, timeline.av_drift) -- Rule 2 addition, not named
  // in that plan's own `files_modified`, mirroring D-10's own precedent
  // immediately above in SHAPE (a GENERIC, evidence-shape-driven
  // override, never gated on `check.id` -- a check with no
  // `end_delta_ms` evidence key on BOTH sides never triggers this branch
  // at all), but DELTA-based rather than a per-side magnitude test, for
  // the same reason every OTHER comparison in this file is delta-based
  // (the RATE itself, immediately below, is `candidate - baseline`, never
  // an absolute magnitude on either side alone): `timeline.av_drift`
  // gates on the fitted RATE only when the ACCUMULATED end delta CHANGE
  // between baseline and candidate also clears the SAME 2ms epsilon
  // `timeline.av_drift.pattern`'s own classifier uses (kDriftEpsilonMs,
  // analyzers/timeline/analyzers.h -- ONE constant, never a second,
  // independently-tuned copy). A per-side (rather than delta) test was
  // tried first and rejected during this task's own execution (recorded
  // in 05-10-SUMMARY.md): requiring BOTH sides' own end delta to
  // independently clear the epsilon can never fire when baseline is a
  // clean reference (end_delta ~ 0, the common case), which would make
  // the flagship check structurally unable to catch a real regression
  // against a clean baseline -- exactly backwards from D-07's own intent.
  // Reason (D-07's own worked coincidence): a fitted rate is an
  // EXTRAPOLATION over the file's own span -- on a short clip at a fine
  // timebase, packet-timestamp rounding alone can push it past the
  // 0.2ms/min tolerance with no real drift present on EITHER side. The
  // accumulated end delta is a directly MEASURED quantity, not an
  // extrapolation, so its OWN cross-side delta stays small when the
  // "drift" is really rounding noise on both sides. 0.2ms/min sustained
  // over the 10-minute reference file this tolerance was calibrated
  // against is exactly 2ms of ACCUMULATED delta relative to a
  // zero-drift baseline -- where the two thresholds coincide. `abs()`
  // routes through `detail::checked_negate` for the SAME CR-03 reason as
  // every other magnitude in this file: `end_delta_ms` is read from an
  // untrusted snapshot too.
  const auto side_end_delta_ms = [](const Measurement& side) -> std::optional<std::int64_t> {
    if (!side.evidence.is_object() || !side.evidence.contains("end_delta_ms") ||
        !side.evidence.at("end_delta_ms").is_number_integer()) {
      return std::nullopt;
    }
    return side.evidence.at("end_delta_ms").get<std::int64_t>();
  };
  const std::optional<std::int64_t> baseline_end_delta_ms = side_end_delta_ms(baseline);
  const std::optional<std::int64_t> candidate_end_delta_ms = side_end_delta_ms(candidate);
  const bool has_end_delta_gate = baseline_end_delta_ms.has_value() && candidate_end_delta_ms.has_value();
  bool end_delta_clears_epsilon = true;  // No gate evidence -- never suppresses the verdict.
  if (has_end_delta_gate) {
    std::int64_t end_delta_change = 0;
    if (!detail::checked_sub(*candidate_end_delta_ms, *baseline_end_delta_ms, &end_delta_change)) {
      return overflow_finding("end_delta_change (D-07 gate)");
    }
    std::int64_t abs_end_delta_change = end_delta_change;
    if (abs_end_delta_change < 0 && !detail::checked_negate(abs_end_delta_change, &abs_end_delta_change)) {
      return overflow_finding("abs_end_delta_change (D-07 gate)");
    }
    end_delta_clears_epsilon = abs_end_delta_change >= kDriftEpsilonMs;
  }
  // Applied at every return point below, after `finding.status`/`finding.
  // message` are set: downgrades a non-pass verdict to `pass` when the
  // gate above is present and did not clear -- never touches an already-
  // passing verdict (a no-op there), and never fires at all when
  // `has_end_delta_gate` is false (the check declared no such evidence).
  const auto apply_end_delta_gate = [&]() {
    if (!end_delta_clears_epsilon && finding.status != Status::pass) {
      finding.status = Status::pass;
      finding.message += " (D-07: end delta change below 2ms epsilon, rate delta ignored)";
    }
  };

  // delta = candidate - baseline, as an exact rational over
  // baseline_den*candidate_den -- cross-multiplication, never a division.
  //
  // Every product and difference from here to the verdict is EXACT
  // (detail::ExactInt, core/exact_int.h -- a 256-bit portable integer), not
  // int64_t. Debug session test-898-ci-nonreproducible: the int64_t
  // version returned `Status::error` on ordinary media -- timeline.
  // av_drift's own reduced rates (e.g. 270183060000/47612048 vs
  // 24030060000/36327640 ms/min on a five-second TS splice) cross-multiply
  // to ~9.8e18, past INT64_MAX. Wherever the int64_t computation did NOT
  // overflow, the exact one produces the identical numbers (so every such
  // verdict and message is unchanged); where it did, the comparator now
  // returns the real verdict. CR-03's contract (a crafted near-INT64_MAX
  // snapshot value must never yield a UB-tainted verdict) is kept: nothing
  // here can wrap, and the bound analysis in core/exact_int.h shows 256
  // bits covers every int64_t input -- the range_finding paths below are
  // defensive only and unreachable for int64_t operands.
  const auto range_finding = [&](std::string_view what) {
    finding.status = Status::error;
    finding.skip_reason = SkipReason::none;
    finding.message = fmt::format(
        "tol comparator: {} exceeded the comparator's 256-bit exact range; cannot determine a verdict", what);
    return finding;
  };
  using detail::ExactInt;
  const ExactInt baseline_num = ExactInt::from_i64(baseline_mag->num);
  const ExactInt baseline_den = ExactInt::from_i64(baseline_mag->den);
  const ExactInt candidate_num = ExactInt::from_i64(candidate_mag->num);
  const ExactInt candidate_den = ExactInt::from_i64(candidate_mag->den);

  ExactInt delta_den;
  if (!ExactInt::try_mul(baseline_den, candidate_den, &delta_den)) {
    return range_finding("delta_den (baseline_den * candidate_den)");
  }
  ExactInt delta_num_lhs;
  ExactInt delta_num_rhs;
  if (!ExactInt::try_mul(candidate_num, baseline_den, &delta_num_lhs) ||
      !ExactInt::try_mul(baseline_num, candidate_den, &delta_num_rhs)) {
    return range_finding("delta_num (num * den cross-products)");
  }
  ExactInt delta_num;
  if (!ExactInt::try_sub(delta_num_lhs, delta_num_rhs, &delta_num)) {
    return range_finding("delta_num (cross-product subtraction)");
  }
  const ExactInt abs_delta_num = delta_num.abs();

  // D-03: either side carrying `estimated` widens the effective threshold
  // magnitudes by kEstimatedToleranceFactor -- exact integer
  // multiplication, never a double conversion, and (being exact) never a
  // silent fallback to the unwidened value either.
  const bool widened = baseline.estimated || candidate.estimated;
  ExactInt effective_num = ExactInt::from_i64(tolerance->num);
  std::optional<ExactInt> effective_warn_num;
  if (tolerance->warn_num.has_value()) {
    effective_warn_num = ExactInt::from_i64(*tolerance->warn_num);
  }
  if (widened) {
    const ExactInt factor = ExactInt::from_i64(kEstimatedToleranceFactor);
    if (!ExactInt::try_mul(effective_num, factor, &effective_num)) {
      return range_finding("estimated-measurement tolerance widening (fail threshold * 3)");
    }
    if (effective_warn_num.has_value() && !ExactInt::try_mul(*effective_warn_num, factor, &*effective_warn_num)) {
      return range_finding("estimated-measurement tolerance widening (warn threshold * 3)");
    }
  }

  const ExactInt tolerance_den = ExactInt::from_i64(tolerance->den);
  bool within_fail = false;
  bool within_warn = false;
  if (tolerance->is_relative) {
    // |delta| * 100 * baseline_den <= percent_num * |baseline| (doc 01
    // section 3's own formula), generalized with an extra candidate_den
    // factor so it stays exact even when the two sides' denominators
    // differ -- derivation recorded in 02-04-SUMMARY.md.
    const ExactInt abs_baseline_num = baseline_num.abs();
    ExactInt lhs;
    if (!ExactInt::try_mul(abs_delta_num, tolerance_den, &lhs) ||
        !ExactInt::try_mul(lhs, ExactInt::from_i64(100), &lhs)) {
      return range_finding("relative-tolerance lhs (|delta| * tolerance_den * 100)");
    }
    ExactInt rhs;
    if (!ExactInt::try_mul(effective_num, abs_baseline_num, &rhs) || !ExactInt::try_mul(rhs, candidate_den, &rhs)) {
      return range_finding("relative-tolerance rhs (tolerance_num * |baseline| * candidate_den)");
    }
    within_fail = ExactInt::compare(lhs, rhs) <= 0;
    if (effective_warn_num.has_value()) {
      ExactInt rhs_warn;
      if (!ExactInt::try_mul(*effective_warn_num, abs_baseline_num, &rhs_warn) ||
          !ExactInt::try_mul(rhs_warn, candidate_den, &rhs_warn)) {
        return range_finding("relative-tolerance warn rhs (warn_num * |baseline| * candidate_den)");
      }
      within_warn = ExactInt::compare(lhs, rhs_warn) <= 0;
    }
  } else {
    ExactInt lhs;
    if (!ExactInt::try_mul(abs_delta_num, tolerance_den, &lhs)) {
      return range_finding("absolute-tolerance lhs (|delta| * tolerance_den)");
    }
    ExactInt rhs;
    if (!ExactInt::try_mul(effective_num, delta_den, &rhs)) {
      return range_finding("absolute-tolerance rhs (tolerance_num * delta_den)");
    }
    within_fail = ExactInt::compare(lhs, rhs) <= 0;
    if (effective_warn_num.has_value()) {
      ExactInt rhs_warn;
      if (!ExactInt::try_mul(*effective_warn_num, delta_den, &rhs_warn)) {
        return range_finding("absolute-tolerance warn rhs (warn_num * delta_den)");
      }
      within_warn = ExactInt::compare(lhs, rhs_warn) <= 0;
    }
  }
  // Rendered operands for the messages below -- decimal strings identical
  // to the former int64_t formatting whenever the values fit int64_t.
  const std::string abs_delta_num_text = abs_delta_num.to_decimal();
  const std::string delta_den_text = delta_den.to_decimal();

  const std::string_view unit_text = unit_suffix(check.unit);
  // D-03: appended to every message below when the widened threshold was
  // actually used for this comparison, so a reader sees why an otherwise
  // out-of-tolerance delta was let through.
  const std::string widened_suffix =
      widened ? fmt::format(" (estimated measurement, tolerance widened {}x)", kEstimatedToleranceFactor) : "";

  if (effective_warn_num.has_value()) {
    // Two-threshold form: the zone the delta falls in decides the status,
    // independent of the check's own severity (doc 01 section 3).
    if (within_warn) {
      finding.status = Status::pass;
      finding.message = fmt::format("delta {}{}/{}{} within warn threshold{}", sign, abs_delta_num_text, delta_den_text,
                                     tolerance->is_relative ? "%" : std::string(unit_text), widened_suffix);
    } else if (within_fail) {
      finding.status = Status::warn;
      finding.message = fmt::format("delta {}{}/{}{} between warn and fail thresholds{}", sign, abs_delta_num_text,
                                     delta_den_text, tolerance->is_relative ? "%" : std::string(unit_text),
                                     widened_suffix);
    } else {
      finding.status = Status::fail;
      finding.message = fmt::format("delta {}{}/{}{} beyond fail threshold{}", sign, abs_delta_num_text, delta_den_text,
                                     tolerance->is_relative ? "%" : std::string(unit_text), widened_suffix);
    }
    apply_end_delta_gate();
    return finding;
  }

  if (within_fail) {
    finding.status = Status::pass;
    finding.message = "delta within tolerance" + widened_suffix;
    return finding;
  }

  finding.status = escalate(finding.severity);
  finding.message = fmt::format("delta {}{}/{}{} exceeds tolerance{}", sign, abs_delta_num_text, delta_den_text,
                                 tolerance->is_relative ? "%" : std::string(unit_text), widened_suffix);
  apply_end_delta_gate();
  return finding;
}

}  // namespace mediadiff
