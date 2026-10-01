// 07-08-PLAN.md (CONTENT-11, T-07-23..T-07-26): the lockstep rendezvous and
// fingerprint_pair. Every count below is derived from the fixture recipes in
// scripts/gen_corpus.sh, never read back from the code under test:
//   * video_hash_base.mp4 is 4 s at 25 fps: 100 decoded frames;
//   * video_frozen_base.mp4 is 6 s at 25 fps: 150 decoded frames, so against
//     video_hash_base.mp4 exactly 50 frames of the longer side are unpaired.
// "One frame in flight" is asserted against the slot's own occupancy counter
// (published minus released, sampled at every publish), and every early-exit
// path is followed by a completed, correct result -- a hang is a ctest timeout.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/lockstep.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/video_decode.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kPerceptual = "content.video.perceptual";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_lockstep";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

// The canonical snapshot bytes of `fp`, with content.video.perceptual removed
// (the one id a pair probe adds): the byte-identity yardstick for "every other
// measurement is what a one-sided probe gives".
std::string bytes_without_perceptual(const mediadiff::Fingerprint& fp, const std::string& name) {
  mediadiff::Fingerprint copy = fp;
  const std::uint32_t perceptual = *mediadiff::builtin_registry().find(kPerceptual);
  std::vector<mediadiff::Measurement> kept;
  for (mediadiff::Measurement& m : copy.measurements) {
    if (m.check_index != perceptual) {
      kept.push_back(std::move(m));
    }
  }
  copy.measurements = std::move(kept);
  const std::string path = scratch(name);
  auto written = mediadiff::write_snapshot(copy, path, mediadiff::builtin_registry());
  REQUIRE(written.has_value());
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const mediadiff::Measurement* find_perceptual(const mediadiff::Fingerprint& fp) {
  const std::uint32_t perceptual = *mediadiff::builtin_registry().find(kPerceptual);
  for (const mediadiff::Measurement& m : fp.measurements) {
    if (m.check_index == perceptual) {
      return &m;
    }
  }
  return nullptr;
}

const mediadiff::Measurement& perceptual_of(const mediadiff::Fingerprint& fp) {
  const mediadiff::Measurement* m = find_perceptual(fp);
  INFO("no content.video.perceptual measurement");
  REQUIRE(m != nullptr);
  return *m;
}

mediadiff::PairResult run_pair(const std::string& baseline, const std::string& candidate,
                               mediadiff::detail::PairProbeLog* log, const mediadiff::ProbeOptions& options = {}) {
  auto result = mediadiff::detail::run_pair_probe(fixture(baseline), fixture(candidate), mediadiff::all_analyzers(),
                                                  options, log);
  INFO("run_pair_probe failed: " << (result.has_value() ? std::string() : result.error().message));
  REQUIRE(result.has_value());
  return std::move(*result);
}

struct PacketScanCapGuard {
  std::int64_t saved = mediadiff::default_packet_scan_max_bytes();
  ~PacketScanCapGuard() { mediadiff::set_default_packet_scan_max_bytes(saved); }
};

mediadiff::TappedFrame frame_with_index(std::int64_t index) {
  mediadiff::TappedFrame frame;
  frame.decode_index = index;
  return frame;
}

}  // namespace

// --- Test 1 (CONTENT-11): one frame in flight --------------------------------

TEST_CASE("lockstep - one frame in flight", "[integration]") {
  mediadiff::detail::PairProbeLog log;
  const mediadiff::PairResult result = run_pair("video_hash_base.mp4", "video_hash_base.mp4", &log);

  // Every decoded frame of both 100-frame sides went through the rendezvous, and
  // the slot's own counter never saw more than one held at once.
  CHECK(log.baseline_frames_published == 100);
  CHECK(log.candidate_frames_published == 100);
  CHECK(log.baseline_max_occupancy == 1);
  CHECK(log.candidate_max_occupancy == 1);
  CHECK(log.pairs_paired == 100);
  CHECK(log.pairs_scored == 100);
  CHECK(log.unpaired_baseline == 0);
  CHECK(log.unpaired_candidate == 0);
  CHECK_FALSE(log.stopped_early);

  // Both fingerprints carry the measurement the lockstep produced.
  CHECK(perceptual_of(result.baseline).skip_reason == mediadiff::SkipReason::none);
  CHECK(perceptual_of(result.candidate).skip_reason == mediadiff::SkipReason::none);
}

// --- Test 2 (TRUST-05, T-07-26): determinism ---------------------------------

