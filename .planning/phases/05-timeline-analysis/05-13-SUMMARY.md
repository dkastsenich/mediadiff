---
phase: 05-timeline-analysis
plan: 13
subsystem: testing
tags: [corpus-digest, perf-baseline, provenance, designated-leg, ci, golden-files]

requires:
  - phase: 05-timeline-analysis (05-12)
    provides: PERF_BASELINE.txt workstation-measured seed, timeline overhead measurement harness and ratchet
provides:
  - "tests/golden/CORPUS_DIGEST.txt with all 26 Phase-5 fixture hashes taken from the designated x64-linux leg's own output: 24 from run 35269755235 (1ec326c), plus the timeline_ts_jump pair re-transcribed from run 35347044190 (a56dd9b) after debug session test-898-ci-nonreproducible changed that recipe"
  - "tests/golden/CORPUS_DIGEST_PROVISIONAL.txt back to its empty-on-purpose state, with a third TRANSCRIBED-FROM-DESIGNATED-LEG marker"
  - "tests/golden/PERF_BASELINE.txt carrying the designated leg's real cachegrind instruction counts (plain=257709408, full=344956981, commit=a56dd9b) from run 35347845434, job 105608545973"
affects: [phase-05-close, ship-phase-05]

actuals:
  tokens: 6400
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "designated-leg transcription (D-GAP-01 lineage): a captured CI job log's own listing is copied byte-for-byte into a committed golden file, never regenerated locally"
    - "perf ledger commit field names the PR head whose tree the pull_request merge ref reproduces, never the ephemeral refs/pull/N/merge SHA"

key-files:
  created: []
  modified:
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/PERF_BASELINE.txt

key-decisions:
  - "PERF_BASELINE.txt values are the designated x64-linux leg's own (run 35347845434, job 105608545973), copied verbatim from the ratchet step's pasteable lines. The workstation seed from 05-12 is retired"
  - "PERF_BASELINE.txt commit field records a56dd9b (PR head), not the printed c1cbc2d: the script stamps git rev-parse --short HEAD, which on pull_request runs is GitHub's ephemeral refs/pull/6/merge commit. That commit is off-branch and moves on every push; its tree c0be71d is byte-identical to a56dd9b's"
  - "Task 3 decision (user): push-transcription. The push was authorized and done; PR #6 is open and ready for review (not a draft)"
  - "Task 1 (push/PR authorization) was carried out by the orchestrator with explicit prior user authorization (PR #6, base main, head gsd/phase-05-timeline-analysis)"

requirements-completed: [DOC-04, PERF-05]

coverage:
  - id: D1
    description: "26 Phase-5 fixture hashes in CORPUS_DIGEST.txt replaced with the designated x64-linux leg's own computed values (24 from run 35269755235 via 1ec326c; the timeline_ts_jump pair from run 35347044190 via a56dd9b)"
    requirement: DOC-04
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (clauses 1-4, including the no-rewrite guard against pinned commit 8caf1f1): all pass on c749c99"
        status: pass
      - kind: other
        ref: "CI run 35347845434, job 105608545973, step 12 (assert_corpus_digest.sh): success on the designated leg"
        status: pass
    human_judgment: true
    rationale: "The orchestrator read the CI job logs directly, not summaries, for runs 35269755235, 35347044190 and 35347845434."
  - id: D2
    description: "CORPUS_DIGEST_PROVISIONAL.txt emptied of all Phase-5 fixture entries. STATUS header reads EMPTY ON PURPOSE; historical context and all three TRANSCRIBED-FROM-DESIGNATED-LEG markers are present"
    requirement: DOC-04
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh clause 3 (zero-entry ledger justified by a well-formed marker): pass"
        status: pass
    human_judgment: false
  - id: D3
    description: "tests/golden/PERF_BASELINE.txt carries a real designated-leg instruction count (PERF-05)"
    requirement: PERF-05
    verification:
      - kind: other
        ref: "CI run 35347845434, job 105608545973, step 27 pasteable lines: plain_instructions value=257709408, full_instructions value=344956981, transcribed verbatim in c749c99"
        status: pass
      - kind: other
        ref: "grep -ci provisional tests/golden/PERF_BASELINE.txt = 0; the script's own grep+sed ledger extraction parses both metrics"
        status: pass
    human_judgment: true
    rationale: "Achieved. One open item: the ratchet has not yet run on CI against this transcribed baseline, because c749c99 is unpushed (see Open Items)."
  - id: D4
    description: "Designated leg confirmed green, with every leg-only gate and the perf ratchet confirmed to have actually run (Task 3)"
    verification:
      - kind: other
        ref: "https://github.com/dkastsenich/mediadiff/actions/runs/35347845434 job 105608545973: all steps success, including 12 (digest assert), 24 (Test, 923/923), and 25-27 (valgrind, bench build, ratchet); the five byte-exact goldens ran by name under MEDIADIFF_DESIGNATED_LEG=1 and passed"
        status: pass
    human_judgment: true
    rationale: "Achieved for the run against the 05-12 seed baseline, where both metrics were within tolerance (change=0%). Running the ratchet against the transcribed baseline still needs the push of c749c99."

duration: 25min (first round) + ~15min (continuation)
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 13: Corpus-Digest and Perf-Baseline Transcription Summary

**The designated x64-linux leg's own numbers are now in the three golden files. CORPUS_DIGEST.txt has all 26 Phase-5 fixture hashes (runs 35269755235 and 35347044190), the provisional ledger is empty again, and PERF_BASELINE.txt holds the leg's cachegrind counts from green run 35347845434: plain=257709408 and full=344956981 at a56dd9b.**

## Performance

- **Duration:** about 25 min in the first round (2026-09-17), plus about 15 min in the continuation (2026-09-18). The debug session test-898-ci-nonreproducible ran between the two rounds and is not counted here.
- **Tasks:** 3 of 3. Task 1 was done by the orchestrator. Task 2 was done in two parts: CORPUS_DIGEST in 1ec326c and a56dd9b, PERF_BASELINE in c749c99. Task 3's decision was made by the user and green was confirmed on run 35347845434.
- **Files modified:** 3

## Accomplishments

- `tests/golden/CORPUS_DIGEST.txt`: all 26 Phase-5 fixture lines now carry the designated leg's hashes.
  - 1ec326c transcribed all 26 from run 35269755235 (job 105365795054, commit `03dd909`).
  - a56dd9b re-transcribed the two `timeline_ts_jump*.ts` hashes from run 35347044190 (job 105605976648). The debug session's fix e55c829 had changed that recipe (`-output_ts_offset` 3.0 -> 5.0).
  - All 80 pre-existing lines from pinned reference commit `8caf1f1` are verbatim (lint clause 4).
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` is empty on purpose, with three `TRANSCRIBED-FROM-DESIGNATED-LEG` markers: 04-21's, run 35269755235's and run 35347044190's.
- `tests/golden/PERF_BASELINE.txt` (c749c99): the two data lines are now the designated leg's own values, copied verbatim:
  ```
  leg=x64-linux metric=plain_instructions value=257709408 commit=a56dd9b
  leg=x64-linux metric=full_instructions value=344956981 commit=a56dd9b
  ```
  The STATUS paragraph was rewritten as a transcription record. It gives the run, the job, the date 2026-09-18, and the merge-ref/tree-identity explanation, and notes that the D-16 input's size depends on the thread count. The header contract is unchanged: line format, CI read-only (D-15), a human pastes the line (D-14), and tolerance semantics. The wall-clock context note is kept and now says "a developer workstation".

## Perf baseline: workstation seed vs designated leg

| Metric | Workstation seed (05-12) | Designated leg (this plan) | Delta |
|---|---|---|---|
| plain_instructions | 258117675 | 257709408 | -408267 (-0.158%) |
| full_instructions | 345365337 | 344956981 | -408356 (-0.118%) |
| measured at | b68085d | a56dd9b (via merge ref c1cbc2d, identical tree) | |
| D-16 input size | 122914409 bytes | 120194289 bytes | |
| environment | ubuntu:24.04 container on a developer workstation, valgrind 1:3.22.0-0ubuntu3 | hosted x64-linux runner, run 35347845434, job 105608545973 | |

I recomputed the deltas for this continuation:
- plain: 257709408 - 258117675 = -408267, and -408267 / 258117675 = -0.1582%.
- full: 344956981 - 345365337 = -408356, and -408356 / 345365337 = -0.1182%.

Both deltas are well inside the ratchet's +/-2% tolerance. The overhead ratio on the leg is 33% (full vs plain).

The two input sizes differ because the D-16 reference file is itself produced by an mpeg4 encode, whose output bytes depend on the encoder's auto thread count. The inputs are therefore not byte-identical across hosts. This is one more reason the baseline is scoped to the designated leg only.

### Why commit=a56dd9b and not the printed c1cbc2d

`check_metric_against_baseline` stamps `git rev-parse --short HEAD`. On a `pull_request` run, HEAD is GitHub's ephemeral `refs/pull/6/merge` commit. For run 35347845434 that was `c1cbc2defd634a9027d277f4dd1b5600b79400dc`. That commit is not on the branch and is re-created on every push. Its tree, `c0be71d30c4aa100b9704e0e438ac91f81905414`, is byte-identical to the tree of PR head `a56dd9bcf3eba5ab8ebae1267761a1a2b531085d`. So a56dd9b is the durable name for the code that was measured. The ratchet parser reads only `value=`; the `commit=` field is informational.

### Verbatim perf step output (run 35347845434, job 105608545973, step 27)

```
measure_timeline_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged.
measure_timeline_perf: instruction counts (valgrind --tool=cachegrind, D-13) -- plain=257709408 full=344956981 overhead_percent=33% (absolute PERF-03 ratio, reported every run per D-14) input=.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4 (120194289 bytes)
measure_timeline_perf: metric 'plain_instructions' within tolerance -- baseline=258117675, measured=257709408, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=plain_instructions value=257709408 commit=c1cbc2d
measure_timeline_perf: metric 'full_instructions' within tolerance -- baseline=345365337, measured=344956981, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=full_instructions value=344956981 commit=c1cbc2d
```

## Task 3 outcome

**Decision (user), recorded verbatim as the option id:** `push-transcription`. The push was authorized and done. PR #6 (https://github.com/dkastsenich/mediadiff/pull/6) is open, not a draft, and mergeable, with base `main` and head `gsd/phase-05-timeline-analysis`.

CI history, read directly from the job logs by the orchestrator:

1. **Run 35277145363** (after 1ec326c was pushed): x64-linux passed the digest assert but failed test 898 ("the unflagged jump trigger pair…"), and Windows failed it too. This opened debug session test-898-ci-nonreproducible, which produced these commits:
   - 9ce943d: exact tol comparator
   - e55c829: jump recipe `-output_ts_offset` 3.0 -> 5.0
   - fb84c7b: WINDOWS #29/#30
   - 8c343c7: session checkpoint
2. **Run 35347044190:** the digest assert failed on exactly the two changed jump fixtures, as expected. Transcription commit a56dd9b followed, then debug close-out 18cb107 (local, unpushed).
3. **Run 35347845434**, https://github.com/dkastsenich/mediadiff/actions/runs/35347845434:
   - It ran on PR head a56dd9b, checked out as merge ref c1cbc2d (identical tree), and concluded success.
   - Designated leg x64-linux, job 105608545973: every step succeeded. That includes step 12 (digest assert), step 24 (Test) and steps 25-27 (valgrind install, bench build, ratchet).
   - Test result: "100% tests passed, 0 tests failed out of 923". The only skip is `unit.console_vt`, which is Windows-only and pre-existing.
   - These five byte-exact goldens ran by name under `MEDIADIFF_DESIGNATED_LEG=1` and passed:
     - #213 `unit.inspect_container` golden
     - #605 `unit.ts_scan_golden` ts_204.ts
     - #606 `unit.ts_scan_golden` ts_multiprogram.ts
     - #607 `unit.ts_scan_golden` ts_single.ts
     - #867 `integration.size_checks` read-only golden
   - Windows (job 105608545920) and arm64-osx (job 105608545859) each passed 918/918.
   - x64-osx failed at Build (WINDOWS #14) and arm64-linux failed at the NuGet feed step (WINDOWS #11). Both are pre-existing, already-recorded entries, out of scope and non-blocking.

## Open Items

- **The ratchet has not yet run on CI against the TRANSCRIBED baseline.** Run 35347845434 compared against the 05-12 workstation seed (258117675 / 345365337), and both metrics came out within tolerance. For the ratchet to compare against the new lines, c749c99 (and this docs commit) must be pushed. The orchestrator will request separate user authorization for that push. Cachegrind counts are deterministic for a fixed binary and input, and PERF_BASELINE.txt is not compiled. The next designated-leg run is therefore **expected** to reproduce 257709408 / 344956981 exactly (change=0%). This is an expectation, not an observation.

## Task Commits

1. **Task 1:** orchestrator, with explicit user authorization (PR #6). No executor commit.
2. **Task 2:**
   - `1ec326c` (feat): 26 Phase-5 hashes from run 35269755235, and the provisional ledger emptied.
   - `a56dd9b` (fix): timeline_ts_jump pair re-transcribed from run 35347044190. This commit came from the debug-session flow.
   - `c749c99` (fix): PERF_BASELINE.txt transcribed from run 35347845434.
3. **Task 3:** checkpoint:decision, resolved as `push-transcription`. No repository files are modified by this task.

**Plan metadata:** this docs commit (SUMMARY, STATE, ROADMAP, REQUIREMENTS).

## Verification (continuation, on c749c99)

- `bash scripts/lint_corpus_digest_provenance.sh`: all 4 clauses pass.
- `bash scripts/lint_bash4_builtins.sh`: clean.
- `cmake --build build/x64-linux`: no work to do.
- `ctest --test-dir build/x64-linux`: "100% tests passed, 0 tests failed out of 923". Locally, 6 tests are skipped: `unit.console_vt` (Windows-only) and the five designated-leg-only goldens (#213, #605-#607, #867). The goldens skip because `MEDIADIFF_DESIGNATED_LEG` is unset on this workstation. They passed by name on CI job 105608545973.
- Ledger parse check: the script's own `grep -E "^leg=x64-linux metric=<m> "` plus its `sed` value extraction yields 257709408 and 344956981.

Task 2's "Locally provable" acceptance criteria:
1. `grep -c 'timeline_' tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` reports **0**. PASS, after an orchestrator follow-up. At c749c99/7bedc42 it reported 2: two STATUS-comment lines added in a56dd9b named the jump fixtures. The ledger had zero entries throughout, as lint clause 3 confirmed. The orchestrator then reworded that comment to describe the fixtures without their file names, so the literal criterion holds. The comment change is the only edit; there are no entries, markers or hashes.
2. `grep -c 'STATUS: EMPTY'` reports 1, and the historical-context paragraph is present (line 24). PASS.
3. `git diff tests/golden/CORPUS_DIGEST.txt | grep -c '^-'`, checked against the commits that landed (excluding the `---` header line). PASS. No non-fixture, non-summary line was removed in either commit.
   - 1ec326c: 27 removed = 26 fixture lines + 1 summary.
   - a56dd9b: 3 removed = 2 fixture lines + 1 summary.
4. `grep -ci 'provisional' tests/golden/PERF_BASELINE.txt` reports 0. PASS.
5. This SUMMARY records the run URL, the job id, the verbatim perf output, and both the workstation and the real perf values. PASS. The captured digest listing itself is in the job log of run 35269755235 (job 105365795054) and run 35347044190 (job 105605976648), and was diffed char-for-char against CORPUS_DIGEST.txt at transcription time.

## Decisions Made

- The PERF_BASELINE commit field is `a56dd9b` rather than the printed `c1cbc2d`; see above.
- A second (and, via a56dd9b, a third) `TRANSCRIBED-FROM-DESIGNATED-LEG` marker was added rather than replacing the earlier ones, so that every clearing event stays auditable. Lint clause 3 accepts multiple markers.
- First round: `scripts/assert_corpus_digest.sh` was run locally to characterize the divergence between this workstation and the designated leg. It fails locally on most non-trivial fixtures, which is the documented, intended end state (tests/golden/README.md). That is not a defect.

## Deviations from Plan

### Auto-fixed Issues

None.

### History: first-round scope deviations (2026-09-17, now resolved)

The first executor halted with these two items. They are kept here as history.

**1. [Orchestrator scope] `tests/golden/PERF_BASELINE.txt` NOT transcribed in the first round**
- **Found during:** Task 2, before editing
- **Issue:** The captured CI run (35269755235) failed at the corpus-digest assertion (step 12). Steps 21-27 (including the perf step) never ran, so no designated-leg instruction count existed.
- **Fix at the time:** none; the file was deliberately left provisional rather than filled with an invented number.
- **Resolution (2026-09-18):** Run 35347845434 got past step 12 and ran the ratchet on the designated leg. Its values were transcribed in c749c99, and `grep -ci provisional` now reports 0.

**2. [Scope] Task 3 not executed in the first round (blocking-human checkpoint)**
- **Issue:** Task 3 has `gate="blocking-human"`, so it is never auto-approved.
- **Resolution (2026-09-18):** The user chose `push-transcription`. Green was confirmed on run 35347845434 (see Task 3 outcome).

### Continuation deviations

**3. [Literal criterion miss, since resolved] `grep -c 'timeline_'` on the provisional ledger reported 2 at this continuation's commit.** Both matches were comment lines from a56dd9b; the file had zero entries. The orchestrator's follow-up commit reworded the comment, and the check now reports 0 (Verification item 1).

**Total deviations:** 0 auto-fixed. Two first-round deferrals are now resolved. One literal-criterion miss (comment-only; entry count 0) was resolved by an orchestrator comment rewording.

## Issues Encountered

Test 898 was not reproducible on CI after the first push (run 35277145363). That was handled by the separate debug session test-898-ci-nonreproducible (.planning/debug/resolved/test-898-ci-nonreproducible.md), not by this plan.

## User Setup Required

None.

## Next Phase Readiness

The plan is complete. What remains for phase close is owned by the orchestrator:
- a user-authorized push of c749c99 and the docs commit;
- a confirming designated-leg run showing the ratchet passing against the transcribed baseline, expected at change=0%;
- phase-level verification.

## Self-Check: PASSED

- `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`, `tests/golden/PERF_BASELINE.txt`: all FOUND on disk
- Commits `1ec326c`, `a56dd9b`, `c749c99`: FOUND in `git log`
- `lint_corpus_digest_provenance.sh` passes; ctest 923/923; the PERF ledger parses with the script's own extraction

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*
