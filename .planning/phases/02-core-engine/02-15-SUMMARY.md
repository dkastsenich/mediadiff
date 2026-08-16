---
phase: 02-core-engine
plan: 15
subsystem: testing
tags: [catch2, ctest, ci, macos, apfs, case-folding, lint]

# Dependency graph
requires:
  - phase: 02-core-engine
    provides: dir_pairing.cpp's byte-wise std::map<std::string, FilePair> ordering (02-11-PLAN.md) and the round-2 gap-closure register (02-12/02-13-PLAN.md)
provides:
  - "A case-collision-free byte-wise-order fixture in tests/unit/test_dir_pairing.cpp that lands five files on any filesystem (case-insensitive or not)"
  - "A permanent scan gate (scripts/lint_fixture_case_collisions.sh) against the same collision class recurring anywhere in tests/"
affects: [02-16-PLAN.md]

# Actuals (#2632)
actuals:
  tokens: 2845
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns: ["fixture-case-allow: opt-out marker convention for deliberate cross-directory case-variant fixtures"]

key-files:
  created:
    - scripts/lint_fixture_case_collisions.sh
  modified:
    - tests/unit/test_dir_pairing.cpp

key-decisions:
  - "Replaced the fixture's colliding lowercase 'beta.snap.json' twin with a distinct 'Zulu.snap.json' stem rather than dropping a name or folding the comparison, per the plan's explicit prohibitions and DIR-04's byte-wise-ordering guarantee."
  - "The expected order is now an explicit five-literal vector instead of a sort of the test's own result, and a case-folded-sort 'teeth' assertion was locally proven to fail against a non-discriminating all-lowercase fixture, then restored — verifying the teeth check actually has teeth rather than asserting it does."
  - "Task 2's acceptance/verify block, as written in 02-15-PLAN.md, referenced `git show HEAD:tests/unit/test_dir_pairing.cpp` to recover the pre-fix content; because this executor commits each task atomically, HEAD already carried Task 1's fix by the time Task 2 ran. Verified against HEAD~1 instead (the commit immediately preceding Task 1's), which is the same pre-fix content the plan intended — a verify-script staleness artifact of the plan's authoring assumption, not a change in what was proven."
  - "No worktree build/ directory pre-existed for this parallel agent (worktrees don't inherit the primary checkout's gitignored build tree). Initialized the vcpkg submodule locally and reconfigured CMake pointing VCPKG_INSTALLED_DIR at the main checkout's already-built build/x64-linux/vcpkg_installed to avoid a 15–40 min FFmpeg rebuild; only this worktree's own sources were recompiled."

requirements-completed: [BUILD-01, BUILD-05, DIR-04]

coverage:
  - id: D1
    description: "Fixture is case-collision-free: five names pairwise distinct under ASCII case folding, straddling the ASCII case boundary (Beta/Zulu below every lowercase stem), so five files land on any filesystem"
    requirement: DIR-04
    verification:
      - kind: unit
        ref: "tests/unit/test_dir_pairing.cpp#dir_pairing: the returned order is byte-wise sorted"
        status: pass
    human_judgment: false
  - id: D2
    description: "Expected order is an explicit byte-wise literal (not a self-sort), and a case-folded-sort teeth assertion proves the fixture still discriminates byte-wise order from case-folded order — locally demonstrated to fail against a non-discriminating fixture, then restored"
    requirement: DIR-04
    verification:
      - kind: unit
        ref: "tests/unit/test_dir_pairing.cpp#dir_pairing: the returned order is byte-wise sorted"
        status: pass
    human_judgment: false
  - id: D3
    description: "Permanent lint scans tests/ for filename-shaped literals colliding under ASCII case folding; self-tests against a synthetic known-bad input, refuses to report clean over a zero-file scan, detects the real pre-fix historical defect, and honours only a reasoned per-file opt-out"
    verification:
      - kind: other
        ref: "bash scripts/lint_fixture_case_collisions.sh (manual invocation, plus probe-tree and HEAD~1-recovered-file runs documented below)"
        status: pass
    human_judgment: false
  - id: D4
    description: "macOS leg (build (arm64-osx)) reports test #40 Passed and 100% tests passed, 0 tests failed out of 297"
    requirement: BUILD-05
    verification: []
    human_judgment: true
    rationale: "No case-insensitive filesystem exists on this Linux host and none can be simulated in a way that exercises write_file()'s truncating open the way APFS does. This SUMMARY makes no claim the arm64-osx leg is green — 02-16-PLAN.md (wave 2) wires this lint into CI, pushes, and reads the resulting run. That CI read is the only place this claim may be made."

duration: 25min
completed: 2026-08-16
status: complete
---

# Phase 02 Plan 15: Case-Collision-Free Byte-Wise-Order Fixture (G-02-4) Summary

**Replaced the colliding `Beta.snap.json`/`beta.snap.json` fixture pair with a straddling `Zulu.snap.json` stem, converted the ordering assertion to an explicit literal with a case-fold "teeth" check, and added `scripts/lint_fixture_case_collisions.sh` as a permanent scan gate — all locally verified on Linux; the macOS leg itself is unproven from this host.**

