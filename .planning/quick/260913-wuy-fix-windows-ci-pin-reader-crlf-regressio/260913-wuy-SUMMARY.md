---
phase: quick
plan: 01
subsystem: infra
tags: [bash, ci, ffmpeg, gen_corpus, windows]

requires: []
provides:
  - "mediadiff_read_ffmpeg_pin (scripts/resolve_pinned_ffmpeg.sh) tolerates a trailing CR on every python3-reader line it consumes"
  - "Three distinguishable pin-reader failure messages: CR-terminated OK now resolves; empty output reports 'produced no output'; any other unexpected first line names itself, print-safe and truncated at 120 chars"
  - "scripts/test_gen_corpus_pin_gate.sh Cases 8-11 proving CRLF/LF/empty/garbage reader-output handling, plus a make_pin_reader_stub + pin_version test harness for future reader changes"
affects: [ci, windows-leg, gen_corpus]

actuals:
  tokens: 3677
  tasks: 2
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Reader-output-shape test harness (make_pin_reader_stub) that emits precomputed real-python3 output through a stub, verified via a revert-and-observe-failure demonstration run against a mktemp copy of scripts/, never the tracked file"

key-files:
  created: []
  modified:
    - scripts/resolve_pinned_ffmpeg.sh
    - scripts/test_gen_corpus_pin_gate.sh

key-decisions:
  - "Closed all three reachable pin-reader failure shapes (CRLF, empty output, unexpected first line), not just the CRLF hypothesis from the CI log, since the empty-output case was independently confirmed reachable and previously silently indistinguishable from CRLF in the log"
  - "Deleted the unreachable post-loop line_num==0 guard and moved its message to a pre-loop check that can actually fire (a here-string over an empty string still iterates once)"
  - "Left the candidate probe loop, FFMPEG_VERSION_LINE/TOKEN and install_pinned_ffmpeg.sh's own reader unchanged, per the plan's audit verdicts -- documented as an 'output-consumption audit' paragraph in the function's header comment"

requirements-completed: [BUILD-08, TRUST-06]

coverage:
  - id: D1
    description: "CRLF-terminated pin-reader output (OK\\r\\n, version\\r\\n, candidates\\r\\n) is tolerated and resolves the pinned ffmpeg candidate exactly as LF output does"
    requirement: BUILD-08
    verification:
      - kind: integration
        ref: "scripts/test_gen_corpus_pin_gate.sh#Case 8 (CRLF tolerated)"
        status: pass
      - kind: integration
        ref: "scripts/test_gen_corpus_pin_gate.sh#Case 9 (LF control)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Empty pin-reader output is reported as 'produced no output while reading <pin file>' and fails the identity gate closed (never silently accepted)"
    requirement: TRUST-06
    verification:
      - kind: integration
        ref: "scripts/test_gen_corpus_pin_gate.sh#Case 10 (empty reader output)"
        status: pass
    human_judgment: false
  - id: D3
    description: "An unexpected first line (neither OK nor ERROR...) is named in the error text, print-safe and truncated at 120 characters"
    requirement: TRUST-06
    verification:
      - kind: integration
        ref: "scripts/test_gen_corpus_pin_gate.sh#Case 11 (unexpected first line named)"
        status: pass
    human_judgment: false
  - id: D4
    description: "The CRLF fix is demonstrably necessary: reverting the CR-strip line causes Case 8 to fail while Case 9 (LF control) still passes, run against a temp copy of scripts/ so the tracked reader is never mutated"
    verification:
      - kind: manual_procedural
        ref: "mktemp -d strip-reverted sandbox run (see Verification Results below); FAIL: Case 8 / PASS: Case 9 / PASS_REVERT_DEMO observed"
        status: pass
    human_judgment: false
  - id: D5
    description: "Whether the Windows CI leg actually goes green on a real push is unresolved by this task -- only provable by a CI run on a pushed commit"
    verification: []
    human_judgment: true
    rationale: "No commit was pushed during this task's execution (constraint: do not push); a real Windows-leg CI run is the only proof of the underlying incident's resolution."

duration: ~15min
completed: 2026-09-14
status: complete
---

# Quick Task 260913-wuy: Windows CI Pin-Reader CRLF Regression Summary

