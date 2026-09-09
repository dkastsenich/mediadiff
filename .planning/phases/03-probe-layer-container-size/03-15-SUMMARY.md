---
phase: 03-probe-layer-container-size
plan: 15
subsystem: probe-layer-container-size
tags: [security, xml-escaping, cli-diagnostics, control-bytes, gap-closure]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: "T-2-33's choke point (sanitize_for_display, scripts/lint_control_bytes.sh) established by 03-11 for the terminal/provenance/Markdown render paths; 03-VERIFICATION.md's Anti-Patterns table (CR-03, WR-01, IN-02) naming what it left open"
provides:
  - "src/report/junit.cpp's xml_escape: C0 control bytes (other than tab/LF/CR) and DEL escaped as a visible \\xHH sequence, closing CR-03 -- a JUnit report can no longer carry an illegal-XML-1.0 byte"
  - "src/cli/diagnostics.{h,cpp}: report_cli_error, the one permitted CLI diagnostic sink -- every command's stderr diagnostic routes through sanitize_for_display before writing, closing WR-01 (44 inline fputs sites migrated)"
  - "scripts/lint_control_bytes.sh: inspect_render.h added to its display-render scan list (IN-02), plus a second, independent rule that no file under src/cli/ other than src/cli/diagnostics.cpp may write directly to stderr"
  - "tests/integration/test_cli_diagnostics_escaping.cpp: cross-platform (no _WIN32 skip) proof that the CLI diagnostic sink escapes control bytes, using an argv-supplied vector"
  - ".planning/phases/02-core-engine/02-SECURITY.md's corrected T-2-33 record: which paths 03-11 closed, which paths 03-15 closed, and that the first closure was premature"
affects: ["any future CLI command file (must route stderr diagnostics through report_cli_error, enforced by the lint's second rule)", "any future JUnit/report renderer touching junit.cpp (control-byte escaping is now xml_escape's own job, not sanitize_for_display's)"]

# Actuals (#2632)
actuals:
  tokens: 16504
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "XML-context-local control-byte escaping: xml_escape emits a visible \\xHH sequence for an illegal-as-XML-1.0 byte IN THE TEXT ITSELF, never an XML numeric character reference (which is equally illegal for that byte range) -- a fix scoped to the renderer's own escaping rules rather than routed through the display-only sanitize_for_display choke point"
    - "One CLI diagnostic sink (src/cli/diagnostics.cpp's report_cli_error): every command file's stderr write goes through it, sanitized, printing-only (never folds in std::exit) so the call site keeps choosing its own exit code; every remaining stdout content write (TTY/JSON/help text) switched from fputs to fwrite so the sink is enforceable by a literal 'no fputs outside this file' lint rule"
    - "Lint scan-list generalization (IN-02's own remedy): sanitize.h's header comment stops naming a fixed render-path count and instead points at the lint's own scan list as the single source of truth, so a future render path is added to the list, not restated (and forgotten) in a comment"

key-files:
  created:
    - src/cli/diagnostics.h
    - src/cli/diagnostics.cpp
    - tests/integration/test_cli_diagnostics_escaping.cpp
  modified:
    - src/report/junit.cpp
    - src/util/sanitize.h
    - tests/unit/test_junit.cpp
    - src/cli/commands/compare.cpp
    - src/cli/commands/dir.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/explain.cpp
    - src/cli/commands/list_checks.cpp
    - src/cli/commands/inspect_render.h
    - src/cli/main.cpp
    - scripts/lint_control_bytes.sh
    - CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - .planning/phases/02-core-engine/02-SECURITY.md

