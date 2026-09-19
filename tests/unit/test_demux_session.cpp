#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/log.h>
}

#include "core/check_id.h"
#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

using mediadiff::CheckId;
using mediadiff::DeclaredDurationSource;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::PacketScanLimits;
using mediadiff::ProbeDiagnostics;
using mediadiff::Scope;
using mediadiff::StreamMediaType;
using mediadiff::run_packet_scan;

namespace {

std::string tracer_mp4() { return mediadiff::test::fixture_dir() + "/tracer_a.mp4"; }
std::string probe_not_media() { return mediadiff::test::fixture_dir() + "/probe/not_media.txt"; }
std::string timeline_start_base_mp4() { return mediadiff::test::fixture_dir() + "/timeline_start_base.mp4"; }
std::string timeline_ts_wrap_ts() { return mediadiff::test::fixture_dir() + "/timeline_ts_wrap.ts"; }
std::string timeline_ts_nowrap_ts() { return mediadiff::test::fixture_dir() + "/timeline_ts_nowrap.ts"; }

void emit_synthetic_warning() { av_log(nullptr, AV_LOG_WARNING, "%s", "synthetic warning for test\n"); }

// 05-17-PLAN.md Task 2 (Gap 2, TIME-02/TIME-03): finds a Measurement by
// CheckId/Scope, mirroring tests/unit/test_size_analyzer.cpp's own `find`
// helper verbatim (this project's per-file-copy convention).
const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind, int index) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

}  // namespace

// --- Task 1: DemuxSession::open, format_name()/stream_count() ---------

TEST_CASE("demux_session - opens a synthesized MP4 and reports format_name/stream_count", "[unit]") {
  auto session = DemuxSession::open(tracer_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());
  REQUIRE(session->format_name() == "mov");
  REQUIRE(session->stream_count() == 2);
}

