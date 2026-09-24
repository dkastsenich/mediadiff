// 06-08-PLAN.md Task 2 (AUDIO-05, AUDIO-06): `audio.loudness.integrated` and
// `audio.loudness.true_peak` through the real CLI -- Behavior Tests 1
// through 9 from that task's own <behavior> block, against the
// `audio_loud_*`/`audio_peak_*` fixtures 06-02 built.
//
// DEVIATION FROM THE PLAN'S OWN files_modified/task split (documented fully
// in 06-08-SUMMARY.md): this file is created here, during Task 2, rather
// than deferred entirely to Task 3 (whose own files_modified list names it
// "Created") -- Task 2's own acceptance criteria requires ALL of Tests 1
// through 9 to exist as a named Catch2 assertion by the end of Task 2, and
// Tests 1/2/3/4/6/7/9 need real fixtures and the CLI to prove at all. Task 3
// EXTENDS this same file with the +/-0.1 LU golden-reference assertion
// against tests/golden/AUDIO_EBUR128_REFERENCE.txt (mirrors 06-05's own
// precedent of adding a test file beyond a task's literal files_modified
// list, recorded the same way in that plan's own summary).
//
// Test 5 (the upward ceiling crossing escalates even when the dB delta
// would otherwise land the finding at `warn`, never mind pass, per D-06 gate
// PRECEDENT for "escalation matters independent of the raw delta") and
// Test 8 (the escalation is driven by evidence shape, not check.id) are
// proven at the UNIT level instead, in tests/unit/test_tolerance.cpp,
// alongside the D-10/D-16 overrides this escalation joins -- a real fixture
// pair cannot isolate "the escalation fired regardless of tolerance" from
// "the delta alone would have failed anyway" the way a hand-built
// Measurement pair can. This file's own Test 5/Test 6 below still prove the
// escalation end-to-end through the real CLI on real fixtures, which the
// unit-level tests alone do not cover.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
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

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::string& profile = "sw-encoder") {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", profile, "--json"});
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

// --- Test 1: audio.loudness.integrated emits one measurement per audio
// stream carrying a quantised RationalValue in LU, with the raw double in
// evidence. ---

TEST_CASE("audio_loudness - audio.loudness.integrated reports a quantised LU value with the raw double in evidence",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_loud_ref.flac"), fixture("audio_loud_ref.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.integrated");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("unit").get<std::string>() == "LU");
  // Value is a RationalValue -- num/den present, num expressed in
  // milli-LU (kLoudnessQuantiserDen == 1000).
  REQUIRE(finding->at("baseline").contains("num"));
  REQUIRE(finding->at("baseline").contains("den"));
  REQUIRE(finding->at("baseline").at("den").get<std::int64_t>() == 1000);
  const nlohmann::ordered_json& evidence = finding->at("evidence").at("baseline");
  REQUIRE(evidence.contains("integrated_lufs"));
  REQUIRE(evidence.at("integrated_lufs").is_number());
  REQUIRE(evidence.at("state").get<std::string>() == "measured");
  REQUIRE(evidence.contains("gating_floor_lufs"));

  expect_declared_set(report, {});
}

// --- Test 2: audio_loud_ref.flac vs audio_loud_ref_copy.flac reports pass;
// vs audio_loud_plus3.flac reports a non-pass well outside the tolerance. ---

TEST_CASE("audio_loudness - audio_loud_ref.flac vs its DOC-03 clean pair reports pass on audio.loudness.integrated",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_loud_ref.flac"), fixture("audio_loud_ref_copy.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.integrated");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() == "pass");

  expect_declared_set(report, {});
}

TEST_CASE("audio_loudness - audio_loud_ref.flac vs audio_loud_plus3.flac reports a non-pass well outside tolerance "
          "on audio.loudness.integrated",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_loud_ref.flac"), fixture("audio_loud_plus3.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.integrated");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() == "fail");

  // `volume=3dB` also moves true_peak (the same tone, gained), and FLAC's
  // OWN encoder picks a different subframe/bit-depth representation for the
  // gained signal's own statistics (never a re-mux, a genuine re-encode) --
  // which in turn moves the container's own file/stream/peak bitrate
  // figures. Every member below is a real, caused effect of applying a
  // volume filter and re-encoding, not filtered noise (D-02).
  expect_declared_set(report, {"audio.sample_fmt", "audio.bit_depth", "audio.loudness.integrated",
                                "audio.loudness.true_peak", "size.file", "size.stream_bitrate", "size.peak_bitrate",
                                "size.overhead"});
}

// --- Test 3: a stream below the gating floor reports the `silent` value
// rather than a number, and comparing two silent streams reports pass. ---

TEST_CASE("audio_loudness - a stream below the gating floor reports the silent evidence state, not a number",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_loud_floor.flac"), fixture("audio_loud_floor.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.integrated");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() == "pass");
  REQUIRE(finding->at("evidence").at("baseline").at("state").get<std::string>() == "silent");
  REQUIRE(finding->at("evidence").at("candidate").at("state").get<std::string>() == "silent");

  expect_declared_set(report, {});
}

// --- Test 4: audio.loudness.true_peak reports the maximum over channels in
// dBTP. ---

TEST_CASE("audio_loudness - audio.loudness.true_peak reports a dBTP value with the raw double in evidence",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_loud_ref.flac"), fixture("audio_loud_ref.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.true_peak");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("unit").get<std::string>() == "dB");
  const nlohmann::ordered_json& evidence = finding->at("evidence").at("baseline");
  REQUIRE(evidence.contains("true_peak_dbtp"));
  REQUIRE(evidence.at("true_peak_dbtp").is_number());
  REQUIRE(evidence.contains("ceiling_state"));
  REQUIRE(evidence.contains("ceiling_dbtp"));

  expect_declared_set(report, {});
}

// --- Test 5 (AUDIO-06): comparing audio_peak_under.flac (baseline) against
// audio_peak_over.flac (candidate) reports a FAIL on audio.loudness.true_peak
// -- the upward ceiling crossing escalates. The exact "escalates even when
// the delta alone would be within tolerance" property is proven at the unit
// level (tests/unit/test_tolerance.cpp) since this real fixture pair's own
// ~1.5dB delta already exceeds the declared 0.3dB tolerance on its own. ---

TEST_CASE("audio_loudness - audio_peak_under.flac vs audio_peak_over.flac FAILS on audio.loudness.true_peak (the "
          "upward ceiling crossing)",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_peak_under.flac"), fixture("audio_peak_over.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.true_peak");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() == "fail");
  REQUIRE(finding->at("evidence").at("baseline").at("ceiling_state").get<std::string>() == "under");
  REQUIRE(finding->at("evidence").at("candidate").at("ceiling_state").get<std::string>() == "above");
  REQUIRE(finding->at("message").get<std::string>().find("asymmetric ceiling crossing") != std::string::npos);

  // The two fixtures also differ enough in integrated loudness (-2.8 vs
  // -1.3 LUFS, tests/golden/AUDIO_EBUR128_REFERENCE.txt) to move
  // audio.loudness.integrated too, and a real gain difference naturally
  // moves every decoded sample -- content.audio.sample_hash's own digest
  // chain differs for the same reason (D-02: one cause, several real
  // effects).
  expect_declared_set(report,
                       {"audio.loudness.integrated", "audio.loudness.true_peak", "content.audio.sample_hash"});
}

// --- Test 6: the reverse comparison -- audio_peak_over.flac as baseline
// against audio_peak_under.flac as candidate -- does NOT escalate; headroom
// gained is not a regression. ---

TEST_CASE("audio_loudness - the reverse comparison (audio_peak_over.flac baseline, audio_peak_under.flac "
          "candidate) does not escalate audio.loudness.true_peak",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_peak_over.flac"), fixture("audio_peak_under.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.true_peak");
  REQUIRE(finding != nullptr);
  // The raw ~1.5dB delta alone lands this outside the 0.3dB tolerance
  // (an ordinary warn/fail verdict, doc 05's own severity=warn for this
  // check) -- the property under test is the ABSENCE of the escalation's
  // own message suffix, not the raw status.
  REQUIRE(finding->at("status").get<std::string>() != "pass");
  REQUIRE(finding->at("message").get<std::string>().find("asymmetric ceiling crossing") == std::string::npos);
  REQUIRE(finding->at("evidence").at("baseline").at("ceiling_state").get<std::string>() == "above");
  REQUIRE(finding->at("evidence").at("candidate").at("ceiling_state").get<std::string>() == "under");

  // Same causal set as Test 5 above (D-02) -- the reversed baseline/
  // candidate order doesn't change WHICH ids move, only their status text.
  expect_declared_set(report,
                       {"audio.loudness.integrated", "audio.loudness.true_peak", "content.audio.sample_hash"});
}

// --- Test 9: under --profile transform the loudness tolerance widens to the
// profile value while the ceiling escalation still applies. ---

TEST_CASE("audio_loudness - --profile transform widens audio.loudness.integrated's tolerance to the profile value",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_loud_ref.flac"), fixture("audio_loud_plus3.flac"), "transform");
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.integrated");
  REQUIRE(finding != nullptr);
  // 06-CHECK-ROSTER.md's own transform profile_tolerance: "1.0LU" (flat,
  // single-threshold) rather than the default "0.5LU,1.0LU" two-threshold
  // form -- distinguishable via the absence of warn_num.
  const std::int64_t tolerance_num = finding->at("tolerance").at("num").get<std::int64_t>();
  const std::int64_t tolerance_den = finding->at("tolerance").at("den").get<std::int64_t>();
  REQUIRE(tolerance_num == tolerance_den);  // exactly 1.0LU, whatever the unreduced num/den pair
  REQUIRE(finding->at("tolerance").at("warn_num").is_null());
}

TEST_CASE("audio_loudness - --profile transform still escalates the upward ceiling crossing on "
          "audio.loudness.true_peak",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_peak_under.flac"), fixture("audio_peak_over.flac"), "transform");
  const nlohmann::ordered_json* finding = find_finding(report, "audio.loudness.true_peak");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() == "fail");
  REQUIRE(finding->at("message").get<std::string>().find("asymmetric ceiling crossing") != std::string::npos);
}

