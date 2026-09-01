---
phase: 02-core-engine
plan: 13
subsystem: ci
tags: [ci, gap-closure, portability, sed, ctest]
status: complete

dependency-graph:
  requires: []
  provides:
    - "arm64-osx CI leg's ctest count parse no longer aborts under BSD sed"
  affects:
    - ".github/workflows/ci.yml"

tech-stack:
  added: []
  patterns:
    - "POSIX-portable sed BRE (`[0-9][0-9]*` instead of GNU-only `\\+`) for any regex that runs on both GNU and BSD sed hosts"

key-files:
  created: []
  modified:
    - ".github/workflows/ci.yml"

decisions:
  - "Used the bracket-repetition POSIX form `[0-9][0-9]*` rather than `sed -E`, per the plan's explicit instruction — both are portable, but `-E` adds an unneeded flag-support assumption"
  - "Left both guard branches (empty-capture, zero-count) untouched — the fix repairs the parse without loosening the anti-false-pass guard"

metrics:
  duration: "~25min (includes a from-scratch vcpkg configure + full x64-linux build to obtain real `ctest -N` output for verification)"
  completed: 2026-08-16

actuals:
  tokens: 711
  tasks: 1
  commits: 1
---

# Phase 02 Plan 13: Dialect-independent ctest count parse (G-02-2) Summary

Replaced the GNU-only `\+` BRE quantifier in `.github/workflows/ci.yml`'s Test-step
count parse with the POSIX-portable `[0-9][0-9]*` form, so the `arm64-osx` leg's
own test-count sanity guard stops aborting under BSD sed before any test runs.

## What Was Built

**Task 1: Make the ctest count parse dialect-independent** (commit `e403e32`)

- Changed line 246 (now shifted by the added comment) of `.github/workflows/ci.yml`
  from `sed -n 's/^Total Tests: \([0-9]\+\)$/\1/p'` to
  `sed -n 's/^Total Tests: \([0-9][0-9]*\)$/\1/p'` — pure POSIX BRE, identical
  semantics under GNU sed and BSD sed.
- Left the `sed -n` invocation, the `^Total Tests: ` anchor, the `$` terminator,
  the `\1` replacement, the `p` flag, and the trailing `|| true` untouched.
- Left both downstream guard branches untouched: the empty-capture check
  (`[ -z "$TOTAL" ]`) and the zero-count check (`[ "$TOTAL" -eq 0 ]`) remain
  distinct, each with its own diagnostic. With the portable pattern, a genuinely
  empty suite (`Total Tests: 0`) still captures the string `0`, which is
  non-empty, so control still reaches the zero-count branch rather than being
  swallowed by the empty-capture branch.
- Added a comment paragraph above the line (in the existing comment block's
  established register) documenting: (a) that macOS legs run BSD sed while
  Windows (Git Bash) and Linux run GNU sed, so GNU-only regex extensions must
  never appear in this pipeline; (b) the specific trap — BSD sed reads a
  backslash before an ordinary character as that literal character, not a
  repetition operator, so the old pattern didn't just mis-count, it matched
  nothing at all, silently emptying `TOTAL` before the guard could even reach
  its zero-count diagnostic; (c) why a comment is warranted — the commit that
  introduced the defect carried its own claim of having been verified, but that
  verification only ever ran under GNU sed, which is precisely how the defect
  shipped.

## Verification

The plan's full automated `<verify>` block was run twice: once before commit
(to confirm the fix), and once again after commit as the tracer feedback gate
(autonomous mode is active this session).

Both runs produced identical results:

- **Control clause** (proves the harness can detect the bug): the OLD pattern
  (`\([0-9]\+\)`) applied under `sed --posix` against a synthetic
  `Total Tests: 296` line captures nothing — confirming the POSIX-minimal
  dialect switch genuinely removes the extension at fault.
