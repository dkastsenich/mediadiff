// SNAP-07: `snapshot` refuses to silently overwrite an existing tracked
// (or, under CI=true, any existing) target without --force, and the
// refusal leaves the existing file byte-identical. Drives the real
// `mediadiff snapshot` subcommand against a scratch git repository created
// in a temporary directory — the gate itself lives in
// src/cli/commands/snapshot.cpp's write_snapshot_gated, invoked here only
// through the CLI surface a user actually calls (02-07-PLAN.md Task 3).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cli_harness.h"
#include "process_spawn.h"
#include "util/fs.h"

using mediadiff::test::CliResult;
using mediadiff::test::EnvVars;
using mediadiff::test::ProcessResult;
using mediadiff::test::run_cli;
using mediadiff::test::run_process;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir(const std::string& tag) {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() / ("mediadiff_snap_gate_" + tag + "_" + std::to_string(now) + "_" +
                                    std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

// Initializes a real git repository at `dir`, with a committer identity so
// `git commit` never fails on a machine with no global user.email/name
// configured (a real CI runner, for instance).
void init_git_repo(const fs::path& dir) {
  REQUIRE(run_process("git", {"init", "-q", dir.string()}).exit_code == 0);
  REQUIRE(run_process("git", {"-C", dir.string(), "config", "user.email", "test@example.com"}).exit_code == 0);
  REQUIRE(run_process("git", {"-C", dir.string(), "config", "user.name", "mediadiff tests"}).exit_code == 0);
}

void write_file(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  out << content;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

void git_add_commit(const fs::path& repo, const fs::path& target, const std::string& message) {
  REQUIRE(run_process("git", {"-C", repo.string(), "add", target.string()}).exit_code == 0);
  REQUIRE(run_process("git", {"-C", repo.string(), "commit", "-q", "-m", message}).exit_code == 0);
}

std::string source_snapshot() { return std::string(MEDIADIFF_FIXTURES_DIR) + "/tracer_a.snap.json"; }

}  // namespace

TEST_CASE("snapshot_safe_write - SNAP-07: an untracked target is overwritten", "[integration]") {
  const fs::path repo = unique_scratch_dir("untracked");
  init_git_repo(repo);
  const fs::path target = repo / "untracked.snap.json";

  CliResult result = run_cli({"snapshot", source_snapshot(), "--out", target.string()});
  REQUIRE(result.exit_code == 0);
  REQUIRE(fs::exists(target));
  REQUIRE(read_file(target).find("schema_version") != std::string::npos);
}

TEST_CASE("snapshot_safe_write - SNAP-07: a tracked target is refused without --force (exit 64, names --force)",
          "[integration]") {
  const fs::path repo = unique_scratch_dir("tracked");
  init_git_repo(repo);
  const fs::path target = repo / "tracked.snap.json";
  write_file(target, R"({"seed": true})");
  git_add_commit(repo, target, "seed");
  const std::string before = read_file(target);

  CliResult result = run_cli({"snapshot", source_snapshot(), "--out", target.string()});
  REQUIRE(result.exit_code == 64);
  REQUIRE(result.err.find("--force") != std::string::npos);

  // The refusal leaves the file byte-identical — checked before/after,
  // not merely "some file exists at this path".
  REQUIRE(read_file(target) == before);
}

TEST_CASE("snapshot_safe_write - SNAP-07: a tracked, zero-byte target is refused", "[integration]") {
  const fs::path repo = unique_scratch_dir("zero");
  init_git_repo(repo);
  const fs::path target = repo / "zero.snap.json";
  write_file(target, "");
  git_add_commit(repo, target, "seed zero-byte file");
  REQUIRE(fs::file_size(target) == 0);

  CliResult result = run_cli({"snapshot", source_snapshot(), "--out", target.string()});
  REQUIRE(result.exit_code == 64);
  REQUIRE(fs::file_size(target) == 0);
}

TEST_CASE("snapshot_safe_write - SNAP-07: --force overwrites a tracked target", "[integration]") {
  const fs::path repo = unique_scratch_dir("force");
  init_git_repo(repo);
  const fs::path target = repo / "tracked_force.snap.json";
  write_file(target, R"({"seed": true})");
  git_add_commit(repo, target, "seed");

  CliResult result = run_cli({"snapshot", source_snapshot(), "--out", target.string(), "--force"});
  REQUIRE(result.exit_code == 0);
  REQUIRE(read_file(target).find("schema_version") != std::string::npos);
}

TEST_CASE("snapshot_safe_write - SNAP-07: CI=true refuses an existing untracked target without --force",
          "[integration]") {
  const fs::path repo = unique_scratch_dir("ci_untracked");
  init_git_repo(repo);
  const fs::path target = repo / "untracked_ci.snap.json";
  write_file(target, R"({"seed": true})");  // deliberately NOT added/committed.
  const std::string before = read_file(target);

  const auto path_env = mediadiff::getenv_utf8("PATH");
  EnvVars env = {{"CI", "true"}, {"PATH", path_env.value_or("")}};
  CliResult result = run_cli({"snapshot", source_snapshot(), "--out", target.string()}, &env);
  REQUIRE(result.exit_code == 64);
  REQUIRE(read_file(target) == before);
}

TEST_CASE("snapshot_safe_write - SNAP-07: two --force runs are byte-identical and leave git status clean",
          "[integration]") {
  const fs::path repo = unique_scratch_dir("idempotent");
  init_git_repo(repo);
  const fs::path target = repo / "idempotent.snap.json";
  write_file(target, R"({"seed": true})");
  git_add_commit(repo, target, "seed");

  CliResult first = run_cli({"snapshot", source_snapshot(), "--out", target.string(), "--force"});
  REQUIRE(first.exit_code == 0);
  git_add_commit(repo, target, "first --force write");
  const std::string after_first = read_file(target);

  CliResult second = run_cli({"snapshot", source_snapshot(), "--out", target.string(), "--force"});
  REQUIRE(second.exit_code == 0);
  const std::string after_second = read_file(target);
  REQUIRE(after_first == after_second);

  ProcessResult status = run_process("git", {"-C", repo.string(), "status", "--porcelain", target.string()});
  REQUIRE(status.exit_code == 0);
  REQUIRE(status.out.empty());
}
