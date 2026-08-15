#include "compare/semantics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <fmt/format.h>

#include "core/profiles.h"

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
      // D-15 / "trust never requires faith": an ignored check's difference
      // is still computed and surfaced, never silently dropped into pass.
      return Status::info;
  }
  return Status::info;
}

// Derives the WIDTHxHEIGHT dimensions the `transform` profile's expectation
// converts a check's baseline into: the absolute pair directly for the
// absolute form, or the baseline multiplied by the exact scale rational for
// the scale-factor form -- requiring an exact integer result on both axes
// (doc 01 section 5, ENG-10). `baseline_text` is only consulted for the
// scale-factor form; the absolute form ignores the baseline entirely, which
// is the whole point of declaring an absolute expectation.
mediadiff::expected<Dimensions, Error> derive_transform_target(const ResolutionExpectation& expectation,
                                                                  const std::string& raw_expectation,
                                                                  const std::string* baseline_text) {
  if (!expectation.is_scale_factor) {
    return expectation.absolute;
  }
  if (baseline_text == nullptr) {
    return mediadiff::unexpected(
        Error{ErrorKind::internal, "transform-affected check's baseline value is not a resolution string"});
  }
  const std::optional<Dimensions> baseline_dims = parse_dimensions(*baseline_text);
  if (!baseline_dims.has_value()) {
    return mediadiff::unexpected(Error{
        ErrorKind::internal, "transform-affected check's baseline value is not a \"WIDTHxHEIGHT\" resolution string"});
  }
  const std::int64_t width_num = baseline_dims->width * expectation.scale_num;
  const std::int64_t height_num = baseline_dims->height * expectation.scale_num;
  if (width_num % expectation.scale_den != 0 || height_num % expectation.scale_den != 0) {
    return mediadiff::unexpected(Error{
        ErrorKind::usage, fmt::format("transform expectation '{}' applied to baseline resolution {}x{} does not "
                                       "derive an exact integer resolution",
                                       raw_expectation, baseline_dims->width, baseline_dims->height)});
  }
  return Dimensions{width_num / expectation.scale_den, height_num / expectation.scale_den};
}

}  // namespace

// compare_exact: doc 01 section 3's `exact` semantic -- pass iff the two
// values are equal after per-check normalization. This plan performs no
// normalization (pix_fmt range-folding etc. is a later analyzer's concern,
// doc 03) -- it compares the decoded Value directly via Value's own
// structural operator== (core/value.h), UNLESS the `transform` profile is
// active, the check carries `transform_affected`, and the policy declares a
// resolution expectation (doc 01 section 5, ENG-10) -- in which case the
// candidate is compared against the value the expectation derives from the
// baseline instead of against the baseline itself. Every other
// configuration (a different profile, a check without the flag, or a
// `transform` policy with no declared expectation) falls straight through
// to the ordinary baseline-equality path -- the transform path is additive,
// never a change to any other check's behavior.
mediadiff::expected<Finding, Error> compare_exact(const CheckDef& check, const Measurement& baseline,
                                                    const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;

  const Severity severity = resolve_severity(check, policy);
  finding.severity = severity;

  const bool transform_active =
      policy.profile == ProfileId::transform && check.transform_affected && policy.transform_expectation.resolution.has_value();

  if (transform_active) {
    const std::string& raw_expectation = *policy.transform_expectation.resolution;
    auto expectation = parse_resolution_expectation(raw_expectation);
    if (!expectation) {
      return mediadiff::unexpected(expectation.error());
    }

    const auto* baseline_str = std::get_if<std::string>(&baseline.value);
    auto target = derive_transform_target(*expectation, raw_expectation, baseline_str);
    if (!target) {
      return mediadiff::unexpected(target.error());
    }

    const auto* candidate_str = std::get_if<std::string>(&candidate.value);
    const std::optional<Dimensions> candidate_dims =
        candidate_str != nullptr ? parse_dimensions(*candidate_str) : std::nullopt;

    if (candidate_dims.has_value() && *candidate_dims == *target) {
      finding.status = Status::pass;
      finding.message = fmt::format("candidate resolution matches derived expectation {}x{}", target->width, target->height);
      return finding;
    }

    finding.status = escalate(severity);
    finding.message =
        fmt::format("candidate resolution does not match derived expectation {}x{}", target->width, target->height);
    return finding;
  }

  if (baseline.value == candidate.value) {
    finding.status = Status::pass;
    finding.message = "values match";
    return finding;
  }

  finding.status = escalate(severity);
  finding.message = "values differ";
  return finding;
}

// comparator_for: all seven Semantic enumerators dispatch to a real
// comparator as of plan 02-04 — a switch with no default: arm, so a future
// Semantic added without a matching case is a -Wswitch (-Werror
// project-wide) compile failure, not a silent gap.
Comparator comparator_for(Semantic semantic) {
  switch (semantic) {
    case Semantic::exact:
      return &compare_exact;
    case Semantic::tol:
      return &compare_tol;
    case Semantic::set:
      return &compare_set;
    case Semantic::presence:
      return &compare_presence;
    case Semantic::hash:
      return &compare_hash;
    case Semantic::dist:
      return &compare_dist;
    case Semantic::span:
      return &compare_span;
  }
  // Unreachable for any valid Semantic — see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return &compare_exact;
}

}  // namespace mediadiff
