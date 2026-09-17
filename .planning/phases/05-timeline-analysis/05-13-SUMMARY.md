---
phase: 05-timeline-analysis
plan: 13
subsystem: testing
tags: [corpus-digest, provenance, designated-leg, ci, golden-files]

requires:
  - phase: 05-timeline-analysis (05-12)
    provides: PERF_BASELINE.txt provisional seed, timeline overhead measurement harness
provides:
  - "tests/golden/CORPUS_DIGEST.txt with all 26 Phase-5 fixture hashes replaced by the designated x64-linux leg's own computed values (run 35269755235)"
  - "tests/golden/CORPUS_DIGEST_PROVISIONAL.txt returned to its empty-on-purpose state with a second TRANSCRIBED-FROM-DESIGNATED-LEG marker recording this event"
affects: [phase-05-close, ship-phase-05]

actuals:
  tokens: 3600
  tasks: 1
  commits: 1

tech-stack:
  added: []
  patterns: ["designated-leg transcription (D-GAP-01 lineage): a captured CI job log's own listing is copied byte-for-byte into a committed golden file, never regenerated locally"]

key-files:
  created: []
  modified:
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt

key-decisions:
  - "PERF_BASELINE.txt deliberately left untouched (still PROVISIONAL): the captured CI run (35269755235) failed at the D-GAP-01 digest-assertion step before vcpkg bootstrap, so the perf ratchet step never ran and produced no real designated-leg instruction count to transcribe -- inventing one would violate the plan's own prohibition against unmeasured numbers"
  - "Task 1 (push/PR authorization) and its outward-facing action were already carried out by the orchestrator with explicit prior user authorization (PR #6 opened against gsd/phase-05-timeline-analysis, base main) before this executor was spawned -- not re-decided here"
  - "Task 3 (the second blocking-human checkpoint, authorizing the push of this transcription commit) was NOT executed -- per gate=\"blocking-human\" semantics it always surfaces to a human, in every mode including auto, and this executor's scope explicitly excludes deciding it"

requirements-completed: []

coverage:
  - id: D1
    description: "26 Phase-5 fixture hashes in CORPUS_DIGEST.txt replaced with the designated x64-linux leg's own computed values, transcribed byte-for-byte from run 35269755235's captured job log"
    requirement: DOC-04
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (clauses 1-4, including the no-rewrite guard against pinned commit 8caf1f1)"
        status: pass
      - kind: other
        ref: "diff <(sed -n '52,77p' tests/golden/CORPUS_DIGEST.txt) designated-leg-hashes.txt -- character-for-character re-verification after the edit"
        status: pass
    human_judgment: true
    rationale: "The captured 26-line listing's fidelity to the live CI job log (run 35269755235, job 105365795054) was established by the orchestrator before this executor was spawned, per the plan's own <flagged_assumptions> A1 and the task's <human-check> instruction to read the log directly rather than trust a summary. This executor cross-verified the transcription against the orchestrator-provided capture file char-for-char but did not independently re-fetch the live CI log."
  - id: D2
    description: "CORPUS_DIGEST_PROVISIONAL.txt emptied of all Phase-5 fixture names, STATUS header rewritten to EMPTY, historical context and both TRANSCRIBED-FROM-DESIGNATED-LEG markers preserved/added"
    requirement: DOC-04
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh clause 3 (zero-entry ledger justified by a well-formed marker)"
        status: pass
    human_judgment: false
  - id: D3
    description: "tests/golden/PERF_BASELINE.txt real designated-leg instruction count (PERF-05)"
    requirement: PERF-05
    verification: []
    human_judgment: true
    rationale: "Not achieved this round by design -- the captured CI run failed at the digest-assertion step before the perf ratchet step ever ran, so no real measurement exists. Deferred to the run that follows Task 3's push, once the leg gets past D-GAP-01's assertion with the now-transcribed hashes."
  - id: D4
    description: "Designated leg confirmed green with every leg-only gate and the perf ratchet proven to have actually run (Task 3)"
    verification: []
    human_judgment: true
    rationale: "Task 3 is a checkpoint:decision with gate=\"blocking-human\" -- it is never auto-approved in any mode and was not executed by this agent. It requires a human to authorize pushing the transcription commit and then to confirm the re-run leg's log directly."

duration: 25min
completed: 2026-09-17
status: halted
---

# Phase 5 Plan 13: Corpus-Digest and Perf-Baseline Transcription (Task 2 of 3) Summary

