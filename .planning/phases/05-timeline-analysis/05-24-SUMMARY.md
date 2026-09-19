---
phase: 05-timeline-analysis
plan: 24
subsystem: testing
tags: [corpus-digest, perf-baseline, provenance, designated-leg, ci, golden-files]

requires:
  - phase: 05-timeline-analysis (05-23)
    provides: narrowed-vocabulary documentation closure and the pre-existing empty CORPUS_DIGEST_PROVISIONAL.txt ledger this plan's pre-flight and capture rely on
provides:
  - "A local pre-flight (Task 1) that ran every designated-leg gate end to end with the expected outcome stated for each, before any push"
  - "An explicit blocking-human authorization (Task 2, reply push-branch) for the single push gsd/phase-05-timeline-analysis 78a023f..8ad53f1, which updated open PR #6 and triggered the designated x64-linux leg"
  - "Captured and human-verified evidence from CI run 35389474602, job 105744204442: the digest assert passed on 162 compared lines with the same three documented exclusions, all five byte-exact goldens ran by name and passed, and the perf ratchet passed on both metrics well within +/-2% tolerance"
  - "The explicit conclusion that no digest transcription and no PERF_BASELINE.txt change are needed: the provisional ledger was already empty and the ratchet passed, so this plan commits nothing beyond planning metadata"
affects: [05-25, phase-05-close, ship-phase-05]

actuals:
  tokens: 3500
  tasks: 3
  commits: 0

tech-stack:
  added: []
  patterns:
    - "designated-leg transcription (D-GAP-01 lineage, unchanged): a captured CI job log's own listing would be copied byte-for-byte into a committed golden file, never regenerated locally — this run required no transcription, which is itself the expected, checked outcome for an empty ledger"
    - "confirm-before-transcribe (05-13 precedent): the five golden test names, the absence of ::error:: skip diagnostics, and the perf step's measurement line are all confirmed directly against the job log text before any conclusion is drawn from a summary of it"

key-files:
  created: []
  modified: []

key-decisions:
  - "No CORPUS_DIGEST.txt transcription and no new TRANSCRIBED-FROM-DESIGNATED-LEG marker: tests/golden/CORPUS_DIGEST_PROVISIONAL.txt already held zero fixture names (only the three pre-existing markers) before this plan ran, and the designated leg's digest assert confirmed 162 compared lines with the same three documented exclusions (mkv_opus_a.webm, mkv_opus_b.webm, CORPUS_DIGEST_SUMMARY=). Neither golden file was touched."
  - "No PERF_BASELINE.txt change and no separate perf commit: the ratchet passed both metrics (plain change=0%, full change=0%, both reported at the 0% integer-percent granularity the script prints) well inside +/-2% tolerance. Per D-14/D-15 the committed baseline changes only by a human-reviewed decision, and none was needed here."
  - "This is the second CI cross-run observation of the transcribed PERF_BASELINE.txt values. 05-13's round 3 (run 35353739056) was the first and measured exactly equal (plain=257709408, full=344956981, change=0% on both). This run (35389474602) measured plain=257624066 (delta -85342, i.e. -85342/257709408 = -0.0331%) and full=345832699 (delta +875718, i.e. +875718/344956981 = +0.2539%) — both well under 0.3% movement after the 05-19/05-22 gap-closure work, so the +/-2% tolerance held with wide margin."
  - "git log origin/gsd/phase-05-timeline-analysis..HEAD is empty after Task 3 (matches the plan's own acceptance criterion for the no-transcription-needed path). The only commits landing after 8ad53f1 are this SUMMARY plus STATE.md/ROADMAP.md/REQUIREMENTS.md — none of them changes anything the designated leg asserts. This run (35389474602) is therefore itself the confirming run for 05-25 to acknowledge."

requirements-completed: []