key-decisions:
  - "xml_escape's control-byte fix emits \\xHH IN THE TEXT ITSELF, never an XML numeric character reference (&#x1B;) -- a numeric character reference is exactly as illegal as the raw byte for this class of code point under XML 1.0, so using one would move the problem rather than fix it. Stated explicitly in the renderer's own comment so a future reader does not 'improve' the fix into an invalid one."
  - "junit.cpp does NOT call sanitize_for_display -- fixing xml_escape in place, in its own escaping context, is correct; routing through the display escaper would double-escape the four XML metacharacters. sanitize.h's header comment was corrected to state what junit.cpp actually does now, replacing the false 'handles its own context' assertion 03-VERIFICATION.md flagged."
  - "report_cli_error is printing-only, never calling std::exit() itself -- every call site keeps choosing its own exit code exactly as before, so the exit-code contract stays legible at the call site rather than hidden inside a printer."
  - "Every remaining fputs (stdout content: TTY report, JSON report, --help text) switched to std::fwrite, not left as fputs -- satisfies the acceptance criterion that zero fputs calls remain in any CLI command file or main.cpp, making 'no direct stream write outside the one sink' a literal, lint-checkable invariant rather than one scoped only to stderr."
  - "The lint's second rule (no direct stderr write under src/cli/ outside diagnostics.cpp) uses a narrower write-call pattern (fputs(...stderr), fprintf(stderr, std::cerr) rather than a bare 'stderr' token match, specifically so src/cli/main.cpp's own _setmode(_fileno(stderr), ...) (which sets binary mode, writes nothing) is not a false positive."
  - "inspect_render.h's two false positives under the render-path rule (a registry check id used only for group-membership comparison, and render_inspect_json's deliberately-unsanitized JSON output) are marked with the existing control-bytes-allow escape valve rather than restructuring the file -- the file already carried a correct, unguarded 4th call site since 03-11; adding it to the scan list only required marking what was already correct."
  - "The cross-platform escaping test uses list-checks --effective --profile/--set (argv-supplied, no baseline/candidate file needed) rather than a crafted filename -- a filename carrying a raw control byte cannot exist on Windows, so a filename-based test would silently not run on one third of the CI matrix, which is exactly the two-of-three-platform coverage gap this plan's own review found in the first closure."
  - ".planning/phases/02-core-engine/02-SECURITY.md's T-2-33 entry is corrected, not simply re-marked closed -- a new Post-Audit Correction section (matching the existing T-2-24 correction's shape) records what the earlier same-day Closure Note missed, why, and what completed it, so the security record shows a threat that was reported closed early rather than silently absorbing the miss."

patterns-established:
  - "Post-Audit Correction sections (now used twice: T-2-24, T-2-33) as the standing shape for correcting a security record that certified something closed before it actually was -- name what was verified and remained true, name what was missed and why, name the fix and its evidence, and update the Audit Trail table with a new row rather than editing the old one."

requirements-completed: [CONT-03, CONT-04]