**Made `mediadiff_read_ffmpeg_pin` tolerate a trailing CR on every consumed line and made its three failure shapes (CR-terminated OK, empty output, unexpected first line) distinguishable in the one error line CI prints, backed by a four-case test extension (Cases 8-11, 40 assertions total) including a revert-and-observe-failure demonstration proving the CRLF fix is load-bearing.**

## Performance

- **Duration:** ~15 min
- **Completed:** 2026-09-14
- **Tasks:** 2 (plus one same-file follow-up fix commit, see Deviations)
- **Files modified:** 2 (`scripts/resolve_pinned_ffmpeg.sh`, `scripts/test_gen_corpus_pin_gate.sh`)

## Accomplishments

- `mediadiff_read_ffmpeg_pin` strips one trailing CR from every reader-loop line before it is matched or stored (`line=${line%$'\r'}`), so a CRLF-terminated `OK` from Windows' python3 resolves the pin instead of falling into the `*)` "unexpected output" arm.
- Moved the empty-reader-output guard from an unreachable post-loop `line_num -eq 0` check (dead code -- a here-string over `""` still iterates once) to a reachable pre-loop check, so `python3 produced no output while reading <pin file>` can now actually fire.
- Named the offending first line in the `*)` arm's error message, rendered through a printable-only filter and truncated at 120 characters, so a red Windows leg is diagnosable from the log text alone.
- Extended `scripts/test_gen_corpus_pin_gate.sh` from 7 to 11 cases (25 to 40 assertions): Case 8 (CRLF tolerated), Case 9 (LF control, same harness -- isolates "CR handling" from "stub broke something"), Case 10 (empty reader output), Case 11 (unexpected first line named).
- Ran the plan's mandated revert-and-observe-failure demonstration against a `mktemp -d` copy of `scripts/`, never the tracked file: with the CR-strip line removed, Case 8 fails and Case 9 still passes (`PASS_REVERT_DEMO`).

## Task Commits

1. **Task 1: Make the pin reader CR-tolerant and its three failure shapes distinguishable** - `4f351b4` (fix)
2. **Follow-up fix (same file, Rule 1):** use unquoted CR-strip form - `6efed2e` (fix)
3. **Task 2: Cover CRLF, LF, empty and garbage reader output in the CI gate test** - `5034c7c` (test)

## Files Created/Modified

- `scripts/resolve_pinned_ffmpeg.sh` - CR strip in `mediadiff_read_ffmpeg_pin`'s reader loop; empty-output guard moved before the loop; unexpected-first-line message now names the offending line; header comment gained an "output-consumption audit" paragraph.
- `scripts/test_gen_corpus_pin_gate.sh` - added `pin_version` and `make_pin_reader_stub` helpers; added Cases 8-11; updated header case-count/observable-signal/deliberate-limits bookkeeping and the summary echo (7 -> 11 cases).

## Decisions Made

