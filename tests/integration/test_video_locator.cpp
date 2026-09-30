// 07-03-PLAN.md Task 2 (CONTENT-02, D-07): the time-aligned frame locator in
// compare_hash, proven on real one-frame corruptions. Every expected index,
// count and time below is a literal derived from the fixture recipes in
// scripts/gen_corpus.sh, each of which was checked against `ffmpeg -f framemd5`
// (which frames differ, and that the dropped variant has 99 frames, all later
// ones keeping their PTS) -- never read back from mediadiff's own output. The
// times are exact: frame N of a 25 fps stream is N/25 s.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "compare/semantics.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/value.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_video_locator";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

const json* find_finding(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

// Runs `compare` and returns the parsed --json report.
json compare_json(const std::vector<std::string>& args) {
  CliResult result = run_cli(args);
  return json::parse(result.out, nullptr, false);
}

// The content.video.frame_hash finding of a compare, as a copy (so a caller
// needs no lifetime reasoning and no statement follows a failure call).
json hash_finding(const std::string& baseline, const std::string& candidate) {
  const json report = compare_json({"compare", fixture(baseline), fixture(candidate), "--json"});
  INFO(baseline << " vs " << candidate);
  REQUIRE_FALSE(report.is_discarded());
  const json* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  return *finding;
}

json rational(std::int64_t num, std::int64_t den) { return json{{"num", num}, {"den", den}}; }

// A range entry's (first, last, differing) triple.
void check_range(const json& range, std::int64_t first, std::int64_t last, std::int64_t differing) {
  CHECK(range.at("first") == first);
  CHECK(range.at("last") == last);
  CHECK(range.at("differing") == differing);
}

// Direct compare_hash over two synthetic video chains (one digest per frame,
// element_stride == 1), with hand-built evidence.
mediadiff::Finding compare_synthetic(const mediadiff::HashChain& baseline, const json& baseline_evidence,
                                     const mediadiff::HashChain& candidate, const json& candidate_evidence) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto index = registry.find("content.video.frame_hash");
  REQUIRE(index.has_value());
  mediadiff::Measurement baseline_measurement;
  baseline_measurement.check_index = *index;
  baseline_measurement.scope = mediadiff::Scope{mediadiff::Scope::Kind::video, 0};
  baseline_measurement.value = baseline;
  baseline_measurement.evidence = baseline_evidence;
  mediadiff::Measurement candidate_measurement = baseline_measurement;
  candidate_measurement.value = candidate;
  candidate_measurement.evidence = candidate_evidence;
  auto finding = mediadiff::compare_hash(registry.at(*index), baseline_measurement, candidate_measurement,
                                         mediadiff::Policy{mediadiff::ProfileId::sw_encoder});
  REQUIRE(finding.has_value());
  return *finding;
}

mediadiff::HashChain synthetic_chain(const std::string& digest, std::vector<std::string> digests) {
  mediadiff::HashChain chain;
  chain.algorithm = "xxh3-128";
  chain.digest = digest;
  chain.element_count = static_cast<std::int64_t>(digests.size());
  chain.block_digests = std::move(digests);
  chain.element_stride = 1;
  return chain;
}

std::vector<std::string> numbered(const std::string& prefix, int count) {
  std::vector<std::string> out;
  for (int i = 0; i < count; ++i) {
    out.push_back(prefix + std::to_string(i));
  }
  return out;
}

}  // namespace

// --- Test 1: a one-frame corruption of an intra-only stream ----------------

