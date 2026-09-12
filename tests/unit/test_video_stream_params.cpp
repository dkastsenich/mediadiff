// 04-06-PLAN.md (VIDEO-01, VIDEO-02): video.codec/profile/level/resolution/
// frame_count -- the five per-video-stream identity checks, exercised
// directly against mediadiff::video_stream_params_analyzer()'s own run()
// (the same AnalyzerSpec src/probe/orchestrator.cpp registers), fed a real
// DemuxSession + PacketScanResult (and, where the test needs the preferred
// access-unit-count source, a ParserScanResult too) so no CLI process
// spawn is needed -- mirrors tests/unit/test_size_analyzer.cpp's own
// established convention.
//
// detail::render_profile_value/render_level_value are exercised directly
// (analyzers.h's own exposed seam) for the two behaviors no single real
// fixture pair can prove empirically: two DIFFERENT unresolved profile
// integers comparing as different rather than collapsing to one shared
// "unknown" word (VIDEO-01-E2), and a level this project's own table does
// and does not cover.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analyzers/video/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::Absent;
using mediadiff::CheckId;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::PacketScanRequest;
using mediadiff::ProbeResults;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::detail::render_level_value;
using mediadiff::detail::render_profile_value;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

// Opens `path` and runs the fused packet+parser scan (parse_access_units =
// true) -- the same shape the real orchestrator produces once ANY
// applicable analyzer in the union declares Pass::parser_scan (which
// video_gop_analyzer(), scoped ContainerFamily::other, always does).
struct ScanBundle {
  DemuxSession session;
  mediadiff::PacketScanOutputs outputs;
};

ScanBundle scan_or_fail(const std::string& path) {
  DemuxSession session = open_or_fail(path);
  PacketScanRequest request;
  request.parse_access_units = true;
  auto result = mediadiff::run_packet_scan(session, request);
  REQUIRE(result.has_value());
  return ScanBundle{std::move(session), std::move(*result)};
}

Fingerprint run_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::video_stream_params_analyzer().run(results, fp);
  return fp;
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind = Scope::Kind::video, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

std::size_t count_video_measurements(const Fingerprint& fp, CheckId id) {
  const auto want = static_cast<std::uint32_t>(id);
  std::size_t n = 0;
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want) {
      ++n;
    }
  }
  return n;
}

}  // namespace

// --- Test 1/2/7: one Measurement per video stream, correct Value
// alternative per check, none on a file with no video stream -------------

TEST_CASE("video_stream_params - video_base.mp4 emits all five checks once each, with the right Value alternative",
          "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("video_base.mp4"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.outputs.packets;
  results.parser_scan = bundle.outputs.access_units;

  const Fingerprint fp = run_analyzer(results);

  const Measurement* codec = find(fp, CheckId::video_codec);
  REQUIRE(codec != nullptr);
  REQUIRE(codec->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(codec->value) == "mpeg4");

  const Measurement* profile = find(fp, CheckId::video_profile);
  REQUIRE(profile != nullptr);
  REQUIRE(std::holds_alternative<std::string>(profile->value));

  const Measurement* level = find(fp, CheckId::video_level);
  REQUIRE(level != nullptr);
  REQUIRE(std::holds_alternative<std::string>(level->value));
  // mpeg4 is not in video.level's own table -- falls through to the raw
  // integer's decimal spelling.
  REQUIRE(std::get<std::string>(level->value) == "1");

  const Measurement* resolution = find(fp, CheckId::video_resolution);
  REQUIRE(resolution != nullptr);
  REQUIRE(std::get<std::string>(resolution->value) == "320x240");

  const Measurement* frame_count = find(fp, CheckId::video_frame_count);
  REQUIRE(frame_count != nullptr);
  REQUIRE(std::holds_alternative<std::int64_t>(frame_count->value));
  REQUIRE(std::get<std::int64_t>(frame_count->value) == 100);

  for (CheckId id : {CheckId::video_codec, CheckId::video_profile, CheckId::video_level, CheckId::video_resolution,
                      CheckId::video_frame_count}) {
    REQUIRE(count_video_measurements(fp, id) == 1);
  }
}

