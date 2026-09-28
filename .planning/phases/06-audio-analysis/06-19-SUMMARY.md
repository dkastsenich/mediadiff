---
phase: 06-audio-analysis
plan: 19
subsystem: timeline
tags: [av-drift, span-basis, wr-07, gap-closure, determinism]

requires:
  - phase: 06-audio-analysis
    provides: "06-15's GAP_BASE corpus-differential recipe (git archive + scratch build against the main repo's vcpkg_installed), reused unchanged here"
provides:
  - "src/analyzers/timeline/av_sync.cpp: detail::span_ticks_for_basis's SpanBasisCandidates::prefers_declared now derived from whether the trimmed span was ACTUALLY reconstructed (raw span present, priming/padding both known, both checked subtractions in range, trimmed result strictly positive) -- never from mere input availability"
  - "src/analyzers/timeline/analyzers.h: the span_ticks_for_basis contract comment rewritten to name the four reconstruction conditions, replacing the 'inputs were supplied' framing"
  - "tests/unit/test_av_sync.cpp: the pre-existing underflow-to-fallback case flipped from REQUIRE(prefers_declared) to REQUIRE_FALSE, plus four new boundary/precision cases (trimmed==0, trimmed==1, checked_sub overflow, no raw span)"
  - "docs/checks/timeline.av_drift.md / timeline.av_drift.pattern.md: the shared-basis paragraphs corrected to describe the reconstruction-outcome rule instead of input-availability"
  - ".planning/WINDOWS.md #32: dated note recording the WR-07 fix and the unchanged MP4-to-TS declared sets, status left open"
affects: [06-20-designated-leg-confirmation]

actuals:
  tokens: 9600
  tasks: 2
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A helper's own boolean preference field must be set from the OUTCOME of the computation it gates, never from the mere availability of its inputs -- a reconstruction that later fails for range/positivity reasons must not leave an earlier 'inputs looked fine' flag still asserting success. WR-07 is a specific instance of this general class: span_ticks_for_basis computed prefers_declared before attempting the reconstruction, so a failed reconstruction inherited a preference that was true for the wrong reason."

key-files:
  created: []
  modified:
    - src/analyzers/timeline/av_sync.cpp
    - src/analyzers/timeline/analyzers.h
    - tests/unit/test_av_sync.cpp
    - docs/checks/timeline.av_drift.md
    - docs/checks/timeline.av_drift.pattern.md
    - .planning/WINDOWS.md

key-decisions:
  - "Reconstruction success is computed as a single local boolean (has_raw_span && both tick counts known && both checked_sub calls succeed && trimmed > 0) BEFORE any field is written, then prefers_declared and the declared-span-ticks assignment both derive from that one boolean -- avoids the pre-fix bug's own shape (a preference set early, correctness of the later branch state notwithstanding)."
  - "The container-field fallback keeps its exact prior behavior and prior position (only taken when reconstruction did not succeed) -- WR-07's flagged assumption A1 (the fallback still fills has_declared_span/declared_span_ticks) is preserved verbatim; only the PREFERENCE flag's derivation changed."
  - "docs/checks/analyzers.h's corrected contract comment avoids the literal phrase 'were supplied' (the plan's own acceptance criterion greps for its absence) by using 'carry a value' instead -- caught after the first draft accidentally reused the exact old phrase twice."
  - "WINDOWS.md #32 was hand-edited via the Edit tool (not the windows CLI mutation tool) since this appends a dated sentence to an EXISTING entry's description cell rather than creating a new entry -- status and frontmatter counts (open_count/fixed_count/total_count) are verified untouched."

patterns-established: []

requirements-completed: [AUDIO-04]

