---
phase: 06-audio-analysis
plan: 07
subsystem: timeline-analysis
tags: [av_drift, priming, tol-comparator, mpeg-ts, mp4, cpp20]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-06's resolve_priming() 4-tier resolver (container-mechanism tier, unknown as a comparable value, D-17 trailing padding)"
  - phase: 05-timeline-analysis
    provides: "D-10's generic, evidence-shape-gated Rule 2 override in src/compare/tol.cpp for timeline.av_offset"
provides:
  - "detail::span_ticks_for_basis() -- a single, shared span-selection helper (video and audio call it identically) that reconstructs a trimmed (priming-/padding-excluded) checkpoint span directly from the packet-derived raw extent whenever a stream's own priming AND padding are both known and convertible"
  - "src/compare/tol.cpp's Rule 2 override generalised to a second evidence shape (span_basis/adjusted_magnitude) alongside D-10's original (comparison_basis/adjusted_offset_ms), still never gated on check.id"
  - "A real gap fix: resolve_priming()'s call site in av_sync.cpp now passes last_packet_discard_padding (D-17), and the padding-tick conversion no longer silently treats a known-zero padding count as unknown"
  - "WINDOWS.md #32 re-measured with concrete post-fix evidence and moved from waived to open (not fixed) -- the MP4-to-TS residual is confirmed structurally unrecoverable within this plan's scope, not merely unimplemented"
affects: [audio-analysis, timeline-analysis, content-quality]

# Actuals (#2632)
actuals:
  tokens: 23265
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Generic, evidence-shape-gated cross-Measurement comparator override (D-10/D-16): a check's Measurement::value always holds a well-defined, single-side-computable magnitude; an OPTIONAL evidence-shape (a basis flag + an alternate magnitude) lets src/compare/tol.cpp swap to that alternate ONLY when BOTH sides of a pair agree, never gated on check.id."
    - "Span reconstruction over container trust: when a container's own declared-duration field cannot be trusted to already exclude priming/padding (MPEG-TS has no true edit list), reconstruct the trimmed extent directly from the packet-derived raw span minus known priming/padding tick counts, rather than trusting the field."

key-files:
  created: []
  modified:
    - src/analyzers/timeline/av_sync.cpp
    - src/analyzers/timeline/analyzers.h
    - src/compare/tol.cpp
    - docs/checks/timeline.av_drift.md
    - docs/checks/timeline.av_drift.pattern.md
    - tests/unit/test_av_sync.cpp
    - tests/unit/test_tolerance.cpp
    - tests/integration/test_timeline_structure.cpp
    - tests/integration/test_timeline_av_sync.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - .planning/WINDOWS.md

key-decisions:
  - "Reverted an early dual-fit design (Measurement::value forced to a raw-basis fit unconditionally) after it regressed a currently-clean, real-priming MP4-vs-MP4 constant-offset pair to a spurious linear-drift -- the K=32 checkpoint algorithm requires video's and audio's spans to represent the SAME real-world elapsed window, and an untrimmed audio span paired against a video span with no equivalent extension manufactures exactly this artifact whenever real priming exists. Kept a SINGLE fit per measurement, computed on the audio stream's own shared preference."
  - "span_ticks_for_basis's trimmed candidate is RECONSTRUCTED from the packet-derived raw extent (raw minus priming minus padding ticks), never read from the container's own declared-duration field -- MPEG-TS's own field is itself an untrimmed PTS-range estimate (WINDOWS.md #32's own root cause), so trusting it would reproduce the exact defect being fixed."
  - "Fixed a real, silently-wrong gate found while wiring this up: the padding-tick conversion was gated on `> 0`, which treated a genuinely known, zero padding count (a real value) as unknown, permanently disqualifying that side from ever preferring the trimmed basis."
  - "Left WINDOWS.md #32 OPEN, not fixed, per the plan's own A2 sanctioned fallback: audio.priming's own evidence confirms the TS side's priming is genuinely unknown after the remux (no skip_samples, no initial_padding, no edit list survives) -- not unread, but structurally unrecoverable from the bitstream within this plan's scope. Fabricating a basis to force a pass would violate D-11 (never soften/fabricate on unknown priming)."
  - "Re-baselined one test (test_timeline_structure.cpp) outside this task's declared files_modified: a genuine two-TS splice pair's own priming is ALSO genuinely unknown, so its span moved from the container's untrimmed estimate to the packet-derived raw extent -- its own timeline.av_drift.pattern now correctly classifies `irregular` (a large, unflagged step) instead of an artifact `linear-drift` the old container-field-based selection produced."

