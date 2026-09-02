#include "compare/engine.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "compare/semantics.h"
#include "core/container_family.h"

namespace mediadiff {

namespace {

// CONT-02 (03-06-PLAN.md Task 3, doc 02 section 2's Edge-cases column):
// reads `id`'s own registered check id (a `container.format` measurement
// at GLOBAL scope, per topology.cpp's own emit shape) out of `fp` and
// resolves it through container_family_token -- the SAME function
// src/probe/demux_session.cpp's ContainerFamily derivation calls, so the
// probe layer's scoping and this demotion can never disagree about what
// family a file belongs to. Returns an empty token when no
// `container.format` measurement exists at all (an unregistered id, or a
// hand-built test Fingerprint that never populated it) -- container_family
// _token's own empty-string convention already means "never demoted",
// which is exactly the right behavior here too.
std::string_view resolve_family(const Fingerprint& fp, const CheckRegistry& registry) {
  const std::optional<std::uint32_t> format_index = registry.find("container.format");
  if (!format_index.has_value()) {
    return std::string_view{};
  }
  for (const Measurement& m : fp.measurements) {
    if (m.check_index != *format_index || m.scope.kind != Scope::Kind::global || m.scope.index != 0) {
      continue;
    }
    if (const auto* value = std::get_if<std::string>(&m.value)) {
      return container_family_token(*value);
    }
    return std::string_view{};
  }
  return std::string_view{};
}

// True iff `id` has the shape `container.<family_token>.<...>` -- THREE OR
// MORE dot-delimited segments, first segment exactly "container", second
// segment exactly `family_token`. Guards against a false match the way
// src/report/model.h's own `group_for` precedent does (meaning from the
// id's own segments, never from CheckDef::group): a hypothetical two-
// segment `container.mp4` id would never be swept up, and `container.
// format` itself (two segments) is structurally exempt by construction --
// it is never demoted, which is CONT-02's whole point (the migration
// itself must still be visible).
bool check_id_matches_family(std::string_view id, std::string_view family_token) {
  if (family_token.empty()) {
    return false;
  }
  const auto first_dot = id.find('.');
  if (first_dot == std::string_view::npos || id.substr(0, first_dot) != "container") {
    return false;
  }
  const std::string_view rest = id.substr(first_dot + 1);
  const auto second_dot = rest.find('.');
  if (second_dot == std::string_view::npos || second_dot + 1 >= rest.size()) {
    return false;
  }
  return rest.substr(0, second_dot) == family_token;
}

// Maps the alternative Value's std::variant actually holds to the
// ValueKind vocabulary CheckDef::value_kind declares against (D-09). Never
// called for the Absent alternative — see value_kind_mismatch below, which
// exempts it before this function would ever be reached.
ValueKind value_kind_of(const Value& value) {
  return std::visit(
      [](const auto& v) -> ValueKind {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::int64_t>) {
          return ValueKind::int64;
        } else if constexpr (std::is_same_v<T, RationalValue>) {
          return ValueKind::rational;
        } else if constexpr (std::is_same_v<T, double>) {
          return ValueKind::real;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return ValueKind::string;
        } else if constexpr (std::is_same_v<T, StringSet>) {
          return ValueKind::string_set;
        } else if constexpr (std::is_same_v<T, Histogram>) {
          return ValueKind::histogram;
        } else if constexpr (std::is_same_v<T, SpanList>) {
          return ValueKind::span_list;
        } else if constexpr (std::is_same_v<T, HashChain>) {
          return ValueKind::hash_chain;
        } else {
          // Absent: unreachable — value_kind_mismatch exempts it before
          // calling this function. static_assert would fire for a value
          // this branch is never instantiated with as long as that holds.
          return ValueKind::int64;
        }
      },
      value);
}

std::string_view value_kind_name(ValueKind kind) {
  switch (kind) {
    case ValueKind::int64:
      return "int64";
    case ValueKind::rational:
      return "rational";
    case ValueKind::real:
      return "real";
    case ValueKind::string:
      return "string";
    case ValueKind::string_set:
      return "string_set";
    case ValueKind::histogram:
      return "histogram";
    case ValueKind::span_list:
      return "span_list";
    case ValueKind::hash_chain:
      return "hash_chain";
  }
  // Unreachable for any valid ValueKind — see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "unknown";
}

// D-09: the registry's declared value_kind is authoritative; a mismatch
// between what a Measurement's Value variant actually holds and what the
// check declares is an error, never a coercion. Absent is exempt — "a
// check that ran but had nothing to measure" is valid regardless of the
// declared kind (core/value.h's own Absent comment). This is the runtime
// backstop for measurements that never passed through
// core/serializer.cpp's value_from_json (which already enforces this at
// JSON-parse time) — a live analyzer constructing a Measurement in-process
// (Phase 3 onward) has no such gate of its own.
bool value_kind_mismatch(const Value& value, ValueKind expected, std::string* observed_name) {
  if (std::holds_alternative<Absent>(value)) {
    return false;
  }
  const ValueKind actual = value_kind_of(value);
  if (actual == expected) {
    return false;
  }
  if (observed_name != nullptr) {
    *observed_name = std::string(value_kind_name(actual));
  }
  return true;
}

// Pairing key: (check_index, scope). Ordered lexicographically by
// check_index first — that is what makes "registry declaration order" the
// primary sort key doc 01's output-order contract needs — then
// scope.kind, then scope.index, which breaks ties within one check the
// same way doc 01 section 8's "scopes sorted" already requires for
// snapshot output.
struct PairKey {
  std::uint32_t check_index;
  Scope::Kind scope_kind;
  int scope_index;