TEST_CASE("lockstep - deterministic", "[integration]") {
  const std::vector<std::string> args = {"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"),
                                         "--json"};
  const CliResult first = run_cli(args);
  REQUIRE_FALSE(first.out.empty());
  REQUIRE(first.out.find(kPerceptual) != std::string::npos);
  // Twenty consecutive runs: whichever producer thread happens to finish first,
  // the bytes never change.
  for (int run = 1; run < 20; ++run) {
    const CliResult again = run_cli(args);
    INFO("run " << run);
    REQUIRE(again.out == first.out);
    REQUIRE(again.exit_code == first.exit_code);
  }
}

// --- Test 3 (T-07-23): one side ends first -----------------------------------

TEST_CASE("lockstep - early end", "[integration]") {
  // video_hash_base.mp4 (4 s, 100 frames) against video_frozen_base.mp4 (6 s,
  // 150 frames, a different size): the longer side's 50-frame tail is drained
  // and counted unpaired without a deadlock -- in either direction.
  mediadiff::detail::PairProbeLog log;
  const mediadiff::PairResult shorter_first = run_pair("video_hash_base.mp4", "video_frozen_base.mp4", &log);
  CHECK(log.pairs_paired == 100);
  CHECK(log.unpaired_baseline == 0);
  CHECK(log.unpaired_candidate == 50);
  CHECK(log.candidate_frames_published == 150);
  CHECK(log.baseline_max_occupancy == 1);
  CHECK(log.candidate_max_occupancy == 1);
  CHECK_FALSE(log.stopped_early);
  const json& evidence = perceptual_of(shorter_first.candidate).evidence;
  CHECK(evidence.at("unpaired_candidate") == 50);
  CHECK(evidence.at("unpaired_baseline") == 0);

  mediadiff::detail::PairProbeLog reversed;
  run_pair("video_frozen_base.mp4", "video_hash_base.mp4", &reversed);
  CHECK(reversed.pairs_paired == 100);
  CHECK(reversed.unpaired_baseline == 50);
  CHECK(reversed.unpaired_candidate == 0);
}

// --- Test 4 (T-07-23, T-07-24): a stopping consumer releases both producers --

TEST_CASE("lockstep - close releases producers", "[integration]") {
  mediadiff::detail::PairProbeLog log;
  log.stop_after_scored_pairs = 5;
  const mediadiff::PairResult result = run_pair("video_hash_base.mp4", "video_hash_alt.mp4", &log);

  CHECK(log.stopped_early);
  CHECK(log.stop_reason == "test_stop");
  CHECK(log.pairs_scored == 5);
  // After the close neither producer published again: at most the five scored
  // pairs plus nothing in flight.
  CHECK(log.baseline_frames_published <= 6);
  CHECK(log.candidate_frames_published <= 6);

  // Both producers ran to their own ends untapped: the fingerprints are whole,
  // and every measurement but the two-file one equals the one-sided probe's.
  auto baseline = mediadiff::fingerprint_input(fixture("video_hash_base.mp4"), mediadiff::builtin_registry());
  auto candidate = mediadiff::fingerprint_input(fixture("video_hash_alt.mp4"), mediadiff::builtin_registry());
  REQUIRE(baseline.has_value());
  REQUIRE(candidate.has_value());
  CHECK(bytes_without_perceptual(result.baseline, "stop_b.snap.json") ==
        bytes_without_perceptual(*baseline, "one_b.snap.json"));
  CHECK(bytes_without_perceptual(result.candidate, "stop_c.snap.json") ==
        bytes_without_perceptual(*candidate, "one_c.snap.json"));
}

// --- Test 5 (T-07-23, Pitfall 10): one side without video --------------------

TEST_CASE("lockstep - one side without video", "[integration]") {
  // A video file against an audio-only one: no hang, no partner, no measurement
  // on either fingerprint, and the video side's producer is never blocked by the
  // side that has nothing to give.
  mediadiff::detail::PairProbeLog log;
  const mediadiff::PairResult result = run_pair("video_hash_base.mp4", "audio_hash_base.mp4", &log);
  CHECK(find_perceptual(result.baseline) == nullptr);
  CHECK(find_perceptual(result.candidate) == nullptr);
  CHECK(log.stopped_early);
  CHECK(log.stop_reason == "no_partner");

  mediadiff::detail::PairProbeLog reversed;
  const mediadiff::PairResult flipped = run_pair("audio_hash_base.mp4", "video_hash_base.mp4", &reversed);
  CHECK(find_perceptual(flipped.baseline) == nullptr);
  CHECK(find_perceptual(flipped.candidate) == nullptr);

  // The same through the CLI: a clean, finding-free compare of the perceptual id.
  const CliResult cli = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("audio_hash_base.mp4"), "--json"});
  const json report = json::parse(cli.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  for (const auto& finding : report.at("findings")) {
    CHECK(finding.at("id") != kPerceptual);
  }
}

