// 06-06-PLAN.md Task 2 (AUDIO-04, D-14/D-15/D-17): `audio.priming` --
// `unknown` as a comparable value, the `{state, source, samples, padding}`
// evidence object, and the mechanism-versus-effect layering across an
// MP4->MKV hop -- proven through the real CLI against the six
// `audio_prime_*` fixtures 06-02 built. Every value below is transcribed
// from a real `mediadiff compare --json` run against the linked FFmpeg 8.1
// this project builds against (never the system ffprobe, which this
// project's own research already flags as a materially different, newer
// build -- see 06-06-SUMMARY.md's own Deviations section for the concrete
// discovery this plan's own execution made of that exact trap).
//
// DEVIATION FROM THE PLAN'S OWN LITERAL TEXT (documented fully in
// 06-06-SUMMARY.md): Task 2's <behavior> Test 4 and one acceptance
// criterion both name `audio_prime_base.mp4` vs `audio_prime_roundtrip2.mp4`
// (the MP4->MKV->MP4 round trip) as the pair that must report `pass`.
// Measured directly against this project's own linked FFmpeg 8.1, that pair
// reports "1024" vs "1014" -- a genuine ~10-sample rounding artifact of the
// MKV `CodecDelay` intermediate's own nanosecond-granularity round trip,
// exactly the risk 06-02-SUMMARY.md's own "Next Phase Readiness" note
// flagged in advance ("06-06 should treat this as data to measure a
// tolerance against, not assume away"). `audio.priming` is registered
// `semantic=exact` over a `string` value (D-14 forces this shape -- no
// numeric tolerance is expressible over "unknown"), so there is no way to
// make that specific pair report `pass` without either regenerating the
// fixture (forbidden: `tests/golden/CORPUS_DIGEST.txt` never rewrites an
// existing line) or adding a tolerance mechanism D-14 explicitly rules out.
// Test 4 below asserts the REAL, measured "fail" result on
// `audio_prime_roundtrip2.mp4` instead of the plan's literal "pass", and
// Test 5 (`audio_prime_roundtrip.mkv`, the single MP4->MKV hop the plan's
// own Test 5 already names) is what actually proves AUDIO-04's
// round-trip-stability must_have empirically.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
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

// --- Test 1: audio_prime_base.mp4 reports a real, known value with evidence
// naming its source ---------------------------------------------------------

TEST_CASE("audio_priming - audio_prime_base.mp4 reports the decimal sample count with evidence naming source "
          "skip_samples",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_prime_base.mp4"), fixture("audio_prime_base.mp4"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("baseline").get<std::string>() == "1024");
  const nlohmann::ordered_json& evidence = priming->at("evidence").at("baseline");
  REQUIRE(evidence.at("state").get<std::string>() == "known");
  REQUIRE(evidence.at("source").get<std::string>() == "skip_samples");
  REQUIRE(evidence.at("samples").get<std::int64_t>() == 1024);
}

// --- Test 2: audio_prime_copy.ts reports the literal "unknown" as its
// VALUE -- never Absent{}, never a skip -------------------------------------

TEST_CASE("audio_priming - audio_prime_copy.ts reports the literal \"unknown\" as its value, state unknown in "
          "evidence",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_prime_copy.ts"), fixture("audio_prime_copy.ts"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("status").get<std::string>() != "skipped");
  REQUIRE(priming->at("baseline").get<std::string>() == "unknown");
  const nlohmann::ordered_json& evidence = priming->at("evidence").at("baseline");
  REQUIRE(evidence.at("state").get<std::string>() == "unknown");
}

// --- Test 3: audio_prime_base.mp4 vs audio_prime_copy.ts reports a
// NON-PASS finding -- the TS copy genuinely plays the samples the MP4
// trims (D-14) --------------------------------------------------------------

TEST_CASE("audio_priming - audio_prime_base.mp4 vs audio_prime_copy.ts reports a non-pass finding, never "
          "skipped:insufficient_data",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_prime_base.mp4"), fixture("audio_prime_copy.ts"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("status").get<std::string>() == "fail");
  REQUIRE(priming->at("baseline").get<std::string>() == "1024");
  REQUIRE(priming->at("candidate").get<std::string>() == "unknown");
}

// --- Test 4: the plan's own literal acceptance criterion names
// audio_prime_base.mp4 vs audio_prime_roundtrip2.mp4 as a must-pass round
// trip. Measured against the real linked FFmpeg 8.1 it is NOT: this test
// asserts the real, observed "fail" (see this file's own top-of-file
// Deviations note and 06-06-SUMMARY.md for the full accounting) -----------

TEST_CASE("audio_priming - DEVIATION: audio_prime_base.mp4 vs audio_prime_roundtrip2.mp4 (the MP4->MKV->MP4 round "
          "trip) reports a genuine, measured non-pass (\"1024\" vs \"1014\"), not the plan's literal \"pass\" -- a "
          "real ~10-sample CodecDelay-ns-rounding artifact 06-02-SUMMARY.md already flagged as a risk to measure "
          "rather than assume away",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_prime_base.mp4"), fixture("audio_prime_roundtrip2.mp4"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("baseline").get<std::string>() == "1024");
  // Real, measured value -- NOT "1024". If this ever starts reporting
  // "1024" again (e.g. a future FFmpeg release rounds CodecDelay
  // differently), that is a genuine improvement: update this assertion and
  // 06-06-SUMMARY.md's Deviations section together, never one without the
  // other.
  REQUIRE(priming->at("candidate").get<std::string>() == "1014");
  REQUIRE(priming->at("status").get<std::string>() == "fail");
}