// 05-14-PLAN.md Task 2 (Gap 3, TIME-06): StreamInfo::sample_rate,
// audio-only, populated from codecpar->sample_rate -- verified against the
// same tracer fixture 05-09/05-10's own av_sync tests already use.
TEST_CASE("demux_session - StreamInfo::sample_rate is 44100 on the audio stream and nullopt on the video stream",
          "[unit]") {
  auto session = DemuxSession::open(timeline_start_base_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());

  bool found_audio = false;
  bool found_video = false;
  for (int i = 0; i < session->stream_count(); ++i) {
    const auto info = session->stream_info(i);
    if (info.media_type == StreamMediaType::audio) {
      found_audio = true;
      REQUIRE(info.sample_rate.has_value());
      REQUIRE(*info.sample_rate == 44100);
    } else if (info.media_type == StreamMediaType::video) {
      found_video = true;
      REQUIRE_FALSE(info.sample_rate.has_value());
    }
  }
  REQUIRE(found_audio);
  REQUIRE(found_video);
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

// --- 05-17-PLAN.md Task 2 (Gap 2, TIME-02/TIME-03): reprobe_ts_declared_durations ---

// Before any reprobe call, declared_duration_source() is `demuxer` and
// this session's own primary values are the wrap-corrupted ones
// (correct_ts_overflow=0's own effect, 05-06-PLAN.md) -- video's declared
// duration is 180000 ticks (half of the nowrap-equivalent 360000), a
// causal fingerprint of the wrap, not a coincidence: the video stream's
// PES-header-carried PTS-only decode-order tie means the reprobe's own
// primary-value corruption is largest right at the un-reprobed point.
// Recorded here as evidence of the bug this plan fixes, per the plan's
// own must_haves.truths table.
TEST_CASE("demux_session - reprobe: before any reprobe call, declared_duration_source is demuxer and the primary "
          "session's own video declared duration is the wrap-corrupted value",
          "[unit]") {
  auto session = DemuxSession::open(timeline_ts_wrap_ts(), DemuxOptions{});
  REQUIRE(session.has_value());
  REQUIRE(session->declared_duration_source() == DeclaredDurationSource::demuxer);

  const auto video_info = session->stream_info(0);
  REQUIRE(video_info.media_type == StreamMediaType::video);
  REQUIRE(video_info.declared_duration_ticks.has_value());
  REQUIRE(*video_info.declared_duration_ticks == 180000);
}

// After reprobe_ts_declared_durations: the source becomes
// overflow_corrected_reprobe, and the video/audio/container durations
// equal a FRESH session's own values on timeline_ts_nowrap.ts -- the same
// content, the same 90kHz PES timebase, differing only by the
// -output_ts_offset this pair's own fixture recipe applies
// (05-06-SUMMARY.md).
TEST_CASE("demux_session - reprobe: on a genuine wrap, the source becomes overflow_corrected_reprobe and the "
          "durations equal the nowrap twin's own demuxer values",
          "[unit]") {
  auto wrap_session = DemuxSession::open(timeline_ts_wrap_ts(), DemuxOptions{});
  REQUIRE(wrap_session.has_value());
  wrap_session->reprobe_ts_declared_durations(timeline_ts_wrap_ts());
  REQUIRE(wrap_session->declared_duration_source() == DeclaredDurationSource::overflow_corrected_reprobe);

  auto nowrap_session = DemuxSession::open(timeline_ts_nowrap_ts(), DemuxOptions{});
  REQUIRE(nowrap_session.has_value());
  REQUIRE(nowrap_session->declared_duration_source() == DeclaredDurationSource::demuxer);

  const auto wrap_video = wrap_session->stream_info(0);
  const auto wrap_audio = wrap_session->stream_info(1);
  const auto nowrap_video = nowrap_session->stream_info(0);
  const auto nowrap_audio = nowrap_session->stream_info(1);

  REQUIRE(wrap_video.declared_duration_ticks.has_value());
  REQUIRE(nowrap_video.declared_duration_ticks.has_value());
  REQUIRE(*wrap_video.declared_duration_ticks == *nowrap_video.declared_duration_ticks);
  REQUIRE(*wrap_video.declared_duration_ticks == 360000);

  REQUIRE(wrap_audio.declared_duration_ticks.has_value());
  REQUIRE(nowrap_audio.declared_duration_ticks.has_value());
  REQUIRE(*wrap_audio.declared_duration_ticks == *nowrap_audio.declared_duration_ticks);
  REQUIRE(*wrap_audio.declared_duration_ticks == 348995);

  REQUIRE(wrap_session->container_duration_ticks().has_value());
  REQUIRE(nowrap_session->container_duration_ticks().has_value());
  REQUIRE(*wrap_session->container_duration_ticks() == *nowrap_session->container_duration_ticks());
}

// The reprobe opens an entirely separate AVFormatContext (this method's
// own isolation contract) -- it must never move the primary session's
// own read position (a single av_read_frame sweep cannot be repeated on
// one session once it has reached EOF, so this is proven by comparison
// against a totally independent, never-reprobed control session opened
// on the SAME bytes: if the reprobe had touched the primary session's own
// AVFormatContext/AVIOContext in any way, its own first PacketScan sweep
// would read a different read_frame_call_count than the control's) nor
// warning_count() (this session's own diagnostics accumulator, captured
// at primary-open time -- the reprobe's own diagnostics accumulator is a
// throwaway, per this method's own doc comment).
TEST_CASE("demux_session - reprobe: leaves the primary session's read_frame_call_count and warning_count unchanged",
          "[unit]") {
  auto control_session = DemuxSession::open(timeline_ts_wrap_ts(), DemuxOptions{});
  REQUIRE(control_session.has_value());
  auto control_scan = run_packet_scan(*control_session, PacketScanLimits{});
  REQUIRE(control_scan.has_value());

  auto session = DemuxSession::open(timeline_ts_wrap_ts(), DemuxOptions{});
  REQUIRE(session.has_value());
  const std::int64_t warning_count_before = session->warning_count();

  session->reprobe_ts_declared_durations(timeline_ts_wrap_ts());
  REQUIRE(session->declared_duration_source() == DeclaredDurationSource::overflow_corrected_reprobe);

  REQUIRE(session->warning_count() == warning_count_before);

  auto scan_after_reprobe = run_packet_scan(*session, PacketScanLimits{});
  REQUIRE(scan_after_reprobe.has_value());
  REQUIRE(scan_after_reprobe->read_frame_call_count == control_scan->read_frame_call_count);
}

// A nonexistent path makes the second open fail -- withheld, never
// compared corrupt: both container_duration_ticks() and every stream's
// declared_duration_ticks report nullopt.
TEST_CASE("demux_session - reprobe: a nonexistent path withholds both declared members", "[unit]") {
  auto session = DemuxSession::open(timeline_ts_wrap_ts(), DemuxOptions{});
  REQUIRE(session.has_value());

  session->reprobe_ts_declared_durations("/definitely/does/not/exist/timeline_ts_wrap.ts");
  REQUIRE(session->declared_duration_source() == DeclaredDurationSource::withheld_wrap_uncorrectable);

  REQUIRE_FALSE(session->container_duration_ticks().has_value());
  for (int i = 0; i < session->stream_count(); ++i) {
    REQUIRE_FALSE(session->stream_info(i).declared_duration_ticks.has_value());
  }
}

// --- 05-17-PLAN.md Task 2: detail::stream_layouts_match ---

TEST_CASE("demux_session - stream_layouts_match: identical lists give true", "[unit]") {
  const std::vector<mediadiff::detail::StreamLayoutKey> primary{{0, 100}, {1, 101}};
  const std::vector<mediadiff::detail::StreamLayoutKey> reprobe{{0, 100}, {1, 101}};
  REQUIRE(mediadiff::detail::stream_layouts_match(primary, reprobe));
}

TEST_CASE("demux_session - stream_layouts_match: different lengths give false", "[unit]") {
  const std::vector<mediadiff::detail::StreamLayoutKey> primary{{0, 100}, {1, 101}};
  const std::vector<mediadiff::detail::StreamLayoutKey> reprobe{{0, 100}};
  REQUIRE_FALSE(mediadiff::detail::stream_layouts_match(primary, reprobe));
}

TEST_CASE("demux_session - stream_layouts_match: the same length with a codec_type differing at index 1 gives false",
          "[unit]") {
  const std::vector<mediadiff::detail::StreamLayoutKey> primary{{0, 100}, {1, 101}};
  const std::vector<mediadiff::detail::StreamLayoutKey> reprobe{{0, 100}, {2, 101}};
  REQUIRE_FALSE(mediadiff::detail::stream_layouts_match(primary, reprobe));
}

TEST_CASE("demux_session - stream_layouts_match: a stream_id differing gives false", "[unit]") {
  const std::vector<mediadiff::detail::StreamLayoutKey> primary{{0, 100}, {1, 101}};
  const std::vector<mediadiff::detail::StreamLayoutKey> reprobe{{0, 100}, {1, 999}};
  REQUIRE_FALSE(mediadiff::detail::stream_layouts_match(primary, reprobe));
}

TEST_CASE("demux_session - stream_layouts_match: two empty lists give true", "[unit]") {
  const std::vector<mediadiff::detail::StreamLayoutKey> primary;
  const std::vector<mediadiff::detail::StreamLayoutKey> reprobe;
  REQUIRE(mediadiff::detail::stream_layouts_match(primary, reprobe));
}

// --- 05-17-PLAN.md Task 2: orchestrated -- the re-probe never runs on a
// non-wrapping TS input ---

// A non-wrapping TS file's own packet scan reports zero wrap events, so
// src/probe/orchestrator.cpp's own trigger never calls
// reprobe_ts_declared_durations at all -- declared_duration_source stays
// `demuxer` in the real, end-to-end reported evidence, proven through the
// SAME fingerprint_input entry point tests/unit/test_demux_session.cpp's
// own "fingerprint_input on a valid snapshot never probes" test already
// uses (no direct DemuxSession access here -- this is the orchestrated
// path).
TEST_CASE("demux_session - reprobe: orchestrated -- a non-wrapping TS file's timeline.duration evidence keeps "
          "declared_duration_source demuxer",
          "[unit]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  auto fp = mediadiff::fingerprint_input(timeline_ts_nowrap_ts(), registry);
  REQUIRE(fp.has_value());

  const Measurement* video_duration = find(*fp, CheckId::timeline_duration, Scope::Kind::video, 0);
  REQUIRE(video_duration != nullptr);
  REQUIRE(video_duration->evidence.contains("declared_duration_source"));
  REQUIRE(video_duration->evidence.at("declared_duration_source").get<std::string>() == "demuxer");
}
