---
phase: 02-core-engine
plan: 18
subsystem: testing
tags: [catch2, filesystem, utf-8, windows, python, ctest]

# Dependency graph
requires:
  - phase: 02-core-engine
    provides: "pair_directories (02-11), tests/process_spawn.h (02-03), src/util/fs.h's UTF-8 conversion shim (D-04)"
provides:
  - "tests/support/utf8_path.h — mediadiff::test::path_from_utf8, the test tree's one narrow/wide boundary crossing for non-ASCII filesystem fixtures"
  - "A Windows-safe process-spawn fixture (test #95) that writes binary bytes instead of text-mode-translated ones"
  - "A Windows-safe non-ASCII fixture (test #36) whose on-disk filename matches its UTF-8 literal"
  - "A site-by-site audit of every non-ASCII byte literal under tests/, classifying each as filesystem-reaching or in-memory"
affects: ["02-17 (goldens/stdout mode)", "02-19 (#236 exit-code divergence)"]

actuals:
  tokens: 1587
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Test-tree UTF-8 path construction mirrors src/cli/dir_pairing.cpp's two-branch utf8_to_path pattern via a header-only helper in tests/support/"

key-files:
  created:
    - tests/support/utf8_path.h
  modified:
    - tests/unit/test_process_spawn.cpp
    - tests/unit/test_dir_pairing.cpp

key-decisions:
  - "Test #95's byte-count delta (2,500 bytes = kLineCount) is a Python text-mode-stdout artifact in the CHILD process, not a defect in tests/process_spawn.h's reader — fixed by writing through sys.stdout.buffer/sys.stderr.buffer instead of the text wrapper, per G-02-5's diagnosis."
  - "Test #36's mojibake is fs::path's narrow constructor decoding via the CRT active code page on MSVC, not a /utf-8 compiler-flag gap or a src/cli/dir_pairing.cpp defect — fixed by routing the fixture's leaf through a new path_from_utf8 helper that mirrors the product's own utf8_to_path conversion, per G-02-6's diagnosis."
  - "Audited all 8 \\xNN/\\u/\\U non-ASCII literal sites under tests/: only test_dir_pairing.cpp:197 reaches the filesystem (now fixed); the other 7 stay in memory or already round-trip through fopen_utf8, so no further path_from_utf8 call sites are needed this round."

requirements-completed: [BUILD-01, BUILD-05, DIR-04, CLI-09]

coverage:
  - id: D1
    description: "Process-spawn fixture (test #95) writes binary bytes; two no-CR assertions added; byte-count constants and tests/process_spawn.h unchanged"
    requirement: "BUILD-05"
    verification:
      - kind: unit
        ref: "ctest --test-dir build/x64-linux -R process_spawn — unit.process_spawn: captures more than one pipe buffer's worth of stdout and stderr exactly"
        status: pass
    human_judgment: true
    rationale: "The defect and fix are Windows-only; a green local (x64-linux) run proves the change is harmless, not that it closes the gap. Closure of G-02-5 can only be confirmed by the build (x64-windows-static-md) CI leg, which this executor cannot run."
  - id: D2
    description: "Non-ASCII fixture (test #36) constructed through path_from_utf8; hex-bytes INFO diagnostic added; sibling-literal audit recorded"
    requirement: "DIR-04"
    verification:
      - kind: unit
        ref: "ctest --test-dir build/x64-linux --output-on-failure — unit.dir_pairing: a tree containing a non-ASCII filename pairs correctly, plus full 297-test suite"
        status: pass
    human_judgment: true
    rationale: "The defect and fix are Windows-only (MSVC's ANSI-code-page narrow-constructor decoding); a green local (x64-linux) run proves the change is harmless on POSIX, where the narrow constructor was already byte-verbatim. Closure of G-02-6 can only be confirmed by the build (x64-windows-static-md) CI leg, which this executor cannot run."

duration: 25min
completed: 2026-08-18
status: complete
---

# Phase 02 Plan 18: Windows Test-Fixture Byte/Encoding Fixes Summary

**Fixed two Windows-only test-fixture defects (G-02-5 Python text-mode stdout, G-02-6 fs::path ANSI-codepage decoding) via a binary-mode child script and a new tests/support/utf8_path.h helper, neither touching product code.**

## Performance

- **Duration:** 25 min
- **Tasks:** 2
- **Files modified:** 3 (1 created, 2 modified)

