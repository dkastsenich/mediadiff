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
#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/video_decode.h"
#include "report/json.h"
#include "report/model.h"
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
// Measured with the pinned ffmpeg (`-f framemd5`): video_corrupt_mpeg4_base.mkv
// decodes all 100 frames; video_corrupt_mpeg4.mkv (packet 40 damaged by the
// `noise` bitstream filter) decodes 99 -- libavcodec rejects the damaged
// packet with AVERROR_INVALIDDATA ("header damaged").
constexpr std::int64_t kCorruptBaseFramesFramemd5 = 100;
constexpr std::int64_t kCorruptFramesFramemd5 = 99;
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


// --- D-09 extended (research Open Question 3): decode errors reach the report

TEST_CASE("video_decode_edges - corrupt stream", "[integration]") {
  auto fp = probe("video_corrupt_mpeg4.mkv");
  const mediadiff::Measurement* hash =
      find_measurement(*fp, "content.video.frame_hash", mediadiff::Scope::Kind::video, 0);
  REQUIRE(hash != nullptr);
  REQUIRE(hash->evidence.is_object());
  const std::int64_t errors = hash->evidence.at("decode_error_count").get<std::int64_t>();
  const std::int64_t corrupt = hash->evidence.at("corrupt_frame_count").get<std::int64_t>();
  CHECK(errors + corrupt > 0);
  CHECK(hash->evidence.at("decode_path_class").get<std::string>().rfind("class2 ", 0) == 0);
  // The frame the decoder refused is absent from the hashed set: the literal
  // framemd5 count, not a value read back from mediadiff.
  CHECK(video_chain(*fp).element_count == kCorruptFramesFramemd5);

  // The decode_path record says class 2 as well.
  REQUIRE(fp->envelope.decode_path.size() == 1);
  CHECK(fp->envelope.decode_path.at(0).at("class") == 2);

  // meta.decode_errors at the video scope is exactly errors + corrupt frames,
  // with both components in evidence (A6).
  const mediadiff::Measurement* meta =
      find_measurement(*fp, "meta.decode_errors", mediadiff::Scope::Kind::video, 0);
  REQUIRE(meta != nullptr);
  const auto* value = std::get_if<std::int64_t>(&meta->value);
  REQUIRE(value != nullptr);
  CHECK(*value == errors + corrupt);
  REQUIRE(meta->evidence.is_object());
  CHECK(meta->evidence.at("decode_errors") == errors);
  CHECK(meta->evidence.at("corrupt_frames") == corrupt);
  CHECK(meta->evidence.contains("first_error_reason"));
}

TEST_CASE("video_decode_edges - clean zero", "[integration]") {
  for (const char* name : {"video_hash_base.mp4", "video_corrupt_mpeg4_base.mkv"}) {
    INFO(name);
    auto fp = probe(name);
    const mediadiff::Measurement* meta =
        find_measurement(*fp, "meta.decode_errors", mediadiff::Scope::Kind::video, 0);
    REQUIRE(meta != nullptr);
    // A real 0 -- never Absent, never a skip.
    const auto* value = std::get_if<std::int64_t>(&meta->value);
    REQUIRE(value != nullptr);
    CHECK(*value == 0);
    CHECK(meta->evidence.at("decode_errors") == 0);
    CHECK(meta->evidence.at("corrupt_frames") == 0);
  }
}

TEST_CASE("video_decode_edges - corrupt vs base", "[integration]") {
  int exit_code = -1;
  const nlohmann::ordered_json report = compare_json(
      {"compare", fixture("video_corrupt_mpeg4_base.mkv"), fixture("video_corrupt_mpeg4.mkv"), "--json"}, &exit_code);
  REQUIRE_FALSE(report.is_discarded());
  CHECK(exit_code == 1);

  const nlohmann::ordered_json* video_meta = nullptr;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.decode_errors" && finding.at("scope").at("kind") == "video") {
      video_meta = &finding;
    }
  }
  REQUIRE(video_meta != nullptr);
  CHECK(video_meta->at("status") == "fail");
  CHECK(video_meta->at("baseline") == 0);
  CHECK(video_meta->at("candidate") == 1);

  const auto* hash = find_finding(report, "content.video.frame_hash");
  REQUIRE(hash != nullptr);
  CHECK(hash->at("status") == "fail");
  CHECK(hash->at("baseline").at("element_count") == kCorruptBaseFramesFramemd5);
  CHECK(hash->at("candidate").at("element_count") == kCorruptFramesFramemd5);
}

// --- T-07-01 / T-07-06: a declared size over the bound is never opened -----

