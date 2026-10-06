// 07-09-PLAN.md Task 2 (CONTENT-05 as amended by D-02): the lockstep's frame
// pairing proven on real fixtures through the CLI -- a dropped frame, a rate
// change, a remux and a raw elementary stream.
//
// Every expected number follows from how the fixture was made
// (scripts/gen_corpus.sh), not from a run of the scorer:
//   * video_loc_huffyuv_drop40.mkv is the 100-frame HuffYUV base with packet 40
//     removed and its 40 ms gap left in the timestamps, so exactly one baseline
//     frame has no partner and every later frame still pairs, all identical;
//   * video_perc_30.mkv is video_perc_60.mkv with every second frame kept
//     (framemd5-verified), so 90 of the 180 baseline frames pair, all identical;
//   * video_hash_base.mkv / .ts are stream copies of video_hash_base.mp4, so the
//     pictures are identical; the MKV pairs by time across the 1 ms timestamp
//     rounding and the TS (no declared frame rate, a 1.4 s start offset) pairs
//     by decode index, every frame still paired;
//   * video_cc_a53.m2v is video_cc_base.m2v plus caption side data, so identical
//     pictures; a raw MPEG-2 ES has no usable timestamps.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

using json = nlohmann::ordered_json;

constexpr const char* kPerceptual = "content.video.perceptual";
constexpr std::int64_t kMicro = 1000000;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// The perceptual finding of `mediadiff compare <a> <b> --json`. A missing
// finding is recorded with FAIL_CHECK, not FAIL: any statement after a Catch2
// FAIL is unreachable and a hard C4702 on MSVC (scripts/lint_dead_code_after_fail.sh).
json perceptual(const std::string& a, const std::string& b) {
  const CliResult result = run_cli({"compare", fixture(a), fixture(b), "--json"});
  const json report = json::parse(result.out, nullptr, false);
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == kPerceptual) {
      return finding;
    }
  }
  FAIL_CHECK("no content.video.perceptual finding in the report");
  return json();
}

const json& evidence_of(const json& finding) {
  REQUIRE(finding.is_object());
  return finding.at("evidence").at("candidate");
}

std::int64_t micro_of(const json& rational) {
  REQUIRE(rational.at("den") == kMicro);
  return rational.at("num").get<std::int64_t>();
}

}  // namespace

TEST_CASE("lockstep_pairing - dropped frame", "[integration]") {
  // Index pairing would misalign every frame after the drop and score the later
  // frames against their neighbours; time pairing leaves exactly one baseline
  // frame unpaired and scores a perfect 1.
  const json finding = perceptual("video_loc_huffyuv.mkv", "video_loc_huffyuv_drop40.mkv");
  const json& evidence = evidence_of(finding);
  CHECK(finding.at("candidate").at("num") == kMicro);
  CHECK(finding.at("candidate").at("den") == kMicro);
  CHECK(finding.at("status") == "pass");
  CHECK(evidence.at("pairing") == "time");
  CHECK_FALSE(evidence.contains("pairing_fallback"));
  CHECK(evidence.at("unpaired_baseline") == 1);
  CHECK(evidence.at("unpaired_candidate") == 0);
  CHECK(evidence.at("pairs_scored") == 99);
  CHECK(micro_of(evidence.at("mean")) == kMicro);
}

TEST_CASE("lockstep_pairing - rate change", "[integration]") {
  // 180 frames at 60 fps against its 90-frame 30 fps decimation: only the
  // coinciding frames pair.
  const json finding = perceptual("video_perc_60.mkv", "video_perc_30.mkv");
  const json& evidence = evidence_of(finding);
  CHECK(finding.at("candidate").at("num") == kMicro);
  CHECK(finding.at("candidate").at("den") == kMicro);
  CHECK(evidence.at("pairing") == "time");
  CHECK(evidence.at("pairs_scored") == 90);
  CHECK(evidence.at("unpaired_baseline") == 90);
  CHECK(evidence.at("unpaired_candidate") == 0);
}

TEST_CASE("lockstep_pairing - remux rounding", "[integration]") {
  // The Matroska remux rounds PTS to 1 ms; the MPEG-TS remux starts 1.4 s later
  // and declares no frame rate. Every one of the 100 frames still pairs and the
  // pictures are identical.
  for (const std::string remux : {"video_hash_base.mkv", "video_hash_base.ts"}) {
    INFO("remux: " << remux);
    const json finding = perceptual("video_hash_base.mp4", remux);
    const json& evidence = evidence_of(finding);
    CHECK(finding.at("candidate").at("num") == kMicro);
    CHECK(finding.at("candidate").at("den") == kMicro);
    CHECK(evidence.at("unpaired_baseline") == 0);
    CHECK(evidence.at("unpaired_candidate") == 0);
    CHECK(evidence.at("pairs_scored") == 100);
  }
  const json mkv = perceptual("video_hash_base.mp4", "video_hash_base.mkv");
  CHECK(evidence_of(mkv).at("pairing") == "time");
  const json ts = perceptual("video_hash_base.mp4", "video_hash_base.ts");
  CHECK(evidence_of(ts).at("pairing") == "index");
  CHECK(evidence_of(ts).at("pairing_fallback") == "candidate_interval_unknown");
}

TEST_CASE("lockstep_pairing - raw es fallback", "[integration]") {
  // A raw MPEG-2 elementary stream has no timestamps: the pair falls back to
  // index pairing and says why, and identical pictures still score 1.
  const json finding = perceptual("video_cc_base.m2v", "video_cc_a53.m2v");
  const json& evidence = evidence_of(finding);
  CHECK(evidence.at("pairing") == "index");
  REQUIRE(evidence.contains("pairing_fallback"));
  CHECK_FALSE(evidence.at("pairing_fallback").get<std::string>().empty());
  CHECK(finding.at("candidate").at("num") == kMicro);
  CHECK(finding.at("candidate").at("den") == kMicro);
  CHECK(evidence.at("unpaired_baseline") == 0);
  CHECK(evidence.at("unpaired_candidate") == 0);
}
