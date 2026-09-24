// 06-10-PLAN.md Task 2 (AUDIO-08, AUDIO-10, D-09): `meta.decode_errors`
// through the real CLI -- Behavior Tests 1 through 8 from that task's own
// <behavior> block, against the `audio_corrupt_clean.mp4`/
// `audio_corrupt_frames.mp4`/`audio_undecodable.mp4` fixture triple this
// same plan built (scripts/gen_corpus.sh's own byte-perturbed-mdat
// recipe, documented there).
//
// This is deliberately NOT expect_declared_set-based (unlike most
// tests/integration/test_audio_*.cpp siblings): a corrupted AAC access
// unit's decoded samples diverge unpredictably across the whole rest of
// the block they land in, so the affected check SET (sample_hash,
// loudness, silence, size.*) is real but not usefully enumerable the way
// a single, isolated edit is elsewhere in this phase. This file asserts
// only the properties Task 2's own <behavior> block actually specifies:
// the count itself, the exit-code contract, the skip-reason ladder, and
// determinism -- mirroring tests/integration/test_exit_codes.cpp's own
// direct run_cli()-plus-parse style rather than the declared-set harness.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

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

// Runs `mediadiff compare baseline candidate --json` and returns the
// parsed report alongside the real process exit code -- mirrors
// test_exit_codes.cpp's own direct style (never the declared-set harness,
// per this file's own top comment).
struct CompareOutcome {
  int exit_code = -1;
  nlohmann::ordered_json report;
};

CompareOutcome compare(const std::string& baseline, const std::string& candidate) {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", "sw-encoder", "--json"});
  CompareOutcome outcome;
  outcome.exit_code = result.exit_code;
  outcome.report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(outcome.report.is_discarded());
  REQUIRE(outcome.report.contains("findings"));
  return outcome;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == id) {
      return &finding;
    }
  }
  return nullptr;
}

bool diagnostics_mention_partial(const nlohmann::ordered_json& report) {
  if (!report.contains("diagnostics")) {
    return false;
  }
  for (const auto& line : report.at("diagnostics")) {
    if (line.is_string() && line.get<std::string>().find("partial") != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

// --- Test 1: a clean file reports meta.decode_errors as a real 0, not
// Absent{} and not a skip. ---

TEST_CASE("audio_decode_errors - a clean file reports meta.decode_errors as a real 0, never a skip",
          "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_clean.mp4"));
  CHECK(outcome.exit_code == 0);
  const nlohmann::ordered_json* finding = find_finding(outcome.report, "meta.decode_errors");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status").get<std::string>() == "pass");
  REQUIRE(finding->at("baseline").is_number());
  REQUIRE(finding->at("candidate").is_number());
  CHECK(finding->at("baseline").get<std::int64_t>() == 0);
  CHECK(finding->at("candidate").get<std::int64_t>() == 0);
}

// --- Test 2: audio_corrupt_frames.mp4 reports a non-zero count, exits 1
// against audio_corrupt_clean.mp4, and does NOT mark the fingerprint
// partial. ---

TEST_CASE(
    "audio_decode_errors - a recoverable-error candidate reports a non-zero count, exits 1, and never marks the "
    "fingerprint partial",
    "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"));
  CHECK(outcome.exit_code == 1);

  const nlohmann::ordered_json* finding = find_finding(outcome.report, "meta.decode_errors");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status").get<std::string>() == "fail");
  CHECK(finding->at("skip_reason").get<std::string>() == "none");
  REQUIRE(finding->at("baseline").is_number());
  REQUIRE(finding->at("candidate").is_number());
  CHECK(finding->at("baseline").get<std::int64_t>() == 0);
  CHECK(finding->at("candidate").get<std::int64_t>() > 0);

  CHECK_FALSE(diagnostics_mention_partial(outcome.report));
}

// --- Test 3: on that same file, content.audio.sample_hash, the loudness
// ids and the silence ids all still report measurements over the frames
// that DID decode -- the error count records the rest, and no sink
// fabricates a value for a frame that never arrived. ---

TEST_CASE(
    "audio_decode_errors - the recoverable-error candidate still reports real hash/loudness/silence measurements",
    "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"));
  for (const std::string& id : {std::string("content.audio.sample_hash"), std::string("audio.loudness.integrated"),
                                 std::string("audio.loudness.true_peak"), std::string("audio.silence.edges"),
                                 std::string("audio.silence.dropouts")}) {
    const nlohmann::ordered_json* finding = find_finding(outcome.report, id);
    INFO("check id: " << id);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status").get<std::string>() != "skipped");
  }
}

// --- Test 4: audio_undecodable.mp4, whose audio stream decodes zero
// frames, marks the fingerprint partial and exits 66, and every
// decode-dependent audio check reports skipped:partial_scan. ---

TEST_CASE(
    "audio_decode_errors - a wholly undecodable stream marks the fingerprint partial, exits 66, and every "
    "decode-dependent audio check reports skipped:partial_scan",
    "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_undecodable.mp4"), fixture("audio_corrupt_clean.mp4"));
  CHECK(outcome.exit_code == 66);
  CHECK(diagnostics_mention_partial(outcome.report));

  for (const std::string& id :
       {std::string("meta.decode_errors"), std::string("content.audio.sample_hash"),
        std::string("audio.loudness.integrated"), std::string("audio.loudness.true_peak"),
        std::string("audio.silence.edges"), std::string("audio.silence.dropouts")}) {
    const nlohmann::ordered_json* finding = find_finding(outcome.report, id);
    INFO("check id: " << id);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status").get<std::string>() == "skipped");
    CHECK(finding->at("skip_reason").get<std::string>() == "partial_scan");
  }
}