coverage:
  - id: D1
    description: "Local pre-flight (Task 1) ran every designated-leg gate end to end with the expected outcome stated for each, entirely read-only, before any push"
    requirement: DOC-04
    verification:
      - kind: other
        ref: "scripts/check_corpus.sh, scripts/lint_corpus_digest_provenance.sh, scripts/assert_corpus_digest.sh, ctest --preset x64-linux, MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux — all run locally with recorded expected-vs-actual outcomes"
        status: pass
    human_judgment: false
  - id: D2
    description: "The single outward-facing push (gsd/phase-05-timeline-analysis, 78a023f..8ad53f1) happened only after an explicit blocking-human authorization (reply push-branch), and no other PR action was taken"
    verification:
      - kind: other
        ref: "checkpoint:decision Task 2, gate=blocking-human, resolved push-branch on 2026-09-18; orchestrator ran exactly one git push"
        status: pass
    human_judgment: true
    rationale: "T-05-95 (Elevation of Privilege) requires this be a human decision, not an auto-approved gate, even under yolo/auto-advance config — gate=blocking-human is never bypassed."
  - id: D3
    description: "The designated x64-linux leg's evidence (digest assert, five byte-exact goldens, perf ratchet, job table) is captured and confirmed by direct inspection of the job log text, not summarized secondhand"
    requirement: PERF-05
    verification:
      - kind: other
        ref: "CI run https://github.com/dkastsenich/mediadiff/actions/runs/35389474602, job 105744204442 — digest-assert line, five golden PASS lines, perf ratchet output all re-read and quoted verbatim from the saved job log by this executor"
        status: pass
    human_judgment: true
    rationale: "T-05-97 (Repudiation): a green run that skipped a leg-only gate would read like a pass in any summary. The plan's own <human-check> requires reading the log directly, which this executor did (see Evidence section) rather than trusting the orchestrator's relay alone."
  - id: D4
    description: "No transcription and no perf-baseline commit are needed, and this fact is recorded explicitly rather than left implicit"
    verification:
      - kind: other
        ref: "git log origin/gsd/phase-05-timeline-analysis..HEAD after Task 3 — empty"
        status: pass
    human_judgment: false

