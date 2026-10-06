// 07-08-PLAN.md (CONTENT-04; D-01, D-02, D-03): content.video.perceptual through
// the real CLI and the in-process lockstep.
//
// The numbers in a live comparison are checked against an INDEPENDENT oracle:
// the test collects every frame's thumbnail of both fixtures through its own
// FrameTap, scores each pair with the integer SSIM header, and recomputes the
// minimum, the floor mean, the first pair below 985000 and the ten worst pairs
// itself, so nothing here reads an expected value back from the scorer it tests.
// The fixtures' truth by construction (scripts/gen_corpus.sh):
//   * video_hash_base.mp4 and video_hash_base.ts decode to identical pixels (the
//     TS is a stream copy), so their score is exactly 1000000 on both sides;
//   * video_perc_degraded.mp4 is the base picture scaled to 88x72 and back and
//     encoded at -q:v 31: the worst thumbnail SSIM is far below 0.985;
//   * video_perc_upscaled.mp4 is the base at 704x576: both thumbnail to 128x104;
//   * video_hash_small.mp4 is 320x240: its thumbnail is 128x96.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/lockstep.h"
#include "probe/orchestrator.h"
#include "probe/video_decode.h"
#include "probe/video_thumbnail.h"
#include "support/fixture_paths.h"
#include "util/ssim_int.h"
#include "util/version.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kPerceptual = "content.video.perceptual";
constexpr std::int64_t kMicro = 1000000;
constexpr std::int64_t kThreshold = 985000;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_perceptual";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

struct CompareOutput {
  int exit_code = 0;
  json finding;
};

// The perceptual finding of `mediadiff compare <a> <b> --json <extra...>`; null
// JSON when the report has none.
CompareOutput compare(const std::string& a, const std::string& b, const std::vector<std::string>& extra = {}) {
  std::vector<std::string> args = {"compare", a, b, "--json"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  const json report = json::parse(result.out, nullptr, false);
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  CompareOutput out;
  out.exit_code = result.exit_code;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == kPerceptual) {
      out.finding = finding;
    }
  }
  return out;
}

CompareOutput compare_fixtures(const std::string& a, const std::string& b, const std::vector<std::string>& extra = {}) {
  return compare(fixture(a), fixture(b), extra);
}

const json& candidate_evidence(const CompareOutput& out) {
  REQUIRE_FALSE(out.finding.is_null());
  return out.finding.at("evidence").at("candidate");
}

std::int64_t micro_of(const json& rational) {
  // Every rational in this check's evidence is over 1000000.
  REQUIRE(rational.at("den") == kMicro);
  return rational.at("num").get<std::int64_t>();
}

// Every frame's thumbnail, in decode order, collected through the tap.
class CollectTap final : public mediadiff::FrameTap {
 public:
  bool publish(const mediadiff::TappedFrame& frame) override {
    if (frame.thumbnail != nullptr) {
      thumbnails.push_back(*frame.thumbnail);
    }
    return true;
  }
  void finish(const mediadiff::TapEnd&) override {}
  std::vector<mediadiff::Thumbnail> thumbnails;
};

std::vector<mediadiff::Thumbnail> thumbnails_of(const std::string& name) {
  CollectTap tap;
  mediadiff::ProbeOptions options;
  options.frame_tap = &tap;
  auto fp = mediadiff::detail::run_probe(fixture(name), mediadiff::all_analyzers(), nullptr, options);
  REQUIRE(fp.has_value());
  return std::move(tap.thumbnails);
}

struct Oracle {
  std::vector<std::int64_t> scores;  // one per pair, pair i = frame i on both sides
  std::int64_t min = 0;
  std::int64_t mean = 0;
  std::optional<std::size_t> first_below;
  std::vector<std::pair<std::int64_t, std::size_t>> worst;  // (score, index), ascending
};