coverage:
  - id: D1
    description: "CR-03 closed: src/report/junit.cpp's xml_escape escapes every C0 control byte (other than tab/LF/CR) and DEL as a visible \\xHH sequence rather than passing it through; a JUnit report generated from a crafted control-byte-carrying tag value contains zero illegal bytes and parses as XML."
    requirement: ""
    verification:
      - kind: unit
        ref: "tests/unit/test_junit.cpp (6 new TEST_CASEs: ESC in attribute/element-body contexts, NUL non-truncation, four-metachar/UTF-8 passthrough unchanged, tab/LF/CR preserved, crafted input re-parses as XML)"
        status: pass
      - kind: other
        ref: "manual end-to-end: mediadiff compare tests/fixtures/tags_esc_{a,b}.mp4 --report junit=... ; LC_ALL=C grep -c C0-range => 0; python3 xml.dom.minidom.parse => valid"
        status: pass
    human_judgment: false
  - id: D2
    description: "WR-01 closed: every CLI diagnostic (44 inline stderr writes across 7 files) routes through one sanitizing helper, src/cli/diagnostics.cpp's report_cli_error; no command file writes to a standard stream directly (fputs count 0); exit codes and control flow unchanged."
    requirement: ""
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux --output-on-failure (620/620 green, including test_exit_codes unchanged)"
        status: pass
      - kind: other
        ref: "grep -rc fputs src/cli/commands/*.cpp src/cli/main.cpp | grep -v ':0$' | wc -l => 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "IN-02 closed: scripts/lint_control_bytes.sh's scan list includes src/cli/commands/inspect_render.h, and a second rule enforces the CLI diagnostic sink; both self-test control clauses fire correctly before every real scan."
    requirement: ""
    verification:
      - kind: other
        ref: "bash scripts/lint_control_bytes.sh -- both self-test clauses pass, real scan clean; verified once during development that reintroducing an inline stderr write in compare.cpp makes the lint fail naming the file/line (not committed)"
        status: pass
    human_judgment: false
  - id: D4
    description: "T-2-33 genuinely closed across every output format the tool produces; .planning/phases/02-core-engine/02-SECURITY.md's record corrected to show the first (03-11) closure was premature and name what 03-15 completed, rather than silently re-marking it closed."
    requirement: ""
    verification:
      - kind: other
        ref: "tests/integration/test_cli_diagnostics_escaping.cpp (4 TEST_CASEs through the real CLI binary, argv-supplied vector, no _WIN32 skip); .planning/phases/02-core-engine/02-SECURITY.md's new Post-Audit Correction section + updated Audit Trail row + Sign-Off line"
        status: pass
    human_judgment: false

duration: ~30min
completed: 2026-09-04
status: complete
---

# Phase 03 Plan 15: Close T-2-33 for real — JUnit control-byte escaping, one CLI diagnostic sink, corrected security record Summary

**Fixed the two render/diagnostic paths (JUnit XML, CLI stderr) that 03-11's T-2-33 closure missed, and corrected the security record to show the miss rather than silently re-marking the threat closed.**

## Performance

- **Duration:** ~30 min
- **Started:** 2026-09-04T21:29Z (approx, first commit 21:34 local)
- **Completed:** 2026-09-04T21:44Z (local commit time)
- **Tasks:** 3
- **Files modified:** 17 (2 new: src/cli/diagnostics.{h,cpp}; 1 new test: test_cli_diagnostics_escaping.cpp)

## Accomplishments

