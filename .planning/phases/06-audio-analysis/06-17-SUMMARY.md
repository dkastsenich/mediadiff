---
phase: 06-audio-analysis
plan: 17
subsystem: audio
tags: [audio-loudness, tolerance-comparator, false-positive, cr-04, gap-closure]

requires:
  - phase: 06-audio-analysis
    provides: 06-08's `ceiling_state` evidence shape and the generic, evidence-shape-gated asymmetric ceiling escalation in `src/compare/tol.cpp` (AUDIO-06) that this plan gates with a deadband, unchanged in every other respect
provides:
  - "src/analyzers/audio/analyzers.h: kCeilingCrossingDeadbandNum/Den (10/1000, 0.010 dB) -- the rational deadband, in the declared unit of whichever check carries ceiling_state, that gates compare_tol's asymmetric escalation"
  - "src/compare/tol.cpp: ceiling_crossing_material, computed from the EXACT signed delta_num/delta_den once formed, gating apply_ceiling_escalation -- a crossing under the deadband keeps its ordinary tolerance verdict with a message suffix naming the deadband instead of escalating to fail"
  - "tests/unit/test_tolerance.cpp: four new TEST_CASEs against the REAL audio.loudness.true_peak CheckDef (via builtin_registry()) proving the 1/9/10/100 milli-dB boundary and the reverse transition"
  - "docs/checks/audio.loudness.true_peak.md and src/core/checks.def (comment-only): the deadband's rule text, rationale, and rejected alternatives"
affects: [06-18, 06-19, 06-20-designated-leg-confirmation]

actuals:
  tokens: 5705
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "A compare-time deadband gated on the SIGNED delta (delta_num/delta_den, never abs_delta_num), computed once per pair from the SAME exact 256-bit cross-multiplication apparatus (detail::ExactInt) the rest of tol.cpp already uses -- no double, no new evidence key, no analyzer-side change. This is the second generic, evidence-shape-gated override family added to tol.cpp's asymmetric-ceiling mechanism (06-08's ceiling_crossed_upward, now paired with 06-17's ceiling_crossing_material), never gated on check.id."

key-files:
  created: []
  modified:
    - src/analyzers/audio/analyzers.h
    - src/compare/tol.cpp
    - tests/unit/test_tolerance.cpp
    - docs/checks/audio.loudness.true_peak.md
    - src/core/checks.def

key-decisions:
  - "The deadband constant (kCeilingCrossingDeadbandNum=10, Den=1000, 0.010 dB) lives beside the OTHER audio decode/loudness constants in src/analyzers/audio/analyzers.h, not in tol.cpp itself -- tol.cpp is the generic compare-engine layer with no per-check constants of its own; analyzers.h already declares checks' own thresholds (e.g. kTruePeakCeilingDbtp lives in src/probe/audio_decode.h, the analogous per-check-family home), and its comment names the four independent justifications (10x the 0.001 dB quantiser step, 10x a measured sub-LSB PCM shift, 1/30 of the check's own 0.3 dB tolerance, 23x below a measured lossy-AAC amplification) plus the explicit caveat that a future ceiling_state producer in a different unit must revisit it."
  - "ceiling_crossing_material is declared next to ceiling_crossed_upward (so apply_ceiling_escalation's lambda definition stays visually adjacent to both flags it reads) but computed later, right after delta_num/delta_den exist -- the exact signed cross-multiplication delta_num*Den >= Num*delta_den, never the already-computed abs_delta_num, so a candidate that reads BELOW its baseline never clears the deadband even on a hand-built pair where ceiling_state disagrees with the raw magnitude (the synthetic test suite's own precedent)."
  - "The suppressed-crossing message suffix is the plan's own literal text (' (ceiling crossing inside the 0.010 deadband: baseline under, candidate above, rise below the deadband -- not escalated)'), hardcoded rather than formatted from the constants -- matches doc 05/06-REVIEW.md's own exact wording and keeps the escalated-vs-suppressed messages visually distinct at a glance ('asymmetric ceiling crossing' vs 'deadband')."
  - "Both rejected alternatives from 06-REVIEW.md (gating on !within_warn/!within_fail, and a third analyzer-side 'at' ceiling_state value) are recorded as comments at BOTH the constant's declaration and the escalation's gating logic, and restated in the doc -- not just in this plan's own PLAN.md -- so a future reader of the code or the check's explain text sees why the simpler shapes were rejected, not only what was built."

requirements-completed: [AUDIO-06]