Oracle oracle(const std::string& baseline, const std::string& candidate) {
  const std::vector<mediadiff::Thumbnail> a = thumbnails_of(baseline);
  const std::vector<mediadiff::Thumbnail> b = thumbnails_of(candidate);
  REQUIRE(a.size() == b.size());
  REQUIRE_FALSE(a.empty());
  Oracle out;
  std::int64_t sum = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    REQUIRE(a[i].width == b[i].width);
    REQUIRE(a[i].height == b[i].height);
    const auto q24 = mediadiff::ssim_plane_q24(a[i].pixels.data(), a[i].width, b[i].pixels.data(), b[i].width,
                                               a[i].width, a[i].height);
    REQUIRE(q24.has_value());
    const std::int64_t micro = mediadiff::q24_to_micro(*q24);
    out.scores.push_back(micro);
    sum += micro;
    if (!out.first_below.has_value() && micro < kThreshold) {
      out.first_below = i;
    }
    out.worst.emplace_back(micro, i);
  }
  out.min = *std::min_element(out.scores.begin(), out.scores.end());
  out.mean = sum / static_cast<std::int64_t>(out.scores.size());  // the sum is positive here
  std::sort(out.worst.begin(), out.worst.end());
  if (out.worst.size() > 10) {
    out.worst.resize(10);
  }
  return out;
}

}  // namespace

// --- Test 6: identical pictures -----------------------------------------------

TEST_CASE("perceptual - identical media", "[integration]") {
  // A stream copy into MPEG-TS decodes to the same pixels: exactly 1 on both
  // sides, a delta of exactly 0, status pass.
  const CompareOutput out = compare_fixtures("video_hash_base.mp4", "video_hash_base.ts");
  REQUIRE_FALSE(out.finding.is_null());
  CHECK(out.finding.at("status") == "pass");
  CHECK(out.finding.at("baseline").at("num") == kMicro);
  CHECK(out.finding.at("baseline").at("den") == kMicro);
  CHECK(out.finding.at("candidate").at("num") == kMicro);
  CHECK(out.finding.at("candidate").at("den") == kMicro);
  CHECK(out.finding.at("unit") == "score");
  CHECK(out.finding.at("tolerance").at("num") == 15);
  CHECK(out.finding.at("tolerance").at("den") == 1000);

  // MPEG-TS declares no frame rate at open, so the comparison pairs by decode
  // index and says why (D-02's recorded fallback) -- and still pairs all 100.
  const json& evidence = candidate_evidence(out);
  CHECK(evidence.at("pairing") == "index");
  CHECK(evidence.at("pairing_fallback") == "candidate_interval_unknown");
  CHECK(evidence.at("pairs_scored") == 100);
  CHECK(evidence.at("unpaired_baseline") == 0);
  CHECK(evidence.at("unpaired_candidate") == 0);
  CHECK(evidence.at("first_below_threshold").is_null());
  CHECK(micro_of(evidence.at("mean")) == kMicro);
  CHECK(evidence.at("worst").size() == 10);

  // The same file against itself pairs by TIME.
  const CompareOutput same = compare_fixtures("video_hash_base.mp4", "video_hash_base_copy.mp4");
  CHECK(same.finding.at("status") == "pass");
  CHECK(candidate_evidence(same).at("pairing") == "time");
  CHECK_FALSE(candidate_evidence(same).contains("pairing_fallback"));
  CHECK(micro_of(candidate_evidence(same).at("mean")) == kMicro);
}

TEST_CASE("perceptual - raw elementary streams pair by index", "[integration]") {
  // No timestamps at all: time pairing is impossible, so the baseline's side is
  // named as the reason, and identical pixels still score exactly 1.
  const CompareOutput out = compare_fixtures("video_cc_base.m2v", "video_cc_a53.m2v");
  REQUIRE_FALSE(out.finding.is_null());
  const json& evidence = candidate_evidence(out);
  CHECK(evidence.at("pairing") == "index");
  CHECK(evidence.at("pairing_fallback") == "baseline_timestamps_unusable");
  CHECK(out.finding.at("candidate").at("num") == kMicro);
}

// --- Test 7: a degraded encode ------------------------------------------------

