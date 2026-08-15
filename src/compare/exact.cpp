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

namespace {

// Registered (D-04's severity/tolerance table already accepts these
// semantics), dispatchable (the Comparator function-pointer type already
// covers them) — just not implemented yet. Plans 02-02 through 02-04 fill
// these in; until then a check declaring one of these semantics fails
// loudly with an internal Error rather than silently mis-comparing.
mediadiff::expected<Finding, Error> compare_unimplemented(const CheckDef&, const Measurement&, const Measurement&,
                                                            const Policy&) {
  return mediadiff::unexpected(Error{ErrorKind::internal, "comparator not yet implemented for this semantic"});
}

}  // namespace

Comparator comparator_for(Semantic semantic) {
  switch (semantic) {
    case Semantic::exact:
      return &compare_exact;
    case Semantic::tol:
    case Semantic::set:
    case Semantic::presence:
    case Semantic::hash:
    case Semantic::dist:
    case Semantic::span:
      return &compare_unimplemented;
  }
  return &compare_unimplemented;
}

}  // namespace mediadiff
