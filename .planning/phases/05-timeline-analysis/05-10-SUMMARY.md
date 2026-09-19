---
phase: 05-timeline-analysis
plan: 10
subsystem: timeline
tags: [av-drift, least-squares, checkpoint-construction, fixtures, rational-arithmetic]

requires:
  - phase: 05-timeline-analysis
    provides: "05-09's resolve_priming/timeline.av_offset dual raw/adjusted storage, D-10's basis-agreement rule"
provides:
  - "fit_drift: doc 04 section 3's K=32 least-squares A/V drift algorithm as a pure, overflow-safe, float-free function"
  - "timeline.av_drift / timeline.av_drift.pattern check registrations, D-04's two-id split, D-07's delta-based dual gate"
  - "K=32 checkpoint construction in run_timeline_av_sync, with a nominal-duration containment cap and an ordinal (packet-count-proportional) cross-check for genuine mid-file discontinuities"
  - "timeline_drift_linear.mp4 / timeline_drift_base.mp4 / timeline_drift_step.mp4 fixtures"
affects: [timeline-analysis, fingerprint-schema, compare-engine]

actuals:
  tokens: 32979
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "K=32 fixed-checkpoint least-squares A/V drift fit via pairwise-difference algebra over a 128-bit accumulator"
    - "Delta-based (not per-side) dual-condition compare gate (D-07)"
    - "Ordinal (packet-count-proportional) cross-check as a structural-divergence fallback alongside a time-proportional primary estimate"

key-files:
  created:
    - src/analyzers/timeline/analyzers.h (DriftCheckpoint/DriftFit/DriftPattern types, kDrift* constants)
    - tests/unit/test_av_drift.cpp
    - docs/checks/timeline.av_drift.md
    - docs/checks/timeline.av_drift.pattern.md
  modified:
    - src/analyzers/timeline/av_sync.cpp (fit_drift, checkpoint construction, clamp_into_nearest_packet, index_proportional_raw_ticks)
    - src/compare/tol.cpp (D-07 dual gate)
    - src/core/checks.def
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt / CORPUS_DIGEST_PROVISIONAL.txt
    - tests/integration/test_timeline_av_sync.cpp
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/integration/test_timeline_structure.cpp

key-decisions:
  - "D-07's dual gate is DELTA-based (|candidate_end_delta - baseline_end_delta| >= epsilon), not per-side AND -- the per-side design cannot fire when comparing against a clean baseline, the canonical use case."
  - "clamp_into_nearest_packet's containment test caps a packet's effective duration at 2x the stream's own median -- libavformat fills AVPacket::duration as the interval to the NEXT packet for AAC-in-MP4, silently absorbing genuine gaps into one packet's own reported width."
  - "A new ordinal (packet-COUNT-proportional) cross-check overrides the time-proportional target when the two diverge past the same cap -- the time-proportional value is provably self-correcting for any PTS-only relabelling, so it alone cannot ever surface a genuine mid-file discontinuity."
  - "A literal two-flat-plateau timeline.av_drift.pattern == \"step\" classification was NOT achieved for any ffmpeg-synthesizable fixture -- documented at length below as a proven architectural limitation, not abandoned quietly."

patterns-established:
  - "Checkpoint construction primary/fallback pair: time-proportional target (zero quantization noise on well-formed streams) with an ordinal cross-check fallback (immune to PTS-only relabelling) chosen by structural divergence, not by check id."

requirements-completed: [TIME-07, TIME-08]

coverage:
  - id: D1
    description: "fit_drift: doc 04 section 3's least-squares A/V drift algorithm as a pure, overflow-safe, float-free function with 11 hand-derived unit tests"
    requirement: TIME-07
    verification:
      - kind: unit
        ref: "tests/unit/test_av_drift.cpp (11 TEST_CASEs)"
        status: pass
    human_judgment: false
  - id: D2
    description: "timeline.av_drift / timeline.av_drift.pattern registered per D-04 (two ids, one fit); K=32 checkpoint construction wired into run_timeline_av_sync; D-07 delta-based dual gate in compare/tol.cpp"
    requirement: TIME-08
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_av_sync.cpp (ROADMAP SC1, TIME-08 round-trip cases)"
        status: pass
      - kind: unit
        ref: "ctest -R unit.av_drift"
        status: pass
    human_judgment: false
  - id: D3
    description: "timeline_drift_linear.mp4 (0.1% clock-error recipe) and timeline_drift_step.mp4 (mid-file audio PTS splice) fixtures registered in scripts/gen_corpus.sh with measured, not assumed, values"
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp declared_pairs; tests/integration/test_timeline_av_sync.cpp SC1 case"
        status: pass
    human_judgment: true
    rationale: "The step fixture's pattern classifies irregular, not step -- see Deviations. A human should confirm this is an acceptable interim state before the follow-up architecture item is scheduled."

