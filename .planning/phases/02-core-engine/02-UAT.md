---
status: diagnosed
phase: 02-core-engine
source: [02-VERIFICATION.md]
started: 2026-08-15T21:45:00Z
updated: 2026-08-16T09:02:00Z
---

## Current Test

[testing complete]

## Tests

### 1. Colour renders as styling in a real Windows console
expected: Colour renders as actual ANSI-interpreted styling rather than literal escape-sequence bytes, in cmd.exe or Windows Terminal, with ENABLE_VIRTUAL_TERMINAL_PROCESSING exercised.
why_human: This sandbox is Linux-only. `unit.console_vt` is skipped here (needs a real TTY), and the Windows console VT-enable path cannot be exercised programmatically in this environment.
result: issue
reported: "I created a PR of this branch into main to generate Windows artifacts during CI - and all builds failed"
severity: blocker
note: |
  Test could not be reached — no Windows binary was produced. PR #2, CI run
  31937349647: 4 of 6 jobs failed (x64-linux and lint passed).
  Only the Windows failure is new to this branch; the other three reproduce
  identically on main (run 31881353367) and predate Phase 2.

### 2. NO_COLOR / non-TTY / CI / GITHUB_ACTIONS precedence, observed visually
expected: Colour appears and disappears exactly as the WR-02-fixed precedence table states — auto-disabled on `NO_COLOR`, non-TTY stdout and `CI=true`, still enabled for `GITHUB_ACTIONS=true` — as seen by an operator in a real terminal session, matching what the unit tests assert against synthetic env vars.
why_human: The decision logic is unit-tested (11 cases, including the two WR-02 regression tests crossing `NO_COLOR` against `GITHUB_ACTIONS=true`) and cannot regress silently, but actual terminal rendering (glyph width, 256-colour vs truecolour fallback) is a visual property this sandbox cannot observe.
result: pass
evidence: |
  Precedence half verified end-to-end through the shipped x64-linux binary
  (not just decide_color against synthetic structs): 11/11 scenarios match
  the documented table, using script(1) for a genuine pty on the TTY rows.
  Non-TTY: bare=plain, GITHUB_ACTIONS=true=colour, CI=true=plain,
  CI+GITHUB_ACTIONS=colour, NO_COLOR=1+GITHUB_ACTIONS=plain (the WR-02 fix),
  NO_COLOR= (empty)+GITHUB_ACTIONS=plain. PTY: bare=colour, NO_COLOR=1=plain,
  CI=true=plain, CI+GITHUB_ACTIONS=colour, --no-color+GITHUB_ACTIONS=plain.
  Visual half confirmed by the operator on Linux: the warn glyph renders as
  yellow styling, not literal escape bytes.
observed: |
  Operator noted colour is confined to the status glyph -- the summary line
  (`pass:0 info:0 warn:1 ...`) and check ids emit no escapes at all. Confirmed
  in source: exactly one styling call site exists in all of src/
  (tty_render.cpp:81), and the rendered finding line carries exactly one
  escape pair, opening before the glyph and resetting after it.
  Not a spec violation -- CLI-08 governs WHEN colour is on/off, REPORT-02
  covers grouping/width, and design doc section 3.2 line 75 states only the
  on/off policy. No document specifies WHAT gets coloured, and no sample TTY
  render exists in claude_docs/ to compare against. Recorded as a deferred
  follow-up rather than a gap.

## Summary

total: 2
passed: 1
issues: 1
pending: 0
skipped: 0
blocked: 0

## Deferred Follow-Ups

- test: 2
  idea: "Severity colour applies only to the status glyph; the summary line (`pass:0 info:0 warn:1 ...`) and check ids are unstyled. CLI-08 and REPORT-02 specify when colour is on/off, never what gets coloured, so glyph-only satisfies the written spec. Revisit whether a diff gate whose job is making severity legible at a glance should style more than one character — and if so, write the answer down, since the absence of a sample TTY render in claude_docs/ is what left this open."
  deferred_at: 2026-08-16
  not_a_gap: "Scope ambiguity in the spec, not a defect. Does not block Phase 2."

## Gaps

