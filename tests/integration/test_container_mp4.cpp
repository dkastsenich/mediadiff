// 03-05-PLAN.md Task 3: cross-format skip correctness for the six
// container.mp4.* checks, and the MP4 half of PROBE-09's fuzz-adjacent
// degradation pair. Every malformed input is generated inside this test
// from a real fixture read into memory (truncated, or filled from a
// fixed-seed PRNG) and written to a scratch temp path -- never committed
// as a media binary, matching the no-media-binaries-in-git constraint.
//
// This task deliberately covers only the MP4 half of PROBE-09; the full
// cross-scanner degradation smoke (ebml_scan, ts_scan too) lands in plan
// 03-10.
//
// Note on PassExecutionLog: the CLI binary this test spawns has no way to
// observe which Pass values actually executed (that instrumentation --
// PassExecutionLog -- is a test-only seam exposed only through
// mediadiff::detail::run_probe, reachable from tests/unit/ but not from a
// spawned child process). The "bmff_scan never ran on an MKV/TS input"
// property is instead proven at the unit level
// (tests/unit/test_mp4_analyzer.cpp's own MKV test asserts the log
// directly); this integration test proves the OBSERVABLE consequence --
// every container.mp4.* finding is skipped:not_applicable_container, never
// pass -- through the real CLI.
//
// 03-10-PLAN.md Task 1: the truncation/random-bytes mutation helpers this
// file originally defined locally now live in tests/support/mutate.h,
// shared with tests/unit/test_probe_fuzz_smoke.cpp and
// tests/integration/test_degradation.cpp -- this file calls them rather
// than keeping a second copy that could drift.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "support/mutate.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string read_whole(const std::string& path) { return mediadiff::test::read_whole(path); }

std::string write_scratch(const std::string& name, const std::string& bytes) {
  return mediadiff::test::write_mutated("test_container_mp4", name, bytes);
}

const std::vector<std::string> kMp4CheckIds = {
    "container.mp4.faststart",  "container.mp4.brands",       "container.mp4.fragmentation",
    "container.mp4.fragment_duration", "container.mp4.edit_list", "container.mp4.timescale",
};

// Guards against the zero-test false pass this project has already been
// bitten by (the plan's own read_first note): fail naming the missing
// fixture rather than silently running against nothing.
void require_fixture_present(const std::string& name) {
  INFO("missing required fixture: " << name);
  REQUIRE(fs::exists(fixture(name)));
}

}  // namespace

// --- Behavior 1/2: MKV and TS never produce a `pass` on any container.mp4.*

TEST_CASE("container_mp4 - all six container.mp4.* findings are skipped:not_applicable_container on an MKV input, "
          "none is pass",
          "[integration]") {
  require_fixture_present("tracer_a.mkv");
  CliResult result = run_cli({"inspect", fixture("tracer_a.mkv"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  for (const std::string& id : kMp4CheckIds) {
    bool found = false;
    for (const auto& entry : doc.at("groups").at("container")) {
      if (entry.at("id") == id) {
        found = true;
        REQUIRE(entry.at("status") == "skipped");
        REQUIRE(entry.at("skip_reason") == "not_applicable_container");
      }
    }
    INFO("missing container.mp4.* entry: " << id);
    REQUIRE(found);
  }
}

TEST_CASE("container_mp4 - all six container.mp4.* findings are skipped:not_applicable_container on a TS input, "
          "none is pass",
          "[integration]") {
  require_fixture_present("topo_ts.ts");
  CliResult result = run_cli({"inspect", fixture("topo_ts.ts"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  for (const std::string& id : kMp4CheckIds) {
    bool found = false;
    for (const auto& entry : doc.at("groups").at("container")) {
      if (entry.at("id") == id) {
        found = true;
        REQUIRE(entry.at("status") == "skipped");
        REQUIRE(entry.at("skip_reason") == "not_applicable_container");
      }
    }
    INFO("missing container.mp4.* entry: " << id);
    REQUIRE(found);
  }
}

// --- Behavior 3: truncation at 1 byte, 10%, 50%, 90% -----------------------

void assert_degrades_cleanly(const std::string& path) {
  CliResult result = run_cli({"inspect", path, "--json"});
  if (result.exit_code == 65) {
    return;  // Acceptable outcome 1: DemuxSession itself refused to open.
  }
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  for (const std::string& id : kMp4CheckIds) {
    bool found = false;
    for (const auto& entry : doc.at("groups").at("container")) {
      if (entry.at("id") == id) {
        found = true;
        // Acceptable outcome 2: every container.mp4.* finding is
        // skipped:unparsed_mechanism -- never a fabricated value derived
        // from the partial walk.
        REQUIRE(entry.at("status") == "skipped");
        REQUIRE(entry.at("skip_reason") == "unparsed_mechanism");
      }
    }
    INFO("missing container.mp4.* entry: " << id);
    REQUIRE(found);
  }
}

TEST_CASE("container_mp4 - a truncated MP4 degrades to exactly one of exit 65 or skipped:unparsed_mechanism, "
          "at every truncation point",
          "[integration]") {
  require_fixture_present("mp4_faststart.mp4");
  const std::string full_bytes = read_whole(fixture("mp4_faststart.mp4"));
  REQUIRE_FALSE(full_bytes.empty());

  SECTION("1 byte") {
    const std::string path = write_scratch("trunc_1byte.mp4", mediadiff::test::truncate_to(full_bytes, 1));
    assert_degrades_cleanly(path);
  }
  SECTION("10%") {
    const std::string path =
        write_scratch("trunc_10pct.mp4", mediadiff::test::truncate_to_fraction(full_bytes, 10, 100));
    assert_degrades_cleanly(path);
  }
  SECTION("50%") {
    const std::string path =
        write_scratch("trunc_50pct.mp4", mediadiff::test::truncate_to_fraction(full_bytes, 50, 100));
    assert_degrades_cleanly(path);
  }
  SECTION("90%") {
    const std::string path =
        write_scratch("trunc_90pct.mp4", mediadiff::test::truncate_to_fraction(full_bytes, 90, 100));
    assert_degrades_cleanly(path);
  }
}

// --- Behavior 4: entirely random bytes -------------------------------------

TEST_CASE("container_mp4 - a file of entirely random bytes (fixed seed) produces exit 65 cleanly, no crash",
          "[integration]") {
  // mediadiff::test::kMutationSeed (tests/support/mutate.h) -- a
  // new-seed-per-run fuzzer would make a red CI leg unreproducible, which
  // this project treats as worse than not having the check at all.
  const std::string path =
      write_scratch("random_bytes.mp4", mediadiff::test::random_bytes(mediadiff::test::kMutationSeed, 4096));

  CliResult result = run_cli({"inspect", path, "--json"});
  REQUIRE(result.exit_code == 65);
}

// --- Behavior 5: permanently-broken canary ---------------------------------
// Mirrors Phase 2's D-16 canary discipline: this test MUST fail the moment
// the degrade path itself breaks (i.e. if a zero-byte input ever reports
// "clean" instead of unparseable). It is not a placeholder -- it is a
// standing proof the degrade path has not silently regressed.

TEST_CASE("container_mp4 - canary: a zero-byte input ALWAYS reports unparseable, never clean", "[integration]") {
  const std::string path = write_scratch("canary_zero_byte.mp4", "");
  CliResult result = run_cli({"inspect", path, "--json"});
  if (result.exit_code == 0) {
    FAIL("degrade path is broken: a zero-byte input reported success (exit 0) instead of exit 65 or an "
         "unparsed_mechanism skip -- this canary exists specifically to catch that regression");
  }
  REQUIRE(result.exit_code == 65);
}