// =============================================================================
// 06-08-PLAN.md Task 3 (AUDIO-05): the +/-0.1 LU reference assertion against
// tests/golden/AUDIO_EBUR128_REFERENCE.txt (06-02's own committed
// `ffmpeg -af ebur128` measurement) -- every FLAC/PCM fixture that file
// names, portable to every CI leg (tests/golden/README.md's own "why every
// measured fixture here is FLAC" section: lossless carriers are
// byte-identical across SIMD dispatch levels, so this assertion is never
// confined to the designated leg the way CORPUS_DIGEST.txt's own five
// per-fixture hashes are).
// =============================================================================

namespace {

// One parsed reference line: `<fixture-name> integrated_lufs=<I> `
// `true_peak_dbtp=<Peak>` -- comment lines (leading '#') and blank lines are
// skipped. Free of any Catch2 dependency (mirrors tests/support/golden.h's
// own golden_check_result()/check_golden() split) so the parser itself, and
// the pure tolerance check below, are both directly unit-testable without
// triggering a real assertion failure on purpose.
struct ReferenceLine {
  std::string fixture_name;
  double integrated_lufs = 0.0;
  double true_peak_dbtp = 0.0;
};

std::vector<ReferenceLine> parse_reference_file(const std::string& path) {
  std::ifstream stream(path);
  REQUIRE(stream.is_open());
  std::vector<ReferenceLine> lines;
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    if (raw_line.empty() || raw_line[0] == '#') {
      continue;
    }
    std::istringstream tokens(raw_line);
    std::string name;
    std::string integrated_token;
    std::string true_peak_token;
    tokens >> name >> integrated_token >> true_peak_token;
    INFO("malformed AUDIO_EBUR128_REFERENCE.txt line: " << raw_line);
    REQUIRE_FALSE(name.empty());
    const std::string kIntegratedPrefix = "integrated_lufs=";
    const std::string kTruePeakPrefix = "true_peak_dbtp=";
    REQUIRE(integrated_token.rfind(kIntegratedPrefix, 0) == 0);
    REQUIRE(true_peak_token.rfind(kTruePeakPrefix, 0) == 0);
    ReferenceLine line;
    line.fixture_name = name;
    line.integrated_lufs = std::stod(integrated_token.substr(kIntegratedPrefix.size()));
    line.true_peak_dbtp = std::stod(true_peak_token.substr(kTruePeakPrefix.size()));
    lines.push_back(line);
  }
  return lines;
}

