---
phase: 06-audio-analysis
plan: 13
subsystem: audio-decode
tags: [ffmpeg, catch2, ci, cross-architecture, arm64, determinism]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: 06-12's D-06 mp3/mp2 class-1 promotion (x86_64-only evidence) and the provisional audio perf baseline
provides:
  - "D-06's mp3/mp2 class-1 promotion closed on real arm64-osx measurement: aac_fixed CONFIRMED, mp3/mp2 DEMOTED to class 2"
  - "tests/golden/CORPUS_DIGEST.txt's 41 Phase-6 fixture hash lines confirmed matching the designated leg's own computed output (no rewrite needed)"
  - "tests/golden/CORPUS_DIGEST_PROVISIONAL.txt cleared back to empty-on-purpose with a new TRANSCRIBED-FROM-DESIGNATED-LEG marker"
  - "tests/golden/PERF_BASELINE.txt's audio ratchet transcribed from the designated leg's own measurement, no longer provisional"
  - "PERF-04 marked complete on the real ratchet gate"
  - "WINDOWS.md #37 and #38 closed on real CI evidence; #39 opened recording the mp2/mp3/ac3_fixed cross-architecture proof gap"
affects: [phase-07-hardware-acceleration, ship-gate]

# Actuals (#2632)
actuals:
  tokens: 19993
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Confirm-or-demote on real evidence: a class-1 cross-architecture promotion is either backed by a real proof that ran on the target architecture, or the code is demoted -- never left assumed."
    - "Explicit, named test-suite documentation of coverage gaps (a passing TEST_CASE whose only job is to make an unprovable claim's real scope discoverable) rather than a silent absence of test cases."

key-files:
  created: []
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - tests/unit/test_audio_decode.cpp
    - tests/integration/test_audio_hash_decoder.cpp
    - docs/checks/content.audio.sample_hash.md
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/PERF_BASELINE.txt
    - .planning/REQUIREMENTS.md
    - .planning/WINDOWS.md

key-decisions:
  - "aac_fixed's class-1 cross-architecture promotion is CONFIRMED by the real arm64-osx CI leg (D-11's two-build proof passed, run 35735099865); mp3/mp2 are DEMOTED to class 2 rather than assumed, since no trustworthy cross-architecture proof exists for either fixture in this LGPL decode-only pin."
  - "ac3_fixed's cross-architecture status is left unchanged (predates D-06's reopening, not the literal subject of this plan's must-have) but is recorded as an equally-unmeasured open gap, not silently assumed proven."
  - "No CORPUS_DIGEST.txt lines were rewritten: the designated leg's own digest and assert steps both succeeded against the already-committed Phase-6 hashes, proving they already matched byte for byte -- the provisional ledger is cleared on that evidence, not on a fresh transcription."

requirements-completed: [AUDIO-09, TRUST-01, PERF-04]

