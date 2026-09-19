// 05-11-PLAN.md Task 3 (TIME-11, DOC-04): the DOC-04 no-others harness
// (tests/integration/timeline_findings.h) proven against this plan's own
// crafted fixtures --
//
//   tests/fixtures/timeline_tc_ndf.mp4 / timeline_tc_ndf_shifted.mp4 -- the
//   timeline.timecode.value TRIGGER pair (05-11-SUMMARY.md's own recorded
//   proof): identical 320x240/25fps/4s testsrc2+sine encodes, differing
//   ONLY in the `-timecode` option's start value (00:00:10:00 vs
//   00:00:20:00). Both carry a tmcd track, so `timeline.timecode` itself
//   stays `pass` -- only the compared STRING changes.
//
//   tests/fixtures/timeline_tc_ndf.mp4 / timeline_tc_absent.mp4 -- the
//   timeline.timecode presence TRIGGER pair: the IDENTICAL encode with no
//   `-timecode` option at all, so no tmcd track exists -- TIME-11's own
//   empty edge.
//
//   tests/fixtures/timeline_tc_ndf.mp4 / timeline_tc_ndf_copy.mp4 -- the
//   byte-identical CLEAN pair (a real `cp`, not a second independent
//   encode).
//
// Every TEST_CASE below carries the literal prefix "timeline_timecode - "
// so `ctest -R "integration\.timeline_timecode"` selects exactly this
// file's cases (TEST_PREFIX "integration." makes the real ctest name
// "integration.<TEST_CASE name>"; an unmatched -R filter exits ZERO and
// prints "No tests were found", which is why the prefix matters).

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

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1: the VALUE trigger pair -- timeline_tc_ndf.mp4 vs
// timeline_tc_ndf_shifted.mp4, under --profile sw-encoder (a fresh
// independent encode pair, matching timeline_jitter.mp4/timeline_vfr.mp4's
// own convention) -----------------------------------------------------
TEST_CASE("timeline_timecode - the value trigger pair declares its complete expected finding set under "
          "--profile sw-encoder, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_tc_ndf.mp4"), fixture("timeline_tc_ndf_shifted.mp4"), "sw-encoder");

  // A differing `-timecode` start value is ONE cause that legitimately
  // moves several facts (D-02), verified empirically against the real
  // binary before being written here: the tmcd track's own metadata
  // "timecode" tag echoes onto the VIDEO stream's own metadata too
  // (05-RESEARCH.md Pattern 4's own finding -- both the video stream's
  // and the tmcd stream's AVStream::metadata carry the identical key),
  // so meta.tags fires once at video scope and once more at the tmcd
  // track's own `data` scope -- the SAME mechanism, not two independent
  // changes.
  expect_declared_set(report,
                       {
                           // The check this task registers -- the compared
                           // byte sequence differs.
                           "timeline.timecode.value",
                           // The tmcd track's own "timecode" tag is echoed
                           // onto the video stream's metadata dictionary.
                           "meta.tags",
                           // The tmcd track's OWN metadata dictionary
                           // (scoped `data`) carries the differing tag too.
                           "meta.tags",
                       });

  const nlohmann::ordered_json* presence = find_finding(report, "timeline.timecode");
  REQUIRE(presence != nullptr);
  CHECK(presence->at("status").get<std::string>() == "pass");
  const nlohmann::ordered_json* value = find_finding(report, "timeline.timecode.value");
  REQUIRE(value != nullptr);
  CHECK(value->at("baseline").get<std::string>() == "00:00:10:00");
  CHECK(value->at("candidate").get<std::string>() == "00:00:20:00");
}

// --- Test 2: the PRESENCE trigger pair -- timeline_tc_ndf.mp4 vs
// timeline_tc_absent.mp4, under --profile sw-encoder --------------------
TEST_CASE("timeline_timecode - the presence trigger pair declares its complete expected finding set under "
          "--profile sw-encoder, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_tc_ndf.mp4"), fixture("timeline_tc_absent.mp4"), "sw-encoder");

  // Removing the tmcd track entirely is ONE cause that legitimately moves
  // several facts (D-02), verified empirically against the real binary:
  // the track count/types/order all change (one fewer stream), the video
  // stream's own echoed "timecode" tag disappears (meta.tags), and the
  // file's own overhead ratio shifts slightly (one fewer track's worth of
  // container structure).
  expect_declared_set(report, {
                                   "container.track_count",
                                   "container.track_types",
                                   "container.track_order",
                                   "timeline.timecode",
                                   "timeline.timecode.value",
                                   "size.overhead",
                                   "meta.tags",
                               });

  const nlohmann::ordered_json* presence = find_finding(report, "timeline.timecode");
  REQUIRE(presence != nullptr);
  CHECK(presence->at("baseline").get<std::string>() == "present");
  CHECK(presence->at("candidate").is_null());
  const nlohmann::ordered_json* value = find_finding(report, "timeline.timecode.value");
  REQUIRE(value != nullptr);
  CHECK(value->at("baseline").get<std::string>() == "00:00:10:00");
  CHECK(value->at("candidate").is_null());
}

