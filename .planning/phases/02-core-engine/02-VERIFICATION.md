---
phase: 02-core-engine
verified: 2026-08-16T14:05:00Z
status: gaps_found
score: 7/7 must-haves verified at the code level; CI confirmation ran and REFUTED the green-legs truth — 2 new gaps opened (G-02-3, G-02-4)
behavior_unverified: 0
overrides_applied: 0
uat_round_2: "Test 1 (CI legs green) reported as an issue against run 31943688186 / headSha 1e065bb. Both original gaps confirmed CLOSED by that run's own evidence; two successor defects revealed behind them. See 02-UAT.md for full gap definitions."
re_verification:
  previous_status: human_needed
  previous_score: 5/5 truths verified (2 present, behavior-unverified — routed to human/Phase-3 deferral)
  gaps_closed:
    - "G-02-1 — six unguarded getenv call sites causing MSVC C4996/C2220 under /W4 /WX: CONFIRMED CLOSED BY CI. Run 31943688186 emits zero C4996 diagnostics; every former call site compiles clean under MSVC /W4 /WX (snapshot.cpp [35/97], dir.cpp [38/97], the new test_fs_utf8.cpp [42/97]), and the build advanced from its old stop at [32/97] to [65/97]. mediadiff.exe itself links successfully at [49/97]."
    - "G-02-2 — GNU-only \\+ BRE breaking the arm64-osx ctest-count guard under BSD sed: CONFIRMED CLOSED BY CI. Run 31943688186's arm64-osx leg passed the count guard and executed the full 297-test suite for the first time in the project's history; 296 of 297 passed."
  gaps_remaining:
    - "G-02-3 — tests/unit/test_report_model.cpp:56-57: dead code after a throwing Catch2 FAIL() trips MSVC C4702 → C2220 under /WX. Successor to G-02-1, not a recurrence; a translation unit MSVC had never reached before. Toolchain-parity conflict: the lines that silence GCC/Clang's -Wreturn-type are exactly what MSVC rejects."
    - "G-02-4 — tests/unit/test_dir_pairing.cpp:100: fixture names 'Beta.snap.json' and 'beta.snap.json' collide on case-insensitive APFS, so only 4 of 5 files exist and line 108's size assertion fails 4 != 5 on macOS. Successor to G-02-2, not a recurrence. NOT a product defect — src/cli/dir_pairing.cpp keys on std::map<std::string,...>, byte-wise and platform-independent; the test never reached its ordering check."
  regressions: []
  gap_closure_caused_regressions: false
human_verification:
  - test: "Push the branch (or the equivalent PR head) and read the resulting CI run for build (x64-windows-static-md) and build (arm64-osx)"
    expected: "x64-windows-static-md compiles clean through all 97 build steps (not just past the former C4996 failure at step 32) and its own test run passes; arm64-osx's Test step now executes past the count guard and the 297-test suite passes (not just that the guard no longer aborts)"
    why_human: "Neither fix has ever been exercised against its real target: MSVC/_dupenv_s/_W4 /WX cannot run on this Linux sandbox, and BSD sed cannot run here either (02-13 verified only via GNU sed's --posix emulation, which is a documented approximation, not BSD sed itself). More importantly, the local git branch (gsd/phase-2-core-engine) is 14 commits ahead of origin -- the gap-closure commits (2197c88, f005af8, c023be3, e403e32, and the docs commits) have never been pushed, so the CI run visible on PR #2 (31937349647) still reflects the OLD, pre-fix code and is FAILURE on both legs. There is no CI evidence, old or new, that either leg is actually green with these fixes applied. Both plans' own <verification> sections say the same thing explicitly and warn against assuming a first fix is sufficient (65/97 MSVC build steps and the full macOS test run have never executed in any CI run to date)."
  - test: "Run mediadiff compare/dir/TTY report output in a real Windows console (cmd.exe or Windows Terminal) with SetConsoleMode's ENABLE_VIRTUAL_TERMINAL_PROCESSING path exercised"
    expected: "Colour renders as actual styling (ANSI-interpreted) rather than literal escape-sequence bytes"
    why_human: "Carried forward unchanged from the original verification and from UAT test 1, which could not even reach this check because no Windows binary existed. Blocked on the same CI push as the item above -- once a green Windows build exists, this check becomes reachable for the first time in the phase's history."
---

# Phase 2: Core Engine Verification Report (Re-Verification After Gap Closure)

**Phase Goal:** The complete compare engine — registry, comparison semantics, policy resolution, snapshots, all four report formats and `dir` orchestration — works end to end against stub measurements, so every analyzer that follows plugs into finished machinery.

