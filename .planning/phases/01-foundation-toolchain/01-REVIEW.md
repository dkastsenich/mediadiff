---
phase: 01-foundation-toolchain
reviewed: 2026-08-14T21:33:47Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - src/util/fs.h
  - src/util/expected.h
  - src/util/version.h
  - src/util/version.cpp
  - src/cli/main.cpp
  - tests/unit/test_license.cpp
  - tests/unit/test_expected.cpp
  - tests/unit/test_fs_utf8.cpp
  - tests/unit/test_console_vt.cpp
  - tests/integration/cli_harness.h
  - tests/integration/test_version_output.cpp
  - tests/integration/check_static_link.cmake
  - CMakeLists.txt
  - CMakePresets.json
  - vcpkg.json
  - scripts/lint_eng16.sh
  - scripts/gen_corpus.sh
  - scripts/gen_corpus.ps1
  - .github/workflows/ci.yml
findings:
  critical: 1
  warning: 5
  info: 0
  total: 6
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-08-14T21:33:47Z
**Depth:** standard
**Files Reviewed:** 18 (+ .github/workflows/ci.yml, CMakePresets.json, vcpkg.json as build config)
**Status:** issues_found

## Summary

The foundation phase is largely sound: the lib/cli split, `expected<T,E>` indirection, and the D-03 exact-match license assertion are all implemented correctly and match their locked decisions. The one genuine correctness defect is in the Windows argv ingestion path (`src/cli/main.cpp`), which is the single most consequential file this phase produces for every later phase's Windows behavior: it silently drops a conversion failure instead of failing loudly, directly contradicting the D-04 invariant that the code's own `src/util/fs.h` comments call out by name. The remaining findings are quality/reliability gaps in the test harness and CI scaffolding that six later phases will build on — not incorrect today, but load-bearing infrastructure with latent bugs that will surface as flakiness or blind spots once real workloads land on top of them.

## Critical Issues

### CR-01: Windows argv UTF-16→UTF-8 conversion failures are silently substituted with empty strings, violating D-04's fail-loudly contract

**File:** `src/cli/main.cpp:60-64`
**Issue:**

```cpp
std::vector<std::string> argv_utf8;
argv_utf8.reserve(static_cast<size_t>(argc_w));
for (int i = 0; i < argc_w; ++i) {
  argv_utf8.push_back(mediadiff::wide_to_utf8(argv_w[i]));
}
```

