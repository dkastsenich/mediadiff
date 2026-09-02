// 03-04-PLAN.md: DOC-03's per-check obligation for the six checks this
// plan registers -- container.track_count/track_types/track_order/
// chapters, meta.tags, meta.tags.language -- each proven through a real
// triggering CLI invocation and a real clean one, spawning the actual
// `mediadiff` binary (never libmediadiff directly), matching
// tests/integration/test_probe_tracer.cpp's own established pattern.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- container.track_count / container.track_types (CONT-09) -----------

TEST_CASE("container_topology - CONT-09: dropping a subtitle track fails track_count and track_types",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("topo_subs.mp4"), fixture("topo_nosubs.mp4"), "--json"});
  REQUIRE(result.exit_code != 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const auto* count = find_finding(report, "container.track_count");
  REQUIRE(count != nullptr);
  REQUIRE(count->at("status") == "fail");

  const auto* types = find_finding(report, "container.track_types");
  REQUIRE(types != nullptr);
  REQUIRE(types->at("status") == "fail");
}

TEST_CASE("container_topology - CONT-09: an identical subtitle pair compares clean on both checks",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("topo_subs.mp4"), fixture("topo_subs_copy.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const auto* count = find_finding(report, "container.track_count");
  REQUIRE(count != nullptr);
  REQUIRE(count->at("status") == "pass");

  const auto* types = find_finding(report, "container.track_types");
  REQUIRE(types != nullptr);
  REQUIRE(types->at("status") == "pass");
}

TEST_CASE("container_topology - a dropped tmcd timecode track is named literally in track_types", "[integration]") {
  CliResult result = run_cli({"compare", fixture("topo_tmcd.mp4"), fixture("topo_notmcd.mp4"), "--json"});
  REQUIRE(result.exit_code != 0);
  REQUIRE(result.out.find("tmcd") != std::string::npos);
}

// --- container.track_order (move, not add+remove) -----------------------

TEST_CASE("container_topology - a same-type stream reorder fails track_order while track_count stays clean",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("topo_order_a.mp4"), fixture("topo_order_b.mp4"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const auto* order = find_finding(report, "container.track_order");
  REQUIRE(order != nullptr);
  REQUIRE(order->at("status") != "pass");

  const auto* count = find_finding(report, "container.track_count");
  REQUIRE(count != nullptr);
  REQUIRE(count->at("status") == "pass");
}

TEST_CASE("container_topology - identical stream order compares clean on track_order", "[integration]") {
  CliResult result = run_cli({"compare", fixture("topo_order_a.mp4"), fixture("topo_order_a.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto* order = find_finding(report, "container.track_order");
  REQUIRE(order != nullptr);
  REQUIRE(order->at("status") == "pass");
}

// --- container.chapters (auto-skip on MPEG-TS) ---------------------------

TEST_CASE("container_topology - container.chapters auto-skips as not_applicable_container on a TS input",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("topo_ts.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  bool found = false;
  for (const auto& entry : doc.at("groups").at("container")) {
    if (entry.at("id") == "container.chapters") {
      found = true;
      REQUIRE(entry.at("status") == "skipped");
      REQUIRE(entry.at("skip_reason") == "not_applicable_container");
    }
  }
  REQUIRE(found);
}

TEST_CASE("container_topology - a chapters pair differs, a no-chapters pair compares clean", "[integration]") {
  // container.chapters is `info` severity (doc 02's own table) -- an info
  // finding never gates the exit code, with or without --strict
  // (src/cli/exit_code.h's own documented mapping), so this proves the
  // triggering/clean distinction through the finding's own status, not
  // through the process exit code.
  CliResult triggering = run_cli({"compare", fixture("topo_chapters.mkv"), fixture("topo_nochapters.mkv"), "--json"});
  const nlohmann::ordered_json triggering_report = nlohmann::ordered_json::parse(triggering.out, nullptr, false);
  REQUIRE_FALSE(triggering_report.is_discarded());
  const auto* triggering_finding = find_finding(triggering_report, "container.chapters");
  REQUIRE(triggering_finding != nullptr);
  REQUIRE(triggering_finding->at("status") != "pass");

  CliResult clean = run_cli({"compare", fixture("topo_nochapters.mkv"), fixture("topo_nochapters.mkv"), "--json"});
  REQUIRE(clean.exit_code == 0);
  const nlohmann::ordered_json clean_report = nlohmann::ordered_json::parse(clean.out, nullptr, false);
  REQUIRE_FALSE(clean_report.is_discarded());
  const auto* clean_finding = find_finding(clean_report, "container.chapters");
  REQUIRE(clean_finding != nullptr);
  REQUIRE(clean_finding->at("status") == "pass");
}

// --- meta.tags (CONT-03) -------------------------------------------------

TEST_CASE("container_topology - CONT-03: volatile-only differences compare clean, naming the ignored key",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("tags_volatile_a.mp4"), fixture("tags_volatile_b.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found_global = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.tags" && finding.at("scope").at("kind") == "global") {
      found_global = true;
      REQUIRE(finding.at("status") == "pass");
      REQUIRE_FALSE(finding.at("evidence").is_null());
      REQUIRE(finding.at("evidence").dump().find("creation_time") != std::string::npos);
    }
  }
  REQUIRE(found_global);
}

TEST_CASE("container_topology - CONT-03: a non-volatile tag difference (title) fails and names it", "[integration]") {
  // meta.tags is `warn` severity (03-CHECK-ROSTER.md) -- a warn finding
  // only gates the exit code under --strict (src/cli/exit_code.h), so this
  // proves the difference through the finding's own status/message.
  CliResult result = run_cli({"compare", fixture("tags_title_a.mp4"), fixture("tags_title_b.mp4"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.tags" && finding.at("scope").at("kind") == "global") {
      found = true;
      REQUIRE(finding.at("status") != "pass");
      REQUIRE(finding.at("message").get<std::string>().find("title") != std::string::npos);
    }
  }
  REQUIRE(found);
}

// --- meta.tags.language (CONT-04) ----------------------------------------

TEST_CASE("container_topology - CONT-04: und vs absent compares clean", "[integration]") {
  CliResult result = run_cli({"compare", fixture("lang_und.mp4"), fixture("lang_absent.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.tags.language" && finding.at("scope").at("kind") == "audio") {
      found = true;
      REQUIRE(finding.at("status") == "pass");
    }
  }
  REQUIRE(found);
}

TEST_CASE("container_topology - CONT-04: eng vs fra differs at warn severity", "[integration]") {
  CliResult result = run_cli({"compare", fixture("lang_eng.mp4"), fixture("lang_fra.mp4"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.tags.language" && finding.at("scope").at("kind") == "audio") {
      found = true;
      REQUIRE(finding.at("status") != "pass");
      REQUIRE(finding.at("severity") == "warn");
    }
  }
  REQUIRE(found);
}
