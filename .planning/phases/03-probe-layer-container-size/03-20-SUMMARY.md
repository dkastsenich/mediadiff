---
phase: 03-probe-layer-container-size
plan: 20
subsystem: infra
tags: [ci, github-actions, ledger, verification, ffmpeg, trust-06, doc-03]

# Dependency graph
requires:
  - phase: 03-16
    provides: Checksum-pinned fixture-synthesis ffmpeg + per-leg CORPUS_DIGEST_SUMMARY= reporting
  - phase: 03-18
    provides: Permanent bash-4-builtin lint guard (the runtime fix for the macOS bash-3.2 corpus-verify crash observed this round)
  - phase: 03-19
    provides: Designated-leg (x64-linux) corpus-digest gate and named/counted CTest exclusion of 5 byte-exact fixture-derived golden tests on non-designated legs
provides:
  - "Real, run-log-cited evidence (run 33990099158, head 1b684de) of exactly which blocking legs are green and which are not — replacing the stale, partially-wrong picture in 03-VERIFICATION.md and WINDOWS.md that was derived from run 33951407521"
  - "WINDOWS.md corrected: entry #10's ffmpeg-version claim fixed (6.1.1-3ubuntu5, not 9.0.1 via apt); a new entry (#15) recording the macOS bash-3.2 corpus-verify crash, now closed on observed CI evidence; a new entry (#16) recording a previously-unrecorded Windows build defect (report_cli_error unqualified), left open; a new entry (#17) recording the designated-leg narrowed golden coverage as an accepted, visible limitation; entry #9 (NOMINMAX) closed on observed evidence that ebml_scan.cpp now compiles cleanly under MSVC"
  - "03-VERIFICATION.md corrected: the false 'generated and verified cleanly on every leg it reached' claim replaced with the per-leg truth (all five legs failed in run 33951407521, not two), plus a dated correction note under the report's own heading"
  - "A plain, evidence-based statement that ROADMAP SC5 is NOT closed this round, with the exact remaining gap named for the next plan"
affects: [phase-03-verification, future-gap-closure-plan, ci-workflow]

# Actuals (#2632)
actuals:
  tokens: 5500
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Ledger corrections routed through gsd-tools windows append/fixed subcommands (never a hand-edit to counts) so the markdown table, the fenced JSON array and the frontmatter counts stay in sync; free-text description corrections applied identically to both the table row and the JSON object in the same edit"
    - "A closure claim is illegitimate without a cited, observed CI step conclusion from a specific run id — applied here to distinguish 'entry #9's specific defect is fixed' (true, evidenced by ebml_scan.cpp.obj compiling cleanly with -DNOMINMAX) from 'the leg's Build step succeeded' (false — a new, unrelated defect now fails it), rather than conflating the two"

key-files:
  created: []
  modified:
    - .planning/WINDOWS.md
    - .planning/phases/03-probe-layer-container-size/03-VERIFICATION.md

key-decisions:
  - "SC5 is reported NOT closed this round, on direct run-log evidence, rather than accepting the plan's optimistic framing ('Observe SC5 met') — only 2/4 of Task 1's own automated <verify> checks pass against the real run (0 non-success blocking jobs check FAILS: 2 of 4 fail; trust06 Passed-line count FAILS: 2 observed, 4 required, because arm64-osx's Test step never runs), so declaring SC5 closed would have been the exact inference-substitution error this plan exists to prevent."
  - "The newly discovered x64-windows-static-md report_cli_error defect (src/cli/main.cpp:297) is recorded in WINDOWS.md but NOT fixed, honoring both the plan's own declared files_modified (WINDOWS.md, 03-VERIFICATION.md only) and Task 2's own acceptance criterion restricting git diff --name-only to ledger/verification/summary files. Task 1's action text contemplates fixing blocking-leg defects 'within scope of whichever plan owns the affected file' — read as authorizing a follow-up plan to do so, not this one, since this plan owns no source file."
  - "Entry #9 (ebml_scan.cpp NOMINMAX/C2059) is closed as fixed on the specific, narrower evidence that ebml_scan.cpp.obj now compiles cleanly under MSVC with -DNOMINMAX (confirmed in the run log), not on the coarser, false claim that the Build step itself succeeded — the Build step still fails, for the new, unrelated report_cli_error defect now tracked separately as #16."

