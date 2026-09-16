# Phase 5 Check-ID Roster — Approved

**Approval decision:** `approve-as-proposed` — all 16 ids exactly as drafted in 05-01-PLAN.md
Task 1, including the five one-semantic-per-id additions beyond doc 04's eleven table rows and
the two tolerance-spelling deviations recorded below.

**Approved by:** GSD executor, auto-mode (`workflow.auto_advance=true`, `mode=yolo`), 05-01-PLAN.md
Task 1's `checkpoint:decision` gate carries `gate="blocking"` (not `blocking-human`), so per
`checkpoints.md`'s auto-mode checkpoint behavior the first listed option
(`approve-as-proposed`) was auto-selected. No edits were requested.

**Approved on:** 2026-09-16

**Date drafted:** 2026-09-16
**Drafted by:** GSD executor (05-01-PLAN.md Task 1)

CLAUDE.md: "Check IDs are forever: additions fine, renames only via alias + deprecation." This
file is the single source of truth for Phase 5 check-id spellings once approved — a plan that
registers an id absent from this file (or registers before approval) is a defect. 16 ids
proposed, all in the `timeline` group.

## Approved roster (16 ids)

| id | group | semantic | unit | value_kind | severity | tolerance | plan | notes |
|----|-------|----------|------|------------|----------|-----------|------|-------|
| `timeline.start` | timeline | tol | ms | rational | fail | `"5ms,20ms"` | 05-01 | `[check.profile_tolerance]` strict_bitexact/remux = `"1ms"`. D-03: one `Scope::Kind::global` measurement (earliest presentation time) plus one per timestamped stream (relative to origin). |
| `timeline.duration` | timeline | tol | ms | rational | fail | `"20ms,40ms"` | 05-04 | **Deviation from doc 04's literal "±1 frame".** A frame-derived threshold is frame-rate-dependent (D-08 rules out data-dependent thresholds); 40 ms is one frame at 25 fps and is a fixed named constant. |
| `timeline.duration.coherence` | timeline | state | none | string | info | — | 05-04 | **Addition.** Split from doc 04's `timeline.duration` row, which declares both a pairwise cross-file tolerance AND an internal triple-disagreement `info` note. `flagged_values` TBD by 05-04 (mirrors `video.hdr.coherence`'s shape, Phase 4 D-10 precedent). |
| `timeline.dts_monotonic` | timeline | tol | count | int64 | fail | `"0"` | 05-05 | **Deviation from doc 04's literal "count exact 0".** Zero-magnitude tolerance, not `exact`, so `--tol timeline.dts_monotonic=2` stays meaningful (mirrors Phase 4's `video.frame_count` "0frames" precedent). |
| `timeline.pts_unique` | timeline | tol | count | int64 | fail | `"0"` | 05-05 | Same zero-magnitude-tolerance reasoning as `timeline.dts_monotonic`. |
| `timeline.gaps` | timeline | span | ms | span_list | fail | — | 05-06 | Scope: every stream carrying timestamps, EXCEPT `Scope::Kind::subtitle` (05-RESEARCH.md Open Question 3 — sparse subtitle PTS would false-positive under a 2x-nominal rule tuned for audio/video). |
| `timeline.wrap_events` | timeline | state | none | string | info | — | 05-06 | **Addition.** `flagged_values = ["ts_33bit_wrap"]`. Doc 04 §1.2: wrap events are recorded `info`; same shared-condition-no-delta reasoning as `video.hdr.coherence`. |
| `timeline.discontinuities` | timeline | span | ms | span_list | fail | — | 05-07 | Unflagged (gating) TS discontinuities plus non-TS discontinuities. Same stream scope as `timeline.gaps`. |
| `timeline.discontinuities.flagged` | timeline | span | ms | span_list | info | — | 05-07 | **Addition.** Split from doc 04's `timeline.discontinuities` row: TS packets under `discontinuity_indicator=1` are *flagged* structure (`info`), unflagged is gating. Mirrors Phase 3's `container.ts.cc_errors`/`container.ts.cc_discontinuities` split. |
| `timeline.jitter` | timeline | tol | ms | rational | fail | `"0.5ms,2ms"` | 05-08 | **Discretion: ONE id, not two.** σ is the compared value; max\|dev\| rides in evidence — two ids would double-report a single jittery stream (D-01/D-02 noise concern). `skipped:vfr` when the stream is not CFR. |
| `timeline.vfr_profile` | timeline | dist | percent | histogram | warn | `"2%"` | 05-08 | D-06: bins keyed on deviation from the stream's own grid (on-grid, one tick, one percent, 2x, 3x, longer), not raw ticks. |
| `timeline.av_offset` | timeline | tol | ms | rational | fail | `"5ms,20ms"` | 05-09 | Priming-adjusted per D-09/D-10/D-11. Primary-stream selection: first video stream that is not an attached picture, one measurement per audio stream; no audio or no video → `skipped:insufficient_data`. |
| `timeline.av_drift` | timeline | tol | ms_per_min | rational | fail | `"0.2ms/min"` | 05-10 | D-04: rate only. Gates on rate AND end-delta clearing the 2 ms epsilon (D-07). K=32 trajectory stored in evidence (TIME-08). |
| `timeline.av_drift.pattern` | timeline | exact | none | string | fail | — | 05-10 | **D-04, locked, one-way.** Pattern class exactly: `constant-offset` / `linear-drift` / `step` / `irregular`. |
| `timeline.timecode` | timeline | presence | none | string | info | — | 05-11 | `tmcd` reachable from `Pass::demux_header` (no decode). S12M and MPEG-2 GOP timecode report `skipped:requires_decode` — no S12M-specific id (05-RESEARCH.md Pitfall 6: `AV_PKT_DATA_S12M_TIMECODE` has no file-demuxer producer in the linked FFmpeg 8.1, so a separately-triggerable id could never have a DOC-03 trigger fixture). |
| `timeline.timecode.value` | timeline | exact | none | string | info | — | 05-11 | **Addition.** Split from doc 04's `timeline.timecode` row (presence + start value under one `presence` semantic). Mirrors Phase 4's `video.hdr.mdcv`/`video.hdr.mdcv.luminance` split. Compares the rendered SMPTE string exactly, drop-frame punctuation included. |

