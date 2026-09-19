# Phase 5: Timeline Analysis - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-16
**Phase:** 5-Timeline Analysis
**Areas offered:** 4 · **Areas selected:** 4 (all)
**Areas discussed:** Finding ownership, Coarse-timebase cadence, Priming contract, Perf gate design

---

## Finding ownership

### Q1 — What should DOC-04's "nothing else" count?

| Option | Description | Selected |
|--------|-------------|----------|
| Whole report, info included | The `test_video_yuvj.cpp` counter: every finding that is not pass or skipped, across all families; noise is fixed by narrowing the fixture | ✓ |
| Whole report, gating only | Count warn/fail/error but let info notes (duration incoherence, TS wrap) ride along undeclared | |
| `timeline.*` findings only | Ignore other families — the filtered count the yuvj test explicitly rejects | |

### Q2 — One cause moves several real facts. How should the assertion treat that?

| Option | Description | Selected |
|--------|-------------|----------|
| Declared set per fixture | The whole-report count must match an explicitly named set; extra members carry written causal reasons | ✓ |
| Demote explained effects to info | Engine attributes a stream's start delta to `av_offset` and demotes it; one gating finding per cause | |
| Narrow fixtures to one check | Engineer each fixture so only one check can fire; real files still multi-report | |

**Notes:** Doc 02's own `container.mp4.edit_list` row settles the layering — "the semantic effect is
asserted by `timeline.start`/`audio.priming` — this check pins the mechanism". The yuvj rule is a
different case: it forbids two *spellings* of one fact, not a mechanism plus its effect. Verified
before asking: the +42 ms offset pair already warns on `container.mp4.edit_list` today, before any
timeline check exists.

### Q3 — What does `timeline.start` measure?

| Option | Description | Selected |
|--------|-------------|----------|
| Global origin + relative per stream | One global-scoped absolute origin; per-stream values relative to it, so a whole-file shift is one finding | ✓ |
| Absolute per stream, as written | Literal doc 04; a TS remux fires on every stream for one cause | |
| Relative only, origin as evidence | Quietest; a real whole-file shift never gates | |

**Notes:** Verified that a stream-copy MP4 → MPEG-TS remux shifts every stream's first PTS by the
muxer's 1.4 s default (video 127920, audio 126000 at 90 kHz).

### Q4 — How does `timeline.av_drift` split into compared facts?

| Option | Description | Selected |
|--------|-------------|----------|
| Rate + pattern IDs | Rate in ms/min on `timeline.av_drift`, class on `timeline.av_drift.pattern`; end delta, step time, residual max and the K=32 trajectory as evidence | ✓ |
| One ID, rate only | Single finding per cause, but a step under the rate tolerance never gates | |
| Three IDs, as the doc gates | Rate, end delta and pattern each compared; one drift fires up to three findings | |

**Notes:** Carried into the question and accepted: "end delta" means drift accumulated start to end,
so a pure constant offset never moves `av_drift` — matching doc 04's offset fixture, where only
`av_offset` is expected to gate.

---

## Coarse-timebase cadence

### Q1 — What rule should TIME-05 use for CFR/VFR?

| Option | Description | Selected |
|--------|-------------|----------|
| Grid conformance, rate from span | Ideal interval as an exact rational; each PTS within one tick of the ideal grid; rate derived from the span | ✓ |
| Tick epsilon of 1, rate from span | Keep mode matching with a 1-tick epsilon; smaller change to `derive_cadence` | |
| Leave D-07 as shipped | Exact-tick equality stays; NTSC-in-Matroska stays VFR and the remux warn stands | |

**Notes:** The conflict was found during this discussion, not assumed. `mediadiff compare ntsc.mp4
ntsc_remux.mkv --profile remux` reports `warn video.frame_rate.measured` (30000/1001 vs 1000/33) on
the current build, because Matroska's 1 ms timebase stores 29.97 fps as 33/33/34 ms. Phase 4's own
A1 note invited exactly this: a misclassification found in execution is "a finding to raise, not a
silent widening". D-07 is amended, not replaced.

