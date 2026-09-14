---
phase: 04-video-analysis
plan: 21
subsystem: testing
tags: [ci, corpus-digest, designated-leg, github-actions, gh, transcription]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-20 published the branch, opened draft PR #5 and captured the designated leg's verbatim listing (run 34776142545, job 103774491730)"
provides:
  - "`tests/golden/CORPUS_DIGEST.txt` is the designated x64-linux leg's own listing, verbatim (138 fixture lines + CORPUS_DIGEST_SUMMARY), with all 80 pre-existing lines from main 8caf1f1 intact"
  - "`tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` holds zero fixture entries, justified by a `# TRANSCRIBED-FROM-DESIGNATED-LEG:` marker naming the run, job and commit it was cleared by"
  - "The corrected digest was pushed only after an explicit human confirmation obtained immediately beforehand, and the designated leg was observed GREEN on every run since, with the five designated-leg-only goldens PASSED"
  - "The x64-windows-static-md leg's corpus-generation failures were driven to root cause across four CI runs and fixed by quick tasks (pin-reader CRLF, BtbN purge re-pin to the ffmpeg-pins mirror, GPL-only interlacing filters)"
affects: [ci, corpus-digest, phase-4-closure]

# Actuals (#2632)
actuals:
  tokens: 45000
  tasks: 3
  commits: 1

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Blocking-human checkpoints run in the orchestrator's main context (execute-plan Pattern B segmented): the transcription executed autonomously, the push and the green-leg verification were confirmed by the human directly"
    - "Provisional-ledger emptiness is justified mechanically: `lint_corpus_digest_provenance.sh` clause 3 accepts a zero-entry ledger only when a well-formed TRANSCRIBED-FROM-DESIGNATED-LEG marker names run/commit"

key-files:
  created:
    - .planning/phases/04-video-analysis/04-21-SUMMARY.md
  modified:
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - scripts/lint_corpus_digest_provenance.sh

key-decisions:
  - "Rule 3 deviation in Task 1: the plan's literal instruction (empty the ledger) contradicted lint clause 3 (a ledger must carry entries); resolved by letting clause 3 accept a zero-entry ledger only when justified by a run/commit-bearing marker line, verified positively and by a negative control"
  - "Windows was fixed rather than documented as an exception: the human chose 'fix windows first' and then each successive fix, so the blocking-leg rule of Task 3 was honoured instead of waived"

patterns-established:
  - "Never point a corpus-generator pin at a BtbN autobuild asset directly; mirror it on the repo's ffmpeg-pins prerelease"
  - "Fixture recipes must use only LGPL-ungated ffmpeg filters (FFmpeg configure `*_filter_deps=\"gpl\"` list), because the Windows generator is an LGPL build"

requirements-completed: [BUILD-05, BUILD-08]

# Coverage (#2632)
coverage:
  decisions: [D-GAP-01, D-GAP-04]

# Metrics
duration: multi-session
completed: 2026-09-14
---

# Phase 4 Plan 21: Transcribe the designated leg's digest, push it, watch the leg go green — Summary

**Designated-leg digest transcribed verbatim, provisional ledger emptied with mechanical justification, pushed after confirmation, and the x64-linux leg observed green (771/771 with the five designated-leg goldens PASSED) on every run since — while the Windows leg's corpus generation was driven from red to green across four root-caused failures.**

## Performance

- Task 1 executed by a gsd-executor segment; Tasks 2 and 3 handled in the orchestrator's main context with the human at every gate.
- Commits: 767c6d0 (Task 1). Pushes: effeae7 (Task 2), then 50a8391, 0a2de17, 0c43cf8 and b52ca4b for the Windows fixes (each confirmed separately).

## Accomplishments

- `tests/golden/CORPUS_DIGEST.txt` is byte-identical to 04-20's captured x64-linux listing (138 lines + `CORPUS_DIGEST_SUMMARY=acb4cd4f328023f355895ef69068a9bd5a52d1196a663974ca961d1ee92a4372`); clause 4 of the provenance lint confirms all 80 pre-existing lines from main `8caf1f1` are present verbatim.
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` carries zero fixture entries; line 32 records `# TRANSCRIBED-FROM-DESIGNATED-LEG: run=34776142545 job=103774491730 commit=196b52a68b05b0880c7c8b6335b345ea378f8219 date=2026-09-13`.
- The digest was pushed once (effeae7) after the human typed "push the digest"; the x64-linux assert step passed on that run and every later one.

## Task Commits

1. **Task 1: Transcribe the designated leg's listing** — `767c6d0` (fix): 3 files, +128/−146 — CORPUS_DIGEST.txt, CORPUS_DIGEST_PROVISIONAL.txt, lint_corpus_digest_provenance.sh (clause 3 marker rule).
2. **Task 2: Confirm, then push** — no commit; push of `effeae7` at 2026-09-14T15:50:34+02:00.
3. **Task 3: Confirm the designated leg is green** — no commit; observation recorded below.

