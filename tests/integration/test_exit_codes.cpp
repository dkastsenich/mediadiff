// CLI-06/CLI-07/CLI-10: every contract exit code (0, 1, 2, 64, 65, 66, 70)
// is produced by at least one case here, each asserted as an exact
// integer, never a loose non-zero check (02-10-PLAN.md Task 2).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURES_DIR) + "/" + name; }

}  // namespace

TEST_CASE("exit_codes - 0: a clean compare exits exactly 0", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_clean.snap.json")});
  CHECK(result.exit_code == 0);
}

TEST_CASE("exit_codes - 1: a compare producing a fail-severity finding exits exactly 1", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_skew.snap.json"),
                               "--profile", "strict-bitexact"});
  CHECK(result.exit_code == 1);
}

TEST_CASE("exit_codes - 2: a worst-warn compare with --strict exits exactly 2", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_skew.snap.json"),
                               "--strict"});
  CHECK(result.exit_code == 2);
}

TEST_CASE("exit_codes - 0: the same worst-warn compare WITHOUT --strict exits exactly 0", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_skew.snap.json")});
  CHECK(result.exit_code == 0);
}

TEST_CASE("exit_codes - 64: an unknown flag exits exactly 64", "[integration]") {
  CliResult result = run_cli({"compare", "--not-a-flag", fixture("tracer_a.snap.json")});
  CHECK(result.exit_code == 64);
}

TEST_CASE("exit_codes - 64: an unknown subcommand exits exactly 64", "[integration]") {
  CliResult result = run_cli({"not-a-subcommand"});
  CHECK(result.exit_code == 64);
}

TEST_CASE("exit_codes - 64: a malformed --tol value (no unit digits) exits exactly 64", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_clean.snap.json"), "--tol",
                               "meta.tool_version=notanumber"});
  CHECK(result.exit_code == 64);
}

TEST_CASE("exit_codes - 64: a --tol in the wrong unit for its check exits 64 and names the expected unit (CLI-10)",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), fixture("tracer_b_clean.snap.json"), "--tol",
                               "meta.tool_version=5ms"});
  CHECK(result.exit_code == 64);
  CHECK(result.err.find("none") != std::string::npos);
}

TEST_CASE("exit_codes - 65: a nonexistent input path exits exactly 65", "[integration]") {
  CliResult result = run_cli({"compare", fixture("tracer_a.snap.json"), "/no/such/file.snap.json"});
  CHECK(result.exit_code == 65);
}

TEST_CASE("exit_codes - 65: a snapshot with an incompatible schema_version major exits exactly 65",
          "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("schema_major_mismatch.snap.json"), fixture("tracer_b_clean.snap.json")});
  CHECK(result.exit_code == 65);
}

TEST_CASE(
    "exit_codes - 66: a partial fingerprint exits exactly 66 and still emits a parseable JSON report carrying the "
    "partial marker",
    "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("partial_decode.a.snap.json"), fixture("partial_decode.b.snap.json"), "--json"});
  REQUIRE(result.exit_code == 66);

  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("diagnostics"));

  bool found_partial_marker = false;
  for (const auto& line : report.at("diagnostics")) {
    if (line.is_string() && line.get<std::string>().find("partial") != std::string::npos) {
      found_partial_marker = true;
    }
  }
  CHECK(found_partial_marker);
}

TEST_CASE("exit_codes - 70: the not-yet-implemented `dir` callback exits exactly 70", "[integration]") {
  CliResult result = run_cli({"dir", "baseline_dir", "candidate_dir"});
  CHECK(result.exit_code == 70);
}
