---
phase: 02-core-engine
plan: 19
subsystem: testing
tags: [cli11, argv-parsing, exit-codes, windows, ci-read]

requires:
  - phase: 02-core-engine
    provides: "02-17 (.gitattributes + stdout/stderr binary mode + TRUST-05 test), 02-18 (process-spawn binary fixture + non-ASCII path fixture)"
provides:
  - "app.allow_windows_style_options(false) on the root CLI::App, closing the last of the three round-4 Windows gaps (G-02-7's #236 half)"
  - "CI run 32153890395 (headSha d727425) — the first run in the project's history where all four required contexts are green simultaneously, including Windows"
affects: ["02-UAT.md Round-4 Gaps", "02-VERIFICATION.md human_verification block", "UAT test 2 (now schedulable against sha d727425)"]

actuals:
  tokens: 805
  tasks: 2
  commits: 1

tech-stack:
  added: []
  patterns:
    - "argv classification pinned identical cross-platform at one call site, same placement discipline as allow_subcommand_prefix_matching (parent-before-child, CLI11 INHERITABLE block)"

key-files:
  created: []
  modified:
    - src/cli/main.cpp
    - tests/integration/test_exit_codes.cpp

key-decisions:
  - "Placed allow_windows_style_options(false) immediately after the existing allow_subcommand_prefix_matching(false) call, before set_version_flag and before any register_*_command call -- verified against the vendored CLI11 2.6.2 headers (vcpkg/packages/cli11_x64-linux/include/CLI/App.hpp:259-267) rather than trusting the plan's claim: allow_windows_style_options_ defaults to true under _WIN32 and is marked INHERITABLE, copied parent-to-child at child-App construction time."
  - "Pushed via the pre-resolved GIT_SSH_COMMAND workaround (personal key), not the gh HTTPS token -- this round touches no file under .github/workflows/, so the workflow-scope restriction should not have applied, but the plan's own instruction was to use the proven-working SSH form regardless, and it succeeded on the first attempt."

requirements-completed: [BUILD-01, BUILD-05, CLI-06]

coverage:
  - id: D1
    description: "app.allow_windows_style_options(false) set once on the root App, above every subcommand registration; exit_code.cpp/.h untouched; both == 65 assertions survive verbatim; two new stderr assertions prove path classification; no TEST_CASE added; suite stays at 298"
    requirement: "CLI-06"
    verification:
      - kind: unit
        ref: "ctest --test-dir build/x64-linux -R exit_codes -- all 12 exit_codes cases Passed, including all four 64 cases and both 65 cases"
        status: pass
      - kind: integration
        ref: "CI run 32153890395, job 95766278227 (build (x64-windows-static-md)) -- integration.exit_codes - 65: a nonexistent input path exits exactly 65 -> Passed; grep -c '64 == 65' over the job log = 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "A CI run whose headSha equals the pushed HEAD exists, and all six jobs were read from that run's own logs -- the round-4 verdict itself"
    requirement: "BUILD-01"
    verification:
      - kind: other
        ref: "CI run 32153890395 (headSha d7274258ad73b61a4927721f0020e66099e65912) -- all four required contexts success: lint (ENG-16 boundary), build (x64-linux) 298/298, build (arm64-osx) 298/298, build (x64-windows-static-md) Build success + Test 298/298"
        status: pass
    human_judgment: true
    rationale: "Whether this run's green result certifies the round -- versus still-open items like UAT test 2, the allow_windows_style_options(false) posture, and re-confirmed deferral of the two non-required legs -- is Task 3's human checkpoint decision, not something this plan can self-certify."

duration: 18min
completed: 2026-08-18
status: complete_with_checkpoint_pending
---

# Phase 02 Plan 19: Argv classification fix and the round-4 CI read Summary

