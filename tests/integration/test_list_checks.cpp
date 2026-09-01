// `list-checks` and the `-v` provenance chain (02-06-PLAN.md Task 3, doc 01
// sections 4/6, ENG-06/ENG-12).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "cli_harness.h"
#include "core/check_id.h"
#include "core/registry.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::builtin_registry;
using mediadiff::kCheckIdStrings;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

bool is_indented(const std::string& line) { return !line.empty() && line[0] == ' '; }

// A "row" line names a check id first; a chain line is indented. The row's
// check id is everything before the first space (no registered check id
// contains one).
std::string row_check_id(const std::string& row) { return row.substr(0, row.find(' ')); }

// A chain line's layer name is its first whitespace-delimited token once
// leading indent is stripped.
std::string chain_layer_name(const std::string& chain_line) {
  std::size_t begin = 0;
  while (begin < chain_line.size() && chain_line[begin] == ' ') {
    ++begin;
  }
  std::size_t end = begin;
  while (end < chain_line.size() && chain_line[end] != ' ') {
    ++end;
  }
  return chain_line.substr(begin, end - begin);
}

std::string config_fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/config/" + name; }

}  // namespace

TEST_CASE("list_checks - ENG-01: list-checks with no flags prints exactly registry.size() non-empty lines",
          "[integration]") {
  CliResult result = run_cli({"list-checks"});
  REQUIRE(result.exit_code == 0);

  const std::vector<std::string> lines = split_lines(result.out);
  CHECK(lines.size() == builtin_registry().size());
  for (const std::string& line : lines) {
    CHECK_FALSE(line.empty());
  }
}

TEST_CASE("list_checks - ENG-12: --effective with no config prints one row per check, each carrying a severity",
          "[integration]") {
  CliResult result = run_cli({"list-checks", "--effective"});
  REQUIRE(result.exit_code == 0);

  const std::vector<std::string> lines = split_lines(result.out);
  REQUIRE(lines.size() == builtin_registry().size());
  for (const std::string& line : lines) {
    CHECK(line.find("severity=") != std::string::npos);
  }
}

TEST_CASE("list_checks - ENG-12: --effective is byte-identical across two runs, checked against a golden",
          "[integration]") {
  CliResult first = run_cli({"list-checks", "--effective"});
  CliResult second = run_cli({"list-checks", "--effective"});
  REQUIRE(first.exit_code == 0);
  REQUIRE(second.exit_code == 0);
  CHECK(first.out == second.out);

  mediadiff::test::check_golden("list_checks_effective", first.out);
}

TEST_CASE("list_checks - ENG-01: row order matches registry declaration order", "[integration]") {
  CliResult result = run_cli({"list-checks"});
  REQUIRE(result.exit_code == 0);

  const std::vector<std::string> lines = split_lines(result.out);
  REQUIRE(lines.size() == builtin_registry().size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    CHECK(row_check_id(lines[i]) == kCheckIdStrings[i]);
  }
}

TEST_CASE(
    "list_checks - ENG-06: the resolved chain under -v names builtin, config, then cli in resolution order, with "
    "every field non-empty",
    "[integration]") {
  CliResult result = run_cli({"list-checks", "--effective", "--config", config_fixture("complete_valid.toml"),
                               "--set", "meta.tool_version=fail", "-v"});
  REQUIRE(result.exit_code == 0);

  const std::vector<std::string> lines = split_lines(result.out);

  // Find the meta.tool_version row and collect every indented line that
  // follows it, up to (but excluding) the next row.
  std::size_t row_index = lines.size();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (!is_indented(lines[i]) && row_check_id(lines[i]) == "meta.tool_version") {
      row_index = i;
      break;
    }
  }
  REQUIRE(row_index < lines.size());
  CHECK(lines[row_index].find("severity=fail") != std::string::npos);

  std::vector<std::string> chain_lines;
  for (std::size_t i = row_index + 1; i < lines.size() && is_indented(lines[i]); ++i) {
    chain_lines.push_back(lines[i]);
  }

  REQUIRE(chain_lines.size() == 3);
  CHECK(chain_layer_name(chain_lines[0]) == "builtin");
  CHECK(chain_layer_name(chain_lines[1]) == "config");
  CHECK(chain_layer_name(chain_lines[2]) == "cli");

  // The builtin line carries checks.def's baseline severity (warn); the
  // cli line carries the resolved value the row itself also shows (fail).
  // Every line carries a non-empty parenthesized detail.
  CHECK(chain_lines[0].find("warn") != std::string::npos);
  CHECK(chain_lines[2].find("fail") != std::string::npos);
  for (const std::string& line : chain_lines) {
    const std::size_t open = line.find('(');
    const std::size_t close = line.find(')');
    REQUIRE(open != std::string::npos);
    REQUIRE(close != std::string::npos);
    CHECK(close > open + 1);  // non-empty detail between the parentheses
  }
}

TEST_CASE("list_checks - ENG-06: -v strictly increases line count over the non-verbose --effective output",
          "[integration]") {
  CliResult without_v = run_cli({"list-checks", "--effective"});
  CliResult with_v = run_cli({"list-checks", "--effective", "-v"});
  REQUIRE(without_v.exit_code == 0);
  REQUIRE(with_v.exit_code == 0);

  const std::size_t without_v_lines = split_lines(without_v.out).size();
  const std::size_t with_v_lines = split_lines(with_v.out).size();
  CHECK(without_v_lines == builtin_registry().size());
  CHECK(with_v_lines > without_v_lines);
}