// --- Test 6 (D-01): a snapshot side takes the sequential path ----------------

TEST_CASE("lockstep - snapshot side", "[integration]") {
  const std::string snap = scratch("video_base.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap, "--force"});
  REQUIRE(taken.exit_code == 0);

  const mediadiff::ProbeOptions options;
  auto pair = mediadiff::fingerprint_pair(snap, fixture("video_hash_alt.mp4"), mediadiff::builtin_registry(), options);
  REQUIRE(pair.has_value());
  auto baseline = mediadiff::fingerprint_input(snap, mediadiff::builtin_registry(), options);
  auto candidate = mediadiff::fingerprint_input(fixture("video_hash_alt.mp4"), mediadiff::builtin_registry(), options);
  REQUIRE(baseline.has_value());
  REQUIRE(candidate.has_value());

  // Exactly what two fingerprint_input calls give, perceptual id included.
  auto full_bytes = [](const mediadiff::Fingerprint& fp, const std::string& name) {
    const std::string path = scratch(name);
    REQUIRE(mediadiff::write_snapshot(fp, path, mediadiff::builtin_registry()).has_value());
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  CHECK(full_bytes(pair->baseline, "sn_pb.snap.json") == full_bytes(*baseline, "sn_b.snap.json"));
  CHECK(full_bytes(pair->candidate, "sn_pc.snap.json") == full_bytes(*candidate, "sn_c.snap.json"));

  // The media side carries the honest one-sided skip.
  CHECK(perceptual_of(pair->candidate).skip_reason == mediadiff::SkipReason::requires_media);
  CHECK(perceptual_of(pair->baseline).skip_reason == mediadiff::SkipReason::requires_media);
}

// --- Test 6b: --no-content never starts a lockstep ---------------------------

TEST_CASE("lockstep - no content is sequential", "[integration]") {
  mediadiff::ProbeOptions options;
  options.content_enabled = false;
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"),
                                          mediadiff::builtin_registry(), options);
  REQUIRE(pair.has_value());
  CHECK(perceptual_of(pair->baseline).skip_reason == mediadiff::SkipReason::requires_decode);
  CHECK(perceptual_of(pair->candidate).skip_reason == mediadiff::SkipReason::requires_decode);
}

// --- Test 7 (T-07-23): errors keep their order and never leak a thread -------

TEST_CASE("lockstep - errors keep their order", "[integration]") {
  const mediadiff::ProbeOptions options;
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();

  // A missing baseline: the baseline's own error.
  auto missing_baseline = mediadiff::fingerprint_pair(fixture("does_not_exist_a.mp4"), fixture("video_hash_base.mp4"),
                                                      registry, options);
  REQUIRE_FALSE(missing_baseline.has_value());
  CHECK(missing_baseline.error().kind == mediadiff::ErrorKind::input_open);
  CHECK(missing_baseline.error().message.find("does_not_exist_a.mp4") != std::string::npos);

  // A missing candidate: the candidate's error.
  auto missing_candidate = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("does_not_exist_b.mp4"),
                                                       registry, options);
  REQUIRE_FALSE(missing_candidate.has_value());
  CHECK(missing_candidate.error().kind == mediadiff::ErrorKind::input_open);
  CHECK(missing_candidate.error().message.find("does_not_exist_b.mp4") != std::string::npos);

  // Both missing: the baseline's.
  auto both = mediadiff::fingerprint_pair(fixture("does_not_exist_a.mp4"), fixture("does_not_exist_b.mp4"), registry,
                                          options);
  REQUIRE_FALSE(both.has_value());
  CHECK(both.error().message.find("does_not_exist_a.mp4") != std::string::npos);

  // A probe that fails inside a producer thread (a file that opens but is not
  // media): the error surfaces, the other side's producer is released and
  // joined, and the call returns.
  const std::string not_media = mediadiff::test::fixture_dir() + "/probe/not_media.txt";
  auto bad_baseline = mediadiff::fingerprint_pair(not_media, fixture("video_hash_base.mp4"), registry, options);
  REQUIRE_FALSE(bad_baseline.has_value());
  auto bad_candidate = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), not_media, registry, options);
  REQUIRE_FALSE(bad_candidate.has_value());
  auto bad_direct = mediadiff::detail::run_pair_probe(not_media, fixture("video_hash_base.mp4"),
                                                      mediadiff::all_analyzers(), options, nullptr);
  REQUIRE_FALSE(bad_direct.has_value());
}

