#pragma once

// PROBE-06/PROBE-07 (doc 02 section 1.3, section 5, 03-07-PLAN.md): ts_scan,
// the THIRD and largest hand-rolled parser in this project to operate
// directly on attacker-influenced binary structure -- MPEG-TS's fixed-size
// packet stream, PSI (PAT/PMT) sections and PCR timestamps. Read-only,
// bounded (never buffers a packet's own elementary-stream payload beyond
// the current 188-byte packet), entirely independent of the media decode
// toolchain this project links: this translation unit opens its own file
// handle (via util/fs.h's fopen_utf8) and parses byte offsets/sizes itself,
// matching src/probe/bmff_scan.h and src/probe/ebml_scan.h's own sibling
// shape (BoundedReader contract, checked-arithmetic discipline, byte-by-
// byte big-endian assembly, complete/stop_offset result convention).
//
// This header builds the SCANNER only. The `container.ts.*` checks it
// feeds, multi-program scoping policy, and carrying `estimated` onto
// Measurement are plan 03-08 -- see MuxRateEstimate's own comment below for
// where that flag originates.
//
// Bounds discipline is the security control here, not a formality:
// `adaptation_field_length` is a one-byte, fully attacker-controlled length
// gating a read inside a fixed-size packet; `section_length` (12 bits) is
// an attacker-declared length driving a loop over program/elementary-
// stream entries; the 13-bit PID field indexes a table an adversarial
// stream can cycle through all 8192 values of. `TsScanResult::complete ==
// false` (with `stop_offset` set) is the ONLY way this scanner reports a
// structural problem with the packet stream itself -- a per-packet
// malformation (a bad adaptation field, an oversized PSI section) instead
// increments `malformed_packets`/`discarded_sections` and the walk
// continues, per this plan's own prohibition against aborting the whole
// scan over one bad packet.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "util/expected.h"