requirements-completed: [TRUST-06, DOC-03]

coverage:
  - id: D1
    description: "Real CI run evidence (run 33990099158) gathered for every element ROADMAP SC5 asks for: per-job conclusions enumerated individually, four TRUST-06 result lines sought verbatim, ffmpeg version lines and corpus-verification lines quoted for all five legs"
    requirement: "TRUST-06"
    verification:
      - kind: other
        ref: "gh run view 33990099158 --json jobs (quoted verbatim in this SUMMARY's CI Evidence section)"
        status: pass
      - kind: other
        ref: "gh run view 33990099158 --log | grep integration.trust06_idempotence | grep -c Passed = 2 (not 4 — arm64-osx's Test step never runs; quoted and explained in this SUMMARY)"
        status: fail
    human_judgment: false
  - id: D2
    description: "WINDOWS.md and 03-VERIFICATION.md corrected to match what real CI actually shows, with every closure citing a specific observed step conclusion and no claim resting on a committed change alone"
    requirement: "DOC-03"
    verification:
      - kind: other
        ref: "node gsd-tools.cjs windows status (exit 0, total_count=17 matching both the table row count and the JSON array length, quoted in this SUMMARY)"
        status: pass
      - kind: other
        ref: "grep -c 'installs ffmpeg 9.0.1 via apt' .planning/WINDOWS.md = 0; grep -c '6.1.1-3ubuntu5' .planning/WINDOWS.md = 2; grep -c 'generated and verified cleanly on every leg it reached' 03-VERIFICATION.md = 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "ROADMAP SC5's true status stated plainly, on run-log evidence, without overstating or understating closure"
    verification: []
    human_judgment: true
    rationale: "Whether the stated SC5 status is honest and appropriately calibrated against the evidence is a judgment a human should confirm, not something a passing test can certify — this SUMMARY's 'SC5 Status' section is the artifact for that review."

# Metrics
duration: ~35min
completed: 2026-09-05
status: complete
---

# Phase 03 Plan 20: Real-CI Evidence for SC5 and Ledger Correction Summary

**Gathered real CI evidence (run 33990099158) showing SC5 is still NOT met — two blocking legs (arm64-osx, x64-windows-static-md) fail at Build for reasons unrelated to TRUST-06 itself — and corrected WINDOWS.md/03-VERIFICATION.md's stale, partially-wrong picture of the prior round's CI run, closing two entries on cited evidence and recording two new ones (one closed, one newly discovered and left open).**

## Performance

- **Duration:** ~35 min (includes triggering and polling one real CI run to completion, ~4 min in CI)
- **Tasks:** 2
- **Files modified:** 2 (`.planning/WINDOWS.md`, `03-VERIFICATION.md`) + this SUMMARY

## Accomplishments

