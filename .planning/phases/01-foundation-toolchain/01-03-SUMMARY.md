---
phase: 01-foundation-toolchain
plan: 03
status: complete
date: 2026-08-14
requirements: [CLI-09]
files_modified:
  - src/util/fs.h
  - src/cli/main.cpp
  - app.manifest
  - CMakeLists.txt
  - tests/unit/CMakeLists.txt
  - tests/unit/test_fs_utf8.cpp
  - tests/unit/test_console_vt.cpp
commits:
  - 60cab12
  - a20f772
  - 51d01e5
---

# Plan 01-03 Summary — Windows text handling

Windows argument, path and console handling landed at the foundation, where locked decision D-04
requires it. Tasks 1 and 2 were completed on 2026-08-13; task 3, a blocking human-verify
checkpoint, was deferred by the user that day and closed on 2026-08-14 against a real console.

## What was built

- **`wmain` entry point** (`src/cli/main.cpp`): UTF-16 argv obtained via `GetCommandLineW` /
  `CommandLineToArgvW` and converted to UTF-8 exactly once, before CLI11 sees anything. All
  internal strings are UTF-8 from that point on.
- **`src/util/fs.h`**: the single file-open shim (`fopen_utf8`), plus the UTF-8/UTF-16 conversion
  helpers and `enable_vt_output()`. Every call site in mediadiff, now and in phases 2–7, goes
  through it and needs no platform conditional of its own.
- **`app.manifest`**: UTF-8 active code page, embedded in the executable.
- **`tests/unit/test_fs_utf8.cpp`**: non-ASCII filename round trip, running on every triplet.
- **`tests/unit/test_console_vt.cpp`**: mechanical assertion of the console-mode contract (added
  2026-08-14 — see "How the checkpoint was closed").

## Recorded per the plan's output contract

**The two code points, and why.** `U+65E5 U+672C U+8A9E` (日本語) is a basic-multilingual-plane
case — each character is a 3-byte UTF-8 sequence and a single UTF-16 code unit. `U+1F3AC` (🎬) is
an astral-plane case: 4 bytes in UTF-8 and, critically, a **surrogate pair** in UTF-16. A
conversion that handles the BMP correctly can still mishandle surrogate pairs, so the second case
tests a distinct failure mode rather than adding volume. Both filenames are constructed by the test
at runtime from `\u`-escapes; no non-ASCII filename is committed to the repository.

**Strict conversion required flags beyond research's snippet.** Research's illustrative
`MultiByteToWideChar` / `WideCharToMultiByte` calls passed no flags. The implementation adds
`MB_ERR_INVALID_CHARS` and `WC_ERR_INVALID_CHARS` so malformed input fails loudly instead of being
silently substituted with U+FFFD. Silent substitution would mean opening a *different file than the
one named* — the precise class of bug this shim exists to prevent.

**Research assumption A1 — the terminal-mode call shape — needed no correction.** Confirmed
empirically, not by inspection. The shape research proposed works as written:

```cpp
HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
DWORD mode = 0;
if (out != INVALID_HANDLE_VALUE && out != nullptr && GetConsoleMode(out, &mode)) {
  SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
```