### Q2 — What should `vfr_profile` bins be keyed on?

| Option | Description | Selected |
|--------|-------------|----------|
| Deviation from the stream's own grid | Fixed fractional buckets; comparable across containers; measures cadence shape | ✓ |
| Ticks, skip across timebases | Doc-literal bins, skipped when the two timebases differ | |
| Ticks, accept the remux warn | Doc-literal, no new machinery, known false positive on remuxes | |

### Q3 — How should `av_drift` avoid firing on timestamp rounding?

| Option | Description | Selected |
|--------|-------------|----------|
| Rate plus an end-delta floor | Gate the rate only when accumulated end delta also clears the 2 ms epsilon | ✓ |
| Derive the tolerance from timebase and span | Adapts to timestamp resolution, but the tolerance becomes data-dependent | |
| Keep 0.2 ms/min, require long fixtures | No engine change; users comparing short clips still get the false positive | |

**Notes:** 0.2 ms/min over the 10-minute reference file is exactly 2 ms, so the two conditions
coincide there rather than fighting.

### Q4 — Fixed or configurable detection thresholds?

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed named constant in v1 | 250 ms, the gap rule, K=32 and the 2 ms epsilon all constants; config deferred with its fingerprint machinery | ✓ |
| Configurable and recorded | Settable and recorded in the fingerprint, mismatches skip — needs a new envelope field, skip reason and fixture pair | |
| Configurable, not recorded | Literal to the doc's "(config)", but two runs at different thresholds compare silently | |

---

## Priming contract

### Q1 — What should Phase 5 read for priming?

| Option | Description | Selected |
|--------|-------------|----------|
| What the demuxer already exposes | `initial_padding` plus the first packet's `AV_PKT_DATA_SKIP_SAMPLES`, source in evidence | ✓ |
| Seam only, always unknown | Literal SC4/TIME-09; least code, but ships a knowingly wrong number where the right one is in the packet | |
| Full AUDIO-04 chain now | Complete precedence resolver; takes a Phase 6 requirement into a phase that may run concurrently | |

**Notes:** Verified with the pinned tools: MP4 and MKV expose first-packet `pts=-1024` with
`skip_samples=1024`; the TS remux of the same file exposes neither. So mixed-knowledge pairs exist
inside Phase 5 itself, not only once Phase 6 lands. This partly satisfies v2's `EXT-05`, which
REQUIREMENTS.md must note.

### Q2 — What does the fingerprint store for `av_offset`?

| Option | Description | Selected |
|--------|-------------|----------|
| Both values plus priming state | Raw, adjusted, and `{state, source, samples}`; compare on the basis both sides share | ✓ |
| Adjusted only, skip on mismatch | Mirrors `skipped:hash_incomparable`; blinds the check on MP4-vs-TS-remux comparisons | |
| Adjusted only, mark unknown as estimated | Reuses P3 D-03's widened tolerance; a ±60 ms window hides real sync breaks | |

### Q3 — When priming is unknown, how should the finding behave?

| Option | Description | Selected |
|--------|-------------|----------|
| Full severity, structured uncertainty | Gates normally; priming carried as a structured object, named in the accept/tune/silence triple | ✓ |
| Demote to warn while unknown | Confidence expressed as severity; a genuine 200 ms sync break would stop blocking | |
| Refuse to report when unknown | Maximally conservative; the flagship check goes dark on every MPEG-TS input | |

**Notes:** The project's own research (`ARCHITECTURE.md`) had flagged severity-versus-confidence as
an unaddressed distinction, naming `timeline.av_offset` with `priming: unknown` as the example. It is
answered here deliberately: visibility, not softening.

### Q4 — Which fixtures prove TIME-10's non-zero-priming path?

| Option | Description | Selected |
|--------|-------------|----------|
| Both arms, offset applied to video | Video-side shift keeps the audio priming signal intact (exact arm) plus TS/itsoffset for the unknown arm | ✓ |
| Keep the doc recipe, restate the expectation | Single fixture, expectation corrected to 20.67 ms with `priming: unknown`; exact arm untested | |
| Unknown arm only, via MPEG-TS | Literal SC4; the recoverable path enabled by Q1 would ship untested | |