**Transcribed 26 Phase-5 fixture hashes from designated-leg CI run 35269755235 into CORPUS_DIGEST.txt, emptied the provisional ledger, left PERF_BASELINE.txt provisional since the perf step never ran on that failed leg, and halted at Task 3's blocking-human push-authorization gate.**

## Performance

- **Duration:** 25 min
- **Started:** 2026-09-17T20:55:00Z (approx.)
- **Completed:** 2026-09-17T21:20:00Z (approx.)
- **Tasks:** 1 of 3 (Task 1 pre-satisfied by the orchestrator; Task 3 deliberately not executed)
- **Files modified:** 2

## Accomplishments

- `tests/golden/CORPUS_DIGEST.txt`: the 26 lines naming every Phase-5 fixture (`timeline_avoffset_unknown.ts` through `timeline_vfr.mp4`) were replaced with the designated `x64-linux` leg's own computed SHA-256 hashes, captured from CI run 35269755235 (job 105365795054, commit `03dd909`). All 80 pre-existing lines from the pinned historical reference commit `8caf1f1` verified untouched by `scripts/lint_corpus_digest_provenance.sh` clause 4. The trailing `CORPUS_DIGEST_SUMMARY=` line was recomputed over the full edited listing (new value `3f85e71816e3498ef1990ffa51a9d4bfe773fe9103125bffed01c6cfdf6cd433`).
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`: returned to its empty-on-purpose state. All 26 Phase-5 fixture-name entries removed; the STATUS header rewritten to `STATUS: EMPTY`; the historical-context paragraph and the original 04-21 `TRANSCRIBED-FROM-DESIGNATED-LEG` marker preserved verbatim; a second marker added (`run=35269755235 job=105365795054 commit=03dd909fac2ffde9f146be47eb7be14ab650f029 date=2026-09-17`).
- `tests/golden/PERF_BASELINE.txt` was intentionally left unchanged (still carries the provisional workstation-measured values from 05-12). See "Deviations" below.

## Task Commits

Task 1 (checkpoint:decision, `push the branch / authorize the CI run that produced the digest listing`) was already carried out by the orchestrator, with explicit prior user authorization, before this executor was spawned — no commit from this executor corresponds to it.

1. **Task 2: Capture the designated leg's listing and transcribe it into the golden files** - `1ec326c` (feat)

Task 3 (checkpoint:decision, gate="blocking-human", `authorize pushing the transcription commit`) was **not executed**. See "Deviations" / STOP below.

**Plan metadata:** this commit (SUMMARY + metadata) — no STATE.md/ROADMAP.md/REQUIREMENTS.md advance performed, because the plan has not reached completion (Task 3 remains open; see status: halted above).

## Files Created/Modified

- `tests/golden/CORPUS_DIGEST.txt` - 26 Phase-5 fixture hash lines transcribed from the designated leg's captured listing; summary trailer recomputed
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - emptied of Phase-5 entries; STATUS header and marker updated to record the transcription event

## Decisions Made

- Left `tests/golden/PERF_BASELINE.txt` untouched rather than forcing a transcription, per the orchestrator's explicit scope instruction and the plan's own prohibition against inventing measurements: the captured CI run (35269755235) failed at the `assert_corpus_digest.sh` step (step 12) before vcpkg bootstrap, so steps 21-27 (Configure/Build/Test/valgrind/perf) never ran. No real designated-leg instruction count exists yet.
- Recorded a **second** `TRANSCRIBED-FROM-DESIGNATED-LEG` marker in the provisional ledger rather than replacing the 04-21 marker, so both historical clearing events (Phase 4's and Phase 5's) remain independently auditable. `scripts/lint_corpus_digest_provenance.sh` clause 3 accepts multiple markers (it greps for any well-formed match), so this does not weaken the guard.
- Did not run `scripts/assert_corpus_digest.sh` as part of the required `<verify>` gate (it is not in the plan's own automated verify list), but ran it separately to characterize the local-vs-designated-leg divergence for this SUMMARY, per Task 2's own instruction item 5. As expected and matching `tests/golden/README.md`'s documented policy and the 04-21 precedent, it now fails locally on essentially every non-trivial fixture (162 of 165 non-excluded lines differ) because `CORPUS_DIGEST.txt` is now the designated leg's answer, not this workstation's ffmpeg-encode output. This is the correct, intended end state, not a defect.

## Deviations from Plan

### Auto-fixed Issues

None — no bugs, missing functionality, or blockers were encountered while executing Task 2 itself.

### Scope deviations (orchestrator-directed, not autonomous)

**1. [Orchestrator scope] `tests/golden/PERF_BASELINE.txt` NOT transcribed — left explicitly provisional**
- **Found during:** Task 2, before editing
- **Issue:** The plan's own Task 2 action text calls for replacing the provisional `PERF_BASELINE.txt` line with the real designated-leg measurement. The captured CI run (35269755235) never reached the perf-measurement step — it failed at the corpus-digest assertion (step 12), and steps 21-27 were skipped as a direct consequence.
- **Fix:** No fix — this executor's explicit scope instruction from the orchestrator was to leave `PERF_BASELINE.txt` untouched rather than invent or approximate a number. This is documented rather than silently deviated from.
- **Files modified:** none (file untouched)
- **Verification:** `grep -ci provisional tests/golden/PERF_BASELINE.txt` still reports 1 (not 0) — this is the expected, honest result given the above, and diverges from the plan's own literal acceptance criterion for that line by design.
- **Impact:** The `PERF-05` requirement and the plan's must-have truth "`PERF_BASELINE.txt` carries a REAL measured instruction count from the designated leg" are **not yet satisfied**. A follow-up run, after Task 3's push gets the leg past the now-fixed digest assertion, is required to capture the real measurement.

**2. [Scope] Task 3 not executed — blocking-human checkpoint**
- **Found during:** immediately after Task 2's commit
- **Issue:** Task 3 (`checkpoint:decision`, `gate="blocking-human"`) authorizes pushing this transcription commit and re-confirming the designated leg green. Per golden rule 6 (checkpoints.md) and this executor's explicit instructions, a `blocking-human` gate is never auto-approved in any mode, including auto-mode, and this executor's scope explicitly excludes deciding it.
- **Fix:** None taken — stopping here is the correct behavior, not a deviation requiring a fix.
- **Files modified:** none
- **Verification:** No push was made; `PR #6` and the remote branch were not touched by this executor.
- **Impact:** The plan's provenance loop (must-have truth: "the designated leg is confirmed green INCLUDING its leg-only byte-exact golden tests and the new perf ratchet step") is not yet closed. That requires Task 3's human authorization, a push, and a confirming CI run.

---

**Total deviations:** 0 auto-fixed; 2 scope items explicitly deferred by design (not bugs).
**Impact on plan:** No scope creep. Both deferrals are structural consequences of the captured CI run having failed before reaching the perf step, and of Task 3's gate design — neither is something Task 2 could or should have worked around.

## Issues Encountered

None beyond the two scope deviations documented above, which are expected structural consequences rather than problems requiring resolution within Task 2's own scope.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

**Not ready to close Phase 5.** Two things remain before the phase's provenance loop can be called closed:

1. **Task 3** (`checkpoint:decision`, `gate="blocking-human"`) must be presented to a human: authorize pushing this transcription commit (`1ec326c`) to `gsd/phase-05-timeline-analysis` / PR #6, and decide whether the PR is marked ready for review at the same time.
2. After that push, the designated `x64-linux` leg must be confirmed green on the re-run — specifically that `assert_corpus_digest.sh` now passes against the transcribed hashes, the five leg-only byte-exact golden tests pass by name, `lint_corpus_digest_provenance.sh` passes, and — this time, since the leg should get past step 12 — the perf ratchet step actually runs and either passes against the still-provisional `PERF_BASELINE.txt` baseline or needs its own follow-up transcription plan.

`tests/golden/PERF_BASELINE.txt` staying PROVISIONAL through this commit is expected and by design; it is not a blocker for pushing, since the perf ratchet's own tolerance check compares against whatever baseline is committed regardless of its provisional/real status — it will simply need a follow-up transcription once the leg produces a real measurement.

## Self-Check: PASSED

- `tests/golden/CORPUS_DIGEST.txt` — FOUND on disk
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` — FOUND on disk
- Commit `1ec326c` — FOUND in `git log --oneline --all`
- `bash scripts/lint_corpus_digest_provenance.sh` — re-ran, all 4 clauses pass
- `git diff <(sed -n '52,77p' CORPUS_DIGEST.txt)` vs the captured designated-leg listing — byte-identical

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-17 (Task 2 only; plan halted at Task 3's blocking-human gate)*
