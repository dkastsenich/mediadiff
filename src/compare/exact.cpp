#include "compare/semantics.h"

namespace mediadiff {

// compare_exact: doc 01 section 3's `exact` semantic — pass iff the two
// values are equal after per-check normalization. This plan performs no
// normalization (pix_fmt range-folding etc. is a later analyzer's concern,
// doc 03) — it compares the decoded Value directly via Value's own
// structural operator== (core/value.h).
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

  if (baseline.value == candidate.value) {
    finding.status = Status::pass;
    finding.message = "values match";
    return finding;
  }

  switch (severity) {
    case Severity::fail:
      finding.status = Status::fail;
      break;
    case Severity::warn:
      finding.status = Status::warn;
      break;
    case Severity::info:
    case Severity::ignore:
      // D-15 / "trust never requires faith": an ignored check's difference
      // is still computed and surfaced, never silently dropped into pass.
      finding.status = Status::info;
      break;
  }
  finding.message = "values differ";
  return finding;
}

// comparator_for: all seven Semantic enumerators dispatch to a real
// comparator as of this plan — a switch with no default: arm, so a future
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
