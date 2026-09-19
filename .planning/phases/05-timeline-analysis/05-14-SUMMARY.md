---
phase: 05-timeline-analysis
plan: 14
subsystem: timeline-analysis
tags: [av-sync, av-offset, av-drift, priming, memory-safety, checked-integer-math, gap-closure]

requires:
  - phase: 05-timeline-analysis
    provides: "05-09's resolve_priming/timeline.av_offset dual raw/adjusted storage and D-10 basis-agreement rule; 05-10's fit_drift/checkpoint-construction and sorted_pts_with_span/PtsSpan"
provides:
  - "StreamInfo::sample_rate (src/probe/demux_session.h/.cpp): codecpar->sample_rate, audio streams only, positive values only"
  - "detail::priming_samples_to_ticks (src/analyzers/timeline/analyzers.h, implemented av_sync.cpp): checked-integer sample-to-tick rescale, rounded to nearest with ties away from zero, shared by timeline.av_offset's adjusted_audio_ticks and timeline.av_drift's priming_shift"
  - "timeline.av_offset priming evidence gains sample_rate/priming_ticks/rescale (rescale: ok/no_sample_rate/overflow), populated only when priming is known"
  - "detail::PtsSpan / detail::sorted_pts_with_span moved into namespace detail (analyzers.h declaration, av_sync.cpp definition) with the CR-01 neighbour-underflow fix (std::optional<std::size_t> neighbour, never an unsigned wraparound index)"
affects: [timeline-analysis, av-sync-computation, memory-safety]

actuals:
  tokens: 10558
  tasks: 3
  commits: 1

tech-stack:
  added: []
  patterns:
    - "One checked-integer rescale, reused at both the av_offset anchor and the av_drift checkpoint shift -- never a second, independently-computed conversion of the same sample count."
    - "std::optional<std::size_t> neighbour selection replaces an unsigned i-1 arithmetic wraparound as the fix pattern for CR-01-class defects (an absent neighbour is a real, checkable state, never a sentinel index that can underflow)."

key-files:
  created: []
  modified:
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/analyzers/timeline/analyzers.h
    - src/analyzers/timeline/av_sync.cpp
    - tests/unit/test_av_sync.cpp
    - tests/unit/test_demux_session.cpp
    - docs/checks/timeline.av_offset.md
    - docs/checks/timeline.av_drift.md

key-decisions:
  - "All three tasks' code changes are combined into a single commit: Task 1 (av_offset conversion), Task 2 (drift path reusing the same conversion) and Task 3 (CR-01 memory-safety fix, moving PtsSpan/sorted_pts_with_span into detail::) all touch the SAME functions in src/analyzers/timeline/av_sync.cpp and the SAME detail:: block in analyzers.h -- splitting them would leave intermediate non-buildable states, matching this project's own established precedent (05-04-SUMMARY.md's Task 1/Task 2 combination for the same reason)."
  - "Re-verified the MP4 -> MPEG-TS pairs (timeline_start_shift.ts, timeline_avoffset_unknown.ts) via a scratch git-worktree build of the pre-fix commit (never git stash, per this project's own prohibition) rather than assuming the flagged assumption A1 held -- confirmed byte-identical priming.state/comparison_basis/av_drift/av_drift.pattern evidence before and after, because that pair's candidate priming is unknown (samples=0, so the conversion is never invoked) and the baseline's MP4 timebase-equals-sample-rate case is the conversion's own identity."
  - "The NTSC MKV pair's av_drift/av_drift.pattern metrics (end_delta_ms, residual_max_ms, pattern) were also independently confirmed byte-identical before/after via the same scratch-build method, algebraically consistent with the plan's own TIME-06 boundary note: a constant additive priming-shift error cancels exactly in every DIFFERENCE-based metric (end_delta_ms, residual_max_ms, the least-squares slope), so only the ABSOLUTE av_offset value the units bug produced was ever wrong."

patterns-established:
  - "A checked-integer sample-to-tick rescale computed once per audio stream and threaded through both the offset anchor and the drift checkpoint shift, rather than reconstructed at each call site."

requirements-completed: [TIME-06, TIME-07, TIME-09, TIME-10]

