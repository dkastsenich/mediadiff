---
status: resolved
phase: 02-core-engine
source: [02-VERIFICATION.md]
started: 2026-08-16T13:20:00Z
updated: 2026-08-18T16:00:00Z
round: 4
prior_round: "Round 1 (2026-08-15/16) produced G-02-1 and G-02-2, closed by plans 02-12/02-13. Round 2 (2026-08-16) confirmed those closed and opened G-02-3 and G-02-4, closed by plans 02-14/02-15. Full prior record in git history and in 02-VERIFICATION.md."
round_3_verdict:
  run_id: 31946964023
  head_sha: 8d4aa4ac99c024c89e9816f843eb0b540e152a4a
  decided_by: human
  decided_at: 2026-08-18
  G-02-3: "CLOSED — confirmed by the run's own log. Build step conclusion `success`; zero C4702/C2220/C4996; test_report_model.cpp.obj clean at [60/97]; both test executables linked."
  G-02-4: "CLOSED — confirmed by the run's own log. arm64-osx `100% tests passed out of 297`; test #40 Passed; zero `test_dir_pairing.cpp:108: FAILED`; zero `4 == 5`."
  windows_leg: "STILL RED, new cause, ROUND-4 GAP OPENED (human decision). The Windows test suite executed for the first time in project history and 7 of 297 tests failed. Recorded below as G-02-5, G-02-6, G-02-7."
  deferred_legs: "RE-CONFIRMED DEFERRED (human decision). CI-x64-osx and CI-arm64-linux both still match their recorded signatures in this run and remain out of scope and non-blocking."
round_4_verdict:
  run_id: 32153890395
  head_sha: d7274258ad73b61a4927721f0020e66099e65912
  decided_by: human
  decided_at: 2026-08-18
  outcome: "ALL FOUR REQUIRED MERGE CONTEXTS GREEN SIMULTANEOUSLY — first time in the project's history. lint (ENG-16 boundary), build (x64-linux) 298/298, build (arm64-osx) 298/298, build (x64-windows-static-md) Build success with zero C4702/C2220/C4996 and Test success at 0 failed out of 298."
  G-02-5: "CLOSED — all four affected tests plus the new TRUST-05 parity test pass on Windows."
  G-02-6: "CLOSED — the non-ASCII filename test passes on Windows."
  G-02-7: "CLOSED — both halves. #236 closed directly by the CLI11 argv-classification fix; #247 confirmed downstream of the stdout binary-mode fix by a designed experiment (its file was never modified) and now passes."
  green_legs_not_regressed: "x64-linux and arm64-osx both moved 297 -> 298, not down. .gitattributes renormalized no existing content (git add --renormalize stages nothing)."
  trust_05_human_item: "CLOSED BY AUTOMATED TEST (human decision). The new `json_schema - TRUST-05: --json to stdout and --json=<path> produce identical bytes` asserts it on all three blocking legs every run. The manual redirected-output check is no longer required."
  uat_test_2: "DEFERRED TO PHASE 3 (human decision). Windows console colour rendering needs a human at real Windows hardware; a usable artifact exists (mediadiff-windows-x64, 1301968 bytes, sha d727425) but the check does not block Phase 2."
  deferred_legs: "RE-CONFIRMED DEFERRED. CI-x64-osx and CI-arm64-linux unchanged signatures, out of scope, non-blocking. The three Deferred Follow-Ups also remain open and non-blocking."
---

## Current Test

