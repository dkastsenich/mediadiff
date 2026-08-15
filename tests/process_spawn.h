#pragma once

// Reusable process-spawn primitive: spawns an arbitrary executable with
// arguments, capturing its exit code, stdout and stderr independently
// without risking a pipe-buffer deadlock. Extracted out of
// tests/integration/cli_harness.h (02-02-PLAN.md Task 3) so
// tests/unit/test_registry_generator.cpp can spawn the Python interpreter
// directly, without depending on cli_harness.h's MEDIADIFF_BINARY compile
// definition — a definition the unit test target never sets, and should
// not need to just to reuse this spawner. cli_harness.h now wraps this
// header rather than duplicating the platform-specific implementation;
// integration-test behaviour is unchanged.
//
// Deliberately free of any assertion logic — mixing spawn logic with
// assertions makes it unreusable (per 01-02-PLAN.md's original rationale
// for cli_harness.h, which this header continues).
//
// Header-only: all functions are `inline` since this header is included
// from multiple test translation units over time.

#include <array>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

#include <thread>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace mediadiff::test {

// Result of spawning a process once: the real process exit code (not
// collapsed into a boolean), plus stdout and stderr captured separately so
// "nothing on stderr" is an assertable property.
struct ProcessResult {
  int exit_code = -1;
  std::string out;
  std::string err;
};

// Environment variable overrides for the child process, as (name, value)
// pairs. When passed to run_process, the child's entire environment is
// replaced with exactly this set rather than inheriting the caller's
// environment.
using ProcessEnvVars = std::vector<std::pair<std::string, std::string>>;

namespace detail {

// The only platform-specific function in this header. Spawns `executable`
// with `args`, capturing stdout/stderr independently without risking a
// pipe-buffer deadlock (both streams are drained concurrently, not read one
// after the other to completion), and returns the child's real exit code.
#if defined(_WIN32)

inline ProcessResult spawn_and_capture(const std::string& executable, const std::vector<std::string>& args,
                                        const ProcessEnvVars* env_override) {
  ProcessResult result;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE out_read = nullptr;
  HANDLE out_write = nullptr;
  HANDLE err_read = nullptr;
  HANDLE err_write = nullptr;
  if (!CreatePipe(&out_read, &out_write, &sa, 0) || !CreatePipe(&err_read, &err_write, &sa, 0)) {
    result.err = "process_spawn: CreatePipe failed";
    return result;
  }
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdOutput = out_write;
  si.hStdError = err_write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};

  std::string cmdline = "\"" + executable + "\"";
  for (const auto& a : args) {
    cmdline += " \"" + a + "\"";
  }

  std::string env_block;
  LPVOID env_ptr = nullptr;
  if (env_override != nullptr) {
    for (const auto& [key, value] : *env_override) {
      env_block += key + "=" + value;
      env_block.push_back('\0');
    }
    env_block.push_back('\0');
    env_ptr = env_block.data();
  }

  BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, env_ptr, nullptr, &si, &pi);
  CloseHandle(out_write);
  CloseHandle(err_write);

  if (!ok) {
    CloseHandle(out_read);
    CloseHandle(err_read);
    result.err = "process_spawn: CreateProcess failed";
    return result;
  }

  auto read_all = [](HANDLE handle, std::string& dest) {
    char buf[4096];
    DWORD read_bytes = 0;
    while (ReadFile(handle, buf, sizeof(buf), &read_bytes, nullptr) && read_bytes > 0) {
      dest.append(buf, read_bytes);
    }
  };

  std::thread out_thread([&]() { read_all(out_read, result.out); });
  std::thread err_thread([&]() { read_all(err_read, result.err); });
  out_thread.join();
  err_thread.join();
  CloseHandle(out_read);
  CloseHandle(err_read);

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  result.exit_code = static_cast<int>(exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return result;
}

#else  // POSIX

inline ProcessResult spawn_and_capture(const std::string& executable, const std::vector<std::string>& args,
                                        const ProcessEnvVars* env_override) {
  ProcessResult result;

  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    result.err = "process_spawn: pipe() failed";
    return result;
  }

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
  posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
  posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
  posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(executable.c_str()));
  for (const auto& a : args) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);

  std::vector<std::string> env_storage;
  std::vector<char*> envp;
  char** env_to_use = environ;
  if (env_override != nullptr) {
    env_storage.reserve(env_override->size());
    for (const auto& [key, value] : *env_override) {
      env_storage.push_back(key + "=" + value);
    }
    envp.reserve(env_storage.size() + 1);
    for (auto& entry : env_storage) {
      envp.push_back(const_cast<char*>(entry.c_str()));
    }
    envp.push_back(nullptr);
    env_to_use = envp.data();
  }

  pid_t pid = -1;
  int spawn_rc = posix_spawnp(&pid, executable.c_str(), &actions, nullptr, argv.data(), env_to_use);
  posix_spawn_file_actions_destroy(&actions);
  close(out_pipe[1]);
  close(err_pipe[1]);

  if (spawn_rc != 0) {
    close(out_pipe[0]);
    close(err_pipe[0]);
    result.err = "process_spawn: posix_spawnp failed";
    return result;
  }

  bool out_open = true;
  bool err_open = true;
  std::array<char, 4096> buf{};

  // Drain both pipes concurrently via select() rather than reading one to
  // EOF before the other — a child that fills the undrained pipe's kernel
  // buffer while writing to it would otherwise deadlock the harness.
  while (out_open || err_open) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;
    if (out_open) {
      FD_SET(out_pipe[0], &read_fds);
      max_fd = out_pipe[0] > max_fd ? out_pipe[0] : max_fd;
    }
    if (err_open) {
      FD_SET(err_pipe[0], &read_fds);
      max_fd = err_pipe[0] > max_fd ? err_pipe[0] : max_fd;
    }

    int sel = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
    if (sel < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    if (out_open && FD_ISSET(out_pipe[0], &read_fds)) {
      ssize_t n = read(out_pipe[0], buf.data(), buf.size());
      if (n > 0) {
        result.out.append(buf.data(), static_cast<std::size_t>(n));
      } else {
        close(out_pipe[0]);
        out_open = false;
      }
    }
    if (err_open && FD_ISSET(err_pipe[0], &read_fds)) {
      ssize_t n = read(err_pipe[0], buf.data(), buf.size());
      if (n > 0) {
        result.err.append(buf.data(), static_cast<std::size_t>(n));
      } else {
        close(err_pipe[0]);
        err_open = false;
      }
    }
  }

  int status = 0;
  waitpid(pid, &status, 0);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

#endif

}  // namespace detail

// Spawns `executable` with `args`, capturing its exit code, stdout and
// stderr. When `env_override` is non-null, the child's environment is
// replaced entirely with the given (name, value) pairs instead of
// inheriting the caller's environment. `executable` is resolved via PATH
// on POSIX (posix_spawnp) so a bare interpreter name ("python3") works
// without the caller resolving an absolute path itself; pass an absolute
// path when one is already known (e.g. CMake's Python3_EXECUTABLE).
inline ProcessResult run_process(const std::string& executable, const std::vector<std::string>& args,
                                  const ProcessEnvVars* env_override = nullptr) {
  return detail::spawn_and_capture(executable, args, env_override);
}

}  // namespace mediadiff::test