requirements-completed: [AUDIO-04]

coverage:
  - id: D1
    description: "detail::span_ticks_for_basis() reconstructs a trimmed checkpoint span from the packet-derived raw extent when priming+padding are both known, falling back to the container field or raw otherwise -- one implementation, called from both video and audio sides"
    requirement: AUDIO-04
    verification:
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis prefers the RECONSTRUCTED trimmed span when priming AND padding are both known (Test 1)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis falls back to raw when priming is unknown, even with padding known (Test 2, shared-basis rule)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis treats a genuinely known, ZERO padding count as known, not absent"
        status: pass
    human_judgment: false
  - id: D2
    description: "src/compare/tol.cpp's Rule 2 override generalised to a second evidence shape (span_basis/adjusted_magnitude), never gated on check.id, proven with a synthetic non-av_drift/non-av_offset check id"
    requirement: AUDIO-04
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol D-16 generalised override: both sides declaring span_basis=adjusted on a SYNTHETIC non-av_drift, non-av_offset check id swaps the compared magnitude to adjusted_magnitude on both sides (Test 8)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol D-16 generalised override: a non-numeric adjusted_magnitude leaves the raw magnitude in place rather than throwing or coercing (Test 9)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Genuine drift is still caught after the fix: real MP4-vs-MP4 priming pairs correctly share the trimmed basis and stay clean; the linear-drift and irregular fixtures still fire"
    requirement: AUDIO-04
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_av_sync.cpp#timeline_av_sync - ROADMAP SC1: the constant-offset, linear-drift and spliced-trim fixtures each declare their complete expected finding set"
        status: pass
    human_judgment: false
  - id: D4
    description: "MP4-to-TS pairs (WINDOWS.md #32) come back CLEAN -- NOT achieved; TS priming confirmed genuinely unrecoverable from the bitstream, so the ledger entry is updated with new evidence and left open rather than closed on an assumption"
    human_judgment: true
    rationale: "This deliverable's own acceptance criterion (a lossless MP4-to-TS remux comparing clean) was not met. Whether the project accepts leaving WINDOWS.md #32 open (as the plan's own A2 flagged_assumption explicitly sanctions) versus requiring further architectural work (e.g. a decode-based priming-detection follow-up) is a scope/priority call a human should confirm, not something a passing/failing test can settle."

