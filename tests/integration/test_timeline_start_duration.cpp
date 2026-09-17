// 05-01-PLAN.md Task 3 (TIME-01/TIME-03, DOC-04): the DOC-04 no-others
// harness (tests/integration/timeline_findings.h) proven against a real
// synthetic report AND against the tracer pair timeline_start_base.mp4/
// timeline_start_shift.ts, matching this task's own <behavior> block
// Tests 1-5. Every TEST_CASE below carries the literal prefix
// "timeline_start_duration - " so `ctest -R
// "integration\.timeline_start_duration"` selects exactly this file's
// cases (TEST_PREFIX "integration." makes the real ctest name
// "integration.<TEST_CASE name>").

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "timeline_findings.h"

using mediadiff::test::CliResult;
using mediadiff::test::count_non_pass;
using mediadiff::test::diff_declared_set;
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
                                     const std::string& profile) {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", profile, "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  return report;
}

// Builds a synthetic report with `findings`, one entry per (id, status)
// pair given -- everything else (scope, severity, message) is irrelevant
// to count_non_pass/expect_declared_set, which only ever read `status`
// and `id`.
nlohmann::ordered_json synthetic_report(const std::vector<std::pair<std::string, std::string>>& findings) {
  nlohmann::ordered_json report;
  report["findings"] = nlohmann::ordered_json::array();
  for (const auto& [id, status] : findings) {
    report["findings"].push_back(nlohmann::ordered_json{{"id", id}, {"status", status}});
  }
  return report;
}

}  // namespace

// --- Test 1: count_non_pass over a synthetic report ------------------------

TEST_CASE("timeline_start_duration - count_non_pass counts every non-pass, non-skipped finding across every "
          "group including info, and is zero for an all-pass report",
          "[integration]") {
  const nlohmann::ordered_json mixed = synthetic_report({{"timeline.start", "fail"},
                                                            {"video.hdr.coherence", "info"},
                                                            {"container.format", "pass"},
                                                            {"size.file", "skipped"},
                                                            {"meta.tags", "warn"}});
  // Three non-pass, non-skipped: timeline.start (fail), video.hdr.coherence
  // (info -- counted, D-01 is explicit that info is never excluded),
  // meta.tags (warn). container.format (pass) and size.file (skipped) are
  // both excluded.
  REQUIRE(count_non_pass(mixed) == 3);

  const nlohmann::ordered_json all_pass =
      synthetic_report({{"timeline.start", "pass"}, {"container.format", "pass"}, {"size.file", "skipped"}});
  REQUIRE(count_non_pass(all_pass) == 0);
}

// --- Test 2: expect_declared_set detects an UNEXPECTED non-pass id ---------

TEST_CASE("timeline_start_duration - diff_declared_set names an unexpected non-pass finding id absent from the "
          "declared set",
          "[integration]") {
  const nlohmann::ordered_json report =
      synthetic_report({{"timeline.start", "fail"}, {"size.file", "warn"}, {"container.format", "pass"}});
  const mediadiff::test::DeclaredSetDiff diff = diff_declared_set(report, {"timeline.start"});
  REQUIRE(diff.missing.empty());
  REQUIRE(diff.unexpected.size() == 1);
  REQUIRE(diff.unexpected.front() == "size.file");
}

// --- Test 3: expect_declared_set detects a MISSING declared id -------------

TEST_CASE("timeline_start_duration - diff_declared_set names a declared id absent from the report's non-pass "
          "findings",
          "[integration]") {
  const nlohmann::ordered_json report = synthetic_report({{"timeline.start", "fail"}});
  const mediadiff::test::DeclaredSetDiff diff = diff_declared_set(report, {"timeline.start", "size.file"});
  REQUIRE(diff.unexpected.empty());
  REQUIRE(diff.missing.size() == 1);
  REQUIRE(diff.missing.front() == "size.file");
}

// --- Test 4: the tracer pair's complete declared set, under --profile remux