TEST_CASE("video_decode_edges - max pixels", "[integration]") {
  // The SPS declares 8208 x 8192, one macroblock column over kMaxVideoPixels
  // (8192 x 8192); nothing in this file is decodable, and nothing tries.
  auto session = mediadiff::DemuxSession::open(fixture("video_huge_dims.h264"), mediadiff::DemuxOptions{});
  REQUIRE(session.has_value());
  mediadiff::PacketScanRequest request;
  request.decode_video = true;
  auto scan = mediadiff::run_packet_scan(*session, request);
  REQUIRE(scan.has_value());
  REQUIRE(scan->video_decode.has_value());
  REQUIRE(scan->video_decode->per_stream.size() == 1);
  const mediadiff::StreamVideoDecode& stream = scan->video_decode->per_stream[0];
  CHECK_FALSE(stream.attempted);
  CHECK(stream.fallback_reason == std::string(mediadiff::kVideoFallbackMaxPixels));
  CHECK(stream.frame_count == 0);
  CHECK(stream.frame_digests.empty());

  auto fp = probe("video_huge_dims.h264");
  const mediadiff::Measurement* hash =
      find_measurement(*fp, "content.video.frame_hash", mediadiff::Scope::Kind::video, 0);
  REQUIRE(hash != nullptr);
  CHECK(std::holds_alternative<mediadiff::Absent>(hash->value));
  CHECK(hash->skip_reason == mediadiff::SkipReason::requires_decode);
  REQUIRE(hash->evidence.is_object());
  CHECK(hash->evidence.at("fallback_reason") == "max_pixels_exceeded");
  // No decode happened, so no decode_path record was written for the stream.
  CHECK(fp->envelope.decode_path.empty());
  // And meta.decode_errors reports the same requires_decode, never a count,
  // and the fingerprint is not marked partial (nothing failed to decode).
  const mediadiff::Measurement* meta =
      find_measurement(*fp, "meta.decode_errors", mediadiff::Scope::Kind::video, 0);
  REQUIRE(meta != nullptr);
  CHECK(meta->skip_reason == mediadiff::SkipReason::requires_decode);
  CHECK_FALSE(fp->partial);
}

// --- T-07-02: per-frame record accounting is deterministic -----------------

namespace {

// Puts the process-wide per-file packet-scan cap back when a test that
// changed it ends. The CLI cannot express a byte-exact budget (its flag is
// integer megabytes and compare.cpp re-sets the cap from it on every run), so
// this test drives the same pipeline compare.cpp runs -- fingerprint both
// inputs, compare_fingerprints, build_report_model, render_json -- directly.
struct PacketScanCapGuard {
  std::int64_t saved = mediadiff::default_packet_scan_max_bytes();
  ~PacketScanCapGuard() { mediadiff::set_default_packet_scan_max_bytes(saved); }
};

std::string render_compare_json(const mediadiff::Fingerprint& baseline, const mediadiff::Fingerprint& candidate,
                                const mediadiff::CheckRegistry& registry) {
  auto policy = mediadiff::resolve_policy(registry, mediadiff::ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  auto findings = mediadiff::compare_fingerprints(baseline, candidate, *policy, registry);
  REQUIRE(findings.has_value());
  const mediadiff::RenderOptions options{};
  const mediadiff::ReportModel model = mediadiff::build_report_model(candidate.envelope, *findings, registry, options);
  return mediadiff::render_json(model, registry, *policy, /*verbose=*/false);
}

}  // namespace

TEST_CASE("video_decode_edges - record budget", "[integration]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const PacketScanCapGuard guard;

  // The byte total an unconstrained sweep of this file accounts: every packet
  // record, every access-unit record and one kVideoFrameRecordBytes charge per
  // hashed frame, measured through the same request the orchestrator builds.
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

  // One frame record short of that total. Every packet and access-unit record
  // still fits, so the packet scan itself is complete; the frame records
  // exhaust the budget on the LAST frame the B-frame fixture's end-of-stream
  // drain hands back. (A budget that ran out mid-stream would end the packet
  // scan right after it -- the next packet record needs more bytes than the
  // frame record that just failed left behind -- and the check would report
  // skipped:partial_scan instead, which is a different, equally deterministic
  // outcome.)
  const std::int64_t budget = unconstrained_total - mediadiff::kVideoFrameRecordBytes;
  mediadiff::set_default_packet_scan_max_bytes(budget);

  auto run = [&]() {
    auto baseline = mediadiff::fingerprint_input(fixture("video_hash_base.mp4"), registry);
    auto candidate = mediadiff::fingerprint_input(fixture("video_hash_base.mp4"), registry);
    REQUIRE(baseline.has_value());
    REQUIRE(candidate.has_value());
    return std::make_pair(std::move(*baseline), std::move(*candidate));
  };

  auto first = run();
  const mediadiff::Measurement* m =
      find_measurement(first.first, "content.video.frame_hash", mediadiff::Scope::Kind::video, 0);
  REQUIRE(m != nullptr);
  REQUIRE(m->evidence.is_object());
  CHECK(m->evidence.at("sampling_state") == "truncated");
  CHECK(m->evidence.at("decode_truncation_reason") == std::string(mediadiff::kDecodeStopFrameRecordBudget));
  CHECK(video_chain(first.first).element_count == kBaseFramesFramemd5 - 1);

  // Two truncated sides are never comparable (a digest match over two
  // independently truncated prefixes cannot vouch for either remainder).
  const std::string json = render_compare_json(first.first, first.second, registry);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(json, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "skipped");
  CHECK(finding->at("skip_reason") == "hash_incomparable");

  // TRUST-05: a second run at the same budget is byte-identical.
  auto second = run();
  CHECK(render_compare_json(second.first, second.second, registry) == json);
}
