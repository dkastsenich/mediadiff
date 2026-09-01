---
phase: 02-core-engine
fixed_at: 2026-09-01T19:37:32Z
review_path: .planning/phases/02-core-engine/02-REVIEW.md
iteration: 2
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 02: Code Review Fix Report

**Fixed at:** 2026-09-01T19:37:32Z
**Source review:** .planning/phases/02-core-engine/02-REVIEW.md (2026-09-01T18:48:45Z re-review)
**Iteration:** 2

**Summary:**
- Findings in scope: 4 (CR-04, WR-04, WR-05, WR-06 — `critical_warning` scope; this review recorded zero Info
  findings)
- Fixed: 4
- Skipped: 0

**Note on this file:** a prior 2026-08-15 round (iteration 1, `findings_in_scope: 6`, `status: partial`) fixed
CR-01/CR-02/CR-03/WR-01/WR-02/WR-03 and deliberately skipped IN-01. That content is preserved in git history
(`git log -- .planning/phases/02-core-engine/02-REVIEW-FIX.md`) — this file now documents only the 2026-09-01
re-review's four new findings, replacing the prior body per this run's own dispatch instructions.

**Verification environment:** all builds and test runs below happened inside an isolated worktree at
`.claude/worktrees/rf-02-41042-1788290857` (branch `gsd-reviewfix/02-41042`), configured against the
`x64-linux` vcpkg triplet using the machine's existing `~/.cache/vcpkg/archives` binary cache (submodule
`vcpkg/` was `git submodule update --init`'d inside the worktree since it did not exist there by default —
this reused the cache, so no FFmpeg rebuild was needed). A full build (`cmake --build --preset x64-linux`)
succeeded with zero warnings under `-Wall -Wextra -Werror`, and the full CTest suite ran green immediately
before the cleanup tail below (303/303 passing, 1 legitimate `unit.console_vt` skip — up from the stated
298-test baseline by exactly the 5 new unit tests WR-04 adds; nothing fell below baseline). These numbers are
reproducible from the main branch (`gsd/phase-2-core-engine`) after this run's cleanup tail fast-forwards it,
**with one exception**: WR-05 touches a `#if defined(_WIN32)` code path this Linux worktree cannot compile
or execute — its own verification section below states exactly what was and was not checked here.

**One explicit exception to "all fixed":** WR-05's fix is applied, committed, and reviewed carefully, but per
this run's own environment constraint it is **verified only indirectly on this host** (a portable reference-
implementation round-trip test of the escaping algorithm, not a real `CreateProcessA`/`git.exe` run) — it
still requires a green `build (x64-windows-static-md)` CI leg before anyone treats it as proven. Every other
finding below is verified directly and completely on this host.

## Fixed Issues

### CR-04: `lint_dead_code_after_fail.sh` fails to detect dead code that follows a `FAIL(...)` call on the *same* line — a proven false-negative in a required merge gate

**Files modified:** `scripts/lint_dead_code_after_fail.sh`
**Commit:** `538ee81`
**Applied fix:** Rewrote the three-state `AWK_PROGRAM`'s state-1→state-2 transition. Previously state 1
("consuming the failure statement") only advanced when the **current line's own trimmed text** ended in
`);`, so `FAIL("x"); int y = 5;` — where the FAIL call's own `);` is not the line's suffix — never
transitioned, and the trailing dead-code statement was never judged. The fix searches for the FAIL-
terminating `);` **anywhere** in the line (from just past the `FAIL(` match onward, so text preceding
`FAIL(` on the same line can never masquerade as the terminator), and when it is found mid-line, judges
whatever text trails it on that **same physical line** immediately under state 2's rule, rather than only
deferring to the next line. When nothing trails the terminator on that line, judgment still defers to the
next surviving line exactly as before — the multi-line-argument case (`FAIL(\n "x");`) is unchanged in
behavior. Added a second synthetic self-test fixture (`FAIL("x"); int y = 5;\n return y;\n}`) alongside the
existing multi-line one, both run through the identical matcher before every real scan; the script now
refuses to proceed if either self-test does not fire.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror` (this script isn't compiled, but the change didn't touch
  anything the build depends on).
- Directly executed the fixed `AWK_PROGRAM` (extracted verbatim from the script) against the review's exact
  reproduction cases: `FAIL("x"); int y = 5;\n return y;\n}` now reports the violation and exits `1` (was
  exit `0`/clean before the fix); the original multi-line self-test fixture (`FAIL(\n "x");\n static int
  u = 0;\n return u;\n}`) still reports a violation and exits `1` — the pre-existing detection was not
  weakened.
- Also directly verified four additional shapes to confirm no regression: a correctly-closed single-line
  `FAIL("x"); }` (exit `0`, no violation), the "coincidental catch" case `FAIL("x"); int d = compute();`
  (still exit `1`, still caught), a same-line trailing `//` comment (exit `0`, correctly not flagged), and a
  correctly-closed multi-line block (exit `0`).