coverage:
  - id: D1
    description: "CR-04: a sub-0.010 dB upward ceiling crossing on audio.loudness.true_peak no longer escalates to fail -- it keeps its ordinary tolerance verdict with a message suffix naming the deadband -- while every crossing of 0.010 dB or more still escalates regardless of tolerance, including one comfortably INSIDE the declared 0.3 dB tolerance (SC3). Proven against the REAL audio.loudness.true_peak CheckDef (builtin_registry()), not a synthetic stand-in: 1 and 9 milli-dB rises pass with the deadband suffix (RED confirmed pre-fix: both reported fail with 'asymmetric ceiling crossing'), a 10 milli-dB rise still escalates (the exact boundary, >=), a 100 milli-dB rise inside tolerance still fails (SC3), and the reverse above->under transition is unaffected (no escalation, no deadband suffix)."
    requirement: "AUDIO-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol ceiling escalation: a rise below the 0.010 dB deadband does not escalate on the real audio.loudness.true_peak check (CR-04)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol ceiling escalation: a rise of exactly the deadband escalates on the real audio.loudness.true_peak check (CR-04)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol ceiling escalation: a material crossing inside the 0.3 dB tolerance still fails on the real audio.loudness.true_peak check (SC3, CR-04)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol ceiling escalation: the reverse transition (above -> under) never escalates and never carries a deadband suffix, on the real audio.loudness.true_peak check (CR-04)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp - all five pre-existing synthetic 't.synthetic_tol_widen' ceiling-escalation tests (06-08), unchanged"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_loudness.cpp#audio_loudness - audio_peak_under.flac vs audio_peak_over.flac FAILS on audio.loudness.true_peak / --profile transform still escalates"
        status: pass
    human_judgment: false
  - id: D2
    description: "The deadband and its rationale are documented where mediadiff explain surfaces them (docs/checks/audio.loudness.true_peak.md's rule paragraph, Why it matters, and Tune) and in src/core/checks.def's comment block (no [[check]] field changed), with no output change beyond the knife-edge verdicts CR-04 targets -- proven by a full ctest run and a clean tests/golden/ diff."
    verification:
      - kind: integration
        ref: "full ctest --test-dir build/x64-linux (1241 passed, 0 failed, 6 pre-existing designated-leg-only skips) + mediadiff explain audio.loudness.true_peak (4 occurrences of \"0.010\") + git diff --exit-code -- tests/golden/ (clean)"
        status: pass
    human_judgment: false

duration: 15min
completed: 2026-09-23
status: complete
---

# Phase 06 Plan 17: The 0.010 dB ceiling-crossing deadband (CR-04) Summary

**A knife-edge quantiser-noise crossing on `audio.loudness.true_peak` (a 0.0002 dB shift that happened to straddle the milli-dB-quantised -1.0 dBTP ceiling) no longer hard-fails, while every crossing of 0.010 dB or more -- including one comfortably inside the check's own 0.3 dB tolerance -- still escalates exactly as before, proven on the real check and the real `audio_peak_under.flac`/`audio_peak_over.flac` fixture pair.**

## Performance

- **Duration:** 15 min
- **Started:** 2026-09-23T20:52:20Z
- **Completed:** 2026-09-23T21:07:07Z
- **Tasks:** 2
- **Files modified:** 5 (2 source, 1 test, 1 doc, 1 registry-comment)

## Accomplishments

