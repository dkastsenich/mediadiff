---
phase: 05-timeline-analysis
plan: 21
subsystem: timeline
tags: [research, timeline.av_drift, checkpoint-mapping, ffprobe, python-harness, SC1]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: 05-18's final av_sync input (TimelinePacketView-based checkpoint construction and fit_drift), which the harness must calibrate against bit for bit
provides:
  - A calibrated, exact (int/Fraction, no float) Python re-implementation of the shipped timeline.av_drift checkpoint mapping and fit_drift, proven bit-identical to real `mediadiff snapshot` evidence on 8 fixtures
  - Two evaluated candidate piecewise checkpoint mappings (D1 segment-proportional, D2 media-clock) against a fixed panel of no-regression, step-recipe, and false-positive-guard fixtures, with a documented terminal-checkpoint artifact explaining why neither meets the step-recipe soundness bar
  - A recorded human decision: narrow the published timeline.av_drift.pattern vocabulary (remove `step`) rather than adopt either candidate design; keep `span:declared` as the checkpoint span source
  - A documented, evidence-backed statement that a seamlessly re-timestamped trim is undetectable from timestamps alone until Phase 6's audio decode path exists
  - A corrected account of the `span:observed` MP4-to-TS residual: the true residual is a false linear-drift on the TS side (39 ms), not the MP4-side regression the harness variant originally reported, plus the reason neither span source alone is sound (AAC priming/padding with no edit list)
affects: [timeline-05-22, timeline-05-23, docs/checks/timeline.av_drift.pattern.md, 05-CHECK-ROSTER.md, REQUIREMENTS.md TIME-07]

# Actuals (#2632)
actuals:
  tokens: 26000
  tasks: 3
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Scratch research harness under .planning/, calibrated with exact equality against real product snapshot evidence before any candidate design is judged (T-05-86 pattern) — reusable for future timeline research plans"

key-files:
  created:
    - .planning/phases/05-timeline-analysis/05-step-research/harness.py
    - .planning/phases/05-timeline-analysis/05-step-research/recipes.py
    - .planning/phases/05-timeline-analysis/05-STEP-DESIGN.md
  modified: []

key-decisions:
  - "narrow-vocabulary: remove `step` from timeline.av_drift.pattern's published vocabulary rather than adopt D1 or D2 — neither candidate meets soundness criterion (c) (step_time within one checkpoint spacing of the real join); D1 never reaches `step`, D2 reaches `step` but at a step_time provably unrelated to the real join (a terminal-checkpoint artifact of the step-recipe construction itself)"
  - "span:declared kept unchanged as the checkpoint span source — span:observed as the harness implemented it introduced a false linear-drift on every healthy discontinuity-free MP4 no-regression file; the orchestrator's review found that regression traces to the harness's variant not honoring the plan's own narrower span:observed definition, but the MPEG-TS side of the MP4-to-TS pairs still produces a false 39 ms linear-drift under span:observed regardless, so declared stays recommended either way"
  - "The residual MP4-to-TS timeline.av_drift/timeline.av_drift.pattern false finding is NOT closed by this plan. It is filed as a follow-up: real AAC priming/padding samples with no edit list make the TS side's declared duration an unreliable estimate, and no span-source choice by itself is sound; a priming/padding-aware span is needed"

patterns-established:
  - "Piecewise checkpoint mapping research pattern (harness.py calibrate/evaluate subcommands, recipes.py builders into tempfile.mkdtemp()) — available if `step` is revisited post-Phase-6"

requirements-completed: []  # TIME-07 intentionally NOT marked complete — the decision defers the vocabulary amendment (SC1/TIME-07/05-CHECK-ROSTER.md/docs) to the plan(s) that implement narrow-vocabulary

