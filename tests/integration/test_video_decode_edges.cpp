// 07-02-PLAN.md (CONTENT-01, PROBE-08, D-05/D-06/D-09, T-07-01/T-07-02): one
// fixture per edge of the video decode path that 07-01's tracer proved --
// D-06's edit-list frames, the EOF drain, cover art, a mid-stream resolution
// change and (Task 2) decode errors, the declared-dimension bound and the
// per-frame record budget.
//
// Every frame count below is a LITERAL measured with the pinned ffmpeg
// (`.ffmpeg-pinned/linux-x86_64/ffmpeg`, `-f framemd5`, which prints one line
// per decoded frame) or with ffprobe -- never a value read back from
// mediadiff itself, which would be a self-referential oracle proving nothing.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// Measured with the pinned ffmpeg on the generated corpus (`-f framemd5`):
//   video_hash_base.mp4   100 frames (4 s at 25 fps, `-bf 2`: a real reorder buffer)
//   video_trim.mp4         87 frames -- a default decode; libavcodec destroys the
//                          one frame the MP4 edit list flags AV_PKT_FLAG_DISCARD
//   video_trim.mkv         88 frames -- the same payload in Matroska, which keeps
//                          no edit list, so nothing is discarded
constexpr std::int64_t kBaseFramesFramemd5 = 100;
constexpr std::int64_t kTrimMp4FramesDefaultDiscard = 87;
constexpr std::int64_t kTrimMkvFramesFramemd5 = 88;
// `ffprobe -show_frames` on video_geom_change.m2v: 24 frames at 352x288 and 25
// at 320x240 (49 of the 50 packets: libavcodec itself drops the last frame of
// the first sequence at the size change; the bytes of the file are not at
// fault).
constexpr std::int64_t kGeomFramesFirstSegment = 24;
constexpr std::int64_t kGeomFramesSecondSegment = 25;

using FingerprintPtr = std::optional<mediadiff::Fingerprint>;