// --- Test 5: audio_prime_base.mp4 vs audio_prime_roundtrip.mkv (the single
// MP4->MKV hop) -- THIS is AUDIO-04's actual round-trip-stability proof:
// same resolved value, different MECHANISM (container_reading.source)
// underneath -----------------------------------------------------------------

TEST_CASE("audio_priming - audio_prime_base.mp4 vs audio_prime_roundtrip.mkv reports pass with the SAME resolved "
          "value while container_reading.source differs (mechanism-versus-effect layering, AUDIO-04)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_prime_base.mp4"), fixture("audio_prime_roundtrip.mkv"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("status").get<std::string>() == "pass");
  REQUIRE(priming->at("baseline").get<std::string>() == "1024");
  REQUIRE(priming->at("candidate").get<std::string>() == "1024");

  const nlohmann::ordered_json& evidence = priming->at("evidence");
  // The top-level resolved `source` is skip_samples on BOTH sides here --
  // libavformat's own matroska demuxer carries AV_PKT_DATA_SKIP_SAMPLES
  // forward on the remuxed packets themselves, so tier 1 resolves directly
  // without ever falling through to the container tier. The mechanism
  // difference AUDIO-04 asks to be visible lives in the SUB-field
  // `container_reading.source` instead -- mp4_edit_list on the MP4 side,
  // mkv_codec_delay on the MKV side -- which is what this test asserts.
  REQUIRE(evidence.at("baseline").at("container_reading").at("source").get<std::string>() == "mp4_edit_list");
  REQUIRE(evidence.at("candidate").at("container_reading").at("source").get<std::string>() == "mkv_codec_delay");
}

// --- Test 6: a stream declaring zero priming and a stream declaring none
// compare as non-pass -- zero and unknown are different values (boundary
// edge, D-14) -----------------------------------------------------------

TEST_CASE("audio_priming - a declared-zero stream (audio_prime_multiedit.mp4) vs an unknown one "
          "(audio_prime_copy.ts) reports a non-pass finding -- zero and unknown never compare equal",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_prime_multiedit.mp4"), fixture("audio_prime_copy.ts"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("status").get<std::string>() == "fail");
  REQUIRE(priming->at("baseline").get<std::string>() == "0");
  REQUIRE(priming->at("candidate").get<std::string>() == "unknown");
  REQUIRE(priming->at("evidence").at("baseline").at("state").get<std::string>() == "known");
  REQUIRE(priming->at("evidence").at("candidate").at("state").get<std::string>() == "unknown");
}

// --- Test 7: the evidence object carries all four of state/source/samples/
// padding, with padding present (null) even when priming is unknown
// (D-17) --------------------------------------------------------------------

TEST_CASE("audio_priming - evidence always carries state, source, samples and padding, padding null (not "
          "omitted) when unknown",
          "[integration]") {
  const nlohmann::ordered_json known_report =
      compare_json(fixture("audio_prime_base.mp4"), fixture("audio_prime_base.mp4"));
  const nlohmann::ordered_json* known_priming = find_finding(known_report, "audio.priming");
  REQUIRE(known_priming != nullptr);
  const nlohmann::ordered_json& known_evidence = known_priming->at("evidence").at("baseline");
  REQUIRE(known_evidence.contains("state"));
  REQUIRE(known_evidence.contains("source"));
  REQUIRE(known_evidence.contains("samples"));
  REQUIRE(known_evidence.contains("padding"));
  REQUIRE(known_evidence.at("padding").get<std::int64_t>() == 0);

  const nlohmann::ordered_json unknown_report =
      compare_json(fixture("audio_prime_copy.ts"), fixture("audio_prime_copy.ts"));
  const nlohmann::ordered_json* unknown_priming = find_finding(unknown_report, "audio.priming");
  REQUIRE(unknown_priming != nullptr);
  const nlohmann::ordered_json& unknown_evidence = unknown_priming->at("evidence").at("baseline");
  REQUIRE(unknown_evidence.contains("padding"));
  REQUIRE(unknown_evidence.at("padding").is_null());
}

// --- Test 8: an unknown priming never softens the resolved severity -- the
// finding gates at the registered severity regardless of state (Phase 5
// D-11) -----------------------------------------------------------------

TEST_CASE("audio_priming - a known-vs-unknown non-pass finding gates at the registered severity, never softened",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_prime_base.mp4"), fixture("audio_prime_copy.ts"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("severity").get<std::string>() == "fail");
  REQUIRE(priming->at("gating").get<bool>() == true);
}

// --- Test 9 (first half): a file with no audio stream emits
// skipped:insufficient_data. (The second half -- a truncated scan emits
// skipped:partial_scan ahead of it -- has no practical way to be forced via
// the real CLI against any fixture small enough for this corpus; a 1MB
// `--probe-memory-budget-mb` is already generous enough not to truncate
// audio_prime_base.mp4. That half is covered directly against
// audio_priming_analyzer()'s own run() in tests/unit/test_audio_priming.cpp
// -- a Rule 2 addition beyond this plan's declared files_modified, mirroring
// tests/unit/test_audio_stream_params.cpp's own established Test 8 pattern.
// See 06-06-SUMMARY.md's own Deviations section.) --------------------------

TEST_CASE("audio_priming - a file with no audio stream emits skipped:insufficient_data, never a value",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("tracer_empty.mp4"), fixture("tracer_empty.mp4"));
  const nlohmann::ordered_json* priming = find_finding(report, "audio.priming");
  REQUIRE(priming != nullptr);
  REQUIRE(priming->at("status").get<std::string>() == "skipped");
  REQUIRE(priming->at("skip_reason").get<std::string>() == "insufficient_data");
  REQUIRE(priming->at("baseline").is_null());
}
