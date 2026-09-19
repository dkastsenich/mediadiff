---
phase: 05-timeline-analysis
plan: 22
subsystem: timeline
tags: [av-drift, drift-pattern, narrow-vocabulary, checkpoint-mapping, gap-closure, SC1, TIME-07, TIME-08]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: "05-21's recorded human decision at the blocking-human checkpoint (05-STEP-DESIGN.md `## Decision`): narrow-vocabulary, span:declared -- the two tokens this plan implements mechanically"
provides:
  - "DriftPattern narrowed to three spellings (constant-offset/linear-drift/irregular) -- step removed from the enum, and DriftFit::step_time_ms removed entirely"
  - "fit_drift's former plateau-detection second branch (largest-jump index, before/after means, flatness check) removed -- every trajectory that fails the first (residual < epsilon) branch now classifies irregular unconditionally, with its residual_max_ms unchanged"
  - "The now-fully-unused kDriftStepResidualMultiple constant removed from analyzers.h (Rule 1 cleanup, zero remaining consumers after fit_drift's step branch was removed)"
  - "Unit coverage (tests/unit/test_av_drift.cpp) pinning the narrow-vocabulary branch: the former step trajectory (100ms jump at the fourth of six checkpoints) now asserts irregular with the same hand-computed residual_max_ms=37; every step_time_ms reference removed"
  - "ROADMAP SC1's integration TEST_CASE (test_timeline_av_sync.cpp) renamed and rewritten to state the decided, measured outcome (irregular) instead of the fixture's original unrealized step-intended framing, with a REQUIRE_FALSE proving step_time_ms is absent from evidence"
  - "Proof (scratch git-worktree build of pre-plan commit 448361e) that removing step changed nothing else: 24 fixture pairs drawn from every timeline_* integration test's own compare_json call sites are byte-identical before/after this plan's changes, except for the absence of step_time_ms"
affects: [timeline-05-23, ROADMAP-SC1, TIME-07, TIME-08, DOC-04, docs/checks/timeline.av_drift.pattern.md, 05-CHECK-ROSTER.md]

