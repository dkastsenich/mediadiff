---
phase: 02-core-engine
plan: 12
subsystem: infra
tags: [cpp, msvc, c4996, getenv, windows, cross-platform]

# Dependency graph
requires:
  - phase: 02-core-engine
    provides: src/util/fs.h's UTF-8 filesystem shim (fopen_utf8, rename_replace_utf8), src/cli/color_policy.h's ColorInputs/decide_color, src/cli/options.cpp's read_color_inputs
provides:
  - "mediadiff::getenv_utf8 in src/util/fs.h -- the single first-party call site of the deprecated C environment accessor, with a _dupenv_s-backed Windows branch that preserves the unset-vs-empty distinction and leaks nothing"
  - "All six repository getenv call sites (options.cpp, snapshot.cpp, dir.cpp, golden.cpp x2 targets, test_exit_codes.cpp, test_snapshot_safe_write.cpp) routed through the shim, each preserving its exact prior truth-test semantics"
  - "A new unit test (test_fs_utf8 - getenv_utf8 separates unset from set-to-empty) proving the unset/set/set-to-empty contract on every triplet (POSIX-only for the set-to-empty section, since Win32 cannot represent it)"
  - "Three corrected sole-reader exclusivity comments (options.h, color_policy.h, options.cpp) that now name the co-sites they previously, falsely, denied"
affects: [02-13, windows-ci]

# Actuals (#2632)
actuals:
  tokens: 4284
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns: ["_dupenv_s + unique_ptr(&std::free) as the leak-free MSVC-safe environment-read pattern, mirroring _wfopen_s's earlier precedent in the same header"]

key-files:
  created: []
  modified:
    - src/util/fs.h
    - src/cli/options.cpp
    - src/cli/options.h
    - src/cli/color_policy.h
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/dir.cpp
    - tests/support/golden.cpp
    - tests/integration/test_exit_codes.cpp
    - tests/integration/test_snapshot_safe_write.cpp
    - tests/unit/test_fs_utf8.cpp

key-decisions:
  - "getenv_utf8 placed in src/util/fs.h (not options.cpp) per the file's own architectural rule: it and main.cpp are the only two places permitted to name wide-character types, so any future _wdupenv_s upgrade could only ever live here"
  - "Chose _dupenv_s over getenv_s: single-call, no ERANGE-is-not-an-error trap, and its null-buffer-means-unset shape maps one-to-one onto every existing call site's prior null-pointer test"
  - "Narrow (ANSI code page) read accepted as adequate rather than upgraded to _wdupenv_s: every variable read through the shim (NO_COLOR, CI, GITHUB_ACTIONS, MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR, UPDATE_GOLDENS, PATH) is ASCII in practice"
  - "Did not consolidate the TTY test (isatty/_isatty), which has the same one-place-in-comments-but-three-places-in-code drift as getenv did -- explicitly out of this closure's scope per the plan, and it is compile-invisible on MSVC (non-Windows preprocessor branches) so it does not block the Windows leg"
  - "Did not extend scripts/lint_eng16.sh to gate this convention -- the user explicitly ruled that out of this closure"

patterns-established:
  - "Platform-primitive shims live in src/util/fs.h with a doc comment carrying the full rationale (contract, why the _s-variant, ownership, encoding honesty) since that comment is now the only place the reasoning survives once call sites are collapsed to one line each"

requirements-completed: [BUILD-01, BUILD-05, CLI-08, CLI-09]

coverage:
  - id: D1
    description: "getenv_utf8 exists in src/util/fs.h with a _WIN32 branch (_dupenv_s, leak-free via unique_ptr) and a non-Windows branch (std::getenv), preserving the unset-vs-set-to-empty distinction"
    requirement: "CLI-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_fs_utf8.cpp#test_fs_utf8 - getenv_utf8 separates unset from set-to-empty"
        status: pass
    human_judgment: false
  - id: D2
    description: "All six former getenv call sites (options.cpp, snapshot.cpp, dir.cpp, golden.cpp, test_exit_codes.cpp, test_snapshot_safe_write.cpp) route through getenv_utf8, each preserving its exact prior truth-test semantics; the deprecated C accessor now appears at exactly one line in the repository"
    requirement: "BUILD-05"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (full 297-test suite, GCC -Wall -Wextra -Werror)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The three false sole-reader exclusivity comments (options.h, color_policy.h, options.cpp) now name the co-sites they previously denied (snapshot.cpp's CI read, compare.cpp/dir.cpp's TTY test)"
    verification:
      - kind: other
        ref: "grep-based absence/presence checks specified in 02-12-PLAN.md Task 3's <verify>, run directly against the committed files"
        status: pass
    human_judgment: false
  - id: D4
    description: "The x64-windows-static-md CI leg actually goes green under /W4 /WX"
    verification: []
    human_judgment: true
    rationale: "This sandbox is Linux-only and cannot run MSVC. Only GCC -Wall -Wextra -Werror was exercised. Ninja stopped at step 32 of 97 in the original failing run, so roughly 65 objects (five remaining src/cli sources, the header-verify target, and both test executables in full) have never been compiled by MSVC in any run -- unrelated MSVC breakage may still be hiding behind this fix. A human (or a subsequent CI run) must push the branch and read the build (x64-windows-static-md) log for a NEW first failure before this can be called proven."

duration: 25min
completed: 2026-08-16
status: complete
---

# Phase 02 Plan 12: Windows getenv C4996 gap closure (G-02-1) Summary

**Introduced `mediadiff::getenv_utf8` in `src/util/fs.h` (an MSVC-safe `_dupenv_s`-backed shim), routed all six repository `getenv` call sites through it, and corrected three comments that had been asserting a "sole reader" convention the code had already broken.**

## Performance

- **Duration:** 25 min
- **Started:** 2026-08-16T10:27:00Z (approx.)
- **Completed:** 2026-08-16T10:52:10Z
- **Tasks:** 3
- **Files modified:** 10

## Accomplishments
- `mediadiff::getenv_utf8` exists in `src/util/fs.h`, is the single first-party call site of the deprecated C environment accessor, and preserves the unset-vs-set-to-empty distinction `ColorInputs`/`NO_COLOR` depend on, with the Windows `_dupenv_s` allocation released on every exit path via `unique_ptr<char, decltype(&std::free)>`.
- All six former `getenv` sites (`src/cli/options.cpp`, `src/cli/commands/snapshot.cpp`, `src/cli/commands/dir.cpp`, `tests/support/golden.cpp` — compiled into two targets, `tests/integration/test_exit_codes.cpp`, `tests/integration/test_snapshot_safe_write.cpp`) now read through the shim, each keeping its exact prior truth-test semantics byte for byte.
- A new unit test case proves the unset/set/set-to-empty contract directly (`tests/unit/test_fs_utf8.cpp`), with the set-to-empty section gated `#ifndef _WIN32` and its comment explaining why that state is unrepresentable on Win32 by construction.
- Three comments that falsely claimed sole-reader exclusivity (`src/cli/options.h`, `src/cli/color_policy.h`, `src/cli/options.cpp`) now name the real co-sites: `snapshot.cpp`'s independent `CI` read, and the TTY test's three call sites (`options.cpp`, `compare.cpp`, `dir.cpp`).
- The full local test suite grew from 296 to 297 tests (one new `getenv_utf8` case) and stays 100% passing under GCC `-Wall -Wextra -Werror` after every task.

## Task Commits

Each task was committed atomically:

1. **Task 1: getenv_utf8 in fs.h, wired through read_color_inputs, proven by a unit test** - `2197c88` (feat)
2. **Task 2: Route the remaining five sites through the shim** - `f005af8` (fix)
3. **Task 3: Make the three sole-reader comments true** - `c023be3` (docs)

_Tracer feedback gate: Task 1 is `type="tracer"`. Auto mode was active (`workflow.auto_advance: true`); its `<verify>` was re-run end-to-end after commit and passed (2 tests discovered under the `unit.test_fs_utf8` filter, both green) before Task 2 began._

## Files Created/Modified
- `src/util/fs.h` - Added `mediadiff::getenv_utf8` (Windows `_dupenv_s`/leak-free `unique_ptr`, POSIX `std::getenv` passthrough), with the full rationale doc comment the plan specified.
- `src/cli/options.cpp` - Deleted the local `read_env` helper; `read_color_inputs` now reads `NO_COLOR`/`CI`/`GITHUB_ACTIONS` through `getenv_utf8`; `stdout_is_tty`'s comment corrected (Task 3).
- `src/cli/options.h` - `read_color_inputs`'s doc comment corrected to name `snapshot.cpp`'s independent `CI` read and the TTY test's three call sites.
- `src/cli/color_policy.h` - Header comment corrected: every environment read goes through `getenv_utf8`; `read_color_inputs` is only the sole reader of the *colour* variables specifically.
- `src/cli/commands/snapshot.cpp` - `ci_env_is_true()` reads `CI` through `getenv_utf8`.
- `src/cli/commands/dir.cpp` - Test-only `MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR` injection read routed through `getenv_utf8`; added `#include "util/fs.h"`.
- `tests/support/golden.cpp` - `update_goldens_requested()` routed through `getenv_utf8`; comment updated to name the shim.
- `tests/integration/test_exit_codes.cpp` - `PATH` read routed through `getenv_utf8` (`value_or("")`); added `#include "util/fs.h"`.
- `tests/integration/test_snapshot_safe_write.cpp` - Same `PATH`-read treatment; added `#include "util/fs.h"`.
- `tests/unit/test_fs_utf8.cpp` - New `set_env`/`clear_env` helpers and a new `TEST_CASE` proving the unset/set/set-to-empty contract.