coverage:
  - id: D1
    description: "WR-07 closed: span_ticks_for_basis's prefers_declared is derived from the reconstruction outcome (raw span present, priming and padding both known, both checked subtractions in range, trimmed result strictly positive), never from input availability alone. The pre-existing underflow-to-fallback test (which asserted prefers_declared=true after a fallback) now asserts false, matching the corrected semantics; four new boundary/precision cases (trimmed==0 not preferred, trimmed==1 preferred, checked_sub overflow not preferred, no raw span not preferred) all pass."
    requirement: "AUDIO-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis does not prefer the adjusted basis when the reconstruction underflows to a non-positive span (WR-07)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis does not prefer the adjusted basis when the trimmed span is exactly 0 (WR-07 boundary)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis prefers it when the trimmed span is exactly 1 tick (WR-07 boundary)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis does not prefer it when the subtraction overflows (WR-07 precision)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - span_ticks_for_basis does not prefer it when no raw span exists, even with priming and padding both known (WR-07)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Amended SC2's two declared finding sets stay unchanged after the av_sync.cpp fix: the round-trip DEVIATION test (integration.audio_priming) and both MP4-to-TS pairs (integration.timeline_av_sync, integration.timeline_start_duration) still declare timeline.av_drift, timeline.av_drift.pattern and audio.priming in their non-pass sets, with zero test-expectation edits."
    requirement: "AUDIO-04"
    verification:
      - kind: integration
        ref: "ctest -R integration.(audio_priming|timeline_av_sync|timeline_start_duration): 40 tests, 40 passed"
        status: pass
      - kind: other
        ref: "git diff --stat -- tests/integration/ is empty"
        status: pass
    human_judgment: false
  - id: D3
    description: "The corpus differential against GAP_BASE (8e37b7284abad06573f4958b5e1cb37695e3956e) shows no changed snapshot on any of 200 real fixtures -- no fixture's timeline.av_drift span_basis moved, proving the fix is output-neutral on this project's own corpus."
    verification:
      - kind: integration
        ref: "200-fixture GAP_BASE vs HEAD snapshot differential: 0 differing"
        status: pass
    human_judgment: false
  - id: D4
    description: "The corrected span_basis meaning is documented in docs/checks/timeline.av_drift.md and timeline.av_drift.pattern.md (the reconstruction-outcome rule replaces the input-availability framing), and .planning/WINDOWS.md #32 carries a dated note naming the fix and the re-measured, unchanged declared sets -- status stays open, frontmatter counts untouched."
    verification:
      - kind: other
        ref: "grep -c reconstructed docs/checks/timeline.av_drift.md == 2; mediadiff explain timeline.av_drift | grep -c reconstructed == 2; grep -n '^| 32 |' .planning/WINDOWS.md shows status open and contains 06-19"
        status: pass
    human_judgment: false

duration: 14min
completed: 2026-09-23
status: complete
---

# Phase 06 Plan 19: span_basis now reports "adjusted" only when the trimmed span was actually reconstructed (WR-07) Summary

**`detail::span_ticks_for_basis`'s `prefers_declared` flag is derived from the reconstruction's real outcome instead of mere input availability, so `tol.cpp`'s D-16 override can no longer compare a genuinely-trimmed span against an untrimmed container fallback under a false "adjusted" label -- both amended SC2 declared sets and all 200 corpus fixtures stay byte-identical.**

## Performance

