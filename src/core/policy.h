#pragma once

// Severity/tolerance resolution through the profile chain (doc 01 sections
// 4-6): built-in default -> profile default -> config [severity]/[tolerance]
// -> CLI --set/--tol, last writer wins. Plan 02-05 built the builtin and
// profile layers, plus the data structure and override mechanism (the
// PolicyProvenance chain, apply_severity_override) layers three and four
// plug into. Plan 02-06 (this file) adds those layers three and four as
// additional passes inside the SAME resolve_policy, over the SAME
// Policy/ResolvedCheck structure.
//
// Tolerance overrides (config's [tolerance], an override block's own
// [tolerance], CLI --tol) are last-writer-wins the same way severity is, but
// carry NO provenance chain entry of their own -- consistent with how the
// very first (builtin/profile) tolerance write never did either, since
// plan 02-05. ENG-06's `-v` display and doc 01 section 4's "resolved chain"
// are scoped to SEVERITY resolution throughout this project; a tolerance
// override is last-write-wins-and-forget, auditable only in the sense that
// CLI-10/ENG-05's grammar rejection still names the offending value.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config/toml_load.h"
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

// One `--set`/`--tol` argv accumulation -- doc 01 section 6's CLI layer
// (layer four). `dimension` selects which of a check's two resolved values
// this entry writes; `glob` and `value` are the text either side of the
// argument's first `=`, already syntax-checked (a well-formed check-id glob,
// a non-empty value) by src/cli/options.cpp before construction -- a
// severity `value` outside the closed four-word set is ALSO rejected there,
// since that check needs no registry lookup. A tolerance `value` is
// grammar-checked here, in resolve_policy, because doing so requires
// knowing which check(s) the glob resolves to and each one's declared Unit.
// `argv_index` is a counter that increments once per entry WITHIN its own
// dimension (severity and tolerance count separately), assigned by
// options.cpp as it walks CLI11's two accumulated vectors in encounter
// order -- this is what lets --set and --tol interleave freely in argv
// without changing either dimension's own relative order.
struct CliOverride {
  enum class Dimension { severity, tolerance };
  Dimension dimension;
  std::string glob;
  std::string value;
  std::size_t argv_index;
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

// Performs all four layers of doc 01 section 4's resolution chain over
// every check in `registry`, in registry declaration order:
//
//   1. builtin -- each check starts from its checks.def baseline, or
//      `Severity::ignore` when `CheckDef::is_volatile` (applied at this
//      layer rather than as a post-pass, so a later layer can still
//      promote it, T-2-19), with a `builtin` provenance entry. A check's
//      baseline tolerance (or profile override) is parsed once here too.
//   2. profile -- for a non-volatile check whose selected profile's
//      override actually differs from the baseline, the override is
//      applied with a `profile` provenance entry appended.
//   3. config -- when `config` holds a value: its top-level `[severity]`
//      and `[tolerance]` entries, each expanded through glob_select in
//      ascending GlobRule::file_order and written to every matched index;
//      then every `[override.*]` block's own `[severity]`/`[tolerance]`
//      entries, walked block-by-block in ascending OverrideBlock::file_order
//      and, within each block, entry-by-entry in ascending GlobRule::file_order
//      (this ordering is doc 01 section 6's own resolution rule for this
//      layer). Every severity write appends a `config`-layer provenance
//      entry naming the glob and its file position; every tolerance write
//      replaces `ResolvedCheck::tolerance` directly (no chain entry -- see
//      this header's own top comment for why).
//   4. cli -- `cli_overrides` applied in span order (which is argv order,
//      by `CliOverride::argv_index` construction in src/cli/options.cpp).
//      Same write shape as layer three: a severity write appends a
//      `cli`-layer provenance entry naming the argv text and position; a
//      tolerance write replaces `ResolvedCheck::tolerance` with no chain
//      entry. A tolerance value `parse_tolerance` rejects for a check its
//      glob resolves to is `ErrorKind::usage` naming the expected unit
//      (CLI-10) -- this is the one layer able to fail after layers one and
//      two have already succeeded, since resolving a CLI glob against the
//      registry to discover each matched check's declared Unit can only
//      happen here.
//
// `config` and `cli_overrides` both default to "layer absent" (nullopt,
// empty span) so every pre-existing two-argument call site -- every
// 02-04/02-05-era comparator unit test, compare/engine.cpp's own internal
// resolve_policy call when a caller passes a bare `Policy{profile}` -- keeps
// compiling and behaving exactly as it did before this plan, unchanged.
mediadiff::expected<Policy, Error> resolve_policy(const CheckRegistry& registry, ProfileId profile,
                                                    const std::optional<ConfigFile>& config = std::nullopt,
                                                    std::span<const CliOverride> cli_overrides = {});

// Appends one later-layer provenance entry to `policy`'s entry for
// `check_index` and makes `severity` that check's new resolved severity --
// last writer wins (doc 01 section 4), and the chain keeps every prior
// entry rather than overwriting it. `check_index` is trusted to be a valid
// index into `policy.per_check`, matching CheckRegistry::at's own trusted-
// index convention. This is the "policy API" every later-layer severity
// write (plan 02-06's own config/CLI passes inside resolve_policy, plus any
// caller building overrides by hand, as plan 02-05's own tests already do)
// goes through.
void apply_severity_override(Policy& policy, std::uint32_t check_index, Severity severity,
                              PolicyProvenance::Layer layer, std::string detail);

// Replaces `policy`'s entry for `check_index` with `tolerance` -- last
// writer wins, mirroring apply_severity_override's index-trust contract,
// but appends no provenance entry (see this header's own top comment: no
// layer's tolerance write, including the very first one resolve_policy
// itself performs, has ever carried a chain entry).
void apply_tolerance_override(Policy& policy, std::uint32_t check_index, Tolerance tolerance);

}  // namespace mediadiff