- Closed all three reachable pin-reader failure shapes rather than only the CRLF hypothesis named in the CI incident, per the plan's own design_decisions (the empty-output case was independently confirmed reachable and, until this task, indistinguishable in the log from a CR-terminated `OK`).
- Left the candidate probe loop and the ffmpeg `-version` line/token consumers unchanged (already CR-tolerant or out of scope), matching the plan's audit verdicts.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Unquoted CR-strip form required for the test's own verification pattern**
- **Found during:** Task 2, while building the mandated revert-and-observe-failure demonstration
- **Issue:** Task 1 implemented the strip as `line="${line%$'\r'}"` (quoted). The plan's own Task 2 `<verify>` block and acceptance criteria require locating this line by the exact literal text `line=${line%` (unquoted form) via `awk`/`grep` to build the strip-reverted sandbox copy. The quoted form's embedded `"` character breaks that literal match, so `grep -c 'line=\${line%'` reported 0 against the tracked file instead of the required 1, and the strip-reverted-copy technique could not isolate the CR-strip line at all.
- **Fix:** Changed to the unquoted form `line=${line%$'\r'}` -- functionally identical (assignment-context suppresses word splitting/globbing either way in bash 3.2+), but matches the plan's literal grep/awk pattern.
- **Files modified:** `scripts/resolve_pinned_ffmpeg.sh`
- **Verification:** `bash scripts/test_gen_corpus_pin_gate.sh` (40/40 pass), `bash -n`, `bash scripts/lint_bash4_builtins.sh`, and the full strip-reverted sandbox demonstration all re-run and passing after the change.
- **Committed in:** `6efed2e` (separate commit, not an amend, since Task 1's commit `4f351b4` had already landed)

---

**Total deviations:** 1 auto-fixed (Rule 1 - bug in already-committed Task 1 code, discovered by Task 2's own mandated verification)
**Impact on plan:** Necessary for Task 2's acceptance criteria to be satisfiable at all; no scope creep, no behavior change beyond matching the plan's literal verification pattern.

## Issues Encountered

None beyond the deviation above.

## Verification Results

- `bash scripts/test_gen_corpus_pin_gate.sh` -- **before:** 25 assertions across 7 cases, 0 failures. **after:** 40 assertions across 11 cases, 0 failures. Exit 0, prints `across 11 cases; 0 failure(s)`.
- New CRLF case (Case 8) fail-before/pass-after evidence: ran the suite against a `mktemp -d` copy of `scripts/` with the CR-strip line (`line=${line%$'\r'}`) removed via `awk`. Result: `FAIL: Case 8 (CRLF tolerated): exit code -- expected exit zero, got 1.` plus 4 further Case 8 assertion failures (route, `.ffmpeg-pinned/`, no `pin unreadable`, no `release-identity mismatch`), while `PASS: Case 9 (LF control)` reported all 5 assertions passing -- reverted-sandbox summary: `5 failure(s)`, ending in `PASS_REVERT_DEMO`. The tracked `scripts/resolve_pinned_ffmpeg.sh` was verified byte-identical to HEAD after the demonstration (`git diff --quiet` succeeded).
- `bash scripts/lint_bash4_builtins.sh` -- clean, both before and after every edit: "Scanned 18 file(s) under scripts/*.sh; no bash-3.2-incompatible construct found."
- `bash scripts/lint_corpus_digest_provenance.sh` -- all 4 clauses passed, unchanged.
- `bash scripts/resolve_pinned_ffmpeg.sh` (direct run, real local python3/LF output, real pinned ffmpeg present) -- exit 0, `FFMPEG_ROUTE=pinned`, reports `9.0.1-https://www.martin-riedl.de`, pin expects `9.0.1` -- names a real pin version in both the stderr line and the acceptance-criteria grep, never `pin unreadable`.
- `ctest --preset x64-linux --output-on-failure` -- **100% tests passed, 0 tests failed out of 771** (6 pre-existing golden/console tests skipped, as expected on this workstation; unchanged from before this task).
- `git status --porcelain` after all three commits -- only `.planning/` bookkeeping (`.planning/milestone.lock`, `.planning/quick/260913-wuy-.../`, `.planning/state.json`) remains untracked; no fixture, golden, digest, workflow, or `gen_corpus.*` file changed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The committed reader now accepts CRLF-terminated pin-reader output and distinguishes empty-output and unexpected-first-line failures in its error text -- the next Windows CI round (a real push, not performed here) should either go green or, if it does not, name the actual offending line instead of the generic "unexpected output" message that started this task.
- **Unresolved, stated honestly per the plan's own success criteria:** this task did not push a commit or trigger a real CI run, so whether the Windows leg (draft PR #5, run 34776142545) actually goes green is not provable from this task's execution alone. If it does not, the new diagnostic gives the next round real evidence (the offending first line) instead of a second hypothesis.

---
*Phase: quick*
*Completed: 2026-09-14*

## Self-Check: PASSED

- FOUND: scripts/resolve_pinned_ffmpeg.sh
- FOUND: scripts/test_gen_corpus_pin_gate.sh
- FOUND: .planning/quick/260913-wuy-fix-windows-ci-pin-reader-crlf-regressio/260913-wuy-SUMMARY.md
- FOUND: 4f351b4 (fix: CR-tolerance + failure-shape naming)
- FOUND: 6efed2e (fix: unquoted CR-strip form)
- FOUND: 5034c7c (test: Cases 8-11)
