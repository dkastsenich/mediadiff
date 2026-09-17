---
phase: 04-video-analysis
plan: 17
subsystem: build
tags: [gcc, warnings-as-errors, pragma, lint, ci, wr-03]

requires:
  - phase: 04-video-analysis
    provides: "04-11/04-12's six video analyzer translation units (color.cpp, frame_types.cpp, gop.cpp, hdr.cpp, interlace.cpp, stream_params.cpp) and 04-REVIEW.md's WR-03 finding naming their unbalanced suppressions"
provides:
  - "Zero file-scope, unbalanced `#pragma GCC diagnostic ignored` suppressions remain under src/analyzers/video/"
  - "A CI-enforced balance lint (scripts/lint_pragma_scope.sh) that fails if a future analyzer file copies the unbalanced form forward"
  - "Per-file measured evidence of whether -Wmaybe-uninitialized still fires on GCC 13.3.0 -O3, recorded in each surviving/removed comment"
affects: ["04-20 (draft-PR three-OS CI run, where Clang/AppleClang/MSVC exposure for the guard this plan preserves gets its first real confirmation)"]

actuals:
  tokens: 5500
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns: ["Bracketed `#pragma GCC diagnostic push`/`ignored`/`pop` around only the flagged function body, inside the pre-existing `#if defined(__GNUC__) && !defined(__clang__)` guard, in place of a file-scope suppression with no `pop`"]

key-files:
  created:
    - scripts/lint_pragma_scope.sh
  modified:
    - src/analyzers/video/color.cpp
    - src/analyzers/video/frame_types.cpp
    - src/analyzers/video/gop.cpp
    - src/analyzers/video/hdr.cpp
    - src/analyzers/video/interlace.cpp
    - src/analyzers/video/stream_params.cpp
    - .github/workflows/ci.yml

key-decisions:
  - "Measured per file against this workstation's GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) rather than assuming the suppression is still needed everywhere it was copied: four of six files (color.cpp, frame_types.cpp, hdr.cpp, interlace.cpp) no longer trigger -Wmaybe-uninitialized at all and had their suppressions removed outright; two (gop.cpp, stream_params.cpp) still trigger it and were bracketed with push/pop around exactly the flagged function bodies (push_skip in both, plus emit_gop_refs in gop.cpp)."
  - "The new lint's scope is explicitly src/analyzers/video/ only, matching the plan's prohibition against touching src/analyzers/container/ or src/analyzers/size/, which carry the same older unbalanced pattern and are out of scope for this gap-closure plan by explicit decision, not oversight."

patterns-established:
  - "Diagnostic-suppression comment convention: a surviving pragma's comment states the measured compiler version, optimisation level, and exact construction site GCC flagged, rather than a general claim about 'a false positive this file works around' with no measurement behind it."

requirements-completed: [BUILD-05]

coverage:
  - id: D1
    description: "No translation unit under src/analyzers/video/ silences -Wmaybe-uninitialized for the remainder of the file; every suppression is either removed or bracketed to its construction site, and the Release (-O3) GCC build stays warning-clean with the six translation units freshly compiled."
    requirement: "BUILD-05"
    verification:
      - kind: other
        ref: "cmake --build --preset x64-linux (forced recompile of src/analyzers/video/*.cpp)"
        status: pass
      - kind: other
        ref: "ctest --preset x64-linux --output-on-failure (770/770 passed, 6 skipped by design, no drop from baseline)"
        status: pass
    human_judgment: false
  - id: D2
    description: "scripts/lint_pragma_scope.sh enforces the push/ignored/pop balance under src/analyzers/video/ in CI, proven able to fail against a real deliberate mutation and to pass clean after reverting it."
    requirement: "BUILD-05"
    verification:
      - kind: other
        ref: "bash scripts/lint_pragma_scope.sh (clean run) and the recorded mutation run below (non-zero exit naming the file)"
        status: pass
      - kind: other
        ref: "bash scripts/lint_bash4_builtins.sh (bash-3.2 portability of the new script)"
        status: pass
    human_judgment: false