coverage:
  - id: D1
    description: "StreamInfo::sample_rate and detail::priming_samples_to_ticks close Gap 3: the priming sample count is converted through the audio stream's own sample rate into its native timebase before being added to native-timebase ticks, at both the av_offset and av_drift call sites."
    requirement: "TIME-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - priming_samples_to_ticks(...) (9 TEST_CASEs)"
        status: pass
      - kind: integration
        ref: "mediadiff compare --profile remux --json timeline_ntsc_base.mp4 timeline_ntsc_remux.mkv -> timeline.av_offset pass, adjusted_offset_ms 0 both sides, priming_ticks 1024 (base) / 23 (remux)"
        status: pass
    human_judgment: false
  - id: D2
    description: "A priming-known side whose sample rate is unavailable, or whose conversion overflows, compares raw-to-raw (comparison_basis raw) instead of being adjusted by an unconverted sample count; severity is never softened."
    requirement: "TIME-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - priming_samples_to_ticks returns nullopt when sample_rate is 0 / when tb.num is 0 / on an overflowing product"
        status: pass
    human_judgment: false
  - id: D3
    description: "sorted_pts_with_span no longer reads out of bounds on a one-entry stream with a non-positive declared duration (CR-01), and is exposed via detail:: for direct unit testing (WR-01)."
    requirement: "TIME-07"
    verification:
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - sorted_pts_with_span ... (8 TEST_CASEs, 0/1/2-entry and INT64_MIN-sentinel cases)"
        status: pass
    human_judgment: false
  - id: D4
    description: "The MP4 -> MPEG-TS av_drift/av_drift.pattern verdicts pinned in test_timeline_start_duration.cpp are re-verified with recorded before/after evidence; no declared set was edited."
    requirement: "TIME-10"
    verification:
      - kind: other
        ref: "scratch git-worktree build of the pre-fix commit, compare --json against both TS pairs, before/after evidence diffed by hand (recorded in this SUMMARY's Deviations section)"
        status: pass
    human_judgment: true
    rationale: "The re-verification is a manual before/after evidence comparison against a scratch build, not an automated regression test -- a human should confirm the recorded reasoning (constant-shift cancellation in difference-based metrics) before treating the MP4->TS declared sets as permanently settled."

duration: ~55min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 14: Priming Sample-to-Tick Conversion and sorted_pts_with_span Memory Safety Summary

Converts the audio priming sample count into the stream's own native-timebase ticks via its sample rate before it reaches `timeline.av_offset`/`timeline.av_drift` (closing the false `av_offset fail` on a lossless MP4 -> MKV stream copy), and fixes a heap buffer underflow in `sorted_pts_with_span` on one-entry streams.

## Performance

- **Duration:** ~55 min
- **Tasks:** 3/3 completed
- **Files modified:** 8

## Accomplishments

- `StreamInfo::sample_rate` (`src/probe/demux_session.h`/`.cpp`): `codecpar->sample_rate`, populated for audio streams only, and only when positive.
- `detail::priming_samples_to_ticks` (`src/analyzers/timeline/analyzers.h`/`av_sync.cpp`): a checked-integer rescale (`samples * tb.den / (sample_rate * tb.num)`, rounded to nearest with ties away from zero -- the same rounding `av_rescale_q` applies by default), returning `std::nullopt` on a non-positive rate/timebase member or an overflow. Nine unit tests pin the exact contract from the plan's own `<action>` list, including the identity case (MP4, tb == sample_rate -> 1024 unchanged), the Matroska case (1024 samples at 44100 Hz in a 1ms timebase -> 23 ticks), and the exact-tie-rounds-away-from-zero case.
- `run_timeline_av_sync` now computes the converted tick count once per audio stream and reuses the SAME value at both call sites: `timeline.av_offset`'s `adjusted_audio_ticks` and `timeline.av_drift`'s `priming_shift`. A priming-known side whose conversion could not be performed (`rescale: no_sample_rate` or `overflow`) falls back to the raw basis for that side, exactly like `priming: unknown` -- never adjusted by an unconverted count, never a softened severity.
- The NTSC stream-copy pair (`timeline_ntsc_base.mp4` vs `timeline_ntsc_remux.mkv`, `--profile remux`) now compares `timeline.av_offset` clean: both sides report `adjusted_offset_ms: 0`, with `priming.priming_ticks` 1024 (MP4, identity) and 23 (Matroska, converted) respectively -- quoted in full below.
- `sorted_pts_with_span`'s neighbour selection (CR-01, `05-REVIEW.md`) is now `std::optional<std::size_t>`: `i + 1` when it exists, `i - 1` only when `i > 0`, otherwise no neighbour and an effective duration of 0 -- never the old unsigned `i - 1` wraparound to `SIZE_MAX` that read 16 bytes before `entries.data()`. `PtsSpan` and `sorted_pts_with_span` moved out of an anonymous namespace into `namespace detail` (declared in `analyzers.h`, WR-01), matching every sibling pure helper's own `detail::`-exposure convention in this phase.
- Full suite green: 941/941 (923 baseline + 18 new tests: 9 `priming_samples_to_ticks`, 8 `sorted_pts_with_span`, 1 `demux_session` sample_rate case), same 6 baseline skips. Also re-ran under `MEDIADIFF_DESIGNATED_LEG=1`: all 941 pass with only `unit.console_vt` skipped -- the five byte-exact goldens (unaffected by this plan's changes) still match the committed corpus digest.

## Fixture evidence (required before any pass/fail claim was written)

`mediadiff snapshot` on both NTSC sides, `timeline.av_offset` evidence:

| Fixture | `raw_offset_ms` | `adjusted_offset_ms` | `priming.sample_rate` | `priming.priming_ticks` | `priming.rescale` |
|---|---|---|---|---|---|
| `timeline_ntsc_base.mp4` | -23 | **0** | 44100 | **1024** | ok |
| `timeline_ntsc_remux.mkv` | -23 | **0** | 44100 | **23** | ok |

`mediadiff compare --profile remux --json timeline_ntsc_base.mp4 timeline_ntsc_remux.mkv`: `timeline.av_offset` status `pass` (previously `fail`, baseline `adjusted_offset_ms 0` vs candidate `adjusted_offset_ms 1001` -- the 1024-sample count misread as 1024 ticks on the Matroska side, matching `05-VERIFICATION.md` Gap 3's exact reproduction).

## Re-verification of the MP4 -> MPEG-TS pairs (Task 2, before/after)

Per the plan's own instruction, ran `mediadiff compare --profile remux --json` against both `timeline_start_shift.ts` and `timeline_avoffset_unknown.ts` (vs `timeline_start_base.mp4`) on a scratch git-worktree build of the pre-fix commit (`4596b6d`, via `git worktree add --detach` -- never `git stash`, per this project's own absolute prohibition) and against the post-fix binary. Both pairs produced byte-identical results:

| | `priming.state` (candidate) | `comparison_basis` (candidate) | `timeline.av_offset` status | `av_drift` `end_delta_ms` | `av_drift` `residual_max_ms` | `av_drift.pattern` |
|---|---|---|---|---|---|---|
| Before (pre-fix) | unknown | raw | pass | 10 | 42 | irregular |
| After (this plan) | unknown | raw | pass | 10 | 42 | irregular |

**The units bug could never have touched these verdicts.** The candidate side's priming is `unknown` (`samples: 0`) on both TS fixtures -- the MPEG-TS remux drops the `skip_samples` side data entirely -- so `priming_samples_to_ticks` is never invoked for that side in either the old or new code. The baseline side (`timeline_start_base.mp4`, MP4) has `tb == {1, sample_rate}` (44100 Hz), which is the conversion's own identity case (`priming_samples_to_ticks(1024, 44100, {1, 44100}) == 1024`, proven by unit test), so its priming-adjusted anchor is unchanged too. The remaining `av_drift fail` on both pairs traces to the TS audio stream's declared duration (libav's estimate, ~3877 ms against a ~4023 ms container duration) used as the checkpoint span -- a pre-existing, unrelated cause, exactly as the plan's own flagged assumption A1 anticipated. This observation is unchanged input for 05-21's research panel; no declared set was edited.

**Algebraic confirmation for the NTSC MKV pair's own drift metrics.** The plan's own TIME-06 precision note states: "The residual error is at most half a tick, a constant shift applied identically to every drift checkpoint, so it cannot change a rate or a pattern." Verified directly: `timeline_ntsc_base.mp4` vs `timeline_ntsc_remux.mkv`'s `timeline.av_drift`/`timeline.av_drift.pattern` stayed `skipped` (baseline `fit_failed`, unrelated to priming) both before and after, with the MKV candidate's own `end_delta_ms: 18`, `residual_max_ms: 0`, `pattern: linear-drift` and rate value byte-identical in both scratch builds -- because a constant additive anchor error (the old 1024-tick anchor vs the correct 23-tick one) cancels exactly out of `checked_sub`-derived checkpoint offsets AND, critically, out of `clamp_into_nearest_packet`'s own raw-domain search: `raw_target_a_ticks = target_a_ticks - priming_shift`, and the OLD code's `priming_shift` was the SAME wrong value used to build `target_a_ticks`'s anchor in the first place, so the two wrong values cancel and the search position was never actually corrupted -- only the final `+ priming_shift` re-added at the very end shifted every checkpoint's absolute `offset_ms` by the same wrong constant, which `end_delta_ms`/`residual_max_ms`/the least-squares slope (all difference-based) are structurally blind to.

