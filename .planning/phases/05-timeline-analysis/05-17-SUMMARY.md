---
phase: 05-timeline-analysis
plan: 17
subsystem: timeline-analysis
tags: [mpegts, 33-bit-wrap, demux-session, overflow-correction, doc04, windows-26]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: "05-06's correct_ts_overflow=0 fix (the primary session's own wrap-corrupted declared durations, WINDOWS.md #26) and 05-16's TimelinePacketView/make_timeline_packet_view (the wrap-count primitive this plan's orchestrator trigger reuses, never a second wrap detector)"
provides:
  - "DemuxSession::reprobe_ts_declared_durations(): a second, overflow-corrected libav open, isolated from the primary session's own AVFormatContext/PacketScan/read_frame_call_count/probe_warnings, that recovers container_duration_ticks()/StreamInfo::declared_duration_ticks on a genuinely-wrapping MPEG-TS file"
  - "DeclaredDurationSource (demuxer / overflow_corrected_reprobe / withheld_wrap_uncorrectable) and DemuxSession::declared_duration_source(), plus a declared_duration_source evidence field on timeline.duration and timeline.duration.coherence"
  - "detail::StreamLayoutKey / detail::stream_layouts_match: the (codec_type, stream_id) layout-match guard that withholds rather than misattributes a reprobe's durations (T-05-75)"
  - "An orchestrator.cpp trigger: on a TS input whose packet scan shows at least one wrap (05-16's view, no second detector), the re-probe runs once before any analyzer reads a declared duration"
