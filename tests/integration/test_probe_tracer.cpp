#include <catch2/catch_test_macros.hpp>

#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

}  // namespace

// 03-02-PLAN.md Task 1: a real MP4 and its byte-identical copy flow
// through fingerprint_input -> DemuxSession -> the pass union ->
// topology.cpp -> the Phase-2 compare engine -> a rendered report --
// neither input is ever a snapshot.
TEST_CASE("probe_tracer - PROBE-01: two clean real media files compare with a passing container.format",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.mp4"), fixture("tracer_a_copy.mp4"), "--json"});

  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "container.format") {
      found = true;
      REQUIRE(finding.at("status") == "pass");
    }
  }
  REQUIRE(found);
}

// CONT-01: a real MP4 compared against a real Matroska file fails
// container.format, baseline "mov", candidate "matroska".
TEST_CASE("probe_tracer - CONT-01: a container-family mismatch fails container.format", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.mp4"), fixture("tracer_a.mkv"), "--json"});

  REQUIRE(result.exit_code != 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "container.format") {
      found = true;
      REQUIRE(finding.at("status") == "fail");
      REQUIRE(finding.at("baseline") == "mov");
      REQUIRE(finding.at("candidate") == "matroska");
    }
  }
  REQUIRE(found);
}

// The same media path through a second command, proving the seam is
// shared -- `inspect` reaches real media through the same fingerprint_input
// entry point `compare` does.
TEST_CASE("probe_tracer - inspect renders container.format for a real media file", "[integration]") {
  CliResult result = run_cli({"inspect", fixture("tracer_a.mp4")});
  REQUIRE(result.exit_code == 0);
  REQUIRE(result.out.find("container.format") != std::string::npos);
}

TEST_CASE("probe_tracer - inspect --json renders a diagnostics object", "[integration]") {
  CliResult result = run_cli({"inspect", fixture("tracer_a.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  REQUIRE(doc.contains("diagnostics"));
  REQUIRE(doc.at("diagnostics").is_object());
}

// A missing baseline file is ErrorKind::input_open, which never falls
// through to a probe attempt -- exit 65, naming the missing path.
TEST_CASE("probe_tracer - a missing baseline file never falls through to a probe attempt", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_does_not_exist.mp4"), fixture("tracer_a.mp4")});
  REQUIRE(result.exit_code == 65);
  REQUIRE(result.err.find("tracer_does_not_exist.mp4") != std::string::npos);
}

// 03-02-PLAN.md Task 2 (PROBE-01 completion): the wall-clock budget and
// its CLI surface.
TEST_CASE("probe_tracer - --probe-timeout 0 exits 65 and stderr names the timeout", "[integration]") {
  CliResult result =
      run_cli({"compare", "--probe-timeout", "0", fixture("tracer_a.mp4"), fixture("tracer_a_copy.mp4")});
  REQUIRE(result.exit_code == 65);
  const bool names_it =
      result.err.find("budget") != std::string::npos || result.err.find("timeout") != std::string::npos;
  REQUIRE(names_it);
}

TEST_CASE("probe_tracer - --probe-timeout with a non-numeric argument exits 64", "[integration]") {
  CliResult result =
      run_cli({"compare", "--probe-timeout", "notanumber", fixture("tracer_a.mp4"), fixture("tracer_a_copy.mp4")});
  REQUIRE(result.exit_code == 64);
}