# Actuals (#2632)
actuals:
  tokens: 5566
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Narrowing a published exact-string vocabulary by deleting the enumerator, its evidence field, and the classification branch that produced it, rather than leaving an unreachable dead code path -- proven equivalent to the prior shipped behavior (which never reached the removed value on any real fixture, per 05-10-SUMMARY.md's own Known Limitation) via a scratch git-worktree before/after diff over every fixture pair the integration test suite exercises."

key-files:
  created: []
  modified:
    - src/analyzers/timeline/analyzers.h
    - src/analyzers/timeline/av_sync.cpp
    - tests/unit/test_av_drift.cpp
    - tests/integration/test_timeline_av_sync.cpp

key-decisions:
  - "Implemented the branch 05-STEP-DESIGN.md's `## Decision` recorded verbatim: narrow-vocabulary (remove step from DriftPattern/DriftFit) and span:declared (no change -- already the shipped checkpoint span source). Task 1's first action read the Decision section mechanically, per the plan's own precondition."
  - "kDriftStepResidualMultiple removed from analyzers.h, beyond the plan's own explicit action list (Rule 1 cleanup): it parameterized only fit_drift's now-removed plateau-detection branch and had zero remaining consumers anywhere in the repository once that branch was gone. kDriftEpsilonMs was kept (still load-bearing for the first branch's own residual-vs-epsilon routing)."
  - "No fixture, recipe, CORPUS_DIGEST.txt, or provisional-ledger change -- A1's expectation (the spec needs no fixture change) held: the narrow-vocabulary branch only removes a classification path, it never adds one, so no new fixture behavior was needed."
  - "The residual MP4-to-TS timeline.av_drift finding stays open (not touched by this plan) -- span:declared means test_timeline_start_duration.cpp's Test 4 and its test_timeline_av_sync.cpp mirror are left exactly as they were; the follow-up is tracked for 05-23 to record in WINDOWS.md, per 05-21-SUMMARY.md's own corrected reason (AAC priming/padding samples with no edit list)."

patterns-established: []

requirements-completed: [TIME-08]  # TIME-07 and DOC-04 are also declared by sibling plan 05-23-PLAN.md (requirements.ready-ids shared-ID gate) -- they mark complete only once 05-23 (the documentation/vocabulary-amendment half of this gap closure) also finishes, so they stay open here even though this plan's own code work is done.

coverage:
  - id: D1
    description: "DriftPattern loses its step enumerator and DriftFit loses step_time_ms; fit_drift's plateau-detection branch is removed -- a trajectory that used to classify step now classifies irregular with its residual max unchanged, proven on the SC1 step-intended pair against the real binary"
    requirement: "TIME-07"
    verification:
      - kind: integration
        ref: "mediadiff compare --profile sw-encoder --json tests/fixtures/timeline_start_base.mp4 tests/fixtures/timeline_drift_step.mp4 -- candidate=irregular, residual_max_ms=60, no step_time_ms in evidence"
        status: pass
    human_judgment: false
  - id: D2
    description: "Unit coverage (11 av_drift TEST_CASEs) pins the narrow-vocabulary branch: no input can produce a step classification, asserted over every existing hand-built trajectory including the former step case"
    requirement: "TIME-07"
    verification:
      - kind: unit
        ref: "ctest -R '^unit\\.(av_drift|av_sync) - ' (35/35 pass)"
        status: pass
    human_judgment: false
  - id: D3
    description: "ROADMAP SC1's integration TEST_CASE states the decided outcome (irregular) for the constant-offset, linear-drift and spliced-trim fixtures, whole-report, and no test in the file calls the third fixture step-intended anymore"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "ctest -R '^integration\\.timeline_av_sync - ROADMAP SC1' (1/1 pass); grep -c 'step-intended' tests/integration/test_timeline_av_sync.cpp == 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "Removing step from what fit_drift can produce changed nothing else: 24 fixture pairs drawn from every compare_json call site across the timeline_* integration tests are byte-identical (--json output) before and after this plan's changes, except for the absence of step_time_ms, verified against a scratch git-worktree build of the pre-plan commit (448361e)"
    verification:
      - kind: other
        ref: "scratch git-worktree build of 448361e (symlinked vcpkg submodule, reused local vcpkg binary cache), diffed --json output over 24 pairs with step_time_ms stripped before comparison -- 24/24 OK"
        status: pass
    human_judgment: false
  - id: D5
    description: "Full suite and the designated-leg byte-exact goldens both green; no golden touched; local corpus digest still byte-identical to the designated leg (no fixture regenerated)"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure (994/994, 6 pre-existing skips); MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure (994/994, unit.console_vt the only skip, all five byte-exact goldens ran and matched)"
        status: pass
      - kind: other
        ref: "git diff --stat -- tests/golden/ (empty); bash scripts/assert_corpus_digest.sh (local fixtures still byte-identical to the designated leg's, no fixture regenerated by this plan)"
        status: pass
    human_judgment: false

# Metrics
duration: ~35min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 22: Narrow-Vocabulary Closes SC1 (Gap 1) Summary

**`step` is removed from `timeline.av_drift.pattern`'s vocabulary and `DriftFit::step_time_ms` no longer exists -- fit_drift's former plateau-detection branch is deleted entirely, every trajectory that used to classify `step` now classifies `irregular` with its residual max reported exactly as before, proven byte-identical to the pre-plan binary on 24 fixture pairs except for the absence of `step_time_ms`.**

## Performance

- **Duration:** ~35 min
- **Started:** 2026-09-18T19:34:02Z (first task commit)
- **Completed:** 2026-09-18T19:41:43Z (SUMMARY authored)
- **Tasks:** 3/3 completed
- **Files modified:** 4

## Accomplishments

- Implemented the human decision recorded in `05-STEP-DESIGN.md`'s `## Decision` section verbatim: `narrow-vocabulary` (step removed) and `span:declared` (unchanged, already the shipped behavior).
- `DriftPattern` narrowed from four to three spellings (`constant_offset`, `linear_drift`, `irregular`); `DriftFit::step_time_ms` removed.
- `fit_drift`'s former plateau-detection second branch (largest raw-offset jump index, before/after means, flatness-against-mean check) deleted -- everything that fails the first branch (`residual_max_ms < kDriftEpsilonMs`) now classifies `irregular` unconditionally, with `residual_max_ms` still computed and reported exactly as before by the first branch's own loop.
- `drift_pattern_to_string` loses its `"step"` case; the `step_time_ms` evidence key is no longer emitted anywhere.
- `kDriftStepResidualMultiple` removed from `analyzers.h` (Rule 1 cleanup) -- it parameterized only the deleted branch and had zero remaining consumers.
- `tests/unit/test_av_drift.cpp` rewritten: the former step case (Test 3) is renamed and its assertion rewritten to state `irregular`, with the same hand-computed `residual_max_ms=37`; every `step_time_ms` reference across Tests 1, 4, 5, 7 and 11 removed. 11/11 `av_drift` tests pass; all 35 `av_drift`/`av_sync` unit tests pass together.
- `tests/integration/test_timeline_av_sync.cpp`'s ROADMAP SC1 `TEST_CASE` renamed and its third sub-block rewritten to state the decided outcome (`irregular`) for the "spliced-trim" fixture instead of the original "step-intended" framing, with a new `REQUIRE_FALSE` proving `step_time_ms` is absent from evidence.
- Proved the narrow change altered nothing else: built the pre-plan commit (`448361e`) in a scratch git worktree (vcpkg submodule symlinked, local vcpkg binary cache reused -- configure completed in ~4s, no FFmpeg rebuild needed) and diffed `--json compare` output, with `step_time_ms` stripped before comparison, over 24 fixture pairs drawn from every `compare_json` call site across `test_timeline_start_duration.cpp`, `test_timeline_av_sync.cpp`, `test_timeline_structure.cpp` and `test_timeline_timecode.cpp`. All 24 pairs matched exactly.
- Full suite (994/994, 6 pre-existing skips) and `MEDIADIFF_DESIGNATED_LEG=1` (994/994, only `unit.console_vt` skipped, all five byte-exact goldens ran and matched) both green. No golden touched. No fixture, recipe, `CORPUS_DIGEST.txt` or provisional-ledger change (A1's expectation held).

## SC1 Step-Intended Pair, Measured

`timeline_start_base.mp4` vs `timeline_drift_step.mp4`, `--profile sw-encoder --json`:

| Field | Before this plan (already, per 05-10-SUMMARY.md) | After this plan |
|---|---|---|
| `candidate` (pattern) | `irregular` | `irregular` |
| `residual_max_ms` | 60 | 60 |
| `step_time_ms` in evidence | absent (pattern never reached `step`) | absent (key no longer exists at all) |

The classification and residual were already `irregular`/60ms before this plan (05-10-SUMMARY.md's own Known Limitation) -- this plan removes the *unreachable* `step` code path and vocabulary entry, it does not change any measured value.

## Three SC1 Pairs Re-Measured (Task 3)

| Pair | Profile | Non-pass findings |
|---|---|---|
| `timeline_start_base.mp4` vs `timeline_avoffset_video_shift.mp4` | sw-encoder | `container.mp4.edit_list` (video, warn), `timeline.start` (video, fail), `timeline.av_offset` (audio, fail) |
| `timeline_drift_base.mp4` vs `timeline_drift_linear.mp4` | sw-encoder | `container.mp4.edit_list` (audio, warn), `timeline.av_drift` (audio, fail), `timeline.av_drift.pattern` (audio, fail) |
| `timeline_start_base.mp4` vs `timeline_drift_step.mp4` | sw-encoder | `timeline.vfr_profile` (audio, warn), `timeline.av_drift.pattern` (audio, fail) |

All three match the sets already committed in the SC1 `TEST_CASE` -- narrow-vocabulary changes the pattern *type*, never which findings fire.

## Task Commits

Each task was committed atomically:

1. **Task 1: The decided branch is wired end to end through av_sync.cpp and proven on the SC1 step-intended pair** - `4556cef` (feat)
2. **Task 2: Unit coverage for the decided branch, including TIME-07's boundary vectors** - `a8953a5` (test)
3. **Task 3: The SC1 integration case asserts the decided outcome whole-report, and span-affected declared sets are re-measured** - `608f3de` (test)

**Plan metadata:** this commit (docs: complete 05-22 plan)

## Files Created/Modified

- `src/analyzers/timeline/analyzers.h` - `DriftPattern` narrowed (step removed), `DriftFit::step_time_ms` removed, `kDriftStepResidualMultiple` removed, doc comments updated to explain the narrow-vocabulary decision
- `src/analyzers/timeline/av_sync.cpp` - `drift_pattern_to_string` loses its `"step"` case; `fit_drift`'s plateau-detection second branch replaced with an unconditional `irregular` assignment; the `step_time_ms` evidence-emission block removed from `run_timeline_av_sync`
- `tests/unit/test_av_drift.cpp` - Test 3 renamed/rewritten to assert `irregular`; every `step_time_ms` reference removed; the `kDriftStepResidualMultiple` static_assert and using-declaration removed
- `tests/integration/test_timeline_av_sync.cpp` - ROADMAP SC1 `TEST_CASE` renamed and its third sub-block rewritten to assert `irregular` with a `step_time_ms`-absence check

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- Implemented exactly the branch 05-STEP-DESIGN.md's `## Decision` recorded: narrow-vocabulary + span:declared.
- Removed `kDriftStepResidualMultiple` beyond the plan's own explicit action list, as a Rule 1 cleanup of a constant left with zero consumers.
- Left the residual MP4-to-TS `timeline.av_drift` finding open (span:declared branch instruction; tracked for 05-23).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Cleanup] Removed the now-fully-unused `kDriftStepResidualMultiple` constant**
- **Found during:** Task 2 (rewriting unit coverage)
- **Issue:** After Task 1 removed `fit_drift`'s plateau-detection branch (the constant's only consumer), `kDriftStepResidualMultiple` had zero remaining references anywhere in the repository except its own definition and a `static_assert` pin in the test file -- a fully vestigial named constant.
- **Fix:** Removed the constant's definition from `analyzers.h` and its `static_assert`/using-declaration from `tests/unit/test_av_drift.cpp`. `kDriftEpsilonMs`'s own pin was kept (still load-bearing).
- **Files modified:** `src/analyzers/timeline/analyzers.h`, `tests/unit/test_av_drift.cpp`
- **Verification:** `grep -rn kDriftStepResidualMultiple src/ tests/` finds no remaining reference; build and full test suite green.
- **Committed in:** `a8953a5` (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 Rule 1 cleanup).
**Impact on plan:** Removes dead code the plan's own step-branch removal left behind; no behavior change, no scope creep.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Gap 1 (SC1's step-vocabulary question) is closed at the code/test level. `05-23-PLAN.md` carries the remaining documentation half: amending `docs/checks/timeline.av_drift.pattern.md`, `05-CHECK-ROSTER.md`'s pattern-vocabulary row, `REQUIREMENTS.md`'s `TIME-07`/`DOC-04` text (which still literally names `step` in the roster/requirement text this plan deliberately left untouched, per A2), and recording the waived residual MP4-to-TS `timeline.av_drift` finding in `WINDOWS.md` with 05-21's corrected reason.
- `TIME-08` is marked complete by this plan (declared only here, no sibling plan). `TIME-07` and `DOC-04` are also declared by `05-23-PLAN.md` -- the shared-ID gate (`requirements.ready-ids`) leaves them open until 05-23 also finishes, even though this plan's own code/test work toward them is done.
- No blockers for `05-23`. This plan touched only `src/analyzers/timeline/analyzers.h`, `src/analyzers/timeline/av_sync.cpp`, `tests/unit/test_av_drift.cpp`, and `tests/integration/test_timeline_av_sync.cpp` -- `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`, `tests/fixtures/GENERATOR_MANIFEST.json`, `docs/checks/*.md`, `05-CHECK-ROSTER.md`, and `test_timeline_start_duration.cpp`'s declared sets are all untouched, exactly as scoped (A1/A2, span:declared).

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

- `src/analyzers/timeline/analyzers.h` - FOUND
- `src/analyzers/timeline/av_sync.cpp` - FOUND
- `tests/unit/test_av_drift.cpp` - FOUND
- `tests/integration/test_timeline_av_sync.cpp` - FOUND
- Commit `4556cef` - FOUND (git log)
- Commit `a8953a5` - FOUND (git log)
- Commit `608f3de` - FOUND (git log)
- Full `ctest` suite: 994/994 passed, 0 failed (6 pre-existing skips, unrelated to this plan)
- `MEDIADIFF_DESIGNATED_LEG=1 ctest`: 994/994 passed, 0 failed (unit.console_vt the only skip)
- `git diff --stat -- tests/golden/`: empty
