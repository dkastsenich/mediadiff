#include <catch2/catch_test_macros.hpp>

#include <string>

extern "C" {
#include <libavutil/log.h>
}

#include "core/error.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "support/fixture_paths.h"

using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::ProbeDiagnostics;

namespace {

std::string tracer_mp4() { return mediadiff::test::fixture_dir() + "/tracer_a.mp4"; }
std::string probe_not_media() { return mediadiff::test::fixture_dir() + "/probe/not_media.txt"; }

void emit_synthetic_warning() { av_log(nullptr, AV_LOG_WARNING, "%s", "synthetic warning for test\n"); }

}  // namespace

// --- Task 1: DemuxSession::open, format_name()/stream_count() ---------

TEST_CASE("demux_session - opens a synthesized MP4 and reports format_name/stream_count", "[unit]") {
  auto session = DemuxSession::open(tracer_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());
  REQUIRE(session->format_name() == "mov");
  REQUIRE(session->stream_count() == 2);
}

TEST_CASE("demux_session - a nonexistent path is ErrorKind::input_open, never throws", "[unit]") {
  auto session = DemuxSession::open("/definitely/does/not/exist/tracer.mp4", DemuxOptions{});
  REQUIRE_FALSE(session.has_value());
  REQUIRE(session.error().kind == ErrorKind::input_open);
}

TEST_CASE("demux_session - a text file is ErrorKind::input_unsupported, never throws", "[unit]") {
  auto session = DemuxSession::open(probe_not_media(), DemuxOptions{});
  REQUIRE_FALSE(session.has_value());
  REQUIRE(session.error().kind == ErrorKind::input_unsupported);
}

// fingerprint_input on a valid *.snap.json returns the same Fingerprint
// read_snapshot would, without ever constructing a DemuxSession -- proven
// by the two results agreeing exactly for a fixture no DemuxSession could
// even open (a JSON document is not a media file).
TEST_CASE("orchestrator - fingerprint_input on a valid snapshot never probes", "[unit]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const std::string snap_path = mediadiff::test::snapshot_dir() + "/tracer_a.snap.json";

  auto direct = mediadiff::read_snapshot(snap_path, registry);
  auto via_orchestrator = mediadiff::fingerprint_input(snap_path, registry);

  REQUIRE(direct.has_value());
  REQUIRE(via_orchestrator.has_value());
  REQUIRE(via_orchestrator->envelope.schema_version == direct->envelope.schema_version);
  REQUIRE(via_orchestrator->envelope.tool_version == direct->envelope.tool_version);
  REQUIRE(via_orchestrator->measurements.size() == direct->measurements.size());
}

// --- Task 2: wall-clock budget, interrupt callback, log capture --------

TEST_CASE("demux_session - a 0ms wall-clock budget fails immediately, never hangs", "[unit]") {
  auto session = DemuxSession::open(tracer_mp4(), DemuxOptions{0});
  REQUIRE_FALSE(session.has_value());
  REQUIRE(session.error().kind == ErrorKind::input_unsupported);
  REQUIRE(session.error().message.find("budget") != std::string::npos);
}

TEST_CASE("demux_session - the default budget succeeds on a normal fixture", "[unit]") {
  auto session = DemuxSession::open(tracer_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());
}

// probe_log_callback's counting/attribution logic is exercised directly
// via a real av_log() call rather than relying on a specific fixture
// reliably triggering a real libav warning -- no deterministic
// warning-triggering recipe was identified within this plan's scope (see
// SUMMARY). This still proves the exact mechanism
// DemuxSession::open uses internally: install the callback, attach an
// accumulator via set_current_probe_diagnostics, emit a WARNING-level
// line, observe the count.
TEST_CASE("demux_session - probe_log_callback counts WARNING-and-above lines into the current accumulator",
          "[unit]") {
  av_log_set_callback(&mediadiff::probe_log_callback);

  ProbeDiagnostics diagnostics;
  mediadiff::set_current_probe_diagnostics(&diagnostics);
  emit_synthetic_warning();
  mediadiff::set_current_probe_diagnostics(nullptr);

  REQUIRE(diagnostics.warning_count == 1);

  av_log_set_callback(av_log_default_callback);
}

TEST_CASE("demux_session - two sequential accumulators on one thread never bleed into each other", "[unit]") {
  av_log_set_callback(&mediadiff::probe_log_callback);

  ProbeDiagnostics first;
  mediadiff::set_current_probe_diagnostics(&first);
  emit_synthetic_warning();
  mediadiff::set_current_probe_diagnostics(nullptr);

  ProbeDiagnostics second;
  mediadiff::set_current_probe_diagnostics(&second);
  mediadiff::set_current_probe_diagnostics(nullptr);

  REQUIRE(first.warning_count == 1);
  REQUIRE(second.warning_count == 0);

  av_log_set_callback(av_log_default_callback);
}

// A real DemuxSession::open on a clean, bitexact-generated fixture reports
// zero warnings -- its own accumulator starts fresh every call (no bleed
// from whatever an earlier test in this same process may have left behind
// via the tests above, since each open() attaches a brand-new
// ProbeDiagnostics).
TEST_CASE("demux_session - a clean fixture's own warning_count is zero", "[unit]") {
  auto session = DemuxSession::open(tracer_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());
  REQUIRE(session->warning_count() == 0);
}
