---
status: diagnosed
trigger: "I created a PR of this branch into main to generate Windows artifacts during CI - and all builds failed"
created: 2026-08-16T00:00:00Z
updated: 2026-08-16T00:00:00Z
gap: G-02-1
mode: find_root_cause_only
bug_class: Bohrbug (deterministic, platform-conditional compile error)
---

## Current Focus

hypothesis: CONFIRMED — `std::getenv` is called from six unguarded source lines across all four
  first-party targets; MSVC deprecates it (C4996) and `mediadiff_apply_warnings()` puts `/W4 /WX`
  on every one of those targets, so each site is a hard error. Only the first was reached.
test: complete (see Evidence)
expecting: n/a — diagnosis delivered
next_action: hand off to gsd-planner. Do NOT fix here (goal: find_root_cause_only).

reasoning_checkpoint:
  hypothesis: "MSVC's CRT deprecates getenv (C4996); mediadiff_apply_warnings() applies /W4 /WX to
    all four first-party targets; the CI Build step builds the default `all` target. Therefore every
    unguarded std::getenv call site in any first-party target is a build-stopping error, not just
    the one in the log."
  confirming_evidence:
    - "CI log run 31937349647: ninja reports '[27/97] ... options.cpp.obj' then 'FAILED: [code=2]'
       with C4996/C2220. Build stopped at [32/97] of 97 — 65 steps never ran."
    - "CMakeLists.txt:219-233,253 + tests/unit/CMakeLists.txt:92 + tests/integration/CMakeLists.txt:99
       — mediadiff_apply_warnings() called on all four targets; MSVC branch is `/W4 /WX /utf-8`."
    - ".github/workflows/ci.yml:214 — `cmake --build --preset x64-windows-static-md`, no --target,
       so the default `all` target (97 steps) is in scope, including both test executables."
    - "Six distinct getenv lines found by grep across src/ and tests/, none inside a _WIN32 guard."
  falsification_test: "If any of the five not-yet-reached sites were inside an `#else // !_WIN32`
    branch, it would not be a Windows defect. Checked each: all six are unguarded. Conversely, the
    POSIX isatty/fileno/ioctl/open/close sites ARE guarded, which is why they compiled clean —
    compare.cpp built successfully at [32/97] despite containing isatty(fileno(stdout))."
  fix_rationale: "n/a — diagnosis-only mode."
  blind_spots:
    - "Cannot run MSVC in this Linux sandbox. Every MSVC claim below is derived from the CI
       diagnostic, the source tree, and Microsoft Learn documentation — never from an executed build."
    - "Because ninja stopped at [32/97], the ~65 unbuilt objects (5 remaining mediadiff sources,
       the header-verify target, and BOTH test executables in full) have NEVER been compiled by
       MSVC in any run. Fixing getenv is necessary but I cannot assert it is sufficient — other,
       unrelated MSVC breakage may be hiding behind it."
  candidate_causes:
    - "code: six unguarded std::getenv call sites (proximate cause)"
    - "config/build: /WX on all four targets escalates C4996 to a hard error (necessary co-condition,
       but intentional and correct project policy — must not be relaxed)"
    - "process/gate: no enforcement confines the platform primitive. scripts/lint_eng16.sh's
       SCAN_DIRS deliberately excludes src/cli — exactly where all three production sites live —
       and its pattern targets stdout/exit, not CRT-deprecated calls. The stated 'one place'
       convention was documented in comments only."
  and_gate: "YES — two conditions must hold simultaneously: (a) an unguarded CRT-deprecated call
    AND (b) /WX on that target. Neither alone fails the build. (b) is deliberate policy and should
    stay, so the actionable cause is (a); (c) the missing enforcement gate is why (a) recurred six
    times instead of once."

## Symptoms

expected: mediadiff builds clean on the `x64-windows-static-md` CI leg under `/W4 /WX`, producing
  the Windows binary the console-VT UAT checkpoint (Test 1) needs.
