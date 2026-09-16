#include "probe/orchestrator.h"

#include <cctype>
#include <cstdio>
#include <utility>
#include <vector>

#include "analyzers/container/analyzers.h"
#include "analyzers/size/analyzers.h"
#include "analyzers/timeline/analyzers.h"
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
  };
  return registry;
}

namespace detail {

mediadiff::expected<Fingerprint, Error> run_probe(const std::string& utf8_path,
                                                     const std::vector<AnalyzerSpec>& analyzers,
                                                     PassExecutionLog* pass_log) {
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

  // PROBE-03 (04-01-PLAN.md Task 2): an analyzer that declared ONLY
  // Pass::parser_scan would otherwise get no sweep run at all -- the
  // parser's own data is produced INSIDE Pass::packet_scan's own arm
  // below (the fused loop in probe/packet_scan.cpp), so packet_scan must
  // always be in the union whenever parser_scan is.
  if (union_passes.test(Pass::parser_scan)) {
    union_passes.set(Pass::packet_scan);
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
      PacketScanRequest request;
      request.limits = PacketScanLimits{};
      request.parse_access_units = union_passes.test(Pass::parser_scan);
      auto scan_result = run_packet_scan(session, request);
      if (scan_result) {
        results.packet_scan = std::move(scan_result->packets);
        results.parser_scan = std::move(scan_result->access_units);
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

mediadiff::expected<Fingerprint, Error> fingerprint_input(const std::string& utf8_path, const CheckRegistry& registry) {
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

  return detail::run_probe(utf8_path, all_analyzers(), nullptr);
}

}  // namespace mediadiff