// --- Test 3: the byte-identical clean pair's empty declared set ------------
TEST_CASE("timeline_timecode - the byte-identical clean pair declares the empty set and count_non_pass is zero",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_tc_ndf.mp4"), fixture("timeline_tc_ndf_copy.mp4"), "sw-encoder");
  expect_declared_set(report, {});
}

// --- Test 4: ROADMAP SC5's first clause -- presence, the exact SMPTE
// start value, and the drop-frame flag are all visible in one finding,
// including the drop-frame arm (Task 1's own empirically-resolved A1) ----
TEST_CASE("timeline_timecode - ROADMAP SC5: presence, the SMPTE start value, and the drop-frame flag are all "
          "reported, and a drop-frame string at the same nominal position compares as different from a "
          "non-drop-frame one",
          "[integration]") {
  // timeline_tc_df.mp4 (30000/1001 NTSC, `-timecode "00:00:10;00"`) against
  // timeline_tc_ndf.mp4 (25fps, `-timecode 00:00:10:00`) -- the SAME
  // nominal HH:MM:SS:FF position, differing ONLY in rate and drop-frame
  // punctuation (Task 1's own gen_corpus.sh comment: a rate change is
  // required to produce a genuine drop-frame timecode at all, so this
  // pair is proven directly rather than folded into a whole-report
  // declared-set assertion).
  const nlohmann::ordered_json report = compare_json(fixture("timeline_tc_df.mp4"), fixture("timeline_tc_ndf.mp4"), "sw-encoder");

  const nlohmann::ordered_json* presence = find_finding(report, "timeline.timecode");
  REQUIRE(presence != nullptr);
  CHECK(presence->at("baseline").get<std::string>() == "present");
  CHECK(presence->at("candidate").get<std::string>() == "present");

  const nlohmann::ordered_json* value = find_finding(report, "timeline.timecode.value");
  REQUIRE(value != nullptr);
  // The exact SMPTE start value, byte for byte, including the punctuation.
  CHECK(value->at("baseline").get<std::string>() == "00:00:10;00");
  CHECK(value->at("candidate").get<std::string>() == "00:00:10:00");
  CHECK(value->at("status").get<std::string>() != "pass");

  const nlohmann::ordered_json& evidence = value->at("evidence");
  CHECK(evidence.at("baseline").at("drop_frame").get<bool>() == true);
  CHECK(evidence.at("candidate").at("drop_frame").get<bool>() == false);
}

// --- Test 5 (TIME-11): the S12M and GOP sources report requires_decode ----
//
// TIME-11 names S12M and MPEG-2 GOP timecode as sources beside tmcd. Neither
// has a no-decode extraction path in this build, so every timecode
// measurement carries both, in a fixed order, as `unreachable_sources` with
// reason requires_decode -- on the tmcd-bearing baseline and on the
// tmcd-absent candidate alike, since the unreachability is a static fact of
// the build, not of the file. The `detail` prose is not asserted.
TEST_CASE("timeline_timecode - TIME-11: both timecode checks report S12M and MPEG-2 GOP timecode as "
          "unreachable_sources with reason requires_decode, on a tmcd-bearing and a tmcd-absent side alike",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_tc_ndf.mp4"), fixture("timeline_tc_absent.mp4"), "sw-encoder");

  const auto expect_unreachable_sources = [](const nlohmann::ordered_json& side) {
    const auto& sources = side.at("unreachable_sources");
    REQUIRE(sources.size() == 2U);
    REQUIRE(sources.at(0).at("source").get<std::string>() == "s12m_timecode");
    REQUIRE(sources.at(0).at("reason").get<std::string>() == "requires_decode");
    REQUIRE(sources.at(1).at("source").get<std::string>() == "mpeg2_gop_timecode");
    REQUIRE(sources.at(1).at("reason").get<std::string>() == "requires_decode");
  };

  int timecode_findings = 0;
  int timecode_value_findings = 0;
  for (const auto& finding : report.at("findings")) {
    const std::string id = finding.at("id").get<std::string>();
    if (id == "timeline.timecode") {
      ++timecode_findings;
    } else if (id == "timeline.timecode.value") {
      ++timecode_value_findings;
    } else {
      continue;
    }
    INFO(id << " finding: " << finding.dump(2));
    expect_unreachable_sources(finding.at("evidence").at("baseline"));
    expect_unreachable_sources(finding.at("evidence").at("candidate"));
  }
  REQUIRE(timecode_findings == 1);
  REQUIRE(timecode_value_findings == 1);
}
