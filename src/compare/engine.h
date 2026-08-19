#pragma once

#include <vector>

#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// Fingerprint x Fingerprint x Policy -> Finding[] (doc 01 section 1's core
// pipeline). Pairs measurements by (check_index, scope), dispatches each
// pair through compare/semantics.h's comparator_for, and emits findings in
// registry declaration order then scope order — so the output never
// depends on which order either fingerprint's measurements happened to be
// read in (TRUST-05).
mediadiff::expected<std::vector<Finding>, Error> compare_fingerprints(const Fingerprint& baseline,
                                                                        const Fingerprint& candidate,
                                                                        const Policy& policy,
                                                                        const CheckRegistry& registry);

}  // namespace mediadiff
