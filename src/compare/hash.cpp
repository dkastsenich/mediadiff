#include "compare/semantics.h"

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

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

// The hash preconditions doc 01 section 3 names: "same decode-path class,
// same sampling, same normalisation" (doc 01 section 7's decode-
// determinism vocabulary). Carried as evidence keys on the Measurement
// (core/model.h's Measurement::evidence, analyzer-attached metadata)
// rather than a dedicated struct -- no analyzer exists yet to populate
// these (Phase 6+ hashing work), so this exact evidence key shape is this
// plan's own design decision (02-CONTEXT.md's "Claude's Discretion"),
// recorded in 02-04-SUMMARY.md. A future analyzer that needs a
// differently-shaped precondition record can extend this table without
// touching the pass/fail/skipped decision logic below.
constexpr std::array<std::string_view, 3> kPreconditionKeys = {"decode_path_class", "sampling_state",
                                                                 "normalization"};

// Returns the name of the first precondition key that disagrees between
// the two evidence objects, or an empty string if every key present on
// either side agrees. A key present on only one side counts as a mismatch
// too -- an undeclared precondition is never assumed to match its absence.
std::string first_precondition_mismatch(const nlohmann::ordered_json& baseline_evidence,
                                         const nlohmann::ordered_json& candidate_evidence) {
  for (std::string_view key : kPreconditionKeys) {
    const bool baseline_has = baseline_evidence.contains(key);
    const bool candidate_has = candidate_evidence.contains(key);
    if (baseline_has != candidate_has) {
      return std::string(key);
    }
    if (baseline_has && candidate_has && baseline_evidence.at(key) != candidate_evidence.at(key)) {
      return std::string(key);
    }
  }
  return "";
}

}  // namespace

// compare_hash: doc 01 section 3's `hash` semantic -- chains equal AND hash
// preconditions match. A precondition mismatch is `skipped:hash_incomparable`
// with a remediation hint, never a fabricated pass or fail (doc 01 section
// 7, T-2-17): the decode-determinism class system exists precisely so a
// class-2 cross-path comparison degrades instead of lying, and this holds
// even when the digests happen to be equal despite the precondition
// mismatch (a coincidental match under different preconditions still
// cannot be trusted).
mediadiff::expected<Finding, Error> compare_hash(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.severity = resolve_severity(check, policy);

  const std::string mismatched_key = first_precondition_mismatch(baseline.evidence, candidate.evidence);
  if (!mismatched_key.empty()) {
    finding.status = Status::skipped;
    finding.skip_reason = SkipReason::hash_incomparable;
    finding.message =
        fmt::format("hash comparison skipped: '{}' precondition differs between baseline and candidate -- use a "
                     "perceptual or epsilon comparison instead",
                     mismatched_key);
    return finding;
  }
  finding.skip_reason = SkipReason::none;

  const auto* baseline_chain = std::get_if<HashChain>(&baseline.value);
  const auto* candidate_chain = std::get_if<HashChain>(&candidate.value);
  if (baseline_chain == nullptr || candidate_chain == nullptr) {
    // D-09 already guarantees both sides hold HashChain or Absent; Absent
    // on either side means there is nothing to declare equal.
    finding.status = escalate(finding.severity);
    finding.message = "hash chain present on only one side";
    return finding;
  }

  if (baseline_chain->algorithm == candidate_chain->algorithm && baseline_chain->digest == candidate_chain->digest) {
    finding.status = Status::pass;
    finding.message = "digests match";
    return finding;
  }

  finding.status = escalate(finding.severity);
  // Per-element first-divergence reporting is deferred to Phase 7 (where
  // per-element digests exist, per this plan's own action text) -- only
  // the element count on each side is reported here.
  finding.message = fmt::format("digests differ ({} baseline element(s), {} candidate element(s))",
                                 baseline_chain->element_count, candidate_chain->element_count);
  return finding;
}

}  // namespace mediadiff
