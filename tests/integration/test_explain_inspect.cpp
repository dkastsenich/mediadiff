// ENG-13/DOC-02/REPORT-07: `explain` resolves every registered check id
// (and its declared aliases) to compiled-in documentation; `inspect`
// renders every implemented check family exactly once, byte-stably, and
// `-v` shows the resolved severity chain through the shared
// src/cli/provenance_render.h renderer (02-10-PLAN.md Task 3).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "cli_harness.h"
#include "core/registry.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::builtin_registry;
using mediadiff::CheckRegistry;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURES_DIR) + "/" + name; }

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

TEST_CASE("explain_inspect - ENG-13: explain meta.tool_version exits 0 and carries all five expected labels",
          "[integration]") {
  CliResult result = run_cli({"explain", "meta.tool_version"});
  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("## What it measures") != std::string::npos);
  CHECK(result.out.find("## Why it matters") != std::string::npos);
  CHECK(result.out.find("### Accept") != std::string::npos);
  CHECK(result.out.find("### Tune") != std::string::npos);
  CHECK(result.out.find("### Silence") != std::string::npos);
}

TEST_CASE("explain_inspect - ENG-13: explain on an unresolvable id exits exactly 64 and names the id",
          "[integration]") {
  CliResult result = run_cli({"explain", "meta.tool_version_nope"});
  CHECK(result.exit_code == 64);
  CHECK(result.err.find("meta.tool_version_nope") != std::string::npos);
}

TEST_CASE(
    "explain_inspect - ENG-13/ENG-03: explain on a declared alias exits 0 and prints the deprecation line naming "
    "the current id",
    "[integration]") {
  CliResult result = run_cli({"explain", "meta.version"});
  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("deprecated alias") != std::string::npos);
  CHECK(result.out.find("meta.tool_version") != std::string::npos);
}

TEST_CASE("explain_inspect - ENG-13: explain resolves for every registered check id, none left undocumented",
          "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    const std::string id(registry.at(i).id);
    CliResult result = run_cli({"explain", id});
    CHECK(result.exit_code == 0);
    CHECK_FALSE(result.out.empty());
  }
}

TEST_CASE("explain_inspect - REPORT-07: inspect on a fixture snapshot exits 0 and names every Group exactly once",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("tracer_a.snap.json")});
  REQUIRE(result.exit_code == 0);
  for (const char* label : {"container:", "video:", "timeline:", "audio:", "content:", "size:", "meta:"}) {
    CHECK(count_occurrences(result.out, label) == 1);
  }
}

TEST_CASE(
    "explain_inspect - REPORT-07: inspect on a snapshot with no meta measurements still names meta with the "
    "no-measurements line",
    "[integration]") {
  CliResult result = run_cli({"inspect", fixture("inspect_no_meta.snap.json")});
  REQUIRE(result.exit_code == 0);
  CHECK(count_occurrences(result.out, "meta:") == 1);
  const auto lines = split_lines(result.out);
  bool found_meta_no_measurements = false;
  for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
    if (lines[i] == "meta:" && lines[i + 1] == "  (no measurements)") {
      found_meta_no_measurements = true;
    }
  }
  CHECK(found_meta_no_measurements);
}

TEST_CASE("explain_inspect - REPORT-07: two inspect runs produce byte-identical stdout, checked against a golden",
          "[integration]") {
  CliResult first = run_cli({"inspect", fixture("tracer_a.snap.json")});
  CliResult second = run_cli({"inspect", fixture("tracer_a.snap.json")});
  REQUIRE(first.exit_code == 0);
  REQUIRE(second.exit_code == 0);
  CHECK(first.out == second.out);

  mediadiff::test::check_golden("inspect_basic", first.out);
}

TEST_CASE("explain_inspect - REPORT-07: inspect on a non-snapshot file exits exactly 65", "[integration]") {
  CliResult result = run_cli({"inspect", mediadiff::test::fixture_dir() + "/config/empty.toml"});
  CHECK(result.exit_code == 65);
}

TEST_CASE(
    "explain_inspect - ENG-06: inspect -v --set shows the resolved severity chain, ending with cli and carrying "
    "the overridden value; without -v the same command prints strictly fewer lines",
    "[integration]") {
  CliResult verbose = run_cli(
      {"inspect", fixture("tracer_a.snap.json"), "-v", "--set", "meta.tool_version=fail"});
  CliResult quiet = run_cli({"inspect", fixture("tracer_a.snap.json"), "--set", "meta.tool_version=fail"});
  REQUIRE(verbose.exit_code == 0);
  REQUIRE(quiet.exit_code == 0);

  const auto verbose_lines = split_lines(verbose.out);
  const auto quiet_lines = split_lines(quiet.out);
  CHECK(verbose_lines.size() > quiet_lines.size());

  // Collect every indented (chain) line following the meta.tool_version
  // measurement row.
  std::size_t row_index = verbose_lines.size();
  for (std::size_t i = 0; i < verbose_lines.size(); ++i) {
    if (verbose_lines[i].rfind("  meta.tool_version ", 0) == 0) {
      row_index = i;
      break;
    }
  }
  REQUIRE(row_index < verbose_lines.size());

  std::vector<std::string> chain_lines;
  for (std::size_t i = row_index + 1;
       i < verbose_lines.size() && verbose_lines[i].size() > 4 && verbose_lines[i].substr(0, 4) == "    "; ++i) {
    chain_lines.push_back(verbose_lines[i]);
  }
  REQUIRE_FALSE(chain_lines.empty());
  CHECK(chain_lines.back().find("cli") != std::string::npos);
  CHECK(chain_lines.back().find("fail") != std::string::npos);
}