duration: ~30min (this session's continuation; full plan spanned multiple sessions across a context-compaction boundary)
completed: 2026-09-20
status: complete
---

# Phase 6 Plan 07: Priming-State-Gated `av_drift` Span Summary

**Extended D-10's shared-basis rule from `timeline.av_offset` to `timeline.av_drift`'s checkpoint span via a generic, evidence-shape-gated `tol.cpp` override -- genuinely fixes the false positive whenever both sides' priming/padding are resolvable, but the specific MP4-to-TS `WINDOWS.md` #32 pairs stay open because MPEG-TS's own priming is confirmed structurally unrecoverable after remux.**

## Performance

- **Duration:** ~30 min this session (continuation after a context-compaction boundary; the full investigation, including a rejected first design, spanned a prior session too)
- **Tasks:** 2/2 completed
- **Files modified:** 11

## Accomplishments

- `detail::span_ticks_for_basis()` (new, in `src/analyzers/timeline/av_sync.cpp`/`analyzers.h`): reconstructs a trimmed (priming-/padding-excluded) checkpoint span directly from the packet-derived raw extent, whenever a stream's own priming AND padding are both known and convertible -- never trusting a container's own declared-duration field, since on MPEG-TS (no true edit list) that field is itself an untrimmed PTS-range estimate. One implementation, called identically from both the video-side and audio-side selection.
- `src/compare/tol.cpp`'s existing D-10 Rule 2 override generalised to a second, generic evidence shape (`span_basis`/`adjusted_magnitude`) alongside the original `comparison_basis`/`adjusted_offset_ms`, still never gated on `check.id`. `timeline.av_offset`'s own behavior is byte-for-byte unchanged (verified: every Phase 5 assertion in `test_av_sync.cpp`/`test_tolerance.cpp` stays green).
- Fixed a real, silently-wrong gap found while wiring this up: `resolve_priming()`'s call site in `av_sync.cpp` was missing the `last_packet_discard_padding` argument (D-17), so padding was never actually resolved from this analyzer at all; and the padding-tick conversion was gated on `> 0`, which silently treated a genuinely known, zero padding count (a real value, e.g. `timeline_start_base.mp4`'s own audio stream) as unknown -- permanently disqualifying that side from ever preferring the trimmed basis.
- Verified fixed for real priming pairs: `timeline_start_base.mp4` vs `timeline_avoffset_video_shift.mp4` (both MP4, real known leading priming on both sides) now correctly reports `span_basis: "adjusted"` on both sides and stays clean (`constant-offset`/`constant-offset`, `pass`).
- Verified NOT fixed for the WINDOWS.md #32 target pairs (`timeline_start_base.mp4` vs `timeline_start_shift.ts`, and vs `timeline_avoffset_unknown.ts`): `audio.priming`'s own evidence confirms the TS side's priming is genuinely `unknown` (source=unknown, samples=0, padding=null -- no `skip_samples` side data, no `initial_padding`, no edit list survives the remux). The shared-basis rule correctly falls back to raw-to-raw per D-11 rather than fabricating a basis. `timeline.av_drift`/`timeline.av_drift.pattern` still fail on both pairs; the classification changed shape (irregular, residual_max_ms=42 -> linear-drift, end_delta_ms=39) but the underlying defect is not closed.
- `docs/checks/timeline.av_drift.md` and `.pattern.md` document the span-basis rule as part of the `--explain` contract.
- `WINDOWS.md` #32 updated with the new measured evidence and moved from `waived` to `open` (not `fixed`), per the plan's own A2 sanctioned fallback -- original description and waiver rationale preserved verbatim, both the table row and the mirrored JSON record updated together.

## Task Commits

1. **Task 1: priming-state-gated checkpoint span, through a generalised evidence-shape-gated override** - `9ada2d1` (feat)
2. **Task 2: close WINDOWS.md #32 on evidence and re-baseline the declared sets this fix moves** - `f00e54d` (docs)

_Both commits include their own test changes (unit tests added alongside Task 1's implementation; integration test comment/evidence re-baselining alongside Task 2's WINDOWS.md update) -- this project's own established per-plan commit granularity, matching 06-06-SUMMARY.md's precedent, rather than a separate RED/GREEN TDD split._

## Files Created/Modified

- `src/analyzers/timeline/av_sync.cpp` - `detail::span_ticks_for_basis()` implementation; fixed `resolve_priming()` call site (missing D-17 argument) and the padding `>0` gating bug; single-fit checkpoint construction using the shared basis decision
- `src/analyzers/timeline/analyzers.h` - `SpanBasisCandidates` struct and `span_ticks_for_basis()` declaration with full worked-rationale doc comments; `kDriftAdjustedMagnitudeDen` constant; extended `timeline_av_sync_analyzer()`'s own doc comment
- `src/compare/tol.cpp` - generalised the `side_prefers_adjusted_magnitude`-style override into `side_adjusted_preference`, recognising both the `comparison_basis`/`adjusted_offset_ms` and `span_basis`/`adjusted_magnitude` evidence shapes
- `docs/checks/timeline.av_drift.md` / `.pattern.md` - document the shared-basis span rule
- `tests/unit/test_av_sync.cpp` - `span_ticks_for_basis` unit tests (reconstruction, shared-basis fallback, known-zero-padding, container-field fallback, underflow fallback)
- `tests/unit/test_tolerance.cpp` - generalised-override unit tests against a synthetic, non-`av_drift`/non-`av_offset` check id (fires, negative half, non-numeric value)
- `tests/integration/test_timeline_structure.cpp` - re-baselined one declared set (`timeline.av_drift.pattern` added as a genuine new member, with causal note) for a TS-vs-TS splice pair whose own priming is genuinely unknown
- `tests/integration/test_timeline_av_sync.cpp` / `test_timeline_start_duration.cpp` - corrected stale pre-fix evidence numbers (`end_delta_ms=-122` -> `39`) in the existing declared-set commentary; membership unchanged (both pairs still fail)
- `.planning/WINDOWS.md` - entry #32 moved `waived` -> `open` with new measured evidence appended, original text preserved verbatim

