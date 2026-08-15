#include "compare/engine.h"

#include <cstdint>
#include <map>
#include <set>
#include <utility>

#include "compare/semantics.h"

namespace mediadiff {

namespace {

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

    const Comparator comparator = comparator_for(check.semantic);
    auto finding = comparator(check, *baseline_it->second, *candidate_it->second, policy);
    if (!finding) {
      return mediadiff::unexpected(finding.error());
    }
    findings.push_back(std::move(*finding));
  }

  return findings;
}

}  // namespace mediadiff