- **Real dialect-independence check**: `ctest --test-dir build/x64-linux -N`
  piped through the NEW pattern under `sed --posix` and under plain GNU `sed`
  both yield `296` — identical, non-zero, matching counts under both dialects.
- **Zero-count preservation**: `Total Tests: 0` still parses to the string `0`
  under `sed --posix` with the new pattern, so the zero-count branch stays
  reachable with its own diagnostic.
- **No GNU-only construct anywhere under `.github/workflows/`**:
  `grep -rF '\+' .github/workflows/` returns 0 matches (this scan also caught,
  and required rewording, an early draft of the inline comment that had
  quoted the literal `\+` sequence as prose — the fixed-string scan doesn't
  distinguish code from comments, so the comment now describes the trap in
  words instead of quoting the offending escape sequence).
- **Shipped line contains the portable pattern verbatim**: confirmed via
  `grep -qF`.

To obtain real `ctest -N` output for this verification, the `vcpkg` submodule
was initialized and the `x64-linux` preset was configured and built from
scratch in this worktree (no prior build tree existed here). vcpkg's binary
cache was warm, so the FFmpeg dependency resolution completed in under a
second; the mediadiff build itself (~90 object files, two test binaries)
completed in a few minutes.

**Honesty about verification (carried from the plan).** BSD sed itself was not
executed — this sandbox has no macOS host. The verification instead used GNU
sed's `--posix` dialect switch, which removes the exact extension at fault and
reproduces the failure mode exactly (confirmed by the control clause), but it
is an emulation of BSD-sed-like behavior, not a direct BSD sed run. Fixing the
parse **unblocks** the `arm64-osx` Test step; it does **not** guarantee the leg
turns green. The 296-test suite has never executed to completion on macOS in
any CI run (the last successful macOS run, before the suite grew, executed 11
tests). A follow-up may be needed if genuine macOS-specific test failures are
revealed once the guard no longer intercepts the run.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Reworded the inline comment to avoid tripping the plan's own portability scan**
- **Found during:** Task 1, first verification attempt
- **Issue:** The comment's first draft illustrated the BSD-sed trap by quoting
  the literal escape sequence backslash-plus in prose (e.g. "reads a backslash
  quantifier (e.g. `\+`) as a literal character"). The plan's `<verify>` step
  includes a fixed-string scan (`grep -rF '\+' .github/workflows/`) asserting
  zero occurrences of that exact string anywhere in the directory — a
  content-blind check that doesn't distinguish code from comments. The comment
  text itself tripped it.
- **Fix:** Reworded the affected sentences to describe the trap in words
  ("a backslash followed by an ordinary character", "a GNU-only one-or-more
  quantifier") without quoting the literal offending sequence. No change to
  the executable pattern itself, which was already correct.
- **Files modified:** `.github/workflows/ci.yml`
- **Commit:** `e403e32` (folded into the single task commit, since this was
  caught before commit)

Otherwise: plan executed exactly as written — no other deviations.

## Auth Gates

None encountered.

## Known Stubs

None. This plan touches only a CI workflow file; no application code, no data
paths, nothing that could stub UI/data wiring.

## Threat Flags

None. This plan's threat model register (T-02-13-01/02/03) was already scoped
and dispositioned by the plan itself; no new security-relevant surface was
introduced beyond what the plan anticipated.

## Self-Check: PASSED

- FOUND: `.github/workflows/ci.yml` (modified, matches plan's single-file scope)
- FOUND: commit `e403e32` in `git log --oneline --all`
- FOUND: `build/x64-linux/tests/unit/mediadiff_unit_tests` and
  `build/x64-linux/tests/integration/mediadiff_integration_tests` (built during
  this plan's verification; not committed — build artifacts, correctly outside
  git)
- CONFIRMED: `grep -rF '\+' .github/workflows/` returns 0 matches post-commit
- CONFIRMED: `POSIX_COUNT` (296) equals `GNU_COUNT` (296) against real ctest
  output post-commit