FingerprintPtr probe(const std::string& name) {
  auto fp = mediadiff::fingerprint_input(fixture(name), mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

const mediadiff::Measurement* find_measurement(const mediadiff::Fingerprint& fp, const std::string& id,
                                                mediadiff::Scope::Kind kind, int index) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  for (const mediadiff::Measurement& m : fp.measurements) {
    if (registry.at(m.check_index).id == id && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

// One conditional assertion and one reachable return -- never a statement after
// an unconditional Catch2 failure call, which MSVC /W4 /WX turns into C4702 on
// the blocking Windows leg (scripts/lint_dead_code_after_fail.sh; the shape is
// block_for's, in tests/unit/test_report_model.cpp).
const mediadiff::HashChain& video_chain(const mediadiff::Fingerprint& fp, int rank = 0) {
  const mediadiff::Measurement* m = find_measurement(fp, "content.video.frame_hash", mediadiff::Scope::Kind::video, rank);
  INFO("no content.video.frame_hash measurement at video[" << rank << "]");
  REQUIRE(m != nullptr);
  const auto* chain = std::get_if<mediadiff::HashChain>(&m->value);
  REQUIRE(chain != nullptr);
  return *chain;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

nlohmann::ordered_json compare_json(const std::vector<std::string>& args, int* exit_code = nullptr) {
  CliResult result = run_cli(args);
  if (exit_code != nullptr) {
    *exit_code = result.exit_code;
  }
  return nlohmann::ordered_json::parse(result.out, nullptr, false);
}

}  // namespace

// --- D-06: an edit-list trim keeps its discarded frame ----------------------

TEST_CASE("video_decode_edges - edit list trim equals remux", "[integration]") {
  auto trim_mp4 = probe("video_trim.mp4");
  auto trim_mkv = probe("video_trim.mkv");
  const mediadiff::HashChain& mp4_chain = video_chain(*trim_mp4);
  const mediadiff::HashChain& mkv_chain = video_chain(*trim_mkv);

  CHECK(mp4_chain.digest == mkv_chain.digest);
  CHECK(mp4_chain.element_count == mkv_chain.element_count);
  CHECK(mp4_chain.block_digests == mkv_chain.block_digests);

  // The MP4 side's first recovered frame is the one the edit list trimmed: it
  // presents BEFORE time zero, and the tick array stores that as-is.
  REQUIRE_FALSE(mp4_chain.element_ticks.empty());
  CHECK(mp4_chain.element_ticks.front() < 0);

  int exit_code = -1;
  const nlohmann::ordered_json report =
      compare_json({"compare", fixture("video_trim.mp4"), fixture("video_trim.mkv"), "--json"}, &exit_code);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
}

TEST_CASE("video_decode_edges - discard kept", "[integration]") {
  auto trim_mp4 = probe("video_trim.mp4");
  auto trim_mkv = probe("video_trim.mkv");
  // A default decode of the trimmed MP4 yields kTrimMp4FramesDefaultDiscard
  // frames; keeping the discarded leading frame (D-06) makes it exactly one
  // more, which is also what the Matroska stream copy decodes to.
  CHECK(video_chain(*trim_mp4).element_count == kTrimMp4FramesDefaultDiscard + 1);
  CHECK(video_chain(*trim_mp4).element_count == kTrimMkvFramesFramemd5);
  CHECK(video_chain(*trim_mkv).element_count == kTrimMkvFramesFramemd5);
}

// --- Pitfall 1: the EOF drain loses nothing ---------------------------------

TEST_CASE("video_decode_edges - drain", "[integration]") {
  // A `-bf 2` stream: the decoder holds frames in its reorder buffer until the
  // flush packet, so a missing drain would hash fewer than the 100 frames
  // `ffmpeg -f framemd5` reports for the same file.
  auto base = probe("video_hash_base.mp4");
  const mediadiff::HashChain& chain = video_chain(*base);
  CHECK(chain.element_count == kBaseFramesFramemd5);
  CHECK(static_cast<std::int64_t>(chain.block_digests.size()) == kBaseFramesFramemd5);
}

// --- Pitfall 12: cover art is never "the video" -----------------------------

TEST_CASE("video_decode_edges - cover art ignored", "[integration]") {
  auto base = probe("video_hash_base.mp4");
  auto cover = probe("video_cover.mp4");

  // The base video is stream 0 at Scope{video, 0}; its digest is exactly the
  // base file's own.
  CHECK(video_chain(*cover).digest == video_chain(*base).digest);
  CHECK(video_chain(*cover).element_count == kBaseFramesFramemd5);

  // The attached picture is stream 1 (Scope{video, 1}): no measurement, not
  // even a skip row, and no decode_path record.
  CHECK(find_measurement(*cover, "content.video.frame_hash", mediadiff::Scope::Kind::video, 1) == nullptr);
  REQUIRE(cover->envelope.decode_path.is_array());
  CHECK(cover->envelope.decode_path.size() == 1);
  CHECK(cover->envelope.decode_path.at(0).at("stream_index") == 0);
}

// --- D-05: a mid-stream resolution change keeps hashing ---------------------

TEST_CASE("video_decode_edges - geometry change", "[integration]") {
  auto fp = probe("video_geom_change.m2v");
  const mediadiff::HashChain& chain = video_chain(*fp);
  CHECK(chain.element_count == kGeomFramesFirstSegment + kGeomFramesSecondSegment);
  CHECK(static_cast<std::int64_t>(chain.block_digests.size()) == chain.element_count);
  // A raw elementary stream delivers no timestamps (Pitfall 11): no tick array.
  CHECK(chain.element_ticks.empty());

  const mediadiff::Measurement* m =
      find_measurement(*fp, "content.video.frame_hash", mediadiff::Scope::Kind::video, 0);
  REQUIRE(m != nullptr);
  REQUIRE(m->evidence.is_object());
  CHECK(m->evidence.at("geometry_change_count") == 1);
  CHECK(m->evidence.at("timestamps") == "unusable");
  // The normalization evidence names the FIRST segment's size.
  const std::string normalization = m->evidence.at("normalization").get<std::string>();
  CHECK(normalization.find("dims=352x288") != std::string::npos);
}

// --- VIDEO-03: a resolution change between two files is video.resolution's --

TEST_CASE("video_decode_edges - resolution change is incomparable", "[integration]") {
  int exit_code = -1;
  const nlohmann::ordered_json report = compare_json(
      {"compare", fixture("video_hash_base.mp4"), fixture("video_hash_small.mp4"), "--json"}, &exit_code);
  REQUIRE_FALSE(report.is_discarded());

  const auto* hash = find_finding(report, "content.video.frame_hash");
  REQUIRE(hash != nullptr);
  CHECK(hash->at("status") == "skipped");
  CHECK(hash->at("skip_reason") == "hash_incomparable");
  REQUIRE(hash->contains("message"));
  CHECK(hash->at("message").get<std::string>().find("normalization") != std::string::npos);

  // One intent, one finding: video.resolution owns the change.
  const auto* resolution = find_finding(report, "video.resolution");
  REQUIRE(resolution != nullptr);
  CHECK(resolution->at("status") != "pass");
  CHECK(exit_code == 1);
}
