---
phase: 05-timeline-analysis
plan: 20
subsystem: timeline-analysis
tags: [dts-monotonic, mpegts, pes-header, iso13818-1, container-truth, gap-closure, gap-4]

requires:
  - phase: 05-timeline-analysis
    provides: "05-15's PesTimestampRecord / PidStats::pes_timestamps and the pure, tested detail::apply_container_dts join this plan wires into the orchestrator; 05-VERIFICATION.md's Gap 4 (and its Orchestrator Correction/Addendum) is this plan's evidence and closure target"
provides:
  - "DtsSource enum (demuxer/container_pes/container_unavailable) and StreamPacketScan::dts_source/dts_container_joined/dts_unjoined_with_pos in src/probe/packet_scan.h -- the seam every DTS-axis consumer now reads"
  - "The orchestrator's packet_scan-implies-ts_scan union rule (MPEG-TS only) and its container-DTS post-pass in src/probe/orchestrator.cpp -- substitutes 05-15's detail::apply_container_dts join once, ahead of every analyzer, so every MPEG-TS DTS consumer (dts_monotonic, size.stream_bitrate, size.peak_bitrate, derive_cadence's DTS fallback) reads the SAME container-truth PacketRecord::dts with no per-analyzer patch"
  - "timeline.dts_monotonic's dts_source evidence object and its container_unavailable -> insufficient_data skip (src/analyzers/timeline/monotonic.cpp), plus the MPEG-TS decode timestamps (UD-3) section in docs/checks/timeline.dts_monotonic.md"
  - "Gap 4 closed: timeline.dts_monotonic passes on timeline_start_base.mp4 vs timeline_start_shift.ts / timeline_avoffset_unknown.ts; the fabricated dts[1]==dts[0] tie is no longer declared in either pair's set; timeline_dts_backward.ts's genuine splice violations are unaffected"
affects: [timeline-analysis, dts-monotonic, size-stream_bitrate, cadence, pass-union]

actuals:
  tokens: 6210
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Single-substitution-point correction: the orchestrator mutates the shared PacketScanResult's own PacketRecord::dts once, ahead of every analyzer, rather than patching each DTS consumer independently -- every reader of packets[*].dts (monotonic.cpp, size.cpp, cadence.cpp) sees the corrected value with zero code change in those files."
    - "Pass-union implication generalized a second time: packet_scan-implies-ts_scan on MPEG-TS mirrors the existing parser_scan-implies-packet_scan rule -- an analyzer never has to declare a pass its own family-scoped post-pass needs."

key-files:
  created: []
  modified:
    - src/probe/packet_scan.h
    - src/probe/orchestrator.cpp
    - src/analyzers/timeline/monotonic.cpp
    - docs/checks/timeline.dts_monotonic.md
    - tests/unit/test_pass_union.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/integration/test_timeline_av_sync.cpp
    - tests/integration/test_timeline_structure.cpp

key-decisions:
  - "container_unavailable is decided in the orchestrator's post-pass (pes_timestamps_truncated OR an incomplete ts_scan with a packet at/beyond stop_offset), never in monotonic.cpp itself -- the skip decision and the substitution decision share one place, so a future DTS consumer (e.g. size.stream_bitrate) can read the same dts_source field for its own skip logic without re-deriving unavailability."
  - "Evidence nests per side (baseline/candidate), confirmed by running the real binary before writing the new regression-guard test, per the plan's own instruction -- the test asserts on the candidate side."
  - "No declared-set member besides timeline.dts_monotonic itself changed on the two affected pairs: size.stream_bitrate's video-scope candidate value moved from a lagged span (382306122.44ms) to an EXACT match with the baseline (378444444.44ms, confirming flagged assumption A1 -- the TS video's DTS span is now exactly 99 frames, like the MP4's), but its status stayed pass on both sides of the change, so nothing was added or dropped over it."

patterns-established: []

requirements-completed: [TIME-01, TIME-04]

