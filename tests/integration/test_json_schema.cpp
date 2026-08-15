// docs/schema/report-1.0.json (02-08-PLAN.md Task 1, REPORT-01): the
// shipped JSON schema actually validates a real emitted report AND
// actually rejects a malformed one -- proving the validator has teeth,
// not merely that it was invoked.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string config_fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/config/" + name; }
std::string snap_fixture(const std::string& name) { return mediadiff::test::snapshot_dir() + "/" + name; }

// MEDIADIFF_REPORT_SCHEMA is injected by tests/integration/CMakeLists.txt
// as an absolute path to docs/schema/report-1.0.json, resolved at
// configure time (matching every other *_DIR/*_PATH definition in that
// file) rather than assumed relative to ctest's own working directory.
nlohmann::json load_schema() {
  std::ifstream stream(MEDIADIFF_REPORT_SCHEMA);
  REQUIRE(stream.is_open());
  nlohmann::json schema;
  stream >> schema;
  return schema;
}

nlohmann::json_schema::json_validator make_validator() {
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(load_schema());
  return validator;
}

bool validates(nlohmann::json_schema::json_validator& validator, const nlohmann::json& instance) {
  try {
    validator.validate(instance);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

TEST_CASE("json_schema - a real compare --json report validates against docs/schema/report-1.0.json",
          "[integration]") {
  CliResult result =
      run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  auto validator = make_validator();
  CHECK(validates(validator, report));
}

TEST_CASE("json_schema - a document with an unknown status value FAILS validation, proving the schema has teeth",
          "[integration]") {
  CliResult result =
      run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"), "--json"});
  REQUIRE(result.exit_code == 0);

  nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE_FALSE(report.at("findings").empty());
  report["findings"][0]["status"] = "not_a_real_status";

  auto validator = make_validator();
  CHECK_FALSE(validates(validator, report));
}

TEST_CASE("json_schema - compare --json -v also validates, proving the optional severity_chain declaration works",
          "[integration]") {
  CliResult result = run_cli(
      {"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_skew.snap.json"), "--json", "-v"});
  // meta.tool_version resolves warn (default severity, no override here);
  // --strict was not given, so a worst-warn result is still exit 0.
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  auto validator = make_validator();
  CHECK(validates(validator, report));

  REQUIRE_FALSE(report.at("findings").empty());
  for (const auto& finding : report.at("findings")) {
    REQUIRE(finding.contains("severity_chain"));
    REQUIRE(finding.at("severity_chain").is_array());
    REQUIRE_FALSE(finding.at("severity_chain").empty());
    for (const auto& entry : finding.at("severity_chain")) {
      REQUIRE(entry.contains("layer"));
      REQUIRE(entry.contains("detail"));
      REQUIRE(entry.contains("value"));
      const std::string layer = entry.at("layer").get<std::string>();
      CHECK((layer == "builtin" || layer == "profile" || layer == "config" || layer == "cli"));
      CHECK_FALSE(entry.at("detail").get<std::string>().empty());
    }
  }
}

TEST_CASE(
    "json_schema - a check overridden at config and cli layers carries the exact chain builtin, config, cli, and "
    "the final value equals the finding's own severity",
    "[integration]") {
  // Same worked scenario tests/integration/test_list_checks.cpp's own -v
  // provenance test uses (02-06-PLAN.md Task 3): complete_valid.toml's
  // [severity] table sets meta.tool_version = fail (a config-layer write),
  // then --set adds a cli-layer write with the same value -- proving the
  // JSON severity_chain surface agrees with the text provenance surface on
  // both the layer sequence AND (uniquely checked here) that the chain's
  // last element matches the finding's own resolved `severity` field.
  CliResult result = run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_skew.snap.json"),
                               "--json", "-v", "--config", config_fixture("complete_valid.toml"), "--set",
                               "meta.tool_version=fail"});
  REQUIRE(result.exit_code == 1);  // meta.tool_version now resolves fail

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  const nlohmann::json* tool_version_finding = nullptr;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "meta.tool_version") {
      tool_version_finding = &finding;
      break;
    }
  }
  REQUIRE(tool_version_finding != nullptr);
  CHECK(tool_version_finding->at("severity") == "fail");

  const auto& chain = tool_version_finding->at("severity_chain");
  REQUIRE(chain.size() == 3);
  CHECK(chain.at(0).at("layer") == "builtin");
  CHECK(chain.at(1).at("layer") == "config");
  CHECK(chain.at(2).at("layer") == "cli");
  CHECK(chain.at(2).at("value") == tool_version_finding->at("severity"));
}

TEST_CASE("json_schema - a compare --json run's skip_reason key is present on every finding, even when none",
          "[integration]") {
  CliResult result =
      run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  for (const auto& finding : report.at("findings")) {
    REQUIRE(finding.contains("skip_reason"));
  }
}

TEST_CASE("json_schema - two identical compare --json runs produce byte-identical stdout", "[integration]") {
  const std::vector<std::string> args = {"compare", snap_fixture("tracer_a.snap.json"),
                                          snap_fixture("tracer_b_clean.snap.json"), "--json"};
  CliResult first = run_cli(args);
  CliResult second = run_cli(args);
  REQUIRE(first.exit_code == 0);
  REQUIRE(second.exit_code == 0);
  CHECK(first.out == second.out);

  mediadiff::test::check_golden("json_schema_basic", first.out);
}
