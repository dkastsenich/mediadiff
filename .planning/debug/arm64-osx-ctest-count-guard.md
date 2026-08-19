---
status: diagnosed
trigger: "G-02-2 — arm64-osx CI leg aborts in its own ctest test-count sanity guard before running any test. Error: could not parse a test count from 'ctest -N' in build/arm64-osx — refusing to assume the suite is healthy."
created: 2026-08-16T00:00:00Z
updated: 2026-08-16T00:00:00Z
mode: find_root_cause_only
bug_class: Bohrbug (deterministic — reproduces on every arm64-osx run; platform-conditional, not transient)
---

## Current Focus

hypothesis: CONFIRMED — `\+` in the sed BRE at ci.yml:246 is a GNU-only extension. On the
  macOS runner `/usr/bin/sed` is BSD sed, which parses `\+` as a literal `+`, so the pattern
  can never match CTest's `Total Tests: 296` line. `TOTAL` is empty and the `-z` guard fires.
test: complete — see Evidence E-01..E-12
expecting: n/a (diagnosis complete)
next_action: return ROOT CAUSE FOUND to orchestrator; gsd-planner authors the fix

reasoning_checkpoint:
  hypothesis: "ci.yml:246's `\\([0-9]\\+\\)` relies on the GNU-only `\\+` BRE quantifier; BSD/macOS sed reads `\\+` as a literal `+`, so no line matches, TOTAL is empty, and the `[ -z \"$TOTAL\" ]` guard aborts the step."
  confirming_evidence:
    - "E-05: of the 5 matrix legs, only x64-linux and arm64-osx reached the Test step. Same script, same ctest output shape. GNU-sed leg printed 'ctest discovered 296 tests'; BSD-sed leg produced an empty capture."
    - "E-06: no ctest stderr appears in the arm64-osx Test step log — ctest itself did not fail, so the missing match is a regex problem, not a ctest problem."
    - "E-11: glibc POSIX-minimal-BRE emulation reproduces the mechanism exactly — with `\\+` demoted from operator, `^T: \\([0-9]\\+\\)$` stops matching 'T: 296' and starts matching 'T: 2+'."
    - "E-12: arm64-osx PASSED this step in run 31849289102 (pre-e684579), when the pattern was the extension-free `grep -c '^  Test #'`."
  falsification_test: "If the Windows leg (Git Bash, GNU sed) or the Linux leg had failed this step with the same message, or if arm64-osx's log showed ctest stderr, the hypothesis would be dead. Neither holds."
  fix_rationale: "Replacing `\\+` with the POSIX-BRE-portable `[0-9][0-9]*` (or switching to `sed -E`) makes the pattern match under both regex dialects, addressing the portability defect itself rather than loosening or deleting the guard."
  blind_spots:
    - "I could not execute BSD sed in this Linux sandbox. The BSD half rests on documented behaviour plus a glibc POSIX-minimal emulation, NOT on a direct BSD sed run."
    - "The full 296-test suite has never executed on macOS (pre-regression it ran 11). Fixing the parser unblocks the step but may expose genuine macOS test failures behind it."
  candidate_causes:
    - "code: GNU-only `\\+` BRE extension in a script that runs on both GNU and BSD sed — CONFIRMED"
    - "environment: macOS-15 runner ships BSD sed as /usr/bin/sed — CONFIRMED as the co-condition"
    - "config: TEST_DIR not matching the preset's binaryDir — RULED OUT (E-02, E-06)"
    - "data: ctest not emitting a `Total Tests:` line / no tests built — RULED OUT (E-03, E-04, E-07)"
  and_gate: "YES — this failure requires TWO conditions simultaneously: (1) the pattern uses the GNU-only `\\+`, AND (2) the leg runs on a host whose sed is BSD. Neither alone fails, which is exactly why only the macOS leg trips it while Linux and Git-Bash-Windows do not. The actionable fix is nonetheless single-sited (condition 1), since condition 2 is fixed by the platform matrix."

## Symptoms

expected: The blocking `arm64-osx` CI leg runs its test suite instead of aborting in its own test-count sanity guard.
actual: The Test step fails before running any test.
errors: |
  ##[error]could not parse a test count from 'ctest -N' in build/arm64-osx — refusing to assume the suite is healthy.
  ##[error]Process completed with exit code 1.
reproduction: CI run 31937349647 job 95141247762 (branch gsd/phase-2-core-engine) and run 31881353367 job 95004285871 (main) — identical symptom on both.
started: Introduced by commit e684579 (2026-08-15 01:18:49). Pre-existing on main; surfaced during Phase 2 UAT. NOT a Phase 2 code regression, but IS a CI-workflow regression.