affects: [05-18 (av_sync.cpp/jitter_vfr.cpp migration, still owns the remaining raw-PTS reads and WINDOWS.md #26/#27/#30's full closure)]

# Actuals (#2632)
actuals:
  tokens: 11964
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "open_context(): the alloc/interrupt-budget/diagnostics-accumulator/avformat_open_input/avformat_find_stream_info sequence factored out of DemuxSession::open into a file-local helper parameterized on correct_ts_overflow, so the reprobe reuses it (with the opposite value) rather than a second, hand-duplicated copy of the same wiring."
    - "A session-level override, not a call-site override: container_duration_ticks()/StreamInfo::declared_duration_ticks check declared_duration_source_ internally and return the reprobed/withheld value transparently -- every existing caller (start_duration.cpp AND av_sync.cpp) gets the corrected value with zero changes to its own call site."

key-files:
  created: []
  modified:
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/probe/orchestrator.cpp
    - src/analyzers/timeline/start_duration.cpp
    - tests/unit/test_demux_session.cpp
    - docs/checks/timeline.duration.md
    - docs/checks/timeline.duration.coherence.md

key-decisions:
  - "The declared-duration override lives entirely inside DemuxSession's own accessors (container_duration_ticks()/stream_info().declared_duration_ticks), not as a parameter threaded through every caller -- this means src/analyzers/timeline/av_sync.cpp's own video_span_ticks/audio_span_ticks (StreamInfo::declared_duration_ticks reads at av_sync.cpp:625/782) are corrected as a side effect of this plan, with av_sync.cpp completely untouched, exactly matching this plan's own objective note that 'the declared members ... feed [av_sync's] declared spans.'"
  - "reprobe_ts_declared_durations takes ONLY the two duration fields from the second, overflow-corrected context -- never start_time, per this plan's own prohibition (that context's start_time sits on libavformat's own shifted epoch once its default wrap correction has run, which would contradict this project's own doc-04-section-1.2 unwrap)."
  - "Test 2's own read_frame_call_count isolation proof compares the primary session's post-reprobe first PacketScan sweep against an entirely independent, never-reprobed control session opened on the same bytes -- not a second sweep on the SAME session, since av_read_frame cannot be usefully repeated on one session once it has reached EOF (a same-session double-sweep is not a valid isolation proof; a cross-session comparison is)."

patterns-established:
  - "A second, isolated libav open for recovery data (never a mutation of the primary session) is the shape any future 'primary session read this raw libav value under a project-specific option that corrupts it' gap should follow, mirroring this plan's own reprobe."

requirements-completed: [TIME-02, TIME-03]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "A wrapping TS file's declared durations (container + both streams) equal its non-wrapping twin's, sourced overflow_corrected_reprobe; timeline.duration.coherence passes at video scope on the wrap pair"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "mediadiff compare --profile remux --json timeline_ts_nowrap.ts timeline_ts_wrap.ts (Task 1's own verify script)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - reprobe: on a genuine wrap, the source becomes overflow_corrected_reprobe and the durations equal the nowrap twin's own demuxer values"
        status: pass
    human_judgment: false
  - id: D2
    description: "The re-probe is scoped to wrapping MPEG-TS only (never runs on a non-wrapping TS or any non-TS input) and fully isolated from the primary session's own AVFormatContext, PacketScan, read_frame_call_count and probe_warnings"
    requirement: "TIME-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - reprobe: leaves the primary session's read_frame_call_count and warning_count unchanged"
        status: pass
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - reprobe: orchestrated -- a non-wrapping TS file's timeline.duration evidence keeps declared_duration_source demuxer"
        status: pass
    human_judgment: false
  - id: D3
    description: "A failed/timed-out reprobe, or a stream-layout mismatch, withholds both declared members (absent, never compared corrupt); withholding proven via a nonexistent reprobe path and 5 detail::stream_layouts_match TEST_CASEs"
    requirement: "TIME-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - reprobe: a nonexistent path withholds both declared members"
        status: pass
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - stream_layouts_match: ... (5 TEST_CASEs)"
        status: pass
    human_judgment: false
  - id: D4
    description: "declared_duration_source documented on both docs/checks/timeline.duration.md and timeline.duration.coherence.md; designated-leg suite green with no golden regenerated"
    requirement: "DOC-04"
    verification:
      - kind: other
        ref: "grep -c declared_duration_source on both docs files (4 hits each); MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux (984/984, all 5 byte-exact goldens run, not skipped); git diff --stat -- tests/golden/ empty"
        status: pass
    human_judgment: false

duration: ~15min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 17: TS Declared-Duration Correction Summary

**An overflow-corrected re-probe recovers `timeline.duration`'s container/stream-declared members on a genuinely-wrapping MPEG-TS file, correcting them transparently for every existing consumer -- including `av_sync.cpp`'s own declared spans, whose file this plan never touches.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-09-18T17:03:30Z (approx, from the preceding commit)
- **Completed:** 2026-09-18T17:17:19Z
- **Tasks:** 3
- **Files modified:** 7

## Accomplishments

- `DemuxSession::reprobe_ts_declared_durations()` (`src/probe/demux_session.h`/`.cpp`): a second, fully isolated libav open with libavformat's default `correct_ts_overflow` (1), recovering `container_duration_ticks()`/`StreamInfo::declared_duration_ticks` on a genuinely-wrapping TS file. Duration fields only -- never `start_time`, which sits on libavformat's own shifted epoch once its default wrap correction has run.
- The alloc/interrupt-budget/diagnostics-accumulator/open/find_stream_info sequence was factored out of `DemuxSession::open` into a file-local `open_context()` helper, parameterized on `correct_ts_overflow`, so the reprobe reuses it with the opposite value rather than duplicating the wiring.
- `DeclaredDurationSource` (`demuxer` / `overflow_corrected_reprobe` / `withheld_wrap_uncorrectable`) and `detail::stream_layouts_match` (the `(codec_type, stream_id)` layout-match guard, T-05-75) close the "withhold rather than misattribute" half of the plan.
- `src/probe/orchestrator.cpp` triggers the reprobe once, only on a TS input whose packet scan reports at least one wrap via 05-16's own `TimelinePacketView::pts_wrap_events()`/`dts_wrap_events()` (never a second wrap detector), before any analyzer reads a declared duration.
- **Measured (snapshot evidence, both fixtures):** container 4023ms (both), video stream 4000ms (both, ticks 360000), audio stream 3877ms (both, ticks 348995). `timeline_ts_wrap.ts` reports `declared_duration_source: overflow_corrected_reprobe`; `timeline_ts_nowrap.ts` reports `demuxer`. All four values equal exactly, per the plan's own acceptance criterion.
- **`timeline.duration.coherence` effect:** video scope now `pass` on the wrap pair (was `fail` before this plan). Audio scope stays `info` -- a real, pre-existing, both-sides-shared container-vs-stream-audio-duration bookkeeping artifact (documented in 05-06-SUMMARY.md's Test 5), unrelated to the wrap and unaffected by this fix.
- **`av_sync.cpp`'s declared spans:** `av_sync.cpp:625`/`:782` read `StreamInfo::declared_duration_ticks` directly (`video_span_ticks`/`audio_span_ticks`, used to construct the K=32 checkpoint spacing). Because this plan's fix lives entirely inside `DemuxSession`'s own accessors, those two reads are now correct-magnitude on the wrap pair with `av_sync.cpp` completely untouched. This does **not** fix `timeline.av_offset`/`timeline.av_drift` themselves on the wrap pair -- both still `fail`, because `av_sync.cpp`'s own `first_presented_pts` calls read raw, un-unwrapped PTS with no unwrap step (WINDOWS.md #26's own note that this file was "not yet filed", plus 05-VERIFICATION.md's Gap 2 artifact list) -- explicitly 05-18's scope, and this plan does not touch `av_sync.cpp` or `jitter_vfr.cpp` per its own instructions.
- 10 new `demux_session` unit TEST_CASEs pin the reprobe's scope, fallback, and isolation contracts (see Task Commits below). No implementation defect found -- all pass against Task 1's own implementation on first run.
- `docs/checks/timeline.duration.md`/`timeline.duration.coherence.md` each gain a "Declared durations on a wrapping MPEG-TS file" section.

## Task Commits

Each task was committed atomically:

1. **Task 1: A wrapping TS file's declared durations come from an overflow-corrected re-probe** - `a491b91` (feat)
2. **Task 2: Re-probe, withholding and layout matching are unit-tested** - `628df24` (test)
3. **Task 3: Document the declared-duration source, confirm no golden moved** - `29016fc` (docs)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/probe/demux_session.h` - `DeclaredDurationSource`, `detail::StreamLayoutKey`/`stream_layouts_match`, `DemuxSession::reprobe_ts_declared_durations()`/`declared_duration_source()`, new private members
- `src/probe/demux_session.cpp` - `open_context()` helper (factored from `DemuxSession::open`), `reprobe_ts_declared_durations()` implementation, `stream_info()`/`container_duration_ticks()` override logic, `detail::stream_layouts_match` implementation
- `src/probe/orchestrator.cpp` - the TS wrap-triggered re-probe step, after the scan-error checks and before `Fingerprint fp;`
- `src/analyzers/timeline/start_duration.cpp` - `declared_duration_source` added to `timeline.duration`/`timeline.duration.coherence` evidence
- `tests/unit/test_demux_session.cpp` - 10 new `demux_session - reprobe ...`/`demux_session - stream_layouts_match ...` TEST_CASEs
- `docs/checks/timeline.duration.md`, `docs/checks/timeline.duration.coherence.md` - "Declared durations on a wrapping MPEG-TS file" sections

## Decisions Made

See `key-decisions` in frontmatter. Most significant: the override lives entirely inside `DemuxSession`'s own accessors rather than being threaded through call sites, so `av_sync.cpp`'s own declared-span reads are corrected as an unplanned-but-anticipated side effect with zero changes to that file.

## Deviations from Plan

None - plan executed exactly as written. The `key-decisions` entries above are documented interpretations, not scope changes: the plan's own Task 1 action text already specified the accessor-level override shape, and Task 2's read_frame_call_count test design (cross-session comparison rather than same-session double-sweep) is an implementation detail of how the plan's own behavior clause ("leaves read_frame_call_count() and warning_count() of the primary session unchanged") is proven, not a change to what was proven.

## Issues Encountered

Task 2's first draft of the `read_frame_call_count` isolation test called `run_packet_scan` twice on the SAME primary session (once before the reprobe, once after) and asserted equal counts -- this failed (`1 == 275`), because `av_read_frame` cannot be meaningfully re-run on one session once it has already reached EOF from a prior sweep; the second call returns `AVERROR_EOF` almost immediately, regardless of whether the reprobe touched anything. Corrected to compare the primary session's own post-reprobe sweep against an independent, never-reprobed control session opened on the same bytes -- a valid isolation proof, since the primary session's read state was never advanced before the reprobe in this version.

## User Setup Required

None - no external service configuration required.

## Wrap Pair Non-Pass Finding List (post-fix)

`mediadiff compare --profile remux --json tests/fixtures/timeline_ts_nowrap.ts tests/fixtures/timeline_ts_wrap.ts`, every non-`pass` finding (container.* / video.gop.* / video.hdr.* `skipped:not_applicable`/`skipped:requires_decode` rows omitted -- unrelated to this pair, present on every remux comparison):

| id | scope | status |
|---|---|---|
| `timeline.start` | global | fail |
| `timeline.duration.coherence` | audio | info |
| `timeline.wrap_events` | video | info |
| `timeline.wrap_events` | audio | info |
| `timeline.jitter` | video | skipped |
| `timeline.jitter` | audio | skipped |
| `timeline.vfr_profile` | video | warn |
| `timeline.vfr_profile` | audio | warn |
| `timeline.av_offset` | audio | fail |
| `timeline.av_drift` | audio | fail |

**Correction to this plan's own project-specifics text:** the expected-remaining-non-pass list named `timeline.av_drift.pattern` as still-failing; the real binary reports it `pass` on this pair (`av_drift.pattern` classifies the candidate's residual shape as `constant-offset`/`linear-drift`/etc. independent of the raw offset magnitude `av_drift` itself gates on, so the two checks can legitimately diverge). `timeline.duration.coherence` was expected "x2 (info)"; only the audio scope remains `info` post-fix -- the video scope now passes, which is this plan's own Task 1 acceptance criterion. `timeline.duration`, `timeline.duration.coherence` (video), `video.frame_rate.measured`, and `size.stream_bitrate`/`size.peak_bitrate` are all `pass` (05-16 and this plan's combined fix). This is 05-18's starting point: `timeline.av_offset`, `timeline.av_drift`, `timeline.av_drift.pattern` (unreachable-in-practice but still an id 05-18 owns verifying), and `timeline.vfr_profile` remain within `av_sync.cpp`/`jitter_vfr.cpp`'s own un-migrated raw-PTS-read scope.

## TDD Gate Compliance

Task 2 carries `tdd="true"`. The strict RED-then-GREEN commit ordering (a failing `test(...)` commit before a `feat(...)` commit) was not followed literally: Task 1 (`type="tracer"`, production-quality by definition, not a throwaway) already implemented and committed `reprobe_ts_declared_durations` as `feat(05-17)` (`a491b91`) before Task 2's `test(05-17)` commit (`628df24`) landed. This mirrors 05-16-SUMMARY.md's identical disclosure for the same structural reason: the plan's own task order places implementation (a tracer, verified end-to-end on the real wrap-pair fixture) ahead of the dedicated contract-pinning test task. Every declared `<behavior>` scenario in Task 2 is nonetheless pinned as a permanent regression test (10 TEST_CASEs, all passing on first run against Task 1's own implementation), and no defect was found requiring a follow-up `feat` commit.

## Next Phase Readiness

- `timeline.duration`/`timeline.duration.coherence` (video scope) are wrap-transparent on `timeline_ts_wrap.ts`; `av_sync.cpp`'s own declared spans are corrected as a side effect with that file untouched.
- WINDOWS.md #26 (`start_duration.cpp`/`stream_params.cpp`/`size.cpp`) is now fully addressed across 05-16 (computed/measured members) and this plan (declared members) -- ready for a human/05-18 decision on formally marking it fixed, since this plan's own declared files_modified is `.planning/`-scoped only for ledger updates and none were made here.
- WINDOWS.md #27 (`jitter_vfr.cpp`) and the remaining `av_sync.cpp` raw-PTS-read gap (05-VERIFICATION.md Gap 2's `av_offset`/`av_drift` artifacts) remain open, explicitly owned by 05-18.
- 05-18 can proceed independently; this plan's fix is additive and does not block or require any change to 05-18's own planned scope.
- No blockers.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

All 7 modified files verified present on disk with the expected content (`src/probe/demux_session.h`, `src/probe/demux_session.cpp`, `src/probe/orchestrator.cpp`, `src/analyzers/timeline/start_duration.cpp`, `tests/unit/test_demux_session.cpp`, `docs/checks/timeline.duration.md`, `docs/checks/timeline.duration.coherence.md`). All three task commit hashes (`a491b91`, `628df24`, `29016fc`) verified present in `git log --oneline --all`. Full local suite 984/984 passing (same 6 pre-existing skips); designated-leg suite 984/984 passing with all five byte-exact goldens run and unchanged (`git diff --stat -- tests/golden/` empty).
