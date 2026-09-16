---
phase: 05-timeline-analysis
plan: 01
subsystem: analysis-checks
tags: [timeline, ffmpeg, rational-time, doc04, tracer, check-registry, catch2]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "the check-id roster / D-CHECK-ROSTER.md pattern, checks.def per-profile override sub-tables, gop.cpp's file-local push_skip/scope_kind_for_stream/compute_stream_scopes convention, and container.ts.pcr_interval/psi_interval's checked-mul/checked-div ms conversion this plan reuses verbatim"
provides:
  - "The Phase-5 16-id `timeline.*` check-id roster (05-CHECK-ROSTER.md), approved before any id was written to checks.def"
  - "`timeline.start`: a registered check emitting ONE global-scope origin measurement plus one per-stream relative measurement (D-03), proven to collapse a whole-file MP4-to-MPEG-TS mux-delay shift into a single finding"
  - "The shared DOC-04 'no-others' harness (tests/integration/timeline_findings.h: count_non_pass/diff_declared_set/expect_declared_set), moved out of test_video_yuvj.cpp so every timeline test reads the same occurrence-counted non-pass assertion"
  - "Three new corpus fixtures (timeline_start_base.mp4, timeline_start_base_copy.mp4, timeline_start_shift.ts) plus their provisional digest entries"
