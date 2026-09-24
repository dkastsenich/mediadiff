// 06-09-PLAN.md Task 2 (AUDIO-07, AUDIO-10): `audio.silence.edges` and
// `audio.silence.dropouts` through the real CLI -- Behavior Tests 1 through
// 8 from that task's own <behavior> block, against the `audio_silence_*`/
// `audio_dropout*` fixtures 06-02 built (plus this task's own Rule 1 fix to
// `audio_dropout_clean.flac`'s recipe -- see this file's own Test 4 comment
// and 06-09-SUMMARY.md).

#include <catch2/catch_test_macros.hpp>

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

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::vector<std::string>& extra_args = {},
                                     const std::string& profile = "sw-encoder") {
  require_fixture(baseline);
  require_fixture(candidate);
  std::vector<std::string> args = {"compare", baseline, candidate, "--profile", profile, "--json"};
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

// --- Test 1: audio_silence_none.flac reports an empty span list on both
// ids as real measured values. ---

TEST_CASE("audio_silence - a continuous tone reports an empty span list on both ids as real measured values",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_silence_none.flac"), fixture("audio_silence_none.flac"));

  const nlohmann::ordered_json* edges = find_finding(report, "audio.silence.edges");
  REQUIRE(edges != nullptr);
  REQUIRE(edges->at("baseline").is_array());
  CHECK(edges->at("baseline").empty());
  CHECK(edges->at("status").get<std::string>() == "pass");

  const nlohmann::ordered_json* dropouts = find_finding(report, "audio.silence.dropouts");
  REQUIRE(dropouts != nullptr);
  REQUIRE(dropouts->at("baseline").is_array());
  CHECK(dropouts->at("baseline").empty());
  CHECK(dropouts->at("status").get<std::string>() == "pass");

  expect_declared_set(report, {});
}

// --- Test 2: comparing audio_silence_none.flac against audio_silence_lead.
// flac reports a non-pass audio.silence.edges -- an introduced leading
// silence gates. ---

TEST_CASE("audio_silence - an introduced leading silence reports a non-pass audio.silence.edges",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_silence_none.flac"), fixture("audio_silence_lead.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.silence.edges");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() != "pass");
  CHECK(finding->at("candidate").size() == 1);

  // The dropout detector never separately reports this same leading region
  // (docs/checks/audio.silence.dropouts.md's own stated exclusion).
  const nlohmann::ordered_json* dropouts = find_finding(report, "audio.silence.dropouts");
  REQUIRE(dropouts != nullptr);
  CHECK(dropouts->at("status").get<std::string>() == "pass");

  // 250ms of added leading silence is a real, causally-explained delta on
  // duration, the hash chain and file size, in addition to the span
  // finding itself (D-02: one cause, several real effects).
  expect_declared_set(report, {"timeline.duration", "audio.silence.edges", "content.audio.sample_hash", "size.file",
                                "size.stream_bitrate", "size.peak_bitrate", "size.overhead"});
}

// --- Test 3: the reverse comparison -- silence REMOVED -- follows
// src/compare/span.cpp's existing introduced-span rule: a baseline span
// absent from the candidate is `removed`, always `info`, never gating. ---

TEST_CASE("audio_silence - the reverse comparison (silence REMOVED) reports info, never gates",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_silence_lead.flac"), fixture("audio_silence_none.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.silence.edges");
  REQUIRE(finding != nullptr);
  // A removed span is always `info` under the `span` semantic's own rule
  // (src/compare/span.cpp), never escalated to the check's own `fail`
  // severity -- `gating` in the JSON report is a static per-check
  // severity flag (`is_gating(finding.severity)`, src/report/json.cpp),
  // not a per-finding verdict, so the property under test is the STATUS
  // string, mirroring test_timeline_structure.cpp's own established
  // precedent for this exact rule.
  CHECK(finding->at("status").get<std::string>() == "info");
  CHECK(finding->at("message").get<std::string>().find("removed") != std::string::npos);
}

// --- Test 4: comparing audio_dropout_clean.flac against audio_dropout.flac
// reports a non-pass audio.silence.dropouts naming the span. ---
//
// DEVIATION (Rule 1 - Bug, documented fully in 06-09-SUMMARY.md):
// scripts/gen_corpus.sh's own literal recipe for audio_dropout_clean.flac
// was byte-for-byte identical to audio_dropout.flac's own recipe (both
// applied the SAME `volume=enable=... :volume=0` interior mute) --
// comparing two byte-identical files could never trigger this check at
// all, defeating the fixture's own stated "clean pair" purpose. Fixed to a
// plain, unmuted 6s tone; tests/golden/CORPUS_DIGEST.txt's own line for
// this fixture was updated (never a NEW line -- this is a content fix to
// an existing, un-anchored Phase-6 entry, confirmed absent from the
// lint's own protected historical commit 8caf1f1).

TEST_CASE("audio_silence - a 400ms interior dropout reports a non-pass audio.silence.dropouts naming the span",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_dropout_clean.flac"), fixture("audio_dropout.flac"));
  const nlohmann::ordered_json* finding = find_finding(report, "audio.silence.dropouts");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() != "pass");
  REQUIRE(finding->at("candidate").size() == 1);
  const nlohmann::ordered_json& span = finding->at("candidate").at(0);
  CHECK(span.contains("start"));
  CHECK(span.contains("end"));

  const nlohmann::ordered_json* edges = find_finding(report, "audio.silence.edges");
  REQUIRE(edges != nullptr);
  CHECK(edges->at("status").get<std::string>() == "pass");

  // FLAC's own encoder picked a different internal bit depth for the
  // muted-interior file (a genuine, verified side effect of the content
  // difference, not a bug -- confirmed via `mediadiff inspect --content`:
  // audio_dropout_clean.flac is s16/16-bit, audio_dropout.flac is
  // s32/24-bit), which in turn makes the two files' decode paths
  // incomparable for the hash (skipped, not counted by count_non_pass) and
  // moves every size.* figure (D-02: one cause, several real effects).
  expect_declared_set(report, {"audio.sample_fmt", "audio.bit_depth", "audio.silence.dropouts", "size.file",
                                "size.stream_bitrate", "size.peak_bitrate", "size.overhead"});
}

