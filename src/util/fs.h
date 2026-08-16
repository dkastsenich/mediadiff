#pragma once

// mediadiff's UTF-8 filesystem shim (D-04) — the single site where encoding
// conversion happens for mediadiff's OWN file I/O (report files,
// mediadiff.toml, snapshot/compare file access).
//
// This header and the Windows entry point in src/cli/main.cpp are the only
// two places in this repository permitted to name wide-character types
// (wchar_t, LPWSTR, ...) or Windows code-page constants (CP_UTF8, ...).
// Every other line of mediadiff — CLI parsing, analyzers, comparators,
// reporters — deals exclusively in UTF-8 std::string. Confining conversion
// to one file is what lets Phases 2 through 7 add roughly 100 more
// file-opening sites without ever touching a wide-character type again.
//
// This shim does NOT wrap FFmpeg's own file access. Research traced
// libavformat's default file:// protocol open path through the pinned
// FFmpeg source (file_open() -> avpriv_open() -> win32_open() ->
// get_extended_win32_path() -> MultiByteToWideChar(CP_UTF8, ...) ->
// _wsopen()) and confirmed it already converts a UTF-8 path to UTF-16
// internally on Windows. A wrapping I/O context over a wide-opened handle
// is therefore unnecessary and stays out of scope.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mediadiff {

#ifdef _WIN32

// Converts a UTF-8 string to UTF-16.
//
// Fails loudly rather than substituting: MB_ERR_INVALID_CHARS makes
// MultiByteToWideChar reject a byte sequence it cannot convert instead of
// silently replacing it with a default character. A substituted character
// would mean opening a DIFFERENT file than the one the user named while
// reporting success — a diff tool that does that produces a confident
// wrong answer, which is exactly what this plan's prohibitions forbid.
//
// An empty input is rejected explicitly before the OS call: the sizing
// call returns 0 both for a zero-length input and for a genuine conversion
// error, so the two outcomes are indistinguishable from the return value
// alone. Treating empty as a caller error up front removes that ambiguity.
//
// Returns an empty std::wstring on any failure, including an empty input.
// Keeps no state between calls, so it is safe to call from multiple
// threads at once — a property Phase 2's bounded worker pool relies on.
inline std::wstring utf8_to_wide(std::string_view s) {
  if (s.empty()) {
    return std::wstring{};
  }
  const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                       static_cast<int>(s.size()), nullptr, 0);
  if (len <= 0) {
    return std::wstring{};
  }
  std::wstring wide(static_cast<size_t>(len), L'\0');
  const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                           static_cast<int>(s.size()), wide.data(), len);
  if (written <= 0) {
    return std::wstring{};
  }
  return wide;
}

// Converts a UTF-16 string to UTF-8, mirroring utf8_to_wide's fail-loud,
// empty-checked, stateless contract in the other direction.
//
// Returns an empty std::string on any failure, including an empty input.
inline std::string wide_to_utf8(std::wstring_view s) {
  if (s.empty()) {
    return std::string{};
  }
  const int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                                       static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  if (len <= 0) {
    return std::string{};
  }
  std::string out(static_cast<size_t>(len), '\0');
  const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                                           static_cast<int>(s.size()), out.data(), len, nullptr,
                                           nullptr);
  if (written <= 0) {
    return std::string{};
  }
  return out;
}

// Opens a file named by a UTF-8 path, via the wide-character file-open API.
//
// An empty path is rejected before any conversion is attempted. A path
// that fails to convert (utf8_to_wide returns empty for a non-empty input
// only on conversion failure, per that function's contract) is also
// rejected — never handed to the OS as an empty or substituted name.
inline FILE* fopen_utf8(std::string_view path, std::string_view mode) {
  if (path.empty()) {
    return nullptr;
  }
  const std::wstring wide_path = utf8_to_wide(path);
  if (wide_path.empty()) {
    return nullptr;
  }
  const std::wstring wide_mode = utf8_to_wide(mode);
  if (wide_mode.empty()) {
    return nullptr;
  }
  // _wfopen_s rather than _wfopen: MSVC deprecates the latter (C4996), and
  // /W4 /WX promotes that to a hard error (BUILD-05). The secure variant
  // reports failure through its errno_t return and leaves the out-parameter
  // untouched, so it is initialised to nullptr here and the failure path
  // returns nullptr — preserving this function's contract exactly.
  FILE* handle = nullptr;
  if (_wfopen_s(&handle, wide_path.c_str(), wide_mode.c_str()) != 0) {
    return nullptr;
  }
  return handle;
}

#else  // !_WIN32

// POSIX (and every other non-Windows platform mediadiff targets) already
// takes byte-oriented UTF-8 paths directly at the OS boundary, so
// fopen_utf8 is a direct passthrough here. The signature matches the
// Windows branch exactly — that identical signature is the entire point of
// the shim, since it means no call site anywhere in mediadiff ever needs a
// platform conditional of its own.
inline FILE* fopen_utf8(std::string_view path, std::string_view mode) {
  if (path.empty()) {
    return nullptr;
  }
  return std::fopen(std::string(path).c_str(), std::string(mode).c_str());
}

#endif  // _WIN32