[ROUND 4 COMPLETE — test 1 RESOLVED (all four required contexts green, run 32153890395). Test 2 DEFERRED TO PHASE 3 by human decision: needs real Windows hardware, does not block Phase 2.]

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
result: issue
reported: "windows build and arm64-osx are still failing in the ci"
severity: blocker
round_3_outcome: |
  RESOLVED FOR arm64-osx, PARTIALLY RESOLVED FOR WINDOWS. Run 31946964023 (headSha 8d4aa4a)
  carried the 02-14/02-15 fixes plus the 02-16 lint wiring.

  - `build (arm64-osx)`: GREEN. `100% tests passed out of 297`. G-02-4 closed.
  - `build (x64-windows-static-md)`: Build step conclusion `success` — zero C4702/C2220/C4996,
    test_report_model.cpp.obj clean at [60/97], both test executables linked at [95/97] and
    [96/97]. G-02-3 closed. The Test step then ran FOR THE FIRST TIME EVER and reported
    `98% tests passed, 7 tests failed out of 297`.
  - `lint (ENG-16 boundary)`: GREEN, with all four lint steps confirmed run by name.
  - `build (x64-linux)`: GREEN, 297/297 — no regression from either wave-1 plan.

  Human verdict 2026-08-18: open a round-4 gap for the 7 Windows test failures (G-02-5,
  G-02-6, G-02-7 below); keep both non-required legs deferred.
evidence: |
  Branch pushed; CI run 31943688186 tested headSha 1e065bb — the exact commit carrying both
  gap fixes. Both target legs still red, but for NEW causes, not the original ones.
note: |
  BOTH ORIGINAL GAPS ARE CLOSED. The fixes worked and were then unmasked by their own success —
  precisely the outcome both plans' scope_caveat sections predicted.

  G-02-1 (Windows): every former getenv site now compiles clean under MSVC /W4 /WX —
  snapshot.cpp at [35/97], dir.cpp at [38/97], the new test_fs_utf8.cpp at [42/97]. The build
  advanced from its old stop at [32/97] to [65/97]. Zero C4996 diagnostics in the run. It now
  fails at [60/97] on an unrelated C4702.

  G-02-2 (arm64-osx): the count guard passed and the full 297-test suite executed for the
  first time in the project's history. 296 of 297 passed. It now fails on one genuine test
  defect the guard had been hiding.

  Two NEW gaps opened below (G-02-3, G-02-4). Neither is a regression from the gap-closure
  work — both are pre-existing Phase-2 defects in code that had never been compiled by MSVC
  or executed on macOS. Test 2 remains unreachable: still no Windows binary.

### 2. Colour renders as real styling in a Windows console
<!-- DEFERRED TO PHASE 3 by human decision 2026-08-18. Artifact available: mediadiff-windows-x64 (1301968 bytes) at sha d727425 from run 32153890395. Does not block Phase 2 completion. -->
expected: |
  Running mediadiff's compare / dir / TTY report output in cmd.exe or Windows Terminal, with
  the `SetConsoleMode` `ENABLE_VIRTUAL_TERMINAL_PROCESSING` path exercised, shows colour as
  actual ANSI-interpreted styling rather than literal escape-sequence bytes.
why_human: |
  Carried forward unchanged from round 1's test 1, which could not be reached because no
  Windows binary was ever produced. Blocked on the same push as test 1 above — once a green
  Windows build exists, this becomes reachable for the first time in the phase's history.
result: deferred_to_phase_3
blocked_by: null
previously_blocked_by: G-02-3
note: |
  ROUND 3 UPDATE — THIS TEST IS NOW REACHABLE FOR THE FIRST TIME IN THE PHASE'S HISTORY.
  Run 31946964023's Windows Build step concluded `success` and produced `mediadiff.exe`
  along with both test executables. The artifact this test has been waiting on since round 1
  now exists.

  It has NOT been run. Running it needs a real Windows console (cmd.exe or Windows Terminal)
  and the built binary — neither available on the Linux dev host. The 7 open Windows test
  failures (G-02-5/6/7) do not block this check: they are test-suite failures, not build
  failures, and the product binary links and is downloadable from the run's artifacts.

  Worth noting one interaction: G-02-5 (newline/text-mode translation) touches how output
  bytes are written on Windows. If this colour check is run before G-02-5 is closed, record
  the mediadiff build sha with the result, since a later newline fix could plausibly change
  what the console receives.

  Round-1 note, retained: the earlier blocker was that no Windows binary had ever built.