## Eliminated

- hypothesis: "$TEST_DIR is wrong for the arm64-osx preset"
  evidence: "CMakePresets.json 'base' sets binaryDir=${sourceDir}/build/${presetName}, and arm64-osx inherits it → build/arm64-osx. The workflow computes TEST_DIR=build/${{ matrix.preset }} = build/arm64-osx. Identical. Independently corroborated by E-06: a bad --test-dir makes ctest print 'Failed to change working directory to ...' on stderr, and stderr is NOT captured by the pipe, so it would appear in the job log. It does not appear."
  timestamp: 2026-08-16

- hypothesis: "ctest -N does not print a 'Total Tests: N' line in the CTest version in play"
  evidence: "E-03: real ctest 3.28.3 output ends with 'Total Tests: 296$' — no leading whitespace, no trailing whitespace (verified via cat -A). E-05: the x64-linux leg parsed 296 out of that exact line using the identical script in the same CI run."
  timestamp: 2026-08-16

- hypothesis: "No test binaries were built on the arm64-osx leg, so the suite is genuinely empty"
  evidence: "E-07: the arm64-osx Build step log shows [92/97] Linking CXX executable tests/unit/mediadiff_unit_tests and [96/97] Linking CXX executable tests/integration/mediadiff_integration_tests. Both test binaries built and linked. Additionally, an empty suite would print 'Total Tests: 0', which under GNU sed yields '0' (non-empty) and would trip the SECOND guard with a different message — not the observed one."
  timestamp: 2026-08-16

- hypothesis: "Some other leg fails the same way, so this is not macOS-specific"
  evidence: "E-08: of the four failing legs in run 31937349647, x64-windows-static-md failed in Build (exit 2), arm64-linux failed in 'Register vcpkg NuGet feed' (exit 1), x64-osx failed in Build (exit 1) — none of the three reached the Test step at all (zero Test-step log lines). Only arm64-osx failed IN the Test step."
  timestamp: 2026-08-16

- hypothesis: "The Windows leg would also trip this (no BSD sed involved, so it must be something else)"
  evidence: "E-09: the workflow sets `defaults: run: shell: bash`, so the Windows leg runs Git Bash, which ships GNU sed. Windows is therefore not exposed to this defect. Consistent with the observation that Windows failed at Build, not Test."
  timestamp: 2026-08-16

## Evidence

- id: E-01
  checked: ".github/workflows/ci.yml:225-256 (Test step) verbatim"
  found: "TOTAL=$(ctest --test-dir \"$TEST_DIR\" -N | sed -n 's/^Total Tests: \\([0-9]\\+\\)$/\\1/p' || true) followed by an `[ -z \"$TOTAL\" ]` abort and an `[ \"$TOTAL\" -eq 0 ]` abort."
  implication: "Empty capture and zero-count are distinct branches; the observed error is the EMPTY branch, meaning the regex matched nothing at all."

- id: E-02
  checked: "CMakePresets.json"
  found: "'base' hidden preset sets binaryDir=${sourceDir}/build/${presetName}; arm64-osx inherits base. testPresets exist for the 3 blocking triplets. Workflow's TEST_DIR=build/arm64-osx matches exactly."
  implication: "TEST_DIR is correct. Path hypothesis eliminated."

- id: E-03
  checked: "Real `ctest --test-dir build/x64-linux -N` output locally (ctest 3.28.3), inspected with cat -A"
  found: "Final line is exactly `Total Tests: 296$` — column 1, single space after colon, no trailing whitespace. Applying the CI pattern under GNU sed 4.9 to this real output yields `296`."
  implication: "CTest's output format is exactly what the pattern assumes. The anchor `^Total Tests: ` and the `$` terminator are both correct. Format hypothesis eliminated."

- id: E-04
  checked: "Timing: `time ctest --test-dir build/x64-linux -N` over 296 tests"
  found: "0.016s real."
  implication: "The arm64-osx Test step's 110 ms wall time (shell start 08:49:38.129 → error 08:49:38.230) is entirely consistent with ctest running to completion normally. Not a timeout or hang."

- id: E-05
  checked: "Test-step output of the two legs that reached it in run 31937349647"
  found: "x64-linux (job 95141247761, SUCCESS): `ctest discovered 296 tests in build/x64-linux.` then the suite ran. arm64-osx (job 95141247762, FAILURE): the `-z` diagnostic, no count."
  implication: "DIFFERENTIAL: identical script text, identical CTest output shape, identical TEST_DIR construction — the only varying factor between pass and fail is the host's sed implementation (GNU on ubuntu-24.04, BSD on macos-15)."