// The pure tolerance check test_size_checks.cpp's own numeric-golden
// convention calls for: a bare boolean failure on a numeric tolerance is a
// debugging dead end, so this returns a diagnostic message naming BOTH
// numbers and the fixture name whenever the two are more than `tolerance_lu`
// apart, and std::nullopt when they are within it. Free of any Catch2
// dependency so Test 2 below (the deliberately perturbed reference) can
// call this directly and inspect the returned message without triggering a
// real test failure.
std::optional<std::string> check_within_tolerance(const std::string& fixture_name, const std::string& metric_name,
                                                    double reference_value, double measured_value,
                                                    double tolerance_lu) {
  const double diff = std::fabs(reference_value - measured_value);
  if (diff <= tolerance_lu) {
    return std::nullopt;
  }
  return fmt::format(
      "{}: {} reference={:.3f} measured={:.3f} diff={:.3f} exceeds the +/-{} LU tolerance", fixture_name,
      metric_name, reference_value, measured_value, diff, tolerance_lu);
}

std::string reference_path() { return mediadiff::test::golden_dir() + "/AUDIO_EBUR128_REFERENCE.txt"; }

}  // namespace

// --- Every fixture line in AUDIO_EBUR128_REFERENCE.txt has a matching
// assertion, within +/-0.1 LU on both integrated loudness and true peak. ---

