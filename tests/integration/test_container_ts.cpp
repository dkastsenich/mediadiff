// 03-08-PLAN.md Tasks 1-2: the six container.ts.* checks (CONT-07), proven
// through the real CLI -- cc_errors/cc_discontinuities (kept independent),
// pcr_interval/psi_interval (D-03 estimated-measurement widening),
// pmt_version_churn, null_ratio (exact ratio), cross-format skip
// correctness, and the incomplete-walk skip. Mirrors
// tests/integration/test_container_mp4.cpp/test_container_mkv.cpp's own
// established pattern. CONT-08's multi-program program-number pairing is
// its own file, tests/integration/test_multiprogram.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_container_ts";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::string write_scratch(const std::string& name, const std::string& bytes) {
  const fs::path path = scratch_dir() / name;
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.is_open());
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return path.string();
}

std::string read_whole(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

const nlohmann::ordered_json* find_group_entry(const nlohmann::ordered_json& doc, const std::string& id) {
  for (const auto& entry : doc.at("groups").at("container")) {
    if (entry.at("id") == id) {
      return &entry;
    }
  }
  return nullptr;
}

const std::vector<std::string> kTsCheckIds = {
    "container.ts.cc_errors",          "container.ts.cc_discontinuities", "container.ts.pcr_interval",
    "container.ts.psi_interval",       "container.ts.pmt_version_churn",  "container.ts.null_ratio",
};

}  // namespace

// --- Test 1/2: container.ts.cc_errors ---------------------------------------

