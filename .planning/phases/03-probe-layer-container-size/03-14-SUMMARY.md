---
phase: 03-probe-layer-container-size
plan: 14
subsystem: infra
tags: [ci, github-actions, bash, ffmpeg, fixture-generation, corpus]

# Dependency graph
requires:
  - phase: 03-12
    provides: TRUST-06 idempotence test wired into the unconditional Test step
provides:
  - "A corpus preflight (scripts/check_corpus.sh) that derives its expected fixture list from scripts/gen_corpus.sh itself, self-tests, and distinguishes generation failure from regression"
  - "Unconditional ffmpeg-install + corpus-generation + corpus-verification step group before Configure on all five CI matrix legs"
  - "Real CI evidence (PR #3, run 33951407521) that the corpus wiring executes correctly in the right order on real runners"
  - "A bash-3.2-compatible check_corpus.sh, proven necessary and fixed by that same real CI run"
affects: [phase-03-verification, ci-workflow, ebml_scan, golden-fixtures]

# Actuals (#2632)
actuals:
  tokens: 2957
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Preflight scripts derive expectations mechanically from their generator's source rather than a hand-maintained list (extract_expected_names pattern, matching lint_dead_code_after_fail.sh/lint_fixture_case_collisions.sh's established shape)"
    - "POSIX-portable bash only in scripts that run under Git Bash (Windows) and BSD bash 3.2 (macOS) — no mapfile/readarray, no GNU-only regex"

key-files:
  created:
    - scripts/check_corpus.sh
  modified:
    - .github/workflows/ci.yml
    - scripts/check_corpus.sh (bash-3.2 portability fix, this continuation)

key-decisions:
  - "Regenerate the corpus every CI run, never cache it — a cached corpus would silently compare fixtures from two different ffmpeg builds, defeating TRUST-06's same-run-same-binary guarantee (recorded in 03-14-PLAN.md's flagged_assumptions)."
  - "Fixed the bash 3.2 portability bug (mapfile -> while-read loop) in place rather than working around it — matches the plan's own instruction that a portability failure is fixed in the script, never by a conditional, skip, or tolerance."
  - "Findings 2-4 (ebml_scan.cpp NOMINMAX/std::max clash, ffmpeg-version-drifted goldens, vcpkg NuGet feed credentials) are recorded in WINDOWS.md and left untouched — they are real pre-existing defects this plan's CI wiring correctly revealed for the first time, not regressions introduced by this plan, and are outside its declared files_modified."

patterns-established:
  - "A CI-only acceptance criterion that says '# blocking legs go green end to end' is verified against a real run, not asserted from static analysis — and when a real run partially fails for reasons outside the plan's scope, the SUMMARY says so explicitly rather than reinterpreting the criterion to fit the result."

requirements-completed: [TRUST-06, DOC-03]

coverage:
  - id: D1
    description: "scripts/check_corpus.sh: self-testing corpus preflight deriving expectations from scripts/gen_corpus.sh"
    requirement: "DOC-03"
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh (80/80 fixtures verified, exit 0)"
        status: pass
      - kind: other
        ref: "negative tests: fixture moved aside (exit 1, named), fixture truncated to 0 bytes (exit 1, named), gen_corpus.sh renamed away (exit 1, zero-file guard)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Corpus generation + verification wired unconditionally before Configure on all five CI matrix legs"
    requirement: "TRUST-06"
    verification:
      - kind: other
        ref: "PR #3, CI run 33951407521 — corpus steps ran on all five legs in the correct order (before Configure); macOS legs failed at the verify step (bash 3.2 mapfile), now fixed by this SUMMARY's commit and re-verified locally"
        status: pass
    human_judgment: true
    rationale: "Full five-leg green was not achieved on the observed run — x64-windows-static-md (pre-existing ebml_scan.cpp NOMINMAX bug) and x64-linux (pre-existing ffmpeg-version-drifted goldens) both failed for reasons outside this plan's files_modified. A human must confirm the corpus-wiring fix (this plan's actual deliverable) is accepted on its own terms, separate from those two unrelated pre-existing defects now tracked in WINDOWS.md #9 and #10."

# Metrics
duration: ~35min (tasks 1-2, prior session) + ~25min (this continuation: fix, local verification, SUMMARY)
completed: 2026-09-05
status: complete
---

# Phase 03 Plan 14: CI Corpus Generation and Verification Summary