**Notes:** Doc 04's own recipe is compromised — `-itsoffset 0.042` on the audio rewrites the edit
list, the priming signal disappears, and the measured unadjusted offset is 20.67 ms, not the doc's
+42 ms.

---

## Perf gate design

### Q1 — What should the gate measure?

| Option | Description | Selected |
|--------|-------------|----------|
| Instruction counts for ratios | Retired instructions under valgrind/cachegrind; wall-clock recorded, never asserted | ✓ |
| Wall-clock, min of N, wide margin | Reuses the existing harness; the shape D-11 refused, with an observed 33–53% swing | |
| CPU time, min of N | Quieter than wall-clock, still frequency- and neighbour-dependent | |

### Q2 — What blocks a merge, given 43–53% measured against a <10% requirement?

| Option | Description | Selected |
|--------|-------------|----------|
| Ratchet against a recorded baseline | Regression gate on a committed baseline; absolute ratios measured and tracked; `PERF-03`'s parser clause amended on the evidence | ✓ |
| Meet the absolute targets | Optimise until under 10%; real work in a phase that doesn't own the parser, and a ratio can be met by slowing the baseline | |
| Gate the absolute budget only | Block on PERF-01's 3 s; observed costs are milliseconds, so it would catch nothing | |

**Notes:** The ratio is high because the baseline pass is very cheap (≈1.5 ms absolute on a 180 s
input), not because the parser is slow. The amendment is recorded openly, in the same shape Phase 4
used for SC5 and PROBE-03.

### Q3 — Where does the regression history live?

| Option | Description | Selected |
|--------|-------------|----------|
| Committed ledger, CI read-only | CI measures, compares, fails, prints the replacement line; a human commits it (UPDATE_GOLDENS discipline); history is the git log | ✓ |
| github-action-benchmark on gh-pages | Charts and alerts out of the box; CI writes to the repo and breaks on fork PRs | |
| Artifacts and job summary only | No repo writes; history lives only in the Actions retention window | |

### Q4 — What is the reference file, and where does it run?

| Option | Description | Selected |
|--------|-------------|----------|
| Promote the 04 generator, designated leg | Same script at 10 minutes/1080p, cached by content hash, gate on `x64-linux` only | ✓ |
| Add a hand-built H.264 stream | Gates the expensive NAL-walk branch; ten minutes of synthetic H.264 is substantial generator work | |
| Two real-encoder references | mpeg4 MP4 plus MPEG-2 TS; doubles generation and measurement time | |

---

## Claude's Discretion

- The timeline check-ID roster, approved at a roster checkpoint in the first plan (phases 3 and 4
  precedent), including the duration-triple incoherence ID, TS wrap events, and whether jitter's
  sigma and max deviation are one check or two.
- Primary-stream selection for `av_offset`/`av_drift`, and explicit skips for audio-only or
  video-only inputs.
- Timecode source scope (TIME-11) — `tmcd` is reachable; MPEG-2 GOP timecode and S12M need
  establishing against the pinned FFmpeg.
- Trajectory storage shape for TIME-08.
- The fixed-point representation of the least-squares fit and jitter sigma — no float, no float sqrt.
- Joining TS `discontinuity_indicator` to demuxed packets for the flagged-versus-unflagged split.
- Which streams `gaps`/`discontinuities` run on, and last-frame duration reconstruction.

## Deferred Ideas

- Configurable detection thresholds with fingerprint recording and mismatch skips — past v1.
- The full `audio.priming` precedence chain — `AUDIO-04`, Phase 6, extending D-09's resolver.
- `EXT-05`'s remaining scope beyond what the demuxer surfaces directly.
- Engine-level finding attribution ("explained by") and report-layer grouping of mechanism with
  effect — rejected here by D-02.
- `meta.tags` noise on cross-container remuxes (key case changes, Matroska's synthesized `DURATION`
  tags) — a Phase 3 volatile-tag question, observed while investigating remux behaviour here.
- Optimising the parser pass to meet an absolute sub-10% ratio.
