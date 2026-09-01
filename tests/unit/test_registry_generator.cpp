// ENG-01/DOC-01 fail-first proof (02-02-PLAN.md Task 3, D-14/D-15/D-16):
// spawns the real tools/gen_registry.py against small, self-contained
// fixture trees under tests/fixtures/registry/<case>/ and asserts it
// rejects each known-bad one for the specific reason it claims to, not
// merely with a non-zero exit code that could just as easily mean the
// fixture path itself was wrong (D-14's lesson from Phase 1's six checks
// structurally incapable of observing their own subject).
//
// Deliberately spawns the interpreter directly via tests/process_spawn.h's
// run_process(), not tests/integration/cli_harness.h's run_cli() — this
// target has no MEDIADIFF_BINARY compile definition and should not need
// one just to invoke Python.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "process_spawn.h"

namespace {

namespace fs = std::filesystem;

using mediadiff::test::ProcessResult;
using mediadiff::test::run_process;

// Spawns tools/gen_registry.py against the fixture tree named `case_name`
// under MEDIADIFF_REGISTRY_FIXTURES, writing generated output into a fresh
// per-case temporary directory. The output directory is cleared before use
// (not after) so a previous failed run's stale files can never make a
// later run's file-existence assertion pass for the wrong reason.
ProcessResult run_generator(const std::string& case_name, const fs::path& out_dir) {
  const fs::path checks_def = fs::path(MEDIADIFF_REGISTRY_FIXTURES) / case_name / "checks.def";
  const fs::path docs_dir = fs::path(MEDIADIFF_REGISTRY_FIXTURES) / case_name / "docs";

  std::error_code ec;
  fs::remove_all(out_dir, ec);
  fs::create_directories(out_dir, ec);

  return run_process(MEDIADIFF_PYTHON, {
                                            MEDIADIFF_GEN_REGISTRY,
                                            "--checks",
                                            checks_def.string(),
                                            "--docs-dir",
                                            docs_dir.string(),
                                            "--out-dir",
                                            out_dir.string(),
                                        });
}

fs::path case_out_dir(const std::string& case_name) {
  return fs::temp_directory_path() / ("mediadiff_registry_gen_test_" + case_name);
}

}  // namespace

TEST_CASE("registry generator: good fixture exits 0 and produces a non-empty check_id.h", "[registry]") {
  const fs::path out_dir = case_out_dir("good");
  ProcessResult result = run_generator("good", out_dir);
  REQUIRE(result.exit_code == 0);

  const fs::path generated = out_dir / "check_id.h";
  REQUIRE(fs::exists(generated));
  REQUIRE(fs::file_size(generated) > 0);
}

TEST_CASE("registry generator: bad_missing_doc exits non-zero and names the offending id", "[registry]") {
  ProcessResult result = run_generator("bad_missing_doc", case_out_dir("bad_missing_doc"));
  REQUIRE(result.exit_code != 0);
  REQUIRE(result.err.find("bad.missing_doc") != std::string::npos);
  // A fixture-path mix-up must not be able to masquerade as this failure:
  // the good fixture's id must never appear in this stderr.
  REQUIRE(result.err.find("good.sample") == std::string::npos);
}

TEST_CASE("registry generator: bad_empty_doc exits non-zero and names the offending id", "[registry]") {
  ProcessResult result = run_generator("bad_empty_doc", case_out_dir("bad_empty_doc"));
  REQUIRE(result.exit_code != 0);
  REQUIRE(result.err.find("bad.empty_doc") != std::string::npos);
}

TEST_CASE("registry generator: bad_grammar exits non-zero and names the offending id", "[registry]") {
  ProcessResult result = run_generator("bad_grammar", case_out_dir("bad_grammar"));
  REQUIRE(result.exit_code != 0);
  REQUIRE(result.err.find("Bad.Grammar") != std::string::npos);
}

TEST_CASE("registry generator: bad_dangling_alias exits non-zero and names the offending alias id", "[registry]") {
  ProcessResult result = run_generator("bad_dangling_alias", case_out_dir("bad_dangling_alias"));
  REQUIRE(result.exit_code != 0);
  REQUIRE(result.err.find("bad.alias_target") != std::string::npos);
}
