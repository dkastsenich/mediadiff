// 03-08-PLAN.md Task 3 (CONT-08, doc 02 section 6): program-scoped
// container.ts.* measurements are keyed by the PSI program_number
// (Scope{Kind::program, index} where `index` IS the program_number, never
// an AVFormatContext::programs[] array position) -- proven through the
// real CLI against a two-program TS, a reordered-declaration sibling (same
// program numbers, different on-wire PAT entry order), and a renumbered
// sibling (a genuine topology mismatch: programs {1,2} vs {1,3}).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::vector<const nlohmann::ordered_json*> find_all_group_entries(const nlohmann::ordered_json& doc,
                                                                    const std::string& id) {
  std::vector<const nlohmann::ordered_json*> out;
  for (const auto& entry : doc.at("groups").at("container")) {
    if (entry.at("id") == id) {
      out.push_back(&entry);
    }
  }
  return out;
}

std::vector<const nlohmann::ordered_json*> find_all_findings(const nlohmann::ordered_json& report,
                                                               const std::string& id) {
  std::vector<const nlohmann::ordered_json*> out;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      out.push_back(&finding);
    }
  }
  return out;
}

bool scope_is_program(const nlohmann::ordered_json& scope, int index) {
  return scope.at("kind") == "program" && scope.at("index") == index;
}

const std::vector<std::string> kProgramScopedIds = {
    "container.ts.pcr_interval",
    "container.ts.psi_interval",
    "container.ts.pmt_version_churn",
};

}  // namespace

// --- Test 1/5/6: scope index equals the PSI program_number -----------------

TEST_CASE("multiprogram - a two-program TS emits program-scoped checks at Scope{program, 1} and "
          "Scope{program, 2} -- the fixture's own PAT program numbers, confirmed via ffprobe -show_programs "
          "independently of this implementation",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("ts_multiprogram.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  for (const std::string& id : kProgramScopedIds) {
    const auto entries = find_all_group_entries(doc, id);
    INFO("check: " << id);
    REQUIRE(entries.size() == 2);
    bool saw_program_1 = false;
    bool saw_program_2 = false;
    for (const auto* entry : entries) {
      // `inspect --json` renders Scope as report/model.cpp's own
      // scope_to_text STRING form ("program[N]"), unlike `compare --json`'s
      // Finding.scope, which is a structured {kind, index} object (Tests
      // 2/3 below use that form via `compare`).
      const std::string scope_text = entry->at("scope").get<std::string>();
      if (scope_text == "program[1]") saw_program_1 = true;
      if (scope_text == "program[2]") saw_program_2 = true;
      // Never a zero/array-position index -- doc 02's program_number
      // domain starts at 1, and 0 would be indistinguishable from a bug
      // that fell back to an array position.
      REQUIRE(scope_text != "program[0]");
    }
    REQUIRE(saw_program_1);
    REQUIRE(saw_program_2);
  }
}

TEST_CASE("multiprogram - a single-program TS's program-scoped checks are keyed at its own program number "
          "(1), not 0",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("ts_single.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  for (const std::string& id : kProgramScopedIds) {
    const auto entries = find_all_group_entries(doc, id);
    INFO("check: " << id);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0]->at("scope").get<std::string>() == "program[1]");
  }
}

TEST_CASE("multiprogram - container.ts.cc_errors is global scope, container.ts.pcr_interval is program scope, "
          "on the same single-program file",
          "[integration]") {
  CliResult result = run_cli({"inspect", fixture("ts_single.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const auto cc_errors = find_all_group_entries(doc, "container.ts.cc_errors");
  const auto pcr_interval = find_all_group_entries(doc, "container.ts.pcr_interval");
  REQUIRE(cc_errors.size() == 1);
  REQUIRE(pcr_interval.size() == 1);
  REQUIRE(cc_errors[0]->at("scope").get<std::string>() == "global");
  REQUIRE(pcr_interval[0]->at("scope").get<std::string>() == "program[1]");
}

// --- Test 2: reordered PAT declaration still pairs by program_number -------

TEST_CASE("multiprogram - two files whose PAT declares the same program numbers in a DIFFERENT on-wire order "
          "still pair every program-scoped check program-to-program, with no unpaired finding",
          "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("ts_multiprogram.ts"), fixture("ts_multiprogram_reordered.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  for (const std::string& id : kProgramScopedIds) {
    const auto findings = find_all_findings(report, id);
    INFO("check: " << id);
    REQUIRE(findings.size() == 2);
    bool saw_program_1 = false;
    bool saw_program_2 = false;
    for (const auto* finding : findings) {
      // A real pairing (not a topology-mismatch fail, not skipped) -- the
      // content is identical between the two files, only the PAT's own
      // on-wire entry order differs.
      REQUIRE(finding->at("status") != "fail");
      REQUIRE(finding->at("status") != "skipped");
      if (scope_is_program(finding->at("scope"), 1)) saw_program_1 = true;
      if (scope_is_program(finding->at("scope"), 2)) saw_program_2 = true;
    }
    REQUIRE(saw_program_1);
    REQUIRE(saw_program_2);
  }
}

// --- Test 3: a renumbered program produces an unpaired topology failure ----

TEST_CASE("multiprogram - a file with programs {1,2} compared against one with programs {1,3} produces a "
          "topology failure naming the unpaired programs, while program 1 still compares normally",
          "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("ts_multiprogram.ts"), fixture("ts_multiprogram_renumbered.ts"), "--json"});
  REQUIRE(result.exit_code != 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  for (const std::string& id : kProgramScopedIds) {
    const auto findings = find_all_findings(report, id);
    INFO("check: " << id);

    bool program_1_present = false;
    bool program_2_unpaired_fail = false;
    bool program_3_unpaired_fail = false;
    for (const auto* finding : findings) {
      const auto& scope = finding->at("scope");
      if (scope_is_program(scope, 1)) {
        program_1_present = true;
        // Program 1 exists identically on both sides -- never itself a
        // topology-mismatch failure.
        REQUIRE(finding->at("status") != "fail");
      } else if (scope_is_program(scope, 2)) {
        REQUIRE(finding->at("status") == "fail");
        REQUIRE(finding->at("message").get<std::string>().find("2") != std::string::npos);
        program_2_unpaired_fail = true;
      } else if (scope_is_program(scope, 3)) {
        REQUIRE(finding->at("status") == "fail");
        REQUIRE(finding->at("message").get<std::string>().find("3") != std::string::npos);
        program_3_unpaired_fail = true;
      }
    }
    REQUIRE(program_1_present);
    REQUIRE(program_2_unpaired_fail);
    REQUIRE(program_3_unpaired_fail);
  }
}
