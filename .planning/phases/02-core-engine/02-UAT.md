---
status: diagnosed
phase: 02-core-engine
source: [02-VERIFICATION.md]
started: 2026-08-16T13:20:00Z
updated: 2026-08-16T14:05:00Z
round: 2
prior_round: "2026-08-15/16 — 2 tests, 1 passed, 1 blocker issue. That blocker produced gaps G-02-1 and G-02-2, both now closed by plans 02-12 and 02-13. Full prior record in git history and in 02-VERIFICATION.md."
---

## Current Test

[testing complete — test 1 reported as issue, test 2 still blocked behind it]

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
expected: |
  Running mediadiff's compare / dir / TTY report output in cmd.exe or Windows Terminal, with
  the `SetConsoleMode` `ENABLE_VIRTUAL_TERMINAL_PROCESSING` path exercised, shows colour as
  actual ANSI-interpreted styling rather than literal escape-sequence bytes.
why_human: |
  Carried forward unchanged from round 1's test 1, which could not be reached because no
  Windows binary was ever produced. Blocked on the same push as test 1 above — once a green
  Windows build exists, this becomes reachable for the first time in the phase's history.
result: blocked
blocked_by: G-02-3
note: |
  Still unreachable, but strictly closer than in round 1. The Windows build now reaches
  [65/97] and links mediadiff.exe successfully at [49/97] — the failure is in a unit-test
  translation unit, not in the product binary. Once G-02-3 lands, the full build should
  produce the artifact this test has been waiting on since round 1.

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
  status: failed
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
  status: failed
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

## Pre-Existing CI Failures (unchanged, not Phase 2 regressions)

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