  bool operator<(const PairKey& other) const {
    if (check_index != other.check_index) return check_index < other.check_index;
    if (scope_kind != other.scope_kind) return scope_kind < other.scope_kind;
    return scope_index < other.scope_index;
  }
};

PairKey key_for(const Measurement& m) { return PairKey{m.check_index, m.scope.kind, m.scope.index}; }

}  // namespace

mediadiff::expected<std::vector<Finding>, Error> compare_fingerprints(const Fingerprint& baseline,
                                                                        const Fingerprint& candidate,
                                                                        const Policy& policy,
                                                                        const CheckRegistry& registry) {
  // Plan 02-05: every finding's severity comes from a fully-resolved
  // Policy::per_check, indexed by registry declaration index, rather than
  // from CheckDef::default_severity directly. A caller that already ran
  // resolve_policy (and possibly layered config/CLI overrides onto it via
  // apply_severity_override) passes a Policy with per_check already
  // populated, which is used as-is so those overrides are never lost. A
  // caller that hand-built a bare Policy{profile} (the pre-02-06 CLI path,
  // and every 02-04-era comparator unit test) gets one resolved here, so
  // every finding this function produces -- including the D-09 mismatch
  // path below -- is consistent with what resolve_severity itself would
  // compute for the same check.
  Policy resolved_policy = policy;
  if (resolved_policy.per_check.empty()) {
    auto resolved = resolve_policy(registry, policy.profile);
    if (!resolved) {
      return mediadiff::unexpected(resolved.error());
    }
    resolved_policy.per_check = std::move(resolved->per_check);
  }

  // CONT-02: resolved ONCE, ahead of pairing -- deliberately unconditional
  // on `policy`/`resolved_policy` (the demotion decides STATUS, severity
  // policy decides how a status GATES; conflating the two would mean the
  // same two files produce different statuses under different policies,
  // breaking the idempotence story this whole project rests on). "user
  // demotes with `--set container.format=info`" (doc 02's own Edge-cases
  // wording) describes the user's WORKFLOW -- they stop container.format
  // from gating the merge -- not a code dependency of this mechanism.
  const std::string_view baseline_family = resolve_family(baseline, registry);
  const std::string_view candidate_family = resolve_family(candidate, registry);
  const bool cross_container_active =
      !baseline_family.empty() && !candidate_family.empty() && baseline_family != candidate_family;

  std::map<PairKey, const Measurement*> baseline_by_key;
  for (const Measurement& m : baseline.measurements) {
    baseline_by_key[key_for(m)] = &m;
  }
  std::map<PairKey, const Measurement*> candidate_by_key;
  for (const Measurement& m : candidate.measurements) {
    candidate_by_key[key_for(m)] = &m;
  }

  // Every key present in EITHER side, iterated in sorted order — registry
  // declaration order first, scope order second (TRUST-05) — regardless of
  // which order either fingerprint's own measurements[] was read in.
  std::set<PairKey> all_keys;
  for (const auto& [k, unused] : baseline_by_key) {
    (void)unused;
    all_keys.insert(k);
  }
  for (const auto& [k, unused] : candidate_by_key) {
    (void)unused;
    all_keys.insert(k);
  }

  std::vector<Finding> findings;
  findings.reserve(all_keys.size());

  for (const PairKey& pair_key : all_keys) {
    if (pair_key.check_index >= registry.size()) {
      return mediadiff::unexpected(Error{ErrorKind::internal, "measurement check_index out of registry range"});
    }
    const CheckDef& check = registry.at(pair_key.check_index);

    const auto baseline_it = baseline_by_key.find(pair_key);
    const auto candidate_it = candidate_by_key.find(pair_key);

    // CONT-02: checked AHEAD of the unpaired-continue below, and
    // unconditionally on whether a Measurement exists on either side --
    // container.mkv.codec_delay's own per-track scope, for example, is
    // present on the real-data side but never on the family-agnostic
    // not-applicable sibling's global-scope skip, which would otherwise
    // make this pair "unpaired" and silently dropped. A cross-container
    // migration must demote it EITHER WAY: doc 02's own promise is that
    // EVERY container.<fmt>.* check on BOTH sides becomes
    // skipped:cross_container, not merely the ones that happened to have a
    // Measurement on both sides.
    if (cross_container_active && (check_id_matches_family(check.id, baseline_family) ||
                                    check_id_matches_family(check.id, candidate_family))) {
      Finding finding;
      finding.id = check.id;
      finding.scope = Scope{pair_key.scope_kind, pair_key.scope_index};
      finding.status = Status::skipped;
      finding.severity = resolved_policy.per_check[pair_key.check_index].severity;
      finding.baseline = baseline_it != baseline_by_key.end() ? baseline_it->second->value : Value{Absent{}};
      finding.candidate = candidate_it != candidate_by_key.end() ? candidate_it->second->value : Value{Absent{}};
      finding.skip_reason = SkipReason::cross_container;
      finding.message =
          fmt::format("check '{}' skipped: {}", check.id, skip_reason_to_string(SkipReason::cross_container));
      findings.push_back(std::move(finding));
      continue;
    }

    if (baseline_it == baseline_by_key.end() || candidate_it == candidate_by_key.end()) {
      // CONT-08 (03-08-PLAN.md Task 3, doc 02 section 6): "program-scoped
      // checks emit one measurement per program ... baseline/candidate
      // pairing by program_number, unpaired programs -> topology fail."
      // A program-scoped measurement (Scope::Kind::program) present on
      // only one side means the two files declare different program
      // topologies -- verified empirically (not assumed) that the
      // ordinary unpaired path below silently DROPS this pair with no
      // Finding at all (not even a skip), which both fails doc 02's own
      // literal requirement and is worse than a silent skip: a program
      // that vanished from a report is a program a reviewer never learns
      // about. Emitted at Status::fail unconditionally, like the
      // cross-container demotion above and the value_kind-mismatch path
      // below -- this is a structural fact about the two inputs' program
      // topology, not something a check's own severity policy should be
      // able to downgrade.
      if (pair_key.scope_kind == Scope::Kind::program) {
        const bool baseline_has = baseline_it != baseline_by_key.end();
        Finding finding;
        finding.id = check.id;
        finding.scope = Scope{pair_key.scope_kind, pair_key.scope_index};
        finding.status = Status::fail;
        finding.severity = Severity::fail;
        finding.baseline = baseline_has ? baseline_it->second->value : Value{Absent{}};
        finding.candidate = !baseline_has ? candidate_it->second->value : Value{Absent{}};
        finding.skip_reason = SkipReason::none;
        finding.message = fmt::format("check '{}': program {} present only on the {} side -- topology mismatch",
                                       check.id, pair_key.scope_index, baseline_has ? "baseline" : "candidate");
        findings.push_back(std::move(finding));
        continue;
      }

      // Unpaired on one side: doc 01 section 10 maps this to
      // meta.missing_candidate / meta.extra_candidate, which Task 2 of
      // this plan deliberately leaves unregistered (their `presence`
      // semantic has no comparator dispatch until plan 02-11) — a
      // functionality gap, not an architectural one. None of this plan's
      // fixtures exercise this path.
      continue;
    }

    const Measurement& baseline_m = *baseline_it->second;
    const Measurement& candidate_m = *candidate_it->second;

    // 03-04-PLAN.md Task 1: an analyzer that deliberately produced no real
    // value for this check on this file (Measurement::skip_reason != none,
    // e.g. container.chapters on an MPEG-TS input) short-circuits straight
    // to a Status::skipped Finding, ahead of both the D-09 mismatch check
    // and the normal comparator dispatch -- neither would know what to do
    // with an explicit "this check does not apply here" marker (Absent
    // alone is ambiguous with "measured and genuinely empty", which is
    // exactly why this is a separate field rather than reusing Absent).
    // Checked before value_kind_mismatch below because Absent is already
    // exempt from that check (D-09) and this path's own Status must win
    // regardless. Prefers baseline's reason when both sides carry one (the
    // common case: an asymmetric cross-family pair only ever has one side
    // set).
    if (baseline_m.skip_reason != SkipReason::none || candidate_m.skip_reason != SkipReason::none) {
      const SkipReason reason =
          baseline_m.skip_reason != SkipReason::none ? baseline_m.skip_reason : candidate_m.skip_reason;
      Finding finding;
      finding.id = check.id;
      finding.scope = candidate_m.scope;
      finding.status = Status::skipped;
      finding.severity = resolved_policy.per_check[pair_key.check_index].severity;
      finding.baseline = baseline_m.value;
      finding.candidate = candidate_m.value;
      finding.skip_reason = reason;
      finding.message =
          fmt::format("check '{}' skipped: {}", check.id, skip_reason_to_string(reason));

      nlohmann::ordered_json skip_evidence;
      if (baseline_m.evidence.is_object()) {
        skip_evidence["baseline"] = baseline_m.evidence;
      }
      if (candidate_m.evidence.is_object()) {
        skip_evidence["candidate"] = candidate_m.evidence;
      }
      if (!skip_evidence.empty()) {
        finding.evidence = std::move(skip_evidence);
      }

      findings.push_back(std::move(finding));
      continue;
    }

    std::string observed_kind;
    const bool mismatch = value_kind_mismatch(baseline_m.value, check.value_kind, &observed_kind) ||
                           value_kind_mismatch(candidate_m.value, check.value_kind, &observed_kind);
    if (mismatch) {
      // D-09: never a coercion — a Finding is still emitted (so the report
      // stays complete) but it carries Status::error, not a fabricated
      // verdict. No comparator ever sees this pair.
      Finding finding;
      finding.id = check.id;
      finding.scope = candidate_m.scope;
      finding.status = Status::error;
      finding.severity = resolved_policy.per_check[pair_key.check_index].severity;
      finding.baseline = baseline_m.value;
      finding.candidate = candidate_m.value;
      finding.skip_reason = SkipReason::none;
      finding.message = fmt::format("value_kind mismatch: check '{}' declares {}, measurement holds {}", check.id,
                                     value_kind_name(check.value_kind), observed_kind);
      findings.push_back(std::move(finding));
      continue;
    }

    const Comparator comparator = comparator_for(check.semantic);
    auto finding = comparator(check, baseline_m, candidate_m, resolved_policy);
    if (!finding) {
      return mediadiff::unexpected(finding.error());
    }

    // Closes Broken Window #1: the ONE seam Finding::evidence is populated
    // at (this plan's own must_have) -- no comparator needs to change and
    // no future comparator can forget. `baseline`/`candidate` members are
    // present only when that side's measurement evidence is a non-null
    // object; both absent leaves finding.evidence null.
    nlohmann::ordered_json evidence;
    if (baseline_m.evidence.is_object()) {
      evidence["baseline"] = baseline_m.evidence;
    }
    if (candidate_m.evidence.is_object()) {
      evidence["candidate"] = candidate_m.evidence;
    }
    if (!evidence.empty()) {
      finding->evidence = std::move(evidence);
    }

    findings.push_back(std::move(*finding));
  }

  return findings;
}

}  // namespace mediadiff