`wide_to_utf8` (`src/util/fs.h:74-91`) returns an empty `std::string` both when the input is genuinely empty AND when `WideCharToMultiByte(..., WC_ERR_INVALID_CHARS, ...)` fails to convert an ill-formed UTF-16 sequence (e.g. an unpaired surrogate — legal in an NTFS filename, and therefore a legal `argv[i]` via `CommandLineToArgvW`). The two outcomes are indistinguishable from the return value alone, exactly as `fs.h`'s own header comment states for `utf8_to_wide`. `fopen_utf8` defends against this ambiguity explicitly (it checks the conversion result and returns `nullptr` rather than opening a substituted/empty path). `main.cpp`'s argv loop does not: it pushes whatever `wide_to_utf8` returns, unconditionally, into the argv array that CLI11 (and every later phase's file-opening code) will treat as the user's actual arguments.

**Concrete failure scenario:** a user on Windows runs `mediadiff.exe <path-containing-a-lone-surrogate>.mp4 baseline.mp4`. The candidate path fails to convert and becomes `""`. `mediadiff::run()` receives an empty positional argument in place of the real filename — not an error, not a loud rejection, just silently the wrong (empty) string standing in for the file the user actually named. This is precisely the "confident wrong answer" scenario the `fs.h` header comment explicitly forbids ("A substituted character would mean opening a DIFFERENT file than the one the user named while reporting success"), except here it happens one layer up, at the point every future CLI flag/positional value for phases 2–7 enters the program.

**Fix:** check each conversion for failure by testing the original wide string's emptiness first, and abort loudly (e.g. `fputs`+`return 64` from `wmain`, mirroring the usage-error exit code family CLI-06 will formalize) when a non-empty `argv_w[i]` converts to an empty string:

```cpp
for (int i = 0; i < argc_w; ++i) {
  const std::wstring_view original(argv_w[i]);
  std::string converted = mediadiff::wide_to_utf8(original);
  if (converted.empty() && !original.empty()) {
    // conversion genuinely failed — fail loudly, per D-04, rather than
    // substituting an empty argument for the one the user actually typed.
    fputs("mediadiff: a command-line argument could not be converted from UTF-16 to UTF-8\n", stderr);
    LocalFree(argv_w);
    return 64;
  }
  argv_utf8.push_back(std::move(converted));
}
```

## Warnings

### WR-01: `scripts/lint_eng16.sh`'s pattern misses the most natural stderr-writing call (`fprintf(stderr, ...)`)

**File:** `scripts/lint_eng16.sh:49`
**Issue:** The word-boundary pattern `(^|[^A-Za-z0-9_])(printf|std::cout|std::cerr|exit\()` requires the character immediately preceding `printf` to be a non-identifier character. `fprintf(stderr, ...)` has `f` immediately before `printf`, which IS an identifier character, so this line never matches — `fprintf` (and `vfprintf`, `fputs`, `puts`, `abort()`, `std::exit`-via-`quick_exit(`/`_Exit(`) can write to `stderr`/`stdout` or terminate the process from inside `libmediadiff` sources without tripping the lint at all. Since D-07 frames ENG-16 enforcement as structural rather than aspirational specifically so it doesn't rot, a gap this easy to hit (a C-style contributor reaching for `fprintf(stderr, "...")` instead of `std::cerr`) undermines that goal quietly.
**Fix:** extend the pattern to also catch the `fprintf`/`vfprintf` family when their first argument is `stdout`/`stderr`, and the `abort(`/`quick_exit(`/`_Exit(` family alongside `exit(`:
```bash
PATTERN='(^|[^A-Za-z0-9_])(printf|std::cout|std::cerr|std::clog|f?printf\(std(out|err)|puts\(|abort\(|quick_exit\(|_Exit\(|exit\()'
```
(adjust to taste — the point is closing the `fprintf(stderr, ...)` gap specifically, since it's the idiomatic C way to hit exactly the invariant this lint exists to enforce.)

### WR-02: CI's zero-test-discovery guard dies before printing its own diagnostic when it actually fires

**File:** `.github/workflows/ci.yml:213-222`
**Issue:**
```bash
TOTAL=$(ctest --test-dir "$TEST_DIR" -N | grep -c '^  Test #')
if [ "$TOTAL" -eq 0 ]; then
  echo "::error::ctest discovered zero tests in $TEST_DIR — a green run that executed nothing is a false pass, not a success."
  exit 1
fi
```
`defaults: run: shell: bash` (line 17-19) makes every `run:` step execute under GitHub Actions' bash default of `-eo pipefail`. `grep -c PATTERN` exits **1** (not 0) when it finds zero matching lines, even though it prints `0`. Under `-e`, a failing command substitution used as the entire right-hand side of an assignment terminates the script immediately — verified empirically:
```
$ bash --noprofile --norc -eo pipefail -c 'TOTAL=$(printf "a\nb\n" | grep -c "^zzz"); echo "reached, TOTAL=$TOTAL"'
# (no output — script exits before "reached" prints)
```
So in the exact scenario this step exists to catch (ctest discovers zero tests — the same failure mode already hit this phase three times per the surrounding comments), the job still goes red, but it never reaches the informative `::error::` message; it dies on the `TOTAL=$(...)` line with `grep`'s bare nonzero exit as the only signal. The safety property holds (no silent pass), but the specific diagnostic this step was written to provide is dead code in exactly the case it matters most.
**Fix:** `grep -c ... || true`, or switch to `wc -l`, which never fails on zero matches:
```bash
TOTAL=$(ctest --test-dir "$TEST_DIR" -N | grep -c '^  Test #' || true)
```

### WR-03: POSIX `cli_harness.h` can leak pipe file descriptors and silently truncate captured output on `EINTR`

**File:** `tests/integration/cli_harness.h:220-246`
**Issue:** Two related defects in the `select()`-based drain loop:
1. **FD leak on hard `select()` error:** `if (sel < 0) { if (errno == EINTR) continue; break; }` — on any non-`EINTR` error, the loop `break`s without closing whichever of `out_pipe[0]`/`err_pipe[0]` is still marked open, leaking that descriptor for the lifetime of the test process. Across the hundreds of `run_cli()` invocations phases 2-7 will add, this accumulates toward "too many open files" in a long-running CI test binary.
2. **`EINTR` on `read()` misreported as EOF:** `ssize_t n = read(...); if (n > 0) {...} else { close(fd); open = false; }` treats `n < 0` (including `errno == EINTR`, a legitimate transient condition under any signal-heavy CI environment) identically to `n == 0` (genuine EOF) — closing the pipe and marking it done. A child process that is momentarily interrupted mid-write can therefore have its output silently truncated, producing a flaky, hard-to-reproduce assertion failure in whatever integration test happens to be running (e.g. `test_version_output.cpp`'s `REQUIRE(result.err.empty())`/output-matching assertions).
**Fix:**
```cpp
ssize_t n = read(out_pipe[0], buf.data(), buf.size());
if (n > 0) {
  result.out.append(buf.data(), static_cast<std::size_t>(n));
} else if (n < 0 && errno == EINTR) {
  continue;  // transient, not EOF
} else {
  close(out_pipe[0]);
  out_open = false;
}
```
and close any still-open fds before the hard-error `break` path.

### WR-04: `waitpid` failure is misreported as a clean exit-code-0 process

**File:** `tests/integration/cli_harness.h:248-250`
**Issue:**
```cpp
int status = 0;
waitpid(pid, &status, 0);
result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
```
`waitpid`'s return value is never checked. If `waitpid` itself fails (e.g. `EINTR`, plausible under the same signal-heavy conditions noted in WR-03), `status` is left at its initializer value of `0`. `WIFEXITED(0)` is true (status `0` is indistinguishable from "process exited normally with code 0"), so `result.exit_code` becomes `0` — a spawn/wait *failure* is reported to every calling test as a *successful, clean exit*. A test asserting `REQUIRE(result.exit_code == 0)` would pass despite the harness never having actually observed the child's real outcome.
**Fix:** loop on `EINTR` and treat other failures as a distinguishable sentinel:
```cpp
int status = 0;
while (waitpid(pid, &status, 0) < 0) {
  if (errno == EINTR) continue;
  result.exit_code = -1;
  return result;
}
result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
```

### WR-05: Windows `spawn_and_capture` builds the child command line without escaping embedded quotes or trailing backslashes

**File:** `tests/integration/cli_harness.h:90-93`
**Issue:**
```cpp
std::string cmdline = "\"" + executable + "\"";
for (const auto& a : args) {
  cmdline += " \"" + a + "\"";
}
```
This wraps every argument in a literal `"`...`"` pair without applying Windows' documented `CommandLineToArgvW` escaping rules (a literal `"` inside an argument must be escaped as `\"`, and a run of backslashes immediately preceding a `"` must be doubled). Any test that ever spawns the CLI with a path or value ending in a backslash (e.g. a Windows temp directory like `C:\Users\foo\AppData\Local\Temp\`) or containing an embedded quote will have its argument boundary corrupted — the trailing `\"` is parsed as an escaped literal quote, consuming the intended closing quote and merging what should be two arguments into one (or worse, shifting every subsequent argument by one position). This is exactly the class of input phase-2's `--set`/`--tol`/path-argument integration tests are likely to exercise on Windows.
**Fix:** implement the standard Win32 argument-quoting algorithm (escape `"` as `\"`, double a backslash run that immediately precedes a `"` or the argument's closing quote) before appending each argument, rather than naive fixed quoting.

---

_Reviewed: 2026-08-14T21:33:47Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