## Summary

total: 2
passed: 0
issues: 1
pending: 0
skipped: 0
blocked: 1

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

- gap_id: G-02-3
  truth: "mediadiff builds clean on x64-windows-static-md under /W4 /WX, producing the Windows binary the console-VT checkpoint needs"
  status: closed
  closed_by: 02-14-PLAN.md
  closed_at: 2026-08-18
  closure_evidence: |
    CI run 31946964023, job 95164409600, headSha 8d4aa4a. Build step conclusion `success`.
    `grep -c` over the 2032-line job log: C4702 = 0, C2220 = 0, C4996 = 0.
    `test_report_model.cpp.obj` built at [60/97] with no diagnostic following it — the exact
    step that previously emitted C4702 at lines 56/57. Build advanced through
    [95/97] Linking mediadiff_unit_tests.exe and [96/97] Linking mediadiff_integration_tests.exe.
    The fix (std::find_if + INFO + REQUIRE + one reachable return) dissolved the parity
    conflict rather than suppressing it: GCC/Clang stay quiet under -Wall -Wextra -Werror and
    MSVC emits nothing under /W4 /WX. Signature and all five call sites unchanged.
    Caveat recorded honestly: the plan's literal criterion asked for `[97/97]` in the log;
    ninja's last printed edge is `[96/97]`. The Build step's own conclusion is `success`, so
    the criterion's intent is met, but the literal string is absent.
  superseded_by: [G-02-5, G-02-6, G-02-7]
  former_status: failed
  reason: "Successor to G-02-1, NOT a recurrence of it. Zero C4996 diagnostics remain — every getenv site compiles clean. The build now advances to [65/97] and fails at [60/97] on an unrelated C4702 in a translation unit MSVC had never reached before."
  severity: blocker
  test: 1
  regression_of_gap_closure: false
  root_cause: |
    tests/unit/test_report_model.cpp's file-local helper `block_for` ends with Catch2's
    FAIL(), which throws. The two statements after it exist solely to give the function a
    return path on GCC/Clang, which otherwise emit -Wreturn-type. MSVC's flow analysis
    correctly proves them unreachable and emits C4702; mediadiff_apply_warnings() applies
    /W4 /WX to all four first-party targets, so /WX escalates it to C2220.

    This is a genuine toolchain-parity conflict, not a one-sided defect: the code that
    silences GCC/Clang is exactly the code MSVC rejects. Any fix must satisfy both, which is
    why deleting the dead lines is not a fix.

    Never caught before because the Windows build previously stopped at [32/97] on the first
    C4996; this file is [60/97]. It is one of the 65 steps G-02-1's own scope_caveat flagged
    as never having been compiled by MSVC in any run.
  artifacts:
    - path: "tests/unit/test_report_model.cpp"
      issue: "lines 56-57 — `static mediadiff::GroupBlock unreachable{...}` and `return unreachable;` after a throwing FAIL(); C4702 → C2220 under /WX"
  missing:
    - "Restructure `block_for` so no dead code exists on ANY toolchain — user-selected approach. Return a pointer (nullptr on miss, checked at the call site) or otherwise give the function a single reachable exit. Do NOT reach for `#pragma warning(suppress: 4702)`: the project's toolchain-parity constraint makes a compiler-specific escape hatch in test code the wrong shape, and it leaves the same trap for the next helper written this way."
    - "Scan for the same FAIL()-then-dead-return shape in other test helpers before declaring this closed — the pattern is copy-paste-prone and 32 build steps still lie beyond [65/97], never compiled by MSVC in any run."
  scope_caveat: "Fixing this is necessary but not provably sufficient. Steps [66/97]–[97/97] have STILL never been compiled by MSVC, and the Windows test suite has never executed. Expect the possibility of a third iteration on this leg; that is not failure, it is the cost of a platform whose first 32 steps took two rounds to clear."
  evidence: "CI run 31943688186 job 95156439373, headSha 1e065bb. Progression vs. run 31937349647 (stopped [32/97], C4996): now [65/97], zero C4996, fails [60/97] C4702."

