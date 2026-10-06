# Phase 7 Check-ID Roster — APPROVED

**Approval decision:** APPROVED as proposed (option `approve-as-proposed`, no edits requested).
07-01-PLAN.md Task 1 carries `gate="blocking-human"`, so auto mode could not select an option; a
human answered the orchestrator's decision prompt.

**Approving reply (verbatim):** `Approve as proposed (Recommended)`
**Mapped option id:** `approve-as-proposed`
**Date approved:** 2026-09-30
**Channel:** orchestrator decision prompt

**Date drafted:** 2026-09-30
**Drafted by:** GSD executor (07-01-PLAN.md Task 1)

CLAUDE.md: "Check IDs are forever: additions fine, renames only via alias + deprecation." Once
approved, this file is the single source of truth for Phase 7 check-id spellings, skip-reason
spellings, the `score` unit, the `quality` report-group rule, the scope rule and the decoder flags
string. A plan that registers a Phase-7 id, skip reason or unit absent from this file, or registers
before approval, is a defect.

## Proposed roster (8 ids)

| id | group | semantic | unit | value_kind | severity | tolerance / profile overrides | plan |
|---|---|---|---|---|---|---|---|
| `content.video.frame_hash` | content | hash | none | hash_chain | fail | `[check.profile_severity]` hw_encoder=`info`, transform=`ignore` (doc 06 §2.1) | 07-01 |
| `content.video.frozen_runs` | content | span | ms | span_list | fail | — | 07-05 |
| `content.video.black_runs` | content | span | ms | span_list | fail | — | 07-05 |
| `video.closed_captions` | video | presence | none | string | fail | — (doc 03 row) | 07-06 |
| `content.video.perceptual` | content | tol | score | rational | info | `"0.015"`; profile_severity hw_encoder=`fail`, transform=`fail`, strict_bitexact=`ignore`, remux=`ignore` (doc 06 §2.2; sw_encoder keeps `info`) | 07-08 |
| `quality.psnr` | quality | tol | db | rational | fail | `"0.5dB"` | 07-10 |
| `quality.ssim` | quality | tol | score | rational | fail | `"0.005"` | 07-10 |
| `quality.vmaf` | quality | tol | score | rational | fail | `"0.5"` (doc 06 §3 `vmaf_drop: 0.5`) | 07-11 |

Vocabulary check: every semantic, value_kind, severity and unit token above is drawn from
`tools/gen_registry.py`'s `SEMANTICS`, `VALUE_KINDS`, `SEVERITIES` and `UNITS` sets, except `score`,
which is an additive unit (below). `db` is already in `UNITS`.

## Additive vocabulary the ids force (each a new enum member, never a rename)

- **Skip reasons** (land together in 07-04, in all six sites `hash_disabled` touches):
  - `not_requested` — a `quality.*` check whose flag was not given.
  - `sampling_conflict` — `quality.vmaf` under `--sample N` (CONTENT-09).
  - `path_incomparable` — a `tol` two-file check whose D-04 scaler or decode-path record differs
    between sides (`hash_incomparable` names a hash and would mislead).
  - `geometry_mismatch` — a two-file check whose sides' display dimensions, thumbnail geometry or
    plane layout cannot be paired.
- **Unit `score`** — a unitless score whose tolerance is written bare (`"0.015"`). Additive to
  `src/core/registry.h`'s `Unit`, `tools/gen_registry.py`'s `UNITS` and `src/core/tolerance.cpp`.
  Alternative: reuse `none`, whose tolerance must then be spelled `"0.015none"`. Lands with its
  first consumer in 07-08.
- **Report group** — `group_for()` maps a `quality.` first segment to `Group::content` (no new
  enumerator, no change to the report schema's closed `group` enum, no golden churn). Alternative:
  an additive `Group::quality` (schema enum plus every renderer plus goldens).

## Two-file scope and single-file rendering

`content.video.perceptual` and every `quality.*` id is measured on the primary video pair — the first
video stream on each side that is not `AV_DISPOSITION_ATTACHED_PIC` — and emitted at `Scope{video, 0}`
on BOTH fingerprints, with each side's real stream index in evidence (`baseline_stream_index`,
`candidate_stream_index`). Any one-sided probe (`snapshot`, `inspect`, `dir` without `--content`, or
the media side of a media-vs-snapshot compare) emits the four ids as `skipped:requires_media` (or
`skipped:requires_decode` under `--no-content`), so `inspect` renders them as explicit skip rows and
SNAP-06 stays clean. A file with no video stream emits none of them, matching the `video.*` family.