actual: Build step fails at [27/97]; ninja stops at [32/97]. No binary, no test run, no artifact.
errors: |
  D:\a\mediadiff\mediadiff\src\cli\options.cpp(251): error C2220: the following warning is treated as an error
  D:\a\mediadiff\mediadiff\src\cli\options.cpp(251): warning C4996: 'getenv': This function or variable
    may be unsafe. Consider using _dupenv_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS.
  ninja: build stopped: subcommand failed.
  Compile line: /W4 /WX /utf-8 -std:c++20 -MD ; cl.exe 14.44.35207 (VS2022 Enterprise 17.14.37)
reproduction: Test 1, .planning/phases/02-core-engine/02-UAT.md — push branch, run the
  x64-windows-static-md CI leg. Deterministic: fails identically every run.
started: Phase 2. `src/cli/options.cpp` does not exist on origin/main; the same leg passed on main
  (run 31881353367 job 95004285813). Confirmed Phase 2 regression.

## Eliminated

- hypothesis: "The POSIX blocks (isatty/fileno/ioctl/open/close/posix_spawn) are also unguarded and
    contribute to the Windows failure."
  evidence: "All are inside `#ifdef _WIN32 ... #else ... #endif` — verified in src/util/fs.h:135
    (inside `#else // !_WIN32`), src/cli/options.cpp:266 (inside stdout_is_tty's #else),
    src/cli/commands/compare.cpp:34-63, src/cli/commands/dir.cpp:36-83,
    src/cli/commands/snapshot.cpp:15-25/109-146, tests/process_spawn.h:26-39/147-282.
    Positive proof: compare.cpp compiled successfully at [32/97] in the failing run despite
    containing `isatty(fileno(stdout))` at line 59."
  timestamp: 2026-08-16

- hypothesis: "Other C4996 classes (deprecated std features such as codecvt, std::result_of,
    wstring_convert, std::iterator) also break the MSVC leg."
  evidence: "Targeted grep across src/ and tests/ for the deprecated-std-feature set returned zero
    hits. Also zero hits for the non-getenv deprecated CRT set (strcpy/sprintf/sscanf/strtok/
    localtime/strerror/putenv/tmpnam/...) outside _WIN32-guarded blocks."
  timestamp: 2026-08-16

- hypothesis: "`libmediadiff` also contains a getenv site."
  evidence: "libmediadiff compiled all 22 objects and linked cleanly ([3..26] + [28/97] 'Linking CXX
    static library mediadiff_core.lib') in the failing run. Grep confirms no getenv under
    src/core, src/config, src/compare, src/report, src/util."
  timestamp: 2026-08-16

## Evidence

- timestamp: 2026-08-16
  checked: "CI run 31937349647, job build (x64-windows-static-md), full Build-step log via `gh run view --log`"
  found: |
    Build graph is 97 steps. Progression: [1-2] registry generation; [3-26] libmediadiff objects;
    [27/97] src\cli\options.cpp.obj -> FAILED (C2220 + C4996); [28/97] link mediadiff_core.lib;
    [29-32] main.cpp, exit_code.cpp, tty_render.cpp, commands\compare.cpp all SUCCEEDED;
    then 'ninja: build stopped: subcommand failed.'
  implication: |
    Exactly ONE C4996 was ever emitted, because ninja stopped 65 steps early. The remaining five
    getenv sites were never compiled by MSVC in this or any run. The orchestrator's concern is
    therefore real and load-bearing: a fix that only touches options.cpp moves the failure to the
    next site, not to green.

- timestamp: 2026-08-16
  checked: "CMakeLists.txt:219-233,253; tests/unit/CMakeLists.txt:92; tests/integration/CMakeLists.txt:99"
  found: |
    function(mediadiff_apply_warnings target)
      if(MSVC) target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
      else()   target_compile_options(${target} PRIVATE -Wall -Wextra -Werror) endif()
    Called on: libmediadiff (233), mediadiff (253), mediadiff_unit_tests (unit:92),
    mediadiff_integration_tests (integration:99).
  implication: |
    ALL FOUR first-party targets are warnings-as-errors on MSVC. The test targets are not exempt.
    `/WX` is the escalation half of the AND-gate.

