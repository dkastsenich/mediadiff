#pragma once

// PROBE-02: PacketScan, the one no-decode `av_read_frame` sweep every
// rate, timeline and size measurement in this project consumes (doc 02
// section 1.2). Borrows the AVFormatContext DemuxSession already opened
// (via DemuxSession::native_context()) -- never re-opens the file, never
// calls any avcodec_* decode function ITSELF (no avcodec_send_packet, no
// avcodec_receive_frame). Every packet's own {pts, dts, duration, size,
// flags, pos} survives verbatim -- pts/dts sentinels (AV_NOPTS_VALUE) are
// NEVER normalized to 0, since a fabricated 0 would place a timing-less
// packet at the origin of the DTS axis.
//
// PROBE-10's shared primitive IS this raw per-stream array
// (StreamPacketScan::packets), not a pre-computed statistics struct: a
// struct like `IntervalStats` was deliberately rejected -- it would force
// a premature choice of which derived statistic to bake in, and either
// underspecify one consumer or carry fields only one consumer uses,
// re-litigating scope every time a new consumer appears (this doc's own
// fragment stats, doc 04's timeline math, doc 06's size all need
// DIFFERENT derived statistics over the SAME raw array). Consumers derive
// their own statistic as a pure function over the shared, read-only
// array -- see src/probe/pass.h's ProbeResults for how it is shared:
// held by value/optional, handed out only as `const ProbeResults&`, never
// copied (a copy would double the footprint D-01's budget just bounded).
//
// PROBE-03 (04-01-PLAN.md Task 2): `run_packet_scan(DemuxSession&, const
// PacketScanRequest&)` optionally fuses probe/parser_scan.h's per-AU walk
// INSIDE this same `av_read_frame` loop -- never a second sweep, and
// probe/parser_scan.cpp's own StreamParserState (not this translation
// unit) is the only place an AVCodecContext is ever constructed; this file
// only ever calls into that helper's own no-decode contract, never
// avcodec_send_packet/avcodec_receive_frame itself, directly or
// indirectly. The pre-Phase-4 `run_packet_scan(DemuxSession&, const
// PacketScanLimits&)` overload below is preserved as a thin inline
// wrapper so every Phase-3 call site and test compiles untouched.

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/rational.h"
#include "probe/audio_decode.h"
#include "probe/parser_scan.h"
#include "util/expected.h"

// Opaque forward declaration, at global scope matching libav's own C
// declaration site (mirrors src/probe/demux_session.h's own
// `struct AVFormatContext;`) -- only detail::make_packet_record's own
// declaration below needs it, and only as an incomplete type; a caller
// that wants the complete definition includes libavcodec/packet.h itself,
// exactly as any real caller of av_packet_alloc would.
struct AVPacket;

