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
