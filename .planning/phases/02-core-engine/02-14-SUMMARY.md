---
phase: 02-core-engine
plan: 14
subsystem: testing
tags: [catch2, msvc, warnings-as-errors, toolchain-parity, ci, lint]

requires:
  - phase: 02-core-engine
    provides: "02-08's ReportModel/GroupBlock report layer, whose test suite (test_report_model.cpp) contained the block_for helper this plan restructures"
provides:
  - "block_for() in tests/unit/test_report_model.cpp restructured to a single reachable exit, dissolving the MSVC C4702 -> C2220 vs GCC/Clang -Wreturn-type toolchain-parity conflict"
  - "scripts/lint_dead_code_after_fail.sh — a permanent, self-testing scan gate for the FAIL()-then-statement shape across tests/*.cpp and tests/*.h"
affects: ["02-16 (wires the new lint into the lint job and reads the resulting Windows CI run)"]

actuals:
  tokens: 2412
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Test helper 'single reachable exit' shape: std::find_if + INFO + REQUIRE + one return, in place of a throwing FAIL() followed by a fall-through object — the pattern test_transform_profile.cpp's transform_check() already used, now converged on in test_report_model.cpp too."
    - "Stateful per-file awk lint (search / consume-multiline-call / judge-next-line) with a self-test control clause run before every real scan, matching the 02-13-PLAN.md precedent of proving the harness fires on a synthetic known-bad input rather than trusting a clean result at face value."

key-files:
  created:
    - scripts/lint_dead_code_after_fail.sh
  modified:
    - tests/unit/test_report_model.cpp

key-decisions:
  - "block_for() keeps its reference-returning signature and all five call sites untouched — a pointer-returning variant was rejected (per the plan) because it would force a `->` rewrite plus a null guard at every call site for no benefit."
  - "The lint matches only the literal FAIL macro (not FAIL_CHECK, which does not throw and so has no unreachable-code consequence) — scoped tightly to the actual defect class G-02-3 diagnosed."
  - "The worktree's build/x64-linux tree did not exist and vcpkg's submodule pin required a git history object the initial shallow clone lacked (ffmpeg@8.1#4's tree); resolved by `git submodule update --init` then `git fetch --unshallow` inside vcpkg before configuring — the local vcpkg binary cache (~/.cache/vcpkg/archives) made the subsequent configure/build a cache-hit, not a 15-40 min FFmpeg rebuild."

patterns-established:
  - "Self-testing lint gates: any new line-based lint should carry a synthetic known-bad fixture run through the identical matcher before the real scan, and refuse to report clean if that control fails."

requirements-completed: [BUILD-01, BUILD-05]

coverage:
  - id: D1
    description: "block_for() restructured to a single reachable exit; no toolchain has dead code to diagnose or a missing return path to complain about"
    requirement: "BUILD-05"
    verification:
      - kind: unit
        ref: "ctest -R '^unit\\.report_model' — 11/11 passed"
        status: pass
      - kind: other
        ref: "cmake --build --preset x64-linux (GCC 13, -Wall -Wextra -Werror) — clean"
        status: pass
    human_judgment: false
  - id: D2
    description: "Windows leg (x64-windows-static-md) compiles test_report_model.cpp.o with zero C4702/C2220 and reaches step [97/97]"
    requirement: "BUILD-01"
    verification: []
    human_judgment: true
    rationale: "MSVC cannot run on this Linux host, and the local Clang -Wunreachable-code proxy was confirmed (ahead of planning) to produce zero diagnostics against this exact shape. Only a pushed CI run can prove C4702 is gone; 02-16-PLAN.md owns that read."
  - id: D3
    description: "scripts/lint_dead_code_after_fail.sh created: reports the repo clean, fires on a synthetic known-bad fixture (both via its own self-test and an external probe), refuses to report clean over a zero-file scan, stays quiet on tests/support/golden.cpp's correctly-shaped FAIL(), and documents its line-based limitations"
    requirement: "BUILD-05"
    verification:
      - kind: other
        ref: "bash scripts/lint_dead_code_after_fail.sh — clean, scanned 49 files"
        status: pass
      - kind: other
        ref: "external probe: /tmp/lintprobe (cd + bash lint_dead_code_after_fail.sh) — exits 1, names tests/probe.cpp:4"
        status: pass
      - kind: other
        ref: "zero-tests-dir probe: cd /tmp && bash .../lint_dead_code_after_fail.sh — exits 1 with zero-files diagnostic"
        status: pass
      - kind: other
        ref: "isolated golden.cpp scan (/tmp/goldenprobe) — clean, 0 findings"
        status: pass
    human_judgment: false