**No `AVIOContext` wrapper was built.** D-04's open verification item stayed resolved-negative:
research traced `avformat_open_input` → `file_open()` → `avpriv_open()` → `win32_open()` →
`MultiByteToWideChar(CP_UTF8, …)` → `_wsopen()` in FFmpeg's own source, so libavformat converts
UTF-8 paths internally on Windows and `fs.h` covers only mediadiff's own I/O. `grep -c AVIOContext
src/util/fs.h` → 0.

**Windows build number: not captured.** The verifying console was `conhost.exe` on a Windows 10
1903+ machine, but `ver` output was not recorded during the session. This is a documentation gap,
not an unverified behaviour — every assertion below was executed. Worth capturing opportunistically
if the same machine is used again.

## How the checkpoint was closed

The checkpoint as originally written asked a human to judge whether coloured output "looked right"
in two terminals and whether `NO_COLOR` suppressed styling. **Inspection before running it showed
three of its five steps were vacuous:** mediadiff emits no styled output at all today — zero ANSI
escapes, no `fmt` styling — and `NO_COLOR` is not read anywhere, because colour handling is CLI-08
and belongs to Phase 2. A human would have been confirming that nothing renders as nothing.

The perceptual steps were therefore replaced with an objective one. The real open question was
research assumption A1 — whether the `GetConsoleMode`/`SetConsoleMode` shape is correct — and that
is a boolean, not a matter of appearance.

`tests/unit/test_console_vt.cpp` (tagged `[console]`) asserts it directly. It **skips** rather than
passes when stdout is not a console, so CI records it as skipped and never as a pass — the same
`skipped ≠ pass` rule this project applies to its own check results, applied to its test suite. CI
structurally cannot run it: Actions redirects stdout to a pipe, `GetConsoleMode` fails on a pipe,
and `enable_vt_output()` correctly no-ops.

To remove the need for a Windows toolchain, the CI Windows leg now publishes
`mediadiff.exe` and `mediadiff_unit_tests.exe` as the `mediadiff-windows-x64` artifact. The
Catch2 binary is self-contained and runs directly with a tag filter — no CMake, no CTest, no build
tree. Verification cost dropped from "install Visual Studio and build FFmpeg" to "download, unzip,
run four commands".

## Checkpoint evidence (2026-08-14, `conhost.exe`)

Run deliberately in `conhost.exe` rather than Windows Terminal. This matters: Windows Terminal
enables VT processing itself, so the check would pass there even if our `SetConsoleMode` call were
wrong. `conhost` is the honest test.

**1. The console-mode contract — the assertion that closes A1:**

```
> mediadiff_unit_tests.exe "[console]"
Filters: [console]
All tests passed (3 assertions in 1 test case)
```

Not skipped — so a real console handle was detected. Three assertions passed: `GetConsoleMode`
succeeded after the call; `ENABLE_VIRTUAL_TERMINAL_PROCESSING` was set; and every console flag that
was set beforehand was still set afterwards (the call adds VT without clobbering line input, echo
or wrapping for whatever runs next in that console).

**2. All four CLI-05 fields render in a real console, no escape leakage:**

```
> mediadiff.exe --version
mediadiff 0.1.0
libavcodec 62.28.100 (built against 62.28.100)
libavformat 62.12.100 (built against 62.12.100)
libavutil 60.26.100 (built against 60.26.100)
license: LGPL version 2.1 or later
features:
```

**3. Negative control — piped, so stdout is not a console:**

```
> mediadiff.exe --version | more
[byte-identical output]
```

`GetConsoleMode` fails on a pipe, `enable_vt_output()` no-ops, and the output is unchanged. This
rules out the failure mode where console-specific handling leaks into a redirected stream — the
case CI exercises on every run.

**4. Non-ASCII directory path, exercising the active-code-page manifest:**

```
> mkdir Ünïcödé-tëst && copy mediadiff.exe Ünïcödé-tëst\ && cd Ünïcödé-tëst && mediadiff.exe --version
        1 file(s) copied.
mediadiff 0.1.0
[full version block]
```

## Verification summary

| Check | Evidence |
|---|---|
| VT processing enabled on a real console | `[console]` test, 3 assertions, conhost.exe |
| Existing console flags preserved | asserted in the same test |
| Research assumption A1 | confirmed — no correction needed |
| Non-ASCII filename round trip (BMP + astral) | `unit.test_fs_utf8`, green on all 3 blocking legs incl. Windows CI |
| Non-ASCII directory path | manual, conhost.exe |
| Piped stdout unchanged | manual negative control |
| Wide-char types confined | `src/cli/main.cpp` and `src/util/fs.h` only |
| No `AVIOContext` wrapper | `grep -c` → 0 |

## Deviations

1. **Task 3's perceptual steps replaced with a mechanical assertion**, for the reason above: there
   is no styled output to perceive yet. The colour-rendering checks are not lost — they move to
   CLI-08 in Phase 2, where `NO_COLOR`, TTY detection and the `GITHUB_ACTIONS` special case are
   built and there is something real to test.
2. **Two files added beyond the plan's `files_modified`:** `tests/unit/test_console_vt.cpp`, and an
   artifact-upload step in `.github/workflows/ci.yml`. Both serve this plan's checkpoint; the
   workflow file is otherwise plan 01-05's and was appended to, not restructured.
3. **Windows build number not recorded** (see above).