TEST_CASE("perceptual - degraded", "[integration]") {
  const Oracle expected = oracle("video_hash_base.mp4", "video_perc_degraded.mp4");
  REQUIRE(expected.min < kThreshold);

  const CompareOutput hw = compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--profile", "hw-encoder"});
  REQUIRE_FALSE(hw.finding.is_null());
  CHECK(hw.finding.at("status") == "fail");
  CHECK(hw.finding.at("severity") == "fail");
  CHECK(hw.finding.at("skip_reason") == "none");
  CHECK(hw.exit_code == 1);

  // The compared value is the minimum pair, and every statistic equals the
  // independent oracle's.
  CHECK(hw.finding.at("baseline").at("num") == kMicro);
  CHECK(hw.finding.at("candidate").at("num") == expected.min);
  CHECK(hw.finding.at("candidate").at("den") == kMicro);
  const json& evidence = candidate_evidence(hw);
  CHECK(evidence.at("pairs_scored") == static_cast<std::int64_t>(expected.scores.size()));
  CHECK(micro_of(evidence.at("mean")) == expected.mean);
  CHECK(micro_of(evidence.at("threshold")) == kThreshold);

  REQUIRE_FALSE(evidence.at("first_below_threshold").is_null());
  const json& first = evidence.at("first_below_threshold");
  REQUIRE(expected.first_below.has_value());
  CHECK(first.at("baseline_index") == static_cast<std::int64_t>(*expected.first_below));
  CHECK(first.at("candidate_index") == static_cast<std::int64_t>(*expected.first_below));
  CHECK(micro_of(first.at("score")) == expected.scores[*expected.first_below]);
  CHECK(micro_of(first.at("score")) < kThreshold);
  CHECK(first.at("pts").at("tb").at("den") > 0);

  // The worst-10 list: ten entries, score ascending, ties by baseline index.
  const json& worst = evidence.at("worst");
  REQUIRE(worst.size() == expected.worst.size());
  for (std::size_t i = 0; i < worst.size(); ++i) {
    INFO("worst[" << i << "]");
    CHECK(micro_of(worst[i].at("score")) == expected.worst[i].first);
    CHECK(worst[i].at("baseline_index") == static_cast<std::int64_t>(expected.worst[i].second));
  }
  CHECK(micro_of(worst[0].at("score")) == expected.min);

  // reference_identity is the baseline's XXH3-128 input identity.
  auto identity = mediadiff::compute_input_identity(fixture("video_hash_base.mp4"));
  REQUIRE(identity.has_value());
  CHECK(evidence.at("reference_identity") == identity->xxh3_128);
  CHECK(evidence.at("baseline_stream_index") == 0);
  CHECK(evidence.at("candidate_stream_index") == 0);

  // D-04's record rides on both sides, spelled exactly.
  const std::string scaler = mediadiff::scaler_record(104);
  const std::string path_signature =
      mediadiff::compose_decode_path_signature() + " flags/" + std::string(mediadiff::kVideoDecoderFlagsRecorded);
  const json& baseline_evidence = hw.finding.at("evidence").at("baseline");
  CHECK(baseline_evidence.at("scaler_path") == scaler);
  CHECK(evidence.at("scaler_path") == scaler);
  CHECK(baseline_evidence.at("decode_path_signature") == path_signature);
  CHECK(evidence.at("decode_path_signature") == path_signature);
  CHECK(baseline_evidence.at("self_score") == true);
  CHECK(evidence.at("sampling_state") == "full");

  // Severity by profile: sw-encoder keeps `info`, hw-encoder and transform
  // gate, strict-bitexact and remux carry no pixel expectation of their own.
  const CompareOutput sw = compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--profile", "sw-encoder"});
  CHECK(sw.finding.at("status") == "info");
  CHECK(sw.finding.at("severity") == "info");
  const CompareOutput transform =
      compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--profile", "transform"});
  CHECK(transform.finding.at("severity") == "fail");
  const CompareOutput strict =
      compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--profile", "strict-bitexact"});
  CHECK(strict.finding.at("severity") == "ignore");
  const CompareOutput remux = compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--profile", "remux"});
  CHECK(remux.finding.at("severity") == "ignore");

  // The unit suffix is a usage error naming the bare form.
  const CliResult bad = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_perc_degraded.mp4"), "--tol",
                                 "content.video.perceptual=0.5dB"});
  CHECK(bad.exit_code == 64);
  CHECK(bad.err.find("bare score tolerance") != std::string::npos);
}

// --- Test 8: one-sided probes -------------------------------------------------

