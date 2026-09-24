// 06-03-PLAN.md Task 2 (AUDIO-01, AUDIO-02): audio.codec/sample_rate/
// sample_fmt/bit_depth/channels/layout, proven through the real CLI --
// `5.1` versus `5.1(side)` on an equal channel count (the headline war
// story), unspecified-layout-as-a-real-value in both directions, and the
// whole-report DOC-04 no-others counter (05-01-PLAN.md Task 3's shared
// harness, tests/integration/timeline_findings.h). Mirrors
// tests/integration/test_video_yuvj.cpp's own established shape (Test 1's
// exactly-one-non-pass-finding assertion, `compare_json`/`find_finding`
// helpers).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "timeline_findings.h"

using mediadiff::test::CliResult;
using mediadiff::test::count_non_pass;
using mediadiff::test::expect_declared_set;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate) {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", "sw-encoder", "--json"});
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

// --- Test 1/2: audio_51.flac vs audio_51_side.flac -- same audio.channels,
// DIFFERENT audio.layout, in the SAME report ------------------------------

TEST_CASE("audio_stream_params - audio_51.flac vs audio_51_side.flac report the SAME audio.channels and a "
          "DIFFERENT audio.layout",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_51.flac"), fixture("audio_51_side.flac"));

  const nlohmann::ordered_json* channels = find_finding(report, "audio.channels");
  REQUIRE(channels != nullptr);
  REQUIRE(channels->at("status").get<std::string>() == "pass");
  REQUIRE(channels->at("baseline") == channels->at("candidate"));

  const nlohmann::ordered_json* layout = find_finding(report, "audio.layout");
  REQUIRE(layout != nullptr);
  REQUIRE(layout->at("status").get<std::string>() != "pass");
  REQUIRE(layout->at("baseline").get<std::string>() == "5.1");
  REQUIRE(layout->at("candidate").get<std::string>() == "5.1(side)");
}

// --- Test 6: the whole-report non-pass count equals the declared finding
// set exactly -- audio.layout is the ONLY non-pass finding this pair
// produces (D-01/D-02 of Phase 5, applied to the audio family) -----------

TEST_CASE("audio_stream_params - audio_51.flac vs audio_51_side.flac's whole-report non-pass count equals "
          "its declared finding set exactly",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_51.flac"), fixture("audio_51_side.flac"));
  // The layout regression is the entire, single cause: no other check
  // legitimately moves on this pair (both sides are FLAC-encoded from the
  // same source signal, differing only in the surround-channel mapping),
  // so the declared set is exactly one member and count_non_pass must
  // equal it with no extras.
  expect_declared_set(report, {"audio.layout"});
}

// --- Test 3: an unspecified layout compares as its own value against a
// real one -- non-pass in BOTH directions ---------------------------------

TEST_CASE("audio_stream_params - an unspecified layout (audio_stereo_s16.wav) versus a real one "
          "(audio_stereo_s24.wav) reports non-pass audio.layout in both directions",
          "[integration]") {
  const nlohmann::ordered_json forward =
      compare_json(fixture("audio_stereo_s16.wav"), fixture("audio_stereo_s24.wav"));
  const nlohmann::ordered_json* forward_layout = find_finding(forward, "audio.layout");
  REQUIRE(forward_layout != nullptr);
  REQUIRE(forward_layout->at("status").get<std::string>() != "pass");
  REQUIRE(forward_layout->at("baseline").get<std::string>() == "2 channels");
  REQUIRE(forward_layout->at("candidate").get<std::string>() == "stereo");

  const nlohmann::ordered_json reverse =
      compare_json(fixture("audio_stereo_s24.wav"), fixture("audio_stereo_s16.wav"));
  const nlohmann::ordered_json* reverse_layout = find_finding(reverse, "audio.layout");
  REQUIRE(reverse_layout != nullptr);
  REQUIRE(reverse_layout->at("status").get<std::string>() != "pass");
  REQUIRE(reverse_layout->at("baseline").get<std::string>() == "stereo");
  REQUIRE(reverse_layout->at("candidate").get<std::string>() == "2 channels");
}

// --- Test 4: two streams both carrying an unspecified layout compare pass
// -- an unspecified value matches only itself ------------------------------

TEST_CASE("audio_stream_params - two unspecified-layout streams (audio_stereo_s16.wav, audio_pcm_base.wav) "
          "compare pass on audio.layout",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_stereo_s16.wav"), fixture("audio_pcm_base.wav"));
  const nlohmann::ordered_json* layout = find_finding(report, "audio.layout");
  REQUIRE(layout != nullptr);
  REQUIRE(layout->at("status").get<std::string>() == "pass");
  REQUIRE(layout->at("baseline").get<std::string>() == "2 channels");
  REQUIRE(layout->at("candidate").get<std::string>() == "2 channels");
}

// --- Test 5: the layout string is produced by DESCRIBING the channel
// layout, so a mono file reports the canonical mono spelling rather than a
// count-derived guess ("1 channels" would be the count-derived guess) -----

TEST_CASE("audio_stream_params - audio_aac_handwritten.mp4 (mono) reports the canonical 'mono' spelling, not "
          "a count-derived '1 channels' guess",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_aac_handwritten.mp4"), fixture("audio_aac_handwritten.mp4"));
  const nlohmann::ordered_json* layout = find_finding(report, "audio.layout");
  REQUIRE(layout != nullptr);
  REQUIRE(layout->at("status").get<std::string>() == "pass");
  REQUIRE(layout->at("baseline").get<std::string>() == "mono");
}
