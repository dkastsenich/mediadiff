#pragma once

// Severity/tolerance resolution through the profile chain (doc 01 sections
// 4-6): built-in default -> profile default -> config [severity]/[tolerance]
// -> CLI --set/--tol, last writer wins. This plan (02-05) builds the
// builtin and profile layers, plus the data structure and override
// mechanism layers three and four (config, CLI) plug into. Layers three and
// four are added by plan 02-06 as additional resolve_policy-style passes /
// apply_severity_override calls over the SAME Policy/ResolvedCheck
// structure; nothing here is a placeholder for them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/tolerance.h"
#include "util/expected.h"

namespace mediadiff {

// One layer's contribution to a check's resolved severity, appended --
// never overwritten -- as each layer runs, so the full history survives
// for ENG-06's `-v` display (plan 02-06's job to render into a Finding;
// this plan's job to make the data exist, per T-2-19). `detail` names the
// source: the profile name for `profile`, the config glob and its file
// position for `config`, or the argv `--set` text for `cli`. `value` is the
// resolved severity's spelling at that layer (e.g. "fail").
struct PolicyProvenance {
  enum class Layer { builtin, profile, config, cli };
  Layer layer;
  std::string detail;
  std::string value;

  bool operator==(const PolicyProvenance&) const = default;
};

// One check's fully-resolved severity/tolerance plus the layer history
// that produced it. `id` mirrors the owning CheckDef::id: resolve_severity
// below uses it to find a check's entry from a bare CheckDef reference (a
// comparator never has the check's registry index, only the CheckDef
// itself), so this is a short linear scan over a handful of entries, the
// same shape as CheckDef::severity_for's own per-check override scan --
// not a map probe, and iteration order over the whole vector is registry
// declaration order by construction (resolve_policy is a single pass over
// the registry in that order).
struct ResolvedCheck {
  std::string_view id;
  Severity severity;
  std::optional<Tolerance> tolerance;
  std::vector<PolicyProvenance> chain;

  bool operator==(const ResolvedCheck&) const = default;
};

// The resolved policy for one compare run: which profile, the per-check
// resolution table, and the `transform` profile's declared expectation
// (ENG-10) -- carried here rather than as a fifth comparator parameter so
// every Comparator signature (compare/semantics.h) stays unchanged.
struct Policy {
  ProfileId profile;
  // Default member initializers on these two trailing fields (rather than
  // leaving them bare) are what let every pre-existing call site across
  // the codebase that still aggregate-initializes a bare `Policy{profile}`
  // (src/cli/commands/compare.cpp's pre-02-06 CLI path, and the 02-04-era
  // comparator unit tests) keep compiling without triggering
  // -Wmissing-field-initializers (-Werror project-wide) — not merely a
  // cosmetic default, since std::vector/std::optional already
  // default-construct to empty/nullopt regardless.
  std::vector<ResolvedCheck> per_check{};
  TransformExpectation transform_expectation{};
};

// Resolves the severity a Finding for `check` should carry under `policy`
// (doc 01 section 4). When `policy.per_check` already holds an entry for
// `check` -- the normal case: a Policy built by resolve_policy below,
// optionally with later-layer overrides appended via
// apply_severity_override -- that entry's severity is authoritative. This
// is what lets an explicit config/CLI override actually gate: every
// comparator (compare/exact.cpp and the six comparators plan 02-04 wrote)
// calls this function exactly once and never sees policy.per_check itself.
// When `policy.per_check` is empty -- a Policy hand-built directly, which
// this plan's own comparator unit tests and the pre-02-06 CLI path
// (src/cli/commands/compare.cpp) both still do -- this recomputes the
// builtin and profile layers fresh from `check` and `policy.profile`
// alone, producing exactly what resolve_policy would have for that check:
// `Severity::ignore` when `check.is_volatile`, otherwise
// `check.severity_for(policy.profile)`.
Severity resolve_severity(const CheckDef& check, const Policy& policy);

// Performs the builtin and profile layers of doc 01 section 4's resolution
// chain over every check in `registry`, in registry declaration order: each
// check starts from its checks.def baseline -- or `Severity::ignore` when
// `CheckDef::is_volatile`, applied here at this builtin layer rather than
// as a post-pass, which is what lets a later config/CLI layer still
// promote it (T-2-19) -- with a `builtin` provenance entry, then (for a
// non-volatile check whose profile override actually differs from the
// baseline) the selected profile's override is applied with a `profile`
// provenance entry appended. A check's baseline tolerance (or profile
// override, via CheckDef::tolerance_for) is parsed once here too, when
// declared. Layers three (config) and four (CLI) are additional overrides
// a later plan applies to these SAME per_check entries via
// apply_severity_override below.
mediadiff::expected<Policy, Error> resolve_policy(const CheckRegistry& registry, ProfileId profile);

// Appends one later-layer provenance entry to `policy`'s entry for
// `check_index` and makes `severity` that check's new resolved severity --
// last writer wins (doc 01 section 4), and the chain keeps every prior
// entry rather than overwriting it. `check_index` is trusted to be a valid
// index into `policy.per_check`, matching CheckRegistry::at's own trusted-
// index convention. This is the "policy API" plan 02-06's config/CLI passes
// call once per override they apply.
void apply_severity_override(Policy& policy, std::uint32_t check_index, Severity severity,
                              PolicyProvenance::Layer layer, std::string detail);

}  // namespace mediadiff