TEST_CASE("video_locator - one frame", "[integration]") {
  const json finding = hash_finding("video_loc_huffyuv.mkv", "video_loc_huffyuv_c40.mkv");
  CHECK(finding.at("status") == "fail");
  const json& evidence = finding.at("evidence");

  CHECK(evidence.at("pairing") == "time");
  CHECK_FALSE(evidence.contains("pairing_fallback"));
  const json& first = evidence.at("first_divergent_frame");
  CHECK(first.at("baseline_index") == 40);
  CHECK(first.at("candidate_index") == 40);
  // The PTS is the baseline's own tick for frame 40 (read from the chain the
  // same finding carries), in its own timebase; Matroska counts milliseconds.
  REQUIRE(finding.at("baseline").at("element_ticks").size() == 100);
  CHECK(first.at("pts").at("value") == finding.at("baseline").at("element_ticks").at(40));
  CHECK(first.at("pts").at("value") == 1600);
  CHECK(first.at("pts").at("tb") == rational(1, 1000));
  // Frame 40 of a 25 fps stream starts at exactly 8/5 s.
  CHECK(first.at("time") == rational(8, 5));

  REQUIRE(evidence.at("divergent_ranges").size() == 1);
  check_range(evidence.at("divergent_ranges").at(0), 40, 40, 1);
  CHECK(evidence.at("divergent_ranges").at(0).at("start_time") == rational(8, 5));
  CHECK(evidence.at("divergent_ranges").at(0).at("end_time") == rational(8, 5));
  CHECK(evidence.at("divergent_range_count") == 1);
  CHECK(evidence.at("differing_frame_count") == 1);
  CHECK(evidence.at("missing_from_candidate").empty());
  CHECK(evidence.at("missing_from_candidate_count") == 0);
  CHECK(evidence.at("extra_in_candidate").empty());
  CHECK(evidence.at("extra_in_candidate_count") == 0);
  CHECK(evidence.at("locator_truncated") == false);

  // Milliseconds appear only in the rendered message, formatted from the exact time.
  CHECK(finding.at("message").get<std::string>().find("frame 40 differs (1600.0 ms)") != std::string::npos);
  CHECK(finding.at("message").get<std::string>().find("samples [") == std::string::npos);
}

// --- Test 2: damage that propagates to the next I-frame ---------------------

TEST_CASE("video_locator - propagation", "[integration]") {
  SECTION("a damaged packet that decodes: one range 40..49 and ten differing frames") {
    // video_loc_mpeg4_c40.mkv: packet 40 of the -g 25 / no-B-frame base damaged
    // at amount 200, which libavcodec conceals rather than rejects (100 frames
    // out); ffmpeg -f framemd5 against the base differs on exactly frames 40-49,
    // because the damage runs through the P-frames to the I-frame at 50.
    const json finding = hash_finding("video_corrupt_mpeg4_base.mkv", "video_loc_mpeg4_c40.mkv");
    const json& evidence = finding.at("evidence");
    CHECK(evidence.at("pairing") == "time");
    REQUIRE(evidence.at("divergent_ranges").size() == 1);
    check_range(evidence.at("divergent_ranges").at(0), 40, 49, 10);
    CHECK(evidence.at("divergent_ranges").at(0).at("start_time") == rational(8, 5));
    CHECK(evidence.at("divergent_ranges").at(0).at("end_time") == rational(49, 25));
    CHECK(evidence.at("divergent_range_count") == 1);
    CHECK(evidence.at("differing_frame_count") == 10);
    CHECK(evidence.at("missing_from_candidate_count") == 0);
    CHECK(finding.at("message").get<std::string>().find("frames 40-49 differ (10 frames)") != std::string::npos);
  }
  SECTION("a damaged packet the decoder rejects: frame 40 is missing, 41..49 differ") {
    // video_corrupt_mpeg4.mkv (07-02): at amount 50 libavcodec rejects packet
    // 40 (99 frames out, every later frame keeping its PTS), so the truth is one
    // frame missing from the candidate plus nine propagated differences -- not a
    // tenth differing frame. framemd5 against the base: 99 lines, frames 41-49 differ.
    const json finding = hash_finding("video_corrupt_mpeg4_base.mkv", "video_corrupt_mpeg4.mkv");
    const json& evidence = finding.at("evidence");
    REQUIRE(evidence.at("divergent_ranges").size() == 1);
    check_range(evidence.at("divergent_ranges").at(0), 41, 49, 9);
    CHECK(evidence.at("differing_frame_count") == 9);
    REQUIRE(evidence.at("missing_from_candidate").size() == 1);
    CHECK(evidence.at("missing_from_candidate").at(0).at("first") == 40);
    CHECK(evidence.at("missing_from_candidate").at(0).at("last") == 40);
    CHECK(evidence.at("missing_from_candidate_count") == 1);
    // The candidate's index is one behind the baseline's after the lost frame.
    CHECK(evidence.at("first_divergent_frame").at("baseline_index") == 41);
    CHECK(evidence.at("first_divergent_frame").at("candidate_index") == 40);
    CHECK(finding.at("message").get<std::string>().find("frame 40 missing from candidate") != std::string::npos);
  }
}

