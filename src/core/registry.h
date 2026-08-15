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

// One registry entry — the shape tools/gen_registry.py's generated
// check_registry.cpp instantiates via C++20 designated initializers, in
// exactly this field declaration order (designated initializers must
// follow declaration order). `default_severity` is the single built-in
// default this plan needs; the per-profile override matrix doc 01 section
// 5 describes is plan 02-05's job — core/policy.h's resolve_severity stub
// already has the signature that later body will fill.
struct CheckDef {
  std::string_view id;
  std::string_view group;
  Semantic semantic;
  Unit unit;
  ValueKind value_kind;
  Severity default_severity;
  bool is_volatile;
  bool requires_pass;
  // Tolerance grammar text ("5ms", "3%", ...), unparsed — the grammar
  // parser is a later plan's job (ENG-05, CLI-10). Empty for semantics
  // that don't consume a tolerance, including every check this plan
  // registers.
  std::string_view tolerance;
};

// A read-only view over a contiguous CheckDef table, in registry
// declaration order — the order tools/gen_registry.py wrote checks.def's
// entries in, which compare/engine.cpp also emits findings in, so report
// output never depends on either fingerprint's measurement read order
// (TRUST-05).
class CheckRegistry {
 public:
  constexpr CheckRegistry(const CheckDef* defs, std::size_t count) : defs_(defs), count_(count) {}

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

  const CheckDef* begin() const noexcept { return defs_; }
  const CheckDef* end() const noexcept { return defs_ + count_; }

 private:
  const CheckDef* defs_;
  std::size_t count_;
};

// Accessor for the built-in registry table generated from checks.def by
// tools/gen_registry.py (D-01). Defined in the generated
// check_registry.cpp — never hand-written.
const CheckRegistry& builtin_registry();

}  // namespace mediadiff