TEST_CASE("perceptual - one sided", "[integration]") {
  const std::string snap = scratch("base.snap.json");
  REQUIRE(run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap, "--force"}).exit_code == 0);

  // The snapshot itself records the honest skip, and it survives a round trip.
  auto read = mediadiff::read_snapshot(snap, mediadiff::builtin_registry());
  REQUIRE(read.has_value());
  const std::uint32_t index = *mediadiff::builtin_registry().find(kPerceptual);
  const mediadiff::Measurement* stored = nullptr;
  for (const mediadiff::Measurement& m : read->measurements) {
    if (m.check_index == index) {
      stored = &m;
    }
  }
  REQUIRE(stored != nullptr);
  CHECK(stored->skip_reason == mediadiff::SkipReason::requires_media);
  CHECK(stored->scope.kind == mediadiff::Scope::Kind::video);
  CHECK(stored->scope.index == 0);
  CHECK(std::holds_alternative<mediadiff::Absent>(stored->value));

  // (1) snapshot baseline vs media candidate, (2) media baseline vs snapshot
  // candidate: never a score, never a failure, and no partial_scan-type reason.
  for (const bool snapshot_first : {true, false}) {
    const std::string a = snapshot_first ? snap : fixture("video_perc_degraded.mp4");
    const std::string b = snapshot_first ? fixture("video_perc_degraded.mp4") : snap;
    const CompareOutput out = compare(a, b, {"--profile", "hw-encoder"});
    INFO("snapshot first: " << snapshot_first);
    REQUIRE_FALSE(out.finding.is_null());
    CHECK(out.finding.at("status") == "skipped");
    CHECK(out.finding.at("skip_reason") == "requires_media");
  }

  // (3) a snapshot taken before this check existed carries no perceptual entry
  // at all: the engine never pairs a measurement present on only one side, so
  // the compare reports nothing for it -- no failure, no warning, no skip.
  json doc = json::parse(std::ifstream(snap), nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  json kept = json::array();
  for (const auto& m : doc.at("measurements")) {
    if (m.at("id") != kPerceptual) {
      kept.push_back(m);
    }
  }
  REQUIRE(kept.size() + 1 == doc.at("measurements").size());
  doc["measurements"] = kept;
  const std::string legacy = scratch("legacy.snap.json");
  {
    std::ofstream out(legacy, std::ios::binary | std::ios::trunc);
    out << doc.dump(2) << "\n";
  }
  const CompareOutput legacy_out = compare(legacy, fixture("video_perc_degraded.mp4"), {"--profile", "hw-encoder"});
  CHECK(legacy_out.finding.is_null());

  // inspect renders the explicit skip row.
  const CliResult inspect = run_cli({"inspect", "--content", "--json", fixture("video_hash_base.mp4")});
  REQUIRE(inspect.exit_code == 0);
  const json inspected = json::parse(inspect.out, nullptr, false);
  REQUIRE_FALSE(inspected.is_discarded());
  bool found_row = false;
  for (const auto& row : inspected.at("groups").at("content")) {
    if (row.at("id") == kPerceptual) {
      found_row = true;
      CHECK(row.at("status") == "skipped");
      CHECK(row.at("skip_reason") == "requires_media");
      CHECK(row.at("scope") == "video[0]");
    }
  }
  CHECK(found_row);

  // --no-content: requires_decode on both sides.
  const CompareOutput no_content =
      compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--no-content", "--profile", "hw-encoder"});
  REQUIRE_FALSE(no_content.finding.is_null());
  CHECK(no_content.finding.at("status") == "skipped");
  CHECK(no_content.finding.at("skip_reason") == "requires_decode");
}

// --- Test 9: cross resolution -------------------------------------------------