**`app.allow_windows_style_options(false)` closed the last Windows divergence (#236, 64 vs 65); the pushed run (32153890395, headSha `d727425`) is the first in the project's history where all four required CI contexts — lint, x64-linux, arm64-osx, and Windows — are simultaneously green, with all seven round-3 failures and the new TRUST-05 test confirmed `Passed` by name.**

Nothing is red among the four required contexts. Leading with that because the whole point of this round was not to average a partial read into "CI is green" — every one of the following claims is sourced directly from run `32153890395`'s own job logs, read by job name and by test name, never inferred from a prior run or the local build.

## Performance

- **Duration:** 18 min (Task 1 local fix/build/test: ~8 min; Task 2 push + CI wait + log read: ~10 min)
- **Tasks:** 2 (Task 3 is the checkpoint this SUMMARY hands off to)
- **Files modified:** 2

## Accomplishments

- `src/cli/main.cpp`: `app.allow_windows_style_options(false);` added immediately after the existing `allow_subcommand_prefix_matching(false)` call (line 82, before the first `register_` call at line 92) — verified against the vendored CLI11 2.6.2 headers that the member defaults to `true` under `_WIN32` and is copied parent-to-child in the `INHERITABLE` block, so placement above subcommand registration is load-bearing.
- `tests/integration/test_exit_codes.cpp`: the `exit_codes - 65: a nonexistent input path exits exactly 65` case keeps `CHECK(result.exit_code == 65)` verbatim and gains two stderr assertions (`could not open snapshot file: ` and the offending path), proving the argument reached `read_snapshot` rather than merely producing the right number.
- `src/cli/exit_code.cpp`/`.h` untouched (`git diff --stat` empty) — confirmed by direct read that the mapping was never the defect.
- Local suite: 298 tests, 0 failed, 1 skipped (`unit.console_vt`). All four lint scripts exit 0. `ctest -R exit_codes` — all 12 cases pass, including all four `64` cases (unaffected) and both `65` cases.
- Branch pushed via `GIT_SSH_COMMAND` pinning `~/.ssh/github_dkastsenich_personal` (the pre-resolved workaround); `git rev-parse HEAD` == `git rev-parse origin/gsd/phase-2-core-engine` == `d7274258ad73b61a4927721f0020e66099e65912`.
- CI run `32153890395` selected by headSha string-equality (not "most recent"). Read job by job, by name, from that run's own logs.

## Task Commits

1. **Task 1: Make argv classification identical on every platform** — `d727425` (fix)

**Task 2** produced no code artifact (push + CI read); this SUMMARY is its output, per the plan's `<files>` list.

No separate plan-metadata commit yet — deferred to the orchestrator per this plan's dispatch instructions (STATE.md/ROADMAP.md updates and the final `docs(02-19): ...` commit happen after Task 3's checkpoint resolves, since Task 3 may still redirect scope).

## Run — the round-4 oracle