**Wired `scripts/gen_corpus.sh` + a new self-testing `scripts/check_corpus.sh` preflight into every CI matrix leg before `Configure`, proved it on a real CI run (PR #3, run 33951407521), and fixed a bash-3.2 incompatibility that run surfaced on both macOS legs — full five-leg green remains blocked on three pre-existing, out-of-scope defects now tracked in WINDOWS.md.**

## Performance

- **Duration:** ~35 min (Tasks 1-2, prior session) + ~25 min (this continuation: Finding-1 fix, local verification, SUMMARY, ledger updates)
- **Tasks:** 3 of 3 (Task 3 checkpoint answered with real CI evidence; acceptance criteria only partially met, recorded honestly below)
- **Files modified:** 2 (`scripts/check_corpus.sh`, `.github/workflows/ci.yml`)

## Accomplishments

- Added `scripts/check_corpus.sh`: a self-testing corpus completeness preflight that extracts its expected fixture list mechanically from `scripts/gen_corpus.sh`'s own `$OUT_DIR/<name>` tokens (never a hand-maintained list), runs a self-test control clause before every real scan, enforces a zero-file guard, and distinguishes a fixture-generation failure from a comparison regression in its own output text.
- Wired an unconditional three-part step group (install ffmpeg -> generate corpus -> verify corpus) into `.github/workflows/ci.yml` before `Configure` on all five matrix legs, removed the now-duplicate post-`Test` Windows ffmpeg install, and kept the PowerShell version-gate cross-check step unchanged.
- **Obtained the first real CI run this project has had of this workflow** (PR #3, https://github.com/dkastsenich/mediadiff/pull/3, run 33951407521) — confirming the corpus steps execute, in the correct order, on real runners across all five legs. This is genuine evidence, not a simulation, and it is what the Task 3 checkpoint existed to produce.
- **Fixed the one in-scope defect that run revealed:** `scripts/check_corpus.sh:62` used `mapfile -t`, a bash-4-only builtin. macOS ships bash 3.2, where `mapfile` does not exist (`command not found`, exit 127), which under `-o pipefail` also produced a broken-pipe error. Replaced with a portable `while IFS= read -r` loop appending to the array — functionally identical on bash 3.2 and 4+, with the zero-file guard and derive-from-generator property fully preserved. Confirmed no other bash-4-only construct (`readarray`, `declare -A`, `${var^^}`/`${var,,}`, `local -n`) exists anywhere in `check_corpus.sh`, `gen_corpus.sh`, or `ci.yml`.
- **Recorded, did not fix,** three pre-existing defects that same CI run revealed for the first time (findings 2-4 below) — surfacing them is this plan's success, not its failure, and they are handed to phase verification / a future gap-closure plan.

## Task Commits

1. **Task 1: A corpus preflight that derives its expectations from the generator itself** — `1e5217e` (feat) — prior session
2. **Task 2: Generate and verify the corpus before the Test step on every matrix leg** — `ac72738` (feat) — prior session
3. **Task 3 checkpoint follow-up: bash-3.2 compatibility fix for `check_corpus.sh`** — `91d9d2f` (fix) — this continuation

**Plan metadata:** commit pending (this SUMMARY + STATE.md + ROADMAP.md + REQUIREMENTS.md)

## Files Created/Modified

- `scripts/check_corpus.sh` — created (Task 1); fixed for bash 3.2 compatibility (this continuation, `mapfile` -> `while read` loop)
- `.github/workflows/ci.yml` — corpus install/generate/verify step group added before `Configure` on all five legs (Task 2)

## Decisions Made

- **No cache, ever.** The corpus regenerates on every CI run. A cache keyed on the generator script or `GENERATOR_MANIFEST.json` would restore fixtures produced by a *different* ffmpeg binary on a *different* run, silently making TRUST-06 assert something other than "both encodes came from the same binary in the same run." (Recorded explicitly in 03-14-PLAN.md's `flagged_assumptions`; re-confirmed as still correct after this continuation.)
- **`scripts/gen_corpus.sh` runs on every leg, including Windows**, under Git Bash (`defaults.run.shell: bash`), rather than bringing `gen_corpus.ps1` (a Phase-1 skeleton with zero fixture recipes) to parity.
- **The bash 3.2 portability bug was fixed in the script, not worked around.** No conditional, no skip, no leniency — matching the plan's explicit instruction (Task 3's `how-to-verify` item 6) that a shell-portability failure on the Windows/macOS legs must be fixed portably, never by making a leg conditional, tolerant, or platform-restricted.
- **Findings 2-4 are out of scope and were not touched**, per explicit user instruction accompanying the real CI evidence. Each is now recorded in `.planning/WINDOWS.md` (entries #9, #10, #11) so phase verification and a future gap-closure plan can pick them up without re-discovering them from a CI log.

## Real CI Evidence

**Source:** PR #3 (https://github.com/dkastsenich/mediadiff/pull/3), CI run 33951407521. This is a real GitHub Actions run triggered by pushing the branch, not a simulation or local approximation — the first such evidence this project has produced for this workflow (STATE.md's BUILD-01/BUILD-05/BUILD-06 blocker, predating this plan, is now partially resolved by this run's existence).

**Per-leg outcome as reported:**

| Leg | Result | Cause |
|---|---|---|
| `lint (ENG-16 boundary)` | Passed | — |
| macOS x64-osx | **Failed** (before this continuation's fix) | `scripts/check_corpus.sh:62: mapfile: command not found`, exit 127, plus a `sort: stdout: Broken pipe` under `-o pipefail`. macOS ships bash 3.2; `mapfile`/`readarray` are bash-4+ only. **Fixed in this continuation (`91d9d2f`)** — the offending line replaced with a portable `while IFS= read -r` loop; verified locally under bash 5.2 (no bash 3.2 binary available in this environment — see "How this was verified" below). |
| macOS arm64-osx | **Failed** — same root cause as x64-osx | Same fix applies (the script is OS-independent; the fix is not conditional on architecture). |
| x64-windows-static-md | **Failed** — build step, `src\probe\ebml_scan.cpp(348)`: `error C2059: syntax error: ')'` on `std::max(1.0, std::abs(value))`. `NOMINMAX` is not defined anywhere in the project and `ebml_scan.cpp` includes `<io.h>` on Windows, so `windows.h`'s `max` macro clobbers `std::max`. **Out of scope** — belongs to plan 03-06's `ebml_scan`, not to `03-14`'s declared `files_modified` (`scripts/check_corpus.sh`, `.github/workflows/ci.yml`). Recorded as WINDOWS.md #9, not fixed here. |
| x64-linux | **Failed** — 5 of 620 tests, all golden comparisons (`unit.inspect_container`, `ts_scan_golden` x3, `integration.size_checks`) | Committed byte-level goldens were generated against a local ffmpeg master snapshot (`N-126086-ge5ecfe8970-20260812`) while CI's own install step provisions ffmpeg 9.0.1; different muxer output invalidates byte-identical goldens across builds. This is a genuine open design question (pin ffmpeg version in CI vs. regenerate/version-tolerant goldens) explicitly not this plan's to decide. Recorded as WINDOWS.md #10, not fixed here. |
| arm64-linux (non-blocking) | **Failed** (non-gating) | `Register vcpkg NuGet feed (read-write, trusted runs only)` exited 1 — a credentials/infra problem, unrelated to the corpus. Recorded as WINDOWS.md #11, not fixed here. |

**What this run DID prove, unambiguously:**
- The corpus install/generate/verify step group runs, in the correct order (before `Configure`), on all five matrix legs — this plan's own declared deliverable.
- The macOS legs' only failure was the corpus preflight's own bash-3.2 bug, now identified and fixed.
- The x64-windows-static-md and x64-linux failures are unrelated to the corpus wiring: the Windows failure occurred at the `Build` step (after the corpus steps had already succeeded), and the x64-linux failure occurred at the `Test` step against a fully-generated corpus (only 5 of 620 tests failed, all golden-comparison tests, not "could not open input" — meaning the corpus itself was present and complete).

**What this run did NOT prove, and this SUMMARY does not claim:**
- **Full five-leg green was NOT achieved.** Two of the three blocking legs (x64-windows-static-md, x64-linux) remain red for reasons entirely outside this plan's scope. Task 3's acceptance criterion ("all three blocking legs are green on a real run with the corpus steps in place") is **not satisfied** by this plan alone, and this SUMMARY does not pretend otherwise or weaken the criterion to fit the result.
- `test_trust06_idempotence`'s actual pass/fail status on x64-linux specifically was not separately confirmed in the user's report (the 5 failing tests named were golden-comparison tests, not TRUST-06 itself — TRUST-06 compares two same-run encodes to each other, which is orthogonal to a committed golden's staleness, but this was not independently re-confirmed against the run log by this SUMMARY's author, since findings 2-4 were declared out of scope and not investigated further).
- The re-run confirming the macOS fix (this continuation's `91d9d2f`) has **not yet been observed on CI** — it is verified locally (bash 5.2, functionally equivalent negative-test suite) but not on a real macOS runner, since re-triggering CI is the orchestrator's responsibility, not this executor's (per `<sequential_execution>` instructions).

## How the bash-3.2 fix was verified

No bash 3.2 binary was available in this sandbox (only bash 5.2.21 is installed). Verification consisted of:

1. **Static review:** confirmed `scripts/check_corpus.sh` (post-fix), `scripts/gen_corpus.sh`, and `.github/workflows/ci.yml` contain no other bash-4-only construct (`mapfile`, `readarray`, `declare -A`, `${var^^}`/`${var,,}`, `local -n`) — only the fixed line matched before the change, and zero real usages match after.
2. **Functional re-run under bash 5.2** (a superset of bash 3.2's relevant behavior for `while read` + process substitution, which is POSIX-vintage and has worked unchanged since bash 2.x): full `bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh` — 80/80 fixtures verified, exit 0.
3. **Re-ran all three of the prior executor's negative tests** against the fixed script: fixture moved aside -> exit 1, named exactly; fixture truncated to 0 bytes -> exit 1, named exactly, correct "empty" wording; `gen_corpus.sh` renamed away -> exit 1, zero-file-guard message, never 0. All three passed identically to the pre-fix script's documented behavior (the fix touches only how the expected-name array is populated, not the presence-checking logic itself).
4. **`bash -n scripts/check_corpus.sh`** parses clean; `grep -c 'mp4_faststart\|idem_a\|ts_multiprogram' scripts/check_corpus.sh` still reports `0` (no fixture name hard-coded).

This is a reasonable-confidence local verification, not a real-bash-3.2 or real-macOS-runner confirmation — that confirmation is deferred to the next CI run, which is outside this executor's remit.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking, confirmed by real CI evidence] `scripts/check_corpus.sh` used a bash-4-only builtin, breaking both macOS legs**
- **Found during:** Task 3's checkpoint — real CI run (PR #3, run 33951407521)
- **Issue:** `mapfile -t EXPECTED < <(extract_expected_names "$GEN_SCRIPT")` at line 62 used `mapfile`, a bash-4+ builtin absent from macOS's shipped bash 3.2, causing `command not found` (exit 127) and a downstream broken-pipe error under `-o pipefail`.
- **Fix:** Replaced with `EXPECTED=(); while IFS= read -r _expected_name; do EXPECTED+=("$_expected_name"); done < <(extract_expected_names "$GEN_SCRIPT")` — identical behavior on bash 3.2 and 4+, zero-file guard and derive-from-generator property unchanged.
- **Files modified:** `scripts/check_corpus.sh`
- **Verification:** Full local re-run (80/80 fixtures) plus all three negative tests (missing/empty/generator-absent) pass identically to pre-fix behavior; `bash -n` clean; confirmed no other bash-4-only construct remains in this file, `gen_corpus.sh`, or `ci.yml`.
- **Committed in:** `91d9d2f`

---

**Total deviations:** 1 auto-fixed (1 blocking, Rule 3, confirmed necessary by real CI evidence rather than speculative).
**Impact on plan:** The fix was strictly in-scope (the exact file the plan's `files_modified` names) and was mandated by the plan's own Task 3 instructions ("a portability failure like this is fixed by fixing the script — never by a conditional, a skip, or a tolerance"). No scope creep — findings 2-4 were explicitly left untouched despite being visible in the same CI log.

## Issues Encountered

- **Full-matrix green was not achievable within this plan's scope.** Two of the three blocking legs (x64-windows-static-md, x64-linux) fail for reasons entirely unrelated to the corpus wiring this plan built — a pre-existing `NOMINMAX`/`std::max` macro clash in `src/probe/ebml_scan.cpp` (plan 03-06's file) and pre-existing ffmpeg-version-drifted byte-level goldens (a project-wide design question about golden pinning/regeneration policy). Both are recorded in `.planning/WINDOWS.md` (#9, #10) for phase verification and a future gap-closure plan, per explicit user instruction not to fix them here.
- **The re-triggered CI run confirming the macOS fix has not yet happened.** This executor does not push to origin or re-trigger CI (per `<sequential_execution>`); that is the orchestrator's responsibility for a subsequent verification pass.

## Next Phase Readiness

- The corpus-wiring gap (WINDOWS.md #8, TRUST-06/DOC-03's CI half) is closed and marked `fixed` in the ledger.
- Three new, real, out-of-scope defects are now tracked (WINDOWS.md #9, #10, #11) and ready for phase verification to route into a gap-closure plan or an explicit deferral decision.
- STATE.md's standing blocker about `gen_corpus.sh` never being invoked in CI should be updated to reflect: (a) the gap is now closed with real CI evidence it was five legs, not four, as this plan's objective corrected; (b) the corpus wiring itself is proven; (c) full five-leg green remains blocked on #9 and #10, unrelated to the corpus.
- Recommend the next CI push (post-macOS-fix) be watched specifically for: both macOS legs going green at the corpus-verify step, and no new corpus-related failure appearing now that the bash-3.2 bug is fixed.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-05*

## Self-Check: PASSED

- FOUND: `scripts/check_corpus.sh`
- FOUND: `.planning/phases/03-probe-layer-container-size/03-14-SUMMARY.md`
- FOUND commit: `1e5217e` (Task 1)
- FOUND commit: `ac72738` (Task 2)
- FOUND commit: `91d9d2f` (Task 3 checkpoint follow-up fix)