## Additions beyond doc 04's literal table-row count (5 ids)

Every one is forced by the same one-semantic-per-id constraint Phases 3 and 4 already ruled on:

1. `timeline.duration.coherence` — split from `timeline.duration` (pairwise tolerance vs. internal
   triple-disagreement `info` note).
2. `timeline.wrap_events` — doc 04 §1.2's TS wrap events, visible when both sides wrap, no delta to
   hang on another check.
3. `timeline.discontinuities.flagged` — split from `timeline.discontinuities` (flagged `info`
   structure vs. unflagged gating spans), mirroring Phase 3's
   `container.ts.cc_errors`/`container.ts.cc_discontinuities` split.
4. `timeline.timecode.value` — split from `timeline.timecode` (presence vs. exact rendered value),
   mirroring Phase 4's `video.hdr.mdcv`/`video.hdr.mdcv.luminance` split.
5. `timeline.av_drift.pattern` — **D-04, locked and one-way.** Listed here for spelling
   confirmation only, not for re-litigation.

## Tolerance spellings that differ from doc 04's literal wording

- **`timeline.duration` is `"20ms,40ms"`, not "±1 frame".** A frame-derived threshold is
  frame-rate-dependent, gating the same delta differently on two files that differ only in rate —
  ruled out by D-08's data-dependent-threshold prohibition. 40 ms is exactly one frame at 25 fps
  and is a fixed, documented constant.
- **`timeline.dts_monotonic`/`timeline.pts_unique` are `tol`/`count` with tolerance `"0"`, not
  `exact`.** A zero-magnitude absolute tolerance behaves identically to exact equality while
  leaving `--tol timeline.dts_monotonic=2` meaningful — the same reasoning Phase 4 recorded for
  `video.frame_count`'s `"0frames"`.

## Discretion items decided in this roster

- **`timeline.jitter` is ONE id, not two.** σ is the compared value; max\|dev\| rides in evidence.
- **Primary-stream selection** for `av_offset`/`av_drift`: first video stream that is not an
  attached picture, one measurement per audio stream. No audio or no video produces an explicit
  `skipped:insufficient_data`.
- **`timeline.gaps`/`timeline.discontinuities` stream scope**: every stream carrying timestamps,
  EXCEPT `Scope::Kind::subtitle`, recorded explicitly in `docs/checks/timeline.gaps.md` rather than
  left implicit.
- **No S12M-specific id.** Verified against the linked FFmpeg 8.1 that `AV_PKT_DATA_S12M_TIMECODE`
  is set only by `libavdevice/decklink_dec.cpp`, which this build does not link. `timeline.timecode`
  covers `tmcd` and reports `skipped:requires_decode` for the other two sources.

## Status

**APPROVED.** This file is the single source of truth for Phase 5 check-id spellings and
attributes. A plan that registers a `timeline.*` id absent from this file, or with different
attributes, is a defect. Later phases may add ids; they may not rename these.
