#pragma once

// The hand-written types tools/gen_registry.py's generated
// check_registry.cpp instantiates (D-01). Nothing in this header is
// generated — the generated files are check_id.h, check_registry.cpp and
// check_explain.cpp, all produced from src/core/checks.def at build time
// and never hand-edited.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mediadiff {

// The seven comparison semantics doc 01 section 3 defines. This plan's
// dispatch (compare/semantics.h) implements only `exact` (compare/exact.cpp)
// — the other six are declared now so a check registered against one of
// them compiles and links; the unimplemented dispatch path returns an
// internal Error, which is a functionality gap plans 02-02 through 02-04
// close, not an architectural one this plan leaves open.
enum class Semantic {
  exact,
  tol,
  set,
  presence,
  hash,
  dist,
  span,
};

// The tag half of Value's std::variant<9> (core/value.h) — kept in its own
// enum, separate from that variant, so a CheckDef can declare its expected
// shape without core/registry.h and core/value.h needing to include each
// other.
enum class ValueKind {
  int64,
  rational,
  real,
  string,
  string_set,
  histogram,
  span_list,
  hash_chain,
};

// The physical/logical unit a check's tolerance grammar (doc 01 section 3)
// is expressed in. Not exhaustive against every unit spelling that grammar
// will eventually need — extended per-phase as real checks are registered
// (D-04). `none` covers a check like this plan's meta.tool_version, whose
// `exact` semantic carries no physical unit at all.
enum class Unit {
  none,
  ms,
  ms_per_min,
  frames,
  percent,
  db,
  lu,
  samples,
  ticks,
  count,
};

// Resolved per-finding severity (doc 01 section 4). `ignore` is distinct
// from a check simply not firing: an ignored check's difference is still
// computed and shown under `-v` (D-15's "trust never requires faith") — it
// just never gates the exit code.
enum class Severity {
  ignore,
  info,
  warn,
  fail,
};

// The five profiles doc 01 section 5 ships. Declared now so
// core/policy.h's Policy{ProfileId} compiles; profile-specific
// severity/tolerance overrides (doc 01 section 5's normative matrix) are
// plan 02-05's job, not this plan's.
enum class ProfileId {
  strict_bitexact,
  sw_encoder,
  hw_encoder,
  remux,
  transform,
};

// Suffix text a user would type in the tolerance grammar (doc 01 section 3,
// CLI-10) for `unit` — e.g. Unit::ms -> "ms", Unit::percent -> "%". Lets a
// tolerance-grammar rejection (src/core/tolerance.cpp) name the check's
// expected unit using the same spelling a user would type, rather than an
// enumerator name like "ms_per_min". Unit::count's canonical spelling is
// "bytes", but tolerance.cpp additionally accepts the bare (no-suffix) form
// doc 01's grammar also lists ("±8") for that one unit — this function
// reports only the canonical spelling; the bare-form exception is
// tolerance.cpp's own concern, not this table's. Unit::none has no
// tolerance spelling of its own (no check registers semantic=tol with
// unit=none); it renders as "none" purely so this switch stays exhaustive
// with no default: arm, matching src/cli/exit_code.h's own pattern.
inline std::string_view unit_suffix(Unit unit) {
  switch (unit) {
    case Unit::none:
      return "none";
    case Unit::ms:
      return "ms";
    case Unit::ms_per_min:
      return "ms/min";
    case Unit::frames:
      return "frames";
    case Unit::percent:
      return "%";
    case Unit::db:
      return "dB";
    case Unit::lu:
      return "LU";
    case Unit::samples:
      return "samples";
    case Unit::ticks:
      return "tick";
    case Unit::count:
      return "bytes";
  }
  // Unreachable for any valid Unit: every enumerator is handled above, with
  // deliberately no default: arm so -Wswitch (-Werror project-wide) catches
  // a future enumerator added without a matching case. This return exists
  // only to satisfy -Wreturn-type.
  return "none";
}

// Textual severity spelling, in both directions -- the vocabulary a user
// types in `mediadiff.toml`'s `[severity]` table or a `--set` argument
// (plan 02-06), and the inverse used for `-v` provenance rendering and
// diagnostics. Exact spelling only, no case folding: a mistyped severity
// word is a usage error, matching profile_from_string's own exact-match
// discipline (T-2-18).
inline std::optional<Severity> severity_from_string(std::string_view text) {
  if (text == "ignore") return Severity::ignore;
  if (text == "info") return Severity::info;
  if (text == "warn") return Severity::warn;
  if (text == "fail") return Severity::fail;
  return std::nullopt;
}

inline std::string_view severity_to_string(Severity severity) {
  switch (severity) {
    case Severity::ignore:
      return "ignore";
    case Severity::info:
      return "info";
    case Severity::warn:
      return "warn";
    case Severity::fail:
      return "fail";
  }
  // Unreachable for any valid Severity -- see unit_suffix's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "ignore";
}

// One profile's severity exception (doc 01 section 5, D-04). A check with
// no entry for a given profile inherits CheckDef::default_severity — a
// deliberate exception reads as an exception rather than being buried in
// one cell of a five-column row.
struct ProfileSeverityOverride {
  ProfileId profile;
  Severity severity;
};

// One profile's tolerance exception, same rationale as
// ProfileSeverityOverride but for CheckDef::default_tolerance.
struct ProfileToleranceOverride {
  ProfileId profile;
  std::string_view tolerance;
};