coverage:
  - id: D1
    description: "A scratch research harness reproduces the shipped K=32 checkpoint trajectory and fit_drift classification bit for bit on 8 calibration fixtures before any candidate design is judged"
    requirement: "TIME-07"
    verification:
      - kind: other
        ref: "python3 .planning/phases/05-timeline-analysis/05-step-research/harness.py calibrate"
        status: pass
    human_judgment: false
  - id: D2
    description: "Two candidate piecewise checkpoint mappings (D1 segment-proportional, D2 media-clock) evaluated on a fixed panel covering doc 04 section 5's step recipe, no-regression fixtures, and false-positive guards"
    requirement: "TIME-07"
    verification:
      - kind: other
        ref: "python3 .planning/phases/05-timeline-analysis/05-step-research/harness.py evaluate"
        status: pass
    human_judgment: false
  - id: D3
    description: "05-STEP-DESIGN.md states the timestamp-only ambiguity (A1: dropout-style trim vs timestamp-stepped edit are byte-identical) and the seamless-re-timestamped-trim limitation (undetectable until Phase 6)"
    requirement: "TIME-07"
    verification:
      - kind: other
        ref: "grep -c 'undetectable from timestamps alone' .planning/phases/05-timeline-analysis/05-STEP-DESIGN.md"
        status: pass
    human_judgment: false
  - id: D4
    description: "A blocking-human decision chooses narrow-vocabulary over adopting a design, and is recorded verbatim in 05-STEP-DESIGN.md's Decision section, with an orchestrator note documenting two review findings verified before presenting it"
    verification: []
    human_judgment: true
    rationale: "This is the human's own architectural/vocabulary decision, made at a gate="blocking-human" checkpoint — not something an automated check can validate beyond confirming the reply was transcribed verbatim (which this plan's acceptance criteria do check textually)"

# Metrics
duration: multi-session (resumed after blocking-human checkpoint)
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 21: Piecewise checkpoint mapping research for `timeline.av_drift.pattern == "step"` Summary

**Human decided narrow-vocabulary (drop `step` from timeline.av_drift.pattern's published values) over adopting either evaluated piecewise checkpoint design, after a calibrated Python harness proved neither D1 (segment-proportional) nor D2 (media-clock) can report a correctly-located step on the doc-04 step recipe.**

## Performance

- **Duration:** multi-session — Tasks 1-2 executed in the original session (~this-session per STATE.md convention), Task 3 resumed after the human answered the blocking-human checkpoint on 2026-09-18
- **Tasks:** 3 (all complete)
- **Files modified:** 3 (harness.py, recipes.py, 05-STEP-DESIGN.md — all new, no product file touched)

## Accomplishments

- Built `harness.py`, a Python-standard-library-only (int/Fraction, zero float) re-implementation of the shipped `timeline.av_drift` checkpoint construction and `fit_drift` classification, calibrated to bit-identical agreement with real `mediadiff snapshot` evidence across 8 fixtures (`timeline_start_base.mp4`, `timeline_start_base_copy.mp4`, `timeline_avoffset_video_shift.mp4`, `timeline_drift_base.mp4`, `timeline_drift_linear.mp4`, `timeline_drift_step.mp4`, `timeline_start_shift.ts`, `timeline_ntsc_remux.mkv`) — every checkpoint field, `end_delta_ms`, `residual_max_ms`, `step_time_ms`, and `pattern` matched exactly with no substitution needed for system ffprobe vs the pinned ffmpeg.
- Built `recipes.py`, generating doc 04 section 5's four step-recipe variants (V1 seamless re-timestamped trim, V2 timestamp-gap trim, V3 content-contiguous jump = `timeline_drift_step.mp4`'s own construction, V4 content-contiguous jump at a different join) into `tempfile.mkdtemp()` with the pinned LGPL ffmpeg, `-flags +bitexact -fflags +bitexact`, no GPL-gated filter.
- Evaluated two candidate piecewise checkpoint mappings (D1 segment-proportional, D2 media-clock) against the full panel (no-regression pairs, V1-V4, and every false-positive guard) under both `span:declared` and `span:observed`. Found and fixed two implementation bugs in the candidates during evaluation (a single-segment fallback that misclassified `timeline_drift_linear.mp4`, and a gap-subtraction bug that silently degenerated D2 into D1) before recording final results.
- Diagnosed exactly why neither candidate reaches soundness criterion (c): the step-recipe construction's own property (declared audio/video spans kept equal at the container level) means the post-splice segment's real audio content is genuinely ~100 ms short of the file's declared end, so checkpoint `k=31` (the absolute terminal checkpoint) always falls past real content — this single boundary artifact keeps D1 at `irregular` and makes D2 report `step` at a `step_time_ms` provably unrelated to the real join (V3 and V4, real joins ~1160 ms apart, both report `step_time_ms=3960`).
- Proved A1 (the flagged timestamp-only ambiguity) directly, not just algebraically: `build_v2_gap_trim` and `build_v3_content_jump` produce byte-identical files (verified by SHA-256), confirming a dropout-style trim and a timestamp-stepped edit over contiguous content are indistinguishable from packet timestamps alone.
- Proved the seamless-trim limitation directly: V1 (genuinely dropping 100 ms of content with no timestamp discontinuity) is smeared into `linear-drift` by every design, confirming it is undetectable from timestamps alone until Phase 6's audio decode path exists.
- Recorded the human's decision (narrow-vocabulary, span:declared) verbatim in `05-STEP-DESIGN.md`'s `## Decision` section, with the date and the two AskUserQuestion selections named.
- Added an `### Orchestrator note (2026-09-18)` correcting the record on two points found during review before the decision was presented: (1) the `span:observed` MP4-side regression reported under "Residual MP4-to-TS drift" is an artifact of the harness's `_resolve_span` always preferring observed extents, not of the plan's own narrower `span:observed` definition (observed extents used only where the container declares no per-stream duration) — under that narrower definition MP4/MKV files keep declared spans and the false `constant-offset`->`linear-drift` flip would not occur; the `span:declared` recommendation still stands for a narrower reason (the TS side of the MP4-to-TS pairs reports a false 39 ms linear-drift under `span:observed` regardless, traced to AAC priming/padding samples with no edit list — no span-source choice alone is sound); (2) D1's failure traces to one boundary point (`k=31`), and a checkpoint-exclusion variant that was never evaluated is recorded as the lead for a future `step` revisit once Phase 6 exists.

