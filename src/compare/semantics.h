#pragma once

// The `compare/` engine's per-semantic dispatch table (doc 01 section 3).

#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// Compares one scoped measurement pair under policy and produces a
// Finding. A plain function pointer, not std::function — comparator_for's
// table is a fixed, build-time-known set (one entry per Semantic
// enumerator), so there is no call site that ever needs type erasure over
// an arbitrary callable.
using Comparator = mediadiff::expected<Finding, Error> (*)(const CheckDef& check, const Measurement& baseline,
                                                            const Measurement& candidate, const Policy& policy);

// Looks up the comparator for `semantic`. All seven Semantic enumerators
// dispatch to a real comparator as of plan 02-04 — one function per .cpp
// file under src/compare/, matching this declaration's signature exactly
// so comparator_for (compare/exact.cpp) can return each one as a plain
// function pointer with no adapter.
Comparator comparator_for(Semantic semantic);

// One comparator per doc 01 section 3 semantic. Declared here (rather than
// only forward-declared where comparator_for needs them) so every
// comparator is independently discoverable and independently testable —
// tests/unit/test_compare_semantics.cpp exercises several of these
// directly via comparator_for(Semantic::...) rather than by name, matching
// the fail-first coverage gate's own pattern, but nothing stops a future
// caller from naming one directly.
mediadiff::expected<Finding, Error> compare_exact(const CheckDef& check, const Measurement& baseline,
                                                    const Measurement& candidate, const Policy& policy);
mediadiff::expected<Finding, Error> compare_tol(const CheckDef& check, const Measurement& baseline,
                                                  const Measurement& candidate, const Policy& policy);
mediadiff::expected<Finding, Error> compare_set(const CheckDef& check, const Measurement& baseline,
                                                  const Measurement& candidate, const Policy& policy);
mediadiff::expected<Finding, Error> compare_presence(const CheckDef& check, const Measurement& baseline,
                                                       const Measurement& candidate, const Policy& policy);
mediadiff::expected<Finding, Error> compare_hash(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy);
mediadiff::expected<Finding, Error> compare_dist(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy);
mediadiff::expected<Finding, Error> compare_span(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy);

}  // namespace mediadiff