// --- Test 5: each fixture against its own copy reports pass on both ids. ---

TEST_CASE("audio_silence - each fixture against its own copy reports pass on both ids", "[integration]") {
  for (const std::string& name : {std::string("audio_silence_none.flac"), std::string("audio_silence_lead.flac"),
                                   std::string("audio_silence_trail.flac"), std::string("audio_dropout.flac"),
                                   std::string("audio_dropout_clean.flac")}) {
    const nlohmann::ordered_json report = compare_json(fixture(name), fixture(name));
    const nlohmann::ordered_json* edges = find_finding(report, "audio.silence.edges");
    REQUIRE(edges != nullptr);
    CHECK(edges->at("status").get<std::string>() == "pass");
    const nlohmann::ordered_json* dropouts = find_finding(report, "audio.silence.dropouts");
    REQUIRE(dropouts != nullptr);
    CHECK(dropouts->at("status").get<std::string>() == "pass");
  }
}

// --- Test 6: a trailing-silence fixture reports the span on
// audio.silence.edges, not on audio.silence.dropouts. ---

TEST_CASE("audio_silence - a trailing-silence fixture reports the span on audio.silence.edges, not on "
          "audio.silence.dropouts",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_silence_none.flac"), fixture("audio_silence_trail.flac"));
  const nlohmann::ordered_json* edges = find_finding(report, "audio.silence.edges");
  REQUIRE(edges != nullptr);
  REQUIRE(edges->at("status").get<std::string>() != "pass");
  REQUIRE(edges->at("candidate").size() == 1);

  const nlohmann::ordered_json* dropouts = find_finding(report, "audio.silence.dropouts");
  REQUIRE(dropouts != nullptr);
  CHECK(dropouts->at("status").get<std::string>() == "pass");

  // Unlike the 250ms-leading-silence pair (Test 2), the 500ms trailing pad
  // here happens to round to the SAME size.file/size.peak_bitrate/
  // size.overhead bucket (verified empirically) -- only the stream-level
  // bitrate figure moves in addition to duration/hash/the span itself.
  expect_declared_set(
      report, {"timeline.duration", "audio.silence.edges", "content.audio.sample_hash", "size.stream_bitrate"});
}

// --- Test 7: a file with no audio stream emits skipped:insufficient_data
// on both ids; with --no-content both emit skipped:requires_decode. ---

TEST_CASE("audio_silence - a file with no audio stream skips insufficient_data on both ids", "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("video_h264_closed.h264"), fixture("video_h264_closed.h264"));
  const nlohmann::ordered_json* edges = find_finding(report, "audio.silence.edges");
  REQUIRE(edges != nullptr);
  CHECK(edges->at("status").get<std::string>() == "skipped");
  CHECK(edges->at("skip_reason").get<std::string>() == "insufficient_data");
  const nlohmann::ordered_json* dropouts = find_finding(report, "audio.silence.dropouts");
  REQUIRE(dropouts != nullptr);
  CHECK(dropouts->at("status").get<std::string>() == "skipped");
  CHECK(dropouts->at("skip_reason").get<std::string>() == "insufficient_data");
}

TEST_CASE("audio_silence - --no-content skips requires_decode on both ids", "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_silence_none.flac"), fixture("audio_silence_none.flac"), {"--no-content"});
  const nlohmann::ordered_json* edges = find_finding(report, "audio.silence.edges");
  REQUIRE(edges != nullptr);
  CHECK(edges->at("status").get<std::string>() == "skipped");
  CHECK(edges->at("skip_reason").get<std::string>() == "requires_decode");
  const nlohmann::ordered_json* dropouts = find_finding(report, "audio.silence.dropouts");
  REQUIRE(dropouts != nullptr);
  CHECK(dropouts->at("status").get<std::string>() == "skipped");
  CHECK(dropouts->at("skip_reason").get<std::string>() == "requires_decode");
}

// --- Test 8: each fixture comparison's whole-report non-pass count equals
// its declared finding set exactly -- proven for every pair above via
// expect_declared_set's own DOC-04 no-others discipline (already asserted
// inline at each call site; this test re-runs the two TRIGGER pairs
// explicitly as the plan's own named Test 8). ---

TEST_CASE("audio_silence - the trigger pairs' whole-report non-pass count equals their declared finding set exactly",
          "[integration]") {
  {
    const nlohmann::ordered_json report =
        compare_json(fixture("audio_silence_none.flac"), fixture("audio_silence_lead.flac"));
    expect_declared_set(report, {"timeline.duration", "audio.silence.edges", "content.audio.sample_hash",
                                  "size.file", "size.stream_bitrate", "size.peak_bitrate", "size.overhead"});
  }
  {
    const nlohmann::ordered_json report =
        compare_json(fixture("audio_dropout_clean.flac"), fixture("audio_dropout.flac"));
    expect_declared_set(report, {"audio.sample_fmt", "audio.bit_depth", "audio.silence.dropouts", "size.file",
                                  "size.stream_bitrate", "size.peak_bitrate", "size.overhead"});
  }
}