// Atomically replaces `to` with `from` (or plainly renames `from` to `to`
// when `to` doesn't exist yet) — the temp-file-then-rename primitive every
// mediadiff writer that must never leave a reader observing a partially-
// written file (SNAP-07's own "leave the existing file byte-identical on
// refusal" contract, and write_snapshot's "no torn file" guarantee) builds
// on.
//
// On POSIX, std::rename() already atomically replaces an existing `to`.
// On Windows, the MSVC CRT's rename() does NOT replace an existing
// destination — it fails with EEXIST-equivalent behavior — so this wraps
// MoveFileExW with MOVEFILE_REPLACE_EXISTING instead, reusing this
// header's own utf8_to_wide conversion rather than asking a caller
// outside fs.h to construct a wide path itself (this header and
// src/cli/main.cpp are the only two places permitted to name
// wide-character types).
//
// Returns false on any failure, including an empty `from`/`to`.
inline bool rename_replace_utf8(std::string_view from, std::string_view to) {
  if (from.empty() || to.empty()) {
    return false;
  }
#ifdef _WIN32
  const std::wstring wide_from = utf8_to_wide(from);
  const std::wstring wide_to = utf8_to_wide(to);
  if (wide_from.empty() || wide_to.empty()) {
    return false;
  }
  return MoveFileExW(wide_from.c_str(), wide_to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  return std::rename(std::string(from).c_str(), std::string(to).c_str()) == 0;
#endif
}

// Reads one environment variable as UTF-8, distinguishing "unset"
// (std::nullopt) from "set to the empty string" (an engaged optional
// holding ""). That distinction is load-bearing: `ColorInputs`' three
// `std::optional<std::string>` fields (src/cli/color_policy.h) exist for
// it, `NO_COLOR`'s own convention is that PRESENCE is the signal rather
// than the value, and the WR-02 regression tests assert exactly this
// unset-versus-empty behaviour.
//
// This is the single first-party call site of the deprecated C environment
// accessor (Windows: `_dupenv_s` rather than `getenv`/`getenv_s`). MSVC's
// CRT deprecates the plain accessor (C4996), and `/W4 /WX` (BUILD-05)
// promotes that to a hard error — the same reason `fopen_utf8` above uses
// `_wfopen_s` instead of `_wfopen` (commit 47d5965). `_CRT_SECURE_NO_WARNINGS`
// is not an alternative: it would disable a whole class of deprecation
// diagnostics repository-wide just to hide this one call.
//
// `_dupenv_s` rather than `getenv_s`: both preserve the unset-versus-empty
// distinction, but `getenv_s` needs a two-call size-probe-then-read
// pattern whose probe call reports `ERANGE` as a non-error condition,
// while `_dupenv_s` is single-call and its null-buffer-means-unset shape
// maps one-to-one onto the null-pointer test every call site already used
// before this shim existed.
//
// Ownership: `_dupenv_s` allocates via `malloc` on success and the caller
// must release it. The `unique_ptr` below does that on every exit path,
// including one where constructing the returned `std::string` throws, so
// no path leaks the CRT allocation.
//
// Encoding honesty: the narrow form reads the CRT's active-code-page view
// of the environment. Inside the `mediadiff` executable, `app.manifest`'s
// `activeCodePage` makes that UTF-8; the two test executables carry no
// manifest (CMakeLists.txt attaches it to `mediadiff` only), so there it
// is the machine's ambient ANSI code page. Every variable read through
// this shim today (`NO_COLOR`, `CI`, `GITHUB_ACTIONS`,
// `MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR`, `UPDATE_GOLDENS`, `PATH`) is
// ASCII in practice, so narrow is adequate rather than merely convenient.
// If a non-ASCII value ever matters, the encoding-exact upgrade is
// `_wdupenv_s` fed through this header's own `wide_to_utf8` — and it can
// only live in this file, since this header and `src/cli/main.cpp` are
// the only two places in the repository permitted to name wide-character
// types.
//
// `name` is `const char*` rather than this header's usual
// `std::string_view`: both CRT entry points require a NUL-terminated name
// and every caller passes a literal, so a `string_view` would only add an
// allocation.
inline std::optional<std::string> getenv_utf8(const char* name) {
#ifdef _WIN32
  char* buffer = nullptr;
  std::size_t count = 0;
  const errno_t err = _dupenv_s(&buffer, &count, name);
  std::unique_ptr<char, decltype(&std::free)> owned(buffer, &std::free);
  if (err != 0) {
    return std::nullopt;
  }
  if (owned == nullptr) {
    return std::nullopt;
  }
  return std::string(owned.get());
#else
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

// Enables virtual-terminal (ANSI escape sequence) output processing on the
// standard output handle. Must be the first statement of the process entry
// point, before the CLI parser is constructed and before any output is
// written — a write that precedes this call would render raw escape text
// instead of styled output.
//
// On Windows: queries the current console mode and, if that query
// succeeds, re-sets the mode with ENABLE_VIRTUAL_TERMINAL_PROCESSING
// added. A failed query means standard output is not attached to a
// console (redirected to a file or a pipe, for example) and the call does
// nothing, because there is no console mode to set.
// On every other platform: a no-op, since ANSI escapes already render
// natively in a POSIX terminal.
inline void enable_vt_output() {
#ifdef _WIN32
  HANDLE out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (out_handle != INVALID_HANDLE_VALUE && out_handle != nullptr && GetConsoleMode(out_handle, &mode)) {
    SetConsoleMode(out_handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#endif
}

}  // namespace mediadiff
