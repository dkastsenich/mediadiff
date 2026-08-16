---
status: testing
phase: 02-core-engine
source: [02-VERIFICATION.md]
started: 2026-08-16T13:20:00Z
updated: 2026-08-16T13:20:00Z
round: 2
prior_round: "2026-08-15/16 — 2 tests, 1 passed, 1 blocker issue. That blocker produced gaps G-02-1 and G-02-2, both now closed by plans 02-12 and 02-13. Full prior record in git history and in 02-VERIFICATION.md."
---

## Current Test

number: 1
name: CI legs go green with the gap fixes applied
expected: |
  On a CI run that actually contains the gap-closure commits:
  - `build (x64-windows-static-md)` compiles clean through all 97 build steps — not merely
    past the former C4996 stop at step 32 — and its own test run passes.
  - `build (arm64-osx)` executes past the ctest count guard and the 297-test suite passes —
    not merely that the guard stops aborting.
awaiting: user response

## Tests

### 1. CI legs go green with the gap fixes applied
expected: |
  On a CI run that actually contains the gap-closure commits:
  - `build (x64-windows-static-md)` compiles clean through all 97 build steps — not merely
    past the former C4996 stop at step 32 — and its own test run passes.
  - `build (arm64-osx)` executes past the ctest count guard and the 297-test suite passes —
    not merely that the guard stops aborting.
why_human: |
  Neither fix has ever been exercised against its real target. MSVC / `_dupenv_s` / `/W4 /WX`
  cannot run on this Linux host, and BSD sed cannot run here either — 02-13 verified only via
  GNU sed's `--posix` emulation, a documented approximation rather than BSD sed itself.

  More decisively: the local branch `gsd/phase-2-core-engine` is 14 commits ahead of origin.
  None of the gap-closure work has been pushed, so the only CI run on PR #2 (31937349647)
  still reflects the pre-fix code and is FAILURE on both legs. There is no CI evidence, old
  or new, that either leg is green with these fixes applied.

  Both plans' own verification sections say exactly this and warn against assuming one fix
  produces one green run: 65 of 97 MSVC build steps have never been compiled in any run, and
  the full macOS test suite has never executed to completion.
result: [pending]

### 2. Colour renders as real styling in a Windows console
expected: |
  Running mediadiff's compare / dir / TTY report output in cmd.exe or Windows Terminal, with
  the `SetConsoleMode` `ENABLE_VIRTUAL_TERMINAL_PROCESSING` path exercised, shows colour as
  actual ANSI-interpreted styling rather than literal escape-sequence bytes.
why_human: |
  Carried forward unchanged from round 1's test 1, which could not be reached because no
  Windows binary was ever produced. Blocked on the same push as test 1 above — once a green
  Windows build exists, this becomes reachable for the first time in the phase's history.
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Deferred Follow-Ups

<!-- Carried forward from round 1. Confirmed still open; not a gap, does not block Phase 2. -->

- test: 2
  idea: "Severity colour applies only to the status glyph; the summary line (`pass:0 info:0 warn:1 ...`) and check ids are unstyled. CLI-08 and REPORT-02 specify when colour is on/off, never what gets coloured, so glyph-only satisfies the written spec. Revisit whether a diff gate whose job is making severity legible at a glance should style more than one character — and if so, write the answer down, since the absence of a sample TTY render in claude_docs/ is what left this open."
  deferred_at: 2026-08-16
  not_a_gap: "Scope ambiguity in the spec, not a defect. Does not block Phase 2."

- source: 02-REVIEW-GAPS.md (WR-01)
  idea: "`getenv_utf8` (src/util/fs.h) carries no documented thread-safety contract, unlike its sibling `utf8_to_wide` in the same header which states one explicitly. No call site runs off the main thread today — all six execute before any thread spawns — but nothing stops a future call site landing inside worker_pool.cpp's job callbacks, where the POSIX `std::getenv` branch is non-reentrant."
  deferred_at: 2026-08-16
  not_a_gap: "Not exploitable in current code; a documentation/robustness improvement."

- source: 02-REVIEW-GAPS.md (IN-01)
  idea: "`<cstdlib>` in src/cli/options.cpp is a dead include after the gap diff removed its sole user (`read_env`'s `std::getenv` call)."
  deferred_at: 2026-08-16
  not_a_gap: "Cosmetic."

## Gaps
