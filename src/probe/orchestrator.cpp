#include "probe/orchestrator.h"

#include <cctype>
#include <cstdio>
#include <utility>
#include <vector>

#include "analyzers/audio/analyzers.h"
#include "analyzers/container/analyzers.h"
#include "analyzers/content/analyzers.h"
#include "analyzers/size/analyzers.h"
#include "analyzers/timeline/analyzers.h"
#include "analyzers/timeline/unwrap.h"
#include "analyzers/video/analyzers.h"
#include "core/snapshot.h"
#include "probe/bmff_scan.h"
#include "probe/demux_session.h"
#include "probe/ebml_scan.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/ts_scan.h"
#include "util/fs.h"
#include "util/version.h"

namespace mediadiff {

namespace {

// Peeks at `utf8_path`'s first non-whitespace byte and reports whether it
// is `{` -- the shape every *.snap.json this project ever writes or reads
// begins with (core/snapshot.cpp writes an ordered_json object; nothing
// under core/ ever emits a top-level JSON array or scalar). read_snapshot's
// own ErrorKind::input_unsupported covers THREE distinct causes (core/
// snapshot.h's own doc comment): "not valid JSON at all", "a
// schema_version whose MAJOR component differs", and "a measurement
// naming an unregistered check ID". Falling through to a probe attempt is
// correct only for the first cause -- a file that IS JSON-shaped but was
// explicitly REJECTED by read_snapshot for a version/content reason must
// surface THAT rejection, not a much less informative "could not probe
// input" from a DemuxSession::open attempt that was never going to
// succeed on JSON bytes anyway (found via
// tests/integration/test_schema_version.cpp and
// test_type_poisoned_snapshot.cpp, both of which assert read_snapshot's
// own message survives to stderr). A file that is NOT JSON-shaped at all
// (a real media file, or Task 1's own "not a media file" text fixture)
// still falls through and reaches DemuxSession::open exactly as before.
bool looks_like_json_document(const std::string& utf8_path) {
  FILE* handle = fopen_utf8(utf8_path, "rb");
  if (handle == nullptr) {
    return false;
  }
  int ch = 0;
  while ((ch = std::fgetc(handle)) != EOF) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    break;
  }
  std::fclose(handle);
  return ch == '{';
}

}  // namespace

