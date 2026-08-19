// CR-01 regression (02-REVIEW.md): a `.snap.json` whose `input_identity`
// (or a measurement's `value`) is syntactically valid JSON but
// type-mismatched at a field core/serializer.cpp's value_from_json /
// core/snapshot.cpp's input_identity_from_json read without checking the
// node's TYPE first (only its presence via .contains()) used to throw
// nlohmann::json::type_error, which was:
//   - uncaught on the `compare`/`snapshot`/`inspect` paths -- std::terminate
//     (SIGABRT, exit 134), a media-processing CLI crashing on untrusted
//     CI-supplied input, exactly the failure mode PROJECT.md exists to
//     prevent.
//   - silently swallowed by WorkerPool::run_indexed's blanket `catch (...)`
//     on the `dir` path, which converted a corrupt candidate snapshot into
//     an apparent zero-finding "clean" file with the WHOLE corpus run
//     exiting 0 -- a false negative in the tool's primary CI-gate use case.
//
// Both halves are proven here through the REAL binary (never a synthetic
// exception thrown directly at the library boundary), asserting EXACT exit
// codes per this project's own verification discipline (D-14/D-15/D-16: a
// check that cannot observe its own subject is worse than no check).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir(const std::string& tag) {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() / ("mediadiff_cr01_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

void write_file(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.is_open());
  out << content;
}

// A well-formed, minimal snapshot -- schema_version/tool_version only, no
// measurements, no input_identity.
constexpr const char* kGoodSnapshot = R"({"schema_version":"1.0","tool_version":"x","measurements":[]})";

// CR-01's own reproduction shape: input_identity.size_bytes is a JSON
// STRING, not a number -- .contains("size_bytes") is true, so the old code
// reached json.at("size_bytes").get<std::int64_t>() unchecked and threw.
constexpr const char* kPoisonedSnapshot =
    R"({"schema_version":"1.0","tool_version":"x",)"
    R"("input_identity":{"basename":"f.mp4","size_bytes":"not-a-number","xxh3_128":"deadbeef"},)"
    R"("measurements":[]})";

}  // namespace

TEST_CASE("type_poisoned_snapshot - CR-01 half 1: compare exits a clean input-error code, never crashes",
          "[integration]") {
  const fs::path dir = unique_scratch_dir("compare");
  const fs::path good = dir / "good.snap.json";
  const fs::path poisoned = dir / "poisoned.snap.json";
  write_file(good, kGoodSnapshot);
  write_file(poisoned, kPoisonedSnapshot);

  const CliResult result = run_cli({"compare", good.string(), poisoned.string(), "--json"});

  // Exact integer, not merely "!= 0" or "< 128" -- kExitInput (65), the
  // ordinary "input did not open or did not parse" contract
  // (src/cli/exit_code.h). Specifically NOT 134 (128 + SIGABRT) and NOT any
  // other >=128 value a crashing process would produce.
  CHECK(result.exit_code == 65);
  CHECK(result.exit_code != 134);
  CHECK(result.out.empty());
  CHECK(result.err.find("input_identity") != std::string::npos);
}

TEST_CASE(
    "type_poisoned_snapshot - CR-01 half 2: dir mode never reports a corrupted pair as an empty-findings clean "
    "pass",
    "[integration]") {
  const fs::path baseline = unique_scratch_dir("dir_baseline");
  const fs::path candidate = unique_scratch_dir("dir_candidate");
  write_file(baseline / "f.snap.json", kGoodSnapshot);
  write_file(candidate / "f.snap.json", kPoisonedSnapshot);

  const CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});

  // The corpus run must NOT exit 0 when a pair could not be evaluated --
  // exact kExitInput (65), matching compare's own mapping for the same
  // ErrorKind::input_unsupported (WorkerPool::run_indexed's per-index
  // hard_error now threads through to dir.cpp's exit-code decision instead
  // of being silently swallowed).
  CHECK(result.exit_code == 65);
  CHECK(result.exit_code != 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  // The corrupted file must be NAMED in the top-level diagnostics -- never
  // silently absorbed with no trace at all.
  bool diagnostic_names_file = false;
  for (const auto& line : report.at("diagnostics")) {
    if (line.get<std::string>().find("f.snap.json") != std::string::npos) {
      diagnostic_names_file = true;
    }
  }
  CHECK(diagnostic_names_file);

  // The corpus-level summary must not present this as a clean run: with
  // exactly one pair, and that pair unresolvable, there must be no
  // synthesized "pass" anywhere in the summary that would let a human
  // reading only summary.pass/warn/fail (not the exit code, not
  // diagnostics) believe the corpus was clean.
  const auto& summary = report.at("summary");
  CHECK(summary.at("pass").get<int>() == 0);
}