**Run:** [32153890395](https://github.com/dkastsenich/mediadiff/actions/runs/32153890395)
**headSha:** `d7274258ad73b61a4927721f0020e66099e65912` — string-equal to the pushed local HEAD, verified before the read began.
**Baseline measured against:** run `32127416710`, headSha `78a6bba` (Windows Build `success`, Test `98% tests passed, 7 tests failed out of 297`, zero C4702/C2220/C4996; other legs: lint success, x64-linux success, arm64-osx success, x64-osx failure, arm64-linux failure).

### Six-row per-job table

| Job | id | Conclusion | Specific observable |
|---|---|---|---|
| `lint (ENG-16 boundary)` | 95766277880 | ✅ success | All four lint steps ran and succeeded by name: `Run ENG-16 boundary lint` → "ENG-16: clean...", `Run D-03 check-id string-literal lint` → "clean. No dotted check-id string literals...", `Run dead-code-after-FAIL portability lint` → "clean. Scanned 50 file(s)...", `Run fixture case-collision portability lint` → "clean. Scanned 50 file(s)...". |
| `build (x64-linux)` | 95766278141 | ✅ success | `100% tests passed, 0 tests failed out of 298`. Up from the 297-test baseline by exactly the one TEST_CASE 02-17 added. No regression. |
| `build (arm64-osx)` | 95766278027 | ✅ success | `100% tests passed out of 298`. Same 298-count parity with x64-linux. |
| `build (x64-windows-static-md)` | 95766278227 | ✅ success | **Build step conclusion: `success`** (read separately from the job's overall conclusion via `gh api .../jobs/95766278227` → `Build success`, `Test success`). `grep -c` over the 2137-line job log: `C4996` = 0, `C4702` = 0, `C2220` = 0. `ctest discovered 298 tests in build/x64-windows-static-md.` **Test step conclusion: `success`** — `100% tests passed, 0 tests failed out of 298`. |
| `build (x64-osx)` | 95766278109 | ❌ failure | Out of scope, unchanged. `ld: warning: ignoring file 'vcpkg_installed/x64-osx/lib/libavformat.a': fat file missing arch 'arm64', file has 'x86_64'` (and four sibling libs). Matches the `CI-x64-osx` signature recorded in `02-UAT.md`. Not counted against the four required contexts. |
| `build (arm64-linux)` | 95766278139 | ❌ failure | Out of scope, unchanged. Failing step is `Register vcpkg NuGet feed (read-write, trusted runs only)`, exits 1. Matches the `CI-arm64-linux` signature recorded in `02-UAT.md`. Not counted against the four required contexts. |

**The four required contexts are all `success`, simultaneously, for the first time in this project's history.**

## The seven round-3 failures, looked up BY NAME (numbers are no longer stable — the suite grew from 297 to 298 when 02-17 added TRUST-05; every test after the insertion point shifted, though these seven happen to have landed at their prior numbers again in this run)

| Test (by name) | Owning plan | Round-3 result | This run's result |
|---|---|---|---|
| `unit.dir_pairing: a tree containing a non-ASCII filename pairs correctly` | 02-18 | `"rÃ©sumÃ©..." == "résumé..."` mojibake | **Passed** (Test #36, 0.01s) |
| `unit.junit - golden: suites appear in group order and testcases in registry order` | 02-17 | golden mismatch at line 1 | **Passed** (Test #67, 0.01s) |
| `unit.markdown_budget - golden: a fixed small model renders byte-identical Markdown` | 02-17 | golden mismatch at line 1 | **Passed** (Test #75, 0.01s) |
| `unit.process_spawn: captures more than one pipe buffer's worth of stdout and stderr exactly` | 02-18 | `85000 (0x14c08) == 82500 (0x14244)` | **Passed** (Test #95, 0.03s) |
| `unit.tty - golden: a fixed model at a fixed width matches the recorded golden` | 02-17 | golden mismatch | **Passed** (Test #201, 0.01s) |
| `integration.exit_codes - 65: a nonexistent input path exits exactly 65` | 02-19 (this plan) | `64 == 65` | **Passed** (Test #236, 0.01s) |
| `integration.explain_inspect - REPORT-07: inspect on a snapshot with no meta measurements still names meta with the no-measurements line` | 02-17 (downstream, deliberately untouched) | `found_meta_no_measurements` → false | **Passed** (Test #247, 0.01s) |

All seven are `Passed`. `grep -c 'golden mismatch'` = 0, `grep -c '85000'` = 0, `grep -c '64 == 65'` = 0, `grep -c 'sumÃ'` = 0 over the full Windows job log.

## The #247 experiment — answered

`tests/integration/test_explain_inspect.cpp` was deliberately left unmodified by every plan in this round (02-17, 02-18, 02-19), specifically so this signal would stay legible. **#247 now passes.** Since nothing in that file changed, its pass is explained entirely by 02-17's stdout binary-mode fix landing upstream of it — `found_meta_no_measurements`'s line-by-line stdout scan no longer has CRLF to defeat the match. **G-02-7 shrinks to its #236 half**, which this plan's own change (Task 1) closed directly.

## The four wrong-reason passes — re-checked, all still Passed

These passed in round 3 only because a CRLF golden happened to match CRLF stdout on the subprocess-fed path; both halves of 02-17's fix needed to land together for them to keep passing now that goldens are LF-pinned and stdout is binary (LF).

| Test | Result this run |
|---|---|
| `explain_inspect - REPORT-07: two inspect runs produce byte-identical stdout, checked against a golden` (inspect_basic) | **Passed** (Test #248) |
| `json_schema - two identical compare --json runs produce byte-identical stdout` (json_schema_basic) | **Passed** (Test #268) |
| `dir_mode - the TTY worst-N table lists the worst-severity file first with ties broken by path` (dir_worst_n) | **Passed** (Test #225) |
| `list_checks - ENG-12: --effective is byte-identical across two runs, checked against a golden` (list_checks_effective) | **Passed** (Test #273) |

Both halves landed together; none regressed.

## The new test — TRUST-05 destination parity

`json_schema - TRUST-05: --json to stdout and --json=<path> produce identical bytes` (Test #262): **Passed** on all three blocking legs — `build (x64-windows-static-md)`, `build (x64-linux)`, `build (arm64-osx)`. This is the mechanical answer to `02-VERIFICATION.md`'s second `human_verification` item (whether `--json` redirected to stdout produces the same bytes as the file destination on Windows) — it is now asserted inside CI rather than only inferable from the round-3 subprocess-golden correlation.

## Non-required legs — confirmed unchanged, not counted

- `build (x64-osx)` (job 95766278109): `ld: warning: ignoring file '...libavformat.a': fat file missing arch 'arm64', file has 'x86_64'` — identical signature to `CI-x64-osx` in `02-UAT.md`.
- `build (arm64-linux)` (job 95766278139): fails at step `Register vcpkg NuGet feed (read-write, trusted runs only)`, exit 1 — identical signature to `CI-arm64-linux` in `02-UAT.md`.

Neither is a required context. Neither is this round's problem. Both re-confirmed still deferred rather than silently absorbed into this round's verdict.

## Per-gap verdict (this run's evidence, not this plan's self-certification)

- **G-02-5** (byte-level output identical on Windows and POSIX — newline/text-mode translation): all four affected tests (`#95`, `#67`, `#75`, `#201`) `Passed`. The new TRUST-05 test also `Passed`, extending the same property to the stdout/file-destination boundary specifically. **The run's evidence supports closure.**
- **G-02-6** (non-ASCII filename round-trips unchanged on Windows): `#36` `Passed`. **The run's evidence supports closure.**
- **G-02-7** (exit-code contract and inspect output hold identically on Windows): `#236` `Passed` (this plan's own fix) and `#247` `Passed` (confirmed downstream of G-02-5, per the deliberately-untouched-file experiment above). **The run's evidence supports closure of both halves.**

Whether these become formally `closed` in `02-UAT.md`, whether any is instead carried forward pending a second look, and whether the round is certified overall is **Task 3's decision, not this plan's** — per the plan's own hard constraint against self-certifying on CI evidence alone.

## Files Created/Modified

- `src/cli/main.cpp` — `app.allow_windows_style_options(false);` added on the root `CLI::App`, with a comment naming the CLI11 default, the CLI-06 consequence, the CI run that surfaced it, and the parent-to-child placement constraint.
- `tests/integration/test_exit_codes.cpp` — two stderr assertions added to the existing 65 test case; nothing else changed.

## Decisions Made

- **Verified the CLI11 default and INHERITABLE-copy claim against the vendored headers before editing**, per the plan's `<read_first>` instruction, rather than trusting the plan's own prose: `vcpkg/packages/cli11_x64-linux/include/CLI/App.hpp:259-267` confirms `allow_windows_style_options_` defaults to `true` under `_WIN32` (`false` elsewhere) and is annotated `INHERITABLE`, matching the plan's claim exactly.
- **Used the pre-resolved `GIT_SSH_COMMAND` push form** rather than attempting the `gh` HTTPS route first, per the dispatch's explicit instruction to use the proven-working form regardless of whether this round's non-workflow-touching diff might have let HTTPS succeed. Succeeded on the first attempt; no retry needed.

## Deviations from Plan

None — plan executed exactly as written for Tasks 1 and 2. No Rule 1/2/3 auto-fixes were needed; the local build was clean on the first attempt, and the pushed run needed no re-run.

## Issues Encountered

None. The CI run completed in ~5 minutes total wall time (fastest of the round-4 sequence), and every job's log contained the expected literals on the first read.

## What This Confirms and What It Does NOT

**Confirmed by this run:**
- All four required merge-gate contexts pass simultaneously for the first time.
- All seven round-3 Windows failures, looked up by name, now pass.
- The four wrong-reason passes from round 3 still pass for the right reason (both 02-17 halves landed together).
- The new TRUST-05 destination-parity test passes on all three blocking legs, closing the `02-VERIFICATION.md` human-verification item mechanically.
- `#247` is confirmed downstream of the newline fix, not its own defect (the file was never touched).

**Does NOT establish (explicitly out of scope, per the plan and per `02-UAT.md`):**
- **UAT test 2** (colour renders as real styling in a Windows console) — CI cannot observe this; `GetConsoleMode` fails under a redirected pipe and `enable_vt_output()` correctly no-ops. The `mediadiff-windows-x64` artifact (job 95766278227, `Upload Windows verification bundle` step succeeded, 1,301,968 bytes) is available at this run's sha for a human to run against a real console. **Record the build sha alongside any result: `d7274258ad73b61a4927721f0020e66099e65912` (short: `d727425`)** — this round changed what bytes Windows stdout carries, so a console run must be against this sha or later.
- **`build (x64-osx)` and `build (arm64-linux)`** — read only to confirm their signatures are unchanged; neither fixed, neither opened as a new gap.
- **The three `02-UAT.md` §Deferred Follow-Ups** — untouched by this plan, not re-verified here.
- **Whether the round is certified** — that is Task 3's call, not this plan's.

## Next Phase Readiness

- All engineering work for round 4 (G-02-5, G-02-6, G-02-7) is landed and its CI evidence is in hand.
- Task 3 (`checkpoint:human-verify`, `gate="blocking"`) is the only remaining step in this plan. This SUMMARY is its input.
- STATE.md/ROADMAP.md and the final `docs(02-19): ...` commit are deferred until Task 3 resolves, since its outcome (certify vs. iterate) determines what those updates should say.

## Self-Check: PASSED

- `test -f src/cli/main.cpp` → FOUND (modified)
- `test -f tests/integration/test_exit_codes.cpp` → FOUND (modified)
- `git log --oneline --all | grep -q d727425` → FOUND
- `git rev-parse HEAD` == `git rev-parse origin/gsd/phase-2-core-engine` → MATCH (`d7274258ad73b61a4927721f0020e66099e65912`)
- CI run `32153890395` headSha == pushed HEAD → MATCH
- Local suite: 298 tests, 0 failed, 1 skipped (re-confirmed)
- All four lint scripts: exit 0 (re-confirmed)

---
*Phase: 02-core-engine*
*Completed: 2026-08-18*