TEST_CASE("container_ts - cc_errors matches (0) between two byte-identical files, fails on a real continuity gap",
          "[integration]") {
  {
    CliResult clean = run_cli({"compare", fixture("ts_single.ts"), fixture("ts_single_copy.ts"), "--json"});
    REQUIRE(clean.exit_code == 0);
    const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(clean.out, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    const auto* finding = find_finding(doc, "container.ts.cc_errors");
    REQUIRE(finding != nullptr);
    REQUIRE(finding->at("baseline") == 0);
    REQUIRE(finding->at("candidate") == 0);
    REQUIRE(finding->at("status") == "pass");
  }
  {
    CliResult gap = run_cli({"compare", fixture("ts_single.ts"), fixture("ts_ccgap.ts"), "--json"});
    REQUIRE(gap.exit_code != 0);
    const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(gap.out, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    const auto* finding = find_finding(doc, "container.ts.cc_errors");
    REQUIRE(finding != nullptr);
    REQUIRE(finding->at("status") == "fail");
    REQUIRE(finding->at("candidate").get<std::int64_t>() > 0);
    REQUIRE(finding->at("evidence").at("candidate").contains("per_pid_errors"));
    REQUIRE(finding->at("evidence").at("candidate").contains("first_cc_error_offset"));
  }
}

// --- Test 3: container.ts.cc_discontinuities never conflated ---------------

TEST_CASE("container_ts - cc_discontinuities is non-zero on a flagged-discontinuity file while cc_errors stays "
          "0 on that SAME file",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("ts_discontinuity.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const auto* errors = find_group_entry(doc, "container.ts.cc_errors");
  const auto* discontinuities = find_group_entry(doc, "container.ts.cc_discontinuities");
  REQUIRE(errors != nullptr);
  REQUIRE(discontinuities != nullptr);
  REQUIRE(errors->at("value") == 0);
  REQUIRE(discontinuities->at("value").get<std::int64_t>() > 0);
}

// --- Test 4/5: container.ts.pcr_interval (D-03 widening) --------------------

TEST_CASE("container_ts - pcr_interval passes within the widened tolerance and fails beyond it -- the widening "
          "is bounded, not a bypass",
          "[integration]") {
  {
    CliResult close = run_cli({"compare", fixture("ts_pcr_close_a.ts"), fixture("ts_pcr_close_b.ts"), "--json"});
    REQUIRE(close.exit_code == 0);
    const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(close.out, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    const auto* finding = find_finding(doc, "container.ts.pcr_interval");
    REQUIRE(finding != nullptr);
    REQUIRE(finding->at("status") == "pass");
    REQUIRE(finding->at("message").get<std::string>().find("estimated") != std::string::npos);
  }
  {
    // Not named `far`: the Windows SDK headers <windows.h> transitively pulls in
    // (via tests/process_spawn.h's _WIN32 CreateProcess path) define `far` as an
    // empty legacy 16-bit-compatibility macro, which silently erases the
    // identifier and breaks this declaration on MSVC only (WINDOWS.md, 03-21).
    CliResult pcr_far = run_cli({"compare", fixture("ts_pcr_far_a.ts"), fixture("ts_pcr_far_b.ts"), "--json"});
    REQUIRE(pcr_far.exit_code != 0);
    const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(pcr_far.out, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    const auto* finding = find_finding(doc, "container.ts.pcr_interval");
    REQUIRE(finding != nullptr);
    REQUIRE(finding->at("status") == "fail");
  }
}

TEST_CASE("container_ts - pcr_interval is skipped:insufficient_data (not a numeric value) on a file with fewer "
          "than two PCRs",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("ts_single_pcr.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const auto* entry = find_group_entry(doc, "container.ts.pcr_interval");
  REQUIRE(entry != nullptr);
  REQUIRE(entry->at("status") == "skipped");
  REQUIRE(entry->at("skip_reason") == "insufficient_data");
  REQUIRE(entry->at("value").is_null());
}

// --- Test 6: container.ts.null_ratio is num/den, not a pre-divided number --

TEST_CASE("container_ts - null_ratio's --json value is a num/den object", "[integration]") {
  CliResult result = run_cli({"inspect", fixture("ts_single.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const auto* entry = find_group_entry(doc, "container.ts.null_ratio");
  REQUIRE(entry != nullptr);
  REQUIRE(entry->at("value").contains("num"));
  REQUIRE(entry->at("value").contains("den"));
  REQUIRE_FALSE(entry->at("value").is_number());
}

// --- Test 7: container.ts.pmt_version_churn ---------------------------------

TEST_CASE("container_ts - pmt_version_churn is 0 and matches between two byte-identical files", "[integration]") {
  CliResult result = run_cli({"compare", fixture("ts_single.ts"), fixture("ts_single_copy.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const auto* finding = find_finding(doc, "container.ts.pmt_version_churn");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("baseline") == 0);
  REQUIRE(finding->at("status") == "pass");
}

// --- Test 8: all six skip as not_applicable_container on MP4/MKV -----------

TEST_CASE("container_ts - all six container.ts.* checks are skipped:not_applicable_container on an MP4 input, "
          "none is pass",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("mp4_faststart.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  for (const std::string& id : kTsCheckIds) {
    const auto* entry = find_group_entry(doc, id);
    INFO("missing container.ts.* entry: " << id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->at("status") == "skipped");
    REQUIRE(entry->at("skip_reason") == "not_applicable_container");
  }
}

TEST_CASE("container_ts - all six container.ts.* checks are skipped:not_applicable_container on an MKV input, "
          "none is pass",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("tracer_a.mkv"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  for (const std::string& id : kTsCheckIds) {
    const auto* entry = find_group_entry(doc, id);
    INFO("missing container.ts.* entry: " << id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->at("status") == "skipped");
    REQUIRE(entry->at("skip_reason") == "not_applicable_container");
  }
}

// --- Test 9: an incomplete ts_scan walk skips every check with an offset ---

TEST_CASE("container_ts - a truncated TS whose ts_scan is incomplete yields all six as "
          "skipped:unparsed_mechanism with stop_offset in evidence",
          "[integration]") {
  // Real leading packets (enough for libav's own mpegts probe and for
  // ts_scan's 5-sync-confirmation stride detection) followed by a run of
  // zero bytes with no sync byte at all -- ts_scan's forward resync search
  // runs off the end of the file, the OTHER documented complete=false
  // trigger (ts_scan.h's own comment), matching
  // tests/unit/test_ts_analyzer.cpp's identical construction. Two
  // IDENTICAL scratch copies so BOTH sides of the pair carry the SAME
  // global-scope skip at all six check ids (mirrors
  // test_container_mkv.cpp's own precedent for the identical pairing
  // problem: the per-program checks' scope would otherwise differ between
  // an incomplete-walk side (global) and a real-data side (program[N]),
  // leaving them unpaired and silently dropped from `findings`).
  const std::string full_bytes = read_whole(fixture("ts_single.ts"));
  REQUIRE(full_bytes.size() >= 188 * 30);
  std::string prefix = full_bytes.substr(0, 188 * 30);
  prefix.append(5000, '\x00');
  const std::string path_a = write_scratch("resync_runoff_a.ts", prefix);
  const std::string path_b = write_scratch("resync_runoff_b.ts", prefix);

  CliResult result = run_cli({"compare", path_a, path_b, "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  for (const std::string& id : kTsCheckIds) {
    const auto* finding = find_finding(report, id);
    INFO("missing container.ts.* finding: " << id);
    REQUIRE(finding != nullptr);
    REQUIRE(finding->at("status") == "skipped");
    REQUIRE(finding->at("skip_reason") == "unparsed_mechanism");
    REQUIRE(finding->at("evidence").at("baseline").contains("stop_offset"));
  }
}