- `kCeilingCrossingDeadbandNum`/`kCeilingCrossingDeadbandDen` (10/1000, i.e. 0.010 dB) added to `src/analyzers/audio/analyzers.h`, beside the other per-check-family constants -- expressed generically as "the declared unit of whichever check carries `ceiling_state`", not hardcoded to true-peak, with all four measured/in-repo justifications for the exact magnitude recorded in the comment.
- `src/compare/tol.cpp`'s `apply_ceiling_escalation` now gates the asymmetric fail escalation on a NEW `ceiling_crossing_material` flag, computed from the exact SIGNED `delta_num`/`delta_den` cross-multiplication (`detail::ExactInt`, the same 256-bit apparatus every other comparison in this file already uses) against the deadband -- never `abs_delta_num`, so a candidate that reads below its baseline can never clear the deadband even if the evidence shape alone flagged an upward crossing. A crossing that is upward but not material keeps its ordinary tolerance verdict and gains a message suffix naming the deadband instead of escalating.
- Four new unit tests in `tests/unit/test_tolerance.cpp` exercise the REAL `audio.loudness.true_peak` `CheckDef` (looked up via `builtin_registry()`, unit `db`, tolerance `0.3dB`, severity `warn`) rather than a synthetic stand-in: a 1 and a 9 milli-dB rise both pass with the deadband suffix, a 10 milli-dB rise (the exact boundary) still escalates, a 100 milli-dB rise INSIDE the 0.3 dB tolerance still fails (SC3), and the reverse above->under transition is unaffected. The five pre-existing synthetic `t.synthetic_tol_widen` ceiling tests (06-08) and the real `audio_peak_under.flac`/`audio_peak_over.flac` integration pair (a ~1.5 dB rise, far above the deadband) pass unchanged.
- `docs/checks/audio.loudness.true_peak.md` gained the deadband in its rule paragraph (with both rejected alternatives named), an updated "Why it matters" ("counts even when its delta is inside tolerance, as long as it clears the deadband"), and a "Tune" mention naming the new constant beside the -1.0 dBTP ceiling (neither user-tunable). `src/core/checks.def`'s `audio.loudness.true_peak` comment block was amended in the same spirit -- no `[[check]]` field changed (`git diff` on that file touches only `#` lines).
- Full `ctest`: 1241 passed, 0 failed (4 new tests over 06-16's 1237 baseline; 6 pre-existing designated-leg-only skips unchanged). `mediadiff explain audio.loudness.true_peak` renders "0.010" four times. `git diff --exit-code -- tests/golden/` exits 0 throughout -- `tests/golden/inspect_audio.txt` still renders `ceiling_state "under"` with no escalation message.

## Task Commits

Each task was committed atomically:

1. **Task 1: A sub-0.010 dB crossing stops escalating while every material crossing, including one inside tolerance, still fails, proven on the real true_peak check and the real fixture pair** - `70f57c1` (feat)
2. **Task 2: Document the deadband where `explain` and the TTY accept/tune/silence triple show it, and prove no golden moved** - `25d0299` (docs)

## Files Created/Modified

- `src/analyzers/audio/analyzers.h` - `kCeilingCrossingDeadbandNum`/`Den` (10/1000, 0.010 dB) and their four-point rationale comment
- `src/compare/tol.cpp` - `#include "analyzers/audio/analyzers.h"`; `ceiling_crossing_material` declared beside `ceiling_crossed_upward`, computed from the exact signed delta once `delta_num`/`delta_den` exist; `apply_ceiling_escalation` gated on both flags, with a new suppressed-crossing message suffix; extended comment block naming both rejected alternatives
- `tests/unit/test_tolerance.cpp` - `true_peak_check()`/`true_peak_measurement()` helpers against the real registry; four new `compare_tol ceiling escalation: ... (CR-04)` `TEST_CASE`s
- `docs/checks/audio.loudness.true_peak.md` - the rule paragraph, "Why it matters", and "Tune" sections updated to name the deadband and its two rejected alternatives
- `src/core/checks.def` - comment-only amendment to the `audio.loudness.true_peak` block naming the deadband constant

## Decisions Made

- **The deadband lives in `src/analyzers/audio/analyzers.h`**, not `tol.cpp` itself -- `tol.cpp` is the generic compare-engine layer; per-check-family constants belong beside the analyzer that emits the evidence the constant gates, matching `kTruePeakCeilingDbtp`'s own precedent in `src/probe/audio_decode.h`.
- **`ceiling_crossing_material` is computed from the SIGNED delta**, never `abs_delta_num` -- a candidate below its baseline can never satisfy the deadband even on a hand-built evidence pair where `ceiling_state` disagrees with the raw magnitude direction, closing a corner case the synthetic test suite's own construction could otherwise expose.
- **The suppressed-crossing message text is the plan's own literal string**, not assembled from the constants at runtime -- keeps the two outcomes ("asymmetric ceiling crossing" vs "deadband") visually and grep-distinguishable, and matches the wording 06-REVIEW.md and the doc both use.
- **Both rejected alternatives (the `!within_warn`/`!within_fail` no-op gate, and a third analyzer-side `ceiling_state` value) are documented in three places** -- the constant's own comment, the escalation's gating comment, and the check's own doc -- so the reasoning survives independently of this plan's own PLAN.md.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## RED value captured

**Task 1, 1 and 9 milli-dB cases:** with the pre-fix `apply_ceiling_escalation` (unconditional escalation on any `ceiling_crossed_upward`, no deadband), both the -1.001 -> -1.000 dBTP pair and the -1.005 -> -0.996 dBTP pair reported `Status::fail` (`3`, not the expected `0`/pass) with the message containing "asymmetric ceiling crossing" (not "deadband") -- confirmed via a temporary revert of the src fix (source restored byte-identical afterward, diffed against the saved patch to confirm) and a direct `ctest -R` run against just the new test case, which failed with exactly those two assertion mismatches before the fix was reapplied.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- CR-04 is closed: SC3 and AUDIO-06 both demonstrably intact (the boundary tests, the SC3 in-tolerance-crossing test, and the real fixture pair under both the default and `transform` profiles all pass).
- No evidence key or value changed, and no snapshot/golden changed -- this plan is compare-time-only, so it carries no perf-ratchet implication (neither the probe nor the decode path was touched).
- 06-13 remains `status: halted` (its confirming CI run is still pending) -- unaffected by this plan. Only 06-20 certifies it.
- Ready for 06-18.

## Self-Check: PASSED

- `70f57c1`, `25d0299` both found in `git log --oneline --all`.
- All 5 modified files exist on disk with the expected content (verified via the `grep`/build/test commands run during execution).
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1241 passed, 0 failed (6 pre-existing designated-leg-only skips, unchanged; 4 new tests over 06-16's 1237 baseline).
- `git diff --exit-code -- tests/golden/`: clean (exit 0).
- `mediadiff explain audio.loudness.true_peak | grep -c "0.010"`: 4.
- `git diff -- src/core/checks.def`: only `#` comment lines changed.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-23*
