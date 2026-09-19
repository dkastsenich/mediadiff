---
phase: 05-timeline-analysis
plan: 18
subsystem: timeline-analysis
tags: [ts-unwrap, av-sync, jitter, vfr-profile, timeline-packet-view, gap-closure, windows-26, windows-27, windows-30]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: "05-16's TimelinePacketView/make_timeline_packet_view/make_timeline_packet_views (the promoted timestamp representation this plan extends to the last two raw-PTS consumers) and 05-17's declared-duration correction (av_sync.cpp's own video_span_ticks/audio_span_ticks, corrected transparently as a prerequisite side effect)"
provides:
  - "src/analyzers/timeline/av_sync.cpp migrated onto TimelinePacketView (multi-stream builder): timeline.av_offset/timeline.av_drift/timeline.av_drift.pattern read epoch-aligned, unwrapped timestamps, never raw wrapped PTS"
  - "src/analyzers/timeline/jitter_vfr.cpp migrated onto TimelinePacketView (per-stream builder): timeline.jitter/timeline.vfr_profile read unwrapped timestamps"
  - "tests/integration/test_timeline_structure.cpp's wrap-pair TEST_CASE converted to a whole-report expect_declared_set assertion, with a causal comment per declared member"
  - "WINDOWS.md #26/#27/#30 closed; a new entry (#31) records av_sync.cpp's inclusion under the same root cause, also closed; #29 left untouched"
affects: [timeline-analysis, av-sync-computation, jitter-vfr-computation, defect-ledger]

# Actuals (#2632)
actuals:
  tokens: 8176
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "An overflowed TimelinePacketView on the SHARED primary video stream skips every compared audio stream's three checks (av_offset, av_drift, av_drift.pattern); an overflowed view on one compared audio stream skips only that stream's three checks -- av_sync.cpp's own two-tier overflow-propagation shape, distinct from jitter_vfr.cpp's simpler per-stream 'both ids skip together' rule, because av_offset/av_drift are cross-stream comparisons and jitter/vfr_profile are not."

key-files:
  created: []
  modified:
    - src/analyzers/timeline/av_sync.cpp
    - src/analyzers/timeline/jitter_vfr.cpp
    - tests/integration/test_timeline_structure.cpp
    - docs/checks/timeline.av_offset.md
    - .planning/WINDOWS.md

key-decisions:
  - "av_sync.cpp uses the MULTI-stream builder (make_timeline_packet_views), not the per-stream one, because av_offset/av_drift compare first PTS ACROSS the primary video stream and each audio stream -- both must land on the same cross-stream 2^33 epoch (05-16's own epoch rule). jitter_vfr.cpp uses the per-stream builder since jitter/vfr_profile are computed independently per stream with no cross-stream comparison."
  - "The wrap pair's measured complete non-pass set (Task 2) is exactly the four members flagged assumption A1 predicted: timeline.start (global, fail), timeline.wrap_events (video/audio, info), timeline.duration.coherence (audio, info). No extra or missing member was found -- the measurement matched the plan's own prediction on first run, so no investigation of a wrap-caused member was needed."

patterns-established:
  - "TimelinePacketView is now the read path for every timestamp-derived timeline consumer in the codebase -- no raw StreamPacketScan::packets read remains anywhere in src/analyzers/timeline/."

requirements-completed: [TIME-02, TIME-05, TIME-06, TIME-07, DOC-04]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "av_sync.cpp reads epoch-aligned timeline packet views for timeline.av_offset/timeline.av_drift/timeline.av_drift.pattern, never raw wrapped PTS -- wrap-transparent on the wrap pair"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "mediadiff compare --profile remux --json timeline_ts_nowrap.ts timeline_ts_wrap.ts -- av_offset/av_drift/av_drift.pattern all status=pass"
        status: pass
      - kind: other
        ref: "grep -v '^\\s*//' src/analyzers/timeline/av_sync.cpp | grep -c \"_stream\\.packets\" -- reports 0; grep -n make_timeline_packet_views av_sync.cpp -- matches"
        status: pass
    human_judgment: false
  - id: D2
    description: "jitter_vfr.cpp reads the per-stream timeline packet view for timeline.jitter/timeline.vfr_profile; an overflowed view skips both ids together"
    requirement: "TIME-05"
    verification:
      - kind: integration
        ref: "mediadiff compare --profile remux --json timeline_ts_nowrap.ts timeline_ts_wrap.ts -- timeline.vfr_profile status=pass on both streams; timeline.jitter status=pass (video) / skipped:vfr (audio, a genuine per-stream cadence classification, not a wrap artifact)"
        status: pass
      - kind: other
        ref: "grep -n make_timeline_packet_view src/analyzers/timeline/jitter_vfr.cpp -- matches"
        status: pass
    human_judgment: false
  - id: D3
    description: "The wrap pair's TEST_CASE is a whole-report expect_declared_set assertion (D-01/D-02), the complete set measured against the real binary with a causal comment per member; the prior scope-limited carve-out is gone"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux -R '^integration\\.timeline_structure - the wrap trigger pair declares' -- 1/1 passed"
        status: pass
      - kind: other
        ref: "grep -ci carve-out tests/integration/test_timeline_structure.cpp -- reports 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "WINDOWS.md #26/#27/#30 marked fixed only after both the full suite and the designated-leg suite pass; a new entry (#31) records av_sync.cpp's inclusion under the same root cause and is itself closed; #29 is untouched"
    requirement: "TIME-06"
    verification:
      - kind: other
        ref: "MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure -- 984/984 passed, all five byte-exact goldens ran (not skipped) and matched, git diff --stat -- tests/golden/ empty; gsd-tools windows fixed 26/27/30, windows append + fixed for the new #31 entry"
        status: pass
    human_judgment: false