- Ran the script itself against the real `tests/` tree: `lint_dead_code_after_fail.sh: clean. Scanned 50
  file(s) under tests; no statement follows a FAIL() call before its enclosing block closes.` (exit `0`) —
  both embedded self-tests passed silently (a failed self-test would have aborted the script with a
  diagnostic before reaching the real scan).
- All four required lint scripts (`lint_eng16.sh`, `lint_check_id_strings.sh`,
  `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`) exit `0` on the real tree.
- Full suite: 303/303 passing (this fix touches no C++ source, so the count contribution is from WR-04
  below).

### WR-04: Untrusted-snapshot values are type-checked but never range-checked, letting a crafted magnitude produce a semantically wrong (not UB) verdict

**Files modified:** `src/core/serializer.cpp`, `tests/unit/test_serializer.cpp`
**Commit:** `3ea72af`
**Applied fix:** Extended `value_from_json`'s `rational` branch to reject `den <= 0` **and** `tb.den <= 0`
as `ErrorKind::input_unsupported`, immediately after CR-01's existing type checks and before the value is
returned. `tb.den` is included (going one property beyond the review's "if the project wants to bound
`tb.den` too" optional suggestion) because `core/rational.h`'s own header comment for `compare_ticks`
states denominators are "assumed strictly positive... a zero or negative denominator is a caller bug, not a
value this function attempts to detect" — i.e. `value_from_json` is exactly the caller responsible for that
invariant, and it was not enforcing it. Extended the `histogram` branch to reject a negative `count` the
same way, before it is stored in the bin — a zero count is left accepted (a legitimate empty bin, not an
invalid magnitude). Both new checks return `Error`, never throw, matching the project's `expected<T,
Error>` contract.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- 5 new unit tests added to `tests/unit/test_serializer.cpp`: a `den <= 0` RationalValue is rejected (both
  `den == 0` and `den < 0`), a `tb.den <= 0` RationalValue is rejected (both cases), a normal positive-
  den/tb.den RationalValue still round-trips exactly as before (no regression), a negative-count Histogram
  bin is rejected, and a zero-count Histogram bin still round-trips (confirms the boundary is `< 0`, not
  `<= 0`).
- All 5 new tests pass; full suite: 303/303 passing, 0 failed, 1 skipped (up from the 298-test baseline by
  exactly these 5 additions — never below baseline).
- Confirmed by direct code reading that the new checks sit strictly after CR-01's existing type checks (so
  a non-integer `den`/`count` still produces CR-01's original, unchanged error message and error path) and
  before any value derived from `den`/`count` is returned or stored — no path exists that reads a
  non-positive `den`/`tb.den` or negative `count` and still returns a `Value`.

### WR-05: `snapshot` command's Windows git-tracked check builds an unescaped `CreateProcessA` command line, allowing argument injection that can bypass the overwrite-protection gate

**Files modified:** `src/cli/commands/snapshot.cpp`
**Commit:** `524ea88`
**Applied fix:** Added `win32_quote_arg`, a single-argument Windows command-line escaper implementing the
documented MSVC C runtime / `CommandLineToArgvW`-compatible quoting rules exactly as the review's Fix
section specified: a run of N backslashes immediately followed by a literal `"` is re-encoded as `2N+1`
backslashes then `"`; a run of N backslashes immediately before the closing quote this function appends is
re-encoded as `2N` backslashes; an argument with no space/tab/quote needs no quoting and passes through
unchanged. `spawn_git_ls_files`'s Windows branch now builds `cmdline` by calling `win32_quote_arg` on
`target.dir` and `target.filename` individually rather than raw string concatenation with hand-written
`\"..\"` quoting. Also corrected the file-header comment (lines above `SplitPath`) that previously claimed
"no quoting/injection concern applies" for both halves — it was true only for the POSIX `posix_spawnp`
branch (a real argv array, never re-tokenized) and false for the Windows `CreateProcessA` branch (whose
`lpCommandLine` string IS re-tokenized by the child's own CRT startup using shell-equivalent rules, "no
shell" notwithstanding) — the comment now states this distinction explicitly, per the review's explicit
request to correct it.

**This fix is UNVERIFIED ON WINDOWS.** Per this run's explicit environment constraint, I could not compile
or execute the `_WIN32` branch, `CreateProcessA`, or a real `git.exe` on this Linux host. What I did
instead, to get the strongest confidence available without a Windows host:
- The POSIX branch, and the rest of the file, still compile clean under `-Wall -Wextra -Werror` on
  `x64-linux` — confirmed by a full rebuild (`cmake --build --preset x64-linux`) succeeding with zero
  warnings, so this fix did not regress the currently-green Linux/macOS legs.
- `win32_quote_arg`'s body is pure string manipulation with no Windows API calls, so I copied it verbatim
  into a standalone program and compiled it with `g++ -std=c++20 -Wall -Wextra -Werror` on this host
  alongside an independent reference tokenizer implementing the *same documented* MSVC CRT argv rules (not
  reusing any of `win32_quote_arg`'s own logic). Round-tripped 13 adversarial inputs — an embedded quote, a
  trailing backslash run (1/2/3 backslashes), a quote preceded by backslashes, an empty string, an
  all-backslash path, a bare double-quote, and **the exact injection payload shape from the review**
  (`x" -C "C:\attacker\path`) — through `win32_quote_arg` then the reference tokenizer, confirming every one
  parses back to exactly the single original argument (never splitting into extra tokens). This is strong
  algorithmic evidence the escaping is correct per the documented rules, but it is **not** a substitute for
  running the actual Windows CRT / `CreateProcessA` / `git.exe` — only a green `build (x64-windows-static-md)`
  CI run (and ideally a targeted manual repro of the review's own injection shape against the built
  Windows binary) can confirm that.
- I did not attempt any change beyond what the review specified (no speculative Windows API usage, no
  untested "clever" escaping shortcut) — the algorithm implemented is the standard, widely-documented one
  (the same shape used by CPython's `subprocess.list2cmdline` and .NET's argument escaper), chosen
  specifically because it is well-understood rather than novel, per this run's own guidance to prefer a
  conservative, standard approach here.

### WR-06: `lint_check_id_strings.sh` has no self-test control clause, unlike its two round-3/4 siblings wired into the same required gate

**Files modified:** `scripts/lint_check_id_strings.sh`
**Commit:** `ffe29fe`
**Applied fix:** Added a self-test control clause identical in shape and placement to
`lint_dead_code_after_fail.sh`'s and `lint_fixture_case_collisions.sh`'s: before the real scan, writes a
synthetic fixture (`const char* x = "meta.tool_version";`) to a temp file, runs the exact same `PATTERN`
(`grep -nE "$PATTERN"`) against it, and exits `1` with a diagnostic if the pattern does not fire. This
specific lint needed the self-test more than its siblings do, precisely because `src/analyzers/` currently
holds only `.gitkeep` files — the real scan alone can never distinguish "clean" from "nothing to scan
regardless of whether the matcher still works," so without this self-test a regex regression in `PATTERN`
would stay completely invisible until Phase 3's first analyzer source lands.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror` (shell-script-only change).
- Ran the script against the real tree: `lint_check_id_strings.sh: clean. No dotted check-id string literals
  found under src/analyzers/.` (exit `0`) — the embedded self-test passed silently first.
- **Directly proved the self-test can fail** (the requirement this finding specifically calls out): copied
  the script, replaced `PATTERN` with a regex that matches nothing (`NEVER_MATCHES_ANYTHING_XYZ`), and ran
  it — it exited `1` with `"the matcher's own self-test did not fire against a synthetic known-bad
  fixture... Refusing to report the real scan as clean"`, never reaching the real scan. This confirms the
  self-test is a genuine control clause, not a no-op that always passes (the exact failure mode the finding
  warns against: "a self-test that cannot fail reproduces the exact defect being fixed").
- All four required lint scripts exit `0` on the real tree (re-confirmed together after this change).

## Skipped Issues

None — all four in-scope findings were fixed.

---

_Fixed: 2026-09-01T19:37:32Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_