// One entry in the deprecated-alias table (doc 01 section 2: "Renames
// without alias are forbidden post-v1"). Carries structure, not a bare
// string pair, so resolution stays unambiguous when a future rename
// crosses namespaces: `new_check_index` is validated at generation time to
// resolve into the CheckDef table, and `new_group` is carried alongside it
// so a caller never has to re-derive the new check's group from the index
// alone.
struct AliasDef {
  std::string_view old_id;
  std::uint32_t new_check_index;
  std::string_view new_group;
};

// One registry entry — the shape tools/gen_registry.py's generated
// check_registry.cpp instantiates via C++20 designated initializers, in
// exactly this field declaration order (designated initializers must
// follow declaration order). `default_severity`/`default_tolerance` are
// the built-in baseline (D-04); `profile_*_overrides` are spans (pointer +
// count) into per-check constexpr override tables the generator emits only
// for checks that declare a `[check.profile_severity]` or
// `[check.profile_tolerance]` sub-table — a profile with no entry in that
// span inherits the baseline. core/policy.h's resolve_severity/resolve_policy
// (plan 02-05) resolve these against a selected ProfileId; layers three and
// four of doc 01 section 4's chain (config, CLI) are plan 02-06's job.
struct CheckDef {
  std::string_view id;
  std::string_view group;
  Semantic semantic;
  Unit unit;
  ValueKind value_kind;
  Severity default_severity;
  // Tolerance grammar text ("5ms", "3%", ...), unparsed — the grammar
  // parser is a later plan's job (ENG-05, CLI-10). Empty for semantics
  // that don't consume a tolerance, including every check this plan
  // registers.
  std::string_view default_tolerance;
  bool is_volatile;
  bool requires_pass;
  // Plan 02-05 / ENG-10: true only for a check the `transform` profile's
  // declared expectation (doc 01 section 5, `[transform] expect.resolution`)
  // converts into a comparison against that expectation rather than against
  // baseline equality. False for every check by default, including every
  // shipped check in Phase 2 — the resolution identity check that would
  // actually carry this flag in production lands in Phase 4; a test-only
  // check exercises it here so the mechanism is proven ahead of that check
  // existing (checks.def's own comment on this key explains the deferral).
  bool transform_affected = false;
  const ProfileSeverityOverride* profile_severity_overrides;
  std::size_t profile_severity_override_count;
  const ProfileToleranceOverride* profile_tolerance_overrides;
  std::size_t profile_tolerance_override_count;

  // Resolves this check's severity for `profile`: the profile's override if
  // one was declared, otherwise `default_severity`. A linear scan over a
  // handful of entries — the override span is per-check and short by
  // construction (at most one entry per profile, five profiles total).
  Severity severity_for(ProfileId profile) const {
    for (std::size_t i = 0; i < profile_severity_override_count; ++i) {
      if (profile_severity_overrides[i].profile == profile) {
        return profile_severity_overrides[i].severity;
      }
    }
    return default_severity;
  }

  // Same resolution as severity_for, for tolerance.
  std::string_view tolerance_for(ProfileId profile) const {
    for (std::size_t i = 0; i < profile_tolerance_override_count; ++i) {
      if (profile_tolerance_overrides[i].profile == profile) {
        return profile_tolerance_overrides[i].tolerance;
      }
    }
    return default_tolerance;
  }
};

// A read-only view over a contiguous CheckDef table, in registry
// declaration order — the order tools/gen_registry.py wrote checks.def's
// entries in, which compare/engine.cpp also emits findings in, so report
// output never depends on either fingerprint's measurement read order
// (TRUST-05) — plus the flat alias table generated alongside it.
class CheckRegistry {
 public:
  constexpr CheckRegistry(const CheckDef* defs, std::size_t count, const AliasDef* aliases,
                           std::size_t alias_count)
      : defs_(defs), count_(count), aliases_(aliases), alias_count_(alias_count) {}

  constexpr std::size_t size() const noexcept { return count_; }

  // `index` is trusted to be < size() by every call site in this phase — an
  // out-of-range index is a programming bug (a CheckId the generator never
  // emitted), not a runtime input needing its own error type.
  const CheckDef& at(std::uint32_t index) const { return defs_[index]; }

  std::optional<std::uint32_t> find(std::string_view id) const {
    for (std::uint32_t i = 0; i < count_; ++i) {
      if (defs_[i].id == id) {
        return i;
      }
    }
    return std::nullopt;
  }

  // Resolves `id` against registered check ids first, then the alias
  // table. When `was_aliased` is non-null, reports whether the match came
  // through an alias — the config parser (plan 02-06) uses that to decide
  // whether ENG-03's deprecation warning is warranted. Returns nullopt only
  // when `id` is neither a registered id nor a declared alias.
  std::optional<std::uint32_t> resolve_alias(std::string_view id, bool* was_aliased = nullptr) const {
    if (auto idx = find(id)) {
      if (was_aliased != nullptr) {
        *was_aliased = false;
      }
      return idx;
    }
    for (std::size_t i = 0; i < alias_count_; ++i) {
      if (aliases_[i].old_id == id) {
        if (was_aliased != nullptr) {
          *was_aliased = true;
        }
        return aliases_[i].new_check_index;
      }
    }
    return std::nullopt;
  }

  const CheckDef* begin() const noexcept { return defs_; }
  const CheckDef* end() const noexcept { return defs_ + count_; }

 private:
  const CheckDef* defs_;
  std::size_t count_;
  const AliasDef* aliases_;
  std::size_t alias_count_;
};

// Accessor for the built-in registry table generated from checks.def by
// tools/gen_registry.py (D-01). Defined in the generated
// check_registry.cpp — never hand-written.
const CheckRegistry& builtin_registry();

}  // namespace mediadiff