## Accomplishments
- Test #95 (`unit.process_spawn`): the generated Python child script now writes through `sys.stdout.buffer`/`sys.stderr.buffer` with explicit flush calls instead of the text-mode wrapper, and two `find('\r') == npos` assertions were added alongside the unchanged byte-count assertions.
- Test #36 (`unit.dir_pairing`): the non-ASCII fixture leaf is now constructed through a new `tests/support/utf8_path.h`'s `mediadiff::test::path_from_utf8`, mirroring `src/cli/dir_pairing.cpp`'s own UTF-8-to-wide conversion; a hex-bytes `INFO` diagnostic precedes the equality assertion.
- Completed the required sibling-literal audit: every `\xNN`/`\u`/`\U` non-ASCII literal under `tests/` was enumerated and classified filesystem-reaching vs. in-memory (see table below).

## Task Commits

1. **Task 1: Make the process-spawn fixture's child write bytes, not text** - `4f17a19` (test)
2. **Task 2: Construct the non-ASCII test path through UTF-8, and audit every sibling site** - `392856b` (test)

## Files Created/Modified
- `tests/support/utf8_path.h` - New header-only `mediadiff::test::path_from_utf8`, mirroring `src/cli/dir_pairing.cpp`'s two-branch conversion (Windows: chains through `util/fs.h`'s UTF-8-to-wide conversion; elsewhere: byte-verbatim narrow constructor). No wide type named directly, no deprecated `u8path`-style factory used.
- `tests/unit/test_process_spawn.cpp` - Generated Python script writes via the binary stream (`sys.stdout.buffer.write`/`sys.stderr.buffer.write`) with explicit flushes; two no-CR `REQUIRE` assertions added; header comment extended with the G-02-5 diagnosis.
- `tests/unit/test_dir_pairing.cpp` - Non-ASCII fixture's leaf now built via `path_from_utf8`; new file-local `hex_bytes` helper plus two `INFO` diagnostics precede the equality `CHECK`; the literal, all five assertions, and 02-15's separate byte-wise-ordering fixture (same file, different test case) are all unchanged.

## Sibling-Literal Audit (Task 2 Part 3, G-02-6's third bullet)

Every `\xNN`/`\u`/`\U`-style non-ASCII literal found under `tests/` (`grep -rn '\x[0-9A-Fa-f][0-9A-Fa-f]' tests/ --include=*.cpp --include=*.h`, plus a direct check of `test_fs_utf8.cpp`'s `\u`/`\U` literals):

| Site | Reaches filesystem? | Disposition |
|------|---------------------|-------------|
| `tests/unit/test_dir_pairing.cpp:197` | **Yes** | Fixed this plan — now routed through `path_from_utf8` |
| `tests/unit/test_toml_load.cpp:138` | No (a `path_glob` string compared in memory against a parsed config value) | No change needed |
| `tests/unit/test_tty_render.cpp:216`, `:224` | No (`\x1B` ANSI escape-sequence probes, in-memory string search) | No change needed |
| `tests/unit/test_tolerance.cpp:59`, `:151`, `:158` | No (tolerance-grammar input strings, parsed in memory) | No change needed |
| `tests/unit/test_markdown_budget.cpp:204` | No (a rendered message body, in-memory string) | No change needed |
| `tests/unit/test_fs_utf8.cpp` (`\u`/`\U`-form literals) | Yes, but already correct by construction — every access goes through `fopen_utf8`/`utf8_to_wide`/`_wremove(utf8_to_wide(...))`, never `std::filesystem::path`'s narrow constructor | No change needed |

**One-sentence verdict on the product's own path handling (G-02-6's `scope_caveat`):** `src/cli/dir_pairing.cpp`'s `utf8_to_path`/`relative_path_to_utf8` pair was read in full and already converts correctly at CP_UTF8 in both directions — the product was never at fault, and it was not modified.

