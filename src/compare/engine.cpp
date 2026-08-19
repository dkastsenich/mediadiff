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

namespace mediadiff {

namespace {

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
    const auto baseline_it = baseline_by_key.find(pair_key);
    const auto candidate_it = candidate_by_key.find(pair_key);
    if (baseline_it == baseline_by_key.end() || candidate_it == candidate_by_key.end()) {
      // Unpaired on one side: doc 01 section 10 maps this to
      // meta.missing_candidate / meta.extra_candidate, which Task 2 of
      // this plan deliberately leaves unregistered (their `presence`
      // semantic has no comparator dispatch until plan 02-11) — a
      // functionality gap, not an architectural one. None of this plan's
      // fixtures exercise this path.
      continue;
    }

    if (pair_key.check_index >= registry.size()) {
      return mediadiff::unexpected(Error{ErrorKind::internal, "measurement check_index out of registry range"});
    }
    const CheckDef& check = registry.at(pair_key.check_index);
    const Measurement& baseline_m = *baseline_it->second;
    const Measurement& candidate_m = *candidate_it->second;

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
    findings.push_back(std::move(*finding));
  }

  return findings;
}

}  // namespace mediadiff