## Research findings recorded here (each amends a doc in the open, in the plan named)

1. **Threading narrowed** (Open Question 1; CONTEXT Claude's discretion said "automatic"). Corrupt
   streams decode non-deterministically at >1 thread even at a fixed count (10/10 distinct chains for
   MPEG-4/H.264 frame threads), so EVERY production video decode runs `thread_count = 1`, recorded as
   `threads=1` in the flags string. Measured cost: still 22.6x realtime on the D-16 reference (PERF-02
   needs 4x). `ProbeOptions::video_decode_threads` stays a test-only control for TRUST-07's 1/4/16
   suite. Consistent with D-11; amends doc 06 §1 (07-12). **This narrows a user-accepted default and is
   the reason this gate is `blocking-human`.**
2. **Pinned decoder settings.** `AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_UNALIGNED`,
   `idct_algo = FF_IDCT_SIMPLE`, packet `AV_PKT_FLAG_DISCARD` cleared before send (D-06), recorded
   verbatim as `bitexact+unaligned;idct=simple;threads=1`.
3. **Error-bearing streams are class 2** (Open Question 3, extends D-09). A stream with any decode
   error or corrupt-flagged frame records `class2 <signature>` even for a proven decoder, because
   measured cross-arch output differs on corrupt input (07-02).
4. **Frozen detector uses SSIM hysteresis on the 128-wide thumbnail for every class** (enter > 0.9995,
   continue > 0.995, minimum 3 frames); exact-hash equality measured GOP-unstable. Black: 8-bit
   thumbnail mean <= black point + 2 (limited 16, full 0) and variance < 4. Constants validated on
   synthetic content only; a real-content review is filed as a follow-up (07-05).
5. **Perceptual does not normalize colour range** (Open Question 2): a range flip is a real difference
   `video.color.range` already reports.
6. **PSNR** gates on the sample-count-weighted combined Y+U+V MSE (doc 06 "luma+chroma"), capped at
   `(6 * bpc) + 12` dB (libvmaf's convention) so identical frames score the cap and the baseline
   self-score is that cap; per-plane values ride in evidence (Open Question 7).
7. **VMAF self-score is computed, never assumed 100** (measured 97.43 on a one-frame identical clip).
   The vcpkg `libvmaf` port is `!windows`: `MEDIADIFF_WITH_VMAF=ON` is refused on Windows at configure
   time and `--vmaf` there is the same exit-64 usage error (Open Question 6, 07-11).
8. **DOC-03 for opt-in checks.** `CoveragePair` gains `extra_args` (`--psnr`, `--ssim`, `--vmaf`).
   `quality.vmaf` is a build-conditional pair: in a default build the gate asserts its full
   default-build contract (every compare reports `skipped:not_requested`; `--vmaf` exits 64 naming
   `MEDIADIFF_WITH_VMAF`), and the designated-leg VMAF build runs its real trigger/clean pair.
9. **Watchdog stall limit = 300 s** (`kDecodeWatchdogStallSeconds`; D-12 leaves the value to the
   planner): the whole audio and timeline ratchets run under cachegrind in under two minutes, so no
   single legitimate libav call approaches it (07-13).
10. **HDR first-frame fixture** is the hand-written H.264 I_PCM SEI stream; an AV1 variant stays
    optional (Open Question 8, 07-07).

## Options presented to the human

- `approve-as-proposed` — the roster, additive vocabulary, scope rule and ten findings exactly as
  listed.
- `approve-with-edits` followed by the exact changes (spelling, severity, tolerance, unit, group or
  scope).
- `quality-own-group` — as proposed, but `quality.*` gets its own additive `Group::quality` (changes
  the report schema's closed `group` enum, every renderer's group switch, and every renderer golden).

## Status

**APPROVED (2026-09-30, `approve-as-proposed`).** This file is the single source of truth for Phase 7
check-id, skip-reason and unit spellings, the `quality` report-group rule, the two-file scope rule and
the decoder flags string. Registration of the listed ids, skip reasons and unit may proceed in the
plans named in the roster table; anything not listed here requires a new approved amendment.