- gap_id: G-02-4
  truth: "The blocking arm64-osx CI leg runs the test suite to completion, so PR #2 can reach a mergeable state"
  status: closed
  closed_by: 02-15-PLAN.md
  closed_at: 2026-08-18
  closure_evidence: |
    CI run 31946964023, job 95164409605, headSha 8d4aa4a. `100% tests passed out of 297`.
    Test #40 `unit.dir_pairing: the returned order is byte-wise sorted` → Passed.
    `grep -c 'test_dir_pairing.cpp:108: FAILED'` = 0; `grep -c '4 == 5'` = 0.
    The `beta.snap.json` → `Zulu.snap.json` replacement holds five distinct files on
    case-insensitive APFS while STRENGTHENING the assertion: byte-wise and case-folded
    orderings now differ in four of five positions rather than one pair, and the teeth
    assertion was watched failing on a de-fanged fixture before being restored.
    `src/cli/dir_pairing.cpp` untouched — the product was never at fault.
    This gap's scope_caveat predicted higher confidence here than on Windows (the macOS
    suite had already run end to end at 296/297) and that prediction held exactly.
  former_status: failed
  reason: "Successor to G-02-2, NOT a recurrence. The count guard fix worked: the full 297-test suite executed for the first time ever on macOS. 296 passed; 1 genuine test defect was revealed behind the guard — exactly what G-02-2's scope_caveat budgeted for."
  severity: blocker
  test: 1
  regression_of_gap_closure: false
  product_defect: false
  root_cause: |
    tests/unit/test_dir_pairing.cpp:100 builds its fixture from five names including BOTH
    "Beta.snap.json" and "beta.snap.json". macOS ships APFS case-insensitive by default, so
    those two names denote ONE file: write_file() writes the second over the first and only
    four files exist on disk. Line 108's REQUIRE(result->size() == names.size()) then compares
    4 against 5 and fails. Catch2 reports "assertions: 2 | 1 passed | 1 failed" — line 107's
    has_value() passed, line 108 failed — which pins the failure to the count, not the order.

    THE PRODUCT IS NOT AT FAULT. src/cli/dir_pairing.cpp:146 keys pairing on
    std::map<std::string, FilePair>, ordered by std::string::operator< — byte-wise and
    platform-independent by construction, with no reliance on directory_iterator order. The
    byte-wise ordering DIR-04 requires holds on macOS; the test never got far enough to check
    it. This is a fixture-portability defect, not a determinism defect.
  artifacts:
    - path: "tests/unit/test_dir_pairing.cpp"
      issue: "line 100 — fixture names 'Beta.snap.json' and 'beta.snap.json' collide on a case-insensitive filesystem, so line 108's size assertion fails on macOS with 4 != 5"
  missing:
    - "Make the fixture portable WITHOUT weakening what it asserts. The case-pair is there to prove uppercase sorts before lowercase byte-wise ('B'=0x42 < 'a'=0x61) — a real property that must still be tested. Replace the colliding pair with names that exercise the same byte-ordering boundary without differing only by case (e.g. distinct stems whose first bytes straddle the upper/lower boundary), so the assertion keeps its teeth on every filesystem."
    - "Do NOT relax the assertion to a case-insensitive comparison, and do NOT drop one of the two names. Either would mute the byte-wise-order check that DIR-04 depends on — the project treats a muted gate as worth nothing."
    - "Audit the other dir_pairing fixtures and any test writing files by name for the same case-collision assumption, so the next macOS run does not surface a fourth round of this."
  scope_caveat: "This is the only failure among 297 tests on macOS, so unlike the Windows leg there is no large unexecuted remainder hiding behind it — the suite ran end to end. Confidence that fixing this greens the leg is correspondingly higher."
  evidence: "CI run 31943688186 job 95156439342, headSha 1e065bb. 296/297 passed; test #40 'unit.dir_pairing: the returned order is byte-wise sorted' FAILED at tests/unit/test_dir_pairing.cpp:108."