namespace mediadiff {

class DemuxSession;

// doc 02 section 1.2's own cap: 5,000,000 packets per stream, beyond
// which that stream's own `partial` flag is set and no further packet
// for that stream is appended (T-3-10). Named so no bare literal appears
// at any use site (this plan's own acceptance criterion).
inline constexpr std::int64_t kMaxPacketsPerStream = 5'000'000;

// D-01's global probe-memory-budget default, in MEGABYTES (the same unit
// `--probe-memory-budget-mb` / `[probe] memory_budget_mb` use): 1024 MB
// (1 GiB). Generous headroom over doc 02's own ~40 B/packet x 5,000,000
// -packet worst case for a single stream (~190 MB), while still being a
// real, assertable ceiling rather than "unbounded".
inline constexpr std::int64_t kDefaultProbeMemoryBudgetMb = 1024;

// The current process-wide default PER-FILE PacketScan byte ceiling
// (D-01): the resolved global probe-memory budget divided by the
// resolved thread count for THIS invocation. Starts at
// kDefaultProbeMemoryBudgetMb converted to bytes (a resolved thread count
// of 1 -- the value every single-file command always resolves to) and is
// set at most once per CLI invocation, before any run_packet_scan call,
// by src/cli/commands/{compare,dir,inspect,snapshot}.cpp -- the SAME
// "resolved once, read many times across dir mode's worker threads"
// pattern src/probe/demux_session.h's own default_wall_clock_budget_ms()
// already established. `dir` mode calls derive_per_file_cap_bytes(budget,
// resolved_threads) before calling the setter; the three single-file
// commands call the setter with derive_per_file_cap_bytes(budget, 1)
// (equivalently, the whole resolved budget). Backed by a relaxed atomic
// for the same write-once-then-read-only reason
// default_wall_clock_budget_ms() documents.
std::int64_t default_packet_scan_max_bytes();
void set_default_packet_scan_max_bytes(std::int64_t bytes);

// D-01: `budget_bytes / threads`, integer division -- the derivation
// every command-entry-point call site performs before calling
// set_default_packet_scan_max_bytes. `threads <= 1` returns budget_bytes
// unchanged (the whole budget, matching a single in-flight file) rather
// than dividing by a value that could be zero. A pure function, so it is
// unit-testable directly, independent of any CLI wiring or process-wide
// state.
std::int64_t derive_per_file_cap_bytes(std::int64_t budget_bytes, int threads);

// One packet's own record, verbatim from AVPacket (doc 02 section 1.2):
// pts/dts/duration/size/pos exactly as libav reported them (AV_NOPTS_VALUE
// preserved, never normalized to 0 -- Test 4), flags as libav's own
// bitfield. int64 throughout, native timebase -- StreamPacketScan::tb
// carries the timebase itself (see below for why it is not duplicated
// per record).
struct PacketRecord {
  std::int64_t pts = 0;
  std::int64_t dts = 0;
  std::int64_t duration = 0;
  std::int64_t size = 0;
  std::int64_t pos = 0;
  int flags = 0;
};

// AVPacket::flags' AV_PKT_FLAG_KEY bit (libavcodec/packet.h) -- confirmed
// stable at 0x0001 since the flag's introduction. Exposed here, next to
// `PacketRecord::flags` itself (rather than hardcoded per-file, this
// project's usual convention for a libav-derived sentinel -- see
// stream_params.cpp's own `kUnknownProfileOrLevel` comment), because TWO
// independent analyzer families need the IDENTICAL bit value:
// src/analyzers/container/mp4.cpp's fragment-duration keyframe walk, and
// src/analyzers/video/frame_types.cpp's VIDEO-12 keyframe-vs-non-keyframe
// degraded histogram (04-09-PLAN.md) -- a single source of truth here is
// what keeps the two from silently drifting apart.
inline constexpr int kPacketFlagKeyframe = 0x0001;

// 05-20-PLAN.md (Gap 4, TIME-04, UD-3): which source produced this
// stream's own `packets[*].dts` values, set once by the orchestrator's
// container-DTS post-pass (src/probe/orchestrator.cpp) -- never inferred
// per-analyzer. `demuxer` (the default) means libavformat's own dts, as
// PacketScan read it verbatim: every non-TS container, and any MPEG-TS
// stream whose container truth could not be established. `container_pes`
// means every joined packet's dts now holds the value
// detail::apply_container_dts (src/probe/ts_scan.h) read directly from
// the stream's own PES headers (ISO/IEC 13818-1 section 2.4.3.7), never
// libavformat's read-back inference. `container_unavailable` means this
// MPEG-TS stream's PES timestamps could not be trusted for a substitution
// (ts_scan's own global PES-record budget was exhausted before this
// stream's list, or a packet's own byte offset falls at or beyond a
// partial scan's `stop_offset`) -- `dts` stays whatever the demuxer
// reported, and a DTS-axis consumer on this stream skips with
// `insufficient_data` rather than judging libavformat's own inferred
// value (05-VERIFICATION.md Gap 4, T-05-85's mitigation).
enum class DtsSource {
  demuxer,
  container_pes,
  container_unavailable,
};

// One stream's own packet array plus its byte total and timebase. `tb` is
// held ONCE per stream, not once per record -- every packet in a stream
// shares its stream's timebase, and per-record duplication would
// multiply the accounted footprint by two extra int64s for no
// information gain.
//
// `packets` is in `av_read_frame` READ ORDER and is NOT guaranteed
// dts-sorted -- a consumer that needs ordering sorts its own copy of the
// indices (plan 03-09's windowing depends on this note being present).
//
// `partial` is true when this stream hit EITHER ceiling first: doc 02's
// 5,000,000-packet-per-stream cap (kMaxPacketsPerStream), or D-01's
// accounted-byte budget (PacketScanLimits::max_bytes). Either way, the
// stream stopped appending rather than growing until the allocator fails
// (T-3-10) -- a truncated sweep, and every dependent check must skip
// rather than compute a number from it (D-02).
struct StreamPacketScan {
  std::vector<PacketRecord> packets;
  std::int64_t byte_total = 0;
  Rational tb{0, 1};
  bool partial = false;

