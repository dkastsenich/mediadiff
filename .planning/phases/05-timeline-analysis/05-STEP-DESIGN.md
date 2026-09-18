# 05-21: Piecewise checkpoint mapping research for `timeline.av_drift.pattern == "step"` (SC1 Gap 1)

Scratch research under `.planning/phases/05-timeline-analysis/05-step-research/` (`harness.py`, `recipes.py`). No product file (`src/`, `tests/`, `scripts/`) was read for anything other than input, and none was modified. All arithmetic in the harness uses Python's arbitrary-precision `int` and `fractions.Fraction`; no `float` appears in any mapping or fit (`grep -c "float(" harness.py` reports 0).

## Calibration

`python3 .planning/phases/05-timeline-analysis/05-step-research/harness.py calibrate` re-derives `timeline.av_drift`'s trajectory (`k`, `t_v_ms`, `offset_ms`), `end_delta_ms`, `residual_max_ms`, `step_time_ms` and `timeline.av_drift.pattern` from raw packet data (via `ffprobe`, cross-checked against the pinned `.ffmpeg-pinned/linux-x86_64/ffmpeg` where needed) and asserts exact equality against the real `mediadiff snapshot` evidence, per checkpoint, per field. All 8 calibration fixtures pass:

```
PASS: timeline_start_base.mp4
PASS: timeline_start_base_copy.mp4
PASS: timeline_avoffset_video_shift.mp4
PASS: timeline_drift_base.mp4
PASS: timeline_drift_linear.mp4
PASS: timeline_drift_step.mp4
PASS: timeline_start_shift.ts
PASS: timeline_ntsc_remux.mkv

8/8 PASS
```

No fixture required substituting pinned-ffmpeg packet data in place of system `ffprobe`; the two agreed on every packet field this task reads (`pts`, `dts`, `duration`, `time_base`, `sample_rate`, `Skip Samples` side data) for all 8 fixtures.

This exact-equality bar (T-05-86) is what every candidate design below is judged against: `compute_candidate_drift(design="D0", span_source="declared")` is re-derived independently from `compute_shipped_drift` (not called through it) so it can also run under `span:observed`, and it reproduces `compute_shipped_drift`'s own output bit for bit wherever the two overlap.

## Candidate designs

