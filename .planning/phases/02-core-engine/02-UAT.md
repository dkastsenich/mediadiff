---
status: testing
phase: 02-core-engine
source: [02-VERIFICATION.md]
started: 2026-08-15T21:45:00Z
updated: 2026-08-15T21:45:00Z
---

## Current Test

number: 1
name: Colour renders as styling, not literal escape bytes, in a real Windows console
expected: |
  Running `mediadiff compare`, `mediadiff dir`, or any TTY report in cmd.exe or
  Windows Terminal shows colour as actual ANSI-interpreted styling — not literal
  escape-sequence bytes printed to the terminal. This exercises the
  SetConsoleMode ENABLE_VIRTUAL_TERMINAL_PROCESSING path that Phase 1 landed and
  Phase 2 is the first to emit output for.
awaiting: user response

## Tests

### 1. Colour renders as styling in a real Windows console
expected: Colour renders as actual ANSI-interpreted styling rather than literal escape-sequence bytes, in cmd.exe or Windows Terminal, with ENABLE_VIRTUAL_TERMINAL_PROCESSING exercised.
why_human: This sandbox is Linux-only. `unit.console_vt` is skipped here (needs a real TTY), and the Windows console VT-enable path cannot be exercised programmatically in this environment.
result: [pending]

### 2. NO_COLOR / non-TTY / CI / GITHUB_ACTIONS precedence, observed visually
expected: Colour appears and disappears exactly as the WR-02-fixed precedence table states — auto-disabled on `NO_COLOR`, non-TTY stdout and `CI=true`, still enabled for `GITHUB_ACTIONS=true` — as seen by an operator in a real terminal session, matching what the unit tests assert against synthetic env vars.
why_human: The decision logic is unit-tested (11 cases, including the two WR-02 regression tests crossing `NO_COLOR` against `GITHUB_ACTIONS=true`) and cannot regress silently, but actual terminal rendering (glyph width, 256-colour vs truecolour fallback) is a visual property this sandbox cannot observe.
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps
