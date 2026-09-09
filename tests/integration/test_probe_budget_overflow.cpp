// 03-12-PLAN.md Task 3 (SIZE-01/DIR-06): the two reproductions recorded in
// 03-VERIFICATION.md's "Behavioral Spot-Checks" table, inverted permanently
// -- an extreme but CLI11-legal --probe-memory-budget-mb/--probe-timeout
// value must exit 64 with a diagnostic naming the maximum, never exit 0
// with size.stream_bitrate/size.peak_bitrate/size.overhead silently
// blanked to skipped:partial_scan, and never exit 65 with a spurious
// wall-clock timeout message. Asserts on the PARSED --json document, not a
// text grep, per this task's own action text -- the load-bearing property
// is "no size.* finding was reduced to a partial_scan skip", which a
// substring search on rendered text could miss if a future change alters
// how a skip renders.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// The extreme-but-CLI11-legal reproduction values from 03-VERIFICATION.md's
// spot-check table: both are ordinary positive integers CLI11's own
// NonNegativeNumber/PositiveNumber validators accept, and both are large
// enough to overflow an unchecked `* 1024 * 1024` / `* 1000` product.
constexpr const char* kExtremeMemoryBudgetMb = "8796093022208";
constexpr const char* kExtremeTimeoutSeconds = "9223372036854776";

// kMaxProbeMemoryBudgetMb/kMaxProbeTimeoutSeconds (src/config/toml_load.h),
// duplicated here as plain text rather than included from production code
// -- this test asserts on the USER-VISIBLE diagnostic text, which should
// match the bound regardless of which internal constant produced it.
constexpr const char* kMaxMemoryBudgetMbText = "1048576";
constexpr const char* kMaxTimeoutSecondsText = "86400";

// Scans a compare --json document's flat `findings` array for any size.*
// finding reduced to skipped/partial_scan -- the exact false-negative
// 03-VERIFICATION.md reproduced (SC3/SC4). Returns the offending check id
// so a failing assertion names which half broke, per this task's own
// action text ("name the offending check id and its skip reason").
std::string find_partial_scan_size_finding(const nlohmann::ordered_json& doc) {
  if (!doc.contains("findings")) {
    return {};
  }
  for (const auto& finding : doc.at("findings")) {
    const std::string id = finding.at("id").get<std::string>();
    if (id.rfind("size.", 0) != 0) {
      continue;
    }
    const std::string status = finding.at("status").get<std::string>();
    const std::string skip_reason = finding.at("skip_reason").get<std::string>();
    if (status == "skipped" && skip_reason == "partial_scan") {
      return id + " (skip_reason=" + skip_reason + ")";
    }
  }
  return {};
}

}  // namespace

// --- Test 1/2: an extreme --probe-memory-budget-mb never silently blanks
//     the size.* family ------------------------------------------------

TEST_CASE("probe_budget_overflow - an extreme --probe-memory-budget-mb exits 64, never 0, naming the maximum",
          "[integration]") {
  const std::string path = fixture("mp4_faststart.mp4");
  CliResult result =
      run_cli({"compare", "--probe-memory-budget-mb", kExtremeMemoryBudgetMb, path, path, "--json"});

  INFO("stderr: " << result.err);
  INFO("stdout: " << result.out);
  REQUIRE(result.exit_code == 64);
  CHECK(result.err.find(kMaxMemoryBudgetMbText) != std::string::npos);

  // Behavior 2: never a report in which a size.* finding was reduced to
  // skipped:partial_scan -- asserted on the actual output (there is none
  // on the exit-64 path; this also proves stdout carries no fabricated
  // report at all, matching every other usage-error path's contract).
  CHECK(result.out.find("partial_scan") == std::string::npos);
}

// --- Test 3: the accepted maximum still produces a real verdict, with
//     size.stream_bitrate NOT skipped -----------------------------------

TEST_CASE("probe_budget_overflow - the accepted maximum --probe-memory-budget-mb still reports real size.* findings",
          "[integration]") {
  const std::string path = fixture("mp4_faststart.mp4");
  CliResult result =
      run_cli({"compare", "--probe-memory-budget-mb", kMaxMemoryBudgetMbText, path, path, "--json"});

  INFO("stderr: " << result.err);
  REQUIRE((result.exit_code == 0 || result.exit_code == 1));

  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  const std::string offender = find_partial_scan_size_finding(doc);
  INFO("size.* finding silently blanked to partial_scan: " << offender);
  CHECK(offender.empty());

  bool saw_stream_bitrate = false;
  for (const auto& finding : doc.at("findings")) {
    if (finding.at("id").get<std::string>() == "size.stream_bitrate") {
      saw_stream_bitrate = true;
      CHECK(finding.at("skip_reason").get<std::string>() != "partial_scan");
    }
  }
  CHECK(saw_stream_bitrate);
}

// --- Test 4: an extreme --probe-timeout exits 64, never the spurious 65
//     wall-clock timeout the unchecked product used to produce ---------

TEST_CASE("probe_budget_overflow - an extreme --probe-timeout exits 64, not 65, naming the maximum",
          "[integration]") {
  const std::string path = fixture("mp4_faststart.mp4");
  CliResult result = run_cli({"compare", "--probe-timeout", kExtremeTimeoutSeconds, path, path});

  INFO("stderr: " << result.err);
  REQUIRE(result.exit_code == 64);
  CHECK(result.exit_code != 65);
  CHECK(result.err.find(kMaxTimeoutSecondsText) != std::string::npos);
  // The pre-fix reproduction's own diagnostic claimed a wall-clock budget
  // had already been exceeded -- the fixed diagnostic must not repeat that
  // false claim.
  CHECK(result.err.find("exceeded") == std::string::npos);
}

// --- Test 5: all four command entry points reject the same extreme
//     budget with the same exit code -------------------------------------

TEST_CASE("probe_budget_overflow - inspect rejects an extreme --probe-memory-budget-mb with exit 64",
          "[integration]") {
  const std::string path = fixture("mp4_faststart.mp4");
  CliResult result = run_cli({"inspect", "--probe-memory-budget-mb", kExtremeMemoryBudgetMb, path});
  INFO("stderr: " << result.err);
  REQUIRE(result.exit_code == 64);
  CHECK(result.err.find(kMaxMemoryBudgetMbText) != std::string::npos);
}

TEST_CASE("probe_budget_overflow - snapshot rejects an extreme --probe-memory-budget-mb with exit 64",
          "[integration]") {
  const std::string path = fixture("mp4_faststart.mp4");
  CliResult result = run_cli({"snapshot", "--probe-memory-budget-mb", kExtremeMemoryBudgetMb, path, "--out",
                               "/dev/null"});
  INFO("stderr: " << result.err);
  REQUIRE(result.exit_code == 64);
  CHECK(result.err.find(kMaxMemoryBudgetMbText) != std::string::npos);
}

TEST_CASE("probe_budget_overflow - dir rejects an extreme --probe-memory-budget-mb with exit 64",
          "[integration]") {
  // Any existing directory works -- CLI11's own ->check(CLI::Range(...))
  // rejects the flag at parse time, before either directory is walked, so
  // the two positional arguments here never need to be a real corpus pair.
  const std::string dir_path = mediadiff::test::fixture_dir();
  CliResult result = run_cli({"dir", "--probe-memory-budget-mb", kExtremeMemoryBudgetMb, dir_path, dir_path});
  INFO("stderr: " << result.err);
  REQUIRE(result.exit_code == 64);
  CHECK(result.err.find(kMaxMemoryBudgetMbText) != std::string::npos);
}

// --- Test 6: an ordinary budget still produces real size.* findings
//     (the nominal path is untouched) ------------------------------------

TEST_CASE("probe_budget_overflow - an ordinary --probe-memory-budget-mb 64 still reports real size.* findings",
          "[integration]") {
  const std::string path = fixture("mp4_faststart.mp4");
  CliResult result = run_cli({"compare", "--probe-memory-budget-mb", "64", path, path, "--json"});

  INFO("stderr: " << result.err);
  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("partial_scan") == std::string::npos);

  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const std::string offender = find_partial_scan_size_finding(doc);
  INFO("size.* finding silently blanked to partial_scan: " << offender);
  CHECK(offender.empty());
}