  // D-09 (05-09-PLAN.md, TIME-06): the FIRST packet actually accepted into
  // `packets` for this stream is inspected for `AV_PKT_DATA_SKIP_SAMPLES`
  // side data, captured INSIDE this existing sweep -- never a second
  // av_read_frame call, never a decode. `std::optional` distinguishes "the
  // first packet carried no such side data" (std::nullopt) from "the side
  // data was present and reported zero" (a real, present 0) -- the same
  // absent-vs-zero reasoning `EbmlTrack::codec_delay_ns`
  // (src/probe/ebml_scan.h) and `PidStats::first_cc_error_offset`
  // (src/probe/ts_scan.h) already establish for this project's own sibling
  // fields. Only the stream's OWN first packet is ever inspected -- a
  // later packet carrying `AV_PKT_DATA_SKIP_SAMPLES` never overwrites this
  // value, whether or not the first packet itself carried the side data.
  // Exists because `PacketRecord` carries no side data of any kind today,
  // and `timeline.av_offset`'s priming resolver needs this signal without
  // opening a decode pass; Phase 6's `audio.priming` (`AUDIO-04`) is this
  // field's designed second consumer.
  std::optional<std::int64_t> first_packet_skip_samples;

  // D-09: `codecpar->initial_padding` verbatim, surfaced through this
  // existing per-stream scan seam (populated alongside `tb` above, from
  // the SAME already-open `AVFormatContext` -- no new libav call site) so
  // `timeline.av_offset`'s priming resolver never needs its own libav
  // include. A plain `std::int64_t`, not `std::optional`: libav's own
  // `codecpar->initial_padding` is always a real reported int (0 by
  // default) -- there is no distinct "absent" state to preserve here, only
  // "declared, and it says N" (05-RESEARCH.md Pattern 3: MP4's own
  // `initial_padding` is a real, reported 0, not an absence).
  std::int64_t initial_padding = 0;

  // 05-20-PLAN.md (Gap 4, TIME-04): which source this stream's own
  // `packets[*].dts` values currently hold, set once by the
  // orchestrator's container-DTS post-pass. `demuxer` by default --
  // every non-TS stream, and any MPEG-TS stream for which no
  // substitution was attempted or possible, keeps this default
  // untouched.
  DtsSource dts_source = DtsSource::demuxer;

  // 05-20-PLAN.md: detail::apply_container_dts's own ContainerDtsJoin
  // counters (src/probe/ts_scan.h), copied here once the orchestrator's
  // post-pass runs -- how many of this stream's packets had their dts
  // replaced by container truth (`dts_container_joined`), and how many
  // carried a non-negative `pos` but did not join any recorded PES
  // timestamp (`dts_unjoined_with_pos`). Both stay 0 when `dts_source`
  // is not `container_pes`.
  std::int64_t dts_container_joined = 0;
  std::int64_t dts_unjoined_with_pos = 0;