coverage:
  - id: D1
    description: "D-06's mp3/mp2 class-1 promotion closed on real arm64 measurement: aac_fixed confirmed, mp3/mp2 demoted to class 2 in determinism_class_for_decoder()"
    requirement: AUDIO-09
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - determinism_class_for_decoder covers the whole normative table"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - MP2 auto-selects the fixed-point sibling by name, now recorded class 2 (D-06 demotion, 06-13-PLAN.md)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_hash_decoder.cpp#audio_hash_decoder - Test 1: auto selects the class-1 fixed-point sibling for AAC (confirmed) and mp2 by name though now recorded class2 (demoted, 06-13-PLAN.md)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_hash_decoder.cpp#audio_hash_decoder - class proof Test 2: class-1 hash compares equal against a snapshot from a different build"
        status: pass
    human_judgment: false
  - id: D2
    description: "The two-build cross-architecture proof's real coverage (aac_fixed only) made explicit and named, rather than inferable from absent test cases"
    requirement: TRUST-01
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_hash_decoder.cpp#audio_hash_decoder - Test 9: the class-1 two-build cross-architecture proof covers aac_fixed only -- ac3_fixed/mp3/mp2 remain unproven cross-architecture and are explicitly excluded by name, never silently"
        status: pass
    human_judgment: false
  - id: D3
    description: "tests/golden/CORPUS_DIGEST.txt's 41 Phase-6 fixture lines confirmed matching the designated leg's own computed output; CORPUS_DIGEST_PROVISIONAL.txt cleared with a new TRANSCRIBED-FROM-DESIGNATED-LEG marker"
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (exit 0, clauses 1-4 all OK, run locally against CI run 35735099865's own decisive evidence)"
        status: pass
    human_judgment: false
  - id: D4
    description: "tests/golden/PERF_BASELINE.txt's audio_plain_instructions/audio_full_instructions transcribed from the designated leg's real measurement, no longer provisional; PERF-04 marked complete"
    requirement: PERF-04
    verification:
      - kind: other
        ref: "designated-leg CI run 35735099865's scripts/measure_audio_perf.sh --check-baseline output (transcribed verbatim, captured before this plan's local work began)"
        status: pass
    human_judgment: false
  - id: D5
    description: "WINDOWS.md #37 (true-peak cross-platform) and #38 (D-12 header-pass SBR) closed on real CI evidence from this round trip; #39 opened recording the unproven mp2/mp3/ac3_fixed cross-architecture gap; #33-#36 reviewed and left unchanged (not affected by this round's evidence)"
    verification: []
    human_judgment: true
    rationale: "Ledger-entry honesty is a judgment call about what a given CI run does and does not prove -- verified above to be internally consistent (built/tested locally) but the classification itself is not something a single automated check certifies."

duration: 25min
completed: 2026-09-22
status: halted
---

# Phase 6 Plan 13: Designated-Leg Round Trip -- Cross-Architecture Proof and Real Baselines Summary

**Closed D-06's mp3/mp2 class-1 promotion on real arm64-osx evidence (confirmed aac_fixed, demoted mp3/mp2 to class 2), confirmed the 41 Phase-6 corpus digest lines already matched the designated leg byte-for-byte, and transcribed the real audio perf baseline -- clearing every provisional marker this phase carried.**

## Performance

- **Duration:** ~25 min (Task 2 + Task 3 local work; Tasks 1 and 3's push/CI-read checkpoints were resolved by the orchestrator before this session)
- **Completed:** 2026-09-22
- **Tasks:** 2 of 3 (Task 1's checkpoint was pre-resolved; this session executed Task 2's cross-architecture proof work and Task 3's local transcription work, stopping before the final push per instructions)
- **Files modified:** 9

## Accomplishments

- **D-06's cross-architecture promotion closed honestly.** The real arm64-osx CI round trip (run 35735099865, commit `e5a1677`) proved only `aac_fixed`'s class-1 cross-architecture bit-exactness -- the D-11 two-build proof, run against the hand-written, byte-identical-by-construction `audio_aac_handwritten.mp4` fixture, passed on real aarch64 hardware. `mp3`'s Test-1 pass and `mp2`'s Test-1 pass on that same leg proved only decoder *selection* (auto correctly labels the class), not decode bit-exactness: `mp2`'s only real corpus fixture (`audio_mp2_base.mpg`) is ffmpeg-encoder output already documented (WINDOWS.md #12) as not architecture-stable, and `mp3` has no real corpus fixture at all in this LGPL decode-only pin. `determinism_class_for_decoder()` now demotes both to class 2 -- still selected by name under "auto" (selection and classification are independent, D-06's own T-06-15), now recorded class2 with a `path_signature`. `ac3_fixed`'s cross-architecture status is equally unmeasured (no real AC-3 fixture exists either) but predates D-06's reopening and is recorded as an open gap (WINDOWS.md #39) rather than silently assumed.
- **A new named test makes the proof's real scope discoverable.** `tests/integration/test_audio_hash_decoder.cpp`'s new Test 9 documents, as a Catch2 assertion, exactly which of the four class-1 fixed-point decoders have a real cross-architecture proof (aac_fixed only) and why the other three do not (missing fixture, or fixture not architecture-stable) -- never a silent absence of coverage.
- **The 41 Phase-6 corpus digest lines needed no rewrite.** The designated leg's own "Report corpus digest" and "Assert the corpus digest matches the committed pin (D-GAP-01)" steps both succeeded against the values Phase 6 had already committed (workstation-computed) -- proving those hashes already equalled the designated leg's real output, byte for byte, across all 206 listing lines. `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` is cleared back to empty-on-purpose with a new `TRANSCRIBED-FROM-DESIGNATED-LEG: run=35735099865 commit=e5a16770c00343da91ad8e299773eac9774a1bd3` marker, and its stale "STATUS: EMPTY ON PURPOSE" narrative is now explicitly framed as history rather than a claim about current contents.
- **The audio perf ratchet is now real.** `tests/golden/PERF_BASELINE.txt`'s `audio_plain_instructions`/`audio_full_instructions` lines are transcribed from the designated leg's own measurement (89344287 / 47339403661, both within tolerance of 06-12's local-container provisional seed), replacing that provisional seed and removing the PROVISIONAL marker. PERF-04 is marked complete on that basis -- the enforced ratchet gate, not the informational wall-clock figure, which was not re-measured on the designated leg this round and is left unstated rather than fabricated.
- **WINDOWS.md brought current.** #37 (arm64-osx true-peak cross-platform instability) and #38 (the D-12 header-pass SBR fix) had both already narrated their own fix in prose but were left in `open` status -- both closed on this round's real CI evidence. #33/#34/#35/#36 reviewed and left unchanged (none affected by this round's evidence). #39 opened recording the mp3/mp2/ac3_fixed cross-architecture proof gap this plan's demotion is based on.

## Task Commits

Task 1's checkpoint (push + CI read) was resolved by the orchestrator before this session began (CI run 35735099865, five prior debug-session commits already on the branch). This session executed:

1. **Task 2: confirm or demote D-06's cross-architecture class-1 promotion on the arm64 leg's own measurement** - `6891346` (fix)
2. **Task 3: transcribe the designated leg's digest and baseline (local work only, per orchestrator instruction to stop before the final push)** - `0d4c9a4` (docs)

**Plan metadata:** not yet committed -- Task 3's own checkpoint (the second push) remains, per this session's explicit instruction to stop before it.

## Files Created/Modified

- `src/probe/audio_decode.h` - `determinism_class_for_decoder()` header comment rewritten to describe the confirm/demote outcome
- `src/probe/audio_decode.cpp` - `mp3`/`mp2` moved from the class-1 table to the class-2 table; `aac_fixed`/`ac3_fixed` kept class 1 with updated rationale comments
- `tests/unit/test_audio_decode.cpp` - normative-table test and the MP2 end-to-end test updated to assert class 2 for `mp3`/`mp2`
- `tests/integration/test_audio_hash_decoder.cpp` - Test 1 updated for mp2's new class2 evidence; new Test 9 documents the two-build proof's real coverage scope
- `docs/checks/content.audio.sample_hash.md` - class-1/class-2 descriptions rewritten to state each decoder's final class and cite the measurement that decided it; no "provisional" wording remains
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - cleared to empty-on-purpose with a new TRANSCRIBED-FROM-DESIGNATED-LEG marker; stale status narrative refreshed
- `tests/golden/PERF_BASELINE.txt` - audio ratchet lines transcribed from the designated leg, PROVISIONAL marker removed
- `.planning/REQUIREMENTS.md` - PERF-04 checked complete, both checkbox and traceability-table surfaces
- `.planning/WINDOWS.md` - #37 and #38 closed with CI evidence appended; #39 added recording the cross-architecture proof gap

## Decisions Made

- **aac_fixed CONFIRMED, mp3/mp2 DEMOTED, ac3_fixed left unchanged and flagged.** This plan's own must-have text ("either CONFIRMED... or DEMOTED... never closed on assumption") is scoped literally to the mp3/mp2 promotion; `ac3_fixed`'s pre-existing class-1 status is a separate, earlier decision this plan does not reopen, but its equally-unmeasured cross-architecture status is recorded honestly (WINDOWS.md #39) rather than left implicit.
- **No CORPUS_DIGEST.txt rewrite was needed or performed.** The designated leg's own assert step passing against the already-committed hashes is the decisive evidence; transcribing values that already match would be a no-op that risks introducing a typo, so the ledger was cleared on that proof instead.
- **PERF-04's informational wall-clock figure was not updated with a fabricated number.** Only the instruction-count ratchet (the actual enforced gate) was transcribed from real CI evidence this round; the wall-clock figure remains 06-12's own local measurement, explicitly labeled as such.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] JSON block in WINDOWS.md broke after adding unescaped quotes to a closure note**
- **Found during:** Task 3 (closing WINDOWS.md #38)
- **Issue:** A closure sentence appended to #38's description included literal double quotes (`("Timeline instruction-count ratchet")`), which are unescaped inside the ledger's trailing JSON mirror block, making it invalid JSON.
- **Fix:** Reworded the sentence to avoid embedded quotes and re-verified the JSON block parses (39 entries, correct status counts).
- **Files modified:** `.planning/WINDOWS.md`
- **Verification:** `python3 -c "import json; json.loads(...)"` on the extracted block parses cleanly with 39 entries.
- **Committed in:** `0d4c9a4` (Task 3 commit)

---

**Total deviations:** 1 auto-fixed (1 bug).
**Impact on plan:** Self-inflicted during this session's own edits, caught and fixed before commit; no effect on the plan's substantive outcome.

## Issues Encountered

None beyond the deviation above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- D-06 is closed with an honest, evidence-backed record: `aac_fixed` is proven cross-architecture, `mp3`/`mp2` are correctly demoted, and `ac3_fixed`'s gap is visible for a future phase to close (a D-10-style hand-written AC-3 bitstream would be the mechanism, WINDOWS.md #39).
- The audio perf ratchet and corpus digest are both real, designated-leg-sourced gates now -- Phase 7 (or any later phase touching audio decode) inherits a trustworthy baseline rather than a self-consistency check.
- **Blocker for phase completion:** Task 3's own checkpoint (the second push, human-approved in principle but owned by the orchestrator per this session's instructions) has not been executed. The orchestrator must push this branch, read the confirming CI run, and verify the four named gates (`assert_corpus_digest`, the audio perf ratchet, `integration.audio_sample_hash`, `integration.audio_hash_decoder`, `integration.audio_corpus_sweep`, `integration.doc03_coverage`) all report Passed on the designated leg before this plan can be marked fully certified.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-22*

## Self-Check: PASSED

All 9 files listed under Files Created/Modified confirmed present on disk (`[ -f ]`). Both task commits (`6891346`, `0d4c9a4`) confirmed present in `git log --oneline --all`. Task 2's `<verify>` commands re-run and passed (build clean, `integration.audio_hash_decoder` 14/14 passed, `grep -c "provisional" docs/checks/content.audio.sample_hash.md` = 0). Task 3's plan-level `<verification>` re-run and passed locally: `scripts/lint_corpus_digest_provenance.sh` exits 0, `scripts/gen_corpus.sh` + `scripts/check_corpus.sh` + `scripts/lint_bash4_builtins.sh` + `scripts/lint_eng16.sh` all clean, full `ctest` suite 1208/1208 passed (6 designated-leg-only skips, expected on this non-designated workstation). Task 3's own remaining acceptance criterion -- confirming the four named gates Passed on a fresh confirming CI run -- is NOT yet satisfiable, since the second push has not happened; this is the intentional stop point this session's instructions specified (`status: halted` above).