duration: 19min
completed: 2026-08-16
status: complete
---

# Phase 02 Plan 14: Dissolve the block_for toolchain-parity conflict Summary

**Restructured a Catch2 test helper's dead-code shape that blocked the MSVC `/W4 /WX` build (C4702→C2220), and shipped a self-testing lint that scans the whole test tree for the same shape going forward.**

## Performance

- **Duration:** 19 min
- **Started:** 2026-08-16T13:53:50+02:00 (base commit f53f6d7)
- **Completed:** 2026-08-16T14:12:31+02:00
- **Tasks:** 2
- **Files modified:** 2 (1 created, 1 modified)

## Accomplishments

- `block_for()` in `tests/unit/test_report_model.cpp` no longer throws unconditionally and falls through to a dead-code return; it now does `std::find_if` + `INFO` (naming the missing group via `group_to_string`) + `REQUIRE` + one reachable `return *it;`. Signature (`const mediadiff::GroupBlock&`, taking `const ReportModel&` and `Group`) and all five call sites are byte-for-byte unchanged.
- Confirmed locally that this is genuinely a *dissolution*, not a suppression: the tree builds clean under GCC 13's `-Wall -Wextra -Werror -O3 -DNDEBUG` (the target's actual flags), all 11 `unit.report_model` tests pass, and the suite is still exactly 297 tests.
- New `scripts/lint_dead_code_after_fail.sh` — a permanent, self-testing awk-based scan gate that flags any statement following a Catch2 `FAIL()` call before its enclosing block closes, across every `*.cpp`/`*.h` under `tests/`. It carries a self-test control clause (a synthetic known-bad fixture run through the identical matcher, on every invocation) and a zero-files guard, matching the discipline `02-13-PLAN.md` established after the round-1 gap that shipped a verification which had only ever run one way.

## Task Commits

Each task was committed atomically:

1. **Task 1: Give block_for a single reachable exit** - `c59a6ed` (fix)
2. **Task 2: Permanent scan gate for the FAIL()-then-statement shape** - `9aa7fa2` (feat)

_Note: this SUMMARY and metadata commit follow as a separate `docs` commit; STATE.md/ROADMAP.md are NOT touched by this worktree agent — the orchestrator owns those after all wave-1 plans complete._

## Files Created/Modified

- `tests/unit/test_report_model.cpp` - `block_for()` body restructured to a single reachable exit (`std::find_if` + `INFO` + `REQUIRE` + `return *it;`); `<algorithm>` added to the include block (alphabetically ahead of `<string>`); a short comment above the helper records why the shape is written this way and names the new lint as the tree-wide gate. No call site, `TEST_CASE`, or other function touched.
- `scripts/lint_dead_code_after_fail.sh` - new. Scans `tests/*.cpp`/`tests/*.h` with a three-state awk matcher (search → consume a possibly-multiline `FAIL(...)` call → judge the next non-blank/non-comment/non-preprocessor line). Self-tests against a synthetic known-bad fixture before every real scan; refuses to report clean over a zero-file scan; documents its line-based (not tokenizer) limitations in a "Known limitation" paragraph matching `lint_eng16.sh`'s and `lint_check_id_strings.sh`'s register.

## Decisions Made