Both candidates detect timestamp discontinuities on the **audio** stream only (never requiring an independently-detected video discontinuity — video is the smooth reference clock in every fixture this panel's step recipes build; requiring a matching video-side split was tried first and abandoned, because it guarantees a skip on exactly the fixtures this research exists to classify). The discontinuity threshold reuses the shipped `kNominalDurationCapMultiplier` (= 2) against the stream's own nominal (median) packet duration — no new configurable constant is introduced (D-08).

A file with **no** detected audio discontinuity is a single segment (the whole file); on this path both D1 and D2 delegate verbatim to `_checkpoints_whole_file`, D0's own exact whole-file map, so they are bit-identical to D0 there — not merely "close", identical trajectories by construction. This matters directly for soundness criterion (a) below, and for the flagged-assumption A2 span-source comparison, which needs a design/span combination that never touches the fixed nominal-rate segment math on files with no genuine splice.

When 2+ segments **are** detected, video's own corresponding segment boundaries are derived by rescaling each segment's own real audio content span (`capped_segment_end(...) - start`, capped at 1x nominal packet width, not the shipped 2x containment cap — see `capped_segment_end`'s own doc comment for why 2x leaks part of the gap into the segment) through the two streams' **nominal** timebase ratio (`rescale_ticks`, e.g. 44100/12800 on this panel's own fixtures) and stacking sequentially from `video_start_ticks`. This is self-consistent by construction: it is the exact same nominal-rate conversion the per-checkpoint loop itself uses, so a segment's own checkpoints never run past that segment's own real content except at one genuinely unavoidable edge case (below).

- **D1 (segment-proportional):** reports the raw, absolute offset per checkpoint, including whatever the join itself contributes. A genuine mid-file sync step shows as two different flat plateaus, the difference between them equal to the join's own size.
- **D2 (media-clock):** the *same* segmentation and per-segment mapping as D1, but each checkpoint's own offset has the cumulative detected gap duration (converted to ms, audio timebase), accumulated up to that checkpoint's own segment, subtracted before reporting. For a clean splice with no other desync, D2's plateaus land at the *same* level on both sides of the join (the jump is fully explained away); D1's plateaus differ by the join's own size.

Every division in both designs truncates toward zero (`trunc_div`, mirroring `detail::checked_div`); every target tick is resolved through the unchanged shipped `nearest_tick` / `clamp_into_nearest_packet` / `index_proportional_raw_ticks` machinery, reused verbatim, never reimplemented.

### Soundness criteria verdicts (against the full panel, both span sources, unless noted)

| Criterion | D1 | D2 |
|---|---|---|
| (a) bit-identical trajectories on every discontinuity-free panel file | **MET** — every no-regression pair and every false-positive-guard file is bit-identical to D0 (same pattern, rate, end_delta_ms, residual_max_ms, step_time_ms), under both `span:declared` and `span:observed` | **MET** — same evidence; D2's gap-subtraction is 0 on a single segment, so it collapses to D1's (= D0's) output exactly |
| (b) constant-offset and linear-drift classifications unchanged | **MET** — `timeline_drift_linear.mp4`, `timeline_drift_base.mp4`, `timeline_ntsc_remux.mkv`, `timeline_start_base.mp4` and its no-regression partners all match D0 exactly | **MET** — same evidence |
| (c) `step` with a `step_time` within one checkpoint spacing of the join on V3 and V4 | **NOT MET** — D1 never reaches `pattern="step"` on V3, V4 or V2 (stays `irregular`); see Panel results | **NOT MET** — D2 *does* reach `pattern="step"` on V3, V4 and V2, but at `step_time_ms=3960` in every case, regardless of where the real join is (V3's real join is at `t_v_ms≈2320`; V4's is at `t_v_ms≈1160` — a different position by design). D2's reported step time tracks a boundary artifact (below), not the real join; see Panel results |
| (d) no new non-pass finding on the false-positive guards | **MET** — `timeline_ts_nowrap.ts`/`timeline_ts_jump.ts`/`timeline_ts_jump_flagged.ts`/`timeline_start_shift.ts`/`timeline_avoffset_unknown.ts` all match D0 exactly, both span sources | **MET** — same evidence |

### The terminal-checkpoint boundary artifact behind criterion (c)'s failure

Both V2/V3 (`timeline_drift_step.mp4`'s own construction: a 3.9 s recording with `PTS`/`DTS` shifted `+4410` ticks, i.e. +100 ms, from audio packet 95 on — `scripts/gen_corpus.sh`) and V4 (the same technique, joined at packet 48 instead) keep the **declared** audio and video durations equal at the container level — this is exactly the property 05-10-SUMMARY.md's own construction relies on to avoid smearing D0's whole-file map into `linear-drift`. That property has a cost: the post-splice segment's own *real* audio content is genuinely ~100 ms *shorter*, in video-tick-equivalents, than the video runway remaining to the file's declared end (the +100 ms timestamp shift is what makes the *declared* duration land back at 4.0 s; it adds no real recorded audio). The K=32 checkpoint grid places `k=31` (`kDriftCheckpointCount-1`) at exactly `video_span_ticks` — the file's absolute declared end — independent of segmentation. That checkpoint's target therefore always falls past the last segment's own real audio content; `clamp_into_nearest_packet` correctly clamps it to the segment's real last packet, producing a value well outside the plateau (D1: `+63 ms` against a `0 ms` plateau on `timeline_drift_step.mp4`/V2/V3; D2: `-37 ms` against a `0 ms` plateau on all four step fixtures). Confirmed with both V3 (join at packet 95, `t_v_ms≈2320`) and V4 (join at packet 48, `t_v_ms≈1160`): the deviating checkpoint is `k=31` in both, an artifact of the recipe's own "declared spans match" construction, not of where the splice itself sits.

Under D1, this single point keeps `residual_max_ms` at 49–62 (all of it attributable to `k=31`; every other checkpoint sits exactly on its own plateau), which is enough to keep `fit_drift`'s classification at `irregular` rather than `step`.

Under D2, the gap-subtraction step correctly cancels the *real* join (both plateaus land at `0 ms`, confirmed per-checkpoint above), which paradoxically lets `fit_drift`'s own pairwise least-squares step search find a *different*, spurious split — between `k=30` (`0 ms`) and the lone outlier `k=31` — and classify **that** as the step, because a single-point group is trivially "flat" against its own mean. This is not a fluke of the shipped `fit_drift`: it is the direct, mechanical consequence of feeding it a trajectory whose only irregularity is a lone boundary point. **D2 reports `pattern="step"` at a `step_time_ms` that is provably unrelated to the real join** — proven directly by V3 and V4 reporting the identical `step_time_ms=3960` despite their real joins sitting roughly 1160 ms apart in `t_v_ms`.

This was found and fixed as two implementation bugs during this task, not treated as acceptable noise:
1. A first cut of the single-segment fallback used the segment-core's own fixed nominal-timebase ratio unconditionally, which cannot represent genuine whole-file span-based drift (`audio_span_ticks != video_span_ticks`, i.e. a real clock-rate mismatch) — it silently misclassified the calibrated `timeline_drift_linear.mp4` fixture as `constant-offset`. Fixed by delegating the true single-segment case to D0's own exact whole-file map.
2. D2's own gap-subtraction computed the boundary between segments using the raw, uncapped packet `duration` field, which libavformat itself fills as "interval to next packet" for the packet immediately before a real gap — exactly the same container quirk `detect_discontinuities`'s own doc comment names. This made the computed gap exactly 0 regardless of the true join size, so D2 silently degenerated to being identical to D1 until fixed (using `capped_segment_end` for the segment boundary, matching the fix already applied to segmentation itself).

Both fixes are Rule-1 auto-fixes (bugs against the design's own stated intent) applied during this task; the panel results below reflect the corrected code.

## Panel results

Every design/span-source combination reproduces D0 exactly wherever D0 itself would classify the file as `constant-offset` or `linear-drift` (no discontinuity is ever detected on those files). The table below shows only the rows where a design's output *could* differ from D0 — the step-recipe variants and the terminal-checkpoint effect — plus one representative no-regression row and one false-positive-guard row as evidence that nothing else moved.

| file | design | span | pattern | end_delta_ms | residual_max_ms | step_time_ms |
|---|---|---|---|---|---|---|
| `timeline_start_base.mp4` (representative no-regression file) | D0 / D1 / D2 | declared | constant-offset | 0 | 0 | — |
| `timeline_start_base.mp4` | D0 / D1 / D2 | observed | linear-drift | 22 | 0 | — |
| `timeline_drift_linear.mp4` (calibrated linear-drift fixture) | D0 / D1 / D2 | declared | linear-drift | -20 | 0 | — |
| `timeline_v1_seamless.mp4` (V1, no timestamp discontinuity) | D0 / D1 / D2 | declared | linear-drift | -100 | 0 | — |
| `timeline_v1_seamless.mp4` | D0 / D1 / D2 | observed | linear-drift | -77 | 0 | — |
| `timeline_drift_step.mp4` (= V3's construction) | D0 | declared | irregular | 0 | 60 | — |
| `timeline_drift_step.mp4` | D1 | declared | **irregular** | 63 | 49 | — |
| `timeline_drift_step.mp4` | D2 | declared | **step** | -37 | 32 | **3960** (wrong — real join ≈2320) |
| `timeline_v2_gap_trim.mp4` (byte-identical to V3) | D1 / D2 | declared | irregular / step | 63 / -37 | 49 / 32 | — / 3960 |
| `timeline_v3_content_jump.mp4` | D1 | declared | **irregular** | 63 | 49 | — |
| `timeline_v3_content_jump.mp4` | D2 | declared | **step** | -37 | 32 | **3960** |
| `timeline_v4_content_jump_alt.mp4` (join at packet 48, real join ≈1160 ms) | D0 | declared | irregular | 0 | 55 | — |
| `timeline_v4_content_jump_alt.mp4` | D1 | declared | **irregular** | 63 | 62 | — |
| `timeline_v4_content_jump_alt.mp4` | D2 | declared | **step** | -37 | 32 | **3960** (wrong — real join ≈1160) |
| `timeline_ts_jump.ts` (false-positive guard) | D0 / D1 / D2 | declared | irregular | 36 | 720 | — |
| `timeline_ts_jump_flagged.ts` (false-positive guard) | D0 / D1 / D2 | declared | irregular | 36 | 720 | — |
| `timeline_start_shift.ts` (false-positive guard, MP4-to-TS) | D0 / D1 / D2 | declared | irregular | 10 | 42 | — |
| `timeline_start_shift.ts` | D0 / D1 / D2 | observed | linear-drift | 39 | 0 | — |
| `timeline_avoffset_unknown.ts` (false-positive guard, MP4-to-TS) | D0 / D1 / D2 | declared | irregular | 10 | 42 | — |
| `timeline_ntsc_remux.mkv` (false-positive guard) | D0 / D1 / D2 | declared / observed | linear-drift | 18 | 0 | — |

`timeline_ntsc_base.mp4` reports `skip` under every design and span source, identically to D0 (a pre-existing property of that fixture's own stream layout, not a regression introduced here).

Full raw table: `python3 .planning/phases/05-timeline-analysis/05-step-research/harness.py evaluate` (reproduces every row above plus every duplicate baseline-file row the panel's pair list re-visits).

**TIME-07 / boundary.** The discontinuity-detection threshold this research introduces is `durations[i] > nominal_duration_ticks * kNominalDurationCapMultiplier` (strictly greater, matching the shipped step-threshold's own "strictly greater" convention), reusing the existing shipped constant — no new configurable value. Pinned one tick either side on `timeline_drift_step.mp4`'s own audio stream (`nominal_duration_ticks=1024`, threshold `=2048`): a packet duration of exactly `2048` ticks does not trigger a split; `2049` does. The real split-triggering packet (`index 94`) has `duration=5434` ticks — 3386 ticks past the threshold, an unambiguous fire. The shipped plateau-flatness bound (2 ms of each side's own mean) and step-residual threshold (`kDriftStepResidualMultiple * kDriftEpsilonMs = 6 ms`) are both **unchanged** — neither design modifies `fit_drift`; both feed it the same unmodified function the shipped code calls, so the existing calibration proof already pins those two boundaries.

**TIME-07 / precision.** Both designs introduce exactly one new division shape beyond D0's own (`audio_delta_ticks = trunc_div(delta_from_video_start_ticks * audio_span_ticks, video_span_ticks)`, unchanged, still used on the single-segment path): `rescale_ticks` (`trunc_div(ticks * from_num * to_den, from_den * to_num)`, a single multiply-then-single-divide, never two chained divisions) for the segment-boundary stacking and the within-segment target mapping. Every product in `rescale_ticks` is bounded by `ticks * from_num * to_den`; on every panel fixture's own timebases (44100, 12800, 90000, 25, ...) this stays far inside a 64-bit product before this task's own use of Python's unbounded `int`, and would fit `Int128Accum`/`ExactInt` in the shipped implementation's own 128-bit intermediate arithmetic. Rounding is truncation toward zero throughout (`trunc_div`), the same convention as every existing shipped division; ties are never broken toward "away from zero" anywhere in either design (that behavior is reserved for `nearest_tick`, reused unmodified). The harness's own `Fraction`-based measurement (this file) never introduces rounding at all in the comparisons above — the rounding error the integer implementation would incur is exactly the `trunc_div` behavior documented above, already exercised in Task 1's exact-equality calibration.

## Ambiguity analysis

A1 (flagged assumption): a 100 ms audio timestamp gap is produced identically by two edits — trimming 100 ms of content while keeping the original timestamps (sync preserved, a dropout) and shifting timestamps by 100 ms over contiguous content (sync stepped). `recipes.py`'s `build_v2_gap_trim` and `build_v3_content_jump` builders demonstrate this is not merely an algebraic claim: `build_v2_gap_trim` deliberately reuses `timeline_drift_step.mp4` verbatim (the same file `build_v3_content_jump` also copies) rather than constructing an independent "trim" fixture, because no ffmpeg-level construction of "trim 100 ms, keep timestamps" is distinguishable, at the packet level, from "shift timestamps 100 ms over contiguous content" — the two edits produce **byte-identical files**. Verified this task by SHA-256: `timeline_v2_gap_trim.mp4` and `timeline_v3_content_jump.mp4` hash to `58b481352221fd591284133407f8b1fe4ac2e0894c5935763ed310664c05ce96`, identically.

Both D1 and D2 assume the second reading (sync stepped, contiguous content) — this is a property of the *mapping's own philosophy* (project real elapsed time proportionally within each detected segment), not a choice either design makes explicitly per file; there is no signal in the packet data itself that could distinguish the two readings (that is exactly what byte-identical V2/V3 proves). A design built on this philosophy reports the same verdict for a genuine dropout-style trim as it does for a genuine timestamp step: whatever `timeline_drift_step.mp4` (V2 = V3) reports above (`irregular` under D1, `step` at the wrong `step_time_ms` under D2) is reported identically for the dropout-style reading of the same bytes, because they are the same bytes.

## Seamless re-timestamped trim

V1 (`timeline_v1_seamless.mp4`, built via `atrim` + `asetpts=PTS-STARTPTS` + `concat` filter, verified via `ffprobe` during construction to genuinely drop 100 ms of `duration_ts` with no resulting timestamp discontinuity) triggers **no** audio discontinuity under `detect_discontinuities` in either D1 or D2; both delegate to D0's own single-segment path and reproduce D0's own reading exactly: `linear-drift`, `end_delta_ms=-100` (declared) / `-77` (observed), `residual_max_ms=0` — the trim is smeared across the whole file's own affine map, never localized, never flagged as anomalous.

This confirms the plan's flagged limitation directly: **a seamlessly re-timestamped trim is undetectable from timestamps alone** under any checkpoint-mapping design evaluated in this research, because the packet timeline it produces carries no discontinuity signal to detect — the missing content is invisible to a design (D0, D1 or D2 alike) that only ever looks at packet timestamps. Distinguishing it from a genuine linear clock-rate mismatch (`timeline_drift_linear.mp4`, which produces the identical `pattern="linear-drift"` reading) requires decoding audio content itself, which is out of scope until Phase 6's audio decode path exists.

## Residual MP4-to-TS drift

A2 (flagged assumption): 05-14 Task 2 found the MP4-to-TS pairs' `av_drift` failure traces to libavformat's *estimated* TS audio stream duration being used as the checkpoint span, not to priming. This research reproduces that finding directly and tests the `span:observed` alternative (first-to-last packet extent) named in the plan.

On the MP4-to-TS false-positive-guard files (`timeline_start_shift.ts`, `timeline_avoffset_unknown.ts`), `span:declared` reproduces the known-bad reading: `pattern="irregular"`, `residual_max_ms=42`. `span:observed` on the *same* files is materially cleaner: `pattern="linear-drift"`, `residual_max_ms=0`. This holds identically for D0, D1 and D2 (none of these files triggers a detected discontinuity, so all three designs take the same single-segment path).

However, `span:observed` is **not recommendable as a default** by the plan's own bar ("state whether `span:observed` changes any verdict on a discontinuity-free MP4/MKV panel file; it must not, to be recommendable"): on `timeline_start_base.mp4` — a discontinuity-free MP4 file, part of the no-regression panel, with no known sync problem — `span:observed` changes the verdict from `constant-offset` (`end_delta_ms=0`, the correct reading) to `linear-drift` (`end_delta_ms=22`, `residual_max_ms=0`), for every design (D0, D1, D2 alike). The same shift reproduces on `timeline_start_base_copy.mp4`, `timeline_avoffset_video_shift.mp4`, `timeline_drift_base.mp4` and `timeline_v_seamless_source.mp4` — every plain, healthy MP4 no-regression file in the panel. `timeline_ntsc_remux.mkv`, the panel's one discontinuity-free MP4/MKV false-positive guard *outside* the MP4-to-TS pairs, is the sole exception where `span:observed` does **not** change the verdict (`linear-drift`, `18 ms`, `residual_max_ms=0` under both span sources) — but that single agreement does not offset the systematic new false `linear-drift` finding `span:observed` introduces across the ordinary MP4 no-regression panel.

Switching the default span source to `observed` would trade a known MP4-to-TS false-positive-style finding for a new, broader false-positive-style finding on ordinary, healthy same-container files — unacceptable given this project's own P0 stance on false positives (PROJECT.md: "a diff tool that cries wolf gets muted, and a muted gate is worth nothing"). No candidate design changes this trade-off, because it is a property of the span source itself, not of the checkpoint mapping.

## Recommendation

```
narrow-vocabulary
span:declared
```

Neither D1 nor D2 meets all four soundness criteria (a)-(d) on the full panel:

- Both meet (a), (b) and (d) cleanly — bit-identical to D0 on every discontinuity-free file in the entire panel (no-regression pairs, false-positive guards, and V1's genuinely seamless splice), and zero new non-pass finding on any false-positive guard, under both span sources.
- Neither meets (c). D1 never reaches `pattern="step"` on V2, V3 or V4 — a real, well-localized two-plateau shape is present for 30 of 32 checkpoints on every step fixture (offsets exactly `0 ms` then exactly `100 ms`, split precisely at each fixture's own real join), but the shipped `fit_drift`'s strict all-points-within-`kDriftEpsilonMs` flatness gate is broken by exactly one checkpoint (`k=31`, the file's own absolute terminal checkpoint) — an architectural consequence of the step-recipe construction itself (declared audio/video spans kept equal, so the post-splice segment's real content is inherently ~100 ms short of the file's own declared end), not a flaw specific to where the splice sits (confirmed identically on V3, join at `t_v_ms≈2320`, and V4, join at `t_v_ms≈1160`). D2 *does* reach `pattern="step"` on V2, V3 and V4, but at a `step_time_ms` that is demonstrably unrelated to the real join — V3 and V4 report the identical `step_time_ms=3960` despite their real joins sitting roughly 1160 ms apart, because D2's step search finds the *same* terminal-checkpoint artifact, not the real splice. Adopting D2 as specified would ship a design that reports `step` with a systematically wrong location.

`span:observed` is not recommended as a default for the reason in "Residual MP4-to-TS drift" above: it introduces a new false `linear-drift` finding (previously a correct `constant-offset`) on every plain, healthy, discontinuity-free MP4 file in the no-regression panel, in exchange for cleaning up the (narrower) MP4-to-TS false-positive case. `span:declared` — today's shipped behavior — is recommended unchanged.

Because the recommendation is `narrow-vocabulary`, no `## Implementation spec` section follows (per this plan's own instruction: implementation spec is written only when recommending adopt).

## Decision

PENDING — awaiting blocking-human checkpoint (05-21 Task 3)