duration: 20min
completed: 2026-09-13
status: complete
---

# Phase 04 Plan 17: Scope or remove file-scope `-Wmaybe-uninitialized` suppressions Summary

Measured, per file, whether GCC 13.3.0 still needs the file-scope `-Wmaybe-uninitialized` suppression each of the six `src/analyzers/video/*.cpp` files carried with no matching `pop`; removed it from four files where it fired nothing, bracketed it to the exact flagged function in the other two, and added a CI lint that fails if the unbalanced form is ever copied into a seventh file.

## Performance

- **Duration:** ~20 min
- **Tasks:** 2
- **Files modified:** 8 (6 analyzer files, 1 new lint script, 1 CI workflow)

## Accomplishments

- Measured each of the six files against this workstation's `c++ --version`: `c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`, at `-O3` (the `CMAKE_BUILD_TYPE=Release` every CMake preset in this project sets):
  - `color.cpp`, `frame_types.cpp`, `hdr.cpp`, `interlace.cpp`: `-Wmaybe-uninitialized` did **not** fire with the suppression removed and the translation unit force-recompiled. Suppression and its now-false comment removed entirely.
  - `gop.cpp`: fired in two places — `push_skip()`'s `measurement.value = Absent{};` and `emit_gop_refs()`'s `measurement.value = *pstream.ref_frame_count;`. Each function body individually bracketed with `#pragma GCC diagnostic push` / `ignored` / `pop`.
  - `stream_params.cpp`: fired once, in `push_skip()`'s `measurement.value = Absent{};`. That function body bracketed the same way.
- Every surviving pragma stays inside the pre-existing `#if defined(__GNUC__) && !defined(__clang__)` guard, unchanged.
- `cmake --build --preset x64-linux` completes with zero `warning:`/`error:` lines after forcing all six translation units to recompile.
- `ctest --preset x64-linux --output-on-failure`: 770/770 passed (6 skipped by design, the designated-CI-leg-only goldens), no drop from the pre-plan baseline — no `emit_*` behavior changed.
- Created `scripts/lint_pragma_scope.sh`: bash-3.2-safe, counts non-comment `diagnostic ignored`/`push`/`pop` lines under `src/analyzers/video/*.cpp,*.h` and fails when the three counts disagree for any file. Carries a zero-input guard, a self-test control clause (known-bad/known-good/comment-only fixtures), and an explicit disclosure that `src/analyzers/container/` and `src/analyzers/size/` are deliberately out of scope.
- Wired the lint into `.github/workflows/ci.yml`'s `lint` job as a new named step, "Run diagnostic-suppression push/pop balance lint (WR-03)".
- Proved the lint can fail: added a second `#pragma GCC diagnostic ignored "-Wunused-variable"` inside `stream_params.cpp`'s existing push/pop guard (still inside the `#if defined(__GNUC__)` block), ran the lint — it exited 1 and printed `UNBALANCED src/analyzers/video/stream_params.cpp ignored=2 push=1 pop=1` — then reverted with `git checkout --` and re-ran to a clean exit 0. See "Mutation Test Evidence" below for both raw outputs.

## Task Commits

1. **Task 1: Measure whether the suppression is still needed, then scope or remove it in all six files** — `c353abe` (fix)
2. **Task 2: Make the balance rule enforceable and prove the lint can fail** — `8e56304` (feat)

_No `docs()` metadata commit was made separate from this SUMMARY's own commit — see Deviations._

## Files Created/Modified

- `src/analyzers/video/color.cpp` — file-scope suppression removed (measured: does not fire)
- `src/analyzers/video/frame_types.cpp` — file-scope suppression removed (measured: does not fire)
- `src/analyzers/video/gop.cpp` — file-scope suppression replaced by two function-scoped push/pop brackets (`push_skip`, `emit_gop_refs`)
- `src/analyzers/video/hdr.cpp` — file-scope suppression removed (measured: does not fire)
- `src/analyzers/video/interlace.cpp` — file-scope suppression removed (measured: does not fire)
- `src/analyzers/video/stream_params.cpp` — file-scope suppression replaced by one function-scoped push/pop bracket (`push_skip`)
- `scripts/lint_pragma_scope.sh` — new CI lint enforcing the balance rule under `src/analyzers/video/`
- `.github/workflows/ci.yml` — new lint-job step invoking the script

