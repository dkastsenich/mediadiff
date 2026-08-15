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

// Looks up the comparator for `semantic`. This plan implements only
// Semantic::exact (compare/exact.cpp); every other semantic's entry
// currently returns a comparator that reports ErrorKind::internal — a
// functionality gap plans 02-02 through 02-04 close, not a type-level one
// this plan leaves open.
Comparator comparator_for(Semantic semantic);

}  // namespace mediadiff