## Task Commits

Each task was committed atomically:

1. **Task 1: A scratch harness reproduces the shipped checkpoint trajectory and pattern bit for bit** - `d09b2ea` (feat)
2. **Task 2: Candidate piecewise mappings evaluated on fixed panel; design note written** - `e5998a2` (feat)
3. **Task 3: Decide how SC1's step is closed (checkpoint:decision, gate=blocking-human)** — no separate task commit; the human's recorded reply is committed as part of this plan's metadata commit below (`05-STEP-DESIGN.md`'s `## Decision` section, edited in this session)

**Plan metadata:** this commit (docs: complete 05-21 plan)

## Files Created/Modified

- `.planning/phases/05-timeline-analysis/05-step-research/harness.py` — calibrated exact re-implementation of the shipped checkpoint mapping and fit_drift, plus D1/D2 candidate mappings, `calibrate`/`evaluate` CLI subcommands
- `.planning/phases/05-timeline-analysis/05-step-research/recipes.py` — doc 04 section 5 step-recipe variant builders (V1-V4), pinned LGPL ffmpeg, bitexact, into `tempfile.mkdtemp()`
- `.planning/phases/05-timeline-analysis/05-STEP-DESIGN.md` — calibration proof, candidate design descriptions, panel results, ambiguity analysis, seamless-trim statement, residual MP4-to-TS drift analysis, recommendation, and the recorded human Decision with the orchestrator's correction note

## Decisions Made

- **narrow-vocabulary over adopt:** neither D1 nor D2 meets all four soundness criteria; both fail criterion (c) specifically due to a step-recipe construction artifact (the terminal checkpoint always falling past real post-splice content), not a flaw specific to where the splice sits. Recorded verbatim by the human at the blocking-human checkpoint.
- **span:declared kept unchanged:** span:observed as tested trades a known MP4-to-TS false-positive-style finding for a new false-positive-style finding (or, per the orchestrator's correction, still leaves a false 39 ms linear-drift on the TS side) — no span-source choice alone is sound while AAC priming/padding samples with no edit list remain unaccounted for.
- **The residual MP4-to-TS drift is deliberately NOT closed here.** It is filed as a follow-up requiring a priming/padding-aware span; 05-22 records the waived residual entry with the corrected reason from the orchestrator note.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] D1/D2 single-segment fallback misclassified `timeline_drift_linear.mp4`**
- **Found during:** Task 2 (candidate mapping implementation)
- **Issue:** A first cut of the single-segment fallback used the segment-core's own fixed nominal-timebase ratio unconditionally, which cannot represent genuine whole-file span-based drift, silently misclassifying the calibrated `timeline_drift_linear.mp4` fixture as `constant-offset`.
- **Fix:** Delegated the true single-segment case to D0's own exact whole-file map, so D1/D2 are bit-identical to D0 (and thus correctly `linear-drift`) whenever no discontinuity is detected.
- **Files modified:** `.planning/phases/05-timeline-analysis/05-step-research/harness.py`
- **Verification:** `evaluate` panel row for `timeline_drift_linear.mp4` matches D0 exactly under D1 and D2.
- **Committed in:** e5998a2 (Task 2 commit)

