// `dir` mode end to end (doc 01 section 10, DIR-01..05, 02-11-PLAN.md
// Task 3): pairing, unpaired findings, thread-count determinism, the
// corpus `files[]` JSON layer against the extended schema, the per-file
// Markdown table, one JUnit `<testsuite>` per file, and the TTY worst-N
// table.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/golden.h"

using mediadiff::test::CliResult;
using mediadiff::test::check_golden;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir(const std::string& tag) {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir = fs::temp_directory_path() /
                        ("mediadiff_dir_mode_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::string snap_fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURES_DIR) + "/" + name; }

void copy_fixture(const std::string& fixture_name, const fs::path& dest) {
  std::ifstream in(snap_fixture(fixture_name), std::ios::binary);
  REQUIRE(in.is_open());
  std::ofstream out(dest, std::ios::binary);
  out << in.rdbuf();
}

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

}  // namespace

TEST_CASE("dir_mode - identical trees exit 0 with zero non-pass findings", "[integration]") {
  const fs::path baseline = unique_scratch_dir("identical_a");
  const fs::path candidate = unique_scratch_dir("identical_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "one.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "two.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "two.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto& summary = report.at("summary");
  CHECK(summary.at("warn").get<int>() == 0);
  CHECK(summary.at("fail").get<int>() == 0);
  CHECK(summary.at("skipped").get<int>() == 0);
  CHECK(summary.at("error").get<int>() == 0);
  CHECK(summary.at("pass").get<int>() > 0);
}

TEST_CASE(
    "dir_mode - one unpaired file each way exits 1 and the files[] array contains both meta findings under the "
    "right relative paths",
    "[integration]") {
  const fs::path baseline = unique_scratch_dir("unpaired_a");
  const fs::path candidate = unique_scratch_dir("unpaired_b");
  copy_fixture("tracer_a.snap.json", baseline / "shared.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "shared.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "only_candidate.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 1);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("files"));

  bool found_missing = false;
  bool found_extra = false;
  for (const auto& file_block : report.at("files")) {
    const std::string relative_path = file_block.at("relative_path").get<std::string>();
    for (const auto& finding : file_block.at("findings")) {
      const std::string id = finding.at("id").get<std::string>();
      if (id == "meta.missing_candidate") {
        CHECK(relative_path == "only_baseline.snap.json");
        found_missing = true;
      } else if (id == "meta.extra_candidate") {
        CHECK(relative_path == "only_candidate.snap.json");
        found_extra = true;
      }
    }
  }
  CHECK(found_missing);
  CHECK(found_extra);
}

TEST_CASE("dir_mode - --threads 1 and --threads 8 produce byte-identical --json on a multi-file corpus",
          "[integration]") {
  const fs::path baseline = unique_scratch_dir("threads_a");
  const fs::path candidate = unique_scratch_dir("threads_b");
  for (int i = 0; i < 10; ++i) {
    const std::string name = "file_" + std::to_string(i) + ".snap.json";
    copy_fixture(i % 2 == 0 ? "tracer_a.snap.json" : "tracer_b_clean.snap.json", baseline / name);
    copy_fixture("tracer_b_clean.snap.json", candidate / name);
  }

  CliResult result_1 = run_cli({"dir", baseline.string(), candidate.string(), "--threads", "1", "--json"});
  CliResult result_8 = run_cli({"dir", baseline.string(), candidate.string(), "--threads", "8", "--json"});

  REQUIRE(result_1.exit_code == result_8.exit_code);
  CHECK(result_1.out == result_8.out);
}

TEST_CASE("dir_mode - files[] is in byte-wise sorted relative-path order", "[integration]") {
  const fs::path baseline = unique_scratch_dir("sorted_a");
  const fs::path candidate = unique_scratch_dir("sorted_b");
  const std::vector<std::string> names = {"zeta.snap.json", "alpha.snap.json", "Beta.snap.json", "1.snap.json"};
  for (const std::string& name : names) {
    copy_fixture("tracer_a.snap.json", baseline / name);
    copy_fixture("tracer_a.snap.json", candidate / name);
  }

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  std::vector<std::string> actual_order;
  for (const auto& file_block : report.at("files")) {
    actual_order.push_back(file_block.at("relative_path").get<std::string>());
  }
  std::vector<std::string> expected_order = actual_order;
  std::sort(expected_order.begin(), expected_order.end());
  CHECK(actual_order == expected_order);
}

TEST_CASE("dir_mode - two empty roots exit 0 with an empty files[] array", "[integration]") {
  const fs::path baseline = unique_scratch_dir("empty_a");
  const fs::path candidate = unique_scratch_dir("empty_b");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("files"));
  CHECK(report.at("files").empty());
}

TEST_CASE("dir_mode - the corpus JSON document validates against docs/schema/report-1.0.json", "[integration]") {
  const fs::path baseline = unique_scratch_dir("schema_a");
  const fs::path candidate = unique_scratch_dir("schema_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_b_skew.snap.json", candidate / "one.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json", "-v"});

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("files"));
  REQUIRE_FALSE(report.contains("findings"));

  nlohmann::json_schema::json_validator validator = make_validator();
  bool valid = true;
  try {
    validator.validate(report);
  } catch (const std::exception& e) {
    valid = false;
    INFO("schema validation error: " << e.what());
  }
  CHECK(valid);
}

