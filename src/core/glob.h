#pragma once

// Segment-wise glob matcher for `--set`/`mediadiff.toml` check-id patterns
// (doc 01 section 2, ENG-02). No <regex> anywhere in this file or its
// translation unit — ENG-02 forbids it outright, which also removes
// catastrophic backtracking as a class from this input surface (T-2-02).
//
// Grammar: pattern and check id are both split on '.' into segments and
// compared segment-wise. A literal segment must be byte-equal. A `*`
// segment matches exactly one whole segment and never part of one. A `**`
// segment is legal only as the final pattern segment and matches one or
// more remaining segments — never zero. A pattern that is empty, or that
// has an empty segment (a leading, trailing, or doubled '.'), or that uses
// `**` anywhere but the final position, is malformed: glob_matches and
// glob_select both treat it as matching nothing; validate_glob reports why.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// Reports whether `pattern` is well-formed without evaluating it against
// any check id. A malformed pattern (empty, an empty segment, or a `**`
// outside the final position) yields a `usage`-kind Error naming the
// specific defect — the config layer (plan 02-06) turns this into an exit
// 64 diagnostic rather than silently treating the pattern as matching
// nothing.
mediadiff::expected<void, Error> validate_glob(std::string_view pattern);

// True if `pattern` matches `check_id` under the grammar above. A
// malformed pattern always returns false (matches nothing) — call
// validate_glob first to distinguish "well-formed but no match" from
// "malformed".
bool glob_matches(std::string_view pattern, std::string_view check_id);

// Walks `registry` in declaration order, returning the indices of every
// CheckDef whose id matches `pattern`, in that same order — so two
// invocations of the same pattern against the same registry produce the
// identical sequence, which is what makes downstream policy resolution
// deterministic.
std::vector<std::uint32_t> glob_select(std::string_view pattern, const CheckRegistry& registry);

}  // namespace mediadiff
