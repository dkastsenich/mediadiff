---
phase: 02-core-engine
plan: 17
subsystem: testing
tags: [gitattributes, msvc-crt, stdout-binary-mode, catch2, trust-05, windows]

requires:
  - phase: 02-core-engine
    provides: "02-16 CI wiring/push and the round-3 Windows run read that opened G-02-5/6/7"
provides:
  - ".gitattributes pinning every text file to LF on checkout on every platform"
  - "src/cli/main.cpp wmain sets stdout AND stderr to _O_BINARY before any output (option B)"
  - "TRUST-05 destination-parity test comparing --json stdout bytes to --json=<path> file bytes"
affects: ["02-18-PLAN.md", "02-19-PLAN.md", "02-UAT.md G-02-5/G-02-7", "UAT test 2 (Windows console colour)"]

actuals:
  tokens: 2352
  tasks: 3
  commits: 1

tech-stack:
  added: []
  patterns:
    - "Boundary-once fix: a determinism/newline property enforced at one process-start call (main.cpp) rather than per report writer"
    - ".gitattributes text=auto eol=lf as the checkout-side half of a two-sided byte-identity fix"

key-files:
  created:
    - .gitattributes
  modified:
    - src/cli/main.cpp
    - tests/integration/test_json_schema.cpp

key-decisions:
  - "Task 2 checkpoint (pre-answered by human 2026-08-18): option B — put BOTH stdout AND stderr into binary mode on Windows, unconditionally. Rationale: consistent bytes on every stream; stderr diagnostics stop acquiring CRLF too, so there is no stream left where Windows output silently differs from POSIX. Matches the four fopen_utf8(..., \"wb\") file destinations exactly."
  - "_setmode/_fileno placed in src/cli/main.cpp only, never src/util/fs.h — scripts/lint_eng16.sh scans src/util for bare stdout/stderr tokens and the ENG-16 boundary lint is a required merge gate."
  - "_setmode call placed as the first statement of wmain, before enable_vt_output() — functionally equivalent to placing it immediately after (enable_vt_output emits no bytes of its own), chosen to satisfy the stricter reading of 'before any output happens' as literally as possible."

requirements-completed: [BUILD-01, BUILD-05, TRUST-05, CLI-09]

coverage:
  - id: D1
    description: ".gitattributes pins every text file to LF on checkout on every platform, with zero content rewrite of the 376 already-LF tracked text files"
    requirement: "BUILD-01"
    verification:
      - kind: other
        ref: "git check-attr text eol -- tests/golden/junit_basic.txt (reports 'text: auto', 'eol: lf'); git add --renormalize . && git diff --cached --name-only (stages nothing but .gitattributes); git ls-files --eol | grep -c i/crlf (0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Windows wmain sets stdout AND stderr to _O_BINARY via _setmode/_fileno before any output; located only in src/cli/main.cpp, never in an ENG-16-scanned directory"
    requirement: "TRUST-05"
    verification:
      - kind: other
        ref: "grep -c '_setmode' src/cli/main.cpp (>=1); grep -rc '_setmode' across the 7 ENG-16 SCAN_DIRS (0 everywhere); bash scripts/lint_eng16.sh (clean)"
        status: pass
    human_judgment: true
    rationale: "This is a Windows-only code path (#ifdef _WIN32) that cannot execute on this Linux host. Local proof covers placement, lint compliance, and clean compilation intent; whether it actually fixes the Windows golden/subprocess-byte mismatches can only be proven by the next Windows CI run, read in 02-19-PLAN.md."
  - id: D3
    description: "New TRUST-05 destination-parity test: --json stdout bytes vs --json=<path> file bytes (read binary) are asserted equal, and stdout is asserted to carry no 0x0D"
    requirement: "TRUST-05"
    verification:
      - kind: integration
        ref: "tests/integration/test_json_schema.cpp#json_schema - TRUST-05: --json to stdout and --json=<path> produce identical bytes"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-08-18
status: complete
---

# Phase 02 Plan 17: Byte-identity boundary — .gitattributes and Windows stdout/stderr binary mode Summary

