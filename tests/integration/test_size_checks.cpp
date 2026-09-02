// 03-09-PLAN.md Task 3 (SIZE-01): the cross-platform determinism assertion
// for the whole size.* family, riding the SAME golden mechanism
// tests/support/golden.{h,cpp} already established (D-12) -- read-only
// when UPDATE_GOLDENS is unset, a missing golden a hard failure, never an
// implicit create. Extends tests/integration/test_determinism.cpp/
// test_idempotence.cpp's own double-run byte-comparison pattern rather
// than duplicating it.
//
// Does NOT golden the whole report (this file's own action text's
// explicit instruction): only the size.* findings' ids, scopes and values
// are extracted into a small canonical text block and goldened -- a full
// report golden would make every unrelated report change a size-test
// failure, training reviewers to refresh goldens without reading them.
//
// The cross-platform byte-identity claim doc 06 section 4 makes ("byte-
// identical results across platforms (idempotence, again)") is verified
// by this SAME golden being compared on the Linux/macOS/Windows CI legs --
// observable nowhere else than a red leg on one platform and green on the
// others.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// Extracts every size.* group entry's own (id, scope, value) into a
// sorted, canonical text block -- deliberately narrow (never the whole
// report): a full-report golden would make every unrelated report change
// a size-test failure, which trains reviewers to refresh goldens without
// reading them, and a golden nobody reads is not a gate.
std::string canonical_size_block(const nlohmann::ordered_json& doc) {
  std::vector<std::string> lines;
  for (const auto& entry : doc.at("groups").at("size")) {
    std::string line = entry.at("id").get<std::string>() + " " + entry.at("scope").get<std::string>() + " ";
    const nlohmann::ordered_json& value = entry.at("value");
    if (value.is_number_integer()) {
      line += std::to_string(value.get<std::int64_t>());
    } else {
      // A RationalValue -- num/den only (never the renderer's own
      // convenience "ms"/"tb" convenience fields, which are a display
      // aid computed by report/json.cpp, not part of this check family's
      // own canonical output).
      line += "num=" + std::to_string(value.at("num").get<std::int64_t>()) +
              " den=" + std::to_string(value.at("den").get<std::int64_t>());
    }
    lines.push_back(std::move(line));
  }
  std::sort(lines.begin(), lines.end());
  std::string block;
  for (const std::string& line : lines) {
    block += line + "\n";
  }
  return block;
}

void require_fixture(const std::string& path) {
  // Test 5's own discipline (this plan's own action text): a suite that
  // silently ran nothing is a false pass, not a success -- fail loudly
  // and NAME the missing fixture, never skip.
  INFO("required fixture is missing: " << path);
  REQUIRE(std::filesystem::exists(path));
}

}  // namespace

// --- Test 1: two consecutive inspect --json runs are byte-identical -------

TEST_CASE("size_checks - two consecutive inspect --json runs produce byte-identical stdout", "[integration]") {
  const std::string path = fixture("size_crf20.mp4");
  require_fixture(path);

  CliResult first = run_cli({"inspect", path, "--json"});
  CliResult second = run_cli({"inspect", path, "--json"});
  REQUIRE(first.exit_code == 0);
  REQUIRE(second.exit_code == 0);
  REQUIRE(first.out == second.out);
}

// --- Test 2/3: a committed, read-only golden pins the size.* values -------

TEST_CASE("size_checks - the size.* findings are pinned by a committed, read-only golden", "[integration]") {
  const std::string path = fixture("size_crf20.mp4");
  require_fixture(path);

  CliResult result = run_cli({"inspect", path, "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  const std::string block = canonical_size_block(doc);
  // Every real value present -- proves size_crf20.mp4 exercises all four
  // check ids with no skip riding along in the golden.
  REQUIRE(block.find("size.file ") != std::string::npos);
  REQUIRE(block.find("size.overhead ") != std::string::npos);
  REQUIRE(block.find("size.stream_bitrate ") != std::string::npos);
  REQUIRE(block.find("size.peak_bitrate ") != std::string::npos);

  mediadiff::test::check_golden("size_checks_size_crf20", block);
}

// --- Test 4: a byte-identical pair compares clean on every size.* check ---

TEST_CASE("size_checks - a byte-identical pair compares pass on every size.* check under sw-encoder",
          "[integration]") {
  const std::string a = fixture("size_crf20.mp4");
  const std::string b = fixture("size_crf20_copy.mp4");
  require_fixture(a);
  require_fixture(b);

  CliResult result = run_cli({"compare", a, b, "--profile", "sw-encoder", "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  std::vector<std::string> size_ids;
  for (const auto& finding : doc.at("findings")) {
    const std::string id = finding.at("id").get<std::string>();
    if (id.rfind("size.", 0) == 0) {
      REQUIRE(finding.at("status") == "pass");
      size_ids.push_back(id);
    }
  }
  std::sort(size_ids.begin(), size_ids.end());
  size_ids.erase(std::unique(size_ids.begin(), size_ids.end()), size_ids.end());
  // All four distinct size.* check ids are present and every one passed
  // (the REQUIRE inside the loop above already enforced the per-finding
  // pass; this asserts none of the four is silently absent).
  REQUIRE(size_ids.size() == 4);
}