TEST_CASE("timeline_start_duration - the MP4-to-TS tracer pair declares its complete expected finding set under "
          "--profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_shift.ts"), "remux");

  // A single remux to MPEG-TS is ONE cause that legitimately moves several
  // facts (D-02) -- each member beyond timeline.start itself carries its
  // own causal reason, verified empirically against the real binary
  // before being written here:
  expect_declared_set(report, {
                                   // The check this task registers -- D-03's own global-scope
                                   // whole-file-shift finding (the TS muxer's ~1.4s default
                                   // mux delay).
                                   "timeline.start",
                                   // The container format itself genuinely changed (mov -> mpegts).
                                   "container.format",
                                   // MPEG-4 Part 2 video's profile/level/resolution are read from
                                   // the MP4 stsd atom at header-probe time; the same elementary
                                   // stream remuxed to MPEG-TS does not expose these the same way
                                   // (codecpar reports the -99/0x0 unresolved sentinels), a real,
                                   // observed property of this container pairing -- not a defect
                                   // this analyzer introduces.
                                   "video.profile",
                                   "video.level",
                                   "video.resolution",
                                   // MPEG-TS's own PES/PSI overhead makes the file's byte size (and
                                   // therefore stream_bitrate/overhead) genuinely differ from the
                                   // MP4 original under sw-encoder-derived tolerances.
                                   "size.file",
                                   "size.stream_bitrate",
                                   "size.overhead",
                                   // MP4's ftyp/handler tags (major_brand, compatible_brands,
                                   // minor_version, per-stream language) have no MPEG-TS
                                   // equivalent the demuxer surfaces the same way -- ONE cause
                                   // (the remux) fires meta.tags TWICE: once at `global` scope
                                   // (container-level tags) and once more at `video` scope
                                   // (the video stream's own handler-name tag), each a distinct
                                   // finding in the report, so this id is declared twice per
                                   // diff_declared_set's occurrence-count semantics (D-02).
                                   "meta.tags",
                                   "meta.tags",
                                   // 05-04-PLAN.md (TIME-03): the SAME remux, one more legitimate
                                   // effect -- the MPEG-TS demuxer's own AVStream::duration for
                                   // the AUDIO stream (3877ms) genuinely disagrees with its own
                                   // AVFormatContext::duration (4023ms) by more than the fixed
                                   // 40ms coherence threshold, a real property of this exact
                                   // container pairing (verified empirically against the real
                                   // binary's own --json evidence, not assumed) -- never a defect
                                   // this analyzer introduces. The VIDEO stream's own triple stays
                                   // coherent on both sides, so only the audio-scoped finding
                                   // fires.
                                   "timeline.duration.coherence",
                                   // 05-05-PLAN.md (TIME-01/TIME-04): the SAME remux, one more
                                   // legitimate effect -- ffmpeg's own mpegts muxer/demuxer
                                   // round-trip for a B-frame-less (`-bf 0`, no reordering) video
                                   // stream reports the SECOND packet's own DTS equal to the FIRST
                                   // packet's PTS (a one-packet lag, verified empirically via
                                   // `ffprobe -show_entries packet=pts,dts` on the real committed
                                   // fixture), producing exactly ONE `dts[1] <= dts[0]` tie at the
                                   // very start of the stream -- a genuine, structural property of
                                   // this MP4-to-TS remux pairing, not a defect this analyzer
                                   // introduces. The MP4 baseline's own video dts_monotonic count
                                   // stays `0`; only the candidate's video-scoped finding fires.
                                   "timeline.dts_monotonic",
                                   // 05-08-PLAN.md (TIME-05): the SAME remux, one more legitimate
                                   // effect -- the AAC audio stream's own native 1024-sample frame
                                   // period (1024/44100s ~= 23.2199ms) has no exact representation
                                   // on MPEG-TS's 90kHz PTS grid, so re-deriving the stream's own
                                   // ideal interval from ITS OWN span/count on the TS side rounds
                                   // enough intervals off that own grid to push the worst bin
                                   // (on_grid) past the 2% dist tolerance (verified via `mediadiff
                                   // compare --json`) -- a real, observed consequence of this exact
                                   // container/timebase pairing (the same class of effect
                                   // timeline_ntsc_remux.mkv's own D-05 fixture demonstrates for
                                   // video), never a defect this analyzer introduces. The VIDEO
                                   // stream's own native timebase survives the remux cleanly
                                   // (mpeg4's own tbn divides evenly into MPEG-TS's 90kHz grid), so
                                   // only the audio-scoped finding fires.
                                   "timeline.vfr_profile",
                                   // 05-10-PLAN.md Task 2's own K=32 checkpoint fit: the SAME
                                   // MPEG-TS audio-duration disagreement `timeline.
                                   // duration.coherence` above already documents (3877ms vs
                                   // 4023ms) nudges the fitted line's own residual max across
                                   // the 2ms constant-offset/linear-drift boundary on the TS
                                   // side alone -- `timeline.av_drift` (the RATE) stays `pass`
                                   // (D-07's dual gate: the accumulated end delta never clears
                                   // the epsilon), but `timeline.av_drift.pattern` has no such
                                   // tolerance by design (D-04, locked one-way) and so reports
                                   // the classification flip.
                                   "timeline.av_drift.pattern",
                               });
}