TEST_CASE("perceptual - cross resolution", "[integration]") {
  // 352x288 and 704x576 both thumbnail to 128x104: scored, not skipped.
  const CompareOutput scored = compare_fixtures("video_hash_base.mp4", "video_perc_upscaled.mp4");
  REQUIRE_FALSE(scored.finding.is_null());
  CHECK(scored.finding.at("skip_reason") == "none");
  const std::string scaler = mediadiff::scaler_record(104);
  CHECK(scaler.find("dst=128x104") != std::string::npos);
  CHECK(scored.finding.at("evidence").at("baseline").at("scaler_path") == scaler);
  CHECK(candidate_evidence(scored).at("scaler_path") == scaler);
  const Oracle expected = oracle("video_hash_base.mp4", "video_perc_upscaled.mp4");
  CHECK(scored.finding.at("candidate").at("num") == expected.min);
  CHECK(expected.min > kThreshold);

  // 352x288 (128x104) against 320x240 (128x96): no pairing is possible.
  const CompareOutput mismatch = compare_fixtures("video_hash_base.mp4", "video_hash_small.mp4");
  REQUIRE_FALSE(mismatch.finding.is_null());
  CHECK(mismatch.finding.at("status") == "skipped");
  CHECK(mismatch.finding.at("skip_reason") == "geometry_mismatch");
  CHECK(mismatch.finding.at("evidence").at("baseline").at("baseline_thumbnail") == "128x104");
  CHECK(mismatch.finding.at("evidence").at("baseline").at("candidate_thumbnail") == "128x96");
  CHECK(mismatch.finding.at("evidence").at("candidate").at("candidate_thumbnail") == "128x96");
}

// --- Test 10: --sample N -------------------------------------------------------

TEST_CASE("perceptual - sampling", "[integration]") {
  // Every second pair is scored: 100 pairs, pairs 0, 2, 4, ... -> 50; an odd
  // stride rounds up (ceil(100 / 3) = 34). `--sample` thins only this score.
  const CompareOutput two = compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--sample", "2"});
  REQUIRE_FALSE(two.finding.is_null());
  CHECK(candidate_evidence(two).at("pairs_scored") == 50);
  CHECK(candidate_evidence(two).at("sampling_state") == "sampled:2");
  CHECK(two.finding.at("evidence").at("baseline").at("sampling_state") == "sampled:2");

  const CompareOutput three = compare_fixtures("video_hash_base.mp4", "video_perc_degraded.mp4", {"--sample", "3"});
  CHECK(candidate_evidence(three).at("pairs_scored") == 34);

  // The sampled minimum is the minimum over the sampled pairs only.
  const Oracle expected = oracle("video_hash_base.mp4", "video_perc_degraded.mp4");
  std::int64_t sampled_min = expected.scores[0];
  for (std::size_t i = 0; i < expected.scores.size(); i += 2) {
    sampled_min = std::min(sampled_min, expected.scores[i]);
  }
  CHECK(two.finding.at("candidate").at("num") == sampled_min);
}

// --- The serialized fields have a reader --------------------------------------

TEST_CASE("perceptual - the live measurement survives a snapshot round trip", "[integration]") {
  auto pair = mediadiff::fingerprint_pair(fixture("video_hash_base.mp4"), fixture("video_perc_degraded.mp4"),
                                          mediadiff::builtin_registry(), mediadiff::ProbeOptions{});
  REQUIRE(pair.has_value());
  const std::string path = scratch("live.snap.json");
  REQUIRE(mediadiff::write_snapshot(pair->candidate, path, mediadiff::builtin_registry()).has_value());
  auto read = mediadiff::read_snapshot(path, mediadiff::builtin_registry());
  REQUIRE(read.has_value());

  const std::uint32_t index = *mediadiff::builtin_registry().find(kPerceptual);
  const mediadiff::Measurement* stored = nullptr;
  const mediadiff::Measurement* live = nullptr;
  for (const mediadiff::Measurement& m : read->measurements) {
    if (m.check_index == index) {
      stored = &m;
    }
  }
  for (const mediadiff::Measurement& m : pair->candidate.measurements) {
    if (m.check_index == index) {
      live = &m;
    }
  }
  REQUIRE(stored != nullptr);
  REQUIRE(live != nullptr);
  CHECK(stored->value == live->value);
  CHECK(stored->evidence == live->evidence);
  for (const char* key : {"reference_identity", "scaler_path", "decode_path_signature", "mean", "first_below_threshold",
                          "worst", "unpaired_baseline", "unpaired_candidate", "pairs_scored", "pairing"}) {
    INFO(key);
    CHECK(stored->evidence.contains(key));
  }
}