// --- Test 8 (T-07-23): a truncated decode is never scored --------------------

TEST_CASE("lockstep - a truncated side skips instead of scoring", "[integration]") {
  PacketScanCapGuard guard;
  mediadiff::set_default_packet_scan_max_bytes(1LL << 30);
  std::int64_t unconstrained_total = 0;
  {
    auto session = mediadiff::DemuxSession::open(fixture("video_hash_base.mp4"), mediadiff::DemuxOptions{});
    REQUIRE(session.has_value());
    mediadiff::PacketScanRequest request;
    request.parse_access_units = true;
    request.decode_video = true;
    auto scan = mediadiff::run_packet_scan(*session, request);
    REQUIRE(scan.has_value());
    REQUIRE_FALSE(scan->packets.partial);
    unconstrained_total = scan->packets.accounted_bytes;
  }
  // One frame record short of the full sweep: the last frame is decoded and
  // dropped, never hashed -- a decode truncated by the record budget.
  mediadiff::set_default_packet_scan_max_bytes(unconstrained_total - mediadiff::kVideoFrameRecordBytes);

  mediadiff::detail::PairProbeLog log;
  const mediadiff::PairResult result = run_pair("video_hash_base.mp4", "video_hash_base.mp4", &log);
  const mediadiff::Measurement& baseline = perceptual_of(result.baseline);
  const mediadiff::Measurement& candidate = perceptual_of(result.candidate);
  CHECK(baseline.skip_reason == mediadiff::SkipReason::partial_scan);
  CHECK(candidate.skip_reason == mediadiff::SkipReason::partial_scan);
  CHECK(baseline.evidence.at("reason") == "frame_record_budget_exhausted");
  CHECK(log.baseline_max_occupancy == 1);
}

// --- Test 9: the rendezvous itself -------------------------------------------

TEST_CASE("lockstep - slot hands frames over one at a time", "[integration]") {
  mediadiff::FrameSlot slot;
  std::atomic<int> published{0};
  std::thread producer([&slot, &published] {
    for (int i = 0; i < 50; ++i) {
      if (!slot.publish(frame_with_index(i))) {
        return;
      }
      ++published;
    }
    slot.finish(mediadiff::TapEnd{});
  });

  std::int64_t expected_index = 0;
  mediadiff::TappedFrame frame;
  while (slot.take(&frame)) {
    // Frames arrive in order, and the producer is parked inside publish(): it
    // has completed exactly the publishes whose frames were already released.
    CHECK(frame.decode_index == expected_index);
    CHECK(published.load() == expected_index);
    ++expected_index;
    slot.release();
  }
  producer.join();
  CHECK(expected_index == 50);
  CHECK(slot.max_occupancy() == 1);
  CHECK(slot.published() == 50);
  CHECK(slot.finished());
}

TEST_CASE("lockstep - close wakes a blocked publish and refuses later ones", "[integration]") {
  mediadiff::FrameSlot slot;
  std::atomic<bool> first_result{true};
  std::atomic<bool> second_result{true};
  std::thread producer([&slot, &first_result, &second_result] {
    first_result = slot.publish(frame_with_index(0));
    second_result = slot.publish(frame_with_index(1));
  });

  mediadiff::TappedFrame frame;
  REQUIRE(slot.take(&frame));
  CHECK(frame.decode_index == 0);
  // The consumer stops with the frame still held: close frees the producer.
  slot.close();
  producer.join();
  CHECK_FALSE(first_result.load());
  CHECK_FALSE(second_result.load());
  // A closed slot has nothing left to take.
  CHECK_FALSE(slot.take(&frame));
}

TEST_CASE("lockstep - take does not return before a frame or the end", "[integration]") {
  mediadiff::FrameSlot slot;
  std::atomic<bool> returned{false};
  std::atomic<bool> got_frame{false};
  std::thread consumer([&slot, &returned, &got_frame] {
    mediadiff::TappedFrame frame;
    got_frame = slot.take(&frame);
    returned = true;
  });
  // Nothing published and nothing finished: take() must keep waiting, however
  // long (a wakeup that advanced without a frame would return here).
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(returned.load());
  slot.finish(mediadiff::TapEnd{});
  consumer.join();
  CHECK(returned.load());
  CHECK_FALSE(got_frame.load());
  // finish() is idempotent: the first report wins.
  mediadiff::TapEnd second;
  second.has_primary = true;
  slot.finish(second);
  CHECK_FALSE(slot.end().has_primary);
}