- **CR-03 closed.** `src/report/junit.cpp`'s `xml_escape` now escapes every C0 control byte (other than tab/LF/CR) and DEL as a visible `\xHH` sequence in the text itself, in addition to its pre-existing four-XML-metacharacter escaping — a numeric XML character reference was deliberately rejected as the fix (it is exactly as illegal as the raw byte for this class of code point, so it would move the problem rather than solve it). A crafted tag value carrying a raw control byte now produces a JUnit report with zero illegal bytes that parses as valid XML — proven both by 6 new unit tests and a manual end-to-end run against the committed `tags_esc_{a,b}.mp4` fixture pair.
- **WR-01 closed.** One new sink, `src/cli/diagnostics.cpp`'s `report_cli_error`, replaces all 44 inline `std::fputs(..., stderr)` diagnostics across `compare.cpp`, `dir.cpp`, `inspect.cpp`, `snapshot.cpp`, `explain.cpp`, `list_checks.cpp` and `main.cpp` — every one now sanitized through `sanitize_for_display` before it reaches a terminal, closing the `dir`-mode directory-listing-filename exposure the 03-11 closure never scanned. Every remaining stdout content write (TTY/JSON reports, `--help` text) switched from `fputs` to `fwrite` so "zero `fputs` in any CLI command file" is a literal, lint-enforceable invariant. Exit codes and control flow are byte-for-byte unchanged (`test_exit_codes` green, 620/620 overall).
- **IN-02 closed.** `inspect_render.h` joined the lint's display-render scan list (it was already a correct, unguarded 4th call site since 03-11); its two genuine false positives under the render-path rule are marked with the existing `control-bytes-allow` escape valve. A second, independent lint rule now enforces the CLI diagnostic sink itself — no file under `src/cli/` other than `diagnostics.cpp` may write directly to stderr — with its own self-test control clause, narrowly scoped so `main.cpp`'s legitimate `_setmode(_fileno(stderr), ...)` binary-mode call is not a false positive.
- **T-2-33's security record corrected, not silently re-closed.** `.planning/phases/02-core-engine/02-SECURITY.md` gained a Post-Audit Correction section (matching the existing T-2-24 correction's shape): what the earlier same-day Closure Note verified and remained true, what it missed (CR-03/WR-01/IN-02, found by this phase's own `03-VERIFICATION.md`), and what `03-15` completed — plus an updated Security Audit Trail row and Sign-Off line. Threat counts are unchanged (88/88, 0 open); only the closure evidence is corrected.

## Task Commits

Each task was committed atomically:

1. **Task 1: Make the JUnit XML renderer control-byte safe, and make the header comment true** - `639dfe6` (fix)
2. **Task 2: One sanitizing CLI diagnostic helper, replacing 44 inline stderr writes** - `a6e6eb8` (feat)
3. **Task 3: Prove the diagnostic path escapes, and record T-2-33 as genuinely closed** - `b1c5d28` (test)

## Files Created/Modified

- `src/report/junit.cpp` - `xml_escape`'s default branch escapes C0 (except tab/LF/CR) and DEL as `\xHH`; both file-level and function-level comments corrected
- `src/util/sanitize.h` - Header comment corrected: names what `junit.cpp` actually does (not "handles its own context"); no longer names a fixed render-path count, points at the lint's scan list instead
- `tests/unit/test_junit.cpp` - 6 new TEST_CASEs for Task 1's behaviors 1-6
- `src/cli/diagnostics.h` / `src/cli/diagnostics.cpp` (new) - `report_cli_error`: the one permitted CLI diagnostic sink, sanitizes before writing, printing-only
- `src/cli/commands/{compare,dir,inspect,snapshot,explain,list_checks}.cpp`, `src/cli/main.cpp` - All inline `fputs(...stderr)` diagnostics replaced with `report_cli_error`; remaining stdout content writes switched to `fwrite`
- `src/cli/commands/inspect_render.h` - Two `control-bytes-allow` markers added (registry check-id group comparison; deliberately-unsanitized JSON output), matching its existing top-of-file rationale
- `scripts/lint_control_bytes.sh` - `inspect_render.h` added to the display-render scan list; a second rule + its own self-test enforces the one-sink CLI diagnostic invariant
- `CMakeLists.txt` - `src/cli/diagnostics.{h,cpp}` added to the CLI target and the header-compile FILE_SET
- `tests/integration/test_cli_diagnostics_escaping.cpp` (new) - 4 TEST_CASEs proving the sink escapes control bytes cross-platform via an argv-supplied vector
- `tests/integration/CMakeLists.txt` - New test registered
- `.planning/phases/02-core-engine/02-SECURITY.md` - T-2-33 entry corrected: Post-Audit Correction section, updated Audit Trail row, updated Sign-Off line

## Decisions Made

See `key-decisions` in frontmatter for full rationale on each. In short: the XML fix stays local to `xml_escape` (never routes through `sanitize_for_display`, never emits an XML numeric character reference); `report_cli_error` is printing-only so exit-code choice stays at the call site; every `fputs` in scope — diagnostic and content alike — was eliminated so "zero direct stream writes outside the sink" is literally checkable; the lint's new rule is scoped narrowly enough to avoid a false positive on `main.cpp`'s legitimate binary-mode call; the cross-platform test uses an argv vector specifically because a control-byte-carrying filename cannot exist on Windows; and the security record was corrected in place rather than quietly re-marked closed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Removed a NUL-byte CLI-diagnostics test case that could not pass by construction**
- **Found during:** Task 3
- **Issue:** An initial draft of `test_cli_diagnostics_escaping.cpp` included a TEST_CASE asserting that an embedded NUL byte in an argv-supplied `--profile` value reaches stderr escaped rather than truncated. This is not achievable through `execve`'s own argv contract: a NUL byte terminates a C string at the OS level, so anything after it in the argument is never seen by the child process at all — this is an OS/exec-layer property, not a defect in `report_cli_error` or `sanitize_for_display`, and no fix in this codebase can change it.
- **Fix:** Replaced the NUL-byte case with a DEL-byte (0x7F) case — DEL is a second, independent byte value beyond the ESC byte already covered, proving the escaping is not accidentally narrowed to only the C0 range, without depending on an argv property the test cannot control.
- **Files modified:** `tests/integration/test_cli_diagnostics_escaping.cpp`
- **Verification:** All 4 TEST_CASEs pass; confirmed the DEL case fails when Task 2's helper is reverted (see Issues Encountered below).
- **Committed in:** `b1c5d28` (Task 3 commit)

**2. [Rule 2 - Missing Critical] Added a 4th TEST_CASE beyond the plan's minimum coverage**
- **Found during:** Task 3
- **Issue:** The plan's own acceptance criterion requires `ctest -R "integration.*diagnostics_escaping"` to select at least 4 tests; the initial draft (ESC-byte, ordinary-value, DEL-byte) had only 3.
- **Fix:** Added a 4th TEST_CASE exercising a second, independent `Error::message` call site (`--set`'s malformed-argument path in `src/cli/options.cpp`'s `append_overrides`, distinct from `--profile`'s `resolve_profile_selection`) — both routed through the same `report_cli_error` helper, directly demonstrating WR-01's "one sink, not 44 individually-patched call sites" fix.
- **Files modified:** `tests/integration/test_cli_diagnostics_escaping.cpp`
- **Verification:** `ctest -R "integration.*diagnostics_escaping"` selects 4 tests, all green.
- **Committed in:** `b1c5d28` (Task 3 commit)

---

**Total deviations:** 2 auto-fixed (1 Rule 1 bug-fix, 1 Rule 2 missing-critical). Both confined to Task 3's own test file; no production code affected.

## Issues Encountered

**Reintroduction verification performed and reverted, not committed (as the plan's own acceptance criteria require).** Task 2: reintroduced one inline `std::fputs(..., stderr)` call in a scratch copy of `compare.cpp` and confirmed `scripts/lint_control_bytes.sh` fails, naming the exact file and line — then restored the file byte-identical to its committed state (diff confirmed empty) before committing. Task 3: reverted `src/cli/diagnostics.cpp`'s `report_cli_error` to an unsanitized `fputs` implementation in a scratch build and confirmed 3 of the 4 new integration tests FAIL (raw ESC/DEL bytes reaching stderr) — then restored the real implementation (diff confirmed empty) before rebuilding and re-verifying all tests green.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

This was the final gap-closure plan for Phase 3 (wave 13, `depends_on: ["03-12"]`). T-2-33 is now genuinely closed across every output format the tool produces (terminal, provenance chain, Markdown — 03-11; JUnit XML, CLI stderr diagnostics — 03-15), and `.planning/phases/02-core-engine/02-SECURITY.md` accurately records the two-stage closure rather than a single premature one. All 620 tests green, all 6 lint scripts green, no golden changed, no exit code changed, no check value changed. Phase 3's own `03-VERIFICATION.md`-flagged blocking gaps (CR-03, WR-01, IN-02) are now resolved; no further Phase 3 plans are outstanding.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-04*

## Self-Check: PASSED

All 3 created files confirmed present on disk (`src/cli/diagnostics.h`, `src/cli/diagnostics.cpp`, `tests/integration/test_cli_diagnostics_escaping.cpp`); all 3 task commit hashes (`639dfe6`, `a6e6eb8`, `b1c5d28`) confirmed present in `git log --oneline --all`.
