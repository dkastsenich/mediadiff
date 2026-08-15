// CLI-04: `--json[=path]` and repeatable `--report kind=path` write
// independent destinations, and every malformed combination is a usage
// error (exit 64) named against the offending argument (02-08-PLAN.md
// Task 3).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir(const std::string& tag) {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir = fs::temp_directory_path() /
                        ("mediadiff_report_flags_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

std::string snap_fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURES_DIR) + "/" + name; }

}  // namespace

TEST_CASE("report_flags - --json=path, --report md=path and --report junit=path write three distinct files",
          "[integration]") {
  const fs::path dir = unique_scratch_dir("three_files");
  const fs::path json_path = dir / "a.json";
  const fs::path md_path = dir / "b.md";
  const fs::path junit_path = dir / "c.xml";

  CliResult result = run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"),
                               "--json=" + json_path.string(), "--report", "md=" + md_path.string(), "--report",
                               "junit=" + junit_path.string()});
  REQUIRE(result.exit_code == 0);

  REQUIRE(fs::exists(json_path));
  REQUIRE(fs::exists(md_path));
  REQUIRE(fs::exists(junit_path));

  const std::string json_content = read_file(json_path);
  const std::string md_content = read_file(md_path);
  const std::string junit_content = read_file(junit_path);

  CHECK_FALSE(json_content.empty());
  CHECK_FALSE(md_content.empty());
  CHECK_FALSE(junit_content.empty());

  const nlohmann::json parsed = nlohmann::json::parse(json_content, nullptr, false);
  CHECK_FALSE(parsed.is_discarded());
  CHECK(md_content.find("# mediadiff report") != std::string::npos);
  CHECK(junit_content.find("<?xml") != std::string::npos);

  // stdout stays free of the JSON report once --json was given a path.
  CHECK(result.out.find("schema_version") == std::string::npos);
}

TEST_CASE("report_flags - bare --json writes the report to stdout", "[integration]") {
  CliResult result =
      run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json parsed = nlohmann::json::parse(result.out, nullptr, false);
  CHECK_FALSE(parsed.is_discarded());
}

TEST_CASE("report_flags - an unrecognized --report kind exits 64", "[integration]") {
  const fs::path dir = unique_scratch_dir("bad_kind");
  CliResult result =
      run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"), "--report",
                "sarif=" + (dir / "x").string()});
  CHECK(result.exit_code == 64);
}

TEST_CASE("report_flags - two --report flags naming the same path exit 64", "[integration]") {
  const fs::path dir = unique_scratch_dir("dup_path");
  const fs::path same_path = dir / "same.txt";
  CliResult result = run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"),
                               "--report", "md=" + same_path.string(), "--report", "junit=" + same_path.string()});
  CHECK(result.exit_code == 64);
}

TEST_CASE("report_flags - --json and --report naming the same path exit 64", "[integration]") {
  const fs::path dir = unique_scratch_dir("json_dup_path");
  const fs::path same_path = dir / "same.out";
  CliResult result = run_cli({"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"),
                               "--json=" + same_path.string(), "--report", "md=" + same_path.string()});
  CHECK(result.exit_code == 64);
}

TEST_CASE("report_flags - a malformed --report argument (no '=') exits 64", "[integration]") {
  CliResult result = run_cli(
      {"compare", snap_fixture("tracer_a.snap.json"), snap_fixture("tracer_b_clean.snap.json"), "--report", "md"});
  CHECK(result.exit_code == 64);
}