## Decisions Made

See `key-decisions` in frontmatter above. In summary: kept a single, shared-preference fit (not a dual raw/declared computation) after empirically proving the alternative regresses real-priming pairs; reconstructed the trimmed span from the raw extent rather than trusting a container's declared-duration field; fixed a real known-zero-padding gating bug found along the way; left WINDOWS.md #32 open on honest evidence rather than closing it on an assumption.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `resolve_priming()` call site missing the D-17 padding argument**
- **Found during:** Task 1, while wiring `span_ticks_for_basis`'s reconstruction path
- **Issue:** `av_sync.cpp`'s own call to `resolve_priming()` passed only `(skip_samples, initial_padding)`, never the third `last_packet_discard_padding` argument 06-06 added -- `priming.padding_samples` was always `std::nullopt` at this call site, so D-16's "padding known" gate could never fire true here even when padding genuinely was known.
- **Fix:** Added the third argument, matching `src/analyzers/audio/priming.cpp`'s own established call pattern.
- **Files modified:** `src/analyzers/timeline/av_sync.cpp`
- **Verification:** `ctest --test-dir build/x64-linux` full suite, 100% passing (1132/1132) after the fix.
- **Committed in:** `9ada2d1` (Task 1 commit)

**2. [Rule 1 - Bug] Padding-tick conversion silently treated a known-zero value as unknown**
- **Found during:** Task 1, while investigating why the MP4 baseline of the WINDOWS.md #32 target pairs never preferred the trimmed basis despite having fully known priming
- **Issue:** `padding_ticks` was only computed when `*priming.padding_samples > 0` -- a genuinely known, zero padding count (real for `timeline_start_base.mp4`'s own audio stream, per 06-06-SUMMARY.md) was silently treated as "padding unknown", permanently disqualifying that side from ever preferring the trimmed basis and defeating D-16's own fix.
- **Fix:** Removed the `> 0` guard; a known padding count of any value (including 0) now converts.
- **Files modified:** `src/analyzers/timeline/av_sync.cpp`
- **Verification:** Re-measured `timeline_start_base.mp4` vs `timeline_avoffset_video_shift.mp4` directly via `mediadiff compare --json` -- both sides now report `span_basis: "adjusted"`; full test suite still 100% passing.
- **Committed in:** `9ada2d1` (Task 1 commit)

**3. [Rule 1 - Bug, self-caught during this task's own execution] An early raw-forced design regressed a currently-clean pair**
- **Found during:** Task 1, mid-implementation (before the committed design)
- **Issue:** A first implementation computed a dual raw/declared fit, reporting the RAW-basis fit unconditionally as `Measurement::value` (matching a literal reading of the plan's action text). This flipped `timeline_start_base.mp4` vs `timeline_avoffset_video_shift.mp4` (a currently-clean, real-priming constant-offset pair) to a spurious `linear-drift` -- the K=32 proportional-checkpoint algorithm requires video's and audio's spans to represent the SAME real-world elapsed window, and an untrimmed (priming-inclusive) audio span paired against a video span with no equivalent extension manufactures exactly this artifact whenever real priming exists, independent of any cross-file comparison.
- **Fix:** Reverted to a SINGLE fit per measurement, computed on the audio stream's own shared basis preference (matching this file's pre-existing declared-preferred convention, now correctly extended with the reconstructed trimmed span). `span_basis`/`adjusted_magnitude` are additive evidence fields computed from that SAME single fit, not a second, independently-computed alternate.
- **Files modified:** `src/analyzers/timeline/av_sync.cpp`, `src/analyzers/timeline/analyzers.h`
- **Verification:** Full test suite, 100% passing (1132/1132); `timeline_start_base.mp4` vs `timeline_avoffset_video_shift.mp4` confirmed `constant-offset`/`constant-offset`, `pass`, on both `timeline.av_drift` and `timeline.av_drift.pattern`.
- **Committed in:** `9ada2d1` (Task 1 commit)

**4. [Rule 2 - out-of-scope test file re-baselined] `test_timeline_structure.cpp`'s "unflagged jump trigger pair" test**
- **Found during:** Task 1, full test suite run after the fix
- **Issue:** This test (outside the plan's own declared `files_modified` for Task 1, which lists only `test_av_drift.cpp`/`test_tolerance.cpp`) failed once the shared-basis fix was in place: `timeline_ts_nowrap.ts` vs `timeline_ts_jump.ts` (a genuine two-segment TS splice, both sides' priming genuinely `unknown`) now measures its span from the packet-derived raw extent instead of the container's own untrimmed PTS-range estimate the pre-fix code used unconditionally for a priming-unaware audio stream. The candidate's own classification correctly moved from an artifact `linear-drift` to `irregular` (residual_max_ms=721), a large unflagged step -- arguably a MORE honest classification of this fixture's own genuine splice, not a regression.
- **Fix:** Added `timeline.av_drift.pattern` as a new declared non-pass member with a full causal-reason comment explaining the basis correction.
- **Files modified:** `tests/integration/test_timeline_structure.cpp`
- **Verification:** Full test suite, 100% passing (1132/1132).
- **Committed in:** `9ada2d1` (Task 1 commit)

---

**Total deviations:** 4 auto-fixed (3 Rule 1 bugs, 1 Rule 2 out-of-scope test re-baseline)
**Impact on plan:** All four were necessary for correctness or to keep the test suite honestly green. No scope creep beyond what the fix itself required.

### Not Achieved (documented, not silently dropped)

**WINDOWS.md #32's own MP4-to-TS pairs do not come back clean** -- Task 1's own Test 3 and Test 4 (full `pass` on `timeline_start_base.mp4` vs `timeline_start_shift.ts`, and vs `timeline_avoffset_unknown.ts`) could not be satisfied. Root cause, confirmed via `audio.priming`'s own evidence rather than assumed: the TS side's priming is genuinely `unknown` after the remux -- no `skip_samples` packet side data, no `initial_padding`, no container edit list survives a `-c copy` MP4-to-TS remux. This is a structural, bitstream-level limitation, not a gap in this plan's own mechanism: the shared-basis rule this plan builds is generic and evidence-shape-gated, and correctly, honestly falls back to raw-to-raw whenever priming genuinely cannot be resolved (D-11: never fabricate a basis to force a pass). Closing this fully would need a NEW priming-discovery mechanism this plan does not build (e.g. a decode-based detection path), explicitly out of scope here and filed via WINDOWS.md #32's own updated, still-open record.

The plan's own `<flagged_assumptions>` A2 explicitly anticipates and sanctions this exact outcome ("if a residual remains OUTSIDE tolerance the ledger entry stays open... rather than being marked fixed"), and Task 2's own action text repeats it verbatim ("A ledger entry closed on an assumption is worse than one left open on evidence"). This SUMMARY documents the decision per that same standard, rather than closing the ledger entry on a hope.

**Verify-block caveat:** Task 2's own `<verify>` block's second automated check (`grep -c 'fixed' .planning/WINDOWS.md` increasing relative to the pre-task file) happens to pass -- but only because this update's own prose uses the phrase "not fixed" twice (in "Left OPEN, not fixed"), not because the entry was actually marked `fixed`. Flagged here so a future reader does not mistake that grep's literal pass for a closure signal; the entry's own `status` field (`open`, verified directly) is the authoritative signal.

## Known Stubs

None introduced by this plan.

## Threat Flags

None -- this plan's threat register (T-06-23 through T-06-25) is fully covered by the existing generic override mechanism's own tampering/repudiation mitigations, already implemented and tested (Test 8/Test 9 above). No new surface introduced beyond what D-10 already covers.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 6's remaining plans (08-13) are unaffected by this plan's scope (audio loudness/silence/hashing checks, not timeline).
- WINDOWS.md #32 remains open, tracked for a future follow-up (decode-based priming detection, or an explicit policy decision to accept the residual for TS-sourced audio specifically) -- not a blocker for Phase 6's remaining plans, none of which touch `timeline.av_drift`/`av_sync.cpp`.
- The generalised `tol.cpp` override (`span_basis`/`adjusted_magnitude`) is now available as precedent for any future check needing a cross-file, evidence-shape-gated basis decision -- the mechanism is proven generic (Test 8/9's synthetic check id) and does not need to be re-derived.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

- FOUND: .planning/phases/06-audio-analysis/06-07-SUMMARY.md
- FOUND: src/analyzers/timeline/av_sync.cpp
- FOUND: src/compare/tol.cpp
- FOUND: commit 9ada2d1 (Task 1)
- FOUND: commit f00e54d (Task 2)