- id: E-06
  checked: "arm64-osx Test step log for any ctest stderr; and locally, what ctest writes on a bad --test-dir"
  found: "Locally a bad --test-dir prints `Failed to change working directory to \"...\" : No such file or directory` to STDERR. The arm64-osx job log shows NO such line — only the shell banner, then the `::error::` diagnostic. Since only ctest's stdout is piped into sed, any ctest stderr would have surfaced in the log."
  implication: "ctest ran successfully on the runner and wrote a normal listing to stdout. The failure is downstream of ctest, in the sed match. This is the single strongest discriminator against every non-regex explanation."

- id: E-07
  checked: "arm64-osx Build step log for test targets"
  found: "`[92/97] Linking CXX executable tests/unit/mediadiff_unit_tests` and `[96/97] Linking CXX executable tests/integration/mediadiff_integration_tests`, plus ~30 integration test object files."
  implication: "Tests were built on the macOS leg. A genuinely empty suite is ruled out."

- id: E-08
  checked: "The other three failing legs in run 31937349647"
  found: "x64-windows-static-md → failed in Build (exit 2); arm64-linux → failed in 'Register vcpkg NuGet feed (read-write, trusted runs only)' (exit 1); x64-osx → failed in Build (exit 1). All three have ZERO Test-step log lines."
  implication: "No other leg reached the guard, so no other leg can confirm or refute it. arm64-osx is the sole leg exhibiting this failure — consistent with it being the only BSD-sed host that gets that far. (x64-osx is also macOS/BSD sed but dies earlier at Build, matching the gap brief's out-of-scope note.)"

- id: E-09
  checked: ".github/workflows/ci.yml:17-19 and the arm64-osx step's shell banner"
  found: "`defaults: run: shell: bash`. Runner banner: `shell: /bin/bash --noprofile --norc -e -o pipefail {0}`."
  implication: "Two consequences. (a) Windows runs Git Bash → GNU sed → immune to this defect. (b) `-o pipefail` is confirmed active, so the `|| true` in the comment is genuinely load-bearing — and it is also what converts the no-match into a silent empty string rather than a pipeline failure."

- id: E-10
  checked: "GNU sed 4.9 behaviour of the CI pattern, including a literal-plus control"
  found: "`printf 'Total Tests: 11\\nTotal Tests: 1+\\n' | sed -n 's/^Total Tests: \\([0-9]\\+\\)$/\\1/p'` prints only `11`, never `1`."
  implication: "Under GNU sed, `\\+` is unambiguously a one-or-more quantifier and does NOT match a literal `+`. Confirms the GNU half of the hypothesis by direct execution."

- id: E-11
  checked: "POSIX-minimal-BRE emulation via glibc re_compile_pattern with RE_SYNTAX_POSIX_MINIMAL_BASIC (which clears RE_BK_PLUS_QM, i.e. removes the '\\+ is an operator' extension). EMULATION, not BSD sed."
  found: |
    GNU BRE (\+ = op)      ^T: \([0-9]\+\)$     vs 'T: 296'  -> MATCH
    POSIX-min (\+ literal) ^T: \([0-9]\+\)$     vs 'T: 296'  -> no match
    POSIX-min (\+ literal) ^T: \([0-9]\+\)$     vs 'T: 2+'   -> MATCH
    POSIX-min              ^T: \([0-9][0-9]*\)$ vs 'T: 296'  -> MATCH
    POSIX-min              ^T: \([0-9][0-9]*\)$ vs 'T: 0'    -> MATCH
  implication: "When the `\\+` extension is removed from the BRE dialect, the pattern stops matching a normal count line and instead matches a digit followed by a literal '+' — precisely the predicted failure mode, and precisely the observed empty capture. The portable `[0-9][0-9]*` form matches under the minimal dialect for both a normal count and zero."

- id: E-12
  checked: "git log -p of the Test step, and CI history across the change"
  found: "Before e684579 (2026-08-15 01:18) the line was `TOTAL=$(ctest --test-dir \"$TEST_DIR\" -N | grep -c '^  Test #' || true)` — an extension-free BRE. e684579 introduced BOTH the sed `\\+` pattern AND the `[ -z \"$TOTAL\" ]` branch whose message is the observed error. In run 31849289102 (commit f90fbdc6, which `git merge-base --is-ancestor` confirms predates e684579), `build (arm64-osx)` SUCCEEDED, its Test step printing `ctest discovered 2 tests in build/arm64-osx.` and then running the 11-test suite."
  implication: |
    Two conclusions.
    (1) The macOS leg DID pass this step before — this is a REGRESSION introduced by e684579, not a
        never-worked condition. The fix is therefore 'restore portability', not 'make macOS work for
        the first time'.
    (2) `grep -c` also structurally cannot produce an empty string (it always prints a number), so the
        `-z` branch was unreachable before e684579. The new branch and the new non-portable pattern
        arrived in the same commit, which is why the defect presents as a hard abort rather than a
        miscount. Note e684579's own comment claims verification 'under bash --noprofile --norc -e -o
        pipefail' — a GNU-sed verification only; the change was never exercised against BSD sed.