- Kept `block_for()`'s reference-returning signature exactly as specified in the plan — a pointer-returning rewrite was explicitly rejected to avoid forcing a `->` + null-guard rewrite at all five call sites for no benefit.
- The lint's pattern matches only the literal `FAIL` macro name (word-boundary guarded so it does not also match `FAIL_CHECK`, which is non-throwing and therefore never produces the unreachable-code shape this gate defends against) — kept narrowly scoped to the actual defect class rather than generalized to every Catch2 assertion macro.
- This worktree had no pre-existing `build/x64-linux` tree and vcpkg's submodule was unpopulated; the pinned commit's `ffmpeg@8.1#4` port tree object was unavailable under the default shallow submodule clone. Resolved with `git submodule update --init --depth 1` followed by `git fetch --unshallow` inside `vcpkg/`, then `cmake --preset x64-linux`. The local vcpkg binary archive cache (`~/.cache/vcpkg/archives`, already populated from prior work on this machine) made the subsequent `vcpkg install` a cache restore (27.6 ms per the log) rather than a 15-40 min uncached FFmpeg build, so the plan's "seconds, not minutes" incremental-compile assumption held once configuration completed. This is scoped to environment setup — no plan file, build flag, or source outside the two listed artifacts was touched.

## Deviations from Plan

**None affecting the plan's `files_modified` or acceptance criteria.** One environment-setup step not explicit in the plan text:

**1. [Rule 3 - Blocking] Populated the vcpkg submodule and unshallowed its clone to satisfy Task 1's precondition**
- **Found during:** Task 1 precondition check
- **Issue:** The plan's precondition assumed `build/x64-linux` was already a configured CMake build tree in this worktree; it was not — the worktree had no `build/` directory and the `vcpkg` submodule directory was empty (`git submodule status` showed the pin but no checkout).
- **Fix:** `git submodule update --init --depth 1`, then `git fetch --unshallow` inside `vcpkg/` (the pinned `ffmpeg@8.1#4` port tree object was not reachable under the initial shallow clone), then `cmake --preset x64-linux` and `cmake --build --preset x64-linux`.
- **Files modified:** None under version control — `vcpkg/` is a submodule checkout and `build/` is gitignored.
- **Verification:** `ctest --test-dir build/x64-linux -N` printed `Total Tests: 297` before any code change, confirming the precondition's literal text was then true; the vcpkg install log showed a cache restore, not a rebuild.
- **Committed in:** N/A (no tracked files changed by this step)

---

**Total deviations:** 1 auto-fixed (1 blocking, environment setup only)
**Impact on plan:** No scope creep — this made the plan's own precondition literally true rather than working around it. No plan artifact, build flag, or unrelated source was touched.

## Issues Encountered

- My first draft of `block_for()`'s explanatory comment referenced "FAIL()'s throw" in prose, which itself matched the plan's own `grep -c 'FAIL(' tests/unit/test_report_model.cpp` acceptance check (expected exactly 0) — the literal string `FAIL(` appearing anywhere in the file, including comments, fails that criterion. Reworded to "that macro throws" with no literal `FAIL(` substring; rebuilt and reverified before committing.

## Known Stubs

None. Both artifacts are complete, real implementations — no placeholder text, no empty stub bindings.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `tests/unit/test_report_model.cpp` and `scripts/lint_dead_code_after_fail.sh` are both ready for `02-16-PLAN.md` to wire the new lint into `.github/workflows/ci.yml`'s `lint` job and push for a real CI read.
- **What this plan does NOT prove, by design:** no MSVC, `/W4`, `/WX`, or C4702 diagnostic was run anywhere in this execution — only a pushed CI run can prove the Windows leg's C4702→C2220 failure is gone. The suggested local Clang proxy (`-Wunreachable-code -Wunreachable-code-return`) was confirmed ahead of planning to produce zero diagnostics against this exact shape and was not attempted here.
- **Expect a further iteration on this leg, as the plan itself documents.** Build steps [66/97]-[97/97] (four unit TUs and all fourteen integration TUs) have never been compiled by MSVC in any run in this project's history; this plan's lint narrows exactly one known defect class across that unexecuted set and certifies nothing else about it. A red Windows leg on the next push is not evidence this fix failed — it would be a new first failure to read fresh.
- `02-15-PLAN.md` (G-02-4, the `arm64-osx` fixture-portability gap) runs independently in the same wave; neither plan alone turns CI green.

## Self-Check: PASSED

- FOUND: tests/unit/test_report_model.cpp
- FOUND: scripts/lint_dead_code_after_fail.sh
- FOUND commit: c59a6ed
- FOUND commit: 9aa7fa2

---
*Phase: 02-core-engine*
*Completed: 2026-08-16*
