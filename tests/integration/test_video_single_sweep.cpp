// 07-08-PLAN.md (CONTENT-07, PROBE-08): one decode sweep per side. Hashing,
// thumbnailing, the frozen and black detectors, caption and HDR capture and the
// lockstep tap all run inside the SAME av_read_frame loop: the read count equals
// a packet-scan-only run of the file, and every one-sided measurement is the
// same with and without the tap, with a tap that stalls, and with one that
// closes mid-stream.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/lockstep.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/video_decode.h"
#include "probe/video_thumbnail.h"
#include "support/fixture_paths.h"

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kPerceptual = "content.video.perceptual";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_video_single_sweep";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

std::string snapshot_bytes(const mediadiff::Fingerprint& fp, const std::string& name, bool drop_perceptual) {
  mediadiff::Fingerprint copy = fp;
  if (drop_perceptual) {
    const std::uint32_t perceptual = *mediadiff::builtin_registry().find(kPerceptual);
    std::vector<mediadiff::Measurement> kept;
    for (mediadiff::Measurement& m : copy.measurements) {
      if (m.check_index != perceptual) {
        kept.push_back(std::move(m));
      }
    }
    copy.measurements = std::move(kept);
  }
  const std::string path = scratch(name);
  auto written = mediadiff::write_snapshot(copy, path, mediadiff::builtin_registry());
  REQUIRE(written.has_value());
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// `av_read_frame` calls of a packet-scan-only sweep of `name`: no decode of any
// kind, the control the fused sweeps are held to.
std::int64_t packet_scan_only_reads(const std::string& name) {
  auto session = mediadiff::DemuxSession::open(fixture(name), mediadiff::DemuxOptions{});
  REQUIRE(session.has_value());
  mediadiff::PacketScanRequest request;
  auto scan = mediadiff::run_packet_scan(*session, request);
  REQUIRE(scan.has_value());
  return scan->packets.read_frame_call_count;
}

// The read count of a full one-sided probe (every sink on) through run_probe.
std::int64_t probe_reads(const std::string& name, const mediadiff::ProbeOptions& options) {
  mediadiff::detail::ProbeScanStats stats;
  auto fp = mediadiff::detail::run_probe(fixture(name), mediadiff::all_analyzers(), nullptr, options, &stats);
  REQUIRE(fp.has_value());
  return stats.read_frame_call_count;
}

mediadiff::Fingerprint one_sided(const std::string& name) {
  auto fp = mediadiff::fingerprint_input(fixture(name), mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

// A tap that is not a rendezvous: it records what it sees and answers at once
// (or after a pause, or by refusing from some frame on). It checks, at publish
// time, that the frame's thumbnail is the thumbnail of THIS frame -- proof the
// thumbnail sink had already run when the tap fired.
class SpyTap final : public mediadiff::FrameTap {
 public:
  int pause_ms = 0;
  int refuse_after = -1;  // publish returns false once this many frames were accepted

  bool publish(const mediadiff::TappedFrame& frame) override {
    ++seen;
    if (frame.thumbnail == nullptr || frame.frame == nullptr) {
      ++missing_thumbnail;
    } else {
      mediadiff::ThumbnailScaler scaler;
      mediadiff::Thumbnail independent;
      if (!scaler.scale(*frame.frame, &independent) || independent.pixels != frame.thumbnail->pixels) {
        ++wrong_thumbnail;
      }
    }
    if (frame.decode_index != seen - 1) {
      ++out_of_order;
    }
    if (pause_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
    }
    if (refuse_after >= 0 && accepted >= refuse_after) {
      return false;
    }
    ++accepted;
    return true;
  }

  void finish(const mediadiff::TapEnd& report) override {
    ++finishes;
    end = report;
  }

  int seen = 0;
  int accepted = 0;
  int missing_thumbnail = 0;
  int wrong_thumbnail = 0;
  int out_of_order = 0;
  int finishes = 0;
  mediadiff::TapEnd end{};
};

}  // namespace

// --- Test 7: one read loop per side ------------------------------------------

TEST_CASE("single_sweep - one read loop per side", "[integration]") {
  mediadiff::detail::PairProbeLog log;
  auto pair = mediadiff::detail::run_pair_probe(fixture("video_hash_base.mp4"), fixture("video_frozen_base.mp4"),
                                                mediadiff::all_analyzers(), mediadiff::ProbeOptions{}, &log);
  REQUIRE(pair.has_value());

  // 100 and 150 packets plus the terminating AVERROR_EOF call, every sink and
  // the tap enabled -- exactly a packet-scan-only sweep of the same file.
  mediadiff::ProbeOptions no_content;
  no_content.content_enabled = false;
  CHECK(log.baseline_read_frame_calls == packet_scan_only_reads("video_hash_base.mp4"));
  CHECK(log.candidate_read_frame_calls == packet_scan_only_reads("video_frozen_base.mp4"));
  CHECK(log.baseline_read_frame_calls == probe_reads("video_hash_base.mp4", no_content));
  CHECK(log.candidate_read_frame_calls == probe_reads("video_frozen_base.mp4", no_content));
  CHECK(log.baseline_read_frame_calls == 101);
  CHECK(log.candidate_read_frame_calls == 151);

  // A file with no video stream at all still runs exactly one sweep, and the
  // other side's producer is never held up by it.
  mediadiff::detail::PairProbeLog audio_log;
  auto audio_pair = mediadiff::detail::run_pair_probe(fixture("video_hash_base.mp4"), fixture("audio_hash_base.mp4"),
                                                      mediadiff::all_analyzers(), mediadiff::ProbeOptions{},
                                                      &audio_log);
  REQUIRE(audio_pair.has_value());
  CHECK(audio_log.baseline_read_frame_calls == 101);
  CHECK(audio_log.candidate_read_frame_calls == packet_scan_only_reads("audio_hash_base.mp4"));
}

// --- Test 8: the tap changes nothing one-sided -------------------------------

TEST_CASE("single_sweep - tap does not change one-sided values", "[integration]") {
  // Four pairs, between them exercising every sink the tap follows: the hash
  // and detectors on testsrc2 video, the A53 caption sink (MPEG-2 GA94), the
  // first-frame HDR sink (an H.264 I_PCM stream with mastering-display SEI), and
  // raw elementary streams with no timestamps at all.
  const std::vector<std::pair<std::string, std::string>> pairs = {
      {"video_hash_base.mp4", "video_hash_alt.mp4"},
      {"video_frozen.mp4", "video_frozen_bf0.mp4"},
      {"video_cc_base.m2v", "video_cc_a53.m2v"},
      {"video_pcm_plain.h264", "video_pcm_hdr.h264"},
  };
  int index = 0;
  for (const auto& [baseline_name, candidate_name] : pairs) {
    INFO(baseline_name << " vs " << candidate_name);
    auto pair = mediadiff::fingerprint_pair(fixture(baseline_name), fixture(candidate_name),
                                            mediadiff::builtin_registry(), mediadiff::ProbeOptions{});
    REQUIRE(pair.has_value());
    const mediadiff::Fingerprint baseline = one_sided(baseline_name);
    const mediadiff::Fingerprint candidate = one_sided(candidate_name);
    const std::string tag = std::to_string(index++);
    CHECK(snapshot_bytes(pair->baseline, "p_b" + tag + ".snap.json", true) ==
          snapshot_bytes(baseline, "o_b" + tag + ".snap.json", true));
    CHECK(snapshot_bytes(pair->candidate, "p_c" + tag + ".snap.json", true) ==
          snapshot_bytes(candidate, "o_c" + tag + ".snap.json", true));
  }
}

// --- Test 8b: ordering, stalling and closing ----------------------------------

TEST_CASE("single_sweep - the tap fires after the thumbnail and changes nothing", "[integration]") {
  const mediadiff::Fingerprint untapped = one_sided("video_frozen.mp4");
  const std::string untapped_bytes = snapshot_bytes(untapped, "tap_none.snap.json", false);

  // An immediate tap sees every decoded frame in order, each with ITS thumbnail.
  {
    SpyTap spy;
    mediadiff::ProbeOptions options;
    options.frame_tap = &spy;
    auto fp = mediadiff::detail::run_probe(fixture("video_frozen.mp4"), mediadiff::all_analyzers(), nullptr, options);
    REQUIRE(fp.has_value());
    CHECK(spy.seen == 150);
    CHECK(spy.missing_thumbnail == 0);
    CHECK(spy.wrong_thumbnail == 0);
    CHECK(spy.out_of_order == 0);
    CHECK(spy.finishes == 1);
    CHECK(spy.end.has_primary);
    CHECK(spy.end.attempted);
    CHECK(spy.end.complete);
    CHECK(spy.end.frames_published == 150);
    CHECK(spy.end.stream_index == 0);
    CHECK(spy.end.video_scope_index == 0);
    // The recorded path: the scaler identity and the decoder flags.
    CHECK(spy.end.scaler_record.find("dst=128x104") != std::string::npos);
    CHECK(spy.end.flags_recorded == std::string(mediadiff::kVideoDecoderFlagsRecorded));
    // One-sided value identity with the tapped run: the tap only ever waits.
    CHECK(snapshot_bytes(*fp, "tap_imm.snap.json", false) == untapped_bytes);
  }

  // A tap that stalls on every frame changes no value either.
  {
    SpyTap spy;
    spy.pause_ms = 1;
    mediadiff::ProbeOptions options;
    options.frame_tap = &spy;
    auto fp = mediadiff::detail::run_probe(fixture("video_frozen.mp4"), mediadiff::all_analyzers(), nullptr, options);
    REQUIRE(fp.has_value());
    CHECK(spy.seen == 150);
    CHECK(snapshot_bytes(*fp, "tap_slow.snap.json", false) == untapped_bytes);
  }

  // A tap that refuses after ten frames: the producer stops publishing, finishes
  // its own sweep untapped, and the result is again identical.
  {
    SpyTap spy;
    spy.refuse_after = 10;
    mediadiff::ProbeOptions options;
    options.frame_tap = &spy;
    auto fp = mediadiff::detail::run_probe(fixture("video_frozen.mp4"), mediadiff::all_analyzers(), nullptr, options);
    REQUIRE(fp.has_value());
    CHECK(spy.seen == 11);
    CHECK(spy.accepted == 10);
    CHECK(spy.finishes == 1);
    CHECK(spy.end.frames_published == 10);
    CHECK(snapshot_bytes(*fp, "tap_close.snap.json", false) == untapped_bytes);
  }
}

// --- Test 8c: attached pictures and the primary stream ------------------------

TEST_CASE("single_sweep - cover art is never the primary stream", "[integration]") {
  // video_cover.mp4 is the base video (stream 0) plus a one-frame attached
  // picture (stream 1): the tap binds to stream 0 and the score against the
  // plain base is exactly 1.
  SpyTap spy;
  mediadiff::ProbeOptions options;
  options.frame_tap = &spy;
  auto fp = mediadiff::detail::run_probe(fixture("video_cover.mp4"), mediadiff::all_analyzers(), nullptr, options);
  REQUIRE(fp.has_value());
  CHECK(spy.seen == 100);
  CHECK(spy.end.stream_index == 0);

  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_cover.mp4"),
                                          mediadiff::builtin_registry(), mediadiff::ProbeOptions{});
  REQUIRE(pair.has_value());
  const mediadiff::Measurement* found = nullptr;
  const std::uint32_t perceptual = *mediadiff::builtin_registry().find(kPerceptual);
  for (const mediadiff::Measurement& m : pair->candidate.measurements) {
    if (m.check_index == perceptual) {
      found = &m;
    }
  }
  REQUIRE(found != nullptr);
  const auto* value = std::get_if<mediadiff::RationalValue>(&found->value);
  REQUIRE(value != nullptr);
  CHECK(value->num == 1000000);
  CHECK(found->evidence.at("candidate_stream_index") == 0);
}