## Round-4 Gaps — first-ever Windows test-suite execution

<!--
  Opened 2026-08-18 by human verdict at 02-16 Task 3, against CI run 31946964023
  (job 95164409600, headSha 8d4aa4a). ALL THREE are successors to G-02-3, not recurrences
  of it: the Windows BUILD is green and the product binary links. These are failures in the
  Windows TEST SUITE, which had never executed in any run in this project's history before
  this one. None is a regression from plans 02-14 or 02-15.

  Grouped by inferred root cause rather than one-gap-per-test, because 7 failing tests appear
  to stem from ~3 causes and fixing them test-by-test would miss the shared mechanism.
  The groupings are INFERENCES FROM OBSERVABLES, not confirmed diagnoses — no code has been
  read for any of them. Each gap says what would confirm or refute its hypothesis.
-->

- gap_id: G-02-5
  truth: "mediadiff's byte-level output is identical on Windows and POSIX — goldens match, and captured subprocess bytes are exactly what the child wrote"
  status: closed
  closed_at: 2026-08-18
  closure_evidence: |
    All four affected tests pass on Windows in run 32153890395: the three golden tests (junit/markdown/tty), and the process_spawn byte-exact capture. The new `json_schema - TRUST-05: --json to stdout and --json=<path> produce identical bytes` also passes on x64-linux, arm64-osx AND x64-windows-static-md — byte-identical --json is now ASSERTED on Windows rather than assumed. Closed by plan 02-17 (.gitattributes + _setmode binary stdout/stderr in wmain, landed in one commit because either alone breaks the other's tests) and plan 02-18 (Python child writes via sys.stdout.buffer).
  severity: blocker
  test: 1
  round: 4
  successor_to: G-02-3
  regression_of_gap_closure: false
  affects_tests: [95, 67, 75, 201]
  hypothesis: |
    Text-mode / newline translation. This is the strongest-supported of the three groupings.

    The decisive observable is test #95: `REQUIRE( result.out.size() == expected_len )` failed
    with `85000 (0x14c08) == 82500 (0x14244)`. The delta is exactly 2500 bytes. If the child
    wrote 2500 newlines and the parent's pipe read translated each LF into CRLF, the byte count
    inflates by exactly 2500. That arithmetic is a strong fingerprint, not a coincidence.

    The three golden failures are consistent with the same cause: all report a mismatch
    "at line 1" where the expected and actual strings render IDENTICALLY in the log — the
    signature of a difference in invisible bytes (a trailing \r), not in visible content.
  artifacts:
    - path: "tests/unit/test_process_spawn.cpp"
      issue: "line 42 — REQUIRE( result.out.size() == expected_len ) fails 85000 == 82500 on Windows; delta is exactly 2500 = the newline count"
    - path: "tests/support/golden.cpp"
      issue: "line 119 — golden mismatch reported for 'junit_basic' (test #67), 'markdown_basic' (test #75), and the tty golden (test #201), each 'at line 1' with visually identical expected/actual"
  missing:
    - "Confirm or refute the CRLF hypothesis before writing any fix. Cheapest decisive check: dump the differing bytes as hex at the first mismatch rather than as text — a 0x0D before 0x0A proves it. Do NOT fix on the strength of the arithmetic alone; the 2500 delta is compelling but it is still an inference."
    - "Find the boundary where translation is being applied. Candidates in order of likelihood: the pipe read in the process-spawn implementation (does it open the handle in text mode?), the golden file read in tests/support/golden.cpp, and the report writers' own stream opening. Determinism is a stated project property, so the answer must be a deliberate choice about where bytes are normalised, not a per-test patch."
    - "Fix at ONE boundary, not four. Four tests failing from one cause must be closed by one change; if the fix needs touching all four test files, the root cause has not actually been found."
    - "Do NOT normalise goldens by stripping \\r inside the comparison helper. That would mute the check on every platform to make one platform pass — the project treats a muted gate as worth nothing, and byte-identical output is a REPORT-level guarantee, not a test-harness convenience."
  scope_caveat: "This is the largest of the three groupings and the only one whose fix plausibly touches production code rather than test code. If the boundary turns out to be in the report writers, the blast radius reaches all four report formats and the fix needs its own verification on the already-green legs, not just Windows."
  evidence: "CI run 31946964023, job 95164409600. Test step: `98% tests passed, 7 tests failed out of 297`. test_process_spawn.cpp(42): 85000 (0x14c08) == 82500 (0x14244). golden.cpp(119) x3."
  lead_from_verification: |
    ADDED 2026-08-18 during round-3 re-verification, then independently re-checked. This is a
    LEAD, not a confirmed diagnosis — it advances the "find the boundary" bullet above rather
    than adding new scope, and it may widen this gap from test-portability to PRODUCT defect.

    **A. No `.gitattributes` exists in the repository.** Golden fixture files are therefore
    checked out with whatever line endings git's platform default produces. On a Windows
    runner that plausibly means CRLF on disk while the code emits LF — which would explain
    tests #67, #75 and #201 (`golden mismatch ... at line 1`, expected and actual visually
    identical) with no product defect at all. Check this FIRST: it is the cheapest of the two
    explanations and, if it is the whole story for the golden trio, the fix is a
    `.gitattributes` entry rather than a code change.

    **B. stdout is never put into binary mode, while file destinations always are.** Verified
    by direct grep, twice, on 2026-08-18:
      - `grep -rn '_setmode\|_O_BINARY\|O_BINARY' src/` → ZERO matches anywhere in src/.
      - File destinations correctly open binary: `src/cli/commands/compare.cpp:71`,
        `src/cli/commands/dir.cpp:410`, `src/cli/commands/dir.cpp:439` and
        `src/core/snapshot.cpp:333` all use `fopen_utf8(..., "wb")`.
      - stdout is written with plain `std::fputs` / `fwrite` and no mode change.

    On Windows the CRT opens stdout in TEXT mode by default, translating every `\n` into
    `\r\n`. If that applies here, then `mediadiff compare --json > out.json` and writing the
    same report to a file via `--json-path` would produce DIFFERENT BYTES — an asymmetry
    entirely invisible on Linux and macOS, where the two paths are identical.

    That would bear directly on a hard project constraint: byte-identical `--json` across
    identical runs. It would also mean the determinism guarantee is currently only PROVEN on
    POSIX, never on Windows, because the Windows test suite had never executed until run
    31946964023.

    **Not confirmed.** Neither the Linux dev host nor any current CI leg can observe Windows
    CRT stdout behaviour, and no test asserts redirected-stdout bytes on Windows. Confirming
    or refuting it needs either a Windows console run or a new test that captures redirected
    stdout and compares it byte-for-byte against the file-destination output.
  missing_addendum:
    - "Resolve lead A before lead B — a `.gitattributes` fix may account for the three golden failures entirely, and knowing that first prevents an unnecessary product change."
    - "If lead B holds, the fix belongs at the stdout boundary once (binary mode at startup), NOT per report writer, and it must be covered by a test that compares redirected-stdout bytes against file-destination bytes — otherwise the guarantee stays unproven on the one platform that can break it."

