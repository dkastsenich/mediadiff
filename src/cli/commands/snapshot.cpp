#include "cli/commands/snapshot.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/exit_code.h"
#include "core/snapshot.h"
#include "util/fs.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace mediadiff {

namespace {

// Splits a UTF-8 path into (directory, filename), matching
// core/snapshot.cpp's own basename_utf8 rationale: plain byte-oriented
// string search rather than std::filesystem::path, which -- constructed
// from a narrow std::string on Windows -- converts via the ambient ANSI
// code page rather than UTF-8. Both halves are handed to `git` as
// argv/cwd, never through a shell, so no quoting/injection concern
// applies either.
struct SplitPath {
  std::string dir;
  std::string filename;
};

SplitPath split_path(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return SplitPath{".", path};
  }
  std::string dir = path.substr(0, pos);
  if (dir.empty()) {
    dir = "/";
  }
  return SplitPath{dir, path.substr(pos + 1)};
}

// Existence check that stays entirely on the fopen_utf8 byte-string path
// (never std::filesystem), matching this file's own encoding discipline.
bool file_exists_utf8(const std::string& path) {
  FILE* handle = fopen_utf8(path, "rb");
  if (handle == nullptr) {
    return false;
  }
  std::fclose(handle);
  return true;
}

// Spawns `git -C <dir> ls-files --error-unmatch -- <filename>`, discarding
// its stdout/stderr, and returns its exit code — 0 means "tracked", any
// other value (including git's own "not tracked" refusal) means "not
// tracked by git's own judgment". Returns std::nullopt when git could not
// even be spawned (not on PATH, spawn failure) — SNAP-07's gate treats
// that identically to "not tracked", per this file's own header comment.
#if defined(_WIN32)

std::optional<int> spawn_git_ls_files(const SplitPath& target) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE null_write = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, &sa, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_write == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdOutput = null_write;
  si.hStdError = null_write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};
  std::string cmdline = "git -C \"" + target.dir + "\" ls-files --error-unmatch -- \"" + target.filename + "\"";

  BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
  CloseHandle(null_write);
  if (!ok) {
    return std::nullopt;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(exit_code);
}

#else  // POSIX

std::optional<int> spawn_git_ls_files(const SplitPath& target) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  const int devnull = open("/dev/null", O_WRONLY);
  if (devnull >= 0) {
    posix_spawn_file_actions_adddup2(&actions, devnull, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
  }

  std::vector<char*> argv = {const_cast<char*>("git"),         const_cast<char*>("-C"),
                              const_cast<char*>(target.dir.c_str()), const_cast<char*>("ls-files"),
                              const_cast<char*>("--error-unmatch"), const_cast<char*>("--"),
                              const_cast<char*>(target.filename.c_str()), nullptr};

  pid_t pid = -1;
  const int spawn_rc = posix_spawnp(&pid, "git", &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (devnull >= 0) {
    close(devnull);
  }
  if (spawn_rc != 0) {
    return std::nullopt;
  }

  int status = 0;
  pid_t wait_rc = -1;
  do {
    wait_rc = waitpid(pid, &status, 0);
  } while (wait_rc == -1 && errno == EINTR);
  if (wait_rc == -1 || !WIFEXITED(status)) {
    return std::nullopt;
  }
  return WEXITSTATUS(status);
}

#endif

bool is_git_tracked(const std::string& path) {
  const SplitPath target = split_path(path);
  if (target.filename.empty()) {
    return false;
  }
  const std::optional<int> exit_code = spawn_git_ls_files(target);
  // git unavailable / spawn failure: treated as untracked (this function's
  // own contract, mirrored in write_snapshot_gated's header comment).
  return exit_code.has_value() && *exit_code == 0;
}

// Default `--out` when none is given (SNAP-01): an input already named
// like a snapshot ("*.snap.json") is rewritten in place; anything else
// gets ".snap.json" appended.
std::string resolve_out_path(const std::string& input, const std::string& out_flag) {
  if (!out_flag.empty()) {
    return out_flag;
  }
  static constexpr std::string_view kSuffix = ".snap.json";
  if (input.size() >= kSuffix.size() &&
      input.compare(input.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
    return input;
  }
  return input + std::string(kSuffix);
}

bool ci_env_is_true() {
  const char* ci = std::getenv("CI");
  return ci != nullptr && std::string_view(ci) == "true";
}

}  // namespace

mediadiff::expected<void, Error> write_snapshot_gated(const Fingerprint& fp, const std::string& out_path, bool force,
                                                       const CheckRegistry& registry) {
  const bool exists = file_exists_utf8(out_path);
  if (exists && !force) {
    // CI=true refuses ANY existing target regardless of tracked-ness — the
    // gate keys on tracked-ness for interactive/local use, but an
    // automated job overwriting any pre-existing baseline without an
    // explicit --force is exactly the failure mode SNAP-07 names.
    if (ci_env_is_true()) {
      return mediadiff::unexpected(
          Error{ErrorKind::usage,
                "refusing to overwrite an existing snapshot under CI=true without --force: " + out_path});
    }
    if (is_git_tracked(out_path)) {
      return mediadiff::unexpected(
          Error{ErrorKind::usage, "refusing to overwrite a git-tracked snapshot without --force: " + out_path});
    }
    // Untracked (or git was unavailable / the path is outside a
    // repository, both treated as untracked) — proceed.
  }
  return write_snapshot(fp, out_path, registry);
}

void register_snapshot_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("snapshot", "Write a *.snap.json fingerprint (or re-materialize an existing one)");

  auto input_path = std::make_shared<std::string>();
  auto out_path = std::make_shared<std::string>();
  auto force_flag = std::make_shared<bool>(false);
  cmd->add_option("file", *input_path, "Media file to fingerprint, or an existing *.snap.json to rewrite")
      ->required();
  cmd->add_option("--out", *out_path,
                   "Output path (default: <file> if it already ends in .snap.json, else <file>.snap.json)");
  cmd->add_flag("--force", *force_flag, "Overwrite an existing git-tracked or CI-protected target");

  // ENG-16 explicitly reserves exit()/stdout/stderr as "the CLI's
  // prerogative" — see src/cli/commands/compare.cpp's identical rationale.
  cmd->callback([input_path, out_path, force_flag]() {
    const CheckRegistry& registry = builtin_registry();

    // No probe layer exists until Phase 3 (D-11: the stub analyzer never
    // enters the shipped binary), so this command has no way to fingerprint
    // real media yet. An input that IS already a valid *.snap.json is not
    // "real media" at all — it's re-read and re-materialized through the
    // exact same write path, which is what this task actually proves.
    // Anything that fails to read as a snapshot (including any real media
    // file) gets the honest, actionable reason below rather than
    // read_snapshot's own more generic "not valid JSON" text.
    auto fp = read_snapshot(*input_path, registry);
    if (!fp) {
      std::fputs(
          "mediadiff: fingerprinting a media file requires the probe layer, which arrives with Phase 3; pass an "
          "existing *.snap.json as <file> to re-materialize/rewrite it instead.\n",
          stderr);
      std::exit(kExitInput);
    }

    const std::string resolved_out = resolve_out_path(*input_path, *out_path);
    auto result = write_snapshot_gated(*fp, resolved_out, *force_flag, registry);
    if (!result) {
      const Error& err = result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    std::exit(kExitClean);
  });
}

}  // namespace mediadiff