- gap_id: G-02-1
  truth: "mediadiff builds clean on x64-windows-static-md under /W4 /WX, producing the Windows binary the console-VT checkpoint needs"
  status: failed
  reason: "User reported: I created a PR of this branch into main to generate Windows artifacts during CI - and all builds failed"
  severity: blocker
  test: 1
  root_cause: "Phase 2 introduced SIX unguarded std::getenv call sites (no _WIN32 guard). MSVC's CRT deprecates getenv (C4996) and mediadiff_apply_warnings() applies /W4 /WX to all four first-party targets, so /WX escalates each to C2220. The CI Build step runs `cmake --build --preset x64-windows-static-md` with no --target, so the default `all` target -- both test executables included -- is in scope. Only ONE C4996 has ever been emitted: ninja stopped at [32/97], so 65 steps never ran and the other five sites have never been compiled by MSVC in any run."
  artifacts:
    - path: "src/cli/options.cpp"
      issue: "line 251 read_env() calls std::getenv -- the only site reached, [27/97]"
    - path: "src/cli/commands/snapshot.cpp"
      issue: "line 175 ci_env_is_true() -- same target, never reached"
    - path: "src/cli/commands/dir.cpp"
      issue: "line 278 inline env read -- same target, never reached"
    - path: "tests/support/golden.cpp"
      issue: "line 23 update_goldens_requested() -- compiled into BOTH /WX test targets, so two compilations"
    - path: "tests/integration/test_exit_codes.cpp"
      issue: "line 137 getenv(\"PATH\") -- test target is NOT exempt from /WX (tests/integration/CMakeLists.txt:99)"
    - path: "tests/integration/test_snapshot_safe_write.cpp"
      issue: "line 136 getenv(\"PATH\") -- same"
    - path: "src/cli/options.h"
      issue: "line 149 'one place' claim is already FALSE -- CI is also read at snapshot.cpp:175; documentation-level defect that survives a getenv-only fix"
    - path: "src/cli/color_policy.h"
      issue: "lines 9-10 'one place' claim FALSE on both counts -- 3 getenv sites, 3 isatty sites"
    - path: "scripts/lint_eng16.sh"
      issue: "lines 26-34 SCAN_DIRS excludes src/cli -- explains why no gate caught the recurrence"
  missing:
    - "One _WIN32-guarded env-read shim in src/util/fs.h -- the codebase's designated home for platform primitives, and the only place besides main.cpp permitted to name wide-char types. Precedent: commit 47d5965 put _wfopen_s there for this exact C4996 reason. A shim in options.cpp would be barred by the project's own rule from ever using the encoding-correct wide form."
    - "_dupenv_s, not getenv_s, and never _CRT_SECURE_NO_WARNINGS. _dupenv_s sets buffer=NULL when unset and yields non-NULL pointing at \"\" when set-but-empty, preserving the unset-vs-empty distinction that ColorInputs's optional<string> and NO_COLOR presence-is-the-signal (WR-02) depend on. Single-call; requires free(), contained by copying into std::string. getenv_s preserves the distinction too but needs a two-call probe whose size-probe returns ERANGE as a non-error."
    - "Route all SIX sites through it, including the three test-side ones -- otherwise the Build step just fails later in the same step."
    - "Correct the three false 'one place' comments in the same change (options.h:149, color_policy.h:9, options.cpp:258-261)."
  scope_caveat: "Fixing getenv is NECESSARY but NOT provably SUFFICIENT. 65 of 97 build steps have never been compiled by MSVC in any run -- five remaining mediadiff sources, the header-verify target, and both test executables in full. Unrelated MSVC breakage may be hiding behind this one. Expect to iterate on the Windows leg rather than assuming one green run follows the first fix."
  debug_session: ".planning/debug/windows-getenv-c4996.md"
  evidence: "CI run 31937349647 job 95141247678; main run 31881353367 job 95004285813 passed the same leg, and src/cli/options.cpp does not exist on origin/main"

