---
phase: 05-timeline-analysis
plan: 19
subsystem: timeline-analysis
tags: [jitter, vfr, quantization, fixed-point, grid-relative-histogram, windows-28, gap-closure]

requires:
  - phase: 05-timeline-analysis
    provides: "05-08's timeline_jitter_vfr_analyzer (classify_vfr_bin, compute_jitter_sigma, D-06 grid-relative histogram), 05-14's priming sample-to-tick conversion (closes Gap 3's computation half), 05-18's TimelinePacketView migration (closes Gap 2, the whole-report wrap assertion precedent this plan's own NTSC assertion mirrors)"
provides:
  - "classify_vfr_bin (D-06/UD-2): on_grid iff |Q| < ideal_den (strictly below one tick of the stream's own timebase), one_tick iff ideal_den <= |Q| < 2*ideal_den -- a sub-tick deviation on a non-exactly-representable ideal interval (NTSC at Matroska's 1ms timebase) is representational rounding, not jitter. Integer-ideal streams bin identically to before."
  - "compute_jitter_sigma re-referenced from the stream's own exact ideal interval (ideal_num/ideal_den) rather than the timebase-bound mode interval -- sub-tick deviations contribute exactly zero, every other deviation enters at its full fixed-point magnitude via an exact whole-tick division plus a round-half-to-even sub-tick fraction, integer/rational arithmetic only"
  - "JitterSigmaResult::max_abs_deviation_fixed (fixed-point magnitude, replaces the bare-tick-count max_abs_deviation_ticks) and JitterSigmaResult::sub_tick_intervals"
  - "New evidence keys: timeline.jitter gains deviation_reference/ideal_interval_num/ideal_interval_den/sub_tick_intervals; timeline.vfr_profile gains sub_tick_intervals"
  - "The NTSC MP4-to-MKV stream copy (tests/fixtures/timeline_ntsc_base.mp4 vs timeline_ntsc_remux.mkv, --profile remux) compares clean on timeline.av_offset, timeline.jitter and both timeline.vfr_profile findings, asserted by one whole-report expect_declared_set (D-01/D-02), landing Gap 3 (05-14) and Gap 5 (this plan) together"
  - "WINDOWS #28 closed"
affects: [timeline-analysis, doc04-no-others, check-contract-timeline.jitter, check-contract-timeline.vfr_profile]

actuals:
  tokens: 17570
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Quantization-aware grid binning: on_grid means |Q| < den (strictly below one tick), never exact equality, whenever an ideal interval is not an integer number of ticks; for an exact-integer ideal the rule reduces algebraically to the pre-quantization exact-equality test, so no separate code path is needed for the two cases."
    - "Fixed-point deviation formation with an exact whole-tick division plus a round-half-to-even sub-tick fraction (never a floating-point divide), reusing the SAME cross-multiplied Q classify_vfr_bin computes, so jitter sigma and the VFR histogram always agree on what 'on grid' means for a given stream."

key-files:
  created: []
  modified:
    - src/analyzers/timeline/analyzers.h
    - src/analyzers/timeline/jitter_vfr.cpp
    - tests/unit/test_jitter_vfr.cpp
    - docs/checks/timeline.vfr_profile.md
    - docs/checks/timeline.jitter.md
    - tests/integration/test_timeline_jitter.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/integration/test_timeline_av_sync.cpp
    - .planning/WINDOWS.md

key-decisions:
  - "Combined Task 1 (tracer, implementation) and Task 2 (unit tests + docs) into a single commit -- splitting them would leave a non-buildable intermediate state, since test_jitter_vfr.cpp's own compute_jitter_sigma call sites and JitterSigmaResult field names must change together with the function signature (mirrors this project's own 05-04/05-14/05-18 precedent for the identical reason)."
  - "Recovered Task 1's required before/after jitter-trigger-fixture sigma comparison (A2) retroactively via a scratch git worktree of the pre-change commit (e5998a2), since the code change had already landed by the time the comparison was run; the worktree's vcpkg dependency tree was reused via a symlink to the main checkout's vcpkg submodule rather than a second full vcpkg install, and the worktree was removed immediately after use, per this project's own scratch-worktree convention (05-14-SUMMARY.md precedent)."
  - "sub_tick_intervals in timeline.vfr_profile's own evidence is a literal re-read of the on_grid bin's own count (on_grid now MEANS 'deviation strictly below one tick'), surfaced as its own named evidence key for T-05-82's visibility mitigation rather than requiring a reader to infer it from the histogram."