TEST_CASE("video_stream_params - a file with no video stream emits none of the five checks", "[unit]") {
  // tracer_empty.mp4 (02-01-PLAN.md's own tracer control): a valid MP4
  // with no video stream at all.
  ScanBundle bundle = scan_or_fail(fixture("tracer_empty.mp4"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.outputs.packets;
  results.parser_scan = bundle.outputs.access_units;

  const Fingerprint fp = run_analyzer(results);

  for (CheckId id : {CheckId::video_codec, CheckId::video_profile, CheckId::video_level, CheckId::video_resolution,
                      CheckId::video_frame_count}) {
    REQUIRE(count_video_measurements(fp, id) == 0);
  }
}

// --- Test 3: the container's own declared frame count differs from the
// counted value on a real corpus fixture -----------------------------------

TEST_CASE(
    "video_stream_params - video_h264_closed.h264's container-declared frame count (0, a raw elementary "
    "stream never sets AVStream::nb_frames) differs from the counted value in evidence",
    "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("video_h264_closed.h264"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.outputs.packets;
  results.parser_scan = bundle.outputs.access_units;

  const Fingerprint fp = run_analyzer(results);
  const Measurement* frame_count = find(fp, CheckId::video_frame_count);
  REQUIRE(frame_count != nullptr);
  REQUIRE(frame_count->skip_reason == SkipReason::none);
  REQUIRE(frame_count->evidence.contains("declared_frame_count"));
  REQUIRE(frame_count->evidence.contains("agrees_with_declared"));
  const auto counted = std::get<std::int64_t>(frame_count->value);
  const auto declared = frame_count->evidence.at("declared_frame_count").get<std::int64_t>();
  REQUIRE(counted != declared);
  REQUIRE_FALSE(frame_count->evidence.at("agrees_with_declared").get<bool>());
}

// --- Test 5/8: video.frame_count is counted from the scan (asserted
// against the scan itself, never a hardcoded number), and skips
// partial_scan when the scan was truncated ---------------------------------

TEST_CASE("video_stream_params - video.frame_count equals the number of access units the parser scan recorded",
          "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("video_base.mp4"));
  REQUIRE(bundle.outputs.access_units.has_value());
  REQUIRE_FALSE(bundle.outputs.access_units->per_stream.empty());
  const auto expected =
      static_cast<std::int64_t>(bundle.outputs.access_units->per_stream[0].access_units.size());

  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.outputs.packets;
  results.parser_scan = bundle.outputs.access_units;

  const Fingerprint fp = run_analyzer(results);
  const Measurement* frame_count = find(fp, CheckId::video_frame_count);
  REQUIRE(frame_count != nullptr);
  REQUIRE(std::get<std::int64_t>(frame_count->value) == expected);
}

TEST_CASE("video_stream_params - video.frame_count skips partial_scan when the packet scan truncated, but the "
          "other four checks still report",
          "[unit]") {
  DemuxSession session = open_or_fail(fixture("video_base.mp4"));
  PacketScanRequest request;
  request.parse_access_units = true;
  request.limits.max_bytes = 5 * static_cast<std::int64_t>(sizeof(mediadiff::PacketRecord));
  auto scan_result = mediadiff::run_packet_scan(session, request);
  REQUIRE(scan_result.has_value());
  REQUIRE(scan_result->packets.partial);

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan_result->packets;
  results.parser_scan = scan_result->access_units;

  const Fingerprint fp = run_analyzer(results);
  const Measurement* frame_count = find(fp, CheckId::video_frame_count);
  REQUIRE(frame_count != nullptr);
  REQUIRE(frame_count->skip_reason == SkipReason::partial_scan);
  REQUIRE(std::holds_alternative<Absent>(frame_count->value));
  REQUIRE(frame_count->evidence.contains("probe_memory_cap_bytes"));

  // codec/profile/level/resolution are codecpar-only -- unaffected by
  // packet-scan truncation.
  const Measurement* codec = find(fp, CheckId::video_codec);
  REQUIRE(codec != nullptr);
  REQUIRE(codec->skip_reason == SkipReason::none);
}

// --- Test 4 (VIDEO-01-E2): detail::render_profile_value's own rule -- an
// unresolved profile renders its raw integer, and two DIFFERENT
// unresolved profile integers render as two DIFFERENT strings -------------

TEST_CASE("video_stream_params - render_profile_value renders the raw integer when unresolved, and two DIFFERENT "
          "unresolved integers render as different strings",
          "[unit]") {
  REQUIRE(render_profile_value(std::nullopt, -99) == "-99");
  REQUIRE(render_profile_value(std::nullopt, 7) == "7");
  REQUIRE(render_profile_value(std::nullopt, -99) != render_profile_value(std::nullopt, 7));
  REQUIRE(render_profile_value(std::optional<std::string>("Main"), 4) == "Main");
}

// --- Test 5 (video.level's own table): a covered codec/level renders the
// human string; an uncovered one renders the raw integer -------------------

TEST_CASE("video_stream_params - render_level_value renders the codec-specific human string for a covered level, "
          "and the raw integer for one it does not cover",
          "[unit]") {
  // H.264: level_idc / 10 . level_idc % 10.
  REQUIRE(render_level_value("h264", 31) == "3.1");
  // HEVC: general_level_idc / 30 . (general_level_idc % 30) / 3.
  REQUIRE(render_level_value("hevc", 123) == "4.1");
  // AV1: 2 + idx/4 . idx%4 (claude_docs/03-video-analysis.md section 2's
  // own worked example: 8 -> "4.0").
  REQUIRE(render_level_value("av1", 8) == "4.0");
  // mpeg2video is not in the table at all -- falls through to the raw
  // integer regardless of value.
  REQUIRE(render_level_value("mpeg2video", 8) == "8");
  // AV_LEVEL_UNKNOWN (-99) always renders as its own raw spelling.
  REQUIRE(render_level_value("h264", -99) == "-99");
}

// --- Test 6: video_prof_a.mp4 vs video_prof_b.mp4 -- both video.profile
// and video.level report a real, non-pass finding --------------------------

TEST_CASE("video_stream_params - video_prof_a.mp4 and video_prof_b.mp4 differ in both profile and level",
          "[unit]") {
  ScanBundle a = scan_or_fail(fixture("video_prof_a.mp4"));
  ProbeResults results_a;
  results_a.demux = &a.session;
  results_a.packet_scan = a.outputs.packets;
  results_a.parser_scan = a.outputs.access_units;
  const Fingerprint fp_a = run_analyzer(results_a);

  ScanBundle b = scan_or_fail(fixture("video_prof_b.mp4"));
  ProbeResults results_b;
  results_b.demux = &b.session;
  results_b.packet_scan = b.outputs.packets;
  results_b.parser_scan = b.outputs.access_units;
  const Fingerprint fp_b = run_analyzer(results_b);

  const Measurement* profile_a = find(fp_a, CheckId::video_profile);
  const Measurement* profile_b = find(fp_b, CheckId::video_profile);
  REQUIRE(profile_a != nullptr);
  REQUIRE(profile_b != nullptr);
  REQUIRE_FALSE(profile_a->value == profile_b->value);

  const Measurement* level_a = find(fp_a, CheckId::video_level);
  const Measurement* level_b = find(fp_b, CheckId::video_level);
  REQUIRE(level_a != nullptr);
  REQUIRE(level_b != nullptr);
  REQUIRE_FALSE(level_a->value == level_b->value);
}
