#include "compare/semantics.h"

#include <string>
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
      // D-15 / "trust never requires faith": an ignored check's difference
      // is still computed and surfaced, never silently dropped into pass.
      return Status::info;
  }
  return Status::info;
}

// True iff `value` (when it holds a string) equals one of `check`'s own
// declared flagged_values -- a non-string value (Absent, or any other
// alternative; never expected in practice since checks.def declares
// value_kind = "string" for every `state` check today, and the engine's
// own D-09 value_kind gate already rejects a mismatch ahead of this
// comparator) is never treated as flagged.
bool value_is_flagged(const Value& value, const CheckDef& check) {
  const auto* text = std::get_if<std::string>(&value);
  if (text == nullptr) {
    return false;
  }
  for (std::size_t i = 0; i < check.flagged_values_count; ++i) {
    if (check.flagged_values[i] == *text) {
      return true;
    }
  }
  return false;
}

}  // namespace

// compare_state: the eighth semantic (04-12-PLAN.md, D-10). Unlike `exact`,
// this does NOT compare baseline against candidate for equality -- it asks
// a value-driven question instead: is EITHER side's value one of this
// check's own declared flagged_values? This is exactly what lets two files
// that SHARE an incoherent state still surface it as a real Finding
// (VIDEO-10's own "even when both files share it") -- under `exact`'s
// baseline-equality rule, baseline == candidate would compare `pass`
// regardless of what that shared value actually was, making the shared
// incoherence invisible in `compare` (the exact invisibility D-10's own
// design rejects the evidence-only alternatives for).
//
// NEVER reads Measurement::evidence -- only the two Value payloads and the
// check's own registered flagged_values, mirroring every other comparator
// in this file's own established "the compare engine never reads evidence"
// invariant (Finding::evidence is populated separately, afterward, by
// src/compare/engine.cpp's own dedicated seam).
//
// Deterministic and allocation-light: a linear scan over a short,
// per-check constexpr array (one check declares two entries today) and a
// handful of std::string_view/std::string comparisons -- no floating
// point anywhere in this path.
mediadiff::expected<Finding, Error> compare_state(const CheckDef& check, const Measurement& baseline,
                                                    const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;

  const Severity severity = resolve_severity(check, policy);
  finding.severity = severity;

  const bool baseline_flagged = value_is_flagged(baseline.value, check);
  const bool candidate_flagged = value_is_flagged(candidate.value, check);

  if (!baseline_flagged && !candidate_flagged) {
    finding.status = Status::pass;
    finding.message = "neither value is flagged";
    return finding;
  }

  finding.status = escalate(severity);
  finding.message = (baseline_flagged && candidate_flagged) ? "both values are flagged" : "a flagged value is present";
  return finding;
}

}  // namespace mediadiff