## Decisions Made
- Fixed the Python child's stream mode rather than `tests/process_spawn.h`'s reader — the reader (`CreatePipe`+`ReadFile` on Windows, `pipe()`+`read()` on POSIX) performs no translation, so changing it would have masked a child-side defect behind a harness-side workaround.
- Fixed the test fixture's path construction rather than any compiler flag or `src/cli/dir_pairing.cpp` — `/utf-8` is already applied to both test targets (`CMakeLists.txt:227`, verified in `read_first`), and the byte-explicit `\xNN` literal is unaffected by any source-charset flag; the defect is specifically at the `fs::path` narrow-constructor boundary.
- `path_from_utf8`'s header comment was tightened during Task 2 so that the literal substrings `utf8_to_wide` and `u8path` each appear exactly the number of times the plan's own grep-based acceptance criteria specify (1 and 0, respectively) — the initial draft over-explained in prose and tripped the exact-count checks; content was reworded, not removed, to stay within those counts while still explaining the "why."

## Deviations from Plan

None — plan executed exactly as written. The one iteration (tightening the new header's prose to satisfy its own grep-based acceptance criteria for `utf8_to_wide`/`u8path` occurrence counts) is an editorial correction made while first authoring the file, not a deviation from the plan's action or scope.

## Issues Encountered
None.

## Verification Performed

**Locally provable (all passed, on this Linux host):**
- All `grep`-based structural checks in both tasks' acceptance criteria (exact occurrence counts for `sys.stdout.buffer.write`, `find('\r')`, `path_from_utf8`, `utf8_to_wide`, `u8path`, wide-type names, `kLineCount`/`kLineBytes`, `relative_path == non_ascii_name`, the unchanged literal).
- `git diff --stat` empty for `tests/process_spawn.h`, `src/cli/dir_pairing.cpp`, and `tests/unit/CMakeLists.txt` — none of the three deliberately-unmodified files changed.
- `cmake --build --preset x64-linux` — clean build, zero warnings, under `-Wall -Wextra -Werror`.
- `ctest --test-dir build/x64-linux --output-on-failure` — **297 tests, 0 failed, 1 skipped** (unchanged from the documented baseline; this plan adds no `TEST_CASE`).
- All four required lint scripts (`lint_eng16.sh`, `lint_check_id_strings.sh`, `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`) — all exit 0.

**CI-only (not observable from this worktree, per the plan's own verification section):**
- `build (x64-windows-static-md)`: `unit.process_spawn` reporting `Passed` with zero `85000` occurrences in the job log.
- `build (x64-windows-static-md)`: `unit.dir_pairing`'s non-ASCII test reporting `Passed` with zero `sumÃ` occurrences in the job log, and zero `C4996` warnings from the new header.
- `build (x64-linux)` / `build (arm64-osx)` CI legs continuing to report 0 failed (the local x64-linux run here is the best available proxy, and it passed).

Per the plan's own confidence framing: test #95's fix is the higher-confidence of the two (the delta exactly matches `kLineCount`, and the parent reader was read and confirmed translation-free). Test #36's fix is nearly as strong but has one more inferential step (the runner's actual active code page was inferred from the mojibake pattern, not directly observed) — the hex `INFO` diagnostic added this plan is what makes a fifth round, if needed, a single log read.

## Deferred Follow-Up (recorded per the plan's "Explicitly out of scope" section)

The Task 2 Part 3 audit confirms the plan's own prediction: **no test exercises a non-ASCII path end-to-end through the built `mediadiff` binary.** `test_fs_utf8.cpp` covers the `fopen_utf8` shim in-process and `test_dir_pairing.cpp` covers the pairing walk in-process, but neither spawns `mediadiff.exe` with a non-ASCII argument — the path `CLI-09` actually promises. Per the plan's explicit instruction, this is recorded as a deferred follow-up rather than closed in this plan (adding such a test would grow the suite total that `02-17-PLAN.md`'s count criteria are written against, and it is a coverage gap rather than one of this round's two gaps).

## Next Phase Readiness
- G-02-5 and G-02-6 are closed pending Windows CI confirmation on the next `build (x64-windows-static-md)` run; nothing further is needed from this plan unless that run still fails, in which case the hex `INFO` diagnostic and the no-CR assertions this plan added should make the next round's diagnosis immediate.
- `02-17-PLAN.md` (the three golden failures and mediadiff's own stdout mode) and `02-19-PLAN.md` (`#236`'s exit-code divergence) remain the other open gap-closure plans; this plan did not touch any file either of them owns.
- The end-to-end non-ASCII-argument-through-the-binary coverage gap (above) is available for a future plan to pick up if desired; it is not blocking.

---
*Phase: 02-core-engine*
*Completed: 2026-08-18*
