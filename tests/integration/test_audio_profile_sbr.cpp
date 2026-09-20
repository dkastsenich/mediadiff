// 06-04-PLAN.md Task 2 (AUDIO-03, D-12): `audio.profile` carrying the
// HE-AAC SBR signaling mode, proven through the real CLI against 06-02's
// hand-written explicit/implicit HE-AAC fixture pair and the plain
// non-SBR AAC-LC proof input -- the explicit/implicit/no-SBR three-bucket
// distinction, pass-independence (the whole point of D-12), and
// `audio.sample_rate`'s own evidence carrying the doubled effective rate
// without moving its compared value. Mirrors
// tests/integration/test_audio_stream_params.cpp's own established shape.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "timeline_findings.h"

using mediadiff::test::CliResult;
using mediadiff::test::expect_declared_set;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// Per-file-local scratch dir (mirrors test_audio_sample_hash.cpp's own
// established convention -- no shared scratch_dir() exists in
// cli_harness.h).
fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_audio_profile_sbr";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::vector<std::string>& extra_args = {}) {
  require_fixture(baseline);
  require_fixture(candidate);
  std::vector<std::string> args = {"compare", baseline, candidate, "--profile", "sw-encoder", "--json"};
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  const CliResult result = run_cli(args);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  return report;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1: the explicit/implicit fixtures name their own signaling mode
// in audio.profile's value -------------------------------------------------

TEST_CASE("audio_profile_sbr - audio_sbr_explicit.mp4 names 'sbr: explicit', audio_sbr_implicit.mp4 names 'sbr: "
          "implicit'",
          "[integration]") {
  const nlohmann::ordered_json explicit_report =
      compare_json(fixture("audio_sbr_explicit.mp4"), fixture("audio_sbr_explicit_copy.mp4"));
  const nlohmann::ordered_json* explicit_profile = find_finding(explicit_report, "audio.profile");
  REQUIRE(explicit_profile != nullptr);
  REQUIRE(explicit_profile->at("baseline").get<std::string>().find("sbr: explicit") != std::string::npos);

  const nlohmann::ordered_json implicit_report =
      compare_json(fixture("audio_sbr_implicit.mp4"), fixture("audio_sbr_implicit.mp4"));
  const nlohmann::ordered_json* implicit_profile = find_finding(implicit_report, "audio.profile");
  REQUIRE(implicit_profile != nullptr);
  REQUIRE(implicit_profile->at("baseline").get<std::string>().find("sbr: implicit") != std::string::npos);
}

// --- Test 2: explicit vs implicit is a real, non-pass audio.profile
// finding, even though the audible result is the same ----------------------

TEST_CASE("audio_profile_sbr - audio_sbr_explicit.mp4 vs audio_sbr_implicit.mp4 reports a non-pass audio.profile",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_sbr_explicit.mp4"), fixture("audio_sbr_implicit.mp4"));
  const nlohmann::ordered_json* profile = find_finding(report, "audio.profile");
  REQUIRE(profile != nullptr);
  REQUIRE(profile->at("status").get<std::string>() != "pass");
}

// --- Test 3: a plain AAC-LC fixture with no SBR at all names NEITHER
// implicit nor explicit -- the third bucket is visible, not collapsed -----

TEST_CASE("audio_profile_sbr - audio_aac_handwritten.mp4 (plain AAC-LC, no SBR) names neither 'implicit' nor "
          "'explicit'",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_aac_handwritten.mp4"), fixture("audio_aac_handwritten_copy.mp4"));
  const nlohmann::ordered_json* profile = find_finding(report, "audio.profile");
  REQUIRE(profile != nullptr);
  const std::string value = profile->at("baseline").get<std::string>();
  REQUIRE(value.find("implicit") == std::string::npos);
  REQUIRE(value.find("explicit") == std::string::npos);
}

// --- Test 6: audio.profile always reports a real, non-empty comparable
// string value (never Absent{}) -- empirically, avformat_find_stream_info()'s
// own internal probing already resolves codecpar->profile (e.g. to
// AV_PROFILE_AAC_LC) for MP4-demuxed AAC in this corpus, so
// avcodec_profile_name() usually returns a named string ("LC", "HE-AAC")
// rather than falling back to render_named_value()'s raw-integer spelling;
// either way the rendered value must be a real, non-empty string ---------

TEST_CASE("audio_profile_sbr - audio.profile reports a real, non-empty comparable string value, never Absent{}",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_aac_handwritten.mp4"), fixture("audio_aac_handwritten_copy.mp4"));
  const nlohmann::ordered_json* profile = find_finding(report, "audio.profile");
  REQUIRE(profile != nullptr);
  REQUIRE(profile->at("baseline").is_string());
  REQUIRE_FALSE(profile->at("baseline").get<std::string>().empty());
}

// --- Test 4: the SAME fixture reports the SAME audio.profile value under
// `compare --content` and `compare --no-content` -- pass-independence,
// D-12's whole point --------------------------------------------------------