// The full analyzer list, assembled in one explicit, hand-written order
// (03-02-PLAN.md Task 3, PROBE-08): byte-identical --json across runs AND
// across platforms (TRUST-05) cannot depend on self-registering
// static-init order, which is unspecified across translation units. Each
// family's own .cpp exposes a named `const AnalyzerSpec&
// <family>_analyzer()` accessor (src/analyzers/container/analyzers.h);
// this is the one place they are assembled into a list, and the only
// place a future family's analyzer gets added.
const std::vector<AnalyzerSpec>& all_analyzers() {
  static const std::vector<AnalyzerSpec> registry = {
      container_topology_analyzer(),
      // 03-04-PLAN.md Tasks 2-3: meta.tags/meta.tags.language.
      container_meta_analyzer(),
      // 03-05-PLAN.md Task 2 (CONT-05): the six container.mp4.* checks --
      // real data ONLY for an actual MP4 input (ContainerFamily::mp4
      // scope, so Pass::bmff_scan never runs for a non-MP4 file), plus a
      // family-agnostic sibling (below) that emits the
      // skipped:not_applicable_container half on every OTHER container.
      // Listed before that sibling so a stable, hand-written analyzer
      // order (TRUST-05) has the "real" producer first.
      container_mp4_analyzer(),
      container_mp4_not_applicable_analyzer(),
      // 03-06-PLAN.md Tasks 1-2 (PROBE-05, CONT-06): the four
      // container.mkv.* checks -- same real-data-first, not-applicable-
      // sibling-second ordering as the mp4 pair above.
      container_mkv_analyzer(),
      container_mkv_not_applicable_analyzer(),
      // 03-08-PLAN.md Tasks 1-3 (CONT-07, CONT-08): the six container.ts.*
      // checks -- same real-data-first, not-applicable-sibling-second
      // ordering as the mp4/mkv pairs above.
      container_ts_analyzer(),
      container_ts_not_applicable_analyzer(),
      // 03-09-PLAN.md (SIZE-01): the four size.* checks -- family-agnostic
      // (ContainerFamily::other), so listed once with no not-applicable
      // sibling (unlike the three container.<family>.* pairs above, every
      // size.* check applies to every container this project probes).
      // Declares Pass::packet_scan, PROBE-10's shared array -- no second
      // sweep, no pre-computed statistics struct.
      size_analyzer(),
      // 04-06-PLAN.md (VIDEO-01, VIDEO-02): the five per-video-stream
      // identity checks (video.codec/profile/level/resolution/frame_count)
      // -- codecpar-only extraction, no registered parser required.
      // Listed before video_gop_analyzer() (below) so a stable,
      // hand-written analyzer order (TRUST-05) keeps the "identity"
      // checks first.
      video_stream_params_analyzer(),
      // 04-08-PLAN.md (VIDEO-03, VIDEO-07, VIDEO-08): video.pix_fmt and
      // the five colour-identity checks -- codecpar-only extraction, no
      // scan of any kind required. Listed directly after
      // video_stream_params_analyzer() so the "identity" checks
      // (TRUST-05's stable, hand-written analyzer order) stay grouped.
      video_color_analyzer(),
      // 04-01-PLAN.md Task 2 (PROBE-03, VIDEO-05): video.gop.length, the
      // phase's tracer check -- Pass::parser_scan's own first production
      // consumer. Declares Pass::parser_scan explicitly (not left to this
      // file's own parser_scan-implies-packet_scan rule below) so this
      // AnalyzerSpec's own required_passes is self-describing.
      video_gop_analyzer(),
      // 04-09-PLAN.md Task 2 (VIDEO-01/VIDEO-05, VIDEO-12): video.frame_types
      // -- listed directly after video_gop_analyzer() so the GOP/frame-type
      // family (the checks this phase's ParserScan-consuming analyzers
      // produce) stays grouped in the stable, hand-written analyzer order
      // (TRUST-05).
      video_frame_types_analyzer(),
      // 04-10-PLAN.md (VIDEO-06): video.interlace -- listed directly after
      // video_frame_types_analyzer() so every ParserScan-consuming video
      // analyzer stays grouped in the stable, hand-written analyzer order
      // (TRUST-05).
      video_interlace_analyzer(),
      // 04-11-PLAN.md (VIDEO-09): video.hdr.mdcv/.luminance/.primaries and
      // video.hdr.cll/.max/.avg -- listed directly after
      // video_interlace_analyzer() so every codecpar-only-extraction video
      // analyzer (video_color_analyzer(), and now this one) stays grouped
      // with the "identity" checks in the stable, hand-written analyzer
      // order (TRUST-05), rather than interleaved among the ParserScan-
      // consuming ones just above it.
      video_hdr_analyzer(),
      // 05-01-PLAN.md Task 2 (TIME-01/TIME-03, D-03): timeline.start, this
      // phase's tracer -- ContainerFamily::other (a timeline check applies
      // to every container), Pass::packet_scan only (no parser_scan, no
      // bmff/ebml/ts_scan of its own -- it opportunistically reads
      // results.bmff/results.ebml for evidence ONLY when another
      // applicable analyzer already populated them for this file). Listed
      // last, after every video analyzer, so the stable, hand-written
      // analyzer order (TRUST-05) keeps this phase's own family grouped
      // and appended, never interleaved among Phase 4's.
      timeline_start_duration_analyzer(),
      // 05-05-PLAN.md (TIME-01/TIME-04): timeline.dts_monotonic and
      // timeline.pts_unique -- ContainerFamily::other (applies to every
      // container; the TS-only unwrap step is a runtime branch inside the
      // analyzer, not a narrower AnalyzerSpec scope), Pass::packet_scan
      // only. Listed directly after timeline_start_duration_analyzer() so
      // this phase's own family stays grouped in the stable, hand-written
      // analyzer order (TRUST-05).
      timeline_monotonic_analyzer(),
      // 05-07-PLAN.md (TIME-02/TIME-04): timeline.discontinuities and
      // timeline.discontinuities.flagged -- same real-data-first,
      // not-applicable-sibling-second ordering as the mp4/mkv/ts pairs
      // above (container_ts_analyzer()/container_ts_not_applicable_analyzer());
      // here the family-agnostic (ContainerFamily::other) spec is listed
      // first since it owns every non-TS container AND is the one that
      // silently no-ops on a TS input, deferring to the ts-scoped sibling.
      timeline_discontinuities_analyzer(),
      timeline_discontinuities_ts_analyzer(),
      // 05-08-PLAN.md (TIME-05): timeline.jitter and timeline.vfr_profile,
      // both consuming the SAME derive_cadence call -- ContainerFamily::other,
      // Pass::demux_header + Pass::packet_scan only, listed directly after
      // this phase's own discontinuities pair so the family stays grouped
      // in the stable, hand-written analyzer order (TRUST-05).
      timeline_jitter_vfr_analyzer(),
      // 05-09-PLAN.md (TIME-06/TIME-09/TIME-10): timeline.av_offset --
      // listed directly after this phase's own jitter/vfr_profile pair so
      // the family stays grouped in the stable, hand-written analyzer
      // order (TRUST-05).
      timeline_av_sync_analyzer(),
      // 05-11-PLAN.md (TIME-11): timeline.timecode / timeline.timecode.value
      // -- listed directly after this phase's own av_offset/av_drift
      // analyzer so the family stays grouped in the stable, hand-written
      // analyzer order (TRUST-05). Pass::demux_header only -- no scan of
      // any kind needed, matching video_color_analyzer()'s own shape.
      timeline_timecode_analyzer(),
      // 06-01-PLAN.md (this phase's tracer): content.audio.sample_hash --
      // ContainerFamily::other (a content check applies to every
      // container), declares Pass::audio_decode (implied into the union
      // alongside Pass::packet_scan below). Listed last, after every
      // Phase 5 analyzer, so the stable, hand-written analyzer order
      // (TRUST-05) keeps this phase's own family appended, never
      // interleaved among Phase 5's.
      content_audio_sample_hash_analyzer(),
      // 06-03-PLAN.md (AUDIO-01, AUDIO-02): audio.codec/sample_rate/
      // sample_fmt/bit_depth/channels/layout -- the six per-audio-stream
      // identity checks, mirroring video_stream_params_analyzer()'s own
      // codecpar-only extraction shape. Listed directly after
      // content_audio_sample_hash_analyzer() so this phase's own
      // registrations stay grouped and appended in commit order (06-01
      // then 06-03), never interleaved among Phase 5's.
      audio_stream_params_analyzer(),
      // 06-06-PLAN.md (AUDIO-04): audio.priming -- completes Phase 5's
      // priming precedence chain over the shared PacketScan array,
      // reading results.bmff/results.ebml opportunistically for D-15's
      // container-mechanism tier (never declaring Pass::bmff_scan/
      // Pass::ebml_scan itself). Listed directly after
      // audio_stream_params_analyzer() so this phase's own registrations
      // stay grouped and appended in commit order, never interleaved among
      // Phase 5's.
      audio_priming_analyzer(),
      // 06-08-PLAN.md (AUDIO-05, AUDIO-06): audio.loudness.integrated/
      // .true_peak -- a pure consumer of the SAME shared decode sweep's
      // own libebur128 sink outputs content_audio_sample_hash_analyzer()
      // already declares Pass::audio_decode for. Listed directly after
      // audio_priming_analyzer() so this phase's own registrations stay
      // grouped and appended in commit order, never interleaved among
      // Phase 5's.
      audio_loudness_analyzer(),
  };
  return registry;
}

