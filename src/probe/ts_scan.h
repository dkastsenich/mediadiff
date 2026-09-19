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
#include <span>
#include <string>
#include <vector>

#include "core/error.h"
#include "probe/packet_scan.h"
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

// 05-07-PLAN.md (TIME-02/TIME-04, T-05-28): the maximum number of
// discontinuity_indicator BYTE OFFSETS recorded per PID
// (PidStats::discontinuity_indicator_offsets below). A crafted TS stream
// can set the indicator on every single packet -- without this bound, the
// per-PID offset list would grow without limit, a memory-exhaustion
// vector, the exact same reasoning `kPidCount`/`kStrideConfirmCount`/
// `kMaxAfLen` (ts_scan.cpp) already carry for every other attacker-facing
// bound in this scanner. Chosen generously large relative to any
// legitimate discontinuity_indicator usage (a real muxer sets it rarely --
// at splice points, not in steady state) while keeping a fixed, small
// worst-case footprint per PID (kPidCount x this bound x 8 bytes).
inline constexpr std::int64_t kMaxDiscontinuityOffsetsPerPid = 256;

// 05-15-PLAN.md (TIME-04, Gap 4, ISO/IEC 13818-1 section 2.4.3.7): one
// PES header's own decode-timestamp truth, read directly from the PES
// header bytes -- never inferred by libavformat's own read-back
// heuristics (05-VERIFICATION.md's Gap 4: a `-c copy` MP4->TS remux
// dropping the MPEG-4 VOL header makes libavformat's `compute_pkt_fields`
// fabricate a DTS tie that does not exist in the container). `pts` is
// always present in a record; a PES header without a PTS
// (`PTS_DTS_flags` '00', or a header this scanner could not parse) never
// produces a record at all. `dts` equals `pts` when the header carries
// PTS only (`dts_present` false) -- ISO/IEC 13818-1's own rule that an
// absent DTS equals the PTS (UD-3), decided once here rather than left
// for every consumer to re-derive.
struct PesTimestampRecord {
  std::int64_t offset = 0;
  std::int64_t pts = 0;
  std::int64_t dts = 0;
  bool dts_present = false;
};

// 05-15-PLAN.md (T-05-68): the GLOBAL (not per-PID) cap on how many
// PesTimestampRecord entries this scanner will ever accumulate across
// every PID combined -- matching kMaxPacketsPerStream's own magnitude
// (probe/packet_scan.h). A crafted TS that signals a PES start on every
// single packet could otherwise grow this list without bound; reaching
// the cap sets `PidStats::pes_timestamps_truncated` on the PID that hit
// it rather than dropping a record silently.
inline constexpr std::int64_t kMaxPesTimestampRecordsTotal = 5'000'000;

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

  // 05-07-PLAN.md (TIME-04), 05-RESEARCH.md Pattern 5: the BYTE OFFSETS of
  // every transport packet on this PID whose adaptation field carried
  // `discontinuity_indicator=1`, in ASCENDING order (matching scan order,
  // which is byte-offset order) -- the seam that lets
  // `timeline.discontinuities`/`timeline.discontinuities.flagged` attribute
  // a presentation-time jump to flagged TS structure by joining against
  // `PacketRecord::pos`, WITHOUT a second byte-level adaptation-field
  // walker (this project's one audited TS parser, PROBE-06/07, stays the
  // single source of truth for this grammar). Bounded by
  // `kMaxDiscontinuityOffsetsPerPid` above; `discontinuity_offsets_truncated`
  // records whether more occurrences existed than the bound could hold --
  // the flag is always set once the bound is reached and never dropped
  // silently.
  std::vector<std::int64_t> discontinuity_indicator_offsets;
  bool discontinuity_offsets_truncated = false;

  // 05-15-PLAN.md (TIME-04, Gap 4): container-truth PES header timestamps
  // for every PES that starts on this PID, in ASCENDING offset order
  // (matching scan order) -- the seam `detail::apply_container_dts`
  // (below) joins demuxed packets against, the same "bounded seam, never
  // dropped silently" contract as `discontinuity_indicator_offsets`
  // above. Bounded GLOBALLY (across every PID) by
  // `kMaxPesTimestampRecordsTotal`; `pes_timestamps_truncated` is set,
  // never cleared, once the shared budget is exhausted.
  std::vector<PesTimestampRecord> pes_timestamps;
  // Exactly one of these four counters is incremented per unit-start
  // packet on this PID whose payload begins a parseable PES packet
  // (`detail::PesParseStatus::no_pes`/`excluded_stream_id` increment
  // nothing, since those are not PES headers this scanner failed to
  // parse -- they are bytes that were never a PES header to begin with,
  // or a PES header this scanner is not required to time-stamp).
  std::int64_t pes_headers_pts_only = 0;
  std::int64_t pes_headers_pts_dts = 0;
  std::int64_t pes_headers_no_pts = 0;
  std::int64_t pes_headers_unparsed = 0;
  bool pes_timestamps_truncated = false;
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

