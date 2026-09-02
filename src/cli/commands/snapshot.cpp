#include "cli/commands/snapshot.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/exit_code.h"
#include "cli/options.h"
#include "core/snapshot.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
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
// code page rather than UTF-8. Both halves are handed to `git` as an
// argument list, never through a shell. On POSIX that argument list is a
// real argv array (posix_spawnp), so no quoting/injection concern applies
// there. On Windows, CreateProcessA instead takes a single
// lpCommandLine string that the CHILD process's own CRT startup
// re-tokenizes using the same quote/backslash rules a shell would use --
// "no shell involved" does not mean "no injection concern" on that path;
// see win32_quote_arg below (WR-05).
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

// WR-05 fix: escapes a single argument for safe embedding in a
// CreateProcessA `lpCommandLine` string, following the MSVC C runtime's
// own documented argv-tokenization rules (the same rules
// CommandLineToArgvW implements, and every MSVC-linked child process --
// including `git.exe` -- parses its command line with): a run of N
// backslashes immediately followed by a literal `"` is re-encoded as
// 2N+1 backslashes then `"` (N backslashes survive as N literal
// backslashes, the extra one escapes the quote so it too becomes
// literal); a run of N backslashes immediately before the CLOSING quote
// this function appends is re-encoded as 2N backslashes (so they survive
// literally without escaping the terminator, which must remain a real
// string delimiter). An argument containing no space/tab/quote needs no
// quoting at all and is passed through unchanged, matching how a real
// argv element with the same content would be received. Without this
// escaping, an embedded `"` in `target.dir`/`target.filename` terminates
// the intended quoted argument early and lets the remaining text be
// re-parsed as ADDITIONAL command-line tokens by git's own CRT startup --
// this is the argument-injection WR-05 (02-REVIEW.md) reports, and is why
// the previous raw `"` + arg + `"` concatenation below was unsafe.
std::string win32_quote_arg(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
    return arg;
  }
  std::string result = "\"";
  for (auto it = arg.begin();; ++it) {
    std::size_t backslash_count = 0;
    while (it != arg.end() && *it == '\\') {
      ++it;
      ++backslash_count;
    }
    if (it == arg.end()) {
      result.append(backslash_count * 2, '\\');
      break;
    }
    if (*it == '"') {
      result.append(backslash_count * 2 + 1, '\\');
      result.push_back('"');
    } else {
      result.append(backslash_count, '\\');
      result.push_back(*it);
    }
  }
  result.push_back('"');
  return result;
}

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
  std::string cmdline = "git -C " + win32_quote_arg(target.dir) + " ls-files --error-unmatch -- " +
                         win32_quote_arg(target.filename);

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
  const auto ci = getenv_utf8("CI");
  return ci.has_value() && *ci == "true";
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

  // ->type_name("TEXT"): see src/cli/options.cpp's add_policy_flags for the
  // fully worked rationale (D-05).
  CLI::Option* input_path = cmd->add_option("file", "Media file to fingerprint, or an existing *.snap.json to rewrite")
                                 ->type_name("TEXT")
                                 ->required();
  CLI::Option* out_path =
      cmd->add_option("--out", "Output path (default: <file> if it already ends in .snap.json, else <file>.snap.json)")
          ->type_name("TEXT");
  CLI::Option* force_flag = cmd->add_flag("--force", "Overwrite an existing git-tracked or CI-protected target");
  ProbeArgs probe_args = add_probe_flags(*cmd);

  // ENG-16 explicitly reserves exit()/stdout/stderr as "the CLI's
  // prerogative" — see src/cli/commands/compare.cpp's identical rationale.
  // Capturing a raw Option* by value is exactly as safe as the shared_ptr
  // it replaces (D-05): the App owns the Option for the whole program
  // lifetime, and this callback only runs during app.parse().
  cmd->callback([input_path, out_path, force_flag, probe_args]() {
    const CheckRegistry& registry = builtin_registry();

    // snapshot reads no mediadiff.toml today (it predates policy
    // resolution entirely), so --probe-timeout has no `[probe]
    // timeout_seconds` config fallback here -- pass std::nullopt for
    // `config`, matching resolve_probe_timeout_ms's own documented
    // fallback-to-nullopt-config contract.
    auto probe_timeout_ms = resolve_probe_timeout_ms(probe_args, std::nullopt);
    if (!probe_timeout_ms) {
      const Error& err = probe_timeout_ms.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    if (probe_timeout_ms->has_value()) {
      set_default_wall_clock_budget_ms(**probe_timeout_ms);
    }

    // Same std::nullopt-config rationale as the --probe-timeout
    // resolution just above -- snapshot reads no mediadiff.toml. This
    // command's own resolved thread count is always 1 (a single file),
    // so derive_per_file_cap_bytes hands it the whole resolved budget.
    auto probe_budget_mb = resolve_probe_memory_budget_mb(probe_args, std::nullopt);
    if (!probe_budget_mb) {
      const Error& err = probe_budget_mb.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    set_default_packet_scan_max_bytes(derive_per_file_cap_bytes(*probe_budget_mb * 1024 * 1024, /*threads=*/1));

    // fingerprint_input (src/probe/orchestrator.h) tries read_snapshot
    // first -- an input that IS already a valid *.snap.json is re-read
    // and re-materialized through the exact same write path unchanged
    // (SNAP-01's own contract) -- and falls through to a real probe of
    // the media bytes only when the input opened but was not a snapshot
    // (PROBE-01, this plan).
    const std::string input_path_text = opt_string(input_path);
    auto fp = fingerprint_input(input_path_text, registry);
    if (!fp) {
      const Error& err = fp.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    const std::string resolved_out = resolve_out_path(input_path_text, opt_string(out_path));
    auto result = write_snapshot_gated(*fp, resolved_out, opt_flag(force_flag), registry);
    if (!result) {
      const Error& err = result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    std::exit(kExitClean);
  });
}

}  // namespace mediadiff