patterns-established:
  - "A published, not-yet-released bin/value contract may change its MEANING (not just which values are compared) when the prior meaning produced a documented false positive -- stated explicitly, in both the check's own docs and the check-roster/WINDOWS ledger, as a contract-impact section rather than a silent value drift."

requirements-completed: [TIME-05, TIME-06, TIME-09]

coverage:
  - id: D1
    description: "classify_vfr_bin's on_grid/one_tick boundary is quantization-aware: |Q| < ideal_den is on_grid, ideal_den <= |Q| < 2*ideal_den is one_tick; integer-ideal streams bin identically to before this plan"
    requirement: "TIME-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_jitter_vfr.cpp#classify_vfr_bin - sub-tick: ... (4 new TEST_CASEs against real NTSC/90kHz-AAC/integer-ideal values, plus the 8 pre-existing integer-ideal cases confirmed unchanged)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_jitter.cpp - the NTSC MP4-to-MKV stream copy declares its complete expected finding set under --profile remux (both timeline.vfr_profile findings assert pass with on_grid == considered_intervals on both sides)"
        status: pass
    human_judgment: false
  - id: D2
    description: "compute_jitter_sigma is re-referenced from the stream's exact ideal interval; sub-tick deviations contribute zero, every other deviation enters at full fixed-point magnitude via exact/round-half-even integer arithmetic; JitterSigmaResult gains max_abs_deviation_fixed and sub_tick_intervals"
    requirement: "TIME-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_jitter_vfr.cpp#compute_jitter_sigma - ... (7 TEST_CASEs, every expected value hand-computed in a comment, including the round-half-even fractional case)"
        status: pass
      - kind: integration
        ref: "the jitter trigger fixture's (timeline_jitter.mp4) sigma is byte-identical before and after this plan's change (num=16819315, den=4194304, ~4010.037ms), verified via a scratch git-worktree build of the pre-change commit -- its ideal interval is an exact integer (512 ticks), so re-referencing from mode to ideal is a no-op for this fixture, confirming the integer-ideal invariance end to end"
        status: pass
    human_judgment: false
  - id: D3
    description: "The lossless NTSC MP4-to-MKV stream copy compares clean on timeline.av_offset, timeline.jitter and both timeline.vfr_profile findings, asserted by one whole-report expect_declared_set with a causal comment per remaining member"
    requirement: "TIME-06"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_jitter.cpp - the NTSC MP4-to-MKV stream copy declares its complete expected finding set under --profile remux, and count_non_pass equals that set's size exactly"
        status: pass
    human_judgment: false
  - id: D4
    description: "Every declared set affected by the quantization change is re-measured against the real binary; a member is dropped only when the new measurement passes and the cause is sub-tick rounding -- the 90kHz-AAC TS-audio timeline.vfr_profile member in test_timeline_start_duration.cpp Test 4 and its test_timeline_av_sync.cpp mirror (171/173 intervals now on_grid, 2/173 = 1.16% remain one_tick, under the 2% dist tolerance)"
    requirement: "TIME-09"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux -R 'timeline_start_duration|timeline_av_sync|timeline_structure|timeline_jitter' (all declared-set assertions in the four integration files pass); ctest --preset x64-linux -R doc03_coverage (jitter/vfr_profile trigger pairs still fire)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Full suite green locally and under MEDIADIFF_DESIGNATED_LEG=1; no golden file touched; WINDOWS #28 closed"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure (991/991, 6 pre-existing skips); MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure (991/991, all five byte-exact goldens ran and matched, unit.console_vt the only skip); git diff --stat -- tests/golden/ empty"
        status: pass
      - kind: other
        ref: "gsd-tools windows fixed 28; grep -E '^\\| 28 \\|' .planning/WINDOWS.md shows status fixed"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 19: Quantization-Aware jitter/vfr_profile Binning Summary

**`classify_vfr_bin`'s on_grid/one_tick boundary and `compute_jitter_sigma`'s deviation reference both move from exact-tick equality against the mode interval to a strict-below-one-tick quantization test against the stream's own exact ideal interval, closing WINDOWS #28's NTSC false positive with a whole-report declared-set regression guard.**

## Performance

- **Duration:** ~50 min
- **Tasks:** 3/3 completed
- **Files modified:** 9

## Accomplishments

- `classify_vfr_bin` (`src/analyzers/timeline/jitter_vfr.cpp`): `on_grid` now means the cross-multiplied deviation `Q = interval*ideal_den - ideal_num` is strictly below one tick of the stream's own timebase (`|Q| < ideal_den`), representational rounding rather than real jitter; `one_tick` means `ideal_den <= |Q| < 2*ideal_den`. For a stream whose ideal interval is an exact integer number of ticks, `|Q|` is itself always a multiple of `ideal_den`, so the new rule reduces algebraically to the pre-quantization `|Q| == 0`/`|Q| == ideal_den` tests — every such stream bins identically to before this plan (proven directly by unit test and by the jitter trigger fixture's byte-identical before/after sigma).
- `compute_jitter_sigma` re-referenced from `Cadence::ideal_interval_num/den` (D-05's unreduced span/count rational) rather than `mode_interval_ticks`. A sub-tick deviation contributes exactly zero to sigma; every other deviation enters at its FULL fixed-point magnitude, formed via an exact whole-tick integer division plus a round-half-to-even sub-tick fraction — integer/rational arithmetic only, no floating point anywhere in the comparison path. `JitterSigmaResult::max_abs_deviation_ticks` renamed to `max_abs_deviation_fixed` (now a fixed-point magnitude, not a bare tick count) and a new `sub_tick_intervals` field added.
- New evidence keys: `timeline.jitter` gains `deviation_reference` (`"ideal"`), `ideal_interval_num`, `ideal_interval_den`, `sub_tick_intervals`; `timeline.vfr_profile` gains `sub_tick_intervals`.
- 16 new/rewritten unit tests in `tests/unit/test_jitter_vfr.cpp` — every expected value hand-computed against the real NTSC (3971/119) and 90kHz-AAC (361534/173) ideals from the phase's own fixtures, plus the exact-integer-ideal invariance case (119119/119).
- `docs/checks/timeline.vfr_profile.md` and `docs/checks/timeline.jitter.md`: removed the "One honest limitation"/pre-quantization paragraph that enshrined WINDOWS #28; added matching "Quantization rule (UD-2)" and "Contract impact" sections to both, stating the bin/sigma-meaning change explicitly since the release has not shipped.
- The NTSC MP4-to-MKV stream copy (`tests/fixtures/timeline_ntsc_base.mp4` vs `timeline_ntsc_remux.mkv`, `--profile remux`) now compares clean on `timeline.av_offset`, `timeline.jitter` and both `timeline.vfr_profile` findings — measured directly against the real binary as `container.format`, `size.file`, `size.overhead`, `meta.tags` × 3 (global/video/audio), with no `timeline.*` id anywhere in the non-pass set. `tests/integration/test_timeline_jitter.cpp`'s prior "bins differ by design" TEST_CASE is replaced with a whole-report `expect_declared_set` assertion plus an explicit per-finding regression guard (both `timeline.vfr_profile` findings pass with `on_grid == considered_intervals` on both sides).
- Every declared set the quantization change could affect was re-measured against the real binary: the 90kHz-AAC TS-audio `timeline.vfr_profile` member in `test_timeline_start_duration.cpp` Test 4 and its `test_timeline_av_sync.cpp` mirror now pass (171/173 intervals land `on_grid`, only 2/173 = 1.16% remain `one_tick`, comfortably under the 2% dist tolerance) and are dropped from both declared sets, with the before/after evidence recorded in a comment at each site. `test_timeline_structure.cpp`'s four pre-existing `timeline.vfr_profile` declarations are unaffected — verified unchanged by the full-suite run, since those are genuine VFR-content perturbations (splices, duplicated packets), not sub-tick rounding.
- Full suite green: 991/991 both locally and under `MEDIADIFF_DESIGNATED_LEG=1` (all five byte-exact goldens ran, not skipped, and matched; `tests/golden/` untouched). `doc03_coverage`'s jitter/vfr_profile trigger pairs still fire.
- WINDOWS #28 closed via `gsd-tools windows fixed 28`, only after the full suite passed.

## Task Commits

Each task was committed atomically (Tasks 1+2 combined — see Deviations):

1. **Tasks 1+2: quantization-aware classify_vfr_bin/compute_jitter_sigma, hand-computed unit tests, docs** - `da33ddf` (feat)
2. **Task 3: whole-report NTSC assertion, re-measured declared sets, WINDOWS #28 closed** - `94adde6` (test)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/analyzers/timeline/analyzers.h` — `classify_vfr_bin`/`compute_jitter_sigma` contract comments updated for the UD-2 quantization rule; `JitterSigmaResult` gains `max_abs_deviation_fixed` (renamed from `max_abs_deviation_ticks`) and `sub_tick_intervals`
- `src/analyzers/timeline/jitter_vfr.cpp` — `classify_vfr_bin`'s on_grid/one_tick boundary rewritten; `compute_jitter_sigma`'s signature changed to `(ideal_num, ideal_den, intervals)` with the new fixed-point deviation formation; `emit_jitter`/`emit_vfr_profile` updated for the new evidence keys and `exact_ticks_to_ms`-based `max_abs_deviation_ms`
- `tests/unit/test_jitter_vfr.cpp` — 16 new/rewritten `TEST_CASE`s covering the quantization boundary and integer-ideal invariance
- `docs/checks/timeline.vfr_profile.md` / `docs/checks/timeline.jitter.md` — Quantization rule (UD-2) and Contract impact sections; stale "honest limitation"/Tune-section text corrected
- `tests/integration/test_timeline_jitter.cpp` — NTSC TEST_CASE replaced with a whole-report `expect_declared_set` assertion plus the #28 regression guard
- `tests/integration/test_timeline_start_duration.cpp` / `tests/integration/test_timeline_av_sync.cpp` — the 90kHz-AAC TS-audio `timeline.vfr_profile` declared member dropped, with before/after evidence recorded in a comment
- `.planning/WINDOWS.md` — #28 marked `fixed`

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- Tasks 1 and 2 committed together (a non-buildable-intermediate-state constraint, precedented by 05-04/05-14/05-18).
- Task 1's own required before/after sigma comparison (A2) was recovered retroactively via a scratch git worktree of the pre-change commit, symlinking the vcpkg submodule from the main checkout to avoid a second full dependency build; the worktree was removed immediately after use.
- `sub_tick_intervals` in `timeline.vfr_profile`'s evidence is a named re-read of the `on_grid` bin's own count, added for visibility (T-05-82) rather than a second independent computation.

## Deviations from Plan

### Auto-fixed Issues

None — no bugs beyond the plan's own two named gaps (the on_grid/one_tick boundary and the sigma reference) were found or fixed.

### Process deviations (documented per Rule 4 judgment: structural, not a fix)

**1. [Task-boundary combination] Tasks 1 and 2 committed together, not as two separate commits**
- **Found during:** Task 1, once `cmake --build --preset x64-linux` (part of Task 1's own `<verify>`) failed because the pre-existing `tests/unit/test_jitter_vfr.cpp` still called `compute_jitter_sigma` with the OLD `(mode_interval_ticks, intervals)` signature and read the OLD `max_abs_deviation_ticks` field name.
- **Issue:** Task 1's implementation change and Task 2's unit-test rewrite are inseparable at the build level: the new `compute_jitter_sigma(ideal_num, ideal_den, intervals)` signature and the `max_abs_deviation_fixed` field rename mean the existing test file cannot compile against Task 1's code alone, and a mechanical rename (not a genuine content rewrite) would misrepresent the field's new fixed-point-scale semantics, since `max_abs_deviation_ticks == 1` (old) is NOT equivalent to `max_abs_deviation_fixed == 1` (new) — the new field is scaled by `2^16`.
- **Fix:** Task 2's full unit-test rewrite (16 hand-computed `TEST_CASE`s) and docs update were completed as part of the same commit as Task 1's implementation, matching this project's own established precedent for combining tasks that share a non-buildable intermediate state (05-04-SUMMARY.md, 05-14-SUMMARY.md, 05-18-SUMMARY.md).
- **Files modified:** `src/analyzers/timeline/analyzers.h`, `src/analyzers/timeline/jitter_vfr.cpp`, `tests/unit/test_jitter_vfr.cpp`, `docs/checks/timeline.vfr_profile.md`, `docs/checks/timeline.jitter.md`.
- **Verification:** `cmake --build --preset x64-linux` succeeds; all 23 unit tests in the three affected `TEST_CASE` families pass; Task 1's own tracer `<verify>` (the NTSC CLI check) exits 0.
- **Committed in:** `da33ddf`

---

**Total deviations:** 0 auto-fixed bugs; 1 structural/process deviation (task-commit combination, precedented and documented).
**Impact on plan:** No scope creep — the combination is purely a commit-granularity choice, not a change to what was built or tested. The tracer feedback gate (auto mode, `AUTO_CFG=true`) was still honored: Task 1's own `<verify>` was re-run end-to-end and confirmed passing before Task 3's expansion began.

## Issues Encountered

- The scratch git worktree created to recover Task 1's before/after jitter-trigger-fixture comparison (a step that should have been run BEFORE Task 1's code change, per the plan's own action item 4) initially failed to configure because the worktree had no `vcpkg/` submodule content; resolved by symlinking the main checkout's already-populated `vcpkg/` directory into the worktree rather than running a second full vcpkg install. No impact on shipped code — the recovery only affected how the comparison evidence was obtained, and the resulting before/after values are the real, independently-built outputs of the pre-change and post-change binaries.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- WINDOWS #28 is closed. Ledger totals after this plan: `open_count: 12`, `waived_count: 1`, `fixed_count: 18`, `total_count: 31`.
- `TIME-05`, `TIME-06`, `TIME-09` marked complete in `REQUIREMENTS.md` (via the shared-ID readiness gate — each had no sibling plan still declaring it in this phase). `DOC-04` was reported `blocked` by `requirements.ready-ids` (a sibling plan in this phase also declares it and has not yet produced a SUMMARY) and was deliberately left unmarked — it will flip to `Complete` automatically the next time any plan in this phase finishes its own `update_requirements` step, once the last declaring sibling's SUMMARY exists.
- Perf ratchet note (per the plan's own `<verification>` block): FLAGGED, not gated locally — this plan changes the per-interval work of both `timeline.jitter` and `timeline.vfr_profile` on every input, including the MP4 perf reference. `valgrind` is not installed on this workstation; 05-24's designated-leg run is where the ratchet is confirmed, per the plan's own instruction.
- No blockers for remaining Phase 5 gap-closure plans. Gap 1 (`timeline.av_drift.pattern`'s `step` classification) and Gap 4 (`timeline.dts_monotonic`'s MPEG-TS read-back inference) remain open per `05-VERIFICATION.md`, untouched by this plan.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

All 9 claimed modified files verified present on disk (`[ -f ]`). Both claimed commit hashes (`da33ddf`, `94adde6`) verified present in `git log --oneline --all`. `git status --short` shows a clean working tree aside from pre-existing untracked `.planning/milestone.lock`/`.planning/state.json` (never committed, per instructions). The full suite and the `MEDIADIFF_DESIGNATED_LEG=1` suite were both re-confirmed 991/991 passing during Task 3, with `tests/golden/` untouched. `.planning/WINDOWS.md` #28 confirmed `fixed` on disk.