- Triggered a fresh real CI run (`33990099158`, head `1b684de`, PR #3) rather than relying on the plan's stale referenced run (`33951407521`) or even the most recent prior-wave run (`33989567384`), since the local branch was one unpushed commit ahead.
- Enumerated all six CI job conclusions individually rather than summarizing, catching that the run's failure set is `{arm64-osx, x64-windows-static-md, x64-osx, arm64-linux}` — four jobs, not the two the stale report described.
- Discovered, by actually counting `Passed` lines rather than checking absence-from-failure-list, that only 2 of the 4 required `integration.trust06_idempotence` result lines exist in this run's log — `arm64-osx`'s `Test` step is `skipped` because its `Build` step fails first. This directly falsifies the plan's own must-have truth #2 and is the central reason SC5 is not closed.
- Corrected `WINDOWS.md` entry #10's ffmpeg-version claim (real CI apt version is `6.1.1-3ubuntu5`, not `9.0.1`) in both the table row and the JSON object.
- Recorded and closed a new ledger entry (#15) for the macOS bash-3.2 `mapfile` corpus-verify crash, now proven fixed at runtime on real CI (both `arm64-osx` corpus steps green in run `33990099158`).
- Recorded (but explicitly did NOT fix) a newly discovered ledger entry (#16) for `src/cli/main.cpp:297`'s unqualified `report_cli_error` call — the reason `x64-windows-static-md` is still red, now that its earlier NOMINMAX defect is fixed.
- Closed entry #9 (NOMINMAX) on the narrow, correct evidence that `ebml_scan.cpp.obj` now compiles cleanly under MSVC with `-DNOMINMAX` — distinct from (and not conflated with) the Build step's overall (still-red) conclusion.
- Recorded a new, open ledger entry (#17) for the designated-leg narrowed golden coverage 03-19 chose, keeping that accepted limitation visible to future rounds.
- Corrected `03-VERIFICATION.md`'s false "generated and verified cleanly on every leg it reached" claim (in both the `gaps.reason` field and the `re_verification.gaps_remaining` entry) with the per-leg truth, plus a dated correction note under the report's own heading.

## Task Commits

1. **Task 1: Observe SC5 on a real run** — `[recorded below]` (docs) — SUMMARY.md with full CI evidence
2. **Task 2: Correct the stale ledger and verification records** — `[recorded below]` (docs) — WINDOWS.md + 03-VERIFICATION.md corrections

**Plan metadata:** committed separately after this SUMMARY (STATE.md/ROADMAP.md).

## CI Evidence (Task 1)

**Precondition check:** plans 03-16 through 03-19 are committed on the branch (confirmed via `git log`); PR #3 is open (`gh pr view 3 --json state` → `OPEN`); `gh` is authenticated. Local branch was one commit (`1b684de`, a docs-only SUMMARY-completion commit for 03-19) ahead of `origin/gsd/phase-03-probe-layer-container-size` — pushed it, which triggered a fresh, real CI run rather than reusing a stale one.

**Run identity:**
```
$ gh run view 33990099158 --json status,conclusion,headSha,headBranch,createdAt,updatedAt
{"conclusion":"failure","createdAt":"2026-09-05T20:26:15Z","headBranch":"gsd/phase-03-probe-layer-container-size","headSha":"1b684de2e99989f4479aa9fee30caca495a476b6","status":"completed","updatedAt":"2026-09-05T20:30:09Z"}
```
Run `33990099158`, head SHA `1b684de2e99989f4479aa9fee30caca495a476b6` (a docs-only commit; the source tree is byte-identical to `351750c`, the head 03-19's own SUMMARY proved green/red on).

**All six job conclusions, enumerated individually (not summarized):**

| Job | Blocking? | Conclusion |
|---|---|---|
| `build (x64-linux)` | Yes (designated) | `success` |
| `build (arm64-osx)` | Yes | `failure` |
| `build (x64-windows-static-md)` | Yes | `failure` |
| `lint (ENG-16 boundary)` | required, not a "blocking build" leg | `success` |
| `build (arm64-linux)` | No | `failure` |
| `build (x64-osx)` | No | `failure` |

**Result: 2 of 3 blocking legs are `success`; `arm64-osx` and `x64-windows-static-md` are both `failure`.** This is the exact failure pattern Task 1's own first automated `<verify>` command checks for — reproduced here:
```
$ gh run view 33990099158 --json jobs --jq '[.jobs[] | select(.name=="build (x64-linux)" or .name=="build (arm64-osx)" or .name=="build (x64-windows-static-md)" or .name=="lint (ENG-16 boundary)") | select(.conclusion != "success")] | length'
2
```
Expected `0` for SC5 to be met; actual `2`. **This single number is why SC5 is not closed this round.**

**TRUST-06 (`integration.trust06_idempotence`) result lines — sought on BOTH x64-linux and arm64-osx, quoted verbatim:**
```
build (x64-linux)  Test  Start 616: integration.trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder
build (x64-linux)  Test  616/622 Test #616: integration.trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder ... Passed  0.01 sec
build (x64-linux)  Test  Start 617: integration.trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this test records whether the pair is byte-identical or merely equivalent
build (x64-linux)  Test  617/622 Test #617: integration.trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this test records whether the pair is byte-identical or merely equivalent ... Passed  0.01 sec
```
`x64-linux`: both cases execute and read `Passed`. `100% tests passed, 0 tests failed out of 622` (full suite, unmodified, designated leg).

**`arm64-osx` has NO `trust06_idempotence` lines at all** — its `Test` step conclusion is `skipped`, because its `Build` step fails first (see below). `gh run view 33990099158 --log | grep 'integration\.trust06_idempotence' | grep -c 'Passed'` → **2**, not the 4 SC5's must-have truth #2 requires. **Absence from a failure list is explicitly not acceptable evidence per this plan's own instruction — and here the direct count confirms the test literally never ran on `arm64-osx`, not merely that it wasn't observed to fail.**

**`x64-windows-static-md` Build step conclusion — quoted:**
```
build (x64-windows-static-md)  Build  D:\a\mediadiff\mediadiff\src\cli\main.cpp(297): error C3861: 'report_cli_error': identifier not found
```
Conclusion: `failure`. This is a NEW, previously-unrecorded defect (see WINDOWS.md #16 below) — the earlier NOMINMAX/C2059 defect (#9) is confirmed fixed (see the Build log immediately preceding: `ebml_scan.cpp.obj` compiles with no error, `-DNOMINMAX` present in the invoked compile flags).

**`arm64-osx` corpus generation/verification step conclusions and the "clean. Verified N fixture(s)" line — quoted:**
```
build (arm64-osx)  Generate media fixture corpus (BUILD-08 / D-08)         success
build (arm64-osx)  Verify the fixture corpus is complete                   success
build (arm64-osx)  Verify the fixture corpus is complete  check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
```
`arm64-osx`'s `Build` step itself fails at a DIFFERENT step, later, for a DIFFERENT reason:
```
build (arm64-osx)  Build  tests/unit/test_ebml_scan.cpp:89:25: error: unused variable 'kClusterId' [-Werror,-Wunused-const-variable]
```
This is WINDOWS.md #13 (pre-existing, open, out of this plan's declared scope) — unrelated to the bash-3.2 corpus fix, which is proven fixed by the two lines quoted above.

**`Install the pinned ffmpeg build (D-GAP-01)` version line, all three blocking legs, quoted:**
```
build (x64-linux):             install_pinned_ffmpeg.sh: ffmpeg version 9.0.1-https://www.martin-riedl.de Copyright (c) 2000-2026 the FFmpeg developers
build (arm64-osx):              install_pinned_ffmpeg.sh: ffmpeg version 9.0.1-https://www.martin-riedl.de Copyright (c) 2000-2026 the FFmpeg developers
build (x64-windows-static-md):  install_pinned_ffmpeg.sh: ffmpeg version n9.0.1-11-ge47273f4d9-20260902 Copyright (c) 2000-2026 the FFmpeg developers
```
Same nominal pinned version family (`9.0.1`) on all three blocking legs.

**Corpus verification on ALL FIVE legs (not just the blocking three) — every leg's `check_corpus.sh` line, quoted:**
```
build (x64-osx):                check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
build (x64-linux):               check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
build (arm64-linux):             check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
build (x64-windows-static-md):   check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
build (arm64-osx):               check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
```
The bash-3.2 corpus-verify crash (WINDOWS.md #15, closed below) is confirmed fixed on all five legs, including both macOS legs — `arm64-osx` above, and `x64-osx` (non-blocking) also green here.

**The two non-blocking legs, recorded, explicitly outside the SC5 claim:**

| Job | Conclusion | Failing step |
|---|---|---|
| `build (arm64-linux)` | `failure` | `Register vcpkg NuGet feed (read-write, trusted runs only)` (WINDOWS.md #11, credential/infra, unrelated to the corpus or to TRUST-06) |
| `build (x64-osx)` | `failure` | `Build` (WINDOWS.md #13, the same AppleClang unused-const-variable defect as `arm64-osx`, since both share the same macOS host per prior-plan A3) |

Neither counts toward or against SC5; WINDOWS.md #11 remains open, untouched by this round.

**Designated-leg corpus-digest policy announcement (item 7 of Task 1's action list) — quoted from a non-designated leg:**
```
build (arm64-osx)  Assert the corpus digest matches the committed pin (D-GAP-01)
This leg (arm64-osx) is NOT the designated leg (x64-linux) for tests/golden/CORPUS_DIGEST.txt (03-19: the pinned ffmpeg builds do not produce byte-identical fixtures across platforms -- see 03-19-SUMMARY.md). This leg's corpus is deliberately NOT asserted against the committed digest.
```
This announcement fires on every non-designated leg (`arm64-osx`, `x64-osx`, `x64-windows-static-md`, `arm64-linux`), confirmed individually in the run log. **The 5-test CTest exclusion itself (`EXCLUDED_TEST_REGEX`/`EXPECTED_EXCLUDED_COUNT=5`) is never actually exercised in this run** — every non-designated leg that reaches `Configure` (`arm64-osx`, `x64-osx`, `x64-windows-static-md`) fails at `Build`, before `Test` ever runs, and `arm64-linux` fails even earlier (before `Configure`). Only `x64-linux` (the designated leg) reaches `Test` in this run, and it runs the full, unfiltered 622-test suite. Recorded honestly rather than asserting the exclusion path was exercised when it was not.

## SC5 Status (stated plainly)

**ROADMAP SC5 is NOT met** on real CI run `33990099158` (head `1b684de`).

What IS proven, on real, cited CI evidence:
- `x64-linux` (the designated, blocking leg) is fully green, full 622-test suite, both `trust06_idempotence` cases `Passed`.
- The corpus-generation and corpus-verification mechanism (D-GAP-02/D-GAP-01) works correctly on all five legs, including both macOS legs — the bash-3.2 crash that used to abort `arm64-osx`/`x64-osx` before `Configure` is fixed and observed fixed.
- `arm64-osx`'s specific NOMINMAX/`std::max` C2059 defect from `x64-windows-static-md` (WINDOWS.md #9) does NOT recur — `ebml_scan.cpp` compiles cleanly under MSVC.

What is NOT proven, and is why SC5 remains open:
- `arm64-osx` (blocking) still fails at `Build`, for an unrelated, pre-existing, still-open defect (WINDOWS.md #13, AppleClang `-Werror,-Wunused-const-variable`). Its `Test` step never runs, so `trust06_idempotence` has NEVER been observed executing on `arm64-osx` in any real CI run to date.
- `x64-windows-static-md` (blocking) still fails at `Build`, now for a DIFFERENT, newly discovered defect (WINDOWS.md #16, `src/cli/main.cpp:297`'s unqualified `report_cli_error` call) — its earlier NOMINMAX defect is fixed, but a new one was hiding behind it.
- Only 2 of the 4 `integration.trust06_idempotence` result lines SC5's own must-have truth requires exist in this run's log (both from `x64-linux`; zero from `arm64-osx`).

**Neither remaining defect is owned by this plan.** Both are recorded in `.planning/WINDOWS.md` (#13 already open, #16 newly appended and open) and are explicitly out of this plan's declared `files_modified` (`WINDOWS.md`, `03-VERIFICATION.md` only — no source file). Per this plan's own flagged assumption A3, closing SC5 requires a follow-up plan to fix WINDOWS.md #13 and #16 (both are small, well-understood, one-line-class fixes — an unused-variable removal and a namespace qualification) and then re-observe on a fresh real CI run. This plan does not soften SC5's criteria or declare it closed on the two legs that DO pass.

## Ledger Corrections (Task 2)

All four corrections plus the conditional fifth were applied, each citing a specific observed CI step conclusion from the Task 1 evidence above — none rests on a committed change alone.

**Correction 1 — entry #10's ffmpeg-version claim.** Was: "CI installs ffmpeg 9.0.1 via apt." Now: CI's apt-installed ffmpeg was actually `6.1.1-3ubuntu5` (Ubuntu 24.04's packaged version), confirmed from that leg's own `ffmpeg -version` output in the original run `33951407521` — a two-major-version gap from the local `N-126086` snapshot the goldens were captured against, not the patch drift originally implied. Applied to both the markdown table row and the fenced JSON object (verified: `grep -c '6.1.1-3ubuntu5' .planning/WINDOWS.md` → `2`).

**Correction 2 — new entry for the macOS bash-3.2 defect.** Appended as entry **#15** via `windows append`, citing the exit-127 crash, commit `91d9d2f` (03-14's fix), 03-18's permanent lint guard, and this round's runtime proof (run `33990099158`: `arm64-osx`'s `Generate media fixture corpus` and `Verify the fixture corpus is complete` steps both `success`, printing "clean. Verified 80 fixture(s)"). Closed immediately via `windows fixed 15`, since the runtime proof this entry exists to record is the very evidence Task 1 gathered.

**Correction 3 — `03-VERIFICATION.md`'s "every leg it reached" claim.** Corrected in both the `gaps.reason` field and the `re_verification.gaps_remaining` entry, replacing the false uniform claim with the per-leg truth: all five build legs failed in run `33951407521` (not two) — `x64-windows-static-md` (NOMINMAX), `x64-linux` (5 golden tests), `arm64-osx`/`x64-osx` (the bash-3.2 corpus-verify crash), and `arm64-linux` (NuGet-feed credentials). Added a dated correction note (`> **Correction note (03-20, 2026-09-05, ...)**`) directly under the report's own top-level heading, citing run `33951407521` as the source and describing the error rather than re-quoting the exact erroneous sentence (verified: `grep -c 'generated and verified cleanly on every leg it reached' 03-VERIFICATION.md` → `0`).

**Correction 4 — close what this round actually closed.**
- Entry **#9** (NOMINMAX/C2059) closed via `windows fixed 9`, on the narrow, correct evidence that `ebml_scan.cpp.obj` compiles cleanly under MSVC with `-DNOMINMAX` in run `33990099158`'s Build log — NOT on the coarser claim that the Build step itself succeeded (it does not; a new, unrelated defect, #16, now fails it). This distinction matters: conflating "the specific defect is gone" with "the step succeeded" is exactly the kind of imprecision this plan exists to correct.
- Entry **#10** was already `fixed` (closed by 03-16); only its description text needed correcting (Correction 1 above), not its status.
- The newly appended macOS entry (**#15**) closed as described in Correction 2.
- Entry **#11** (arm64-linux NuGet credentials, non-blocking) left `open` — nothing in this round observes it, and it is out of scope by this plan's own flagged assumption A1.
- **New, NOT closed:** entry **#16** appended for `src/cli/main.cpp:297`'s unqualified `report_cli_error` call, the reason `x64-windows-static-md` is still red. Recorded per the prior-wave orchestrator's explicit instruction and this plan's own scope boundary — NOT fixed, since `src/cli/main.cpp` is not in this plan's declared `files_modified`.

**Correction 5 — conditional (designated-leg policy).** 03-19 implemented the `designated` policy (confirmed via 03-19-SUMMARY.md's key-decisions). Appended entry **#17**, left `open`, recording which 5 byte-exact fixture-derived golden tests run only on `x64-linux`, that the exclusion is named-and-counted (`EXPECTED_EXCLUDED_COUNT=5`) and announced (never silent) on every other leg, and the outstanding follow-up (a future ffmpeg-pin bump must regenerate `tests/golden/CORPUS_DIGEST.txt` from the designated leg's real CI output in the same commit).

**Ledger integrity check:**
```
$ node gsd-tools.cjs windows status --raw | ...
total_count=17, open_count=12, fixed_count=5, waived_count=0
```
Markdown table rows: 17. Fenced JSON array entries: 17. Both representations agree.

## Files Created/Modified

- `.planning/WINDOWS.md` — entry #10's ffmpeg-version claim corrected (table + JSON); entry #9 closed on cited MSVC-compile evidence; new entry #15 (macOS bash-3.2 corpus crash) appended and closed; new entry #16 (Windows report_cli_error defect) appended, left open; new entry #17 (designated-leg narrowed golden coverage) appended, left open; entry #11 untouched, still open.
- `.planning/phases/03-probe-layer-container-size/03-VERIFICATION.md` — the false "every leg it reached" claim corrected in both occurrences (`gaps.reason`, `re_verification.gaps_remaining`); a dated correction note added under the report's own top-level heading citing run `33951407521` and pointing to this SUMMARY for current status.

## Decisions Made

See `key-decisions` in frontmatter above — summarized: (1) SC5 reported not-closed on direct evidence rather than accepting the plan's optimistic "Observe SC5 met" framing; (2) the newly discovered Windows defect is recorded, not fixed, honoring this plan's own declared scope; (3) entry #9 is closed on the narrower, correct evidence (the specific compile error is gone) rather than the coarser, false one (the Build step succeeded).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking, scope-bounded] Task 1's action text contemplated fixing blocking-leg build defects within this plan; declined, per this plan's own declared scope**
- **Found during:** Task 1 (CI evidence gathering)
- **Issue:** Task 1's `<action>` text says "If any blocking leg is not success, diagnose it, fix it within the scope of whichever plan owns the affected file, push, and re-observe." Both `arm64-osx` (WINDOWS.md #13, `tests/unit/test_ebml_scan.cpp`) and `x64-windows-static-md` (new, WINDOWS.md #16, `src/cli/main.cpp`) are not `success`.
- **Resolution:** Not fixed in this plan. This plan's own frontmatter declares `files_modified: [.planning/WINDOWS.md, .planning/phases/.../03-VERIFICATION.md]` only, and Task 2's own acceptance criteria require `git diff --name-only` to list only ledger/verification/SUMMARY files — "whichever plan owns the affected file" is read as authorizing a FUTURE plan, not this one, since this plan owns no source file. Both defects were instead recorded honestly in WINDOWS.md (one already open as #13, one newly appended as #16) and SC5 is reported not-closed rather than softened.
- **Files modified:** None (deliberately) — recorded in `.planning/WINDOWS.md` only.
- **Verification:** `git diff --name-only` (staged for this plan) lists only `.planning/WINDOWS.md`, `03-VERIFICATION.md`, and this SUMMARY.
- **Committed in:** Task 2's commit.

---

**Total deviations:** 1 (a scope-boundary judgment call, not a bug or missing-feature fix)
**Impact on plan:** None on correctness — the plan's own acceptance criteria for Task 2 already required exactly this behavior; the deviation note exists because Task 1's own action text, read literally, could be misread as authorizing an in-scope fix.

## Issues Encountered

- **Only 2 of the 4 required TRUST-06 `Passed` lines exist in the real run's log**, because `arm64-osx`'s `Test` step never runs (its `Build` step fails first, on an unrelated pre-existing defect). This is the direct, cited reason SC5's must-have truth #2 is unmet — not a tooling or evidence-gathering problem, a real gap.
- **The designated-leg CTest exclusion logic (`EXCLUDED_TEST_REGEX`) is never actually exercised** in this run, because every non-designated leg either fails before `Configure` (`arm64-linux`) or fails at `Build` before `Test` (`arm64-osx`, `x64-osx`, `x64-windows-static-md`). Recorded honestly in the CI Evidence section above rather than asserting the exclusion path ran when it did not.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

**SC5 remains open.** A follow-up plan must, in order:
1. Fix `WINDOWS.md` #13 — remove or use the unused `constexpr std::uint64_t kClusterId` at `tests/unit/test_ebml_scan.cpp:89` (or annotate `[[maybe_unused]]`) so `arm64-osx`'s `Build` step succeeds.
2. Fix `WINDOWS.md` #16 — qualify the call at `src/cli/main.cpp:297` as `mediadiff::report_cli_error(...)` so `x64-windows-static-md`'s `Build` step succeeds.
3. Push and re-observe a fresh real CI run: confirm `x64-linux`, `arm64-osx`, `x64-windows-static-md` all `success`, with `integration.trust06_idempotence` observed `Passed` (not merely absent from a failure list) on BOTH `x64-linux` and `arm64-osx` — exactly the four-line evidence bar this plan's own Task 1 set and could not fully clear this round.
4. Only then close SC5 in ROADMAP.md and mark entries #13/#16 fixed on that run's cited evidence.

Entry #17 (designated-leg narrowed golden coverage) and entry #11 (arm64-linux NuGet credentials) remain open by design — neither is expected to close as part of the SC5 follow-up above.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-05*