- gap_id: G-02-6
  truth: "A non-ASCII filename round-trips through mediadiff's path handling unchanged on Windows"
  status: closed
  closed_at: 2026-08-18
  closure_evidence: |
    `unit.dir_pairing: a tree containing a non-ASCII filename pairs correctly` passes on Windows in run 32153890395. Closed by plan 02-18's `tests/support/utf8_path.h` `path_from_utf8` helper, which mirrors src/cli/dir_pairing.cpp's two-branch conversion. The assertion and its non-ASCII literal are unchanged; no platform guard, no ASCII retreat. Sibling-literal audit completed: 8 sites examined, only the fixed one reaches the filesystem.
  severity: blocker
  test: 1
  round: 4
  successor_to: G-02-3
  regression_of_gap_closure: false
  affects_tests: [36]
  hypothesis: |
    Filesystem/console character encoding. `tests/unit/test_dir_pairing.cpp:204` failed with
    `CHECK( (*result)[0].relative_path == non_ascii_name )` expanding to
    `"rÃ©sumÃ©.snap.json" == "résumé.snap.json"`.

    `Ã©` is the textbook rendering of the two UTF-8 bytes 0xC3 0xA9 (é) interpreted one-byte-
    per-character through a Latin-1/CP1252 lens. So the bytes are intact and the DECODING is
    wrong — a codepage problem at a boundary, not data loss.
  not_related_to: |
    This is in the same FILE that plan 02-15 edited, and that resemblance is misleading.
    02-15 changed the fixture in the byte-wise-ordering test at roughly lines 100-110.
    Test #36 is a separate test case at line 204 that 02-15 never touched, and its failure is
    a character-encoding mismatch rather than a count or ordering mismatch. Do not treat this
    as fallout from G-02-4's closure.
  artifacts:
    - path: "tests/unit/test_dir_pairing.cpp"
      issue: "line 204 — CHECK( (*result)[0].relative_path == non_ascii_name ) fails with mojibake: 'rÃ©sumÃ©.snap.json' vs 'résumé.snap.json'"
  missing:
    - "Establish WHICH boundary mis-decodes before fixing: the source literal's own encoding as MSVC sees it (does the file need a BOM, or /utf-8 — the project already specifies /utf-8, so verify it is actually applied to this target), the filesystem write, the directory read, or the comparison. Print the bytes of both sides in hex at the failure; the side holding 0xC3 0xA9 is correct and the side holding two separate characters is the mis-decoded one."
    - "The project already commits to /utf-8 on MSVC in its stated constraints. If that flag is present and this still fails, the defect is at the std::filesystem boundary (narrow vs wide path APIs), which is a different and more interesting fix than a compiler flag."
    - "Do NOT fix by making the test ASCII-only. The test exists because non-ASCII filenames are real, and mediadiff is a cross-platform CLI whose users have them."
  scope_caveat: "One failing test, but the underlying question — how mediadiff handles non-ASCII paths on Windows — is broader than this assertion and may already affect the product binary, not just the test. Whatever is learned here should be checked against the CLI's actual path handling before the gap is called closed."
  evidence: "CI run 31946964023, job 95164409600. test_dir_pairing.cpp(204), assertions: 5 | 4 passed | 1 failed."