## Performance

- **Duration:** ~25 min
- **Tasks:** 2
- **Files modified:** 2 (1 test file edited, 1 new lint script)

## Accomplishments

- `tests/unit/test_dir_pairing.cpp`'s byte-wise-order fixture now holds five names pairwise distinct under ASCII case folding (`1`, `Beta`, `Zulu`, `alpha`, `zeta` stems), so the count that failed 4-vs-5 on APFS in CI run 31943688186 cannot recur for this fixture on any filesystem.
- The `REQUIRE(result->size() == names.size())` collision detector is unmodified.
- The expected order is now an explicit five-literal `const std::vector<std::string>` in byte-wise order (`1.snap.json`, `Beta.snap.json`, `Zulu.snap.json`, `alpha.snap.json`, `zeta.snap.json`), auditable from the source rather than derived by sorting the test's own result.
- A new "teeth" assertion sorts a copy of that expected order with an ASCII case-folding comparator (`std::tolower` cast through `unsigned char`) and checks it differs from the byte-wise order — proving the fixture still discriminates the two orderings rather than merely claiming to.
- **The teeth assertion was actually exercised, not just written:** the fixture was temporarily replaced with an all-lowercase, non-discriminating set, the build was rerun, and `ctest` confirmed the test FAILS (`CHECK( case_folded_order != expected_order )` failed with both sides printing the identical five-element list) — then the original fix was restored and reverified.
- `scripts/lint_fixture_case_collisions.sh` was created: it extracts filename-shaped double-quoted literals from every `*.cpp`/`*.h` under `tests/`, groups them by ASCII-lowercased form (forced via `LC_ALL=C`), and flags any group with more than one distinct spelling. It self-tests against a synthetic known-bad fixture on every invocation, refuses to report clean over a zero-file scan, honours a `fixture-case-allow: <reason>` per-file opt-out (a bare marker with no reason does not count and the file is still scanned), and documents its own line-based-scan/no-runtime-concatenation/ASCII-only-fold limitations.
- No path under `src/` appears in the diff; `src/cli/dir_pairing.cpp` (the correct, unmodified `std::map<std::string, FilePair>` byte-wise-ordering source of truth) was read for confirmation only.
- `.github/workflows/ci.yml` is untouched — wiring the new lint into CI is `02-16-PLAN.md`'s job.

## Task Commits

Each task was committed atomically:

1. **Task 1: A case-collision-free fixture that keeps the byte-order teeth** - `6be87ac` (test)
2. **Task 2: Permanent scan gate for case-colliding fixture names** - `a5e86d7` (chore)

_No plan-metadata commit in this worktree — SUMMARY.md is committed separately below; STATE.md/ROADMAP.md updates are the orchestrator's responsibility after this wave's worktrees merge._

## Files Created/Modified

- `tests/unit/test_dir_pairing.cpp` — fixture, expected order and teeth assertion in `dir_pairing: the returned order is byte-wise sorted`; `<cctype>` include added
- `scripts/lint_fixture_case_collisions.sh` — new permanent lint (created)

## Decisions Made

See `key-decisions` in frontmatter: the `Zulu.snap.json` substitution (not a drop, not a case-insensitive relaxation), the explicit-literal-plus-teeth-assertion design, the `HEAD~1` adjustment to Task 2's verify script (a plan-authoring staleness artifact given this executor's per-task atomic commits, not a change in what was proven), and the worktree build-environment setup (vcpkg submodule init + `VCPKG_INSTALLED_DIR` reuse to avoid a from-scratch FFmpeg rebuild).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Worktree had no pre-built `build/x64-linux` and no vcpkg submodule checked out**
- **Found during:** Task 1's precondition check (`ctest --test-dir build/x64-linux -N` requires a configured tree)
- **Issue:** This worktree is a fresh git worktree; `build/` is gitignored and not shared, and the `vcpkg` submodule was uninitialized, so neither the precondition's assumed configured tree nor the toolchain existed.
- **Fix:** Ran `git submodule update --init vcpkg` in the worktree, then configured CMake with `-DVCPKG_INSTALLED_DIR=<main-checkout>/build/x64-linux/vcpkg_installed -DVCPKG_MANIFEST_INSTALL=OFF` to reuse the main checkout's already-built FFmpeg/Catch2/etc. artifacts instead of rebuilding them from scratch (15–40 min). Only this worktree's own sources needed compiling.
- **Files modified:** none (build-environment setup only; no repo file touched)
- **Verification:** `cmake --build --preset x64-linux` completed clean; full suite at 297 tests, 100% passed
- **Committed in:** n/a — build-directory/environment setup, not a repo change