## Decisions Made

- Measured rather than assumed: the plan's own flagged assumption ("whether `-Wmaybe-uninitialized` still fires at all on this workstation's GCC is unknown until Task 1 measures it") resolved to "no" for four of six files and "yes, at a specific line" for the other two — recorded in each file's own comment (or removal) rather than left as a blanket assumption.
- Bracketed at function granularity, not statement granularity: `emit_gop_refs` has two `Measurement` constructions (one `Absent{}` skip path, one real value), and GCC's inlined diagnostic pointed at the second; the whole function was bracketed rather than only the second block, since the plan's own guidance says "the single `emit_*` function body GCC named, not the file, and not the whole anonymous namespace" and the function is small enough that function-level bracketing is already the narrowest practical unit.

## Deviations from Plan

None — plan executed exactly as written. One clarification: the plan's `<output>` step says "Create `.planning/phases/04-video-analysis/04-17-SUMMARY.md` when done," and this executor run committed the two production-code tasks individually per `execute-plan.md`'s per-task commit protocol; this SUMMARY and the STATE/ROADMAP/REQUIREMENTS metadata are committed together in the standard `docs(04-17): ...` metadata commit that follows this file, per that same workflow's `git_commit_metadata` step — not a deviation, just noting the commit sequence for the reader of `git log`.

## Mutation Test Evidence (Task 2)

**Failing run** (deliberate second `diagnostic ignored` added inside `stream_params.cpp`'s existing guard):

```
lint_pragma_scope.sh: self-test control clause fired correctly (known-bad fixture flagged, known-good fixture passed clean, comment-only mention ignored).
UNBALANCED src/analyzers/video/stream_params.cpp ignored=2 push=1 pop=1
pragma-scope violation: at least one file under src/analyzers/video/ has a diagnostic suppression whose 'ignored' count does not equal its 'push'/'pop' counts -- a suppression left open past the statement that needs it (WR-03).
```
Exit code: 1

**Green run** (after `git checkout -- src/analyzers/video/stream_params.cpp`):

```
lint_pragma_scope.sh: self-test control clause fired correctly (known-bad fixture flagged, known-good fixture passed clean, comment-only mention ignored).
lint_pragma_scope.sh: clean. Scanned 7 file(s) under src/analyzers/video/*.cpp,*.h; every 'diagnostic ignored' is bracketed by a matching push/pop pair. (src/analyzers/container/ and src/analyzers/size/ are deliberately out of scope -- see this script's own head comment.)
```
Exit code: 0

## Issues Encountered

None.

## User Setup Required

None — no external service configuration required.

## Cross-Compiler Exposure (explicitly not claimed here)

Per this plan's own `<verification>` requirement: Clang, AppleClang and MSVC exposure for the pragmas preserved in `gop.cpp` and `stream_params.cpp` is **NOT** verified locally in this plan. Resolution is the three-OS matrix on the draft PR in 04-20 (Human Decision 4's CI-leg step), which will compile these two files under Clang (Linux CI leg's non-GCC configuration, if any), AppleClang, and MSVC for the first time.

## Next Phase Readiness

WR-03 is closed: the balance rule is both applied (four files cleaned, two scoped) and enforced (CI lint, proven able to fail). No known stubs, no new fixtures, no corpus digest changes. Ready for the next gap-closure plan in this phase's Human Decision 3 sequence, or for the 04-20 draft-PR push once all gap-closure plans in this wave complete.

## Self-Check: PASSED

All key files present on disk (`src/analyzers/video/{color,frame_types,gop,hdr,interlace,stream_params}.cpp`, `scripts/lint_pragma_scope.sh`, `.github/workflows/ci.yml`, this SUMMARY). Both task commits (`c353abe`, `8e56304`) confirmed present in `git log --oneline --all`.