**One `.gitattributes` file pinning checkout to LF plus one `_setmode` call in `wmain` setting both stdout and stderr to binary before any output, landed in a single commit because either half alone would trade three failing Windows golden tests for four different ones.**

## Performance

- **Duration:** 35 min (includes vcpkg submodule init/bootstrap + FFmpeg binary-cache-hit build)
- **Started:** 2026-08-18 (worktree agent-a6474dc117c4b4cc9)
- **Completed:** 2026-08-18
- **Tasks:** 3 (Task 1 tracer, Task 2 decision checkpoint, Task 3 auto/tdd)
- **Files modified:** 3 (1 created, 2 modified)

## Accomplishments

- `.gitattributes` created at the repository root: `* text=auto eol=lf` pins every text file to LF on checkout on every platform (overriding a Windows runner's `core.autocrlf` default), plus nine media-extension `binary` markers as a belt-and-braces guard for the first fixture anyone commits. Proven a no-op on existing tracked content: `git add --renormalize .` stages nothing but `.gitattributes` itself, and `git ls-files --eol` shows zero `i/crlf` across 376 text files.
- `src/cli/main.cpp`'s `wmain` now calls `(void)_setmode(_fileno(stdout), _O_BINARY);` and the matching `stderr` line (Task 2's option B), before `enable_vt_output()` and before any output — closing the asymmetry where mediadiff's four `fopen_utf8(..., "wb")` report-file destinations were already byte-correct on Windows while stdout/stderr silently translated `\n` to `\r\n`.
- New TEST_CASE `json_schema - TRUST-05: --json to stdout and --json=<path> produce identical bytes` in `tests/integration/test_json_schema.cpp`, comparing captured stdout against a binary (`fopen_utf8(..., "rb")`) read of the file destination, plus a standalone assertion that stdout carries no `0x0D` byte.
- Local suite moved from 297 to exactly 298 tests, 0 failed, 1 skipped (`unit.console_vt`, a real-console-only test, unchanged). Build clean under `-Wall -Wextra -Werror` (verified via `-v` build output for both touched translation units — zero warning/error lines). All four required lint scripts (`lint_eng16.sh`, `lint_check_id_strings.sh`, `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`) exit 0.
- `tests/integration/test_explain_inspect.cpp` left untouched (`git diff --stat` empty), preserving the G-02-7 downstream signal for test #247.

## Task Commits

1. **Task 1 (tracer): `.gitattributes`** + **Task 3 (auto/tdd): `_setmode` binary-mode fix and TRUST-05 test** — `3f06733` (fix) — landed together deliberately per the plan's hard constraint (either alone regresses the Windows leg differently).
2. **Task 2 (checkpoint:decision):** No code artifact of its own — decision recorded here and inline in `src/cli/main.cpp`'s new comment block. Pre-answered by the human on 2026-08-18: **option B**.