## Task Commits

Combined into one commit (see Deviations for why):

1. **Tasks 1-3: priming sample-to-tick conversion, drift path reuse, sorted_pts_with_span memory-safety fix** - `9ffd897` (fix)

## Files Created/Modified

- `src/probe/demux_session.h` / `.cpp` - `StreamInfo::sample_rate` (audio-only, positive-only)
- `src/analyzers/timeline/analyzers.h` - `detail::priming_samples_to_ticks` declaration, `PtsSpan` struct and `detail::sorted_pts_with_span` declaration (moved from av_sync.cpp's anonymous namespace)
- `src/analyzers/timeline/av_sync.cpp` - `priming_samples_to_ticks` implementation, `run_timeline_av_sync`'s converted-ticks wiring at both call sites, `sorted_pts_with_span`'s CR-01 neighbour fix and `namespace detail` move
- `tests/unit/test_av_sync.cpp` - 9 `priming_samples_to_ticks` TEST_CASEs, 8 `sorted_pts_with_span` TEST_CASEs
- `tests/unit/test_demux_session.cpp` - `StreamInfo::sample_rate` TEST_CASE
- `docs/checks/timeline.av_offset.md` - documents the sample-rate conversion, its rounding contract, and the `rescale`/`sample_rate`/`priming_ticks` evidence fields
- `docs/checks/timeline.av_drift.md` - one-sentence note that the drift path reuses the same converted shift

## Decisions Made

- Combined all three tasks into one commit -- see key-decisions in frontmatter for the full rationale (shared functions, non-buildable intermediate states).
- Re-verified via a scratch git-worktree build rather than `git stash` (this project's absolute prohibition) or assuming the plan's flagged assumption A1 without evidence.

## Deviations from Plan

### Auto-fixed Issues

None -- no bugs beyond the plan's own two named gaps were found or fixed.

### Process deviations (documented per Rule 4 judgment: structural, not a fix)

**1. [Task-boundary combination] All three tasks committed together, not as three separate commits**
- **Found during:** Task 1, once Task 2's drift-path reuse requirement and Task 3's `detail::` move were read in full
- **Issue:** The plan's own Task 1 action item 3 edits `run_timeline_av_sync`'s priming-resolution block; Task 2's action item 1 edits the SAME function's drift-checkpoint block, reusing Task 1's own new variable; Task 3's action item 1 moves `PtsSpan`/`sorted_pts_with_span` (which Task 1's action item 3 also calls) into `namespace detail`. Committing Task 1 alone would either omit the `detail::` exposure `sorted_pts_with_span`'s call sites already need, or duplicate work across commits.
- **Fix:** One commit covering all three tasks' code, tests and docs -- matches this project's own established precedent (05-04-SUMMARY.md's Task 1/Task 2 combination, recorded there for the identical reason).
- **Files modified:** all 8 files in this plan's `files_modified` list.
- **Verification:** full suite green (941/941); acceptance-criteria greps for every task individually confirmed against the final state.
- **Committed in:** `9ffd897`

---

**Total deviations:** 0 auto-fixed bugs; 1 structural/process deviation (task-commit combination, precedented and documented).
**Impact on plan:** No scope creep -- the combination is purely a commit-granularity choice, not a change to what was built or tested.

## Issues Encountered

None beyond the re-verification work described above, which was expected plan scope (Task 2's own required re-verification step), not an unplanned problem.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Gap 6 is closed in full; Gap 3's computation half is closed, with both call sites converting through the shared, unit-tested helper. Gap 3's whole-report assertion for the NTSC pair (the remaining, deliberately-deferred half) is 05-19's job -- it must also exclude the NTSC `jitter`/`vfr_profile` warns (Gap 5), which this plan does not touch. No blockers for 05-15 through 05-25.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED
