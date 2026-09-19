---
phase: 05-timeline-analysis
plan: 25
subsystem: testing
tags: [corpus-digest, perf-baseline, provenance, designated-leg, ci, golden-files, gap-closure]

requires:
  - phase: 05-timeline-analysis (05-24)
    provides: designated-leg evidence capture confirming no transcription and no perf-baseline commit were needed, plus the identification of this plan's Task 2 as the second (redundant, docs-only) confirming CI run
provides:
  - "A read-only local re-verification (Task 1) of the committed state before any second push: provenance lint, digest assert, full ctest and designated-leg ctest all green"
  - "An explicit blocking-human authorization (Task 2, reply push-all) for the single push gsd/phase-05-timeline-analysis 8ad53f1..d40c040, updating open PR #6 with only a planning-metadata commit"
  - "Captured and directly-verified evidence from CI run 35391084761, job 105749349968 (build x64-linux, the designated leg): digest assert on 162 compared lines with the same three documented exclusions, all five byte-exact goldens by name passing, the provenance lint's own clause-by-clause pass, and the perf ratchet passing both metrics within tolerance with the printed PERF-03 overhead ratio recorded"
  - "Direct confirmation, from the raw log text, that no ::error:: skip diagnostic was ever emitted -- the eight string occurrences are all literal Run-step echo text, never a triggered GitHub Actions error annotation"
  - "Mapping of every non-designated-leg outcome to a WINDOWS.md id, including a correction of record: arm64-linux's failure is WINDOWS.md #11 (NuGet feed registration step exits 1), not the 'no mono on arm64 runners' cause 05-24-SUMMARY.md and this plan's own orchestrator prompt had attributed it to -- the Install mono step completes cleanly in this run's log"
  - "The phase-level conclusion that the designated leg is confirmed green on the final gap-closure state (head d40c040, code identical to 8ad53f1 outside .planning/), with every leg-only gate run, and that the perf baseline was independently confirmed unchanged across two separate CI runs after the gap-closure work"
affects: [phase-05-close, ship-phase-05]

actuals:
  tokens: 4200
  tasks: 2
  commits: 0

tech-stack:
  added: []
  patterns:
    - "confirm-before-transcribe (05-13/05-24 precedent, continued): every acceptance line in this SUMMARY -- the digest assert line, all five golden PASS lines, the lint's clause-by-clause output, the perf ratchet ratios, and both non-designated-leg failure lines -- was re-read directly from the saved job log text with grep, not relayed from the orchestrator's state_of_the_world block, before being recorded here"
    - "WINDOWS.md attribution correction discipline: a prior SUMMARY's causal claim (arm64-linux = 'no mono') is corrected here only after independently re-reading the log and finding the Install mono step's own success output, not by trusting either the prior SUMMARY or the orchestrator's restated correction without verification"

key-files:
  created: []
  modified: []

key-decisions:
  - "Task 2 reply: push-all, verbatim, with context. The human chose to push d40c040 (updates PR #6, triggers a CI re-run on docs-only changes) even though it was redundant for confirmation purposes -- reasoning recorded verbatim: 'Push d40c040 now (updates PR #6, triggers a CI re-run on docs-only changes). Redundant for confirmation, but keeps PR #6 in sync with the local branch.' There was no transcription commit and no perf-baseline commit in the pushed range -- the push carried only the planning-metadata commit d40c040 (this plan's own predecessor, 05-24's docs commit)."
  - "Push range: gsd/phase-05-timeline-analysis 8ad53f1..d40c040 (single commit, docs(05-24): capture designated-leg gap-closure evidence, no transcription needed). No tests/golden/* file was touched by this push -- the plan's own possible-change slot (dropping a PERF_BASELINE.txt commit) did not apply, since 05-24 committed no such file."
  - "Confirming run: https://github.com/dkastsenich/mediadiff/actions/runs/35391084761 (event pull_request, head d40c040, conclusion success). Designated leg (build x64-linux) is job 105749349968, success."
  - "arm64-linux attribution corrected: the failure at 'Register vcpkg NuGet feed (read-write, trusted runs only)' (exit code 1, ##[error] at arm64linux.log:5798) is WINDOWS.md #11, a credentials/infra problem in the NuGet feed registration itself. The Install mono for vcpkg's NuGet client (Linux) step immediately preceding it completes with 'No VM guests are running outdated hypervisor (qemu) binaries on this host.' and no error -- confirming the prior 'no mono on arm64 runners' attribution recorded in 05-24-SUMMARY.md (and echoed by this plan's own orchestrator prompt before verification) was wrong. This executor's Task 1 checkpoint (from the earlier session) had already flagged the mismatch; this task resolves it by direct log inspection rather than deferring to either prior claim."
  - "x64-osx attribution: WINDOWS.md #14 unchanged and confirmed correct -- 'ld: symbol(s) not found for architecture arm64' at x64osx.log:5089, followed by '##[error]Process completed with exit code 1.' at x64osx.log:5096, matches #14's Apple-Silicon cross-link triplet/architecture mismatch verbatim."
  - "No PERF_BASELINE.txt change: the committed baseline (plain=257709408, full=344956981) was measured against twice now on independent CI runs after the 05-19/05-22 gap-closure work -- once in 05-24 (run 35389474602: plain=257624066, full=345832699) and again in this run (35391084761: plain=257624066, full=345832718). Both re-runs measured within 0.26% of baseline on both metrics, well inside the +/-2% ratchet tolerance. No baseline adoption decision was needed or made."

requirements-completed: [DOC-04, PERF-05]

coverage:
  - id: D1
    description: "The committed transcription and baseline state re-verify locally, end to end, before any second push (Task 1)"
    requirement: DOC-04
    verification:
      - kind: other
        ref: "bash scripts/lint_corpus_digest_provenance.sh && bash scripts/assert_corpus_digest.sh && ctest --preset x64-linux --output-on-failure && MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure -- all four run locally, all green, per the completed-tasks table (N/A commit, read-only)"
        status: pass
    human_judgment: false
  - id: D2
    description: "The second push happens only after an explicit blocking-human authorization, reply recorded verbatim with its full context"
    verification:
      - kind: other
        ref: "checkpoint:decision Task 2, gate=blocking-human, resolved push-all on 2026-09-18; orchestrator ran exactly one git push (8ad53f1..d40c040), no other PR #6 action taken"
        status: pass
    human_judgment: true
    rationale: "T-05-99 (Elevation of Privilege) requires this be a human decision, not an auto-approved gate, even under yolo/auto-advance config -- gate=blocking-human is never bypassed."
  - id: D3
    description: "The designated x64-linux leg is confirmed green from its job log: digest assert, five byte-exact goldens by name, provenance lint, and perf ratchet with ratios, all re-read directly from the saved log text rather than relayed secondhand"
    requirement: PERF-05
    verification:
      - kind: other
        ref: "CI run https://github.com/dkastsenich/mediadiff/actions/runs/35391084761, job 105749349968 (build x64-linux) -- digest-assert line, five golden PASS lines, provenance-lint clause output, and perf ratchet output all re-read and quoted verbatim from /tmp/.../scratchpad/ci-r5/x64linux.log and lint.log by this executor via grep, not trusted from the orchestrator's relay"
        status: pass
    human_judgment: true
    rationale: "T-05-101 (Repudiation): a green run that skipped a leg-only gate would read like a pass in any summary. The plan's own acceptance criteria require reading the log directly and confirming the absence of any emitted ::error:: skip diagnostic, which this executor did (see Evidence section) rather than trusting the orchestrator's relay alone."
  - id: D4
    description: "Every non-designated leg's status is mapped to a WINDOWS.md entry id, with the arm64-linux attribution corrected against direct log evidence rather than carried forward from a prior (incorrect) SUMMARY claim"
    verification:
      - kind: other
        ref: "x64osx.log:5089/5096 (WINDOWS.md #14, ld: symbol(s) not found for architecture arm64) and arm64linux.log:5798 (WINDOWS.md #11, Register vcpkg NuGet feed step exit 1; Install mono step at arm64linux.log:5729-5733 completes cleanly, correcting 05-24-SUMMARY.md's 'no mono' claim)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Phase-level conclusion recorded: the designated leg is confirmed green on the final gap-closure state (head d40c040, code identical to 8ad53f1 outside .planning/), with every leg-only gate run; the perf baseline is unchanged and was confirmed twice after the gap closure"
    verification:
      - kind: other
        ref: "git log origin/gsd/phase-05-timeline-analysis..HEAD is empty (confirmed via git status at Task 2 start); the push range 8ad53f1..d40c040 touched only .planning/ files (this plan's own predecessor's docs commit), never any src/ or tests/golden/* file -- so d40c040's code is byte-identical to 8ad53f1's code, which is the same code 05-24's first confirming run (35389474602) already measured"
        status: pass
    human_judgment: false

duration: ~15min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 25: Gap-Closure Push Authorization and Confirming-Run Evidence Summary

**A redundant docs-only push (8ad53f1..d40c040) was authorized by explicit human decision (`push-all`) and its confirming CI run (35391084761, job 105749349968) re-verified every designated-leg gate green from the raw log text, closing DOC-04 and PERF-05 and correcting a prior mis-attribution of the arm64-linux failure to WINDOWS.md #11 instead of "no mono."**

## Performance

- **Duration:** ~15 min this continuation (Task 1's local re-verification was completed in an earlier session per the orchestrator's continuation state; this session covers Task 2's decision recording and evidence capture).
- **Started:** 2026-09-18
- **Completed:** 2026-09-18
- **Tasks:** 2 of 2.
- **Files modified:** 0 repository files. Only this SUMMARY plus STATE.md/ROADMAP.md/REQUIREMENTS.md are committed by this plan's docs commit.

## Accomplishments

- **Task 1 (prior session, tracer):** Local pre-flight re-ran `lint_corpus_digest_provenance.sh`, `assert_corpus_digest.sh`, the full `ctest --preset x64-linux` suite, and the designated-leg `MEDIADIFF_DESIGNATED_LEG=1` ctest run, all green, before any second push was even considered. Read-only; no repository files modified.
- **Task 2 (this continuation, checkpoint:decision, gate="blocking-human"):** The human replied `push-all`. The orchestrator ran exactly one `git push origin gsd/phase-05-timeline-analysis`, moving `8ad53f1..d40c040` — a single planning-metadata commit (05-24's own docs commit), no transcription, no perf-baseline commit. This updated open PR #6; no other PR action was taken. The confirming CI run (35391084761) was then read directly from its saved job logs and every acceptance-criteria item verified by grep against the raw text (see Evidence below).

## Evidence (verbatim from the job logs, verified directly by this executor)

**Run:** https://github.com/dkastsenich/mediadiff/actions/runs/35391084761
**Event:** `pull_request`, head `d40c040`, conclusion `success`

### Job results table

| Job | Result | Job ID |
|---|---|---|
| lint (ENG-16 boundary) | success | 105749346720 |
| build (x64-linux) — **designated leg** | success | 105749349968 |
| build (arm64-osx) | success | 105749350024 |
| build (x64-windows-static-md) | success | 105749350108 |
| build (x64-osx) | failure — WINDOWS.md #14 | 105749350109 |
| build (arm64-linux) | failure — WINDOWS.md #11 (attribution corrected, see below) | 105749350149 |

### Digest assert (D-GAP-01)

```
x64linux.log:4345: assert_corpus_digest.sh: self-test OK -- known-good (Opus+summary-only diff) passed, non-vacuity control (non-Opus diff) failed, count-guard control (excluded count != 3) failed.
x64linux.log:4346: assert_corpus_digest.sh: compared 162 line(s); did not compare: the mkv_opus_a.webm line, the mkv_opus_b.webm line, the CORPUS_DIGEST_SUMMARY= line.
```

Matches every prior designated-leg run's shape exactly (162 compared, 3 named exclusions — WINDOWS.md #22/#24-waived libopus cross-host nondeterminism).

### The five byte-exact golden tests (by name, all Passed)

```
x64linux.log:6937: 248/994 Test #248: unit.inspect_container - golden: the container+meta section for one representative fixture per family ... Passed  0.02 sec
x64linux.log:7791: 675/994 Test #675: unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden ... Passed  0.01 sec
x64linux.log:7793: 676/994 Test #676: unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden ... Passed  0.01 sec
x64linux.log:7795: 677/994 Test #677: unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden ... Passed  0.01 sec
x64linux.log:8315: 937/994 Test #937: integration.size_checks - the size.* findings are pinned by a committed, read-only golden ... Passed  0.01 sec
```

Test summary (x64linux.log:8431): `100% tests passed, 0 tests failed out of 994`.

### Provenance lint (lint.log)

```
lint.log: lint_corpus_digest_provenance.sh: clause 1 OK -- 'tests/golden/CORPUS_DIGEST.txt' has exactly one CORPUS_DIGEST_SUMMARY= line, and it is the last line.
lint.log: lint_corpus_digest_provenance.sh: clause 2 OK -- CORPUS_DIGEST_SUMMARY= matches the SHA-256 of the preceding listing.
lint.log: lint_corpus_digest_provenance.sh: clause 3 OK -- 'tests/golden/CORPUS_DIGEST_PROVISIONAL.txt' has zero entries, justified by a well-formed TRANSCRIBED-FROM-DESIGNATED-LEG marker: TRANSCRIBED-FROM-DESIGNATED-LEG: run=34776142545 job=103774491730 commit=196b52a68b05b0880c7c8b6335b345ea378f8219 date=2026-09-13
lint.log: lint_corpus_digest_provenance.sh: clause 4 RUN -- all 80 pre-existing line(s) from 8caf1f1 are present verbatim in 'tests/golden/CORPUS_DIGEST.txt'.
lint.log: lint_corpus_digest_provenance.sh: all clauses passed.
```

**Final line: `lint_corpus_digest_provenance.sh: all clauses passed.`**

### Perf ratchet output (verbatim, x64linux.log:8904-8914)

```
measure_timeline_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged.
measure_timeline_perf: instruction counts (valgrind --tool=cachegrind, D-13) -- plain=257624066 full=345832718 overhead_percent=34% (absolute PERF-03 ratio, reported every run per D-14) input=.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4 (120194289 bytes)
measure_timeline_perf: metric 'plain_instructions' within tolerance -- baseline=257709408, measured=257624066, change=0% (tolerance +/-2%).
measure_timeline_perf: metric 'full_instructions' within tolerance -- baseline=344956981, measured=345832718, change=0% (tolerance +/-2%).
```

**PERF-03 absolute overhead ratio: 34%** (printed every run per D-14, unchanged from 05-24's measurement).

Fractional deltas against the committed baseline:

| Metric | Baseline | Measured (this run, 35391084761) | Measured (05-24, run 35389474602) | Delta vs baseline this run |
|---|---|---|---|---|
| plain_instructions | 257709408 | 257624066 | 257624066 | -0.0331% |
| full_instructions | 344956981 | 345832718 | 345832699 | +0.2542% |

Both metrics measured identically to 05-24's run within 19 instructions on `full_instructions` (345832718 vs 345832699) and exactly on `plain_instructions` (257624066 both times) — this is the **second consecutive CI confirmation** the baseline is unchanged after the 05-19/05-22 gap-closure work, on code that is byte-identical between the two runs (the only diff in the push range was `.planning/` metadata).

### No `::error::` skip diagnostic emitted

```
$ grep -c "::error::" x64linux.log
8
$ grep -n "##\[error\]" x64linux.log
(no matches)
```

All 8 occurrences of the literal string `::error::` in x64linux.log are inside `Run`-step command-echo blocks (the `^[[36;1m...^[[0m` cyan-highlighted display of the shell script's own guard-clause source text, e.g. `echo "::error::a byte-exact fixture-derived golden test was SKIPPED..."`), never an actually-triggered GitHub Actions error annotation. The distinction is confirmed by the absence of any `##[error]` line anywhere in x64linux.log — `##[error]` is the literal prefix GitHub Actions emits for a real triggered error, and it appears zero times in this log (it does appear in x64osx.log and arm64linux.log, at the genuine failure points documented below).

### x64-osx failure (WINDOWS.md #14, confirmed unchanged)

```
x64osx.log:5089: ld: symbol(s) not found for architecture arm64
x64osx.log:5096: ##[error]Process completed with exit code 1.
```

Matches WINDOWS.md #14 exactly: the non-blocking, cross-built x86_64-from-arm64-host leg's known triplet/architecture link mismatch.

### arm64-linux failure — attribution corrected to WINDOWS.md #11

```
arm64linux.log:5729-5733 (Install mono for vcpkg's NuGet client (Linux), final lines):
  No containers need to be restarted.
  No user sessions are running outdated binaries.
  No VM guests are running outdated hypervisor (qemu) binaries on this host.
  (step completes with no error)

arm64linux.log:5798 (Register vcpkg NuGet feed (read-write, trusted runs only)):
  ##[error]Process completed with exit code 1.
```

**Correction of record:** 05-24-SUMMARY.md, and this plan's own orchestrator prompt before this executor verified the logs, attributed arm64-linux's failure to "no mono on arm64 runners." That claim is wrong. The `Install mono for vcpkg's NuGet client (Linux)` step completes cleanly with no error output at all (confirmed above). The actual failure is in the subsequent `Register vcpkg NuGet feed (read-write, trusted runs only)` step, which exits 1 — exactly WINDOWS.md #11's description: `'Register vcpkg NuGet feed (read-write, trusted runs only)' step exits 1, a credentials/infra problem`. This plan's own Task 1 checkpoint (completed in the earlier session) had flagged the mismatch between the two claims; this task resolves it by direct log inspection rather than deferring to either the prior SUMMARY or the orchestrator's restated correction.

## Task Commits

1. **Task 1: Local re-verification** — read-only, no repository files modified. Completed in an earlier session (see completed_tasks table in the resume prompt).
2. **Task 2: Push authorization and confirming-run evidence** — `checkpoint:decision`, `gate="blocking-human"`, resolved `push-all`. No repository files modified by this task; the orchestrator performed the single authorized `git push`, moving `8ad53f1..d40c040` and updating open PR #6. This executor then read the confirming run's logs directly.

**Plan metadata:** this docs commit (SUMMARY, STATE, ROADMAP, REQUIREMENTS).

## Files Created/Modified

None (repository files). This SUMMARY plus `.planning/STATE.md`, `.planning/ROADMAP.md`, and `.planning/REQUIREMENTS.md` are the only files this plan's docs commit touches.

## Decisions Made

See `key-decisions` in the frontmatter for the full rationale and verbatim reply. In short: the human chose `push-all` — a deliberate, acknowledged-redundant push to keep PR #6 in sync with the local branch, even though the confirming evidence would have been equally valid reading 05-24's own run had the human chosen `nothing-to-push` instead. `DOC-04` and `PERF-05` are marked `requirements-completed` here: this is the last of the two gap-closure plans declaring these shared IDs (05-24 deliberately left them unmarked, per its own Next Phase Readiness note, so the shared-ID readiness gate would wait for this plan). The `update_requirements` step's `requirements.ready-ids` call determines what actually flips in REQUIREMENTS.md — this plan does not force it.

## Deviations from Plan

None - plan executed exactly as written. Task 1's four commands all passed locally in the prior session; Task 2's checkpoint resolved to `push-all`, one of the four anticipated options, and every acceptance criterion (verbatim reply, revert-commit rules for `push-without-perf` — not applicable here since that option wasn't chosen, digest/golden/lint/perf-ratchet capture, non-designated-leg WINDOWS.md mapping, no `::error::` skip diagnostic) was met.

## Issues Encountered

None. The arm64-linux attribution mismatch flagged by Task 1's checkpoint (in the earlier session) was resolved during this task by direct log inspection, per the resume instructions — this is documented above as a correction of record, not an unresolved issue.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **Phase-level conclusion:** The designated leg is confirmed green on the final gap-closure state (head `d40c040`, code identical to `8ad53f1` outside `.planning/`), with every leg-only gate run: the digest assert, all five byte-exact goldens by name, the full provenance lint, and the perf ratchet with both PERF-03 ratios recorded. No `::error::` skip diagnostic ever fired. Every non-designated-leg failure is attributed to an existing WINDOWS.md entry (#14 for x64-osx, #11 for arm64-linux, attribution now corrected).
- **Perf baseline:** confirmed unchanged across two independent CI runs after the 05-19/05-22 gap-closure work (05-24's run 35389474602 and this plan's run 35391084761), both measuring within 0.26% of the committed baseline on both metrics. No baseline adoption decision was needed.
- This plan does not run phase verification or mark the phase complete — per the resume instructions, the orchestrator runs code review, the regression gate, and re-verification next.
- `DOC-04` and `PERF-05` are ready to mark complete now that both gap-closure plans declaring them (05-24, 05-25) have finished; the shared-ID readiness gate (`requirements.ready-ids`) decides the actual REQUIREMENTS.md update.

## Self-Check: PASSED

- `git log --oneline -5` at session start showed `d40c040` as HEAD, matching the plan's own predecessor commit — confirmed before any action.
- `git log origin/gsd/phase-05-timeline-analysis..HEAD` is empty (confirmed via `git status` at Task 2 start: origin already at `d40c040`) — the authorized push has already landed, nothing further to push.
- CI run 35391084761 / job 105749349968 (build x64-linux): FOUND and `success`, confirmed via the saved job log at the scratchpad path the orchestrator captured (`/tmp/.../scratchpad/ci-r5/x64linux.log`).
- Digest-assert line, five golden PASS lines, provenance-lint clause output, and perf-ratchet output: all re-read and quoted verbatim from the saved logs by this executor via `grep`, not merely relayed from the orchestrator's `state_of_the_world` context.
- `::error::` absence claim independently verified: 8 occurrences, all inside `Run`-step echo blocks; zero `##[error]` lines in x64linux.log.
- x64osx.log and arm64linux.log failure lines independently re-read and cross-checked against WINDOWS.md #14 and #11 respectively; the arm64-linux "no mono" claim was found to be incorrect on direct inspection and corrected here.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*