namespace mediadiff {

// The 13-bit PID field structurally bounds the domain to exactly 8192
// values (0x0000..0x1FFF) -- this is why the per-PID table below is a
// fixed-size array indexed directly by PID, never an unordered_map keyed
// on attacker-supplied values (T-3-31): an unordered_map would grow to the
// exact same bound with hashing/allocation overhead and no benefit, since
// the bound is already structural in the wire format, not something this
// scanner has to enforce itself.
inline constexpr int kPidCount = 8192;

// PID 0x1FFF is the reserved "null packet" PID (stuffing, doc 02 section
// 5) -- counted in TsScanResult::null_packets, deliberately NOT reflected
// in PidStats[0x1FFF] (see TsScanResult::pid_stats's own comment), so the
// packet-accounting identity (sum of per-PID counts + null_packets ==
// total_packets) holds without double-counting.
inline constexpr int kNullPid = 0x1FFF;

// One PID's accumulated state across the whole scan. `first_cc_error_offset`
// is std::optional specifically so "no error yet" is distinguishable from
// "an error at byte offset 0" (mirrors EbmlTrack::codec_delay_ns's own
// absent-vs-zero reasoning in ebml_scan.h). `scrambling_seen_mask` is a
// 4-bit bitmask (bit N set means transport_scrambling_control value N was
// observed at least once on this PID) rather than a single latched value,
// so a PID that toggles between clear and scrambled mid-stream is still
// fully represented.
struct PidStats {
  std::int64_t packets = 0;
  std::int64_t cc_errors = 0;
  std::int64_t cc_discontinuities = 0;
  std::int64_t duplicates = 0;
  std::uint8_t scrambling_seen_mask = 0;
  std::optional<std::int64_t> first_cc_error_offset;
};

// One PCR sample, recorded at the moment `adaptation_field()`'s PCR_flag
// was seen set and the field validated in-bounds. `ticks` is
// `program_clock_reference_base * 300 + program_clock_reference_extension`
// -- 27 MHz ticks, the reserved 6 bits between base and extension already
// skipped at extraction time (never folded into either field).
struct PcrSample {
  std::int64_t offset;
  int pid;
  std::int64_t ticks;
};

// One program's PAT/PMT-derived state (doc 02 section 5). `pcr_pid` and
// `version_number` are std::optional because a program named by the PAT
// may not have had its PMT parsed yet (or ever, on a truncated/malformed
// stream) -- collapsing "not yet known" into a sentinel int would make
// that distinction invisible to a later plan's checks. `version_changes`
// counts version_number CHANGES only: the first sighting establishes the
// baseline and is never counted as a change (this plan's own Task 2 Test
// 5) -- naming it `version_changes` rather than merely storing the raw
// history is deliberate, since the roster's `pmt_version_churn` (03-08)
// needs exactly this count, not a version_number timeline to re-derive it
// from.
struct TsProgram {
  int program_number = 0;
  int pmt_pid = -1;
  std::optional<int> pcr_pid;
  std::vector<int> es_pids;
  std::optional<int> version_number;
  int version_changes = 0;
  std::vector<std::int64_t> pmt_offsets;
};

// The mux-rate estimate derived from a pair of consecutive PCR samples on
// the same PID (doc 02 section 5): `bytes_per_second_num / den` is kept as
// an EXACT, unreduced rational -- never resolved to a double, never even
// resolved via integer division here -- so a later byte-offset-to-
// milliseconds conversion (plan 03-08) stays exact per D-07. `estimated`
// is always `true` by construction: this struct exists ONLY when a real
// measurement was NOT taken directly, and every value plan 03-08 derives
// from it (container.ts.pcr_interval, container.ts.psi_interval, any other
// byte-offset-to-time conversion) must carry that flag onto
// Measurement::estimated so D-03's 3x tolerance widening applies -- the
// flag originates HERE, at the point the estimate is computed, rather than
// being bolted on downstream where a future derived value could be added
// without one (this plan's own key_link).
struct MuxRateEstimate {
  std::int64_t bytes_per_second_num = 0;
  std::int64_t bytes_per_second_den = 1;
  // Consecutive same-PID PCR pairs whose pcr_delta was zero or negative (a
  // 2^33-tick PCR wrap, or a genuine discontinuity) and were therefore
  // excluded from the estimate rather than risking a division by a
  // wrapped-to-zero or negative delta (T-3-36).
  std::int64_t skipped_pairs = 0;
  bool estimated = true;
};

// The whole scan's result. `complete == false` (with `stop_offset` set to
// the byte offset the walk stopped at) is the ONLY channel through which
// this scanner reports a structural problem with the PACKET STREAM itself
// (no valid stride ever found, or a resync search that never found the
// next valid sync position before EOF) -- every field below is
// meaningless (zero-valued/empty) when `complete` is false. A per-packet
// or per-section malformation does NOT set `complete = false`; it
// increments `malformed_packets`/`discarded_sections` and the walk
// continues (this plan's own prohibition against aborting the whole scan
// over one bad packet).
struct TsScanResult {
  int stride = 0;
  std::int64_t total_packets = 0;
  std::int64_t null_packets = 0;
  std::int64_t malformed_packets = 0;
  std::int64_t resync_bytes_skipped = 0;
  std::int64_t discarded_sections = 0;

  // Heap-allocated (never a TsScanResult data member placed directly on
  // the stack): an 8192-entry PidStats array is a few hundred kilobytes,
  // too large to risk on a thread with a small default stack (e.g. some
  // platforms default a worker thread to a 1 MiB stack) -- wrapping it in
  // a unique_ptr keeps TsScanResult itself small (one pointer plus a
  // handful of scalars/vectors) so it can be constructed, moved through
  // mediadiff::expected, and returned by value with no stack-size risk,
  // while the PID domain itself stays the SAME structurally-bounded
  // 8192-entry array T-3-31 requires. `pid_stats()` below is the sole
  // accessor every caller in this project uses, so the indirection stays
  // an implementation detail.
  std::unique_ptr<std::array<PidStats, kPidCount>> pids;

  std::vector<PcrSample> pcr_samples;
  // Byte offsets of every PAT section occurrence (PID 0x0000) -- one PAT
  // covers every program at once, unlike PMTs, which are per-program
  // (TsProgram::pmt_offsets below).
  std::vector<std::int64_t> pat_offsets;
  std::vector<TsProgram> programs;
  std::optional<MuxRateEstimate> mux_rate;