## Decisions Made
- Placed the shim in `src/util/fs.h` rather than `options.cpp`, per that header's own rule that it (and `main.cpp`) are the only two files permitted to name wide-character types — architecturally the only place a future `_wdupenv_s` upgrade could live.
- Chose `_dupenv_s` over `getenv_s`: single-call, no `ERANGE`-is-not-an-error trap, and its null-buffer-means-unset shape is a mechanical, behaviour-preserving substitution for every existing call site's prior `nullptr` test.
- Accepted the narrow (ANSI code page) read as adequate rather than upgrading to `_wdupenv_s`: every variable this shim reads (`NO_COLOR`, `CI`, `GITHUB_ACTIONS`, `MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR`, `UPDATE_GOLDENS`, `PATH`) is ASCII in every practical case.
- Left the TTY test (`isatty`/`_isatty`) un-consolidated — it has the same one-place-in-comments-but-three-places-in-code drift `getenv` had, but it is compile-**invisible** on MSVC (sits inside non-Windows preprocessor branches at all three sites) so it does not block the Windows leg, and the plan explicitly scoped it out.
- Did not extend `scripts/lint_eng16.sh` to gate this convention going forward, per the plan's explicit instruction (the user ruled this out of the closure).

## Deviations from Plan

None — plan executed exactly as written. All three tasks' `<verify>` blocks ran and passed unmodified; no Rule 1/2/3 auto-fixes were needed.

## Issues Encountered
- The build precondition (`build/x64-linux` configured, `ctest -N` reporting 296 tests) was not initially met in this fresh worktree — no build directory, and the `vcpkg` submodule was uninitialized. Resolved by running `git submodule update --init --recursive` and configuring with `VCPKG_DEFAULT_BINARY_CACHE=~/.cache/vcpkg/archives`, which restored every vcpkg dependency (including the FFmpeg build) from the machine's existing local binary cache in ~11 seconds rather than requiring a fresh 15–40 minute build. Precondition then verified met (296 tests discovered) before Task 1 began.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

**What is proven:** The tree builds warnings-clean under GCC's `-Wall -Wextra -Werror`; the deprecated C environment accessor survives at exactly one line in the repository (`src/util/fs.h`'s non-Windows branch); all six former call sites name `getenv_utf8`; the full 297-test suite passes, including the WR-02 colour-precedence regressions, the SNAP-07 CI write gate, the `dir` exit-70 injection path, and the golden harness; a new test case directly asserts the unset-versus-empty contract.

**What is NOT proven, and this SUMMARY makes no claim otherwise:** Nothing in this execution exercised MSVC, `/W4 /WX`, or `_dupenv_s` — this is a Linux sandbox. GCC has no C4996 analogue, so the whole defect class this plan fixes was, and remains, invisible to every check that ran here. Ninja stopped at step 32 of 97 in the original CI failure, so roughly 65 objects (five remaining `src/cli` sources, the header-verify target, and both test executables in full) have never been compiled by MSVC in any run — unrelated MSVC breakage may still be hiding behind this fix. The correct next action is to push this branch and read the `build (x64-windows-static-md)` CI log for a **new** first failure, not to assume green. `02-13-PLAN.md` fixes the `arm64-osx` test-count guard independently; neither plan alone turns CI fully green.

---
*Phase: 02-core-engine*
*Completed: 2026-08-16*

## Self-Check: PASSED

- FOUND: src/util/fs.h
- FOUND: tests/unit/test_fs_utf8.cpp
- FOUND: .planning/phases/02-core-engine/02-12-SUMMARY.md
- FOUND commit: 2197c88 (Task 1)
- FOUND commit: f005af8 (Task 2)
- FOUND commit: c023be3 (Task 3)
- FOUND commit: 3e89b4b (plan metadata)