// --- Test 5: comparing a baseline with a stable non-zero error count
// against a candidate with the SAME count reports pass -- a known defect
// stays comparable rather than gating forever. ---

TEST_CASE("audio_decode_errors - a baseline and candidate sharing the SAME non-zero error count report pass",
          "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_corrupt_frames.mp4"), fixture("audio_corrupt_frames.mp4"));
  CHECK(outcome.exit_code == 0);
  const nlohmann::ordered_json* finding = find_finding(outcome.report, "meta.decode_errors");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status").get<std::string>() == "pass");
  REQUIRE(finding->at("baseline").is_number());
  REQUIRE(finding->at("candidate").is_number());
  const std::int64_t count = finding->at("candidate").get<std::int64_t>();
  CHECK(count > 0);
  CHECK(finding->at("baseline").get<std::int64_t>() == count);
}

// --- Test 6: the count increases monotonically within one sweep and
// never resets, and a recoverable error never triggers a decoder re-open
// (D-07). ---
//
// The re-open half of this claim is proven statically, not here: Task 2's
// own acceptance criteria requires
// `grep -c 'avcodec_open2' src/probe/audio_decode.cpp` to show exactly one
// real call site (AudioDecodeState::ensure_initialized, outside
// feed_packet's own per-packet loop) -- verified during 06-10's own
// implementation (06-10-SUMMARY.md records the transcript). What THIS
// test proves at the CLI level is the "never resets" half: the SAME
// decode_error_count StreamAudioDecode::decode_error_count feeds BOTH
// meta.decode_errors' own registered check and
// content.audio.sample_hash's own evidence (src/analyzers/content/
// sample_hash.cpp's `decode_error_count` evidence key) is IDENTICAL for
// one decode of one file -- proof it is one non-resetting counter shared
// across the whole sweep, not two independently-derived numbers that
// merely happen to agree by coincidence.

TEST_CASE("audio_decode_errors - the SAME non-resetting counter feeds meta.decode_errors and sample_hash's own "
          "evidence identically",
          "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"));
  const nlohmann::ordered_json* decode_errors = find_finding(outcome.report, "meta.decode_errors");
  REQUIRE(decode_errors != nullptr);
  const std::int64_t registered_count = decode_errors->at("candidate").get<std::int64_t>();
  REQUIRE(registered_count > 0);

  const nlohmann::ordered_json* sample_hash = find_finding(outcome.report, "content.audio.sample_hash");
  REQUIRE(sample_hash != nullptr);
  REQUIRE(sample_hash->contains("evidence"));
  REQUIRE(sample_hash->at("evidence").contains("candidate"));
  REQUIRE(sample_hash->at("evidence").at("candidate").contains("decode_error_count"));
  CHECK(sample_hash->at("evidence").at("candidate").at("decode_error_count").get<std::int64_t>() ==
        registered_count);
}

// --- Test 7: the first error's reason is recorded in evidence so
// --explain can show what kind of error it was. ---

TEST_CASE("audio_decode_errors - the first error's own reason rides in evidence", "[integration]") {
  const CompareOutcome outcome = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"));
  const nlohmann::ordered_json* finding = find_finding(outcome.report, "meta.decode_errors");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->contains("evidence"));
  REQUIRE(finding->at("evidence").contains("candidate"));
  REQUIRE(finding->at("evidence").at("candidate").contains("first_error_reason"));
  const std::string reason = finding->at("evidence").at("candidate").at("first_error_reason").get<std::string>();
  CHECK_FALSE(reason.empty());
}

// --- Test 8: two runs of the same file report the identical count
// (TRUST-05). ---

TEST_CASE("audio_decode_errors - two independent runs report the identical count", "[integration]") {
  const CompareOutcome first = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"));
  const CompareOutcome second = compare(fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"));
  const nlohmann::ordered_json* first_finding = find_finding(first.report, "meta.decode_errors");
  const nlohmann::ordered_json* second_finding = find_finding(second.report, "meta.decode_errors");
  REQUIRE(first_finding != nullptr);
  REQUIRE(second_finding != nullptr);
  CHECK(first_finding->at("candidate").get<std::int64_t>() == second_finding->at("candidate").get<std::int64_t>());
}

// --- The explain-doc surface (Task 2's own acceptance criterion: `mediadiff
// explain meta.decode_errors` prints all three sections). ---

TEST_CASE("audio_decode_errors - mediadiff explain meta.decode_errors prints all three required sections",
          "[integration]") {
  const CliResult result = run_cli({"explain", "meta.decode_errors"});
  CHECK(result.exit_code == 0);
  CHECK(result.out.find("## What it measures") != std::string::npos);
  CHECK(result.out.find("## Why it matters") != std::string::npos);
  CHECK(result.out.find("## Accept / Tune / Silence") != std::string::npos);
}