// 05-07-PLAN.md (TIME-04, T-05-28): appends `offset` to
// `stats.discontinuity_indicator_offsets`, bounded by
// `kMaxDiscontinuityOffsetsPerPid`. On reaching the bound, sets
// `stats.discontinuity_offsets_truncated` and appends nothing further --
// once set, the flag is never cleared and no further offset is ever
// recorded for this PID, even if the caller keeps invoking this function.
// Exposed here as a single, separately-testable PURE function over
// `PidStats` (mirrors `step_continuity`'s own exposure convention just
// above) so `tests/unit/test_ts_continuity.cpp` can drive a table of
// offsets directly, without constructing a whole TS file.
void record_discontinuity_offset(PidStats& stats, std::int64_t offset);

// 05-15-PLAN.md (TIME-04, Gap 4, ISO/IEC 13818-1 section 2.4.3.7): the
// only possible outcomes of parsing one unit-start packet's payload as a
// PES header. `no_pes`/`excluded_stream_id` are not parse failures --
// they mean "these bytes were never a PES header this scanner needed to
// time-stamp" (a payload that does not start `00 00 01`, or a stream_id
// ISO 13818-1 defines as carrying no optional PES header at all, e.g.
// padding_stream). `malformed`/`truncated_in_packet` ARE parse failures
// (an out-of-spec header, or one whose declared length runs past the
// bytes this one TS packet actually carries) -- `parse_pes_timestamps`
// never fabricates a timestamp in either case.
enum class PesParseStatus {
  no_pes,
  no_timestamps,
  pts_only,
  pts_dts,
  excluded_stream_id,
  malformed,
  truncated_in_packet,
};

struct PesParseResult {
  PesParseStatus status = PesParseStatus::no_pes;
  std::int64_t pts = 0;
  std::int64_t dts = 0;
};

// Parses `payload` (the bytes of a unit-start packet, from the payload
// start index to the end of the 188-byte TS packet) as a PES header,
// reading ONLY the fields needed to establish PTS/DTS presence and value
// (ISO/IEC 13818-1 section 2.4.3.7). Every index is checked against
// `payload.size()` before the read -- `payload` is the only data source,
// and the 4-bit prefix ahead of each PTS/DTS field is never validated
// (05-15-PLAN.md's own A2: only the three marker bits make the bit
// layout unambiguous). Exposed here, mirroring `step_continuity`'s own
// exposure convention, so `tests/unit/test_ts_scan.cpp` can drive a table
// of hand-built byte buffers directly, without constructing a whole TS
// packet.
PesParseResult parse_pes_timestamps(std::span<const std::uint8_t> payload);

// 05-15-PLAN.md (T-05-68): appends `record` to `stats.pes_timestamps`
// when `remaining_budget` is above 0 (decrementing it); otherwise sets
// `stats.pes_timestamps_truncated` and appends nothing. `remaining_budget`
// is threaded through `run_ts_scan` from a single counter initialized to
// `kMaxPesTimestampRecordsTotal`, shared across every PID -- mirrors
// `record_discontinuity_offset`'s own bounded-seam shape, except the
// budget here is global rather than per-PID.
void record_pes_timestamp(PidStats& stats, const PesTimestampRecord& record, std::int64_t& remaining_budget);