coverage:
  - id: D1
    description: "Container-truth DTS reaches every MPEG-TS DTS consumer through one orchestrator substitution point, proven end to end on the MP4-to-TS tracer pair and the unknown-priming mirror: timeline.dts_monotonic now passes on both, with dts_source evidence showing container_pes/container_joined=100"
    requirement: "TIME-04"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_start_duration.cpp - the MP4-to-TS tracer pair declares its complete expected finding set under --profile remux"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_av_sync.cpp - the MPEG-TS remux (unknown-priming) pair declares its complete expected finding set under --profile remux"
        status: pass
    human_judgment: false
  - id: D2
    description: "dts_monotonic reports its DTS source in evidence on every measurement (TS and non-TS alike) and skips insufficient_data when MPEG-TS container truth is unavailable; the packet_scan-implies-ts_scan union rule is pinned"
    requirement: "TIME-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - on an MPEG-TS input, Pass::packet_scan implies Pass::ts_scan / the same analyzer list on an MP4 input never logs Pass::ts_scan"
        status: pass
    human_judgment: false
  - id: D3
    description: "The declared sets stop enshrining the fabricated tie (removed from the MP4-to-TS tracer set and its mirror); the <= rule's genuine case (timeline_dts_backward.ts's two real splice violations) is unaffected; a regression guard proves the PTS-only self-compare pair stays pass with the correct evidence shape"
    requirement: "TIME-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp - an MPEG-TS stream copy whose PES headers carry PTS only reports dts_monotonic pass from container-truth DTS"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp - the dts_backward trigger pair declares its complete expected finding set under --profile remux, and count_non_pass equals that set's size exactly"
        status: pass
    human_judgment: false
  - id: D4
    description: "Full suite and the designated-leg byte-exact goldens both green; no golden touched"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure (994/994, 6 pre-existing skips); MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure (994/994, all five byte-exact goldens ran and matched, unit.console_vt the only skip)"
        status: pass
      - kind: other
        ref: "git diff --stat -- tests/golden/ (empty)"
        status: pass
    human_judgment: false

duration: ~45min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 20: Container-Truth DTS Closes Gap 4 Summary

**MPEG-TS `timeline.dts_monotonic` now judges the PES header's own decode-timestamp truth instead of libavformat's read-back inference -- the fabricated `dts[1]==dts[0]` tie is gone, every other MPEG-TS DTS consumer reads the same substituted value through one orchestrator post-pass, and `timeline_dts_backward.ts`'s genuine splice violations still count exactly as before.**

## Performance

- **Duration:** ~45 min
- **Started:** 2026-09-18
- **Completed:** 2026-09-18
- **Tasks:** 3/3 completed
- **Files modified:** 8

## Accomplishments

