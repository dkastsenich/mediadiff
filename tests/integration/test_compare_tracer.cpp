#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

// MEDIADIFF_FIXTURES_DIR is injected by tests/integration/CMakeLists.txt as
// an absolute path to tests/fixtures/snapshots — using it (rather than a
// path relative to the process's current working directory) means these
// tests pass regardless of ctest's WORKING_DIRECTORY, which defaults to
// the build tree, not the source tree.
std::string fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURES_DIR) + "/" + name; }

}  // namespace

// SNAP-02/TRUST-05: the tracer's clean path — two snapshots that agree on
// every registered check produce exit 0 and a single `pass` finding whose
// skip_reason key is present (ENG-14: `skipped != pass` is asserted here by
// the key's mere existence, not just its value).
TEST_CASE("compare_tracer - SNAP-02: clean compare exits 0 with one pass finding", "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_clean.snap.json"), "--json"});

  REQUIRE(result.exit_code == 0);

  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  REQUIRE(report.at("findings").size() == 1);

  const auto& finding = report.at("findings").at(0);
  REQUIRE(finding.at("status") == "pass");
  REQUIRE(finding.contains("skip_reason"));
  REQUIRE(finding.at("skip_reason") == "none");
}

// TRUST-05/CLI-06: a tool-version skew is a `warn`-severity `exact`-semantic
// mismatch, and --strict promotes a worst-warn result to exit 2 — the
// contract doc 00 section 3.1 fixes, not this test's own invention.
TEST_CASE("compare_tracer - TRUST-05: skew compare with --strict exits 2 on meta.tool_version", "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_skew.snap.json"), "--json", "--strict"});

  REQUIRE(result.exit_code == 2);

  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.at("findings").size() == 1);

  const auto& finding = report.at("findings").at(0);
  REQUIRE(finding.at("id") == "meta.tool_version");
  REQUIRE(finding.at("status") == "warn");
}

// TRUST-05 (compare-path half only — see 02-01-PLAN.md's Flagged
// Assumptions: the full guarantee, including which envelope fields are
// excluded once the envelope gains time-varying members, is plan 02-07's).
// Running the identical clean compare twice produces byte-identical
// stdout.
TEST_CASE("compare_tracer - TRUST-05: identical runs produce byte-identical JSON", "[integration]") {
  const std::vector<std::string> args = {"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_clean.snap.json"),
                                          "--json"};
  CliResult first = run_cli(args);
  CliResult second = run_cli(args);

  REQUIRE(first.exit_code == 0);
  REQUIRE(second.exit_code == 0);
  REQUIRE(first.out == second.out);
}

// CLI-06/02-RESEARCH.md Pitfall 1: an unrecognized flag is a CLI11 parse
// error, mapped to exit 64 — NOT a value in CLI11's own ExitCodes 100-127
// range, which is what App::exit()'s return value would be if trusted
// directly.
TEST_CASE("compare_tracer - CLI-06: unrecognized flag exits 64, not CLI11's own range", "[integration]") {
  CliResult result = run_cli({"compare", "--not-a-flag", "a", "b"});
  REQUIRE(result.exit_code == 64);
}