**2. [Rule 3 - Blocking] Task 2's verify script referenced a stale `HEAD` for the pre-fix fixture**
- **Found during:** Task 2 verification (proving the lint detects the real historical defect)
- **Issue:** The plan's automated `<verify>` block for Task 2 uses `git show HEAD:tests/unit/test_dir_pairing.cpp` to recover the pre-Task-1 content, written under the assumption Task 1's change was not yet committed when Task 2 ran. This executor commits atomically per task, so by Task 2's verification `HEAD` already carried Task 1's fix — `git show HEAD:...` would have returned the already-clean file, making the intended "prove the gate fires on the real historical defect" check vacuous.
- **Fix:** Used `git show HEAD~1:tests/unit/test_dir_pairing.cpp` instead — `HEAD~1` is the commit immediately before Task 1's, i.e. the exact original pre-fix content (confirmed to contain both `"Beta.snap.json"` and `"beta.snap.json"`). Ran the lint against that recovered content in an isolated probe directory (`/tmp/casold`) and confirmed it flagged `beta.snap.json: Beta.snap.json beta.snap.json` with exit 1.
- **Files modified:** none (verification-only substitution; the shipped script and its acceptance criteria are unaffected)
- **Verification:** `bash lint_fixture_case_collisions.sh` in `/tmp/casold` exited 1 and named both colliding spellings
- **Committed in:** n/a — a verification-step correction, not a code change

---

**Total deviations:** 2 auto-fixed (both Rule 3 — blocking issues that prevented literal execution of the plan's stated commands, neither changing what was built or what was proven)
**Impact on plan:** No scope creep. Both deviations are executor-environment/verification-script adjustments; the shipped artifacts (fixture, lint script) match the plan's `must_haves` and prohibitions exactly.

## Issues Encountered

None beyond the two deviations above.

## Verification Evidence (Linux-provable)

- `cmake --build --preset x64-linux` — clean, zero diagnostics under `-Wall -Wextra -Werror`.
- `grep -c '"Zulu.snap.json"'` → 2; `grep -c '"beta.snap.json"'` → 0; `grep -c '"Beta.snap.json"'` → 2.
- `grep -c 'result->size() == names.size()'` → 1 (collision detector intact).
- `grep -c 'std::tolower'` → 1; `grep -c 'unsigned char'` → 1; `grep -c '#include <cctype>'` → 1.
- `grep -c '__APPLE__\|TARGET_OS_MAC\|SKIP('` → 0 (no platform guard or skip).
- `git diff --name-only` (against dispatch base `f53f6d7`) → `scripts/lint_fixture_case_collisions.sh`, `tests/unit/test_dir_pairing.cpp` only; nothing under `src/`.
- `ctest --test-dir build/x64-linux -R '^unit\.dir_pairing'` → 11/11 passed.
- `ctest --test-dir build/x64-linux -N` → `Total Tests: 297`; full run → 100% passed, 0 failed out of 297.
- Teeth-assertion negative control: fixture temporarily replaced with an all-lowercase, non-discriminating set → test #40 FAILED with `CHECK( case_folded_order != expected_order )` showing identical lists on both sides → original restored, suite green again at 297.
- `bash scripts/lint_fixture_case_collisions.sh` from the repo root → clean, scanned 49 files.
- Probe tree (`Gamma.snap.json`/`gamma.snap.json`) → lint exits 1, names both spellings.
- `HEAD~1` recovery of the real pre-fix `test_dir_pairing.cpp` → lint exits 1, names `Beta.snap.json`/`beta.snap.json`.
- No-`tests`-directory invocation (`cd /tmp && bash .../lint_fixture_case_collisions.sh`) → exits 1 with the "scan target does not exist" diagnostic.
- Bare `fixture-case-allow:` marker (no reason) → file still scanned and still flagged.
- `fixture-case-allow: <reason>` marker → file skipped, skip printed.
- `grep -c 'Known limitation' scripts/lint_fixture_case_collisions.sh` → 1.
- `git diff --name-only` does not list `.github/workflows/ci.yml`.

## Honesty About What Is NOT Proven Here

No case-insensitive filesystem is available on this Linux host, and none can be simulated in a way that exercises `write_file()`'s truncating `std::ofstream` open the way APFS does. Every claim above is Linux-local or source-level. **This SUMMARY makes no claim that CI job `build (arm64-osx)` is green.** That read belongs exclusively to `02-16-PLAN.md` (wave 2), which wires this plan's lint (and 02-14's Windows-leg fix) into `.github/workflows/ci.yml`, pushes, and reads the resulting run.

## Next Phase Readiness

- `02-16-PLAN.md` can proceed: it wires `scripts/lint_fixture_case_collisions.sh` (this plan) and `02-14-PLAN.md`'s Windows-leg fix into the `lint`/CI jobs, pushes, and performs the only legitimate read of `build (arm64-osx)` and `build (x64-windows-static-md)` going green.
- No blockers introduced by this plan. The two pre-existing, non-blocking CI failures (`CI-x64-osx`, `CI-arm64-linux`) and the three deferred hygiene items recorded in `02-UAT.md` remain explicitly out of scope, as directed.

---
*Phase: 02-core-engine*
*Completed: 2026-08-16*