namespace detail {

mediadiff::expected<Fingerprint, Error> run_probe(const std::string& utf8_path,
                                                     const std::vector<AnalyzerSpec>& analyzers,
                                                     PassExecutionLog* pass_log, const ProbeOptions& options) {
  auto session_result = DemuxSession::open(utf8_path, DemuxOptions{});
  if (!session_result) {
    return mediadiff::unexpected(session_result.error());
  }
  DemuxSession session = std::move(*session_result);

  const ContainerFamily family = container_family_from_format_name(session.format_name());

  // Select every analyzer whose scope applies (`ContainerFamily::other`
  // means "every container") in `analyzers`' own order -- an analyzer
  // scoped to a different family is not run at all, which is what keeps
  // the pass union honest rather than producing meaningless
  // not-applicable noise (this plan's own action text).
  std::vector<const AnalyzerSpec*> applicable;
  PassSet union_passes;
  // Pass::demux_header always runs -- opening the DemuxSession above
  // already performed it, regardless of whether any applicable analyzer
  // happens to declare it (Task 3 Test 3: zero applicable analyzers still
  // produces a valid Fingerprint with demux_header having run and no
  // other pass executed).
  union_passes.set(Pass::demux_header);
  for (const AnalyzerSpec& spec : analyzers) {
    if (spec.scope != ContainerFamily::other && spec.scope != family) {
      continue;
    }
    applicable.push_back(&spec);
    union_passes |= spec.required_passes;
  }

  // 06-01-PLAN.md (Claude's Discretion, "--content/--no-content"): when
  // content decode is disabled for this invocation, Pass::audio_decode is
  // removed from the union entirely -- ProbeResults::audio_decode stays
  // std::nullopt regardless of which analyzers declared the pass, and
  // every decode-consuming analyzer reports skipped:requires_decode
  // rather than a fabricated value.
  if (!options.content_enabled) {
    union_passes.clear(Pass::audio_decode);
  }

  // PROBE-03 (04-01-PLAN.md Task 2): an analyzer that declared ONLY
  // Pass::parser_scan would otherwise get no sweep run at all -- the
  // parser's own data is produced INSIDE Pass::packet_scan's own arm
  // below (the fused loop in probe/packet_scan.cpp), so packet_scan must
  // always be in the union whenever parser_scan is.
  if (union_passes.test(Pass::parser_scan)) {
    union_passes.set(Pass::packet_scan);
  }

  // 06-01-PLAN.md (AUDIO-10, PROBE-08): mirrors the parser_scan
  // implication immediately above -- Pass::audio_decode's own data is
  // produced INSIDE Pass::packet_scan's own arm too (probe/packet_scan.cpp's
  // fused loop), so packet_scan must always be in the union whenever
  // audio_decode is.
  if (union_passes.test(Pass::audio_decode)) {
    union_passes.set(Pass::packet_scan);
  }

  // 05-20-PLAN.md (Gap 4, TIME-04): on MPEG-TS, packet_scan implies
  // ts_scan -- the container-DTS post-pass below needs 05-15's own PES
  // seam (PidStats::pes_timestamps) whenever packets are scanned on a TS
  // input, even when no applicable analyzer's own scope declared
  // Pass::ts_scan directly (mirrors the parser_scan-implies-packet_scan
  // rule just above).
  if (family == ContainerFamily::ts && union_passes.test(Pass::packet_scan)) {
    union_passes.set(Pass::ts_scan);
  }

  ProbeResults results;
  mediadiff::expected<void, Error> packet_scan_error;
  mediadiff::expected<void, Error> bmff_scan_error;
  mediadiff::expected<void, Error> ebml_scan_error;
  mediadiff::expected<void, Error> ts_scan_error;
  union_passes.for_each([&](Pass pass) {
    if (pass == Pass::demux_header) {
      results.demux = &session;
    } else if (pass == Pass::ts_scan) {
      // PROBE-06/PROBE-07 (03-07-PLAN.md Tasks 1-3): mirrors
      // Pass::bmff_scan/Pass::ebml_scan's own arms -- in the union
      // whenever an applicable analyzer's scope is ContainerFamily::ts.
      // container_ts_analyzer() (src/analyzers/container/ts.cpp,
      // 03-08-PLAN.md) is the real, registered production consumer that
      // declares this pass; this arm is not dead code. run_ts_scan opens
      // `utf8_path` itself (deliberately libav-free, independent of
      // DemuxSession) rather than reading through the already-open
      // session.
      auto scan_result = run_ts_scan(utf8_path);
      if (scan_result) {
        results.ts = std::move(*scan_result);
      } else {
        ts_scan_error = mediadiff::unexpected(scan_result.error());
      }
    } else if (pass == Pass::ebml_scan) {
      // PROBE-05 (03-06-PLAN.md Task 1): mirrors Pass::bmff_scan's own
      // arm below -- only ever in the union when an applicable analyzer's
      // scope is ContainerFamily::mkv (container_mkv_analyzer(),
      // src/analyzers/container/mkv.cpp), never for an MP4/TS input.
      // run_ebml_scan opens `utf8_path` itself (deliberately libav-free,
      // independent of DemuxSession) rather than reading through the
      // already-open session.
      auto scan_result = run_ebml_scan(utf8_path);
      if (scan_result) {
        results.ebml = std::move(*scan_result);
      } else {
        ebml_scan_error = mediadiff::unexpected(scan_result.error());
      }
    } else if (pass == Pass::bmff_scan) {
      // PROBE-04 (03-05-PLAN.md Task 1): this pass is only ever in the
      // union when an applicable analyzer's scope is ContainerFamily::mp4
      // (container_mp4_analyzer(), src/analyzers/container/mp4.cpp) --
      // never for an MKV/TS input, matching this plan's own prohibition
      // that the scanner never runs on bytes it cannot interpret.
      // run_bmff_scan opens `utf8_path` itself (it is deliberately
      // libav-free, independent of DemuxSession) rather than reading
      // through the already-open session.
      auto scan_result = run_bmff_scan(utf8_path);
      if (scan_result) {
        results.bmff = std::move(*scan_result);
      } else {
        bmff_scan_error = mediadiff::unexpected(scan_result.error());
      }
    } else if (pass == Pass::packet_scan) {
      // PROBE-02/PROBE-03/PROBE-10 (03-03-PLAN.md Task 1, extended by
      // 04-01-PLAN.md Task 2): still exactly one av_read_frame sweep --
      // stored once in ProbeResults and handed to every applicable
      // analyzer as a const reference -- PassSet's own "each pass runs
      // exactly once" guarantee (PROBE-08) is what makes this a SINGLE
      // sweep even when more than one analyzer declared Pass::packet_scan.
      // PacketScanLimits{} default-constructs its own max_bytes from
      // default_packet_scan_max_bytes() -- the per-file cap this
      // invocation's own command entry point already resolved and set
      // (D-01) -- so this call site never has to know that value itself.
      // `parse_access_units` fuses Pass::parser_scan's own per-AU walk
      // INSIDE this same call (never a second dispatch arm; the
      // implication just above guarantees packet_scan is always in
      // `union_passes` whenever parser_scan is, so this IS the only place
      // either pass's own data is ever produced).
      // 06-01-PLAN.md (AUDIO-10, PROBE-08): mirrors `parse_access_units`'s
      // own comment immediately above -- `decode_audio` fuses
      // Pass::audio_decode's own per-stream decode INSIDE this same call
      // (never a second dispatch arm; the implication above guarantees
      // packet_scan is always in `union_passes` whenever audio_decode is).
      PacketScanRequest request;
      request.limits = PacketScanLimits{};
      request.parse_access_units = union_passes.test(Pass::parser_scan);
      request.decode_audio = union_passes.test(Pass::audio_decode);
      // 06-05-PLAN.md (AUDIO-09, D-08): the ONLY input to decoder
      // selection -- never a profile (this invocation's own resolved
      // Policy is not even in scope here).
      request.hash_decoder = options.hash_decoder;
      auto scan_result = run_packet_scan(session, request);
      if (scan_result) {
        results.packet_scan = std::move(scan_result->packets);
        results.parser_scan = std::move(scan_result->access_units);
        results.audio_decode = std::move(scan_result->audio_decode);
      } else {
        packet_scan_error = mediadiff::unexpected(scan_result.error());
      }
    }
    // Later plans (the three raw scanners) add their own pass bodies as
    // new arms here.
    if (pass_log != nullptr) {
      pass_log->push_back(pass);
    }
  });

  if (!packet_scan_error) {
    // A PacketScan failure is exceptional (e.g. an av_packet_alloc
    // allocation failure) -- every other outcome, including a truncated
    // sweep, is a successful (if `partial`) PacketScanResult, not an
    // Error. Propagated as a hard error rather than silently leaving
    // results.packet_scan unset, so the caller sees the real cause
    // instead of a later, less informative "no packet_scan data" surprise
    // from whichever analyzer needed it.
    return mediadiff::unexpected(packet_scan_error.error());
  }
  if (!bmff_scan_error) {
    // Reserved for "the file could not be opened at all" (ErrorKind::
    // input_open) -- every STRUCTURAL box-tree problem is instead carried
    // in a successful BmffScanResult with complete=false, per bmff_scan.h's
    // own contract, and never reaches this branch.
    return mediadiff::unexpected(bmff_scan_error.error());
  }
  if (!ebml_scan_error) {
    // Same reservation as bmff_scan_error above, mirrored for
    // ebml_scan.h's own complete/stop_offset contract.
    return mediadiff::unexpected(ebml_scan_error.error());
  }
  if (!ts_scan_error) {
    // Same reservation as bmff_scan_error/ebml_scan_error above, mirrored
    // for ts_scan.h's own complete/stop_offset contract.
    return mediadiff::unexpected(ts_scan_error.error());
  }

  // 05-17-PLAN.md (Gap 2, TIME-02/TIME-03, WINDOWS.md #26): on a
  // genuinely-wrapping MPEG-TS input, DemuxSession's own primary session
  // (opened with correct_ts_overflow=0, demux_session.h's own header
  // comment) reports wrap-corrupted container/per-stream declared
  // durations. session.reprobe_ts_declared_durations() recovers them from
  // a second, overflow-corrected open -- run ONLY when the packet scan
  // itself observed at least one wrap, via 05-16-PLAN.md's own
  // TimelinePacketView::pts_wrap_events()/dts_wrap_events() (the SAME
  // wrap-detecting primitive timeline.wrap_events already uses, never a
  // second wrap detector), so every non-wrapping file -- the common case,
  // and every non-TS file -- pays nothing for this step. Must run before
  // any analyzer below reads a declared duration.
  if (family == ContainerFamily::ts && results.packet_scan.has_value()) {
    bool any_wrap = false;
    for (const StreamPacketScan& stream : results.packet_scan->per_stream) {
      const TimelinePacketView view = make_timeline_packet_view(stream, /*is_ts=*/true);
      if (view.pts_wrap_events() > 0 || view.dts_wrap_events() > 0) {
        any_wrap = true;
        break;
      }
    }
    if (any_wrap) {
      session.reprobe_ts_declared_durations(utf8_path);
    }
  }

  // 05-20-PLAN.md (Gap 4, TIME-04, UD-3): the container-DTS post-pass --
  // on MPEG-TS, substitute the container's own PES-header DTS truth
  // (05-15's detail::apply_container_dts) for every stream's dts, once
  // here, ahead of every analyzer, so every DTS-axis consumer
  // (timeline.dts_monotonic, size.stream_bitrate, size.peak_bitrate,
  // derive_cadence's DTS fallback) reads the SAME substituted values --
  // there is no per-analyzer patch. Never runs on a non-TS input; when
  // results.packet_scan is unset (Pass::packet_scan not in this file's
  // union), there is nothing to substitute.
  if (family == ContainerFamily::ts && results.packet_scan.has_value()) {
    for (std::size_t i = 0; i < results.packet_scan->per_stream.size(); ++i) {
      StreamPacketScan& stream = results.packet_scan->per_stream[i];
      const std::int64_t pid = session.stream_info(static_cast<int>(i)).stream_id;
      if (!results.ts.has_value() || pid < 0 || pid > 8191) {
        // No PES seam to join against at all, or a stream_id libavformat
        // never set to a real PID -- container truth cannot be
        // established for this stream.
        stream.dts_source = DtsSource::container_unavailable;
        continue;
      }
      const PidStats& pid_stats = results.ts->pid_stats(static_cast<int>(pid));
      bool unavailable = pid_stats.pes_timestamps_truncated;
      if (!unavailable && !results.ts->complete) {
        for (const PacketRecord& pkt : stream.packets) {
          if (pkt.pos >= results.ts->stop_offset) {
            unavailable = true;
            break;
          }
        }
      }
      if (unavailable) {
        // ts_scan's own global PES-record budget was exhausted before
        // this PID's list, or this stream carries a packet at/after a
        // partial scan's stop_offset -- container truth is not fully
        // known for this stream; a DTS-axis consumer skips rather than
        // judging libavformat's own inferred values (T-05-85).
        stream.dts_source = DtsSource::container_unavailable;
        continue;
      }
      // 05-REVIEW.md WR-01 fix (orchestrator fix spec point 3): the
      // stride-aware join -- `results.ts->stride` is the packet size
      // ts_scan detected (188, 192 or 204), which `apply_container_dts`
      // needs to translate its own recorded PES offsets (measured in the
      // FULL container stride) into libavformat's `PacketRecord::pos`
      // convention (a logical 188-byte-packet-stream offset) before the
      // join lookup. `joined_mask`, sized to this stream's own packets,
      // records exactly which ones joined -- timeline.dts_monotonic
      // (fix spec point 1) judges only those, never a mix of PES-header
      // truth and libavformat's own inferred read-back.
      std::vector<bool> joined_mask;
      const mediadiff::detail::ContainerDtsJoin join = mediadiff::detail::apply_container_dts(
          stream.packets, pid_stats.pes_timestamps, results.ts->stride, &joined_mask);
      stream.dts_container_joined = join.joined;
      stream.dts_unjoined_with_pos = join.unjoined_with_pos;
      // Fix spec point 2: zero packets joined means container truth could
      // not be established for this stream at all -- report
      // container_unavailable (never container_pes with nothing actually
      // joined) so timeline.dts_monotonic skips through the existing gate
      // rather than judging libavformat's own inferred values under a
      // container_pes label.
      stream.dts_source = mediadiff::detail::resolve_dts_source(join);
      if (stream.dts_source == DtsSource::container_pes) {
        stream.dts_joined = std::move(joined_mask);
      }
    }
  }

  Fingerprint fp;
  fp.envelope.schema_version = std::string(kSchemaVersion);
  fp.envelope.tool_version = tool_version();
  // Folds the header pass's own libav warning count into the envelope
  // (Task 2, PROBE-01 completion) -- present unconditionally (as an
  // integer, 0 for a clean file) so `inspect --json` always shows a
  // `diagnostics` object, per this plan's own acceptance criterion.
  fp.envelope.diagnostics["probe_warnings"] = session.warning_count();
  // D-01's resolved per-file PacketScan byte cap for THIS invocation --
  // present unconditionally (not only when Pass::packet_scan actually
  // ran) so a truncated run is explainable from its own report without
  // requiring the invocation to be remembered, and so `inspect --json`
  // always carries it regardless of which analyzers happen to be
  // registered yet.
  fp.envelope.diagnostics["probe_memory_cap_bytes"] = default_packet_scan_max_bytes();

  for (const AnalyzerSpec* spec : applicable) {
    spec->run(results, fp);
  }

  return fp;
}

}  // namespace detail

mediadiff::expected<Fingerprint, Error> fingerprint_input(const std::string& utf8_path, const CheckRegistry& registry,
                                                             const ProbeOptions& options) {
  auto snapshot_result = read_snapshot(utf8_path, registry);
  if (snapshot_result) {
    return snapshot_result;
  }
  if (snapshot_result.error().kind != ErrorKind::input_unsupported) {
    // input_open (the bytes never opened at all) propagates unchanged --
    // a missing file must not become a probe attempt.
    return mediadiff::unexpected(snapshot_result.error());
  }
  if (looks_like_json_document(utf8_path)) {
    // JSON-shaped but explicitly rejected by read_snapshot (a
    // schema_version major mismatch, an unregistered check ID, or
    // malformed content) -- that rejection is authoritative and must not
    // be silently reinterpreted as "try probing it as media instead".
    return mediadiff::unexpected(snapshot_result.error());
  }

  return detail::run_probe(utf8_path, all_analyzers(), nullptr, options);
}

mediadiff::expected<Fingerprint, Error> fingerprint_input(const std::string& utf8_path, const CheckRegistry& registry) {
  return fingerprint_input(utf8_path, registry, ProbeOptions{});
}

}  // namespace mediadiff