**2. [Rule 1 - Bug] D2's gap-subtraction computed zero gap due to a libavformat packet-duration quirk**
- **Found during:** Task 2 (candidate mapping implementation)
- **Issue:** D2's gap-subtraction used the raw, uncapped packet `duration` field for the segment boundary, which libavformat fills as "interval to next packet" for the packet immediately before a real gap — making the computed gap exactly 0 and silently degenerating D2 into D1's output.
- **Fix:** Used `capped_segment_end` for the segment boundary, matching the fix already applied to segmentation itself.
- **Files modified:** `.planning/phases/05-timeline-analysis/05-step-research/harness.py`
- **Verification:** D2's step-fixture plateaus now land at 0 ms on both sides of the join (the gap-subtraction correctly cancels the real join), distinct from D1's output.
- **Committed in:** e5998a2 (Task 2 commit)

**3. [Rule 1 - Bug, orchestrator review] `span:observed`'s reported MP4-side regression was a harness-variant artifact, not evidence against the plan's own `span:observed` definition**
- **Found during:** Task 3 resume, orchestrator review before presenting the blocking-human checkpoint
- **Issue:** `harness.py`'s `_resolve_span` always prefers the observed packet extent when the `span:observed` variant runs, regardless of whether the container declares a per-stream duration. The plan's own text defines `span:observed` narrowly — observed extents used only where the container declares none. Under the plan's actual definition, MP4/MKV files (which do declare durations) would keep declared spans, so the false `constant-offset`->`linear-drift` flip reported on `timeline_start_base.mp4` and its MP4 siblings is an artifact of how the harness implemented the variant, not evidence against the plan's own `span:observed` proposal.
- **Fix:** Not a code fix (this is a research/documentation plan; no product or harness code was touched in Task 3 per the plan's own file-scope prohibition). Documented as an `### Orchestrator note (2026-09-18)` appended after `## Decision` in `05-STEP-DESIGN.md`, correcting the record without rewriting the original research sections. The `span:declared` recommendation is unaffected — it now rests on the narrower, still-valid finding that the TS side of the MP4-to-TS pairs reports a false 39 ms linear-drift under `span:observed` regardless of the definition used.
- **Files modified:** `.planning/phases/05-timeline-analysis/05-STEP-DESIGN.md` (Decision section only, per resume instructions — no harness.py change)
- **Verification:** Manual review of `_resolve_span`'s implementation against the plan's stated `span:observed` definition (Task 2's action text, line "used when the container carries no per-stream declared duration").
- **Committed in:** this plan's metadata commit (docs: complete 05-21 plan)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs during Task 2's original execution, 1 Rule 1 documentation correction during Task 3's resume review)
**Impact on plan:** All three necessary for correctness of the recorded evidence. No scope creep — no product file (`src/`, `tests/`, `scripts/`) was touched at any point in this plan.

## Issues Encountered

None beyond the deviations above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 05-22 must implement the narrow-vocabulary branch: remove `step` from `timeline.av_drift.pattern`'s published vocabulary, and amend SC1, TIME-07, `05-CHECK-ROSTER.md`, and `docs/checks/timeline.av_drift.pattern.md` accordingly.
- 05-22 must also implement the span:declared branch: keep the shipped span-source behavior unchanged, and file the waived residual MP4-to-TS drift entry with this plan's corrected reason (AAC priming/padding samples with no edit list; a priming/padding-aware span is needed; tracked as a follow-up, not closed here).
- TIME-07 is deliberately left incomplete by this plan (`requirements-completed: []`) — the shared-ID gate defers marking it complete to whichever plan(s) actually implement the narrowing.
- No blockers for 05-22; the decision and its evidence trail are fully recorded in `05-STEP-DESIGN.md`.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*
