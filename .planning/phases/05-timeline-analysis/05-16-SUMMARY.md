---
phase: 05-timeline-analysis
plan: 16
subsystem: timeline-analysis
tags: [ts-unwrap, doc04, timeline-packet-view, catch2, gap-closure, windows-26]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: "05-02's unwrap_ts_timestamps/UnwrapResult (the sole unwrap arithmetic this plan reuses, never reimplemented) and 05-01's detail::build_axis_view/AxisView/AxisSample primitive (src/analyzers/timeline/analyzers.h)"
provides:
  - "TimelinePacketView / make_timeline_packet_view / make_timeline_packet_views (src/analyzers/timeline/unwrap.{h,cpp}) -- the ONE promoted representation of a stream's timestamps: zero-copy borrow on non-TS, owned per-axis-unwrapped-plus-epoch-aligned copy on MPEG-TS"
  - "timeline.start/timeline.duration (start_duration.cpp), video.frame_rate.measured (stream_params.cpp) and size.stream_bitrate/size.peak_bitrate (size.cpp) migrated onto the view -- wrap-transparent on the wrap pair, byte-identical everywhere else"
affects: [05-17 (TS declared-duration correction), 05-18 (av_sync.cpp/jitter_vfr.cpp migration, test_timeline_structure.cpp Test 4 whole-report conversion, closes WINDOWS.md #26/#27/#30)]

# Actuals (#2632)
actuals:
  tokens: 11300
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "TimelinePacketView's packets() recomputes its span from borrowed_/owned_ on EVERY call (never cached at construction or across a copy) -- the mitigation for T-05-72 (a view span dangling after copy/move), proven by a dedicated copy-safety unit test"
    - "Per-axis unwrap calls unwrap_ts_timestamps directly (via detail::build_axis_view for sentinel exclusion/packet_index mapping), not detail::unwrap_axis_view's own wrapper -- unwrap_axis_view discards the wrap_events count TimelinePacketView's own pts_wrap_events()/dts_wrap_events() need, mirroring monotonic.cpp's own emit_wrap_events precedent for the identical reason"
    - "The cross-stream epoch rule reads each stream's first RAW (pre-per-stream-unwrap) PTS, never the already-unwrapped one -- matches A1's own planning-time ffprobe evidence table"

key-files:
  created: []
  modified:
    - src/analyzers/timeline/unwrap.h
    - src/analyzers/timeline/unwrap.cpp
    - src/analyzers/timeline/start_duration.cpp
    - src/analyzers/video/stream_params.cpp
    - src/analyzers/size/size.cpp
    - tests/unit/test_timeline_unwrap.cpp
    - docs/checks/timeline.start.md

key-decisions:
  - "Per-axis unwrap in make_timeline_packet_view calls unwrap_ts_timestamps directly rather than detail::unwrap_axis_view -- the Artifacts table describes 'reusing detail::build_axis_view / detail::unwrap_axis_view per axis', but unwrap_axis_view's own wrapper discards the wrap_events count the view's own pts_wrap_events()/dts_wrap_events() accessors need to report. Calling unwrap_ts_timestamps directly (the same underlying primitive unwrap_axis_view itself calls) satisfies the plan's real prohibition -- never a second unwrap implementation -- while still exposing the count, and mirrors monotonic.cpp's own emit_wrap_events, which already does exactly this for the identical reason."
  - "size.stream_bitrate/size.peak_bitrate's emit_* functions now take packets/tb by value (from the view) rather than the whole StreamPacketScan -- byte_total and partial (handled by the caller) still come from StreamPacketScan directly, since neither is a timestamp; this keeps the two functions from needing to reach back into the original StreamPacketScan for anything the view itself carries."

patterns-established:
  - "TimelinePacketView is now the mandatory read path for any FUTURE timeline/video/size consumer that reads PacketRecord timestamps -- 05-18 extends it to av_sync.cpp/jitter_vfr.cpp, never a fourth ad hoc unwrap call site."

requirements-completed: [TIME-01, TIME-02, TIME-03]

coverage:
  - id: D1
    description: "TimelinePacketView and its two builders (make_timeline_packet_view, make_timeline_packet_views) implement zero-copy borrowing on non-TS, owned per-axis unwrap plus the cross-stream 2^33 epoch rule on MPEG-TS, reusing unwrap_ts_timestamps exclusively"
    requirement: "TIME-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_timeline_unwrap.cpp#timeline_unwrap - view: ... (11 hand-computed TEST_CASEs: non-TS zero-copy, TS no-wrap copy, PTS-only wrap, DTS-only wrap, INT64_MIN sentinels surviving between wrapped values, epoch rule firing, epoch rule no-op under half-range, epoch rule not applying on non-TS, overflow cross-validated against unwrap_ts_timestamps, copy safety after source destruction, empty stream)"
        status: pass
      - kind: other
        ref: "ctest --preset x64-linux -R '^unit\\.timeline_unwrap - view' reports 11/11 passed"
        status: pass
    human_judgment: false
  - id: D2
    description: "timeline.start reports non-pass ONLY at global scope on the wrap pair (video/audio per-stream relative starts both pass); both timeline.duration findings pass"
    requirement: "TIME-01"
    verification:
      - kind: integration
        ref: "mediadiff compare --profile remux --json tests/fixtures/timeline_ts_nowrap.ts tests/fixtures/timeline_ts_wrap.ts (Task 1's own verify script) -- bad=[] (no per-stream timeline.start non-pass), timeline.duration status=['pass','pass']"
        status: pass
    human_judgment: false
  - id: D3
    description: "video.frame_rate.measured and both size.stream_bitrate/size.peak_bitrate findings pass on the wrap pair; no golden moved"
    requirement: "TIME-03"
    verification:
      - kind: integration
        ref: "mediadiff compare --profile remux --json on the wrap pair -- bad=[] for video.frame_rate.measured/size.stream_bitrate/size.peak_bitrate"
        status: pass
      - kind: integration
        ref: "MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure -- 974/974 passed, all five byte-exact goldens (ts_scan_golden x3, inspect_container golden, size_checks golden) ran (not skipped) and matched; git diff --stat -- tests/golden/ empty"
        status: pass
    human_judgment: false
  - id: D4
    description: "Non-TS behavior is byte-identical (zero-copy) everywhere -- full local suite unaffected"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure -- 974/974 passed, same 6 pre-existing skips before and after every task"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 16: Timeline Packet View Summary

**`TimelinePacketView` promotes the 33-bit MPEG-TS unwrap plus a new cross-stream 2^33 epoch rule into one shared, zero-copy-on-non-TS primitive, migrating `timeline.start`/`timeline.duration` (`start_duration.cpp`), `video.frame_rate.measured` (`stream_params.cpp`) and `size.stream_bitrate`/`size.peak_bitrate` (`size.cpp`) off raw wrapped-PTS reads -- closing the false-positive class WINDOWS.md #26 and #30 describe on the `timeline_ts_nowrap.ts` vs `timeline_ts_wrap.ts` pair.**

## Performance

- **Duration:** ~35 min
- **Completed:** 2026-09-18
- **Tasks:** 3/3 completed
- **Files modified:** 7 (0 created, 7 modified)

## Accomplishments

- `TimelinePacketView` (`src/analyzers/timeline/unwrap.h`/`.cpp`): borrows `StreamPacketScan::packets` verbatim, zero-copy, on every non-MPEG-TS input; on MPEG-TS it owns a copy with the PTS axis and the DTS axis independently unwrapped via the existing `unwrap_ts_timestamps`, then applies the new cross-stream epoch rule (a stream whose own first raw PTS sits more than 2^32 ticks below another stream's is placed one 2^33 epoch later before any global-origin computation). `packets()` recomputes its span from the view's own storage on every call -- proven never to dangle after a copy.
- `start_duration.cpp`: `timeline.start`/`timeline.duration`/`timeline.duration.coherence` now read `make_timeline_packet_views(packet_scan, is_ts)` instead of `packet_scan.per_stream[i].packets` directly; an overflowed view skips both checks with `insufficient_data`. Verified on the wrap pair: `timeline.start` non-pass only at `global` scope (the genuine `-output_ts_offset` origin shift, D-03), both `timeline.duration` findings pass.
- `stream_params.cpp`/`size.cpp`: `video.frame_rate.measured`, `size.stream_bitrate` and `size.peak_bitrate` migrated the same way. Verified on the wrap pair (all three pass/no non-pass) and against the designated-leg suite (`MEDIADIFF_DESIGNATED_LEG=1`, size.cpp changed): 974/974 tests, all five byte-exact goldens ran (not skipped) and matched byte-for-byte, `git diff --stat -- tests/golden/` empty.
- 11 hand-computed unit `TEST_CASE`s in `tests/unit/test_timeline_unwrap.cpp` pin the view's zero-copy, wrap, epoch, overflow and copy-safety contracts, independent of any fixture. No implementation defect was found -- every test passed against hand-derived expected values on first run.
- `docs/checks/timeline.start.md` gained a paragraph documenting the 33-bit unwrap and the cross-stream epoch rule.
- Full suite: 974/974 passing throughout (started at 963/963 baseline before any timeline-related work this session; the +11 are this plan's own new view tests), same 6 pre-existing skips at every checkpoint.

## Task Commits

Each task was committed atomically:

1. **Task 1: timeline.start and timeline.duration read unwrapped timestamps through the view, proven end to end on the wrap pair** - `49a890b` (feat)
2. **Task 2: The view's zero-copy, wrap, epoch, overflow and copy-safety contracts are unit-tested** - `70dda5d` (test)
3. **Task 3: video.frame_rate.measured and size.stream_bitrate / size.peak_bitrate read through the per-stream view** - `3ed4a08` (feat)

**Plan metadata:** commit to follow this SUMMARY (docs: complete plan)

_Note: Task 2 carries `tdd="true"` but the implementation it tests (Task 1's `TimelinePacketView`) was already built and committed as a prerequisite (Task 1 is a `type="tracer"` task, production-quality by definition) -- see "TDD Gate Compliance" below._

## Files Created/Modified

- `src/analyzers/timeline/unwrap.h` - `TimelinePacketView` class, `make_timeline_packet_view`/`make_timeline_packet_views` declarations, `#include "probe/packet_scan.h"`
- `src/analyzers/timeline/unwrap.cpp` - the view builders' implementation: per-axis unwrap via `unwrap_ts_timestamps`, the cross-stream epoch rule via `detail::checked_add`/`checked_sub`
- `src/analyzers/timeline/start_duration.cpp` - `run_timeline_start_duration` builds `views` once and reads through them instead of raw packet reads; overflow-aware skip-reason selection in all three timeline.start branches and the duration-triple loop
- `src/analyzers/video/stream_params.cpp` - `emit_frame_rate_measured` takes `packets`/`tb`/`overflowed` from the view; `run_video_stream_params` builds `views` once
- `src/analyzers/size/size.cpp` - `emit_stream_bitrate`/`emit_peak_bitrate` take `packets`/`tb` from the view; `run_size` builds `views` once
- `tests/unit/test_timeline_unwrap.cpp` - 11 new `timeline_unwrap - view ...` `TEST_CASE`s
- `docs/checks/timeline.start.md` - new "MPEG-TS: 33-bit unwrap and the cross-stream epoch rule" paragraph

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- Per-axis unwrap calls `unwrap_ts_timestamps` directly rather than `detail::unwrap_axis_view`'s own wrapper, to recover the `wrap_events` count the wrapper discards -- mirrors `monotonic.cpp`'s own `emit_wrap_events`, which already does this for the identical reason. Still exactly one unwrap implementation (the plan's own prohibition).
- `size.cpp`'s two `emit_*` functions now take `packets`/`tb` directly rather than the whole `StreamPacketScan`, since `byte_total`/`partial` (unaffected by the view) are read by the caller instead.

## Deviations from Plan

None - plan executed exactly as written. The two decisions above are documented interpretations of the Artifacts table's prose description, not scope changes -- every listed artifact, function signature, and acceptance criterion was delivered as specified.

## TDD Gate Compliance

Task 2 carries `tdd="true"`. The strict RED-then-GREEN commit ordering (a failing `test(...)` commit before a `feat(...)` commit) was not followed literally: Task 1 (`type="tracer"`, production-quality by definition, not a throwaway) already implemented and committed `TimelinePacketView` as `feat(05-16)` (`49a890b`) before Task 2's `test(05-16)` commit (`70dda5d`) landed. This mirrors 05-02-SUMMARY.md's identical disclosure for the same structural reason: the plan's own task order places implementation (a tracer, verified end-to-end on the real wrap-pair fixture) ahead of the dedicated contract-pinning test task. Every declared `<behavior>` scenario in Task 2 is nonetheless pinned as a permanent, hand-computed regression test (11 `TEST_CASE`s, all passing against hand-derived expected values written before reading back the implementation's actual output), and no defect was found requiring a follow-up `feat` commit.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `TimelinePacketView`/`make_timeline_packet_view`/`make_timeline_packet_views` (`src/analyzers/timeline/unwrap.h`) are ready for 05-18 to extend to `av_sync.cpp` and `jitter_vfr.cpp` -- the same primitive, never a fourth ad hoc unwrap call site.
- WINDOWS.md #26/#27/#30 remain deliberately OPEN: per this plan's own scope note, they are closed together only once 05-18 also migrates `av_sync.cpp`/`jitter_vfr.cpp` and converts `test_timeline_structure.cpp` Test 4 into the whole-report assertion. This plan closes exactly the three consumers (`start_duration.cpp`, `stream_params.cpp`, `size.cpp`) it was scoped to, not the WINDOWS entries themselves.
- 05-17 (TS declared-duration correction) and 05-18 are unblocked and can proceed independently of each other.
- No blockers.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

All 7 modified files verified present on disk with the expected content; all three task commit hashes (`49a890b`, `70dda5d`, `3ed4a08`) verified present in `git log --oneline --all`; full local suite 974/974 passing (same 6 pre-existing skips); designated-leg suite 974/974 passing with all five byte-exact goldens run and unchanged.