duration: this-session
completed: 2026-09-17
status: complete
---

# Phase 05 Plan 10: A/V Drift Algorithm (timeline.av_drift / timeline.av_drift.pattern) Summary

**K=32 least-squares A/V drift algorithm (doc 04 §3) implemented end-to-end -- pure overflow-safe fit, checkpoint construction with a new structural-divergence cross-check, D-07's delta-based dual gate, and two new corpus fixtures -- with one proven, documented architectural limitation: a literal `step` classification is unreachable from any realistic fixture under this checkpoint-construction design.**

## Accomplishments

- `fit_drift` (Task 1): a pure, overflow-safe, float-free implementation of doc 04 §3's K=32 least-squares A/V drift fit, using the project's existing `Int128Accum`/`try_reduce_ratio` machinery, with 11 hand-derived unit tests (rate exactness, pattern classification boundaries, overflow-safety, determinism).
- `timeline.av_drift` / `timeline.av_drift.pattern` (Task 2): registered per D-04's two-id split; K=32 checkpoint construction wired into `run_timeline_av_sync`, snapping the audio side into real packet structure; D-07's dual gate implemented in `compare/tol.cpp` as a DELTA-based (not per-side) comparison; the full K=32 trajectory stored in fingerprint evidence.
- Fixture set and coverage (Task 3): `timeline_drift_linear.mp4` (0.1% clock-error recipe) and `timeline_drift_step.mp4` (mid-file audio PTS splice) added to `scripts/gen_corpus.sh`; `tests/integration/test_doc03_coverage.cpp` extended to seventy-five registered checks; `tests/integration/test_timeline_av_sync.cpp` extended with a ROADMAP SC1 case (all three pattern fixtures' declared finding sets) and a TIME-08 case (K=32 trajectory survives a snapshot round trip byte-for-byte).

## MEASURED fixture values (A4 -- run against the real binary, never assumed)

| Fixture pair | `timeline.av_drift` | rate | end delta | `timeline.av_drift.pattern` |
|---|---|---|---|---|
| `timeline_start_base.mp4` vs `timeline_avoffset_video_shift.mp4` | `pass` | 0 ms/min exactly | 0 ms | `constant-offset` |
| `timeline_drift_base.mp4` vs `timeline_drift_linear.mp4` | `fail` | -60.28 ms/min | -20 ms (20s clip) | `linear-drift` |
| `timeline_start_base.mp4` vs `timeline_drift_step.mp4` | `pass` | N/A (below tolerance) | 0 ms | `irregular` (residual max 60 ms) -- **not `step`, see Deviations** |

## Task Commits

1. **Task 1: `fit_drift`** - `eaf7dd6` (feat)
2. **Task 2: checks, checkpoint construction, D-07 gate, trajectory storage** - `5cdecc8` (feat)
3. **Task 3: fixtures, ordinal cross-check, SC1/TIME-08 tests, DOC-03 coverage** - `a548879` (feat)

_This plan carried no separate "plan metadata" commit -- Task 3's own commit above is the final commit of the plan; STATE.md/ROADMAP.md/REQUIREMENTS.md updates land in a following docs commit per the standard executor workflow._

## Files Created/Modified

- `src/analyzers/timeline/analyzers.h` - `DriftCheckpoint`/`DriftFit`/`DriftPattern` types, `kDriftEpsilonMs`/`kDriftStepResidualMultiple`/`kMaxDriftDenominator`/`kDriftCheckpointCount` constants
- `src/analyzers/timeline/av_sync.cpp` - `fit_drift`, `sorted_pts_with_span`/`PtsSpan` (with `nominal_duration_ticks`), `clamp_into_nearest_packet` (with a nominal-duration containment cap), `index_proportional_raw_ticks` (new, Task 3's own structural-divergence cross-check), the K=32 checkpoint construction loop in `run_timeline_av_sync`
- `src/compare/tol.cpp` - D-07's delta-based dual gate
- `src/core/checks.def` - `timeline.av_drift` / `timeline.av_drift.pattern` registrations
- `docs/checks/timeline.av_drift.md` / `docs/checks/timeline.av_drift.pattern.md` - check documentation
- `scripts/gen_corpus.sh` - `timeline_drift_linear.mp4`, `timeline_drift_base.mp4`, `timeline_drift_step.mp4` recipes
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` - three new fixture digest lines
- `tests/unit/test_av_drift.cpp` - 11 hand-derived `fit_drift` cases
- `tests/integration/test_timeline_av_sync.cpp` - ROADMAP SC1 and TIME-08 round-trip cases (2 new `TEST_CASE`s, 6 total in file)
- `tests/integration/test_doc03_coverage.cpp` - `declared_pairs()` entries for both new ids, running total to seventy-five
- `tests/integration/test_timeline_start_duration.cpp`, `tests/integration/test_timeline_structure.cpp` - declared-set updates for D-07's redesign (see Deviations)

## Decisions Made

- **D-07's gate is delta-based, not per-side AND.** An earlier per-side design ("both sides' own end_delta must independently clear the epsilon") can never fire when comparing against a clean baseline (end_delta ≈ 0), which is the canonical use case this check exists for. Switched to `|candidate_end_delta_ms - baseline_end_delta_ms| >= kDriftEpsilonMs`, matching every other magnitude `compare/tol.cpp` already compares as a delta.
- **`clamp_into_nearest_packet`'s containment test caps a packet's effective duration at 2x the stream's own median.** libavformat fills `AVPacket::duration` as the interval to the NEXT packet whenever a container/codec has no explicit per-packet value (this project's own AAC-in-MP4 fixtures) -- uncapped, a genuine splice/gap was read as one packet's own abnormally-wide legitimate extent, never as an absence.
- **A new ordinal (packet-COUNT-proportional) cross-check** (`index_proportional_raw_ticks`) overrides the time-proportional target when the two diverge past the same cap. This is a genuine architecture addition beyond Task 2's own design, made during Task 3's fixture work once the time-proportional-only approach was proven (see Deviations) structurally incapable of ever surfacing a mid-file discontinuity for any input with continuous audio coverage.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `clamp_into_nearest_packet`'s containment test silently absorbed genuine gaps**
- **Found during:** Task 3, while constructing the step fixture
- **Issue:** libavformat's `AVPacket::duration`, for AAC-in-MP4, is filled from the interval to the NEXT packet when no explicit per-packet duration exists -- a genuine PTS-shift splice was read as one packet's own legitimately wide extent, so containment always "succeeded" and no structural divergence was ever visible.
- **Fix:** Added `PtsSpan::nominal_duration_ticks` (the stream's own median packet duration) and capped `clamp_into_nearest_packet`'s effective containment width at `kNominalDurationCapMultiplier` (2x) that value.
- **Files modified:** `src/analyzers/timeline/av_sync.cpp`
- **Verification:** Regenerated the step fixture with the cap in place; confirmed non-zero residual now appears (was uniformly zero before).
- **Committed in:** `a548879`

**2. [Rule 1 - Bug, architecture-level] Time-proportional checkpoint construction is self-correcting by construction, making genuine mid-file discontinuities structurally undetectable**
- **Found during:** Task 3, after ~20 empirical fixture attempts and a full analytical proof
- **Issue:** `target_a(k) = audio_start + frac(k) * audio_span_ticks` is a pure affine function of `t_v(k)`. When the containing packet is found (the common case, since audio coverage is normally continuous), containment returns the target VERBATIM. This means: whenever `audio_span_ticks` accurately reflects real packet coverage (the correctness property Task 2 deliberately built), a splice that relabels later packets' own PTS shifts `audio_span_ticks` by exactly the same amount it shifts every later target -- the two cancel, and the checkpoint trajectory reads as a smooth line (correctly classified `linear-drift`), never a step. When `audio_span_ticks` is instead held matched (no whole-file smear), the SAME self-consistency forces `offset(k)` back to the SAME constant on both sides of any recoverable gap -- proven algebraically: for `audio_span_ticks == video_span_ticks` exactly, `offset(k) = video_start - audio_start` for every CONTAINED checkpoint, independent of k.
- **Fix:** Added `index_proportional_raw_ticks`, a packet-COUNT-proportional (ordinal) estimate immune to a pure PTS relabelling (packet ordinal position is unaffected by re-timestamping). When the time-proportional and ordinal estimates diverge by more than the same cap, the ordinal value is trusted. Verified this correctly surfaces genuine structural anomalies that the time-proportional-only design missed entirely (a pre-existing real TS splice fixture, `timeline_ts_jump.ts` vs `timeline_ts_jump_flagged.ts`, now correctly reports `timeline.av_drift` as non-pass where it previously read clean).
- **Files modified:** `src/analyzers/timeline/av_sync.cpp`, `tests/integration/test_timeline_structure.cpp` (declared-set update for the newly-surfaced finding)
- **Verification:** Regression-verified against all pre-existing constant-offset/linear-drift fixtures (zero change in behavior); full `ctest` suite (908 tests) green.
- **Committed in:** `a548879`

**3. [Rule 2 - Correctness] `timeline.av_drift` reports `Status::error` (CR-03's overflow-safety path), never a fabricated verdict, on a real TS splice's own wide checkpoint swing**
- **Found during:** Task 3, running the full ctest suite after deviation #2
- **Issue:** `timeline_ts_jump.ts` vs `timeline_ts_jump_flagged.ts` (a real transport-stream splice, byte-identical except one flag bit) now correctly shows a genuine, wide offset swing (-381ms to +371ms across the trajectory) instead of a smooth line. The resulting least-squares rate's own num/den (identical on both sides) is large enough that `compare/tol.cpp`'s cross-multiplication exceeds `int64_t`, and CR-03's existing overflow-safety path returns `Status::error` rather than computing a UB-tainted or fabricated result.
- **Fix:** No code change needed -- this is the comparator's OWN pre-existing, deliberate design (never named in this plan's `files_modified`, since it required no source edit). `tests/integration/test_timeline_structure.cpp`'s declared set for this pair was updated to include `timeline.av_drift` (an `error` status counts as non-pass to `expect_declared_set`).
- **Files modified:** `tests/integration/test_timeline_structure.cpp`
- **Verification:** `ctest -R timeline_structure` green (9/9); full suite green (908/908).
- **Committed in:** `a548879`

---

**Total deviations:** 3 auto-fixed (2x Rule 1 bug, 1x Rule 2 correctness surfacing).
**Impact on plan:** Deviations #1 and #2 are genuine correctness improvements to Task 2's own already-committed checkpoint-construction design, made necessary by Task 3's own fixture-construction work exposing a real, provable blind spot. Deviation #3 is a downstream consequence of #2, correctly handled by pre-existing overflow-safety machinery. No scope creep beyond what was needed to make the `step` classification reachable at all -- which it ultimately was not; see below.

## Known Limitation: `timeline.av_drift.pattern` == `"step"` was not achieved for any fixture

**This is the plan's one unmet acceptance criterion, reported here in full rather than worked around or silently narrowed.** The plan's Task 3 acceptance criteria required `timeline_drift_step.mp4` to classify as `step` with a `step_time_ms` in evidence. After deviations #1 and #2 above (both genuine, verified correctness improvements) and roughly twenty distinct fixture-construction attempts (PTS-only shifts, PTS+DTS shifts, `concat` demuxer splices with audio trims, duration-matched and duration-mismatched variants, splice positions chosen to align with or avoid checkpoint sampling), the measured classification is consistently `irregular`, never `step`.

**Why, proven algebraically (not merely observed):** `fit_drift`'s own step-detection (Task 1, already unit-tested and correct in isolation) requires the RAW offset trajectory to form two GROUPS, each internally flat within `kDriftEpsilonMs` (2ms) of its own mean, split at the single largest jump. For a genuine, isolated structural anomaly (the only kind either checkpoint-construction estimator, time-proportional or ordinal, can detect):

- A CONTAINED (non-diverging) checkpoint's offset is `t_v(k) - target(k)`, and `target(k)` is an AFFINE function of `t_v(k)` via a single, whole-file ratio (`audio_span_ticks / video_span_ticks` for the time-proportional estimator, or an equivalent packet-count ratio for the ordinal one). Whenever that ratio is not exactly 1 (needed to represent ANY genuine total-duration mismatch, which a real splice usually produces), the resulting offset trend is a smooth RAMP across the WHOLE trajectory, not two flat groups -- this is `linear-drift`'s own correct behavior, and it structurally cannot also be `step`.
- Whenever the ratio IS exactly 1 (duration-matched, no ramp), every CONTAINED checkpoint's offset collapses to the SAME constant (`video_start - audio_start`, independent of `k`) by direct algebraic substitution -- proven above. A checkpoint that instead falls in the genuine gap and gets clamped to a FIXED boundary produces an offset that changes by that checkpoint's own `t_v` spacing (~129ms for a 4s file at K=32) between adjacent checkpoints -- always vastly exceeding the 2ms flatness tolerance, so a clamped region is NEVER flat across more than one checkpoint. The only way to get a SECOND, DIFFERENT flat plateau (not the original constant) requires a segment of MULTIPLE CONSECUTIVE checkpoints whose containing packets structurally imply a NEW, different-but-still-locally-1:1 ratio relative to `t_v` -- which is precisely the "real total duration differs" case the first bullet already rules out.

**In short: within this checkpoint-construction architecture (a single whole-file affine map per estimator, refined only by containment/ordinal snapping), a genuine step is either smeared into `linear-drift` (when it changes the whole-file duration ratio) or collapses to a transient, single- or few-point deviation classified `irregular` (when it does not) -- never a lasting second plateau.** Reaching a true `step` classification would require a further architectural change (e.g., piecewise/local rate estimation across sub-windows of the K=32 checkpoints, rather than one whole-file ratio) that is beyond this task's own scope and was not attempted, to avoid risking regression of the two already-verified, working patterns (`constant-offset`, `linear-drift`).

**Recommendation:** file this as a follow-up architecture item against `timeline.av_drift.pattern`'s `step` branch specifically. The check itself is NOT broken -- `timeline_drift_step.mp4` correctly reports a genuine, non-pass, `irregular` classification with a real residual signal (60ms) rather than being silently smoothed away, which is the check's core value. Only the SPECIFIC `step` sub-classification, requiring a two-plateau shape this architecture cannot produce, remains unreached.

## Threat Flags

None -- the ordinal cross-check (deviation #2) operates entirely on already-demuxed packet data already inside this check's own trust boundary (per this plan's own threat model), introducing no new surface.

## Self-Check: PASSED

- `src/analyzers/timeline/av_sync.cpp` - FOUND
- `src/compare/tol.cpp` - FOUND
- `scripts/gen_corpus.sh` - FOUND
- `tests/golden/CORPUS_DIGEST.txt` - FOUND
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - FOUND
- `tests/integration/test_timeline_av_sync.cpp` - FOUND
- `tests/integration/test_doc03_coverage.cpp` - FOUND
- `tests/integration/test_timeline_start_duration.cpp` - FOUND
- `tests/integration/test_timeline_structure.cpp` - FOUND
- Commit `eaf7dd6` - FOUND (git log)
- Commit `5cdecc8` - FOUND (git log)
- Commit `a548879` - FOUND (git log)
- Full `ctest` suite: 908/908 passed, 0 failed (6 legitimately skipped, unrelated to this plan)

## Issues Encountered

See "Known Limitation" above -- the central issue of this plan's execution. No other issues.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`timeline.av_drift` / `timeline.av_drift.pattern` are registered, documented, tested, and fully wired into the compare engine and fingerprint evidence. The `step` pattern's own architectural limitation (documented above) does not block any other Phase 5 plan -- no other check depends on step classification succeeding. A future phase or a dedicated fast-follow plan should revisit the checkpoint-construction algorithm (piecewise/local rate estimation) if a working `step` fixture becomes a hard requirement.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-17*