- `DtsSource` (`demuxer` / `container_pes` / `container_unavailable`) and `StreamPacketScan::dts_source` / `dts_container_joined` / `dts_unjoined_with_pos` in `src/probe/packet_scan.h` -- the seam every DTS-axis consumer now reads.
- `src/probe/orchestrator.cpp` gains the `packet_scan`-implies-`ts_scan` union rule (MPEG-TS only, mirroring the existing `parser_scan`-implies-`packet_scan` rule) and the container-DTS post-pass: for each MPEG-TS stream, it joins 05-15's `detail::apply_container_dts` against the PID's own `PidStats::pes_timestamps`, once, directly after 05-17's re-probe step and ahead of every analyzer. A stream whose container truth cannot be trusted (`ts_scan`'s global PES-record budget exhausted before this PID's list, or a packet at/beyond a partial scan's `stop_offset`) is marked `container_unavailable` and left untouched. Non-TS inputs never enter this block.
- `timeline.dts_monotonic` (`src/analyzers/timeline/monotonic.cpp`) now takes the stream's own `StreamPacketScan` (not just its packets): it skips `insufficient_data` when `is_ts && dts_source == container_unavailable`, and otherwise adds a `dts_source` evidence object (`{"source","container_joined","unjoined_with_pos"}`) to every computed measurement, TS and non-TS alike -- non-TS always reads `{"source":"demuxer","container_joined":0,"unjoined_with_pos":0}`.
- `docs/checks/timeline.dts_monotonic.md` gains the "MPEG-TS decode timestamps (UD-3)" section: the `<=` rule is unchanged, the DTS judged on MPEG-TS is now the PES header's own value (or its PTS when PTS-only, ISO/IEC 13818-1's absent-DTS rule), libavformat's read-back inference is never judged, and the `dts_source` evidence contract is documented as a new key present on every measurement.
- Two new `pass_union` unit tests pin the union rule: an MPEG-TS input with only `demux_header`+`packet_scan` declared logs `Pass::ts_scan` exactly once; the same analyzer list on an MP4 input never logs it.
- Re-measured against the real binary before editing (Task 3's own instruction): `timeline_start_base.mp4` vs `timeline_start_shift.ts` and vs `timeline_avoffset_unknown.ts` both now report `timeline.dts_monotonic` `pass` on the video stream (was 1 `fail` violation on each before this plan), with evidence `dts_source={"source":"container_pes","container_joined":100,"unjoined_with_pos":0}`. `timeline.dts_monotonic` removed from both pairs' declared sets in `test_timeline_start_duration.cpp` (Test 4) and `test_timeline_av_sync.cpp` (its mirror), replaced with a causal note explaining the correction. `timeline_start_base.mp4` vs `timeline_dts_backward.ts` is unchanged: still 2 genuine `fail` violations (splice-induced, first_violation_index 50/88), the `<=` rule's real case untouched by this plan.
- New regression-guard `TEST_CASE` in `test_timeline_structure.cpp` (`timeline_structure - an MPEG-TS stream copy whose PES headers carry PTS only reports dts_monotonic pass from container-truth DTS`): compares `timeline_start_shift.ts` against itself, asserts video-scope `dts_monotonic` `pass` with candidate value 0 and evidence `dts_source.source=container_pes`/`container_joined=100` on the candidate side (evidence nests per side -- confirmed against a real run before writing the assertion), and re-confirms `timeline_dts_backward.ts`'s own pair still reports exactly 2 non-pass `timeline.dts_monotonic` findings.
- Full suite green: 994/994 (was 993/993 before this plan's own new tests; 6 pre-existing skips: `unit.console_vt` plus 5 designated-leg-only byte-exact goldens). `MEDIADIFF_DESIGNATED_LEG=1 ctest`: also 994/994, all five byte-exact goldens ran (not skipped) and matched, `unit.console_vt` the only skip; `git diff --stat -- tests/golden/` empty.

## Task Commits

Each task was committed atomically:

1. **Task 1: Container-truth DTS reaches dts_monotonic on MPEG-TS, proven end to end on the MP4-to-TS tracer pair** - `33a9590` (feat)
2. **Task 2: dts_monotonic reports its DTS source and skips when container truth is unavailable, and the pass-union rule is pinned** - `2b8e91e` (feat)
3. **Task 3: The declared sets stop enshrining the inferred tie, genuine violations still count, and the DTS-consumer impact is recorded** - `348aa6e` (test)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/probe/packet_scan.h` -- `DtsSource` enum, `StreamPacketScan::dts_source`/`dts_container_joined`/`dts_unjoined_with_pos`
- `src/probe/orchestrator.cpp` -- the `packet_scan`-implies-`ts_scan` union rule and the container-DTS post-pass
- `src/analyzers/timeline/monotonic.cpp` -- `emit_dts_monotonic` takes the stream's own `StreamPacketScan`, adds `dts_source` evidence, skips `insufficient_data` on `container_unavailable`
- `docs/checks/timeline.dts_monotonic.md` -- the "MPEG-TS decode timestamps (UD-3)" section and the `dts_source` evidence contract
- `tests/unit/test_pass_union.cpp` -- two new `TEST_CASE`s pinning the MPEG-TS-only union rule
- `tests/integration/test_timeline_start_duration.cpp` / `tests/integration/test_timeline_av_sync.cpp` -- `timeline.dts_monotonic` dropped from the MP4-to-TS tracer declared set and its unknown-priming mirror, with a causal correction note replacing the old comment
- `tests/integration/test_timeline_structure.cpp` -- new regression-guard `TEST_CASE`

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- `container_unavailable` is decided once, in the orchestrator's post-pass, so the skip decision and the substitution decision share one place.
- Evidence nests per side (baseline/candidate) -- confirmed against the real binary before writing the new test's assertions, per the plan's own instruction.
- No declared-set member besides `timeline.dts_monotonic` changed: `size.stream_bitrate`'s video-scope candidate value moved to an exact match with the baseline (flagged assumption A1's own predicted outcome -- the TS video's DTS span is now exactly 99 frames, like the MP4's) but stayed `pass` on both sides throughout, so nothing else was added or dropped from either declared set.

## Re-measured Declared Sets (Task 3, before editing)

**`timeline_start_base.mp4` vs `timeline_start_shift.ts`** (`--profile remux --json`), non-pass findings after this plan's Tasks 1+2 landed:
```
container.format global fail
video.profile video fail
video.level video fail
video.resolution video fail
timeline.start global fail
timeline.duration.coherence audio info
timeline.av_drift audio fail
timeline.av_drift.pattern audio fail
size.file global fail
size.stream_bitrate audio warn
size.overhead global info
meta.tags global warn
meta.tags video warn
```
(13 findings -- `timeline.dts_monotonic` absent; the pre-Task-1 set carried the same 13 plus `timeline.dts_monotonic video fail`, 14 total.) `timeline.dts_monotonic` evidence (video, candidate side): `{"source":"container_pes","container_joined":100,"unjoined_with_pos":0}`, `violation_count`=0.

**`timeline_start_base.mp4` vs `timeline_avoffset_unknown.ts`**: identical non-pass set to the pair above (same `-c copy` recipe), same before/after shape.

**`timeline_start_base.mp4` vs `timeline_dts_backward.ts`** (unaffected by this plan -- recorded for completeness):
```
container.format global fail
video.frame_rate.measured video warn
timeline.start global fail
timeline.duration video fail
timeline.duration audio fail
timeline.duration.coherence audio info
timeline.dts_monotonic video fail
timeline.dts_monotonic audio fail
timeline.gaps audio fail
timeline.vfr_profile video warn
timeline.vfr_profile audio warn
timeline.av_drift audio fail
timeline.av_drift.pattern audio fail
size.file global fail
size.stream_bitrate video fail
size.stream_bitrate audio fail
size.peak_bitrate video fail
size.peak_bitrate audio fail
size.overhead global info
meta.tags global warn
meta.tags video warn
meta.tags audio warn
```
Both `timeline.dts_monotonic` findings unchanged before and after this plan (genuine splice-induced violations, container truth was already available and correctly judged pre- and post-substitution since the splice itself is real).

## Before/After: `size.stream_bitrate` and `video.frame_rate.measured` on affected TS fixtures

Measured via a scratch git worktree of the pre-plan commit (`5e9db8f`, symlinking the main checkout's `vcpkg/` submodule, removed after use -- never `git stash`) for "before", and the current build for "after":

| Pair | Finding | Before (candidate `ms`) | After (candidate `ms`) | Status before -> after |
|---|---|---|---|---|
| start_base.mp4 vs start_shift.ts | `size.stream_bitrate` (video) | 382306122.4444 | 378444444.4444 (exact match with baseline) | pass -> pass |
| start_base.mp4 vs start_shift.ts | `size.stream_bitrate` (audio) | 71909585.2672 | 71909585.2672 (unchanged) | warn -> warn |
| start_base.mp4 vs start_shift.ts | `video.frame_rate.measured` | 25000.0 | 25000.0 (unchanged) | pass -> pass |
| start_base.mp4 vs avoffset_unknown.ts | (same recipe, identical values to the row above) | | | |
| start_base.mp4 vs dts_backward.ts | `size.stream_bitrate` (video/audio) | 795174890.33 / 116822740.99 | unchanged (genuine splice re-encode, container truth already available pre-plan) | fail -> fail |
| start_base.mp4 vs dts_backward.ts | `video.frame_rate.measured` | 40296.68 | unchanged | warn -> warn |

The video-scope `size.stream_bitrate` change on the two `-c copy` remux pairs is the flagged assumption A1 case landing exactly as predicted: with container truth, the TS video's DTS span is now exactly 99 frames, like the MP4's, instead of the demuxer's one-frame-lagged inferred span -- an EXACT match with the baseline, but the status was already `pass` before this plan (the old lagged span was still within tolerance), so no declared-set member changed.

## Deviations from Plan

None - plan executed exactly as written. All three tasks' acceptance criteria passed on the first implementation attempt; no auto-fixes, no architectural questions.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Gap 4 is closed. `05-VERIFICATION.md`'s frontmatter gap "Goal / SC2 / TIME-04 -- timeline.dts_monotonic reports a DTS violation the file does not contain" is resolved by this plan; a future phase-close pass should mark it accordingly.
- No blockers for remaining Phase 5 gap-closure plans. This plan touched only `src/probe/packet_scan.h`, `src/probe/orchestrator.cpp`, `src/analyzers/timeline/monotonic.cpp`, `docs/checks/timeline.dts_monotonic.md`, and the three affected test files -- `src/analyzers/timeline/av_sync.cpp` and every step/span decision file (05-22's scope) are untouched, exactly as scoped.
- `DtsSource`/`dts_container_joined`/`dts_unjoined_with_pos` are now available on `StreamPacketScan` for any future DTS-axis consumer (e.g. a `size.stream_bitrate`-specific `dts_source` evidence key, if ever wanted) with no further orchestrator change needed -- the substitution already happened by the time any analyzer runs.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

All 8 claimed modified files verified present on disk (`[ -f ]`). All three claimed commit hashes (`33a9590`, `2b8e91e`, `348aa6e`) verified present in `git log --oneline --all`. Full suite (994/994) and `MEDIADIFF_DESIGNATED_LEG=1` (994/994) both re-confirmed passing, `tests/golden/` untouched.
