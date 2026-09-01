#include "compare/semantics.h"

#include <variant>

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

}  // namespace

// compare_presence: doc 01 section 3's `presence` semantic -- pass iff both
// sides are Absent or both are non-Absent. Never compares the values
// themselves; a value-level comparison, if wanted, is a separate `tol`
// check on the same extraction (doc 01 section 3's own note).
mediadiff::expected<Finding, Error> compare_presence(const CheckDef& check, const Measurement& baseline,
                                                       const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;
  finding.severity = resolve_severity(check, policy);

  const bool baseline_absent = std::holds_alternative<Absent>(baseline.value);
  const bool candidate_absent = std::holds_alternative<Absent>(candidate.value);

  if (baseline_absent == candidate_absent) {
    finding.status = Status::pass;
    finding.message = baseline_absent ? "both absent" : "both present";
    return finding;
  }

  finding.status = escalate(finding.severity);
  finding.message = baseline_absent ? "absent -> present" : "present -> absent";
  return finding;
}

}  // namespace mediadiff