## Human confirmations (verbatim, in order)

| Gate | Human's words | Action taken |
|------|---------------|--------------|
| Task 2 push (first asked) | "fix windows first" | push deferred; quick task 260913-wuy fixed the pin reader's CRLF handling |
| Task 2 push | "push the digest" | `git push` of effeae7 → run 34864822708 |
| Task 3 verdict | "designated leg confirmed, re-pin windows via a mirrored release" | Task 3 designated-leg step accepted; Windows route chosen |
| mirror creation | "create the mirror" | prerelease `ffmpeg-pins` created on main, asset uploaded |
| push | "push the re-pin" | push of 50a8391 → run 34878514356 |
| push | "push the interlace fix" | push of 0a2de17 → run 34882668138 |
| push | "push the lgpl chain" | push of 0c43cf8 at 2026-09-14T19:25:06Z → run 34886767317 |
| push | "push the getenv fix" | push of b52ca4b at 2026-09-14T20:08:01Z → run 34891069554 |

## Task 1: transcription evidence

- Source: the VERBATIM block in 04-20-SUMMARY.md (run 34776142545, job 103774491730, commit 196b52a). `scripts/gen_corpus.sh` and `scripts/corpus_digest.sh` were never run into the committed file.
- `bash scripts/lint_corpus_digest_provenance.sh`: clauses 1–4 OK (clause 4: "all 80 pre-existing line(s) from 8caf1f1 are present verbatim").
- Negative control for the clause 3 rule: with the marker line removed the lint exits 1; restored, it passes.
- Local `bash scripts/assert_corpus_digest.sh` exits 1 by design (the file holds CI-runner bytes; the workstation ffmpeg produces different bytes).

## Task 3: CI observations (x64-linux is the designated leg)

| Run | Head | lint | x64-linux | arm64-osx | x64-windows-static-md | x64-osx | arm64-linux |
|-----|------|------|-----------|-----------|------------------------|---------|-------------|
| 34864822708 | effeae7 | success | success | success | failure (pinned download 404: BtbN purged autobuild-2026-09-02-13-13) | failure (pre-existing) | failure (pre-existing) |
| 34878514356 | 50a8391 | success | success | success | failure (`No option name near 'interleave_top'`: tinterlace is GPL-only) | failure (pre-existing) | failure (pre-existing) |
| 34882668138 | 0a2de17 | success | success | success | failure (`No such filter: 'interlace'`: interlace is GPL-only too) | failure (pre-existing) | failure (pre-existing) |
| 34886767317 | 0c43cf8 | success | success | success | failure (corpus generation and check_corpus now PASS; Build: `test_golden.cpp(112)` C4996 `getenv` under /W4 /WX) | failure (pre-existing) | failure (pre-existing) |
| 34891069554 | b52ca4b | success | success | success | success (install, corpus, check, digest assert, Build, Test 766/766, PowerShell cross-check) | failure (pre-existing, non-blocking) | failure (pre-existing, non-blocking) |

x64-osx ("Build") and arm64-linux ("Register vcpkg NuGet feed") fail identically on main's latest run 34353206899 and are not among this plan's blocking legs.

Designated leg, run 34882668138 (job 104105514613), quoted from the log:

```
12. Assert the corpus digest matches the committed pin (D-GAP-01): success
assert_corpus_digest.sh: compared 136 line(s); did not compare: the mkv_opus_a.webm line, the mkv_opus_b.webm line, the CORPUS_DIGEST_SUMMARY= line.
158/771 Test #158: unit.inspect_container - golden: the container+meta section for one representative fixture per family ...   Passed    0.01 sec
490/771 Test #490: unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden ...   Passed    0.00 sec
491/771 Test #491: unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden ...   Passed    0.01 sec
492/771 Test #492: unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden ...   Passed    0.00 sec
751/771 Test #751: integration.size_checks - the size.* findings are pinned by a committed, read-only golden ...   Passed    0.01 sec
100% tests passed, 0 tests failed out of 771
```

The same five goldens were PASSED and the assert step succeeded on runs 34864822708, 34878514356 and 34886767317 as well (run 34886767317, job 104119230993: assert step success, `CORPUS_DIGEST_SUMMARY=acb4cd4f…4372` unchanged after the LGPL recipe swap, 100% of 771 tests passed).

Run 34891069554 (head b52ca4b) is the closing observation. Its overall conclusion is `success`: the two red legs are declared non-blocking in `ci.yml` (`continue-on-error: ${{ !matrix.blocking }}`), matching Task 3's blocking set of lint, x64-linux, arm64-osx and x64-windows-static-md.

Designated leg, run 34891069554 (job 104133597041), quoted from the log:

```
12. Assert the corpus digest matches the committed pin (D-GAP-01): success
CORPUS_DIGEST_SUMMARY=acb4cd4f328023f355895ef69068a9bd5a52d1196a663974ca961d1ee92a4372
158/771 Test #158: unit.inspect_container - golden: the container+meta section for one representative fixture per family ...   Passed
490/771 Test #490: unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden ...   Passed
491/771 Test #491: unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden ...   Passed
492/771 Test #492: unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden ...   Passed
751/771 Test #751: integration.size_checks - the size.* findings are pinned by a committed, read-only golden ...   Passed
100% tests passed, 0 tests failed out of 771
```

Windows leg, run 34891069554 (job 104133597199), quoted from the log:

```
8. Install the pinned ffmpeg build (D-GAP-01): success
9. Generate media fixture corpus (BUILD-08 / D-08): success
10. Verify the fixture corpus is complete: success
12. Assert the corpus digest matches the committed pin (D-GAP-01): success
23. Build: success
24. Test: success
26. PowerShell corpus generator version-gate and manifest-order cross-check (Windows, owed to Plan 04): success
check_corpus.sh: clean. Verified 138 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
CORPUS_DIGEST_SUMMARY=acb4cd4f328023f355895ef69068a9bd5a52d1196a663974ca961d1ee92a4372
100% tests passed, 0 tests failed out of 766
```

The Windows generator (BtbN win64-lgpl 9.0.1 from the `ffmpeg-pins` mirror) therefore produced the same 138-fixture digest as the Linux GPL generator, byte for byte, and MSVC compiled and ran the whole branch (766 registered tests; the five designated-leg goldens are not registered there).

## Findings outside this plan's scope, and how they were closed

1. **Pin reader rejected CRLF python3 output on the Windows runner** (run 34776142545) → quick task 260913-wuy (commits 4f351b4, 6efed2e, 5034c7c): CR stripped per line, empty-output guard, offending first line named; regression cases 8–11 in `scripts/test_gen_corpus_pin_gate.sh`.
2. **BtbN autobuild asset purged (404)** (run 34864822708, also broke main) → prerelease `ffmpeg-pins` on main mirrors `ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0.zip` (sha256 084874559d…3bf58e); quick task 260914-qlk (3a5ca95) re-pinned `scripts/ffmpeg_pin.json`.
3. **`tinterlace` is GPL-only** (run 34878514356) → quick task 260914-ryu (bf9a42f) swapped to `interlace=…:lowpass=off`, byte-identical.
4. **`interlace` is GPL-only as well** (run 34882668138; FFmpeg n9.0.1 configure: `interlace_filter_deps="gpl"`) → quick task 260914-t47 (d137242): LGPL chain `setparams,separatefields,select,weave` + `-r 25/2`, byte-identical (sha256 equal for tff, bff and seg_a on the workstation ffmpeg).
5. **MSVC C4996 on a raw `std::getenv`** (run 34886767317, the first time this branch's C++ reached MSVC; `tests/unit/test_golden.cpp:112`, added by 35db578) → quick task 260914-tzq (dd5be50, 47f02c4): read through `mediadiff::getenv_utf8` (the single permitted accessor, src/util/fs.h) and a new `scripts/lint_getenv_shim.sh` in the lint job so no raw accessor call can land again.

## Deviations from Plan

- **Task 1, Rule 3 (blocking contradiction):** the plan required an emptied ledger while lint clause 3 required entries. Resolved by the marker rule described above; the plan's must-have ("zero remaining provisional entries … records the run id and commit sha") is satisfied literally.
- **Task 3 took four CI runs instead of one** because the Windows blocking leg failed for four different reasons; the human chose to fix each rather than waive the leg.

## Issues Encountered

- `gh run view --log` refuses while sibling jobs are still running; job logs were fetched through the jobs-logs REST endpoint with ANSI/CR stripping.

## User Setup Required

None.

## Next Phase Readiness

- Every blocking leg is green on the branch head b52ca4b (run 34891069554), the digest is designated-leg provenance end to end, and the provisional ledger is empty with a mechanical justification. Phase 4 can proceed to verification.
- x64-osx (`Build`) and arm64-linux (`Register vcpkg NuGet feed`) stay red exactly as on main's latest run 34353206899; they are non-blocking by the matrix definition and belong to a separate fix.
- Draft PR #5 remains a draft; merging is the human's call after phase verification.

## Self-Check: PASSED

- tests/golden/CORPUS_DIGEST.txt byte-identical to the captured listing: FOUND
- tests/golden/CORPUS_DIGEST_PROVISIONAL.txt zero entries + marker line 32: FOUND
- commit 767c6d0: FOUND
- pushes effeae7 / 50a8391 / 0a2de17 / 0c43cf8 / b52ca4b on origin: FOUND