// 05-15-PLAN.md (TIME-04, Gap 4): the outcome of `apply_container_dts`
// below -- how many demuxed packets had their `dts` replaced by
// container truth (`joined`), and how many carried a non-negative `pos`
// (so a join was structurally possible) but did not match any recorded
// PES timestamp (`unjoined_with_pos`). A packet with a negative `pos` (a
// frame the demuxer split out of a multi-frame PES, carrying no PES
// header of its own) is neither counted nor touched.
struct ContainerDtsJoin {
  std::int64_t joined = 0;
  std::int64_t unjoined_with_pos = 0;
};

// Joins `packets` (one stream's demuxed `PacketRecord`s, in `pos`-ascending
// or arbitrary order -- each is looked up independently) against `records`
// (one PID's `PidStats::pes_timestamps`, ASCENDING by offset, the
// invariant that struct's own comment guarantees), replacing `dts` in
// place wherever `record.offset == packet.pos + (ts_packet_size - 188)`
// AND the packet's own raw `pts` equals the record's `pts` -- the pts
// match is what stops an unrelated record at a coincidentally-reused
// offset from attaching to the wrong packet (T-05-70), and is why a wrong
// `ts_packet_size` can only ever fail to join, never produce a false one
// (05-REVIEW.md WR-01 fix). `ts_packet_size` is the stride `run_ts_scan`
// detected (188, 192 or 204, `TsScanResult::stride`) -- ts_scan's own
// recorded `PesTimestampRecord::offset` is a byte offset in the FULL
// container stride, while libavformat's `PacketRecord::pos` is a logical
// 188-byte-packet-stream offset; the two differ by exactly
// `ts_packet_size - 188` on every packet (05-15-SUMMARY.md's own
// measurement: +4 on `ts_192.ts`, +16 on `ts_204.ts`, both stride-wide
// constants). Defaults to 188 (no adjustment) so every existing 2-arg call
// site and test keeps behaving identically. On a join, `dts` becomes the
// record's `dts` when `dts_present`, otherwise the record's `pts`
// (ISO/IEC 13818-1's own absent-DTS-equals-PTS rule, UD-3). A packet
// whose `pos` is negative is left untouched and uncounted; a packet with
// a non-negative `pos` that does not join keeps its own `dts` unchanged.
// When `joined_mask` is non-null, it is resized to `packets.size()` and
// set to `true` at every index that joined, `false` everywhere else --
// `timeline.dts_monotonic` is this seam's consumer (05-REVIEW.md WR-01
// fix): it judges only the packets this mask marks joined, so a mixed
// joined/unjoined DTS axis on one stream can never produce a spurious
// violation at the boundary between PES-header truth and libavformat's
// own inferred read-back. A pure function otherwise: 05-20 is the plan
// that wires this into the orchestrator's DTS-axis consumers.
ContainerDtsJoin apply_container_dts(std::span<PacketRecord> packets, std::span<const PesTimestampRecord> records,
                                      int ts_packet_size = 188, std::vector<bool>* joined_mask = nullptr);

// 05-REVIEW.md WR-01 fix (orchestrator fix spec point 2): resolves one
// MPEG-TS stream's own `DtsSource` from `apply_container_dts`'s own join
// outcome for that stream. Zero packets joined means container truth
// could not be established for this stream AT ALL -- reported as
// `container_unavailable` (the SAME "fully joined or skip" gate
// `timeline.dts_monotonic` already trusts for the pre-existing
// `pes_timestamps_truncated`/partial-scan reasons), never `container_pes`
// over a stream where nothing actually joined. A pure function so the
// orchestrator's own decision is directly unit-testable without a real
// TS fixture.
inline DtsSource resolve_dts_source(const ContainerDtsJoin& join) {
  return join.joined > 0 ? DtsSource::container_pes : DtsSource::container_unavailable;
}

}  // namespace detail

}  // namespace mediadiff