// --- Test 3: a dropped frame is "missing", never "everything after differs" -

TEST_CASE("video_locator - dropped frame", "[integration]") {
  const json finding = hash_finding("video_loc_huffyuv.mkv", "video_loc_huffyuv_drop40.mkv");
  CHECK(finding.at("status") == "fail");
  const json& evidence = finding.at("evidence");
  CHECK(evidence.at("pairing") == "time");
  REQUIRE(evidence.at("missing_from_candidate").size() == 1);
  CHECK(evidence.at("missing_from_candidate").at(0).at("first") == 40);
  CHECK(evidence.at("missing_from_candidate").at(0).at("last") == 40);
  CHECK(evidence.at("missing_from_candidate_count") == 1);
  // Every other frame is identical and keeps its PTS, so nothing else differs:
  // index alignment would have said frames 40..98 differ.
  CHECK(evidence.at("differing_frame_count") == 0);
  CHECK(evidence.at("divergent_ranges").empty());
  CHECK(evidence.at("divergent_range_count") == 0);
  CHECK_FALSE(evidence.contains("first_divergent_frame"));
  CHECK(evidence.at("extra_in_candidate_count") == 0);

  const std::string message = finding.at("message").get<std::string>();
  CHECK(message.find("frame 40 missing from candidate") != std::string::npos);
  CHECK(message.find(" differ") == std::string::npos);
  CHECK(message.find("40-98") == std::string::npos);
}

// --- Tests 4 and 5: range merging at one-frame gaps -------------------------

TEST_CASE("video_locator - merge gap one", "[integration]") {
  // Frames 40 and 42 differ; frame 41 matches. A gap of exactly one matching
  // frame merges: one range 40..42 with two differing frames.
  const json finding = hash_finding("video_loc_huffyuv.mkv", "video_loc_huffyuv_c40_42.mkv");
  const json& evidence = finding.at("evidence");
  REQUIRE(evidence.at("divergent_ranges").size() == 1);
  check_range(evidence.at("divergent_ranges").at(0), 40, 42, 2);
  CHECK(evidence.at("divergent_ranges").at(0).at("start_time") == rational(8, 5));
  CHECK(evidence.at("divergent_ranges").at(0).at("end_time") == rational(42, 25));
  CHECK(evidence.at("divergent_range_count") == 1);
  CHECK(evidence.at("differing_frame_count") == 2);
  CHECK(finding.at("message").get<std::string>().find("frames 40-42 differ (2 frames)") != std::string::npos);
}

TEST_CASE("video_locator - merge gap two", "[integration]") {
  // Frames 40 and 43 differ; 41 and 42 match. A gap of two does not merge.
  const json finding = hash_finding("video_loc_huffyuv.mkv", "video_loc_huffyuv_c40_43.mkv");
  const json& evidence = finding.at("evidence");
  REQUIRE(evidence.at("divergent_ranges").size() == 2);
  check_range(evidence.at("divergent_ranges").at(0), 40, 40, 1);
  check_range(evidence.at("divergent_ranges").at(1), 43, 43, 1);
  CHECK(evidence.at("divergent_ranges").at(1).at("start_time") == rational(43, 25));
  CHECK(evidence.at("divergent_range_count") == 2);
  CHECK(evidence.at("differing_frame_count") == 2);
  CHECK(finding.at("message").get<std::string>().find("2 frames differ across 2 ranges, first at frame 40") !=
        std::string::npos);
}

// --- Test 6: a snapshot baseline yields byte-identical locator evidence -----