affects: [05-timeline-analysis (plans 05-04 through 05-11, which append sibling timeline.* checks to this plan's analyzers.h/orchestrator.cpp registration point and reuse timeline_findings.h)]

# Actuals (#2632)
actuals:
  tokens: 18900
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "Global-origin-plus-per-stream-relative scoping (D-03): one Scope::Kind::global measurement plus one per-stream measurement holding (stream's own value MINUS the global origin), so a whole-file shift reports as ONE finding instead of one per stream"
    - "ms-unit RationalValue construction via checked_mul-then-checked_div (truncating), matching container.ts.pcr_interval/psi_interval exactly -- an unreduced exact fraction was tried and rejected after it overflowed the compare engine's cross-multiplication on a real fixture"
    - "Occurrence-counted (not set-deduplicated) DOC-04 declared-set assertion: the SAME check id firing at two different scopes for one cause counts as two declared entries, not one"

key-files:
  created:
    - src/analyzers/timeline/analyzers.h
    - src/analyzers/timeline/start_duration.cpp
    - docs/checks/timeline.start.md
    - tests/integration/timeline_findings.h
    - tests/integration/test_timeline_start_duration.cpp
    - tests/unit/test_timeline_start_duration.cpp
    - .planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md
  modified:
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_video_yuvj.cpp
    - tests/integration/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "ms values use checked_mul-then-checked_div (truncating), not an unreduced exact fraction -- the unreduced form overflowed compare/tol.cpp's cross-multiplication on a real MP4 fixture, discovered empirically by running the built binary, not by inference"
  - "Mechanism-citation evidence (edit-list / codec-delay) reads the generated CheckId enum via kCheckIdStrings[...], never a hand-typed dotted string literal, to satisfy scripts/lint_check_id_strings.sh"
  - "diff_declared_set/expect_declared_set compare per-id OCCURRENCE COUNTS, not set membership -- meta.tags legitimately fires twice (global + video scope) for the single MP4-to-TS remux cause, which a set-based comparison cannot represent"
  - "Added tests/unit/test_timeline_start_duration.cpp as a Rule 2 deviation: Task 2's tdd=\"true\"/<behavior> block names scenarios (AV_NOPTS_VALUE skip, tie-never-replaces-champion determinism, checked-arithmetic overflow) not reliably reachable through a CLI-level fixture alone"

patterns-established:
  - "src/analyzers/timeline/analyzers.h is the registration point later 05-04..05-11 plans append sibling timeline.* analyzers to"
  - "tests/integration/timeline_findings.h is the one shared DOC-04 harness every later timeline integration test reuses"

requirements-completed: [TIME-01, TIME-03, DOC-04]

coverage:
  - id: D1
    description: "The Phase-5 16-id timeline.* check-id roster is approved and committed before any id is written to checks.def"
    verification:
      - kind: other
        ref: ".planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md (commit 1a1d141)"
        status: pass
    human_judgment: true
    rationale: "Roster approval is a design/documentation artifact selected via an auto-mode checkpoint:decision (auto-selected first proposed option, gate=blocking); not independently testable business logic."
  - id: D2
    description: "timeline.start emits one global-scope origin measurement and one per-stream relative measurement; a whole-file MP4-to-MPEG-TS shift produces exactly ONE non-pass finding at global scope, with every per-stream finding staying pass"
    requirement: "TIME-01"
    verification:
      - kind: integration
        ref: "integration.timeline_start_duration - the MP4-to-TS tracer pair declares its complete expected finding set under --profile remux, and count_non_pass equals that set's size exactly"
        status: pass
      - kind: integration
        ref: "integration.timeline_start_duration - the tracer pair's timeline.start finding fires at GLOBAL scope only, and every per-stream timeline.start finding stays pass"
        status: pass
      - kind: unit
        ref: "unit.timeline_start_duration - the full global-origin-then-per-stream-relative pipeline (Test 1/Test 2/Test 3)"
        status: pass
    human_judgment: false
  - id: D3
    description: "timeline.start values are exact RationalValue millisecond counts via checked integer arithmetic, never a float; overflow refuses a wrapped/fabricated value; AV_NOPTS_VALUE is never coerced to 0"
    requirement: "TIME-03"
    verification:
      - kind: unit
        ref: "unit.timeline_start_duration - first_presented_pts returns std::nullopt when every packet carries the AV_NOPTS_VALUE sentinel"
        status: pass
      - kind: unit
        ref: "unit.timeline_start_duration - global_origin_ticks returns std::nullopt (never a wrapped or fabricated origin) when the cross-multiplication comparison overflows int64_t"
        status: pass
      - kind: unit
        ref: "unit.timeline_start_duration - ticks_to_ms returns std::nullopt (never a wrapped value) when the checked multiplication overflows int64_t"
        status: pass
    human_judgment: false
  - id: D4
    description: "The shared DOC-04 no-others harness (count_non_pass/diff_declared_set/expect_declared_set) replaces test_video_yuvj.cpp's local copy, with one definition repository-wide"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "integration.video_yuvj - yuvj420p vs yuv420p-limited-range produces EXACTLY ONE non-pass finding across the WHOLE report, and it is video.color.range"
        status: pass
      - kind: integration
        ref: "integration.timeline_start_duration - count_non_pass counts every non-pass, non-skipped finding across every group including info, and is zero for an all-pass report"
        status: pass
      - kind: other
        ref: "grep -rc 'std::size_t count_non_pass' tests/integration/ reports exactly one match (timeline_findings.h)"
        status: pass
    human_judgment: false
  - id: D5
    description: "DOC-03 coverage: timeline.start has a declared triggering fixture pair and a declared clean fixture pair, verified against the live registry"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "integration.doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
    human_judgment: false

duration: ~30min (post-roster-approval commit span; substantial additional implementation/debugging work was executed in the same session before a context-window compaction boundary and is not separately timestamped)
completed: 2026-09-16
status: complete
---

# Phase 5 Plan 01: Timeline Analysis Tracer (`timeline.start`) Summary

**`timeline.start` registered end-to-end with global-origin-plus-per-stream-relative scoping (D-03), proven to collapse a whole-file MP4-to-MPEG-TS mux-delay shift into ONE finding instead of one per stream, plus a shared DOC-04 no-others harness reused across timeline integration tests.**

## Performance

- **Duration:** ~30 min (commit span from roster approval to final unit-test commit; see `duration` note above)
- **Completed:** 2026-09-16
- **Tasks:** 3/3
- **Files modified:** 18 (7 created, 11 modified)

## Accomplishments

- Approved the Phase-5 16-id `timeline.*` check-id roster (`05-CHECK-ROSTER.md`) before any id was registered, per D-04/D-10's one-way rating.
- Implemented `timeline.start` end to end: one `Scope::Kind::global` measurement (the file's earliest presentation PTS across every timestamped stream) plus one per-stream measurement (that stream's own first PTS minus the global origin) — verified empirically against the real binary that an MP4-to-TS remux (the TS muxer's own ~1.4s default mux delay) produces exactly ONE non-pass finding at global scope, with every per-stream finding staying `pass`.
- Built the shared DOC-04 "no-others" harness (`tests/integration/timeline_findings.h`): `count_non_pass` moved verbatim from `test_video_yuvj.cpp`, plus new `diff_declared_set`/`expect_declared_set` helpers using occurrence-counted (not set-deduplicated) comparison, since the same check id can legitimately fire at more than one scope for a single cause.
- Registered three new corpus fixtures and their provisional digest entries; added `timeline.start` to `test_doc03_coverage.cpp`'s declared-pairs table (running total now sixty-two).
- Pinned the analyzer's four pure `detail::` helpers (`first_presented_pts`, `global_origin_ticks`, `ticks_to_ms`, `subtract_ms`) as fixture-free unit tests, covering the AV_NOPTS_VALUE skip, the stable-tie-never-replaces-the-champion determinism, and checked-arithmetic overflow refusing a wrapped value — scenarios named in Task 2's own `<behavior>` block that a CLI-level fixture cannot reliably reach.

## Task Commits

Each task was committed atomically:

1. **Task 1: Approve the Phase-5 `timeline.*` check-id roster** - `1a1d141` (docs)
2. **Task 2: `timeline.start` end to end** - `fafcdc0` (feat)
3. **Task 3: the DOC-04 no-others harness** - `fbe0072` (test)

Additional commit (Rule 2 deviation, see below):
- `84168f4` (test) - retroactive unit-level coverage for Task 2's pure `detail::` helpers

**Plan metadata:** commit to follow this SUMMARY (docs: complete plan)

## Files Created/Modified

- `src/analyzers/timeline/analyzers.h` - `timeline_start_duration_analyzer()` declaration plus the exposed `detail::` pure-function surface
- `src/analyzers/timeline/start_duration.cpp` - the analyzer implementation: D-03's global-origin-plus-relative scoping, skip-reason priority (`partial_scan` > `no_timing_data` > `insufficient_data`), mechanism-citation evidence
- `src/probe/orchestrator.cpp` - registers `timeline_start_duration_analyzer()` in `all_analyzers()`
- `src/core/checks.def` - registers `timeline.start` (tol/ms, 5ms warn / 20ms fail, 1ms under strict_bitexact/remux)
- `docs/checks/timeline.start.md` - full check documentation (What it measures / Why it matters / Accept-Tune-Silence)
- `CMakeLists.txt` - adds the new source/header to `libmediadiff`
- `scripts/gen_corpus.sh` - three new fixture recipes (`timeline_start_base.mp4`, `_base_copy.mp4`, `_shift.ts`)
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` - new fixture hashes, provisional pending designated-leg transcription
- `tests/golden/list_checks_effective.txt` - refreshed via `UPDATE_GOLDENS=1` for the new registry row
- `tests/integration/test_doc03_coverage.cpp` - declares `timeline.start`'s triggering/clean fixture pair
- `tests/integration/timeline_findings.h` - the shared DOC-04 harness (`count_non_pass`/`diff_declared_set`/`expect_declared_set`)
- `tests/integration/test_video_yuvj.cpp` - refactored to consume the shared harness instead of a local `count_non_pass`
- `tests/integration/test_timeline_start_duration.cpp` - 6 integration `TEST_CASE`s proving the harness and the tracer pair together
- `tests/integration/CMakeLists.txt` - registers the new integration test file
- `tests/unit/test_timeline_start_duration.cpp` - 15 unit `TEST_CASE`s pinning the pure `detail::` helpers
- `tests/unit/CMakeLists.txt` - registers the new unit test file

## Decisions Made

- **ms conversion via checked_mul/checked_div, never an unreduced exact fraction.** The unreduced form (`{num = ticks*1000*tb.num, den = tb.den}`) was implemented first, matching an initial reading of "never divide" in the check's own doc draft, but overflowed `compare/tol.cpp`'s cross-multiplication on the real `timeline_start_base.mp4` fixture (observed `status: error` instead of `pass`) — discovered by running the built binary and parsing its `--json` output, not by inference. Fixed by switching to the established project convention (matching `container.ts.pcr_interval`/`psi_interval`'s `bytes_to_ms`), and corrected the now-inaccurate doc-comment claims in both `analyzers.h` and `docs/checks/timeline.start.md`.
- **Mechanism citation via the generated `CheckId` enum, never a hand-typed string.** Evidence citing `container.mp4.edit_list`/`container.mkv.codec_delay` as the shift's mechanism reads `kCheckIdStrings[static_cast<std::size_t>(CheckId::...)]`, not a dotted string literal, satisfying `scripts/lint_check_id_strings.sh`.
- **Occurrence-counted DOC-04 declared-set comparison.** The tracer pair's own declared set surfaced a real design gap: `meta.tags` fires twice (once at `global`, once at `video` scope) for the single remux cause. A `std::set`-based declared-id comparison cannot represent "this id fires twice" — redesigned `diff_declared_set`/`expect_declared_set` to compare per-id occurrence counts via `std::map<std::string, std::size_t>`, with the caller declaring an id N times to mean "expect N occurrences."

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed ms-conversion overflow in `ticks_to_ms`/`subtract_ms`**
- **Found during:** Task 2, empirical verification against the real binary
- **Issue:** An unreduced exact-fraction ms representation overflowed the compare engine's cross-multiplication on a real fixture, producing `status: error` instead of `pass` for every per-stream `timeline.start` finding
- **Fix:** Switched to `checked_mul`-then-`checked_div` (truncating), matching `container.ts.pcr_interval`/`psi_interval`'s established convention; corrected the doc-comment claims in `analyzers.h` and `docs/checks/timeline.start.md` accordingly
- **Files modified:** `src/analyzers/timeline/analyzers.h`, `src/analyzers/timeline/start_duration.cpp`, `docs/checks/timeline.start.md`
- **Verification:** `./build/x64-linux/mediadiff compare timeline_start_base.mp4 timeline_start_shift.ts --profile remux --json` shows every per-stream `timeline.start` finding at `pass`
- **Committed in:** `fafcdc0` (Task 2 commit)

**2. [Rule 3 - Blocking] Replaced hand-typed check-id string literals with generated enum lookups**
- **Found during:** Task 2, running `scripts/lint_check_id_strings.sh`
- **Issue:** Mechanism-citation evidence code used hand-typed dotted string literals (`"container.mp4.edit_list"`, `"container.mkv.codec_delay"`), violating D-03's "refer to checks through the generated CheckId enum" rule
- **Fix:** Replaced with `kCheckIdStrings[static_cast<std::size_t>(CheckId::container_mp4_edit_list)]` and the mkv equivalent
- **Files modified:** `src/analyzers/timeline/start_duration.cpp`
- **Verification:** `bash scripts/lint_check_id_strings.sh` exits 0
- **Committed in:** `fafcdc0` (Task 2 commit)

**3. [Rule 1 - Bug] Redesigned the DOC-04 declared-set comparison from set-membership to occurrence-counted**
- **Found during:** Task 3, running the new integration test against the real tracer pair (`count_non_pass(report) == 10`, declared set size `== 9`)
- **Issue:** The original `diff_declared_set`/`expect_declared_set` design deduplicated declared ids into a `std::set`, which cannot represent `meta.tags` legitimately firing twice (global + video scope) for one remux cause
- **Fix:** Rewrote both functions to compare per-id occurrence counts via `std::map<std::string, std::size_t>`; a caller now declares an id N times to assert N occurrences
- **Files modified:** `tests/integration/timeline_findings.h`, `tests/integration/test_timeline_start_duration.cpp`
- **Verification:** all 6 `integration.timeline_start_duration` test cases pass; full `ctest` suite green (792/792)
- **Committed in:** `fbe0072` (Task 3 commit)

**4. [Rule 2 - Missing Critical] Added unit-level coverage for `timeline.start`'s pure `detail::` helpers**
- **Found during:** post-Task-2 review against the task's own `tdd="true"`/`<behavior>` block
- **Issue:** Several declared behaviors (the AV_NOPTS_VALUE skip, the stable-tie-never-replaces-the-champion determinism, checked-arithmetic overflow refusing a wrapped/fabricated value) had been verified empirically against the real binary before commit but were not pinned as a permanent, fixture-free regression test — a gap against the task's own TDD framing
- **Fix:** Added `tests/unit/test_timeline_start_duration.cpp` (15 `TEST_CASE`s) driving `first_presented_pts`/`global_origin_ticks`/`ticks_to_ms`/`subtract_ms` directly against hand-built input, every expected value hand-computed before running (fail-first discipline); all 15 passed on first execution
- **Files modified:** `tests/unit/test_timeline_start_duration.cpp`, `tests/unit/CMakeLists.txt`
- **Verification:** `ctest --preset x64-linux -R "unit\.timeline_start_duration"` — 15/15 pass
- **Committed in:** `84168f4`

---

**Total deviations:** 4 auto-fixed (2 Rule 1 bug fixes, 1 Rule 3 blocking fix, 1 Rule 2 missing-critical addition)
**Impact on plan:** All four were necessary for correctness (overflow-free comparison, a design-complete DOC-04 assertion) or lint/TDD compliance. No scope creep beyond the plan's own three tasks.

## TDD Gate Compliance

Task 2 carries `tdd="true"`. The strict RED-then-GREEN commit ordering (a failing `test(...)` commit before the `feat(...)` commit) was **not** followed literally: the implementation commit (`fafcdc0`, `feat`) landed before the dedicated unit-test commit (`84168f4`, `test`). The underlying behaviors were verified empirically against the real binary (CLI `--json` output, hand-traced arithmetic) before each commit, and the Task 3 integration tests (`fbe0072`) plus the retroactive unit tests (`84168f4`) now pin every declared `<behavior>` scenario as a permanent regression test — but the literal commit-order gate was not observed. Documented here per this project's own gate-sequence-validation discipline.

## Issues Encountered

- **`inspect --json -v` never renders a `Measurement::evidence` field.** Investigated as a possible bug in the new analyzer; confirmed via a side-by-side comparison against an unrelated, pre-existing check (`video.gop.length`) that this is project-wide `inspect` renderer behavior, not something this plan introduced. No fix applied; not covered by any acceptance criterion.
- **The generic `"ms"` convenience field in `--json` output looks confusing for a value whose `num` is already in ms** (e.g. `-23000.0` for `num=-23`). Confirmed via `container.ts.pcr_interval`'s existing output on an unrelated fixture that this is a pre-existing, generic renderer quirk (multiplies `num/den` by 1000 unconditionally), not a defect in this plan's code.
- **`video.profile`/`video.level`/`video.resolution` differ between the MP4 and MPEG-TS fixtures for the identical mpeg4 elementary stream.** Confirmed via direct `--json` comparison that MPEG-TS's demuxer does not populate `codecpar` profile/level/width/height for mpeg4-video the same way MP4's `stsd` atom does — a genuine, pre-existing Phase-4 check behavior, included in the DOC-04 declared set with a causal-reason comment rather than treated as a defect.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `src/analyzers/timeline/analyzers.h` is the registration point plans 05-04 through 05-11 append sibling `timeline.*` analyzers to.
- `tests/integration/timeline_findings.h` is ready for reuse by every later timeline integration test needing the DOC-04 no-others assertion.
- The 16-id roster in `05-CHECK-ROSTER.md` is approved and available for the remaining `timeline.*` checks this phase registers.
- No blockers. The three new fixture hashes remain PROVISIONAL in `CORPUS_DIGEST_PROVISIONAL.txt` pending the designated-leg CI transcription (a later plan's responsibility, per the existing convention).

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-16*

## Self-Check: PASSED

All 8 created files verified present on disk; all 4 task/deviation commit hashes (`1a1d141`, `fafcdc0`, `fbe0072`, `84168f4`) verified present in `git log --oneline --all`.