- gap_id: G-02-7
  truth: "mediadiff's documented exit-code contract and inspect output hold identically on Windows"
  status: closed
  closed_at: 2026-08-18
  closure_evidence: |
    Both halves closed in run 32153890395. #236 `integration.exit_codes - 65` passes: closed by plan 02-19's `allow_windows_style_options(false)` on the root App above subcommand registration — the real cause was CLI11 classifying `/no/such/file.snap.json` as a Windows-style OPTION so it never reached read_snapshot. src/cli/exit_code.cpp was correct all along (input_open -> 65 unconditionally) and was NOT modified; the `== 65` assertion was NOT relaxed. #247 `integration.explain_inspect - REPORT-07` passes WITHOUT its file ever being modified this round — a designed experiment confirming it was downstream of 02-17's stdout fix, not an independent defect.
  severity: blocker
  test: 1
  round: 4
  successor_to: G-02-3
  regression_of_gap_closure: false
  affects_tests: [236, 247]
  hypothesis: |
    Genuine behavioral divergence — the least understood of the three groupings, and the one
    most likely to be a real product defect rather than a test-portability defect.

    - Test #236 `tests/integration/test_exit_codes.cpp:94`: `CHECK( result.exit_code == 65 )`
      expanded to `64 == 65`. A nonexistent input path exits 64 on Windows where the contract
      requires 65. Since the CLI maps Error.kind to exit codes, this suggests a nonexistent
      path is being classified as a different error kind on Windows — plausibly because the
      underlying errno/GetLastError mapping differs.
    - Test #247 `tests/integration/test_explain_inspect.cpp:112`:
      `CHECK( found_meta_no_measurements )` → false. This one MAY be downstream of G-02-5:
      if the check scans inspect's stdout line-by-line for a specific line, CRLF endings
      could defeat the match without any inspect-level defect.
  missing:
    - "Split these two before planning a fix. They are grouped here because both are behavioral, not because they share a cause. #247 in particular must be re-checked AFTER G-02-5 lands — if the newline fix makes it pass, it was never its own defect and this gap shrinks to #236 alone."
    - "For #236: determine which Error.kind a nonexistent path produces on Windows and why it maps to 64 rather than 65. The exit-code contract is a TRUST-level guarantee — a CI gate that reports the wrong exit code is exactly the 'muted gate' failure this project treats as P0. Fix the mapping, not the test's expectation."
    - "Do NOT relax either assertion to accept the Windows value. 64 vs 65 is a contract violation, not a platform quirk to be documented away."
  scope_caveat: "The #236 exit-code divergence is the only failure among the seven that points at the PRODUCT rather than at test portability or byte handling. It deserves the most scrutiny even though it is one test, and it should not be planned as a sibling of the newline work."
  evidence: "CI run 31946964023, job 95164409600. test_exit_codes.cpp(94): 64 == 65. test_explain_inspect.cpp(112): found_meta_no_measurements → false, assertions: 3 | 2 passed | 1 failed."