TEST_CASE("video_locator - snapshot equivalence", "[integration]") {
  const std::string snap_path = (scratch_dir() / "video_loc_huffyuv.snap.json").string();
  REQUIRE(run_cli({"snapshot", fixture("video_loc_huffyuv.mkv"), "--out", snap_path}).exit_code == 0);

  const json live = hash_finding("video_loc_huffyuv.mkv", "video_loc_huffyuv_c40.mkv");
  const json report =
      compare_json({"compare", snap_path, fixture("video_loc_huffyuv_c40.mkv"), "--json"});
  REQUIRE_FALSE(report.is_discarded());
  const json* from_snapshot = find_finding(report, "content.video.frame_hash");
  REQUIRE(from_snapshot != nullptr);

  CHECK(from_snapshot->at("status") == "fail");
  REQUIRE(from_snapshot->at("evidence").contains("first_divergent_frame"));
  // Byte for byte: the same stored per-frame arrays drive both runs.
  CHECK(from_snapshot->at("evidence").dump() == live.at("evidence").dump());
  CHECK(from_snapshot->at("message") == live.at("message"));

  // And the drop case, where the locator's answer depends on the stored ticks.
  const std::string drop_live_path = fixture("video_loc_huffyuv_drop40.mkv");
  const json drop_live = hash_finding("video_loc_huffyuv.mkv", "video_loc_huffyuv_drop40.mkv");
  const json drop_report = compare_json({"compare", snap_path, drop_live_path, "--json"});
  REQUIRE_FALSE(drop_report.is_discarded());
  const json* drop_snapshot = find_finding(drop_report, "content.video.frame_hash");
  REQUIRE(drop_snapshot != nullptr);
  CHECK(drop_snapshot->at("evidence").dump() == drop_live.at("evidence").dump());
}

// --- Test 7: index fallback when a side has no usable timestamps ------------

TEST_CASE("video_locator - index fallback", "[integration]") {
  const json unusable = json{{"timestamps", "unusable"}, {"frame_interval", json{{"num", 0}, {"den", 0}}}};
  const mediadiff::HashChain baseline = synthetic_chain("a", numbered("d", 6));
  std::vector<std::string> candidate_digests = numbered("d", 6);
  candidate_digests[3] = "changed";
  const mediadiff::HashChain candidate = synthetic_chain("b", candidate_digests);

  const mediadiff::Finding finding = compare_synthetic(baseline, unusable, candidate, unusable);
  CHECK(finding.status == mediadiff::Status::fail);
  const json& evidence = finding.evidence;
  CHECK(evidence.at("pairing") == "index");
  CHECK(evidence.at("pairing_fallback") == "baseline_timestamps_unusable");
  REQUIRE(evidence.at("divergent_ranges").size() == 1);
  check_range(evidence.at("divergent_ranges").at(0), 3, 3, 1);
  // With no ticks there is no time to state: never a fabricated one.
  CHECK_FALSE(evidence.at("divergent_ranges").at(0).contains("start_time"));
  CHECK_FALSE(evidence.at("first_divergent_frame").contains("pts"));
  CHECK_FALSE(evidence.at("first_divergent_frame").contains("time"));
  CHECK(evidence.at("first_divergent_frame").at("baseline_index") == 3);
  CHECK(evidence.at("differing_frame_count") == 1);
  CHECK(finding.message.find("paired by decode order (baseline_timestamps_unusable)") != std::string::npos);
}

TEST_CASE("video_locator - index fallback on a real fixture with no frame rate", "[integration]") {
  // An MPEG-TS stream reports no average frame rate ({0, 0}), so pairing against
  // it cannot use time and falls back to decode order, recorded in the evidence.
  const json finding = hash_finding("video_hash_base.ts", "video_hash_alt.mp4");
  const json& evidence = finding.at("evidence");
  CHECK(evidence.at("pairing") == "index");
  CHECK(evidence.at("pairing_fallback") == "baseline_interval_unknown");
  // A different quantizer changes every one of the 100 frames, and decode order
  // pairs them one to one: a single range and no unpaired frame.
  CHECK(evidence.at("differing_frame_count") == 100);
  CHECK(evidence.at("missing_from_candidate_count") == 0);
  CHECK(evidence.at("extra_in_candidate_count") == 0);
}

// --- T-07-11: evidence lists are capped, with exact totals -------------------