duration: ~25min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 18: MPEG-TS Unwrap Migration Completion (av_sync.cpp / jitter_vfr.cpp) Summary

**Migrates the last two raw-timestamp consumers (`av_sync.cpp`, `jitter_vfr.cpp`) onto 05-16's `TimelinePacketView`, converts the wrap pair's integration test into a whole-report `expect_declared_set` assertion, and closes WINDOWS.md #26/#27/#30 -- Gap 2 (05-VERIFICATION.md SC2) is now fully closed: every `timeline.*` consumer reads epoch-aligned, unwrapped MPEG-TS timestamps.**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-09-18T17:19:45Z (approx, from the preceding plan's completion)
- **Completed:** 2026-09-18
- **Tasks:** 3/3 completed
- **Files modified:** 5

## Accomplishments

- `src/analyzers/timeline/av_sync.cpp`: `run_timeline_av_sync` now builds `make_timeline_packet_views(packet_scan, is_ts)` once (the MULTI-stream builder, since `av_offset`/`av_drift` compare first PTS ACROSS the primary video stream and each audio stream, and both must share the same cross-stream 2^33 epoch per 05-16's own rule). Every raw `video_stream.packets`/`audio_stream.packets` read (`detail::first_presented_pts` and both `detail::sorted_pts_with_span` calls) now reads `views[idx].packets()`. An overflowed video-stream view skips all three checks (`timeline.av_offset`, `timeline.av_drift`, `timeline.av_drift.pattern`) for EVERY compared audio stream; an overflowed audio-stream view skips only that stream's three checks -- both with `SkipReason::insufficient_data`, reason `video_view_overflowed`/`audio_view_overflowed`. `tb`, declared durations, priming and scopes are unchanged.
- `src/analyzers/timeline/jitter_vfr.cpp`: `run_timeline_jitter_vfr` builds `make_timeline_packet_views(packet_scan, is_ts)` (per-stream use is sufficient here -- no cross-stream comparison). The raw `stream_scan.packets` read feeding `derive_cadence` now reads `views[i].packets()`. An overflowed view skips both `timeline.jitter` and `timeline.vfr_profile` together, following the file's existing "both ids skip for the same reason" rule.
- **Verified on the wrap pair (`timeline_ts_nowrap.ts` vs `timeline_ts_wrap.ts`, `--profile remux`):** `timeline.av_offset`/`timeline.av_drift`/`timeline.av_drift.pattern` all `pass` (baseline/candidate `raw_offset_ms` -23/-24, `end_delta_ms` 10/11, `residual_max_ms` 42/42). `timeline.vfr_profile` `pass` on both streams; `timeline.jitter` `pass` on video, `skipped:vfr` on audio (a genuine per-stream cadence classification unrelated to the wrap -- audio's own interval distribution does not conform to its grid closely enough for CFR, the same class of pre-existing artifact `timeline.duration.coherence`'s audio-scope `info` finding documents, not a false positive).
- **Task 2's measured complete non-pass set** for the wrap pair, taken directly from the real binary AFTER both this plan's migrations, equals flagged assumption A1's own prediction exactly -- no extra and no missing member:

  | id | scope | status | cause |
  |---|---|---|---|
  | `timeline.start` | global | fail | D-03: the applied `-output_ts_offset 95440.34` genuinely shifts the absolute origin |
  | `timeline.wrap_events` | video | info | state semantic: the candidate genuinely wraps |
  | `timeline.wrap_events` | audio | info | state semantic: the candidate genuinely wraps |
  | `timeline.duration.coherence` | audio | info | pre-existing, both-sides-shared MPEG-TS audio-duration bookkeeping artifact (Test 5's own precedent) |

- `tests/integration/test_timeline_structure.cpp`'s wrap-pair `TEST_CASE` (Test 4) is renamed to `"timeline_structure - the wrap trigger pair declares its complete expected finding set under --profile remux, and timeline.wrap_events is the state-semantic non-pass case on both streams"` and now calls `expect_declared_set` with the four-member set above, each carrying its own causal comment. The prior "deliberately NOT a whole-report assertion" carve-out comment block is gone. The existing per-finding `wrap_events` baseline/candidate/count-2 assertions are kept unchanged.
- `docs/checks/timeline.av_offset.md` gained an "MPEG-TS wraparound" section documenting the epoch-aligned unwrap rule this check's reads now go through.
- `.planning/WINDOWS.md`: #26 (`start_duration.cpp`/`stream_params.cpp`/`size.cpp`), #27 (`jitter_vfr.cpp`) and #30 (`size.cpp`/`tol.cpp` interaction) marked `fixed` via `gsd-tools windows fixed`, only after both the full local suite (984/984) and the designated-leg suite (`MEDIADIFF_DESIGNATED_LEG=1`, 984/984, all five byte-exact goldens run -- not skipped -- and matched, `git diff --stat -- tests/golden/` empty) passed. A new entry (#31) records `av_sync.cpp`'s inclusion under the same root cause -- found by 05-VERIFICATION.md Gap 2, not previously filed -- and is itself marked `fixed`. #29 (the `tol.cpp` message-text-rendering defect) is untouched, still `open`. Ledger totals: `open_count: 13`, `waived_count: 1`, `fixed_count: 17`, `total_count: 31`.
- Full local suite: 984/984 passing throughout, same 6 pre-existing skips (`unit.console_vt` plus the five byte-exact goldens, which run only on the designated leg) at every checkpoint.

## Task Commits

Each task was committed atomically:

1. **Task 1: A/V offset and drift read the epoch-aligned views, proven end to end on the wrap pair** - `261fe46` (feat)
2. **Task 2: jitter/vfr_profile read the per-stream view, and the wrap pair becomes a whole-report assertion** - `5205f25` (feat)
3. **Task 3: Close WINDOWS #26/#27/#30, record av_sync.cpp's inclusion, and document the av_offset wrap rule** - `70b6bfe` (docs)

**Plan metadata:** commit to follow this SUMMARY (docs: complete plan)

## Files Created/Modified

- `src/analyzers/timeline/av_sync.cpp` - `run_timeline_av_sync` builds `views` via `make_timeline_packet_views` once; every raw packet read replaced; two-tier overflow-propagation (video-view overflow skips every compared audio stream, audio-view overflow skips only that stream)
- `src/analyzers/timeline/jitter_vfr.cpp` - `run_timeline_jitter_vfr` builds `views` via `make_timeline_packet_views`; `stream_packets` sourced from the view; overflow skips both ids together
- `tests/integration/test_timeline_structure.cpp` - Test 4 renamed and converted to `expect_declared_set` with the measured four-member set, each with a causal comment
- `docs/checks/timeline.av_offset.md` - new "MPEG-TS wraparound" section
- `.planning/WINDOWS.md` - #26/#27/#30 marked `fixed`; new entry #31 appended and marked `fixed`; #29 untouched

## Decisions Made

See `key-decisions` in frontmatter. Most significant: `av_sync.cpp` needs the multi-stream `TimelinePacketView` builder (cross-stream epoch alignment for a cross-stream comparison), while `jitter_vfr.cpp` only needs the per-stream builder (no cross-stream comparison) -- both correctly follow 05-16's own documented builder-choice rule, not an arbitrary pick.

## Deviations from Plan

None - plan executed exactly as written. The measured wrap-pair non-pass set matched flagged assumption A1 exactly on the first real-binary run; no wrap-caused member was found requiring investigation or a fix beyond the two files the plan already named.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Gap 2 (05-VERIFICATION.md SC2) is fully closed: `timeline.start`, `timeline.duration(.coherence)`, `video.frame_rate.measured`, `size.stream_bitrate`/`size.peak_bitrate` (05-16/05-17), `timeline.av_offset`/`timeline.av_drift(.pattern)` and `timeline.jitter`/`timeline.vfr_profile` (this plan) all read epoch-aligned, unwrapped MPEG-TS timestamps. No raw `StreamPacketScan::packets` read remains anywhere in `src/analyzers/timeline/`.
- WINDOWS.md #26/#27/#30 are closed; #31 (this plan's own new entry) is closed too. #29 (the `tol.cpp` message-text defect) remains open, untouched by this plan, out of scope.
- No blockers for remaining Phase 5 gap-closure plans (05-19 through 05-22, per WINDOWS.md #28's jitter/vfr_profile quantization work and the other open gaps in 05-VERIFICATION.md).

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

All 5 modified files verified present on disk with the expected content (`src/analyzers/timeline/av_sync.cpp`, `src/analyzers/timeline/jitter_vfr.cpp`, `tests/integration/test_timeline_structure.cpp`, `docs/checks/timeline.av_offset.md`, `.planning/WINDOWS.md`). All three task commit hashes (`261fe46`, `5205f25`, `70b6bfe`) verified present in `git log --oneline --all`. Full local suite 984/984 passing (same 6 pre-existing skips); designated-leg suite 984/984 passing with all five byte-exact goldens run and unchanged (`git diff --stat -- tests/golden/` empty). WINDOWS.md ledger verified on disk: #26/#27/#30 `fixed`, #29 `open`, new entry #31 (`av_sync.cpp`) `fixed`.