TEST_CASE("dir_mode - corpus totals equal the element-wise sum of the per-file summaries", "[integration]") {
  const fs::path baseline = unique_scratch_dir("totals_a");
  const fs::path candidate = unique_scratch_dir("totals_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_b_skew.snap.json", candidate / "one.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "only_candidate.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  int expected_pass = 0, expected_info = 0, expected_warn = 0, expected_fail = 0, expected_skipped = 0,
      expected_error = 0;
  for (const auto& file_block : report.at("files")) {
    const auto& s = file_block.at("summary");
    expected_pass += s.at("pass").get<int>();
    expected_info += s.at("info").get<int>();
    expected_warn += s.at("warn").get<int>();
    expected_fail += s.at("fail").get<int>();
    expected_skipped += s.at("skipped").get<int>();
    expected_error += s.at("error").get<int>();
  }

  const auto& totals = report.at("summary");
  CHECK(totals.at("pass").get<int>() == expected_pass);
  CHECK(totals.at("info").get<int>() == expected_info);
  CHECK(totals.at("warn").get<int>() == expected_warn);
  CHECK(totals.at("fail").get<int>() == expected_fail);
  CHECK(totals.at("skipped").get<int>() == expected_skipped);
  CHECK(totals.at("error").get<int>() == expected_error);
}

TEST_CASE("dir_mode - the TTY worst-N table lists the worst-severity file first with ties broken by path",
          "[integration]") {
  const fs::path baseline = unique_scratch_dir("worst_n_a");
  const fs::path candidate = unique_scratch_dir("worst_n_b");
  copy_fixture("tracer_a.snap.json", baseline / "m_missing.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "x_extra.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "z_clean.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "z_clean.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--no-color"});
  check_golden("dir_worst_n", result.out);
}

TEST_CASE("dir_mode - JUnit emits one testsuite per file named by relative path", "[integration]") {
  const fs::path baseline = unique_scratch_dir("junit_a");
  const fs::path candidate = unique_scratch_dir("junit_b");
  const fs::path junit_path = unique_scratch_dir("junit_out") / "report.xml";
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "only_candidate.snap.json");

  CliResult result =
      run_cli({"dir", baseline.string(), candidate.string(), "--report", "junit=" + junit_path.string()});
  REQUIRE(result.exit_code == 1);

  std::ifstream in(junit_path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buf;
  buf << in.rdbuf();
  const std::string xml = buf.str();

  CHECK(xml.find("<testsuite name=\"only_baseline.snap.json\"") != std::string::npos);
  CHECK(xml.find("<testsuite name=\"only_candidate.snap.json\"") != std::string::npos);
}