  bool complete = false;
  std::int64_t stop_offset = 0;

  // PID 0x1FFF (kNullPid) is intentionally NEVER indexed here -- null
  // packets are counted in `null_packets` only, so
  // `sum(pid_stats(p).packets for p in 0..8191) + null_packets ==
  // total_packets` holds as an exact identity (this plan's own Task 1
  // Test 4) with no double-counting.
  const PidStats& pid_stats(int pid) const { return (*pids)[static_cast<std::size_t>(pid)]; }
  PidStats& pid_stats(int pid) { return (*pids)[static_cast<std::size_t>(pid)]; }
};

// Runs the bounded stride-autodetected packet walk (PAT/PMT parsing, PCR
// extraction, continuity-counter tracking, mux-rate estimation) against
// `utf8_path`, opened through this translation unit's own bounded reader
// (never DemuxSession -- this scanner is deliberately libav-free, matching
// bmff_scan/ebml_scan's own justification).
//
// The `Error` channel is reserved for "the file itself could not be opened
// at all" (ErrorKind::input_open) -- every STRUCTURAL problem with the
// packet stream is reported through `TsScanResult::complete`/`stop_offset`
// instead, never through this Error channel and never by throwing.
mediadiff::expected<TsScanResult, Error> run_ts_scan(const std::string& utf8_path);

namespace detail {

// PROBE-07: the ISO 13818-1 section 2.4.3.3 continuity-counter carve-outs,
// implemented as a single, separately-testable PURE function -- exposed
// here (mirrors ebml_scan.h's own detail::read_element_id_for_test
// precedent) specifically so tests/unit/test_ts_continuity.cpp can drive a
// table of nine hand-verified cases directly, without constructing a whole
// TS file. A test that only ever observed this function through a full
// run_ts_scan() call could not distinguish "the carve-out is implemented"
// from "the carve-out happens to hold on this one fixture" -- PROBE-07
// exists precisely because the naive implementation LOOKS right until you
// hand-verify the ISO carve-outs against it.
//
// One PID's continuity-tracking state, carried from one packet to the
// next. `initialized` is false both before the PID's first packet is seen
// AND immediately after a flagged discontinuity (which resets tracking to
// "next packet establishes a fresh baseline, whatever its CC value" --
// this plan's own action text: "the CC that follows [a discontinuity] is
// accepted regardless of value"). `duplicate_available` tracks whether the
// ONE permitted duplicate (identical CC, payload present) at the CURRENT
// expected position has already been consumed -- ISO 13818-1 permits
// exactly one repeated packet per position, not an unbounded run of them.
struct PidContinuityState {
  bool initialized = false;
  int expected_cc = 0;
  bool duplicate_available = true;
};

// Which counter (if any) a single continuity step should increment. `none`
// covers both a normal payload-carrying advance AND an adaptation-field-
// only packet (which the carve-out excludes from CC tracking entirely --
// this is the single most common false alarm in a hand-rolled CC checker,
// per this plan's own action text).
enum class ContinuityIncrement {
  none,
  duplicate,
  error,
  discontinuity,
};

struct ContinuityStepResult {
  PidContinuityState next_state;
  ContinuityIncrement increment;
};

// One continuity step. `continuity_counter` is the packet's own 4-bit CC
// field (0-15); `has_payload` is derived from `adaptation_field_control`
// (payload-only '01' or adaptation-plus-payload '11') -- the carve-out
// that an adaptation-field-only packet ('10') never advances or is checked
// against the expected counter; `discontinuity_indicator` is the
// adaptation field's own flag, when present (false when no adaptation
// field/no flags byte was read for this packet, which correctly disables
// the discontinuity carve-out on a packet that cannot possibly declare
// it).
ContinuityStepResult step_continuity(const PidContinuityState& prev, int continuity_counter, bool has_payload,
                                      bool discontinuity_indicator);

}  // namespace detail

}  // namespace mediadiff