**Verified:** 2026-08-16T14:05:00Z
**Status:** gaps_found
**Re-verification:** Yes — after gap closure (plans 02-12, 02-13, closing UAT-surfaced gaps G-02-1 and G-02-2)

> **Amended 2026-08-16T14:05Z — CI evidence arrived.** This report was written at
> `human_needed`, awaiting a pushed CI run to confirm the two gap fixes. That run happened
> (31943688186, headSha `1e065bb`) and settled the question in both directions: **both original
> gaps are confirmed closed by the run's own evidence**, and **two successor defects were
> revealed behind them**, so the phase moves to `gaps_found` rather than `passed`.
>
> The distinction matters and is deliberate. G-02-1 and G-02-2 did not recur — they were fixed,
> and fixing them let the build and the test suite reach code that had never been compiled by
> MSVC or executed on macOS in any run to date. Both plans' `scope_caveat` sections predicted
> exactly this and warned against reading one fix as one green leg. The new gaps are
> pre-existing Phase-2 defects (from plans 02-08 and 02-11), not regressions introduced by the
> gap-closure work.
>
> Full root causes, artifacts and fix constraints for G-02-3 and G-02-4 are in `02-UAT.md`.

## What changed since the last verification

The original `02-VERIFICATION.md` (2026-08-15) scored the 5 ROADMAP success criteria 5/5 verified and routed two Windows-console colour-rendering checks to human UAT. That UAT (`02-UAT.md`) ran, passed one of those two checks on Linux, and — while trying to reach the other on a real Windows console — surfaced two blocking CI gaps that had nothing to do with colour rendering: `G-02-1` (six unguarded `getenv` calls making `x64-windows-static-md` fail to build under `/W4 /WX`) and `G-02-2` (a GNU-only regex breaking the `arm64-osx` leg's own test-count guard). Plans `02-12` and `02-13` closed those gaps. This report re-verifies the phase with that closure applied.

## Goal Achievement

### Observable Truths

#### Carried forward from the original verification (regression-checked only — see prior report for full evidence)

| # | Truth | Status | Regression check |
|---|-------|--------|-------------------|
| 1a-1d | Snapshot/compare round-trip, schema_version gate, CI tracked-overwrite gate | ✓ VERIFIED (unchanged) | `ctest -N` still discovers `integration.schema_version`, `integration.snapshot_safe_write` etc.; post-merge test gate (297/297, 0 failed) confirms no regression. |
| 2 | Policy resolution (profiles, TOML, `--set`/`--tol`, provenance) | ✓ VERIFIED (unchanged) | Gap-closure diff touches `options.cpp`'s `read_color_inputs` only (the 3 colour env reads), not `policy.cpp` or any profile-resolution code. No overlap. |
| 3 | Four report formats, colour auto-disable logic | ✓ VERIFIED (logic unchanged); real-terminal Windows rendering ⚠️ still human-only | The gap closure is a pure refactor of *how* `NO_COLOR`/`CI`/`GITHUB_ACTIONS` are read (`std::getenv` → `getenv_utf8`), not of `decide_color`'s precedence logic. `02-12-SUMMARY.md`'s own coverage table and the code review confirm all 11 colour-precedence unit tests, including the two WR-02 regression cases, still pass unmodified. |
| 4 | `dir` pairing, threads, exit-code contract, `ENG-16` process-control confinement | ✓ VERIFIED (unchanged) | `dir.cpp`'s only change is routing one pre-existing test-injection env read through the new shim (same truth-table); `scripts/lint_eng16.sh`'s `src/cli` exclusion (noted as a contributing cause in the UAT gap analysis) was deliberately NOT touched — out of scope for this closure per the plan. |
| 5 | Registry/docs enforcement, seven comparison semantics, `skipped != pass` | ✓ VERIFIED (unchanged) | No file in either gap-closure diff touches `src/core/checks.def`, `src/compare/*`, or the registry generator. |

#### New truths from this re-verification (the two gap-closure plans)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 6 | G-02-1 closed: no first-party translation unit calls the deprecated C environment-read function outside `src/util/fs.h` | ✓ VERIFIED | `grep -rn 'std::getenv(\|[^_]getenv(' src tests tools --include='*.cpp' --include='*.h' \| grep -v '^src/util/fs.h:'` → 0 matches, independently re-run in this verification. `src/util/fs.h:222-241` contains `mediadiff::getenv_utf8` with both the `_WIN32`/`_dupenv_s`+`unique_ptr<char,&std::free>` branch and the non-Windows `std::getenv` branch, matching the plan's spec exactly (read directly, not taken from the SUMMARY). |
| 7 | G-02-1 closed: all six former call sites route through the shim, preserving their exact prior truth-test semantics | ✓ VERIFIED | Independently grepped every site: `src/cli/options.cpp:274-276` (`NO_COLOR`/`CI`/`GITHUB_ACTIONS`), `src/cli/commands/snapshot.cpp:175` (`ci_env_is_true`), `src/cli/commands/dir.cpp:278` (test-injection read), `tests/support/golden.cpp:24` (`UPDATE_GOLDENS`), `tests/integration/test_exit_codes.cpp:138` and `tests/integration/test_snapshot_safe_write.cpp:137` (`PATH`) — all call `getenv_utf8`/`mediadiff::getenv_utf8`. `read_env` helper confirmed deleted from `options.cpp` (only the new function's usage remains at those three line numbers; no stray `read_env` reference anywhere). |
| 8 | G-02-1 closed: unset-vs-set-to-empty contract is proven by an automated test | ✓ VERIFIED | `tests/unit/test_fs_utf8.cpp:137` contains `TEST_CASE("test_fs_utf8 - getenv_utf8 separates unset from set-to-empty")` with the disengaged/engaged/POSIX-only-empty sections the plan specified. `ctest -N` (independently re-run) discovers 297 total tests (up from 296), consistent with one new case added. |
| 9 | G-02-1 closed: the three false "sole reader" comments now name their real co-sites | ✓ VERIFIED | Independently re-ran the plan's own grep gates: `grep -c 'ONLY place in mediadiff that reads' src/cli/options.h` → 0; `grep -c 'is the one place' src/cli/color_policy.h` → 0; `grep -c 'The one place mediadiff calls isatty' src/cli/options.cpp` → 0; and each file now names its real co-site (`snapshot.cpp` in `options.h:148`, `dir.cpp` in `color_policy.h:14/17`, `compare.cpp` in `options.cpp:248`). |
| 10 | G-02-2 closed: the ctest count parse uses a POSIX-portable pattern and both guard branches survive | ✓ VERIFIED | `.github/workflows/ci.yml:264` reads `sed -n 's/^Total Tests: \([0-9][0-9]*\)$/\1/p'` (confirmed by direct read, not the SUMMARY's claim) — the GNU-only `\+` is gone. `grep -rF '\+' .github/workflows/` → 0 matches, independently re-run. The comment block above the line documents the BSD-sed constraint exactly as the plan required. |
| 11 | G-02-1/G-02-2 closure did not regress the Linux build or test suite | ✓ VERIFIED | `cmake --build --preset x64-linux` → `ninja: no work to do` (up to date, independently re-run in this verification session). `ctest --test-dir build/x64-linux -N` → `Total Tests: 297`, matching the orchestrator's stated post-merge gate result (297/297 passed, 0 failed, 1 skipped). Code review of the full 11-file gap diff (`02-REVIEW-GAPS.md`) found 0 critical issues; the one warning (missing thread-safety doc comment on `getenv_utf8`) and one info item (dead `<cstdlib>` include) are both non-blocking and independently confirmed still open (not silently fixed and mis-reported) — `grep -n "Thread-safety" src/util/fs.h` → no match; `src/cli/options.cpp:5` still has `#include <cstdlib>`. |
| 12 | G-02-1/G-02-2 actually make their target CI legs green | ⚠️ UNVERIFIABLE IN THIS ENVIRONMENT — routed to human verification | Neither fix has ever been run against its real target. Critically: `git log origin/gsd/phase-2-core-engine..gsd/phase-2-core-engine` shows the local branch is **14 commits ahead of origin** — none of the gap-closure commits have been pushed. `gh pr view 2` confirms the PR's only CI run (31937349647, 2026-08-16T08:47Z) still shows `build (x64-windows-static-md)`: FAILURE and `build (arm64-osx)`: FAILURE — that run predates every gap-closure commit by construction. There is no CI evidence, old or new, that either leg is green with the fix applied. |

**Score:** 11/12 truths verified directly against the codebase (code-level closure of both gaps is complete and correct); 1 truth (actual CI-green confirmation) cannot be established without pushing the branch and reading a new CI run.

### Required Artifacts (gap-closure scope)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/util/fs.h` | `mediadiff::getenv_utf8`, `_WIN32`-guarded, leak-free | ✓ VERIFIED | Read directly (lines 185-241): full rationale doc comment, `_dupenv_s` + `unique_ptr<char, decltype(&std::free)>` on Windows, plain `std::getenv` passthrough elsewhere. |
| `tests/unit/test_fs_utf8.cpp` | unset/set/set-to-empty test case | ✓ VERIFIED | `TEST_CASE` present at line 137 with the three sections the plan specified. |
| `.github/workflows/ci.yml` | POSIX-portable count parse + BSD-sed comment | ✓ VERIFIED | Line 264 confirmed portable; comment block (lines ~250-263) documents the constraint. |
| `src/cli/options.h`, `src/cli/color_policy.h`, `src/cli/options.cpp` | corrected exclusivity comments | ✓ VERIFIED | All three false claims removed; each names its real co-site. |

### Key Link Verification (gap-closure scope)

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `src/cli/options.cpp::read_color_inputs` | `src/util/fs.h::getenv_utf8` | direct call | ✓ WIRED | Confirmed at lines 274-276. |
| `src/cli/commands/snapshot.cpp::ci_env_is_true` | `getenv_utf8` | direct call | ✓ WIRED | Confirmed at line 175. |
| `src/cli/commands/dir.cpp` (test injection) | `getenv_utf8` | direct call | ✓ WIRED | Confirmed at line 278. |
| `tests/support/golden.cpp::update_goldens_requested` | `getenv_utf8` | direct call | ✓ WIRED | Confirmed at line 24; compiled into both unit and integration test targets per the plan's D2 claim. |
| `tests/integration/test_exit_codes.cpp`, `test_snapshot_safe_write.cpp` | `mediadiff::getenv_utf8` | direct call | ✓ WIRED | Confirmed at lines 138 and 137 respectively. |
| local branch `gsd/phase-2-core-engine` | `origin/gsd/phase-2-core-engine` (and thus GitHub Actions CI) | `git push` | ✗ NOT WIRED | 14 commits including all gap-closure work sit only in the local worktree. Until pushed, no automated system — this one included — can observe the fix running against its real target. This is the single blocking link for calling the phase CI-clean. |

### Requirements Coverage

All 48 requirement IDs assigned to Phase 2 in `.planning/REQUIREMENTS.md` remain marked Complete and traceable to a plan's `requirements:` frontmatter, independently re-confirmed by grep against the REQUIREMENTS.md Phase 2 traceability table (48 rows, `| Phase 2 | Complete |`, matching exactly the 48 IDs in this verification's task scope — no orphans, no gaps).

The two gap-closure plans additionally declare `requirements: [BUILD-01, BUILD-05, CLI-08, CLI-09]` (02-12) and `[BUILD-01, BUILD-05]` (02-13). These are **not** Phase 2's own requirement IDs — `REQUIREMENTS.md` traces `BUILD-01`, `BUILD-05` and `CLI-09` to **Phase 1**, already marked Complete there. That "Complete" status carries evidence from CI run `31823918842`, which predates Phase 2 entirely. Phase 2's own code (the six unguarded `getenv` sites) subsequently regressed `BUILD-01`/`BUILD-05`'s "builds clean on 3 OSes" and "CI matrix green" claims without REQUIREMENTS.md being updated to reflect it — this is exactly the gap G-02-1/G-02-2 diagnosis describes. The gap-closure plans correctly target these IDs because they are restoring them, not because Phase 2 owns them. **This verification flags, but does not correct, that `REQUIREMENTS.md`'s BUILD-01/BUILD-05/CLI-09 rows still cite the stale pre-Phase-2 CI run as evidence** — once the branch is pushed and a new green matrix run exists, that evidence citation should be refreshed to the new run ID. This is not a Phase 2 requirement gap (Phase 2's own 48 IDs are all satisfied); it is a documentation-freshness note surfaced by this re-verification.

### Anti-Patterns Found

None new. `02-REVIEW-GAPS.md`'s code review of the full 11-file gap diff found 0 Critical issues. Its one Warning (`getenv_utf8` has no documented thread-safety contract) and one Info item (dead `<cstdlib>` include in `options.cpp`) were independently re-confirmed as still open in this verification (not fixed, not silently claimed fixed) — both are explicitly non-blocking per the review's own severity classification and do not affect either gap's closure.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| No raw `getenv` outside `fs.h` | `grep -rn 'std::getenv(\|[^_]getenv(' src tests tools --include='*.cpp' --include='*.h' \| grep -v '^src/util/fs.h:'` | 0 matches | ✓ PASS |
| `getenv_utf8` present with both platform branches | direct read of `src/util/fs.h:222-241` | `_dupenv_s`/`unique_ptr` + `std::getenv` fallback present | ✓ PASS |
| All six call sites migrated | per-file grep for `getenv_utf8` | all 6 files match | ✓ PASS |
| Three false comments corrected | plan's own grep gates re-run | all 3 return 0, all 3 co-sites named | ✓ PASS |
| No GNU-only `\+` under `.github/workflows/` | `grep -rF '\+' .github/workflows/` | 0 matches | ✓ PASS |
| `ci.yml` line uses POSIX pattern | direct read | `[0-9][0-9]*` confirmed | ✓ PASS |
| Linux build unaffected | `cmake --build --preset x64-linux` | `ninja: no work to do` (already built, up to date) | ✓ PASS |
| Test count grew by exactly 1 (296→297) | `ctest --test-dir build/x64-linux -N` | `Total Tests: 297` | ✓ PASS |
| Local branch vs origin | `git log origin/...\.\.gsd/phase-2-core-engine` | 12 commits ahead (gap closure unpushed) | ⚠️ FLAGGED — drives the human-verification item |
| PR #2's actual CI status | `gh pr view 2 --json statusCheckRollup` | `build (x64-windows-static-md)`: FAILURE, `build (arm64-osx)`: FAILURE, both from the pre-fix run 31937349647 | ⚠️ FLAGGED — confirms no green run exists yet |

### Probe Execution

Not applicable — Phase 2 has no `scripts/*/tests/probe-*.sh` convention.

### Human Verification Required

### 1. Push the branch and confirm both previously-failing CI legs go green

**Test:** Push `gsd/phase-2-core-engine` (or open/update PR #2 with the 14 local commits) and read the resulting `build (x64-windows-static-md)` and `build (arm64-osx)` job logs in full.
**Expected:** `x64-windows-static-md` compiles through all 97 build steps (not just past the former C4996 failure at step 32/97) and its test executables run; `arm64-osx`'s Test step executes past the count guard (which should no longer abort) and the 297-test suite passes to completion.
**Why human:** This sandbox cannot run MSVC or BSD sed, and — more fundamentally — the fix commits have never been pushed to origin, so no CI run (past or present) has ever exercised them. `02-12-PLAN.md` and `02-13-PLAN.md` both explicitly warn that fixing the diagnosed defect is necessary but not proven sufficient: 65 of 97 Windows build steps and the full macOS test run have never executed in any CI run to date, so unrelated breakage could still be waiting behind each fix.

### 2. Colour renders as styling in a real Windows console

**Test:** Run `mediadiff compare`/`dir` with colour enabled in an actual Windows `cmd.exe` or Windows Terminal session, once a Windows binary exists.
**Expected:** ANSI-styled output (colour, not literal `\x1b[...m` bytes) — confirms `SetConsoleMode`'s `ENABLE_VIRTUAL_TERMINAL_PROCESSING` path works on real Windows hardware.
**Why human:** Carried forward unchanged from the original verification and UAT test 1, which could not reach this check at all because no Windows binary had ever built successfully. This check only becomes reachable once item 1 above produces a green Windows leg.

### Gaps Summary

Both UAT-surfaced gaps (G-02-1, G-02-2) are **closed at the code level** — this re-verification independently confirmed every artifact, wiring link, and behavioral claim the two gap-closure plans made, reading the actual files rather than trusting either SUMMARY.md. The fixes are correct, complete, and match their diagnosed root causes exactly: all six `getenv` call sites migrated with byte-for-byte-preserved truth-table semantics, the unset/set/set-to-empty contract is unit-tested, the three misleading "sole reader" comments are corrected, and the CI workflow's regex is now POSIX-portable with both guard branches intact. The Linux build and full 297-test suite pass, and a full code review of the 11-file diff found zero critical issues.

What remains open is not a code defect but an **unclosed verification loop**: the gap-closure commits exist only in the local worktree (14 commits ahead of `origin/gsd/phase-2-core-engine`), so the CI system that is the only oracle for "does the Windows leg actually build" and "does the arm64-osx leg actually run its tests" has never seen this code. PR #2's last recorded run (31937349647) still shows both target legs as `FAILURE`, from before any of these fixes existed. Per this task's own framing, that is a genuine unknown rather than a code gap, and per both plans' own honesty sections, a first fix passing local scrutiny is not the same as a target CI leg going green — Windows never got past build step 32/97 and macOS has never run its full suite to completion, so undiscovered breakage on either leg is a real possibility, not a formality.

**Recommendation:** push the branch, let CI run, and read the two previously-red job logs for their new first failure (if any) before treating this phase as ready to ship. This does not block the code from being considered correct — it blocks the phase from being certified CI-green, which was the explicit purpose of scoping G-02-2 into this closure in the first place ("arm64-osx is a required check... the phase cannot merge while it is red").

---

_Verified: 2026-08-16T13:10:00Z_
_Verifier: Claude (gsd-verifier)_