- gap_id: G-02-2
  truth: "The blocking arm64-osx CI leg runs the test suite instead of failing its own test-count guard, so PR #2 can reach a mergeable state"
  status: failed
  reason: "Surfaced by the same PR run as G-02-1. NOT a Phase 2 regression -- reproduces identically on main -- but it is a BLOCKING matrix leg, so the phase cannot merge while it is red. Scoped into gap closure on that basis, with the user's explicit agreement."
  severity: blocker
  test: 1
  regression_of_phase_2: false
  root_cause: "CONFIRMED by CI differential. .github/workflows/ci.yml:246 uses `sed -n 's/^Total Tests: \\([0-9]\\+\\)$/\\1/p'`. `\\+` is a GNU extension, not POSIX BRE. The arm64-osx leg runs on macos-15, whose /usr/bin/sed is BSD sed, which treats `\\+` as a LITERAL `+` -- the pattern then demands `Total Tests: <digit>+` and can never match the real `Total Tests: 296`. `sed -n` with no match prints nothing and EXITS 0, so neither `|| true` nor pipefail intervenes: TOTAL is empty, the `-z` branch fires, and the step aborts before any test runs. A REGRESSION, not a never-worked: introduced by commit e684579, which swapped an extension-free `grep -c '^  Test #'` for this sed pattern AND added the `-z` branch that now fires, in the same commit."
  artifacts:
    - path: ".github/workflows/ci.yml"
      issue: "line 246 -- non-portable GNU-only `\\+`. Whole-workflow scan found this is the ONLY GNU-only regex construct (line 211's `grep -iE` is portable), so the fix is single-sited."
  missing:
    - "Replace `\\([0-9]\\+\\)` with pure-POSIX `\\([0-9][0-9]*\\)`. Verified to yield 296 against real ctest output and 0 against `Total Tests: 0`, so the guard's stated anti-false-pass intent survives. `sed -E` also works but adds a flag assumption for no benefit."
    - "Add a comment that this pipeline runs under BSD sed on the macOS legs -- e684579's own comment documents a GNU-sed-only verification, which is exactly how the defect shipped."
  scope_caveat: "Unblocks the step but does NOT guarantee a green leg. The 296-test suite has never executed on macOS -- the last successful macOS run (31849289102) executed 11. Budget a follow-up for genuine macOS test failures revealed behind the guard."
  debug_session: ".planning/debug/arm64-osx-ctest-count-guard.md"
  evidence: "CI run 31937349647 job 95141247762 (branch) and run 31881353367 job 95004285871 (main), identical symptom: 'could not parse a test count from ctest -N ... refusing to assume the suite is healthy'. The x64-linux leg runs the same step and passes; the x64-osx leg never reaches the Test step because its Build fails first (see CI-x64-osx below)."

## Pre-Existing CI Failures (not Phase 2 regressions)

<!-- Recorded for accuracy: these reproduce identically on main and are NOT
     gaps in Phase 2's deliverables. They are tracked here because they were
     surfaced by the same PR run and three of them are what "all builds
     failed" actually refers to. -->

- id: CI-arm64-osx
  blocking: true
  job: "build (arm64-osx) -- Test step"
  symptom: "could not parse a test count from 'ctest -N' -- refusing to assume the suite is healthy"
  root_cause: ".github/workflows/ci.yml:246 uses `sed -n 's/^Total Tests: \\([0-9]\\+\\)$/\\1/p'`. `\\+` is a GNU sed extension; BSD sed on macOS reads it as a literal '+', so the pattern never matches, TOTAL is empty, and the guard fires. GNU sed on Linux matches, which is why only the macOS legs trip it."
  fix_hint: "Use a POSIX-portable BRE (`[0-9][0-9]*`) or `sed -E` with `([0-9]+)`."
  reproduces_on_main: "run 31881353367 job 95004285871"

- id: CI-x64-osx
  blocking: false
  job: "build (x64-osx) -- Build step"
  symptom: "ld: symbol(s) not found for architecture arm64, preceded by 'ignoring file ... found architecture x86_64, required architecture arm64' for every vcpkg lib"
  root_cause: "The x64-osx preset cross-builds from the arm64 macos-15 host. vcpkg honours VCPKG_TARGET_TRIPLET=x64-osx and produces x86_64 static libs, but the project's own compile/link is not given -arch x86_64, so clang targets the arm64 host default and rejects every dependency."
  fix_hint: "Set CMAKE_OSX_ARCHITECTURES=x86_64 in the x64-osx preset so the project's objects match the triplet's. Known failure class already flagged in research/STACK.md."
  reproduces_on_main: "run 31881353367 job 95004285928"

- id: CI-arm64-linux
  blocking: false
  job: "build (arm64-linux) -- Register vcpkg NuGet feed (read-write, trusted runs only)"
  symptom: "step exits 1 in ~15ms with no captured stdout/stderr"
  root_cause: "Undetermined -- the step produces no diagnostic output. The failure is in `NUGET_EXE=\"$(vcpkg fetch nuget | tail -n 1)\"` or the mono invocation on the ubuntu-24.04-arm runner; the same step succeeds on x64-linux. Needs a run with the step's output surfaced before a cause can be claimed."
  fix_hint: "Investigate -- do not guess. Likely mono/NuGet availability on arm64, but this is unverified."
  reproduces_on_main: "run 31881353367 job 95004285885"
