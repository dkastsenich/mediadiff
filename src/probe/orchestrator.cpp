#include "probe/orchestrator.h"

#include <cctype>
#include <cstdio>
#include <utility>
#include <vector>

#include "analyzers/container/analyzers.h"
#include "core/snapshot.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
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

  ProbeResults results;
  mediadiff::expected<void, Error> packet_scan_error;
  union_passes.for_each([&](Pass pass) {
    if (pass == Pass::demux_header) {
      results.demux = &session;
    } else if (pass == Pass::packet_scan) {
      // PROBE-02/PROBE-10 (03-03-PLAN.md Task 1): one av_read_frame sweep,
      // stored once in ProbeResults and handed to every applicable
      // analyzer as a const reference -- PassSet's own "each pass runs
      // exactly once" guarantee (PROBE-08) is what makes this a SINGLE
      // sweep even when more than one analyzer declared Pass::packet_scan.
      // PacketScanLimits{} default-constructs its own max_bytes from
      // default_packet_scan_max_bytes() -- the per-file cap this
      // invocation's own command entry point already resolved and set
      // (D-01) -- so this call site never has to know that value itself.
      auto scan_result = run_packet_scan(session, PacketScanLimits{});
      if (scan_result) {
        results.packet_scan = std::move(*scan_result);
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