TEST_CASE("video_locator - evidence lists are capped with exact totals", "[integration]") {
  SECTION("differing ranges") {
    // 300 frames, every third one differing: 100 ranges, each separated by two
    // matching frames, so none merges.
    const json no_timestamps = json{{"timestamps", "unusable"}, {"frame_interval", json{{"num", 0}, {"den", 0}}}};
    const mediadiff::HashChain baseline = synthetic_chain("a", numbered("d", 300));
    std::vector<std::string> candidate_digests = numbered("d", 300);
    for (int i = 0; i < 300; i += 3) {
      candidate_digests[static_cast<std::size_t>(i)] = "changed" + std::to_string(i);
    }
    const mediadiff::HashChain candidate = synthetic_chain("b", candidate_digests);
    const mediadiff::Finding finding = compare_synthetic(baseline, no_timestamps, candidate, no_timestamps);
    const json& evidence = finding.evidence;
    CHECK(evidence.at("divergent_ranges").size() == 64);
    CHECK(evidence.at("divergent_range_count") == 100);
    CHECK(evidence.at("differing_frame_count") == 100);
    CHECK(evidence.at("locator_truncated") == true);
    // The kept ranges are the FIRST 64, ascending.
    check_range(evidence.at("divergent_ranges").at(0), 0, 0, 1);
    check_range(evidence.at("divergent_ranges").at(63), 189, 189, 1);
    CHECK(finding.message.find("lists truncated at 64 ranges") != std::string::npos);
  }
  SECTION("missing ranges, by time") {
    // The candidate keeps every third baseline frame (30 ticks apart at 1/250
    // against the baseline's 10): baseline frames 3k+1 and 3k+2 are missing, 100
    // ranges of two frames, 200 frames in all; nothing else differs.
    const mediadiff::HashChain baseline = [] {
      mediadiff::HashChain chain = synthetic_chain("a", numbered("d", 300));
      for (int i = 0; i < 300; ++i) {
        chain.element_ticks.push_back(10 * i);
      }
      chain.element_tb = mediadiff::Rational{1, 250};
      return chain;
    }();
    const mediadiff::HashChain candidate = [] {
      std::vector<std::string> digests;
      for (int i = 0; i < 300; i += 3) {
        digests.push_back("d" + std::to_string(i));
      }
      mediadiff::HashChain chain = synthetic_chain("b", digests);
      for (int j = 0; j < 100; ++j) {
        chain.element_ticks.push_back(30 * j);
      }
      chain.element_tb = mediadiff::Rational{1, 250};
      return chain;
    }();
    const json evidence_in = json{{"timestamps", "pts"}, {"frame_interval", json{{"num", 1}, {"den", 25}}}};
    const mediadiff::Finding finding = compare_synthetic(baseline, evidence_in, candidate, evidence_in);
    const json& evidence = finding.evidence;
    CHECK(evidence.at("pairing") == "time");
    CHECK(evidence.at("differing_frame_count") == 0);
    CHECK(evidence.at("missing_from_candidate").size() == 64);
    CHECK(evidence.at("missing_from_candidate_count") == 200);
    CHECK(evidence.at("missing_from_candidate").at(0).at("first") == 1);
    CHECK(evidence.at("missing_from_candidate").at(0).at("last") == 2);
    CHECK(evidence.at("missing_from_candidate").at(63).at("first") == 190);
    CHECK(evidence.at("extra_in_candidate_count") == 0);
    CHECK(evidence.at("locator_truncated") == true);
  }
}

// --- Test 8: the audio locator is untouched ----------------------------------

TEST_CASE("video_locator - audio unchanged", "[integration]") {
  const json report = compare_json({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_alt.mp4"), "--json"});
  REQUIRE_FALSE(report.is_discarded());
  const json* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  const json& evidence = finding->at("evidence");
  // The block locator's own keys, and none of the frame locator's.
  CHECK(evidence.contains("first_divergent_block"));
  CHECK(evidence.contains("sample_range"));
  CHECK(evidence.contains("divergent_block_count"));
  CHECK_FALSE(evidence.contains("pairing"));
  CHECK_FALSE(evidence.contains("differing_frame_count"));
  CHECK(finding->at("message").get<std::string>().find("divergent block") != std::string::npos);
}