TEST_CASE("audio_loudness - every AUDIO_EBUR128_REFERENCE.txt fixture matches mediadiff's own measurement within "
          "+/-0.1 LU",
          "[integration]") {
  constexpr double kToleranceLu = 0.1;
  const std::vector<ReferenceLine> reference_lines = parse_reference_file(reference_path());
  REQUIRE(reference_lines.size() == 11);  // every fixture 06-02 measured -- a silent drop would hide coverage

  for (const ReferenceLine& line : reference_lines) {
    require_fixture(fixture(line.fixture_name));
    const nlohmann::ordered_json report = compare_json(fixture(line.fixture_name), fixture(line.fixture_name));

    const nlohmann::ordered_json* integrated = find_finding(report, "audio.loudness.integrated");
    REQUIRE(integrated != nullptr);
    const nlohmann::ordered_json* true_peak = find_finding(report, "audio.loudness.true_peak");
    REQUIRE(true_peak != nullptr);

    const double measured_integrated = integrated->at("evidence").at("baseline").at("integrated_lufs").get<double>();
    const double measured_true_peak = true_peak->at("evidence").at("baseline").at("true_peak_dbtp").get<double>();

    const std::optional<std::string> integrated_diag = check_within_tolerance(
        line.fixture_name, "integrated_lufs", line.integrated_lufs, measured_integrated, kToleranceLu);
    INFO(integrated_diag.value_or(""));
    CHECK_FALSE(integrated_diag.has_value());

    const std::optional<std::string> true_peak_diag =
        check_within_tolerance(line.fixture_name, "true_peak_dbtp", line.true_peak_dbtp, measured_true_peak,
                                kToleranceLu);
    INFO(true_peak_diag.value_or(""));
    CHECK_FALSE(true_peak_diag.has_value());
  }
}

// --- A deliberately perturbed reference line makes the assertion fail with
// both numbers and the fixture name printed -- proven against the pure
// checker directly (never by actually corrupting the committed golden). ---

TEST_CASE("audio_loudness - check_within_tolerance's own diagnostic names both numbers and the fixture on a "
          "deliberately perturbed reference value",
          "[integration]") {
  const std::optional<std::string> diag =
      check_within_tolerance("audio_loud_ref.flac", "integrated_lufs", /*reference_value=*/-21.8,
                              /*measured_value=*/-21.8 + 5.0, /*tolerance_lu=*/0.1);
  REQUIRE(diag.has_value());
  CHECK(diag->find("audio_loud_ref.flac") != std::string::npos);
  CHECK(diag->find("-21.8") != std::string::npos);
  CHECK(diag->find("-16.8") != std::string::npos);

  // Within tolerance: no diagnostic at all.
  const std::optional<std::string> clean =
      check_within_tolerance("audio_loud_ref.flac", "integrated_lufs", -21.8, -21.75, 0.1);
  CHECK_FALSE(clean.has_value());
}
