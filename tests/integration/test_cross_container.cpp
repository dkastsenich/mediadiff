// CONT-02 (03-06-PLAN.md Task 3): a cross-container comparison demotes
// every `container.<fmt>.*` finding on BOTH sides to
// `skipped:cross_container`, while `container.format` itself still flags
// and every unscoped check still compares -- proven through the real CLI,
// including the policy-independence diff and a snapshot-versus-live pair
// (behaviors 2-7; behavior 1, container_family_token's own mapping table,
// is proven directly in tests/unit/test_container_family.cpp).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_cross_container";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

// Every finding whose skip_reason is cross_container, as a sorted set of
// "id@scope" strings -- comparable across two separate CLI runs (Test 5's
// own policy-independence diff needs this).
std::set<std::string> cross_container_skip_ids(const nlohmann::ordered_json& report) {
  std::set<std::string> ids;
  for (const auto& finding : report.at("findings")) {
    if (finding.value("status", "") == "skipped" && finding.value("skip_reason", "") == "cross_container") {
      const auto& scope = finding.at("scope");
      ids.insert(finding.at("id").get<std::string>() + "@" + scope.dump());
    }
  }
  return ids;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 2: every container.mp4.* AND container.mkv.* finding demotes ----

TEST_CASE("cross_container - an MP4-vs-MKV comparison demotes every container.mp4.* AND container.mkv.* "
          "finding on BOTH sides to skipped:cross_container",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mkv_cues_front.mkv"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const std::vector<std::string> kMp4Ids = {
      "container.mp4.faststart",  "container.mp4.brands",       "container.mp4.fragmentation",
      "container.mp4.fragment_duration", "container.mp4.edit_list", "container.mp4.timescale",
  };
  const std::vector<std::string> kMkvIds = {
      "container.mkv.cues_placement",
      "container.mkv.codec_delay",
      "container.mkv.timestamp_scale",
      "container.mkv.duration_element",
  };

  for (const std::string& id : kMp4Ids) {
    const auto* finding = find_finding(report, id);
    INFO("missing container.mp4.* finding: " << id);
    REQUIRE(finding != nullptr);
    REQUIRE(finding->at("status") == "skipped");
    REQUIRE(finding->at("skip_reason") == "cross_container");
  }
  // container.mkv.codec_delay is per-track scoped (audio[0] on the MKV
  // side) rather than global -- present in `findings` here specifically
  // BECAUSE the demotion fires unconditionally on Measurement presence
  // (engine.cpp's own comment), not merely when both sides paired
  // normally.
  for (const std::string& id : kMkvIds) {
    bool found_any = false;
    for (const auto& finding : report.at("findings")) {
      if (finding.at("id") == id) {
        found_any = true;
        REQUIRE(finding.at("status") == "skipped");
        REQUIRE(finding.at("skip_reason") == "cross_container");
      }
    }
    INFO("missing container.mkv.* finding: " << id);
    REQUIRE(found_any);
  }
}

// --- Test 3: container.format itself is NOT demoted -------------------------

TEST_CASE("cross_container - container.format itself is never demoted -- it reports the difference", "[integration]") {
  CliResult result = run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mkv_cues_front.mkv"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const auto* finding = find_finding(report, "container.format");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("skip_reason") == "none");
  REQUIRE(finding->at("status") != "skipped");
}

// --- Test 4: unscoped checks still compare normally -------------------------

TEST_CASE("cross_container - unscoped checks (container.track_count, meta.tags) still compare normally",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mkv_cues_front.mkv"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const auto* track_count = find_finding(report, "container.track_count");
  REQUIRE(track_count != nullptr);
  REQUIRE(track_count->at("skip_reason") == "none");

  bool found_tags = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.tags") {
      found_tags = true;
      REQUIRE(finding.at("skip_reason") == "none");
    }
  }
  REQUIRE(found_tags);
}

// --- Test 5: the demotion is unconditional on severity policy --------------

TEST_CASE("cross_container - the demotion is unconditional on severity policy: --set container.format=info "
          "produces the IDENTICAL set of skipped:cross_container findings",
          "[integration]") {
  CliResult baseline_run = run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mkv_cues_front.mkv"), "--json"});
  const nlohmann::ordered_json baseline_report = nlohmann::ordered_json::parse(baseline_run.out, nullptr, false);
  REQUIRE_FALSE(baseline_report.is_discarded());

  CliResult demoted_run = run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mkv_cues_front.mkv"),
                                    "--set", "container.format=info", "--json"});
  const nlohmann::ordered_json demoted_report = nlohmann::ordered_json::parse(demoted_run.out, nullptr, false);
  REQUIRE_FALSE(demoted_report.is_discarded());

  REQUIRE(cross_container_skip_ids(baseline_report) == cross_container_skip_ids(demoted_report));

  // Only container.format's OWN severity/status differs between the two
  // runs (its status escalation is governed by severity policy; the
  // demotion mechanism is not).
  const auto* baseline_format = find_finding(baseline_report, "container.format");
  const auto* demoted_format = find_finding(demoted_report, "container.format");
  REQUIRE(baseline_format != nullptr);
  REQUIRE(demoted_format != nullptr);
  REQUIRE(demoted_format->at("severity") == "info");
}

// --- Test 6: two MP4s produce zero cross_container findings ----------------

TEST_CASE("cross_container - comparing two MP4s produces NO cross_container finding at all", "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mp4_faststart_copy.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(cross_container_skip_ids(report).empty());
}

// --- Test 7: a snapshot-versus-live cross-container pair demotes identically

TEST_CASE("cross_container - a snapshot taken from an MKV compared against a live MP4 demotes identically to "
          "two live files",
          "[integration]") {
  const std::string snap_path = (scratch_dir() / "mkv_snapshot.snap.json").string();
  CliResult snap_result = run_cli({"snapshot", fixture("mkv_cues_front.mkv"), "--out", snap_path});
  REQUIRE(snap_result.exit_code == 0);
  REQUIRE(fs::exists(snap_path));

  CliResult live_pair =
      run_cli({"compare", fixture("mp4_faststart.mp4"), fixture("mkv_cues_front.mkv"), "--json"});
  const nlohmann::ordered_json live_report = nlohmann::ordered_json::parse(live_pair.out, nullptr, false);
  REQUIRE_FALSE(live_report.is_discarded());

  CliResult snap_pair = run_cli({"compare", fixture("mp4_faststart.mp4"), snap_path, "--json"});
  const nlohmann::ordered_json snap_report = nlohmann::ordered_json::parse(snap_pair.out, nullptr, false);
  REQUIRE_FALSE(snap_report.is_discarded());

  REQUIRE(cross_container_skip_ids(live_report) == cross_container_skip_ids(snap_report));
}