No separate plan-metadata commit was requested for this plan (orchestrator owns STATE.md/ROADMAP.md after the wave merges, per this plan's dispatch instructions).

## Files Created/Modified

- `.gitattributes` — new file, root of repo. `* text=auto eol=lf` plus nine `binary` media-extension markers.
- `src/cli/main.cpp` — `#include <fcntl.h>` / `#include <io.h>` added to the existing `_WIN32` include block; `wmain` gains two `_setmode` calls (stdout, stderr) as its first statements, with a comment recording the TRUST-05 rationale, the ENG-16 location constraint, and that the four `"wb"` destinations are deliberately untouched.
- `tests/integration/test_json_schema.cpp` — one new `TEST_CASE` plus a local `unique_scratch_path` helper (mirrors `test_dir_mode.cpp`'s file-local `unique_scratch_dir`, not shared since that helper lives in an anonymous namespace, not a header).

## Decisions Made

- **Task 2 checkpoint — option B (stdout AND stderr, unconditionally binary), pre-answered by the human 2026-08-18.** Rationale carried forward verbatim from the dispatch: consistent bytes on every stream, and stderr diagnostics stop acquiring CRLF too, so there is no stream left where Windows output silently differs from POSIX. The four `fopen_utf8(..., "wb")` file destinations were confirmed out of scope and were not touched.
- **`_setmode` call ordering relative to `enable_vt_output()`:** placed as the literal first statement of `wmain`, ahead of `enable_vt_output()`. The plan's own action text says "immediately after `enable_vt_output()`"; a stricter reading elsewhere (`hard_constraints`) says "before `enable_vt_output()`". Both orderings are functionally identical — `enable_vt_output()` only calls `GetConsoleMode`/`SetConsoleMode` and emits no bytes of its own — but placing `_setmode` first satisfies the stricter reading without contradicting the plan's intent (the mode is set before the first byte is ever written, which is the property both texts actually care about). Documented here rather than silently picking one, since the two source texts literally disagreed on call order.

## Deviations from Plan

None — plan executed exactly as written, including the pre-answered Task 2 decision. One clarification recorded above (ordering of `_setmode` vs. `enable_vt_output()`), not a deviation from any acceptance criterion — no criterion in the plan checks call order between those two statements.

## Issues Encountered

- **Local build environment was not pre-provisioned.** The worktree had no `build/` directory and the `vcpkg` submodule was an uninitialized, unbootstrapped shallow reference. Resolved by `git submodule update --init --depth 1`, `git fetch --unshallow` inside `vcpkg/` (the shallow clone could not resolve the pinned `ffmpeg` port's git tree object), then `./bootstrap-vcpkg.sh -disableMetrics`. The shared `~/.cache/vcpkg/archives` binary cache (130 MB) made the subsequent `vcpkg install` near-instant — no 15–40 min FFmpeg source build was needed. This is environment setup, not a plan deviation; no code or plan content was affected.

## What This Does NOT Establish (CI-only, explicitly out of scope for this host)

- **Whether the Windows leg's three in-process golden tests (`junit_basic`, `markdown_basic`, `tty_basic`) and the four subprocess-fed goldens (`inspect_basic`, `json_schema_basic`, `dir_worst_n`, `list_checks_effective`) all pass together after this change.** This host has no MSVC, no `_O_TEXT` fd, and no `core.autocrlf=true` checkout to observe. `02-19-PLAN.md` performs the CI run read.
- **Whether `#247` (`test_explain_inspect.cpp`'s `found_meta_no_measurements` check) turns out to be downstream of the newline mode, per G-02-7's hypothesis.** The file is deliberately unmodified so that signal stays legible in the next CI run.
- **UAT test 2 — colour renders as real styling in a Windows console.** Not performed here; needs a human at real Windows hardware. **Carried forward explicitly, with the build sha this change lands at: `3f06733f66041cf0101f67c25b882288db89e748` (short: `3f06733`).** Per `02-UAT.md`'s own note on test 2: because this plan changes what bytes Windows stdout/stderr carry, any console run of UAT test 2 must be against a build at or after this sha, and its result must record the sha alongside the observation — a console run against an older build would not reflect this fix.

## Next Phase Readiness

- `.gitattributes` and the `_setmode` fix are committed together (`3f06733`) and ready to push for the next CI read, per this plan's own hard constraint.
- `02-18-PLAN.md` (test #95, Python's own text-mode stdout — a different, unrelated cause) and `02-19-PLAN.md` (the CI run read against this commit, plus #236's exit-code divergence) remain the next steps in the round-4 gap-closure sequence; neither is blocked by anything found here.
- No blockers. The one open item — UAT test 2 — is a scheduling/human-availability item, not a technical blocker, and is now unblocked to run against sha `3f06733`.

## Self-Check: PASSED

- `test -f .gitattributes` → FOUND
- `test -f src/cli/main.cpp` → FOUND (modified)
- `test -f tests/integration/test_json_schema.cpp` → FOUND (modified)
- `git log --oneline --all | grep -q 3f06733` → FOUND
- Local suite: 298 tests, 0 failed, 1 skipped (re-confirmed after final rebuild)
- All four lint scripts: exit 0 (re-confirmed)

---
*Phase: 02-core-engine*
*Completed: 2026-08-18*