duration: ~20min (this continuation; Tasks 1-2 were completed in an earlier session per the orchestrator's continuation state)
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 24: Designated-Leg Evidence Capture (Gap Closure) Summary

**The designated x64-linux leg (CI run 35389474602, job 105744204442) confirmed the gap-closure work clean: digest assert passed on 162 compared lines, all five byte-exact goldens ran and passed, and the perf ratchet held both metrics within 0.26% of the transcribed baseline — so this plan transcribes nothing and commits no perf-baseline change.**

## Performance

- **Duration:** ~20 min this continuation (Tasks 1-2 — local pre-flight and the human's push authorization — were completed in an earlier session; see Task Commits below).
- **Tasks:** 3 of 3.
- **Files modified:** 0 repository files (Task 3 concluded no transcription and no perf commit were needed; only planning metadata — this SUMMARY, STATE.md, ROADMAP.md — is committed by this plan).

## Accomplishments

- **Task 1 (prior session):** Local pre-flight ran every designated-leg gate (`check_corpus.sh`, `lint_corpus_digest_provenance.sh`, `assert_corpus_digest.sh`, both ctest presets) end to end, entirely read-only, with expected outcomes stated for each. All green.
- **Task 2 (prior session, human decision):** The human replied `push-branch` at the blocking-human checkpoint. The orchestrator ran exactly one `git push origin gsd/phase-05-timeline-analysis`, moving `78a023f..8ad53f1`. This updated the already-open PR #6; no other PR action was taken.
- **Task 3 (this continuation):** Captured and verified the designated leg's evidence from CI run 35389474602, job 105744204442 (event `pull_request`, head `8ad53f12acfaa7260d55e6e5dffe174f67b23669`, run conclusion `success`). Confirmed:
  - The provisional ledger (`tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`) already held zero fixture names — only the three pre-existing `TRANSCRIBED-FROM-DESIGNATED-LEG` markers — so no ledger read was needed to know a transcription would be a no-op; the digest-assert step's own output confirms the same 162-line, 3-exclusion shape as every prior green run.
  - No `::error::` skip diagnostic fired on either guard (the byte-exact-golden skip guard or the perf-step no-output guard) — the eight `::error::` string occurrences in the log are all literal shell-script text inside `Run` command echoes of the guard clauses themselves, never an emitted diagnostic.
  - The perf ratchet printed a real measurement and passed both metrics within tolerance.
  - Non-designated-leg outcomes: x64-osx and arm64-linux failed, both pre-existing infra issues (Apple Silicon cross-link, no mono on arm64 runners) already recorded and out of this plan's scope; lint, Windows and arm64-osx all succeeded.
  - Concluded, per the plan's own decision rule: ledger empty + ratchet passed = no transcription, no perf commit. `git log origin/gsd/phase-05-timeline-analysis..HEAD` is empty.

## Evidence (verbatim from the job log, verified directly by this executor)

**Run:** https://github.com/dkastsenich/mediadiff/actions/runs/35389474602
**Job:** `build (x64-linux)`, job id `105744204442`
**Head SHA:** `8ad53f12acfaa7260d55e6e5dffe174f67b23669` (full 40-hex; this is the real branch head)
**Checked-out (merge-ref) SHA:** `fb90ce6` — the ephemeral `refs/pull/6/merge` commit GitHub creates for `pull_request` runs, not a branch commit. Per 05-13's precedent, the ratchet step's printed `commit=` field is informational only; the durable name for what was measured is the branch head, `8ad53f1`.

### Job results table

| Job | Result |
|---|---|
| lint (ENG-16 boundary) | success |
| build (x64-linux) — **designated leg**, job 105744204442 | success |
| build (x64-windows-static-md) | success |
| build (arm64-osx) | success |
| build (x64-osx) | failure — pre-existing infra (Apple Silicon cross-link), out of scope |
| build (arm64-linux) | failure — pre-existing infra (no mono on arm64 runners), out of scope |

### Digest assert (D-GAP-01), self-test + comparison lines

```
assert_corpus_digest.sh: self-test OK -- known-good (Opus+summary-only diff) passed, non-vacuity control (non-Opus diff) failed, count-guard control (excluded count != 3) failed.
assert_corpus_digest.sh: compared 162 line(s); did not compare: the mkv_opus_a.webm line, the mkv_opus_b.webm line, the CORPUS_DIGEST_SUMMARY= line.
```

Matches every prior designated-leg run's shape exactly (162 compared, 3 named exclusions — the WINDOWS.md #22/#24-waived libopus cross-host nondeterminism). No line in `tests/golden/CORPUS_DIGEST.txt` needed replacement.

### The five byte-exact golden tests (by name, all Passed)

```
248/994 Test #248: unit.inspect_container - golden: the container+meta section for one representative fixture per family ... Passed
675/994 Test #675: unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden ... Passed
676/994 Test #676: unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden ... Passed
677/994 Test #677: unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden ... Passed
937/994 Test #937: integration.size_checks - the size.* findings are pinned by a committed, read-only golden ... Passed
```

Test summary: `100% tests passed, 0 tests failed out of 994`. The one skip (`118 - unit.console_vt`) is the pre-existing, Windows-only, documented exclusion — not one of the five byte-exact goldens.

No `::error::` skip diagnostic appears anywhere in the log as an actually-emitted line (the eight matches for the literal string `::error::` are all inside `Run` command display blocks — the shell script's own guard-clause source text being echoed by GitHub Actions' command logging, never a triggered error output).

### Perf ratchet output (verbatim)

```
measure_timeline_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged.
measure_timeline_perf: instruction counts (valgrind --tool=cachegrind, D-13) -- plain=257624066 full=345832699 overhead_percent=34% (absolute PERF-03 ratio, reported every run per D-14) input=.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4 (120194289 bytes)
measure_timeline_perf: metric 'plain_instructions' within tolerance -- baseline=257709408, measured=257624066, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=plain_instructions value=257624066 commit=fb90ce6
measure_timeline_perf: metric 'full_instructions' within tolerance -- baseline=344956981, measured=345832699, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=full_instructions value=345832699 commit=fb90ce6
```

The script prints `change=0%` at integer-percent granularity for both; the exact fractional deltas against the committed baseline are:

| Metric | Baseline (05-13, still committed) | Measured (this run) | Delta | Delta as fraction of baseline |
|---|---|---|---|---|
| plain_instructions | 257709408 | 257624066 | -85342 | -85342/257709408 = -0.0331% |
| full_instructions | 344956981 | 345832699 | +875718 | +875718/344956981 = +0.2539% |

Both are well inside the +/-2% ratchet tolerance, and both are under 0.3% movement — a small, expected drift after the 05-19 (per-interval jitter/vfr_profile work on every input) and 05-22 (possible av_sync mapping change) gap-closure work, not a regression requiring a new baseline. This is the **second** independent CI cross-run observation of the transcribed baseline (05-13's round 3, run 35353739056, was the first and measured exactly equal on both metrics with zero drift).

## Task Commits

1. **Task 1: Local pre-flight** — read-only, no repository files modified. Completed in an earlier session; results reported in this continuation's prompt context.
2. **Task 2: Push authorization** — `checkpoint:decision`, `gate="blocking-human"`, resolved `push-branch`. No repository files modified by this task; the orchestrator performed the single authorized `git push`, moving `78a023f..8ad53f1` and updating open PR #6.
3. **Task 3: Capture and (conditional) transcription** — no repository files modified. The ledger was already empty and the ratchet passed, so neither `tests/golden/CORPUS_DIGEST.txt` nor `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` nor `tests/golden/PERF_BASELINE.txt` was touched, per the plan's own decision rule ("If the ledger is empty and the ratchet passes, this plan commits nothing").

**Plan metadata:** this docs commit (SUMMARY, STATE, ROADMAP; REQUIREMENTS.md untouched — see Decisions Made).

## Files Created/Modified

None (repository files). This SUMMARY plus `.planning/STATE.md` and `.planning/ROADMAP.md` are the only files this plan's docs commit touches.

## Decisions Made

See `key-decisions` in the frontmatter for the full rationale. In short: the provisional ledger was already empty (no `timeline_drift_step.mp4` or any other name to transcribe) and the perf ratchet passed both metrics with wide margin, so per the plan's own stated rule this plan commits no transcription and no perf-baseline change. `DOC-04`/`PERF-05` are left `requirements-completed: []` here deliberately — 05-25 shares both IDs, and the shared-ID readiness gate (`requirements.ready-ids`) reports `0/2 requirement(s) ready to mark complete` until 05-25 also finishes; this plan does not force it.

## Deviations from Plan

None - plan executed exactly as written. The plan's own flagged assumption A1 ("the provisional ledger holds zero or one name... only 05-22 may add `timeline_drift_step.mp4`") resolved to the zero-name branch, and A2 ("the perf ratchet may fail") resolved to pass-with-margin — both anticipated outcomes, not deviations.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- This plan's job (evidence capture and conditional transcription) is done. `git log origin/gsd/phase-05-timeline-analysis..HEAD` is empty after Task 3: the only commits landing after `8ad53f1` are this plan's own planning-metadata commit (SUMMARY, STATE, ROADMAP; REQUIREMENTS.md unaffected) and 05-25's own commits later — none of them changes anything the designated leg asserts.
- **05-25 can acknowledge this run (35389474602, job 105744204442) as the confirming run.** 05-25's Task 1 will find nothing to push (its own re-verification of the current local state should pass identically to Task 1 here, since no golden file changed), and its Task 2 checkpoint should select the `nothing-to-push` option rather than authorizing a second push.
- `DOC-04` and `PERF-05` stay `Gaps Found` in REQUIREMENTS.md until 05-25 finishes and the shared-ID gate reports both ready.

## Self-Check: PASSED

- `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`, `tests/golden/PERF_BASELINE.txt`: all FOUND on disk, unchanged (git diff empty for all three against `HEAD`)
- `git log origin/gsd/phase-05-timeline-analysis..HEAD`: empty, confirmed
- CI run 35389474602 / job 105744204442: FOUND and `success`, confirmed via the saved job log at the scratchpad path the orchestrator captured
- Digest-assert line, five golden PASS lines, and perf-ratchet output: all re-read and quoted verbatim from the saved log by this executor, not merely relayed from the orchestrator's context

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*