- timestamp: 2026-08-16
  checked: ".github/workflows/ci.yml Build step (line 214) and CMakePresets.json buildPresets"
  found: |
    run: cmake --build --preset x64-windows-static-md
    Build preset carries no "targets" key -> CMake builds the generator default (`all`).
  implication: |
    Both test executables are inside the CI Build step's scope. Their getenv sites will fail the
    same step, not merely the later Test step.

- timestamp: 2026-08-16
  checked: "grep -rn getenv over src/ tests/ tools/, then per-site inspection for _WIN32 guards"
  found: |
    COMPLETE SET — six source lines, seven compilations (golden.cpp is compiled into two targets):

    | # | Site                                        | Function                  | Target(s)                                     | /WX | Reached in CI |
    |---|---------------------------------------------|---------------------------|-----------------------------------------------|-----|---------------|
    | 1 | src/cli/options.cpp:251                     | read_env()                | mediadiff                                     | yes | YES (failed)  |
    | 2 | src/cli/commands/snapshot.cpp:175           | ci_env_is_true()          | mediadiff                                     | yes | no            |
    | 3 | src/cli/commands/dir.cpp:278                | run_dir() inline read     | mediadiff                                     | yes | no            |
    | 4 | tests/support/golden.cpp:23                 | update_goldens_requested()| mediadiff_unit_tests AND *_integration_tests  | yes | no (x2)       |
    | 5 | tests/integration/test_exit_codes.cpp:137   | TEST_CASE body (PATH)     | mediadiff_integration_tests                   | yes | no            |
    | 6 | tests/integration/test_snapshot_safe_write.cpp:136 | TEST_CASE body (PATH)| mediadiff_integration_tests                   | yes | no            |

    None is inside a _WIN32 guard. Comment-only mentions (not calls): src/cli/options.h:149,
    src/cli/color_policy.h:9, src/cli/tty_render.h:8, src/cli/options.cpp:247, tests/support/golden.cpp:18.
  implication: |
    Six lines must change (or route through one shim) to green the MSVC leg. Fixing only #1 relocates
    the failure to #2/#3 (same target), and then to #4/#5/#6 (test targets).