- **Duration:** 14 min
- **Started:** 2026-09-23T21:42:00Z
- **Completed:** 2026-09-23T21:56:00Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Closed WR-07 (06-REVIEW.md, VERIFICATION.md gap 3): `span_ticks_for_basis` previously set `prefers_declared = priming_ticks.has_value() && padding_ticks.has_value()` -- true purely from input availability, even when the subsequent reconstruction underflowed to a non-positive span and fell through to the container's untrimmed `declared_duration_ticks` field. The function now computes reconstruction success as a single boolean (raw span present, both tick counts known, both `checked_sub` calls stay in range, trimmed result strictly positive) and derives `prefers_declared` from that outcome directly. A side whose reconstruction fails can no longer advertise `span_basis: "adjusted"` over an untrimmed fallback value.
- RED then GREEN, landed as separate commits (this task carries no `tdd="true"` attribute, but its own action text calls for the RED-before-GREEN sequence, and 06-18's own SUMMARY flagged the cost of skipping it): the pre-existing underflow test's assertion was flipped to `REQUIRE_FALSE` and four new boundary/precision cases were added and committed first (`e08469e`), confirmed failing (4/4) against the unmodified implementation, then the fix landed in a second commit (`296674c`) that turned all four green.
- `src/analyzers/timeline/analyzers.h`'s `span_ticks_for_basis` contract comment rewritten to name the four reconstruction conditions explicitly (raw span exists, both tick counts known, both subtractions in range, result strictly positive) instead of the old "both `priming_ticks` and `padding_ticks` were supplied" framing. The corresponding caller comment near `span_prefers_declared` in `av_sync.cpp` was updated to match; the anchor comment's claim that `priming_ticks` holds a value whenever the preference is true was left unchanged, since it stays true under the new semantics (reconstruction success requires `priming_ticks.has_value()`).
- Re-ran the amended SC2 assertions this task's own tracer-feedback gate required: `integration.audio_priming`'s round-trip DEVIATION test, `integration.timeline_av_sync` (`timeline_start_base.mp4` vs `timeline_avoffset_unknown.ts`) and `integration.timeline_start_duration` (vs `timeline_start_shift.ts`) all pass unchanged -- `git diff --stat -- tests/integration/` is empty, so no declared finding set was edited to accommodate the fix.
- `docs/checks/timeline.av_drift.md` and `timeline.av_drift.pattern.md`'s shared-basis paragraphs corrected: `span_basis` reads `"adjusted"` only when the trimmed span was actually reconstructed, never merely because priming/padding inputs were known.
- `.planning/WINDOWS.md` entry #32 gained a dated note (via `Edit`, not the CLI mutation tool) naming the WR-07 fix and the two re-measured, unchanged MP4-to-TS ctest families; status stays `open` (the underlying MPEG-TS priming-unknown residual is unaffected by this fix) and the frontmatter's `open_count`/`fixed_count`/`total_count` were left untouched.
- Corpus-wide differential (GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e` vs HEAD, 06-15's exact recipe: `git archive` the base commit into scratch, configure against the main repo's already-built `vcpkg_installed`, build only the `mediadiff` target, snapshot every real fixture with both binaries, diff pairs) over all 200 real fixtures in `tests/fixtures/`: **0 differing snapshots** -- byte-identical, no fixture's `timeline.av_drift` `span_basis` moved. Scratch tree deleted afterward.
- Full `ctest --test-dir build/x64-linux --output-on-failure`: **1251 passed, 0 failed** (4 new tests over the 1247-test baseline noted at plan start; 6 pre-existing designated-leg-only skips unchanged). `git diff --exit-code -- tests/golden/` exits 0 throughout.

## Task Commits

Each task was committed atomically:

1. **Task 1 (RED): span_ticks_for_basis's prefers_declared must follow the reconstruction outcome** - `e08469e` (test)
2. **Task 1 (GREEN): span_ticks_for_basis's prefers_declared now follows the reconstruction outcome** - `296674c` (feat)
3. **Task 2: Document span_basis's corrected reconstruction-outcome rule; prove the corpus and both amended SC2 pairs unchanged** - `c050ccd` (docs)

## Files Created/Modified

- `src/analyzers/timeline/av_sync.cpp` - `span_ticks_for_basis`'s reconstruction-outcome-derived `prefers_declared`; corrected function-level and caller-level comments
- `src/analyzers/timeline/analyzers.h` - `span_ticks_for_basis`'s contract comment rewritten to name the four reconstruction conditions
- `tests/unit/test_av_sync.cpp` - flipped underflow assertion; four new boundary/precision `span_ticks_for_basis` cases
- `docs/checks/timeline.av_drift.md` - shared-basis paragraph corrected to the reconstruction-outcome rule
- `docs/checks/timeline.av_drift.pattern.md` - same correction where it restates the rule
- `.planning/WINDOWS.md` - entry #32 gained a dated note; status/counts untouched

## Decisions Made

- **Reconstruction success computed as one local boolean before any field write** -- `has_raw_span && priming.has_value() && padding.has_value() && checked_sub(...) && checked_sub(...) && trimmed > 0` -- then both `prefers_declared` and the declared-span-ticks assignment derive from that single value, rather than the pre-fix shape of setting `prefers_declared` early and only conditionally overwriting the span fields afterward.
- **The container-field fallback's own behavior is unchanged** (WR-07's flagged assumption A1) -- only the PREFERENCE flag's derivation moved; the fallback still fills `has_declared_span`/`declared_span_ticks` from `declared_duration_ticks` whenever reconstruction does not succeed, exactly as before.
- **Avoided the literal phrase "were supplied" in the corrected analyzers.h comment** (the plan's own acceptance criterion greps for its absence, `grep -c "were supplied" == 0`) -- caught after a first draft accidentally reused the exact old phrase twice; reworded to "carry a value" instead.
- **WINDOWS.md #32 hand-edited via the Edit tool, not the windows CLI mutation command** -- this appends one dated sentence to an EXISTING entry's description cell rather than creating a new ledger entry, and the plan explicitly required status/counts to stay untouched.

## Deviations from Plan

None - plan executed exactly as written. Task 1 is `type="tracer"` per the plan's own frontmatter; its tracer feedback gate was evaluated per checkpoints.md row 2 (auto mode active, no `gate="blocking-human"` on the task) -- the task's own `<verify>` command was re-run and passed, so expansion into Task 2 proceeded without a checkpoint, logged as `⚡ Tracer verified end-to-end — expanding`.

## Issues Encountered

None. The one near-miss (accidentally reusing "were supplied" in the corrected analyzers.h comment) was caught by re-running the acceptance-criteria grep before committing, and fixed in the same pre-commit pass -- no separate deviation, since it never reached a commit.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- WR-07 is closed. AUDIO-04 remains marked complete (it already was, per this plan's own `must_haves` framing this as an "edge (lifted)" hardening) -- this plan corrects `span_basis`'s semantic honesty without changing any declared finding set.
- 06-13 remains `status: halted` (its confirming CI run is still pending) -- unaffected by this plan; only 06-20 certifies it.
- `.planning/WINDOWS.md` #32 stays open with a fresh dated note -- the underlying MPEG-TS priming-unknown residual (a decode-based priming-detection mechanism this plan deliberately does not build, per the plan's own prohibitions) remains for a future plan.
- The corpus is proven output-neutral (0/200 differing) and the full suite is green (1251/1251) -- ready for 06-20's designated-leg CI confirmation.

## Self-Check: PASSED

- `e08469e`, `296674c`, `c050ccd` all found in `git log --oneline --all`.
- All 6 modified files exist on disk with the expected content (verified via the `grep`/build/test commands run during execution).
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1251 passed, 0 failed (6 pre-existing designated-leg-only skips, unchanged; 4 new WR-07 tests over the 1247-test baseline).
- `git diff --exit-code -- tests/golden/`: clean (exit 0).
- Corpus differential: 200/200 fixtures byte-identical, 0 differing, GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e`.
- `git diff --stat -- tests/integration/` is empty -- no SC2 declared set was edited.
- `grep -c "were supplied" src/analyzers/timeline/analyzers.h` == 0.
- `mediadiff explain timeline.av_drift | grep -c "reconstructed"` == 2.
- `.planning/WINDOWS.md` entry #32 status still `open`, description contains "06-19", frontmatter counts unchanged.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-23*
