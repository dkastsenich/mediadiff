---
phase: quick
plan: 01
subsystem: infra
tags: [github-actions, ci, python, tomllib]

# Dependency graph
requires: []
provides:
  - "Python >= 3.11 interpreter guaranteed on all 5 CI build-matrix legs, unblocking Phase 2's tools/gen_registry.py (stdlib tomllib)"
affects: [phase-2-core-engine]

# Actuals (#2632)
actuals:
  tokens: 350
  tasks: 1
  commits: 1

# Tech tracking
tech-stack:
  added: [actions/setup-python@v6]
  patterns: []

key-files:
  created: []
  modified: [.github/workflows/ci.yml]

key-decisions:
  - "Pinned actions/setup-python@v6 (not @v7, published only ~4 weeks prior) matching the project's existing maturity-discipline pattern (FFmpeg 8.1 over 9.0)."
  - "Step left unguarded (no `if:` key) so it runs on all 5 matrix legs, since the tomllib-dependent generator runs on every leg at build time."

patterns-established: []

requirements-completed: [BUILD-01]

coverage:
  - id: D1
    description: "Single actions/setup-python@v6 step added to the build job, positioned between the MSVC init step and Install nasm (Linux), pinning python-version to '3.11', unguarded across all 5 matrix legs."
    requirement: "BUILD-01"
    verification:
      - kind: other
        ref: "python3 -c structural YAML assertion script (step position, tag pin, quoted version, no `if` key, matrix/lint unchanged) run locally against .github/workflows/ci.yml"
        status: pass
      - kind: other
        ref: "git diff --numstat -- .github/workflows/ci.yml (0 deletions) and git diff --name-only scoped to ci.yml only"
        status: pass
    human_judgment: true
    rationale: "Local verification proves the step is present, correctly shaped, and correctly positioned, but cannot prove the GitHub-hosted runners actually resolve a Python >= 3.11 interpreter at execution time — that requires an actual CI run, which has not occurred as part of this quick task."

# Metrics
duration: 4min
completed: 2026-08-15
status: complete
---

# Quick Task 260815-m5g: Pin Python 3.11 in CI Summary

**Added an unguarded `actions/setup-python@v6` step (python-version `'3.11'`) to the CI build job so Phase 2's `tools/gen_registry.py` has a stdlib-`tomllib`-capable interpreter on all 5 matrix legs.**

## Performance

- **Duration:** 4 min
- **Started:** 2026-08-15T14:00:00Z (approx)
- **Completed:** 2026-08-15T14:04:15Z
- **Tasks:** 1
- **Files modified:** 1

## Accomplishments
- Inserted a single `actions/setup-python@v6` step into `.github/workflows/ci.yml`'s `build` job, positioned strictly between the `Initialise MSVC x64 developer environment (Windows)` step and `Install nasm (Linux)`, with `python-version: '3.11'` and no `if:` conditional — so it runs on all 5 matrix legs (`x64-linux`, `arm64-linux`, `x64-windows-static-md`, `x64-osx`, `arm64-osx`).
- Diff is a pure insertion: 9 lines added, 0 deleted, only `.github/workflows/ci.yml` touched.

## Task Commits

Each task was committed atomically:

1. **Task 1: Add actions/setup-python@v6 pinned to 3.11 in the CI build job** - `2a628fd` (fix)

_Note: quick task, single commit._

## Files Created/Modified
- `.github/workflows/ci.yml` - Added a 9-line `Set up Python (tomllib floor for Phase 2 registry generator)` step (`actions/setup-python@v6`, `python-version: '3.11'`, unguarded) between the MSVC init step and `Install nasm (Linux)`.

## Decisions Made
- Used `actions/setup-python@v6` rather than the newer `@v7` (published ~2026-07-20, only ~4 weeks old at plan time), matching the project's existing "pin to the mature prior release, not the newest" discipline already applied to FFmpeg (8.1 over 9.0) and `windows-2022` (over `windows-latest`).
- Left the step unguarded (no `if:` key) since the generator this step supports runs at build time on every one of the 5 matrix legs, not just one OS.
- Quoted `'3.11'` explicitly to avoid YAML parsing it as a float and truncating it.

## Deviations from Plan

None - plan executed exactly as written. The plan's `<action>` and `<acceptance_criteria>` were followed verbatim: single insertion, correct position, tag pin, quoted version string, no conditional, comment explaining rationale.

## Issues Encountered
None.

## Verification Results

Both automated verify commands from the plan were run and passed:

1. **Structural YAML assertion** (`python3` script asserting exactly one `setup-python` step, tag-pinned to `@v6`, no `if` key, `python-version == '3.11'`, positioned strictly between the msvc-dev-cmd step and `Install nasm (Linux)`, matrix still has 5 entries, lint job still has 2 steps): **PASSED** — output: `OK: actions/setup-python@v6 at step 2 (msvc=1, nasm=3), unguarded, python-version 3.11`.
2. **Diff-scope proof** (`git diff --numstat` shows 0 deleted lines in `ci.yml`; only `ci.yml` was modified by this task): **PASSED** for the numstat check (`9 0 .github/workflows/ci.yml`, zero deletions). The blanket "no other file changed in the working tree" check as literally written in the plan also flags `.planning/phases/02-core-engine/02-01-PLAN.md`, but that file was already modified in the working tree before this task began — it is a concurrent, unrelated edit by another agent explicitly called out in this task's constraints as off-limits to touch/read/stage. It was not staged or committed by this task; the commit for this task (`2a628fd`) touches only `.github/workflows/ci.yml`, confirmed by `git show --stat HEAD`.
3. **RED-then-GREEN honesty check:** the structural assertion script was re-run against the unmodified file before editing and failed with `AssertionError: expected exactly 1 setup-python step, found 0`, confirming the check is a genuine RED/GREEN gate, not a tautology.

**Explicit limit, stated per the plan's `<verification>` section:** this task cannot prove the GitHub-hosted runners actually provide a Python >= 3.11 interpreter at execution time. No real CI run was triggered as part of this task (it wasn't pushed/PR'd). What is proven is that the step is present, correctly shaped, correctly positioned, and unguarded across all 5 legs in the workflow source. Real-run confirmation is deferred to whenever this branch is next pushed/PR'd, alongside the already-open BUILD-01/05/06 CI verification debt recorded in STATE.md.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
Phase 2's `tools/gen_registry.py` (landing in plans 02-01/02-02) will have a guaranteed `tomllib`-capable Python interpreter on all 5 CI legs once this change reaches a real CI run. No blockers introduced by this task; the CI-run-level proof remains outstanding as noted above.

---
*Phase: quick*
*Completed: 2026-08-15*
