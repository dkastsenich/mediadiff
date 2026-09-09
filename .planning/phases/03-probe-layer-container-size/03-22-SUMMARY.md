---
phase: 03-probe-layer-container-size
plan: 22
subsystem: testing
tags: [ci, ffmpeg-determinism, github-actions-path, corpus-digest, ledger-reconciliation, gap-closure]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: 03-21's three-blocking-legs-reach-Test precondition (WINDOWS.md #13/#16 closed, all three blocking legs Build=success/Test!=skipped)
provides:
  - Real CI run 34023871831 (head 64bc168) in which all three blocking legs AND lint conclude success at the job level, closing ROADMAP SC5 on observed evidence
  - Two newly-exposed blocking-leg defects found and fixed within this plan's own 3-round-trip budget (a Windows GITHUB_PATH format bug, and the arm64-osx doc03_coverage bitrate-margin failure WINDOWS.md #20 left open from 03-21)
  - One newly-exposed, deliberately-unfixed defect (WINDOWS.md #22): x64-linux's own fixture generation is not run-to-run reproducible on the same commit/pinned ffmpeg
  - TRUST-06's REQUIREMENTS.md status reconciled: both sites already read Complete/checked, and this run is the first evidence in this phase's history that makes that assertion true rather than aspirational
affects: [any future ffmpeg-pin bump, any future CI workflow change touching install_pinned_ffmpeg.sh or the designated-leg digest policy]

# Actuals (#2632)
actuals:
  tokens: 5330
  tasks: 2
  commits: 5

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "cygpath -w conversion at the GITHUB_PATH write site, gated on `command -v cygpath`, to bridge Git-Bash-resolved MSYS paths into a native-Windows-path form consumable by a later pwsh-shell step in the same job"

key-files:
  created: []
  modified:
    - scripts/install_pinned_ffmpeg.sh
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - .planning/WINDOWS.md

key-decisions:
  - "Used run 34023871831 (head 64bc168) as the closing evidence run, not the two earlier candidate runs the orchestrator identified (34021508083 head dfc9e8d, and 34022461121 head 6b57c2a) -- both of those, on direct inspection, failed Task 1's own literal job-level verify command: 34021508083's x64-windows-static-md job conclusion was `failure` (a then-undiscovered PowerShell-step PATH bug, WINDOWS.md #21) despite Build/Test both succeeding, and 34022461121's x64-linux job conclusion was `failure` (a digest mismatch confined to mkv_opus_a/b.webm, WINDOWS.md #22) despite an unrelated docs-only diff from dfc9e8d. Neither run could have honestly closed SC5 under the plan's own literal verify commands; fixing #21 and #20 (see below) and pushing twice more produced a run where all three blocking legs' own job-level conclusions read success."
  - "Fixed WINDOWS.md #20 (arm64-osx doc03_coverage stream_bitrate margin) inside this plan despite the plan's declared files_modified naming only WINDOWS.md/REQUIREMENTS.md, because Task 1's own <files> field explicitly anticipates 'whichever source or test file a failing Test step names' and its own <action> instructs diagnosing and fixing any blocking-leg Test failure within the 3-round-trip budget. Applied 03-21's own already-drafted fix (700k/715k bitrates) and regenerated tests/golden/CORPUS_DIGEST.txt's size_near_b.mp4 entry from real x64-linux CI output (run 34023549561), never derived locally, per D-GAP-01's established procedure."
  - "Did not attempt to fix WINDOWS.md #22 (x64-linux's own corpus-digest non-reproducibility, discovered mid-round on mkv_opus_a/b.webm) -- diagnosing libopus's encode determinism is a genuinely separate, open-ended investigation outside this plan's 3-round-trip budget and declared scope; recorded with both runs' exact hash values so a future round can reproduce and isolate it."
  - "TRUST-06's REQUIREMENTS.md status required no text edit: both carrying sites already read Complete/checked from a prior round, and this plan's own evidence is what first makes that assertion true rather than unsupported -- recorded as a decision (evidence, not edit) rather than left silently unchanged."

requirements-completed: [TRUST-06, TRUST-09, DOC-03]

coverage:
  - id: D1
    description: "One real CI run (34023871831, head 64bc168) shows all three blocking legs (x64-linux, arm64-osx, x64-windows-static-md) and the required lint job concluding success at the job level, closing ROADMAP SC5's CI-release-blocker claim on observed evidence."
    requirement: TRUST-06
    verification:
      - kind: e2e
        ref: "gh run view 34023871831 --json jobs (four legs != success count == 0)"
        status: pass
      - kind: e2e
        ref: "gh run view 34023871831 --log --job <x64-linux>: 2 trust06_idempotence Passed lines; --job <arm64-osx>: 2 trust06_idempotence Passed lines"
        status: pass
      - kind: e2e
        ref: "gh run view 34023871831 --json jobs: build (x64-windows-static-md) Test step conclusion == success"
        status: pass
    human_judgment: false
  - id: D2
    description: "x64-linux's designated-leg gates (DOC-03 coverage, TRUST-09 TSDuck-derived ts_scan_golden) observed Passed in the same closing run."
    requirement: DOC-03
    verification:
      - kind: e2e
        ref: "gh run view 34023871831 --log --job <x64-linux>: 3 ts_scan_golden + 2 doc03_coverage Passed lines (>=4 required)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Two newly-exposed blocking-leg defects (Windows GITHUB_PATH format bug; arm64-osx doc03_coverage margin, WINDOWS.md #20) diagnosed and fixed within budget; ledger entries #13/#16/#18/#19/#20/#21 all read fixed with cited observed evidence and a resolved_at, in both the markdown table and the JSON array."
    verification:
      - kind: other
        ref: "node gsd-tools.cjs windows status (exit 0, total==22 matching both representations)"
        status: pass
      - kind: other
        ref: "python3 ledger-disposition check: 13/16 fixed+resolved_at, 11/14/17 open"
        status: pass
    human_judgment: false
  - id: D4
    description: "WINDOWS.md #22 (x64-linux's own corpus-digest run-to-run non-reproducibility on mkv_opus_a/b.webm) is a genuinely new, unresolved finding that threatens the evidentiary basis of the designated-leg byte-exact policy (WINDOWS.md #17) beyond this phase's prior understanding of WINDOWS.md #12 (cross-architecture-only variance)."
    verification: []
    human_judgment: true
    rationale: "Whether this is tolerable status quo, needs libopus-encoder investigation, or needs the byte-exact assertion narrowed to exclude Opus fixtures is a scoping/priority call for a human or a future round, not something this plan's own evidence can resolve -- it was discovered, not caused, by this plan, and fixing it is explicitly out of the plan's declared files_modified."

duration: 41min
completed: 2026-09-06
status: complete
---

# Phase 3 Plan 22: ROADMAP SC5 closed on observed CI evidence -- two more blocking-leg defects found and fixed within budget

**Real CI run 34023871831 (head 64bc168) shows all three blocking legs and lint concluding success at the job level for the first time in this phase's history -- reached only after discovering and fixing a Windows GITHUB_PATH format bug and finishing 03-21's own already-diagnosed arm64-osx bitrate-margin fix, using exactly the plan's full 3-round-trip budget.**

## Performance

- **Duration:** 41 min
- **Started:** 2026-09-06T08:40:00Z
- **Completed:** 2026-09-06T09:21:00Z
- **Tasks:** 2
- **Files modified:** 4 (`scripts/install_pinned_ffmpeg.sh`, `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST.txt`, `.planning/WINDOWS.md`) -- beyond the plan's own declared `files_modified` (`.planning/WINDOWS.md`, `.planning/REQUIREMENTS.md`), per Task 1's own `<files>` clause explicitly anticipating "whichever source or test file a failing Test step names"

## Accomplishments

- Chose real CI run **34023871831** (head `64bc168`) as the closing evidence run, after determining on direct inspection that neither run the orchestrator identified as a starting candidate (`34021508083` head `dfc9e8d`, `34022461121` head `6b57c2a`) actually satisfied Task 1's own literal job-level verify command -- each had a genuine, previously-unrecorded blocking-leg defect hiding behind Build/Test both succeeding.
- Found and fixed **WINDOWS.md #21**: `install_pinned_ffmpeg.sh` appended an MSYS-style POSIX path (Git Bash's `pwd -P`) to `GITHUB_PATH`; every bash-invoked later step resolved it fine (which is why Build/Test both succeeded), but the Windows-only "PowerShell corpus generator version-gate" step spawns a native `pwsh` child that cannot resolve a path with no drive-letter prefix, so `& ffmpeg` failed with "was not found" and the whole `build (x64-windows-static-md)` job read `failure` despite Build and Test both concluding `success`. Fixed by converting through `cygpath -w` when available (a no-op on Linux/macOS, verified locally).
- Finished **WINDOWS.md #20** (already fully diagnosed by 03-21, deferred there for a dedicated round trip): widened `size_near_a.mp4`/`size_near_b.mp4`'s bitrate margin from 700k/730k (~2.7% measured, only ~0.3 points under the 3% warn bound) to 700k/715k (~1.0% measured locally), then regenerated `tests/golden/CORPUS_DIGEST.txt`'s `size_near_b.mp4` entry and `CORPUS_DIGEST_SUMMARY` from real x64-linux CI output (run `34023549561`), never derived locally, per D-GAP-01's established procedure.
- Found, recorded, and deliberately left open **WINDOWS.md #22**: x64-linux's own designated-leg corpus digest is not run-to-run reproducible on the identical commit and pinned ffmpeg binary -- `mkv_opus_a.webm`/`mkv_opus_b.webm` differed byte-for-byte between runs `34021508083` and `34022461121` (a docs-only diff between their two heads, confirmed via `git diff --name-only`). This is a genuinely new discovery beyond WINDOWS.md #12's previously-understood cross-architecture-only SIMD variance, and it did not recur in the two subsequent runs this plan pushed.
- Observed and quoted all evidence ROADMAP SC5 and this plan's own must-haves require: four `trust06_idempotence` `Passed` lines (two per named leg), the `x64-windows-static-md` `Test` step concluding for the first time ever observed in this phase's CI history, DOC-03/TRUST-09 gates passing on the designated leg, and per-leg `check_corpus.sh: clean` lines.
- Closed WINDOWS.md #20 and #21 on cited observed evidence from the closing run; confirmed #13/#16 (already closed by 03-21) and #11/#14/#17 (still open, non-blocking/accepted-limitation) are correctly dispositioned; left #22 open with its evidence recorded.
- Confirmed REQUIREMENTS.md's TRUST-06 status needed no text change: both carrying sites already read `Complete`/checked from a prior round, and this plan's evidence is the first in this phase's history to make that assertion actually supported.

## Task Commits

Each task was committed atomically:

1. **Task 1 (round 1 of 3): fix WINDOWS.md #21 (Windows GITHUB_PATH format bug)** - `3b4ecd0` (fix)
2. **Task 1 (round 2 of 3): fix WINDOWS.md #20 (bitrate-margin widening)** - `2a5ce3b` (fix)
3. **Task 1 (round 3 of 3): regenerate CORPUS_DIGEST.txt from real CI output** - `64bc168` (fix)
4. **Task 2: ledger reconciliation (close #20/#21, confirm TRUST-06)** - `b814fa5` (docs)

**Plan metadata:** (this commit)

_Round trips consumed: 3 of the 3-round-trip budget allotted to Task 1 -- all three used, evidence completed within budget._

## Files Created/Modified

- `scripts/install_pinned_ffmpeg.sh` - Converts the `GITHUB_PATH` entry through `cygpath -w` when available, so the Windows-only pwsh cross-check step can resolve the pinned ffmpeg's directory
- `scripts/gen_corpus.sh` - `size_near_b.mp4`'s target bitrate widened from 730k to 715k for a safer clean-pair margin
- `tests/golden/CORPUS_DIGEST.txt` - `size_near_b.mp4`'s hash and `CORPUS_DIGEST_SUMMARY` regenerated from real x64-linux CI output (run 34023549561)
- `.planning/WINDOWS.md` - Recorded #21/#22 (new), closed #20/#21 on cited observed evidence; #13/#16/#18/#19 confirmed already closed; #11/#14/#17 confirmed still open

## CI Evidence

### Evidence run: [34023871831](https://github.com/dkastsenich/mediadiff/actions/runs/34023871831), head `64bc168684fac34e649f16a18f6a4c736ec192e4`

`gh run view 34023871831 --json status,conclusion,headSha`:
```
status=completed, conclusion=success, headSha=64bc168684fac34e649f16a18f6a4c736ec192e4
```

All six jobs, individually:

| Job | Conclusion |
|---|---|
| lint (ENG-16 boundary) | success |
| build (x64-linux) | **success** |
| build (arm64-osx) | **success** |
| build (x64-windows-static-md) | **success** |
| build (arm64-linux) — non-blocking | failure (WINDOWS.md #11, open, unchanged) |
| build (x64-osx) — non-blocking | failure (WINDOWS.md #14, open, unchanged) |

Task 1's own verify #1 command: `[.jobs[] | select(name in the 4 required) | select(.conclusion != "success")] | length` → **`0`**.

Four `integration.trust06_idempotence` result lines, quoted verbatim:

- `build (x64-linux)`: `617/623 Test #617: integration.trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder ... Passed 0.01 sec`
- `build (x64-linux)`: `618/623 Test #618: integration.trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this test records whether the pair is byte-identical or merely equivalent ... Passed 0.01 sec`
- `build (arm64-osx)`: `612/618 Test #612: integration.trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder ... Passed 0.03 sec`
- `build (arm64-osx)`: `613/618 Test #613: integration.trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this test records whether the pair is byte-identical or merely equivalent ... Passed 0.03 sec`

`build (x64-windows-static-md)`'s `Test` step conclusion: `success` (via `gh run view --json jobs`, step-level query). **This is the first time in this phase's entire CI history (across runs 33951407521, 33983460934, 33990099158, 34020446940, 34021508083, and now 34023871831) that this leg's Test step has been observed concluding at all**, let alone `success` -- it also incidentally ran and Passed its own two `trust06_idempotence` cases (`612/618`, `613/618`), matching x64-linux and arm64-osx exactly.

`x64-linux`'s DOC-03/TRUST-09 gate lines, quoted (5 total, ≥4 required):
- `437/623 unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden ... Passed`
- `438/623 unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden ... Passed`
- `439/623 unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden ... Passed`
- `527/623 integration.doc03_coverage - dir-mode-only checks: meta.missing_candidate/meta.extra_candidate trigger on an unpaired file each way and are absent (clean) on a fully-paired directory ... Passed`
- `528/623 integration.doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one ... Passed`

Per-blocking-leg corpus verification, quoted (3 of 3, ≥3 required):
- `build (x64-linux)`: `check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.`
- `build (arm64-osx)`: `check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.`
- `build (x64-windows-static-md)`: `check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.`

Non-blocking legs, outside the SC5 claim in both directions:
- `build (arm64-linux)`: failure at "Register vcpkg NuGet feed (read-write, trusted runs only)" (WINDOWS.md #11, open, unchanged this round).
- `build (x64-osx)`: failure at "Build" (WINDOWS.md #14, open, unchanged this round).

Workflow-gate integrity: `git diff --stat 6b57c2a HEAD -- .github/workflows/ci.yml CMakeLists.txt` → empty. No leg was made non-blocking, no `EXCLUDED_TEST_REGEX`/`EXPECTED_EXCLUDED_COUNT` was touched.

### Prior runs consulted (and why they were not used as the closing run)

- **`34021508083`** (head `dfc9e8d`, 03-21's own final run): `build (x64-windows-static-md)` job conclusion was `failure` despite `Build`=success/`Test`=success, because of the not-yet-discovered `GITHUB_PATH` bug (WINDOWS.md #21) in an unrelated later step ("PowerShell corpus generator version-gate"). Task 1's own job-level verify command would have failed against this run.
- **`34022461121`** (head `6b57c2a`, in progress at plan dispatch): `build (x64-linux)` job conclusion was `failure` at "Assert the corpus digest matches the committed pin", even though `6b57c2a` is a docs-only diff from `dfc9e8d` (confirmed via `git diff --name-only`) -- the mismatch was confined to `mkv_opus_a.webm`/`mkv_opus_b.webm` (WINDOWS.md #22, a genuine run-to-run non-determinism, not a source regression). `build (arm64-osx)` also failed (`doc03_coverage`, WINDOWS.md #20, expected and not yet fixed at that point).
- **`34023109337`** (head `3b4ecd0`, after fixing #21): confirmed the `GITHUB_PATH` fix -- `build (x64-windows-static-md)` now `success`. `build (arm64-osx)` still `failure` (WINDOWS.md #20, not yet fixed).
- **`34023549561`** (head `2a5ce3b`, after widening the bitrate margin): confirmed `build (arm64-osx)` now `success`. `build (x64-linux)` deliberately `failure` at the digest-assert step (the fixture bytes changed on purpose; this run's sole purpose was to capture the new correct hash from the "Report corpus digest" step's own log, per D-GAP-01's procedure) -- captured `ed780ca3...  size_near_b.mp4` and `CORPUS_DIGEST_SUMMARY=2f681066...`, with `size_near_a.mp4`'s hash and every other of the 80 fixtures unchanged in that same diff, confirming the digest flake (#22) did not recur on this run.

## Decisions Made

See `key-decisions` in the frontmatter. Summarized: used the first run that actually satisfied Task 1's own literal verify commands rather than either orchestrator-suggested candidate; fixed both newly-discovered blocking-leg defects within Task 1's own declared modification allowance and 3-round-trip budget; left the newly-discovered digest non-reproducibility (#22) open as a genuinely separate investigation; made no edit to REQUIREMENTS.md since its existing text was already correct and is now, for the first time, supported.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `install_pinned_ffmpeg.sh` appends an MSYS-style path to `GITHUB_PATH`, unusable by a later pwsh step**
- **Found during:** Task 1, inspecting run 34021508083's `build (x64-windows-static-md)` job-level conclusion (which read `failure` despite Build/Test both succeeding)
- **Issue:** `dirname "$REAL_CANDIDATE_PATH"` (Git Bash's own `pwd -P`, e.g. `/d/a/mediadiff/.../bin`) was appended verbatim to `GITHUB_PATH`; bash-invoked later steps resolve it fine, but the Windows-only "PowerShell corpus generator version-gate" step's native `pwsh` child cannot resolve a path with no drive-letter prefix, so its positive-path `ffmpeg` invocation failed with "was not found," failing that step and the whole job.
- **Fix:** Convert the `GITHUB_PATH` entry through `cygpath -w` when available (Windows Git Bash ships it; absent and inert on Linux/macOS).
- **Files modified:** `scripts/install_pinned_ffmpeg.sh`
- **Verification:** Local dry run with `GITHUB_ENV`/`GITHUB_PATH` pointed at scratch files, `cygpath` absent, confirmed byte-identical output to the pre-fix behavior; real CI run 34023109337 confirmed `build (x64-windows-static-md)` job conclusion `success`.
- **Committed in:** `3b4ecd0` (also recorded as WINDOWS.md #21, closed in `b814fa5`)

**2. [Rule 3 - Blocking] arm64-osx's `doc03_coverage` clean-pair margin still failing (WINDOWS.md #20, already diagnosed by 03-21)**
- **Found during:** Task 1, confirming run 34021508083's `build (arm64-osx)` job-level conclusion
- **Issue:** `size_near_a.mp4`/`size_near_b.mp4`'s 700k/730k bitrate pair measured only ~2.7% apart on arm64-osx (vs. the 3% warn bound), a margin already diagnosed and deliberately deferred by 03-21.
- **Fix:** Applied 03-21's own drafted fix verbatim (700k/715k, ~1.0% measured locally), then regenerated `tests/golden/CORPUS_DIGEST.txt`'s `size_near_b.mp4` entry and summary from real x64-linux CI output (never derived locally).
- **Files modified:** `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST.txt`
- **Verification:** Real CI run 34023549561 confirmed only `size_near_b.mp4`'s hash changed (5 in `git diff --name-only` scope: `scripts/gen_corpus.sh` change; digest capture; nothing else); real CI run 34023871831 confirmed `build (arm64-osx)`: `success`, `100% tests passed out of 618`.
- **Committed in:** `2a5ce3b`, `64bc168` (also recorded fixed as WINDOWS.md #20 in `b814fa5`)

---

**Total deviations:** 2 auto-fixed (both Rule 3 - blocking). **Impact:** Both were necessary to satisfy this plan's own literal verify commands and ROADMAP SC5's own claim; neither touched `.github/workflows/ci.yml`'s blocking/exclusion configuration (confirmed empty diff). No scope creep beyond the "whichever source or test file a failing Test step names" allowance Task 1's own `<files>` field grants.

## Known Stubs

None.

## Threat Flags

None. This plan's own threat register (T-3-102 through T-3-105) targets exactly the failure modes it guards against -- inferring a closure from absence-in-a-failure-list, a half-applied ledger edit, a hand-edited WINDOWS.md representation, and manufacturing green by narrowing the gate -- none of which occurred (verified via the equality-asserting verify commands and the empty `ci.yml`/`CMakeLists.txt` diff).

## Issues Encountered

Two genuinely new, previously-unobserved CI defects surfaced mid-round beyond the two the plan anticipated finding (the plan expected only "if a blocking leg reaches Test and fails it"; the Windows `GITHUB_PATH` bug instead manifested as an unrelated later step failing a job whose Build/Test both succeeded, which is why neither the orchestrator's evidence nor 03-21's own SUMMARY had previously surfaced it). Both were diagnosed and, where in scope, fixed within the plan's 3-round-trip budget; the budget was fully consumed (3 of 3) doing so, leaving no reserve had a fourth defect appeared.

## User Setup Required

None -- no external service configuration required.

## Next Phase Readiness

- **ROADMAP SC5 is met**, observed on real CI run 34023871831: all three blocking legs and lint conclude success, four `trust06_idempotence` lines observed `Passed` on the two named legs, `x64-windows-static-md`'s `Test` step observed concluding for the first time in this phase's history, and DOC-03/TRUST-09 gates observed passing on the designated leg.
- **WINDOWS.md #22 is a new, open, and somewhat concerning finding**: x64-linux's own designated-leg fixture generation is not proven run-to-run reproducible (observed once, on `mkv_opus_a.webm`/`mkv_opus_b.webm`). It did not recur in this plan's own two subsequent pushes, but a single non-recurrence is not proof of stability -- a future round revisiting the designated-leg policy (WINDOWS.md #17) should treat #22 as open evidence against assuming x64-linux is internally deterministic, not as a resolved flake.
- WINDOWS.md #11 (arm64-linux NuGet) and #14 (x64-osx cross-build) remain open, non-blocking, and unchanged by this plan.

## Self-Check: PASSED

- `[ -f scripts/install_pinned_ffmpeg.sh ]` -- FOUND
- `[ -f scripts/gen_corpus.sh ]` -- FOUND
- `[ -f tests/golden/CORPUS_DIGEST.txt ]` -- FOUND
- `[ -f .planning/WINDOWS.md ]` -- FOUND
- `git log --oneline --all | grep -q 3b4ecd0` -- FOUND
- `git log --oneline --all | grep -q 2a5ce3b` -- FOUND
- `git log --oneline --all | grep -q 64bc168` -- FOUND
- `git log --oneline --all | grep -q b814fa5` -- FOUND
- `node gsd-tools.cjs windows status` -- exit 0, total 22 matching both representations
- Task 1 and Task 2's own `<verify>` commands re-run above against run 34023871831 -- all PASS
- Task 1 and Task 2's own `<acceptance_criteria>` re-checked above -- all PASS

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-06*
