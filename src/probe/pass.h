#pragma once

// The probe layer's pass-declaration seam (PROBE-08, doc 02 section 7):
// every analyzer declares which raw scans it needs via
// AnalyzerSpec::required_passes; the orchestrator (src/probe/orchestrator.h)
// computes the union across every scoped analyzer and executes each pass
// exactly once per file into a shared ProbeResults, which every analyzer's
// run() then reads. No analyzer opens the input file itself (this plan's
// own prohibition) -- src/probe/orchestrator.cpp owns every read.

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "core/model.h"

namespace mediadiff {

class DemuxSession;

// The six raw scans this phase (and Phase 4's parser_scan) can request.
// `parser_scan` is declared now, ahead of its Phase-4 implementation
// (PROBE-03), so a Phase-4 analyzer's declared PassSet compiles without
// reopening this enum. `kCount` is a sentinel used only to size PassSet's
// bitset and to iterate every enumerator -- never a value a real
// AnalyzerSpec declares.
enum class Pass : std::uint8_t {
  demux_header,
  packet_scan,
  parser_scan,
  bmff_scan,
  ebml_scan,
  ts_scan,
  kCount,
};

// A small bitset over Pass -- not a std::set, since the whole domain is
// six known bits and a std::set would pay tree-node allocation for what
// is really a one-machine-word membership test.
class PassSet {
 public:
  constexpr PassSet() = default;

  constexpr PassSet(std::initializer_list<Pass> passes) {
    for (Pass p : passes) {
      set(p);
    }
  }

  constexpr void set(Pass p) { bits_ |= (1u << static_cast<unsigned>(p)); }
  constexpr bool test(Pass p) const { return (bits_ & (1u << static_cast<unsigned>(p))) != 0; }
  constexpr PassSet operator|(const PassSet& other) const { return PassSet(bits_ | other.bits_); }
  constexpr PassSet& operator|=(const PassSet& other) {
    bits_ |= other.bits_;
    return *this;
  }

  // Iterates every Pass present, in Pass enumerator order -- what makes
  // the orchestrator's "each pass runs exactly once, in a deterministic
  // order" property (PROBE-08) achievable from any union of PassSets.
  template <typename Fn>
  void for_each(Fn&& fn) const {
    for (unsigned i = 0; i < static_cast<unsigned>(Pass::kCount); ++i) {
      const Pass p = static_cast<Pass>(i);
      if (test(p)) {
        fn(p);
      }
    }
  }

 private:
  constexpr explicit PassSet(unsigned bits) : bits_(bits) {}
  unsigned bits_ = 0;
};

// The container family a probed file belongs to, derived from
// DemuxSession::format_name()'s first token (doc 02 section 2).
// `other` means both "a real family this phase doesn't special-case" and
// the scope every family-agnostic analyzer (this plan's container.format
// topology check) declares to mean "applies to every container".
enum class ContainerFamily : std::uint8_t {
  mp4,
  mkv,
  ts,
  other,
};

// Derives the ContainerFamily a `format_name()` value belongs to, for the
// orchestrator's pass-union scoping (PROBE-08). Implemented in
// src/probe/demux_session.cpp, next to the format_name() extraction it is
// the direct counterpart of.
ContainerFamily container_family_from_format_name(std::string_view format_name);

// The outputs of whichever passes the orchestrator ran for one file. This
// plan populates only `demux`; later plans (03-03 PacketScan, 03-05..08's
// raw scanners) add members here without any existing consumer's declared
// PassSet or run() signature having to change.
struct ProbeResults {
  const DemuxSession* demux = nullptr;
};

// One analyzer family's registration (PROBE-08): the passes it needs, the
// container family it applies to (`ContainerFamily::other` == "every
// container"), and the function that appends zero or more Measurements to
// `fp` from `results`. A raw function pointer, not std::function -- every
// analyzer's run() is a free function with no captured state, and this
// keeps AnalyzerSpec a trivially-constructible aggregate.
struct AnalyzerSpec {
  std::string_view name;
  PassSet required_passes;
  ContainerFamily scope;
  void (*run)(const ProbeResults&, Fingerprint&);
};

// The full analyzer list, assembled by src/probe/orchestrator.cpp in one
// explicit, hand-written order (03-02-PLAN.md Task 3: byte-identical
// --json across runs AND across platforms, TRUST-05, cannot depend on
// self-registering static-init order, which is unspecified across
// translation units).
const std::vector<AnalyzerSpec>& all_analyzers();

}  // namespace mediadiff