TEST_CASE("audio_profile_sbr - audio_sbr_implicit.mp4 reports the SAME audio.profile value under --content and "
          "--no-content",
          "[integration]") {
  const nlohmann::ordered_json with_content =
      compare_json(fixture("audio_sbr_implicit.mp4"), fixture("audio_sbr_implicit.mp4"), {"--content"});
  const nlohmann::ordered_json without_content =
      compare_json(fixture("audio_sbr_implicit.mp4"), fixture("audio_sbr_implicit.mp4"), {"--no-content"});

  const nlohmann::ordered_json* profile_with = find_finding(with_content, "audio.profile");
  const nlohmann::ordered_json* profile_without = find_finding(without_content, "audio.profile");
  REQUIRE(profile_with != nullptr);
  REQUIRE(profile_without != nullptr);
  REQUIRE(profile_with->at("baseline") == profile_without->at("baseline"));
  REQUIRE(profile_with->at("baseline").get<std::string>().find("sbr: implicit") != std::string::npos);
}

TEST_CASE("audio_profile_sbr - a snapshot of audio_sbr_implicit.mp4 reports the SAME audio.profile value as a "
          "live compare",
          "[integration]") {
  require_fixture(fixture("audio_sbr_implicit.mp4"));
  const std::string snap_path = (scratch_dir() / "audio_profile_sbr_implicit.snap.json").string();
  const CliResult snap_result = run_cli({"snapshot", fixture("audio_sbr_implicit.mp4"), "--out", snap_path});
  INFO("snapshot stdout: " << snap_result.out << "\nsnapshot stderr: " << snap_result.err);
  REQUIRE(snap_result.exit_code == 0);

  const nlohmann::ordered_json live = compare_json(fixture("audio_sbr_implicit.mp4"), fixture("audio_sbr_implicit.mp4"));
  const nlohmann::ordered_json from_snapshot = compare_json(snap_path, fixture("audio_sbr_implicit.mp4"));

  const nlohmann::ordered_json* live_profile = find_finding(live, "audio.profile");
  const nlohmann::ordered_json* snap_profile = find_finding(from_snapshot, "audio.profile");
  REQUIRE(live_profile != nullptr);
  REQUIRE(snap_profile != nullptr);
  REQUIRE(live_profile->at("baseline") == snap_profile->at("baseline"));
}

// --- Test 5: audio.sample_rate's evidence carries core_rate_hz and
// effective_rate_hz for the implicit fixture -- 06-04-PLAN.md deviation
// (Rule 1): empirically, avformat_find_stream_info()'s own internal
// probing already resolves codecpar->sample_rate to the SBR-doubled value
// for this fixture (88200, matching the bounded probe's own decoded rate)
// entirely within the header pass, reproducible identically with and
// without --content -- so core_rate_hz and effective_rate_hz AGREE here
// rather than differing by a formulaic factor of two (a literal doubling
// would fabricate a false, quadrupled rate; see
// DemuxSession::implicit_probe_rate_hz_'s own doc comment). The compared
// value itself is always the core rate, doubled by the demuxer or not ---

TEST_CASE("audio_profile_sbr - audio.sample_rate's evidence on audio_sbr_implicit.mp4 carries the probe's own "
          "decoded rate as effective_rate_hz, matching the compared core rate",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_hash_base.mp4"), fixture("audio_sbr_implicit.mp4"));
  const nlohmann::ordered_json* rate = find_finding(report, "audio.sample_rate");
  REQUIRE(rate != nullptr);
  REQUIRE(rate->contains("evidence"));
  const nlohmann::ordered_json& evidence = rate->at("evidence");
  REQUIRE(evidence.contains("candidate"));
  const std::int64_t core = evidence.at("candidate").at("core_rate_hz").get<std::int64_t>();
  const std::int64_t effective = evidence.at("candidate").at("effective_rate_hz").get<std::int64_t>();
  REQUIRE(core == 88200);
  REQUIRE(effective == core);
  // The COMPARED value itself is the core rate.
  REQUIRE(rate->at("candidate").get<std::int64_t>() == core);
}

// --- Test 7: the whole-report non-pass count equals the declared finding
// set exactly for the explicit-vs-implicit pair -- 06-04-PLAN.md deviation
// (Rule 1): the declared set is NOT audio.profile alone. audio.sample_rate
// ALSO fires here (44100 vs 88200) -- these two fixtures were built with
// different ASC-declared base rates (06-02's gen_he_aac.py), and per the
// empirical finding above, codecpar->sample_rate for the implicit fixture
// is already resolved to its SBR-doubled value at the header pass, which
// is genuinely different from the explicit fixture's own core rate. Both
// causally follow from the same encode-time SBR-signaling difference, so
// this is one cause moving two declared facts (D-02) -----------------------

TEST_CASE("audio_profile_sbr - audio_sbr_explicit.mp4 vs audio_sbr_implicit.mp4's whole-report non-pass count "
          "equals its declared finding set exactly",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_sbr_explicit.mp4"), fixture("audio_sbr_implicit.mp4"));
  expect_declared_set(report, {"audio.profile", "audio.sample_rate"});
}