  // 05-REVIEW.md WR-01 fix (orchestrator fix spec point 1): per-packet
  // join outcome for this stream's own `packets[*].dts` substitution --
  // index i is `true` iff `packets[i].dts` was replaced by PES-header
  // truth (a join), `false` otherwise (a negative-`pos` split-out frame,
  // or a non-negative-`pos` packet that did not match any recorded PES
  // timestamp). Sized to `packets.size()` and populated ONLY when
  // `dts_source` is `container_pes` (empty otherwise, mirroring
  // `dts_container_joined`/`dts_unjoined_with_pos`'s own "both stay 0
  // unless container_pes" contract). `timeline.dts_monotonic` is the sole
  // consumer: it judges only the packets this marks joined, so a mixed
  // joined/unjoined DTS axis on one stream can never produce a spurious
  // violation at the boundary between PES-header truth and libavformat's
  // own inferred read-back.
  std::vector<bool> dts_joined;
};

// The whole scan's result: one StreamPacketScan per AVStream (doc 02
// section 1.2; sized from AVFormatContext::nb_streams, which libav
// itself bounds -- T-3-12), plus a whole-result `partial` that is true
// iff ANY stream is partial, or the sweep itself ended early on a
// non-EOF negative av_read_frame return (a truncated region of the file,
// also the D-02 case).
struct PacketScanResult {
  std::vector<StreamPacketScan> per_stream;
  bool partial = false;

  // The running accounted-byte total across every stream's store, as of
  // the moment the scan finished (or was truncated) --
  // sizeof(PacketRecord) times the number of packets actually appended,
  // summed over every stream. Set exactly once, at the end of
  // run_packet_scan, from the running total its own append loop tracked.
  // This is the number PacketScanLimits::max_bytes bounds; a test and a
  // diagnostic can both read it with zero OS interaction (D-01's own
  // "accounted, not measured" requirement).
  std::int64_t accounted_bytes = 0;

  // The number of av_read_frame calls this scan made, including the
  // terminating (AVERROR_EOF or truncating-error) call -- exposed so a
  // test can assert an EXACT sweep count (PROBE-10's "exactly one sweep"
  // proof) rather than merely trusting a comment. Always populated by
  // run_packet_scan; not gated behind any injectable seam, since it costs
  // nothing to compute unconditionally and every production caller is
  // free to ignore it.
  std::int64_t read_frame_call_count = 0;