- id: E-13
  checked: "Whole-workflow scan for other GNU-only regex constructs that execute on macOS legs"
  found: "ci.yml:246 is the ONLY occurrence of `\\+`/`\\?`/`\\|` in any sed/grep in .github/workflows/. The only other match is ci.yml:211 `grep -iE \"restored|installing|building\"`, which uses -E (ERE) and is portable to BSD grep."
  implication: "The portability defect is single-sited. Fixing line 246 does not leave a sibling BSD landmine in the same workflow."

- id: E-14
  checked: "Portable replacement validated against real ctest output and the zero case (GNU sed + POSIX-minimal emulation)"
  found: "`sed -n 's/^Total Tests: \\([0-9][0-9]*\\)$/\\1/p'` yields `296` against real ctest output, and yields `0` against `Total Tests: 0`. E-11 shows the same form matches under the extension-free dialect."
  implication: "The portable form preserves the guard's stated intent — a zero-test suite still produces TOTAL=0, which is non-empty, so control reaches the `-eq 0` branch and the false-pass is still caught with its correct, specific message."

## Resolution

root_cause: |
  Two conditions had to hold simultaneously (AND-gate):

  (1) CODE — .github/workflows/ci.yml:246 parses CTest's count with
      `sed -n 's/^Total Tests: \([0-9]\+\)$/\1/p'`. The `\+` quantifier is a GNU
      extension, not POSIX BRE. POSIX leaves a backslash before an ordinary
      character undefined, so it is not portable by specification.

  (2) ENVIRONMENT — the `arm64-osx` leg runs on `macos-15`, whose `/usr/bin/sed`
      is BSD sed. BSD sed implements POSIX BRE without the GNU extension and, per
      its documented/observed behaviour, treats `\+` as a LITERAL `+`. The pattern
      therefore demands a line of the form `Total Tests: <digit>+` and can never
      match the real `Total Tests: 296`.

  With no match, `sed -n` prints nothing and exits 0, so `|| true` and `pipefail`
  never intervene: `TOTAL` becomes the empty string, `[ -z "$TOTAL" ]` is true, and
  the step aborts with "could not parse a test count" BEFORE any test executes.

  Neither condition alone fails — which is exactly why the GNU-sed legs (x64-linux,
  and Windows via Git Bash) pass the identical script while only the macOS leg trips.

  Introduced by commit e684579, which replaced the extension-free
  `grep -c '^  Test #'` with this sed pattern and simultaneously added the `-z`
  branch that now fires. The arm64-osx leg passed this step before that commit
  (run 31849289102), so this is a regression, not a never-worked condition.

fix: NOT APPLIED — diagnose-only mode (goal: find_root_cause_only). gsd-planner owns the fix.

verification: n/a — no fix applied this session.

files_changed: []

suggested_fix_direction: |
  Make the pattern dialect-independent at ci.yml:246. Preferred, most conservative:

    TOTAL=$(ctest --test-dir "$TEST_DIR" -N | sed -n 's/^Total Tests: \([0-9][0-9]*\)$/\1/p' || true)

  `[0-9][0-9]*` is pure POSIX BRE — identical semantics under GNU and BSD sed, and no
  dependency on any flag being supported. `sed -E ... ([0-9]+)` also works on both, but
  adds a flag-support assumption for no benefit.

  Guard intent is preserved: `Total Tests: 0` still captures "0", which is non-empty, so
  the `-z` branch does not fire and control reaches `[ "$TOTAL" -eq 0 ]`, which still
  catches the zero-test false pass with its own specific message (verified E-14/E-11).

  Two things worth folding into the fix plan:
  - The step's inline comment should note that this pipeline runs under BSD sed on the
    macOS legs, so GNU-only regex extensions must never be reintroduced. e684579's own
    comment documents a GNU-sed-only verification, which is how the defect shipped.
  - Fixing this UNBLOCKS the step but does not guarantee a green leg: the 296-test suite
    has never run on macOS (the last successful macOS run executed 11 tests). Expect the
    fix plan to need a follow-up pass for any genuine macOS test failures revealed behind
    the guard.