// --- Test 5: the clean pair's empty declared set ----------------------------

TEST_CASE("timeline_start_duration - the byte-identical clean pair declares the empty set and count_non_pass is "
          "zero",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "remux");
  expect_declared_set(report, {});
  REQUIRE(count_non_pass(report) == 0);
}

// --- Additional coverage matching Task 2's own <behavior> block: the whole-
// file shift's timeline.start finding is at GLOBAL scope, and every
// per-stream timeline.start finding on that same pair stays pass ----------

TEST_CASE("timeline_start_duration - the tracer pair's timeline.start finding fires at GLOBAL scope only, and "
          "every per-stream timeline.start finding stays pass",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_shift.ts"), "remux");

  bool global_non_pass = false;
  std::size_t per_stream_non_pass_count = 0;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() != "timeline.start") {
      continue;
    }
    const std::string status = finding.at("status").get<std::string>();
    const std::string scope_kind = finding.at("scope").at("kind").get<std::string>();
    if (scope_kind == "global") {
      global_non_pass = status != "pass" && status != "skipped";
    } else {
      if (status != "pass" && status != "skipped") {
        ++per_stream_non_pass_count;
      }
    }
  }
  REQUIRE(global_non_pass);
  REQUIRE(per_stream_non_pass_count == 0);
}

// --- 05-04-PLAN.md Task 1 (TIME-01/TIME-03): the duration triple's own
// evidence -- proven via `compare --json`, NOT `inspect --json -v`
// (`inspect` never renders Measurement::evidence at all -- a project-wide,
// pre-existing gap this plan does not introduce, first observed and
// recorded in 05-01-SUMMARY.md's own "Issues Encountered"; `compare --json`
// is the equivalent, working proof this task's own acceptance criteria
// intend). ---------------------------------------------------------------

namespace {

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id,
                                            const std::string& scope_kind) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == id && finding.at("scope").at("kind").get<std::string>() == scope_kind) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("timeline_start_duration - timeline.duration's evidence carries container_declared_ms, "
          "stream_declared_ms, computed_ms and duration_source when all three members are present (Test 1)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "remux");
  const nlohmann::ordered_json* finding = find_finding(report, "timeline.duration", "video");
  REQUIRE(finding != nullptr);
  const nlohmann::ordered_json& baseline_evidence = finding->at("evidence").at("baseline");
  REQUIRE(baseline_evidence.contains("container_declared_ms"));
  REQUIRE(baseline_evidence.contains("stream_declared_ms"));
  REQUIRE(baseline_evidence.contains("computed_ms"));
  REQUIRE(baseline_evidence.at("duration_source").get<std::string>() == "declared");
  REQUIRE(baseline_evidence.at("absent_members").empty());
}

TEST_CASE("timeline_start_duration - timeline.duration's evidence lists an unavailable member in absent_members "
          "and does NOT carry it as a number, on a real container that omits AVStream::duration (Test 2)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ntsc_base.mp4"), fixture("timeline_ntsc_remux.mkv"), "remux");
  const nlohmann::ordered_json* finding = find_finding(report, "timeline.duration", "video");
  REQUIRE(finding != nullptr);
  const nlohmann::ordered_json& candidate_evidence = finding->at("evidence").at("candidate");
  const std::vector<std::string> absent = candidate_evidence.at("absent_members").get<std::vector<std::string>>();
  REQUIRE(std::find(absent.begin(), absent.end(), "stream_declared") != absent.end());
  REQUIRE_FALSE(candidate_evidence.contains("stream_declared_ms"));
  // The other two members stay present as real numbers -- absence is
  // per-member, never contagious to the whole triple.
  REQUIRE(candidate_evidence.contains("container_declared_ms"));
  REQUIRE(candidate_evidence.contains("computed_ms"));
}