  // The peak value `accounted_bytes` ever reached during the scan --
  // identical to `accounted_bytes` today, since the store only ever
  // grows during a scan (nothing is ever removed mid-sweep). Exposed
  // under its own name (a method, matching accounted_bytes' own field's
  // read-as-a-call-site usage in every consumer below) so a future
  // consumer that adds a shrink path does not have to guess which
  // meaning "accounted_bytes" originally carried.
  std::int64_t peak_accounted_bytes() const { return accounted_bytes; }
};

// The two independent ceilings PacketScan enforces (T-3-10), whichever
// binds first: `max_bytes` (D-01's derived per-file cap, defaulted from
// default_packet_scan_max_bytes() so a caller that constructs
// `PacketScanLimits{}` automatically gets the CLI-resolved value for this
// invocation) and `max_packets_per_stream` (doc 02's 5M cap, injectable
// here -- rather than hardcoded inside run_packet_scan -- so a unit test
// can exercise the ceiling at a small value without generating a
// 5,000,000-packet fixture).
struct PacketScanLimits {
  std::int64_t max_bytes = default_packet_scan_max_bytes();
  std::int64_t max_packets_per_stream = kMaxPacketsPerStream;
};

// PROBE-03 (04-01-PLAN.md Task 2): the request/outputs pair for the
// parser-enabled form of run_packet_scan below. `parse_access_units` is
// what src/probe/orchestrator.cpp sets from
// `union_passes.test(Pass::parser_scan)` -- when false, `access_units`
// stays std::nullopt and the whole call behaves identically to the
// pre-Phase-4 PacketScanLimits-only overload (Test 4).
// 06-01-PLAN.md (AUDIO-10, PROBE-08): `decode_audio` fuses the audio
// decode sweep (probe/audio_decode.h) INSIDE this same av_read_frame
// loop, mirroring `parse_access_units`'s own fusion exactly -- a stream's
// packet bytes are never retained past this loop iteration (D-01's own
// per-file budget), so a later, independent decode pass could not read
// them again without re-opening the file. Set from
// `union_passes.test(Pass::audio_decode)` by src/probe/orchestrator.cpp.
struct PacketScanRequest {
  PacketScanLimits limits{};
  bool parse_access_units = false;
  bool decode_audio = false;
};

struct PacketScanOutputs {
  PacketScanResult packets;
  std::optional<ParserScanResult> access_units;
  // Populated only when `PacketScanRequest::decode_audio` was set --
  // std::nullopt otherwise, mirroring `access_units`'s own contract.
  std::optional<AudioDecodeResult> audio_decode;
};

// One `av_read_frame` sweep of `session`'s already-open AVFormatContext,
// bounded by `request.limits`. Loops av_read_frame until it returns
// AVERROR_EOF (the clean end of the readable region); any OTHER negative
// return ends the sweep early and marks the WHOLE result `partial` (a
// stream that stopped early is exactly the D-02 case) -- never calls any
// avcodec_* decode function ITSELF (see this header's own top comment for
// the parser-fusion exception, entirely inside probe/parser_scan.cpp). A
// zero-packet input (a valid container with no readable packets) returns
// an empty-but-valid result, not an error. Never throws; every failure
// this function itself can produce (e.g. an av_packet_alloc allocation
// failure) returns through the Error channel.
//
// When `request.parse_access_units` is set, this is the ONE place either
// Pass::packet_scan's or Pass::parser_scan's own data is ever produced --
// fused INSIDE this same loop (probe/packet_scan.cpp), never a second
// sweep (PROBE-03, this plan's own single-sweep invariant,
// packet_scan.h's own read_frame_call_count field is the proof a test
// pins this against).
mediadiff::expected<PacketScanOutputs, Error> run_packet_scan(DemuxSession& session, const PacketScanRequest& request);

// Pre-Phase-4 overload, preserved for every existing call site and test
// (04-01-PLAN.md Task 2): a thin inline wrapper over the new form above
// with `parse_access_units = false`, discarding the (always-nullopt)
// ParserScanResult. Never touches probe/parser_scan.h's own detail:: seam
// itself -- the new overload it forwards to is what may.
inline mediadiff::expected<PacketScanResult, Error> run_packet_scan(DemuxSession& session,
                                                                      const PacketScanLimits& limits) {
  PacketScanRequest request;
  request.limits = limits;
  request.parse_access_units = false;
  auto outputs = run_packet_scan(session, request);
  if (!outputs) {
    return mediadiff::unexpected(outputs.error());
  }
  return std::move(outputs->packets);
}

namespace detail {

// Test-only extraction point (Test 4, PROBE-02's "AV_NOPTS_VALUE
// preserved, never normalized to 0" proof): converts one already-read
// AVPacket into its own PacketRecord, EXACTLY as run_packet_scan's own
// append loop does -- a straight field copy with no normalization
// branch, so a sentinel pts/dts survives verbatim. Exposed here so
// tests/unit/test_packet_scan.cpp can construct a synthetic AVPacket
// directly (via its own libavcodec/packet.h include) and observe the
// conversion in isolation, without needing a real fixture that happens
// to contain a genuinely-unset-timestamp packet -- FFmpeg's own
// muxers/demuxers reliably synthesize a pts for every packet they
// produce, so no such fixture is producible under this project's
// bitexact-only, no-committed-binaries fixture discipline (D-08).
PacketRecord make_packet_record(const AVPacket& pkt);

}  // namespace detail

}  // namespace mediadiff