## Pre-Existing CI Failures (unchanged, not Phase 2 regressions)

<!--
  Re-confirmed AGAIN in run 31946964023 (2026-08-18), both matching their recorded
  signatures exactly. Human re-confirmed deferral at 02-16 Task 3: both stay out of scope
  and non-blocking. Neither is counted against the four required merge contexts.
    - CI-x64-osx  (job 95164409614): `ld: warning: ignoring file
      'vcpkg_installed/x64-osx/lib/libavformat.a': fat file missing arch 'arm64',
      file has 'x86_64'` — unchanged.
    - CI-arm64-linux (job 95164409618): failing step is
      `Register vcpkg NuGet feed (read-write, trusted runs only)` — unchanged.
  Both reproduce on main; neither is blocking.
-->
<!-- Re-confirmed in run 31943688186. Both reproduce on main; neither is blocking. -->

- id: CI-x64-osx
  blocking: false
  status: unchanged
  symptom: "ld: ignoring file 'vcpkg_installed/x64-osx/lib/lib*.a' — found architecture 'x86_64', required architecture 'arm64'"
  root_cause: "The x64-osx preset cross-builds from the arm64 macos-15 host. vcpkg honours VCPKG_TARGET_TRIPLET=x64-osx and emits x86_64 static libs, but the project's own compile/link is never given -arch x86_64, so clang targets the arm64 host default and rejects every dependency."
  fix_hint: "Set CMAKE_OSX_ARCHITECTURES=x86_64 in the x64-osx preset. Known failure class already flagged in research/STACK.md."

- id: CI-arm64-linux
  blocking: false
  status: unchanged
  symptom: "'Register vcpkg NuGet feed' step exits 1 in ~15ms with no captured output"
  root_cause: "Undetermined — the step produces no diagnostic. Likely mono/NuGet availability on the ubuntu-24.04-arm runner, but unverified."
  fix_hint: "Investigate with the step's output surfaced; do not guess."
