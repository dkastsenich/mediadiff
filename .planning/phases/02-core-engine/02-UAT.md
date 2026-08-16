---
status: complete
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
  root_cause: "Phase 2 code calls std::getenv in the mediadiff target; MSVC raises C4996 ('getenv' unsafe), which /WX escalates to C2220. src/cli/options.cpp:251 is the first occurrence to fail the build; src/cli/commands/snapshot.cpp:175 and src/cli/commands/dir.cpp:278 are the same defect and were not reached before ninja stopped."
  artifacts:
    - path: "src/cli/options.cpp"
      issue: "line 251 read_env() calls std::getenv -- C4996 under /W4 /WX"
    - path: "src/cli/commands/snapshot.cpp"
      issue: "line 175 std::getenv(\"CI\") -- same C4996, not yet reached by the build"
    - path: "src/cli/commands/dir.cpp"
      issue: "line 278 std::getenv(\"MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR\") -- same C4996, not yet reached"
  missing:
    - "An MSVC-safe env-var read (_dupenv_s / getenv_s under _WIN32), matching the existing _wfopen_s precedent for C4996 in this codebase -- not a blanket _CRT_SECURE_NO_WARNINGS"
    - "Route all three call sites through the single read_env() helper so the platform primitive stays confined to one place, per src/cli/options.h's stated convention"
  debug_session: ""
  evidence: "CI run 31937349647 job 95141247678; main run 31881353367 job 95004285813 passed the same leg, and src/cli/options.cpp does not exist on origin/main"

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