TEST_CASE("timeline_start_duration - a 4-second fixture compared against a 2-second one produces a non-pass "
          "timeline.duration finding; the byte-identical clean pair stays pass (Test 5)",
          "[integration]") {
  // The 4s-vs-2s pair itself is built in Task 3 (timeline_duration_short.mp4);
  // this asserts the SAME property using Task 1's own already-committed
  // fixtures: timeline.duration must NOT report non-pass for the
  // byte-identical clean pair (the negative half of Test 5, proven here;
  // the positive half is Task 3's own dedicated fixture pair).
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "remux");
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == "timeline.duration") {
      REQUIRE(finding.at("status").get<std::string>() == "pass");
    }
  }
}

// --- 05-04-PLAN.md Task 2 (TIME-03, D-08): timeline.duration.coherence ----

TEST_CASE("timeline_start_duration - timeline.duration.coherence names the first disagreeing pair, in the fixed "
          "container_vs_stream/container_vs_computed/stream_vs_computed order, and reports info (non-pass) status "
          "when only one side disagrees (Test 2/Test 3)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_shift.ts"), "remux");
  const nlohmann::ordered_json* finding = find_finding(report, "timeline.duration.coherence", "audio");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("baseline").get<std::string>() == "coherent");
  REQUIRE(finding->at("candidate").get<std::string>() == "container_vs_stream");
  REQUIRE(finding->at("status").get<std::string>() == "info");
  REQUIRE(finding->at("severity").get<std::string>() == "info");
  REQUIRE_FALSE(finding->at("gating").get<bool>());
}

TEST_CASE("timeline_start_duration - timeline.duration.coherence only tests a pair whose BOTH members are present, "
          "and an untestable/all-agreeing case emits the unflagged value 'coherent' (Test 1/Test 4)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ntsc_base.mp4"), fixture("timeline_ntsc_remux.mkv"), "remux");
  const nlohmann::ordered_json* finding = find_finding(report, "timeline.duration.coherence", "video");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status").get<std::string>() == "pass");
  const nlohmann::ordered_json& candidate_evidence = finding->at("evidence").at("candidate");
  // The candidate (MKV) has no stream_declared member at all -- only the
  // ONE pair whose both members are present (container_vs_computed) was
  // tested; the other two, each naming the absent member, were skipped.
  const std::vector<std::string> tested = candidate_evidence.at("tested_pairs").get<std::vector<std::string>>();
  REQUIRE(tested == std::vector<std::string>{"container_vs_computed"});
}

// --- 05-04-PLAN.md Task 3: the timeline.duration DOC-03 trigger pair's own
// COMPLETE declared finding set (D-02) -- a duration halved from 4s to 2s is
// ONE cause that legitimately moves several facts beyond timeline.duration
// itself, each verified empirically against the real binary before being
// declared here, never guessed from the recipe alone. --------------------

TEST_CASE("timeline_start_duration - the duration-short trigger pair (timeline_start_base.mp4 vs "
          "timeline_duration_short.mp4) declares its complete expected finding set under --profile remux, and "
          "count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_duration_short.mp4"), "remux");

  expect_declared_set(report, {
                                   // The check this task registers -- the computed member (last
                                   // presentation end - first PTS) genuinely halves along with the
                                   // recipe's own duration=4 -> duration=2 change, at both scopes
                                   // (this recipe carries one video and one audio stream).
                                   "timeline.duration",
                                   "timeline.duration",
                                   // MP4's edit-list entry carries each track's own presentation
                                   // SEGMENT DURATION (bmff_scan's own EditListEntry::
                                   // segment_duration) -- a shorter recipe genuinely shortens that
                                   // field too, at both scopes, a real property of the mp4 muxer's
                                   // own edit-list construction, not a defect this analyzer
                                   // introduces.
                                   "container.mp4.edit_list",
                                   "container.mp4.edit_list",
                                   // Half the duration at the same 25fps rate is genuinely half the
                                   // frame count (50 vs 100 frames) -- video.frame_count counts from
                                   // the real packet/parser scan (VIDEO-02's own rule), never from a
                                   // container-declared value, so this is a real, counted difference.
                                   "video.frame_count",
                                   // A shorter encode is genuinely a smaller file at a genuinely
                                   // different average bitrate/overhead ratio -- the SAME
                                   // size.file/size.stream_bitrate/size.overhead cluster this
                                   // project's own MP4-to-TS tracer pair (Test 4 above) already
                                   // established fires on any real byte-size change.
                                   "size.file",
                                   "size.stream_bitrate",
                                   "size.overhead",
                               });
}