- timestamp: 2026-08-16
  checked: "src/util/fs.h:1-21 header contract, and the _wfopen_s precedent at fs.h:111-120 (commit 47d5965)"
  found: |
    fs.h:3-13 — "mediadiff's UTF-8 filesystem shim (D-04) — the single site where encoding conversion
    happens ... This header and the Windows entry point in src/cli/main.cpp are the only two places
    in this repository permitted to name wide-character types (wchar_t, LPWSTR, ...) or Windows
    code-page constants (CP_UTF8, ...)."
    fs.h:111-115 — "_wfopen_s rather than _wfopen: MSVC deprecates the latter (C4996), and /W4 /WX
    promotes that to a hard error (BUILD-05). The secure variant reports failure through its errno_t
    return and leaves the out-parameter untouched, so it is initialised to nullptr here and the
    failure path returns nullptr — preserving this function's contract exactly."
    Commit 47d5965 "fix(win): use _wfopen_s so MSVC C4996 does not trip warnings-as-errors".
  implication: |
    (a) The precedent is explicit: choose the secure `_s` variant, never _CRT_SECURE_NO_WARNINGS.
    (b) fs.h is the codebase's designated home for #ifdef-ed platform primitives, and the ONLY place
        (besides main.cpp) allowed to use wide-character types. That is architecturally decisive:
        a wide-variant env read (_wdupenv_s) can only live in fs.h. A shim placed in options.cpp
        would be barred by the codebase's own rule from using the wide, encoding-correct form.
    (c) options.cpp's own read_env comment already gestures at fs.h's convention ("matching
        src/util/fs.h's own 'confine platform-specific I/O primitives to one file' convention for
        a different primitive", options.cpp:258-261).

- timestamp: 2026-08-16
  checked: "The 'one place' claims at src/cli/options.h:143-151, src/cli/color_policy.h:9-10,
    src/cli/options.cpp:258-261, src/cli/commands/compare.cpp:41-46 — cross-checked against grep"
  found: |
    SECOND, DOCUMENTATION-LEVEL DEFECT — three of four claims are FALSE as of this snapshot:

    (A) options.h:143-151: "this function, and the platform-specific isatty check it calls, are the
        ONLY place in mediadiff that reads those three environment variables or performs that TTY test"
        -> FALSE for `CI`: also read at src/cli/commands/snapshot.cpp:175 (ci_env_is_true()).
        -> FALSE for the TTY test: isatty(fileno(stdout)) also at compare.cpp:59 and dir.cpp:74.
        -> TRUE for NO_COLOR and GITHUB_ACTIONS (options.cpp is genuinely their only reader).

    (B) color_policy.h:9-10: "read_color_inputs (src/cli/options.h/.cpp) is the one place `getenv`
        and the TTY test are actually called from"
        -> FALSE on BOTH counts. getenv: 3 sites in src/ (options.cpp:251, snapshot.cpp:175,
           dir.cpp:278). TTY test: 3 sites (options.cpp:266, compare.cpp:59, dir.cpp:74).

    (C) options.cpp:258-261: "The one place mediadiff calls isatty (POSIX) / _isatty (MSVC) --
        neither name is permitted outside this function"
        -> FALSE. compare.cpp:59 and dir.cpp:74 both call isatty(fileno(stdout)).

    (D) compare.cpp:41-43: "The one place a terminal width is ever queried for this run"
        -> FALSE (dir.cpp:60 query_terminal_size()), but dir.cpp:47-50 openly documents itself as
           "the second, independent terminal-query site", so this is a stale un-updated comment
           rather than an unnoticed violation. Lowest severity of the four.
  implication: |
    The convention that would have prevented this bug was asserted in comments and violated in code.
    Note the causal asymmetry: the isatty violations (C) are compile-INVISIBLE on MSVC because they
    sit inside `#else // !_WIN32` branches, while the getenv violations are compile-VISIBLE because
    getenv is called unconditionally on every platform. Same broken convention, only one of the two
    breaks the build — which is why the drift went unnoticed. Any fix that consolidates getenv
    without also correcting (A)/(B)/(C) leaves the comments lying about the new state too.

- timestamp: 2026-08-16
  checked: "scripts/lint_eng16.sh SCAN_DIRS and PATTERN — the project's only source-convention lint"
  found: |
    SCAN_DIRS = (src/core src/config src/probe src/analyzers src/compare src/report src/util).
    Header comment: "All rendering and process control live in the CLI target's own directory,
    which this lint deliberately does not scan."
    PATTERN matches printf/puts/putchar/std::cout/std::cerr/std::clog/stdout/stderr/exit(/_exit(/abort(
  implication: |
    WHY THIS WAS NOT CAUGHT: no gate covers it. (1) src/cli — where all three production getenv sites
    live — is deliberately outside the only convention lint's scan scope. (2) That lint's pattern is
    about streams and process exit, not CRT-deprecated calls. (3) GCC/Clang have no C4996 analogue,
    so the whole class is invisible to every non-Windows leg and to local Linux development.
    (4) The x64-windows-static-md leg is the sole detector, and it only reports the FIRST site.

- timestamp: 2026-08-16
  checked: "Microsoft Learn — _dupenv_s/_wdupenv_s and getenv_s/_wgetenv_s reference pages"
  found: |
    _dupenv_s: "If the variable isn't found, then buffer is set to NULL, numberOfElements is set to 0,
      and the return value is 0 because this situation isn't considered to be an error condition."
      Allocates via malloc on success; CALLER MUST free().
      -> unset  => buffer == NULL, retval 0
      -> empty  => buffer != NULL pointing at "", numberOfElements == 1
    getenv_s: "pReturnValue — Returns the buffer size that's required, or 0 if the variable isn't found."
      Two-call probe/read pattern; the size-probe call (buffer=NULL, numberOfElements=0) returns
      ERANGE, which the caller must treat as success-with-size, not failure.
      -> unset  => *pReturnValue == 0
      -> empty  => *pReturnValue == 1 (the terminating null alone)
  implication: |
    BOTH preserve the unset-vs-empty distinction that ColorInputs's std::optional<std::string> shape
    and NO_COLOR's presence-is-the-signal convention (color_policy.h:17-22, WR-02 regression tests)
    depend on. _dupenv_s is preferable on three grounds: it is what the compiler diagnostic itself
    names; it is single-call (no ERANGE-is-not-an-error trap); and the unset case is a plain
    `buffer == nullptr` test that maps one-to-one onto the existing `value == nullptr` shape of all
    six call sites — a mechanical, behaviour-preserving substitution. Its one cost, the mandatory
    free(), is contained if the shim copies into std::string and frees before returning.
    INFERENCE, NOT VERIFIED: I could not execute MSVC. This rests on the Learn documentation and
    the compiler's own suggestion text in the CI log.

- timestamp: 2026-08-16
  checked: "app.manifest and its CMake attachment (CMakeLists.txt:255-264)"
  found: |
    app.manifest sets <activeCodePage>UTF-8</activeCodePage>, and CMakeLists.txt:260 attaches it via
    `if(WIN32) target_sources(mediadiff PRIVATE app.manifest)` — to the mediadiff EXECUTABLE ONLY.
    Neither test executable gets the manifest.
  implication: |
    SECONDARY CONSIDERATION for the planner, not a cause of this failure. A narrow `_dupenv_s` reads
    the CRT's ANSI-code-page view of the environment. Inside `mediadiff` the manifest makes that
    UTF-8; inside the two test executables it is the machine's ambient ANSI code page. All six
    variables read here (NO_COLOR, CI, GITHUB_ACTIONS, MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR,
    UPDATE_GOLDENS, PATH) are ASCII in every practical case, so narrow is adequate. The only
    non-ASCII exposure is PATH on a localized machine in test_exit_codes.cpp:137 /
    test_snapshot_safe_write.cpp:136, which merely round-trip it to a child process. If the planner
    wants encoding-exactness rather than adequacy, the wide form (_wdupenv_s + fs.h's existing
    wide_to_utf8) is available — but ONLY if the shim lives in src/util/fs.h, per fs.h:7-9.

## Resolution

root_cause: |
  Two conditions hold simultaneously (AND-gate):
  (1) CODE — Phase 2 introduced six unguarded `std::getenv` call sites (src/cli/options.cpp:251,
      src/cli/commands/snapshot.cpp:175, src/cli/commands/dir.cpp:278, tests/support/golden.cpp:23,
      tests/integration/test_exit_codes.cpp:137, tests/integration/test_snapshot_safe_write.cpp:136),
      none behind a _WIN32 guard. MSVC's CRT deprecates `getenv` and emits C4996 at each.
  (2) BUILD CONFIG — `mediadiff_apply_warnings()` applies `/W4 /WX` to all four first-party targets
      (libmediadiff, mediadiff, mediadiff_unit_tests, mediadiff_integration_tests), and the CI Build
      step runs `cmake --build --preset x64-windows-static-md` with no --target, so all four are in
      scope. `/WX` escalates each C4996 to C2220, a hard error.
  Neither condition alone fails the build; (2) is deliberate, correct project policy and must not be
  relaxed, so the actionable cause is (1).
  (3) CONTRIBUTING — no gate confines the platform primitive. The "one place calls getenv" rule was
      asserted only in header comments (options.h:149, color_policy.h:9), those comments are already
      FALSE, scripts/lint_eng16.sh deliberately excludes src/cli from its scan, and GCC/Clang have no
      C4996 analogue — so the class is undetectable outside the single Windows CI leg, which reports
      only its first occurrence. This is why the violation recurred six times rather than once.
fix: "(not applied — goal: find_root_cause_only)"
verification: "(not applied)"
files_changed: []
