---
phase: 02-core-engine
verified: 2026-08-18T15:44:33Z
status: passed
score: 22/22 must-have truths verified (17 carried/regression-checked from prior rounds + 5 round-4 closure truths); 0 behavior_unverified
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "14/17 truths verified directly (round-3 report); 3 open blockers G-02-5, G-02-6, G-02-7"
  gaps_closed:
    - truth: "G-02-5 — mediadiff's byte-level output is identical on Windows and POSIX (goldens match; captured subprocess bytes are exactly what the child wrote)"
      evidence: |
        CI run 32153890395 (headSha d7274258ad73b61a4927721f0020e66099e65912), pulled fresh
        via `gh run view --job 95766278227 --log` in THIS session (not transcribed from any
        SUMMARY): grep over the 2137-line Windows job log returns 0 for `C4996`, `C4702`,
        `C2220`, `golden mismatch`, `85000`, and `FAILED`. All four affected tests confirmed
        Passed by exact name: #67 `unit.junit - golden`, #75 `unit.markdown_budget - golden`,
        #201 `unit.tty - golden`, #95 `unit.process_spawn`. The new
        `integration.json_schema - TRUST-05: --json to stdout and --json=<path> produce
        identical bytes` (#262) also Passed on x64-linux, arm64-osx AND
        x64-windows-static-md — independently confirmed by reading
        tests/integration/test_json_schema.cpp directly: it reads the file side in binary via
        fopen_utf8(...,"rb"), asserts stdout carries no 0x0D, and compares full byte equality.
        Root cause verified in source: src/cli/main.cpp's wmain sets
        `_setmode(_fileno(stdout), _O_BINARY)` and the matching stderr line as literally the
        first statements of wmain, before enable_vt_output() and before any output — read
        directly at lines 195, 233-234, 241 of src/cli/main.cpp. .gitattributes (`* text=auto
        eol=lf`) confirmed present and confirmed a no-op on existing content in this session
        (`git add --renormalize .` stages nothing but a pre-existing, unrelated UAT.md edit).
    - truth: "G-02-6 — a non-ASCII filename round-trips through mediadiff's path handling unchanged on Windows"
      evidence: |
        CI run 32153890395, job 95766278227: test #36
        `unit.dir_pairing: a tree containing a non-ASCII filename pairs correctly` confirmed
        Passed by exact name in the raw log pulled this session; `grep -c 'sumÃ'` returns 0.
        tests/support/utf8_path.h read directly: `path_from_utf8` routes through
        `mediadiff::utf8_to_wide` on Windows (never std::filesystem::path's narrow
        constructor, never the deprecated u8path), byte-verbatim elsewhere — mirrors
        src/cli/dir_pairing.cpp's own two-branch conversion exactly, confirmed by reading both
        files. tests/unit/test_dir_pairing.cpp's literal
        `"r\xC3\xA9sum\xC3\xA9.snap.json"` and its `relative_path == non_ascii_name` assertion
        confirmed unchanged. src/cli/dir_pairing.cpp confirmed untouched across the entire
        round-4 commit range (4f17a19^..f11f807).
    - truth: "G-02-7 — mediadiff's documented exit-code contract and inspect output hold identically on Windows"
      evidence: |
        CI run 32153890395, job 95766278227: test #236
        `integration.exit_codes - 65: a nonexistent input path exits exactly 65` and test #247
        `integration.explain_inspect - REPORT-07: inspect on a snapshot with no meta
        measurements...` both confirmed Passed by exact name; `grep -c '64 == 65'` over the
        raw log returns 0. src/cli/main.cpp confirmed to set
        `app.allow_windows_style_options(false)` immediately after
        `allow_subcommand_prefix_matching(false)`, above every `register_*_command` call (line
        82, before line 92 — confirmed by direct read). src/cli/exit_code.cpp/.h confirmed
        byte-for-byte untouched across the whole round-4 range: `input_open` maps to 65 and
        `usage` to 64 unconditionally, no platform branch, no default: arm — read directly.
        tests/integration/test_exit_codes.cpp's `== 65` assertion confirmed unchanged
        (`/no/such/file.snap.json` unchanged); two new stderr assertions added and confirmed
        present. #247's file (tests/integration/test_explain_inspect.cpp) confirmed untouched
        across the entire round-4 range — its pass is the designed experiment proving it was
        downstream of G-02-5's stdout fix, not its own defect, exactly as G-02-7's `missing:`
        bullet asked to be established.
  gaps_remaining: []
  regressions: []
  gap_closure_caused_regressions: false
deferred:
  - item: "UAT test 2 — colour renders as real styling in a Windows console"
    status: deferred_to_phase_3
    decided_by: human
    decided_at: 2026-08-18
    evidence: "02-UAT.md round_4_verdict.uat_test_2 frontmatter: 'DEFERRED TO PHASE 3 (human decision). Windows console colour rendering needs a human at real Windows hardware; a usable artifact exists (mediadiff-windows-x64, 1301968 bytes, sha d727425) but the check does not block Phase 2.' Artifact existence and exact byte size independently confirmed this session via `gh api repos/dkastsenich/mediadiff/actions/runs/32153890395/artifacts` -> {\"name\":\"mediadiff-windows-x64\",\"size_in_bytes\":1301968}. This is a human decision, not a re-opened blocker: the round-4 human checkpoint (02-19-PLAN.md Task 3) explicitly deferred it. Not counted as an open human-verification item in this report."
  - item: "CI-x64-osx"
    status: deferred_non_blocking
    evidence: "Job 95766278109, run 32153890395, independently re-pulled this session: 'ld: warning: ignoring file vcpkg_installed/x64-osx/lib/libavformat.a: fat file missing arch arm64, file has x86_64' -- identical signature to the recorded 02-UAT.md entry. Not a required merge context. Root cause: x64-osx preset cross-builds from an arm64 host without -arch x86_64."
  - item: "CI-arm64-linux"
    status: deferred_non_blocking
    evidence: "Job 95766278139, run 32153890395, independently re-pulled this session: fails at 'Register vcpkg NuGet feed (read-write, trusted runs only)' -- identical signature to the recorded 02-UAT.md entry. Not a required merge context. Root cause undetermined (mono/NuGet availability), documented as such."
  - item: "Deferred Follow-Up: getenv_utf8 thread-safety contract undocumented"
    status: deferred_non_blocking
    evidence: "02-UAT.md Deferred Follow-Ups, deferred 2026-08-16. Not exploitable in current code (all six call sites execute before any thread spawns); a documentation/robustness improvement, untouched by round 4."
  - item: "Deferred Follow-Up: dead <cstdlib> include in src/cli/options.cpp"
    status: deferred_non_blocking
    evidence: "02-UAT.md Deferred Follow-Ups, deferred 2026-08-16. Cosmetic; untouched by round 4."
  - item: "Deferred Follow-Up: severity colour applies only to the status glyph, not the summary line"
    status: deferred_non_blocking
    evidence: "02-UAT.md Deferred Follow-Ups, deferred 2026-08-16. Scope ambiguity in the spec (CLI-08/REPORT-02 never specify what gets coloured), not a defect. Untouched by round 4."
  - item: "Deferred Follow-Up: no test exercises a non-ASCII path end-to-end through the built mediadiff binary"
    status: deferred_non_blocking
    evidence: "02-18-SUMMARY.md's Task 2 Part 3 audit: test_fs_utf8.cpp and test_dir_pairing.cpp both cover non-ASCII handling in-process, but neither spawns mediadiff.exe with a non-ASCII argument -- the CLI-09 path CLI-09 actually promises. Recorded as a deferred follow-up per the plan's explicit instruction, not added in round 4 (would have grown the suite total 02-17's count criteria were written against)."
human_decisions:
  - decision: "Round-4 certify-or-iterate checkpoint (02-19-PLAN.md Task 3)"
    decided_by: human
    decided_at: 2026-08-18
    outcome: "Certified. All four required merge contexts green simultaneously (first time in project history). G-02-5, G-02-6, G-02-7 all closed. #247 confirmed downstream of G-02-5's stdout fix (its file never modified). The stdout/file byte-parity human-verification item from the prior VERIFICATION.md closed by the new automated TRUST-05 test rather than requiring manual confirmation. UAT test 2 deferred to Phase 3. CI-x64-osx, CI-arm64-linux and the three Deferred Follow-Ups re-confirmed deferred and non-blocking."
    source: "02-UAT.md frontmatter round_4_verdict block, status: resolved"
---

# Phase 2: Core Engine Verification Report (Round-4 Sealing Report — Supersedes Prior `gaps_found` Report)

**Phase Goal:** The complete compare engine — registry, comparison semantics, policy resolution, snapshots, all four report formats and `dir` orchestration — works end to end against stub measurements, so every analyzer that follows plugs into finished machinery.

**Verified:** 2026-08-18T15:44:33Z
**Status:** passed
**Re-verification:** Yes — round 4, sealing report, superseding the prior `02-VERIFICATION.md` (`status: gaps_found`, `gaps_remaining: [G-02-5, G-02-6, G-02-7]`)

## Why this report supersedes the prior one, and what changed the verdict

The prior report's three remaining gaps (G-02-5 byte-level output divergence, G-02-6 non-ASCII path mis-decoding, G-02-7 exit-code/inspect divergence) were all Windows-only failures discovered when the Windows test suite executed for the first time in the project's history (CI run 31946964023). Three plans closed them:

- **02-17**: `.gitattributes` (`* text=auto eol=lf`) pinning checkout to LF on every platform, plus `_setmode(_fileno(stdout/stderr), _O_BINARY)` in `wmain` before any output — landed in one commit because either half alone converts the failures into a different set rather than fixing them. New `TRUST-05` destination-parity test.
- **02-18**: the process-spawn test fixture's Python child now writes through `sys.stdout.buffer`/`sys.stderr.buffer` (a test-fixture defect, not a product defect); a new `tests/support/utf8_path.h` helper constructs non-ASCII test paths the same way the product does.
- **02-19**: `app.allow_windows_style_options(false)` on the root `CLI::App`, above every subcommand registration, so a `/`-rooted argument is a path on Windows exactly as it is everywhere else — the actual cause of the `64 vs 65` divergence (CLI11 2.6.2 defaults this to `true` under `_WIN32`).

All three landed, were pushed together, and **CI run `32153890395` (headSha `d7274258ad73b61a4927721f0020e66099e65912`) is the first run in this project's history where all four required merge contexts are green simultaneously**, including a Windows Test step at `0 tests failed out of 298`. A human reviewed that run on 2026-08-18 and certified the round (`02-UAT.md` `round_4_verdict`, `status: resolved`).

**This verification independently re-derived every load-bearing claim rather than trusting `02-UAT.md`, the three SUMMARYs, or the prior `02-VERIFICATION.md`.** Concretely, in this session:

- Read `.gitattributes`, `src/cli/main.cpp` (the full `_setmode` placement and the `allow_windows_style_options(false)` placement), `tests/support/utf8_path.h`, `tests/unit/test_process_spawn.cpp`, `tests/unit/test_dir_pairing.cpp`, `tests/integration/test_json_schema.cpp`, `tests/integration/test_exit_codes.cpp`, and `src/cli/exit_code.cpp` directly — not the SUMMARYs' descriptions of them.
- Ran `git diff --stat` across the entire round-4 commit range (`4f17a19^..f11f807`) against every prohibited file: `src/cli/exit_code.cpp`/`.h`, `tests/integration/test_explain_inspect.cpp`, `tests/support/golden.cpp`, `tests/process_spawn.h`, `src/cli/dir_pairing.cpp` — all confirmed empty (untouched).
- Ran `git add --renormalize .` myself and confirmed it stages nothing but a pre-existing, unrelated `02-UAT.md` edit already in the working tree — `.gitattributes` is a genuine no-op on existing content, confirmed directly rather than trusted from the plan's own claim.
- Grepped for `UPDATE_GOLDENS`, `pragma warning(suppress`, `SKIP(`, and debt markers (`TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER`) across every round-4-touched file — all zero.
- **Pulled CI run `32153890395`'s raw job logs myself via `gh run view --job <id> --log`**, independent of `02-19-SUMMARY.md`'s transcription, for the Windows job, the x64-linux job, the arm64-osx job, the lint job, and both non-required legs — confirmed `headSha` string-equality against local `HEAD` via `gh run view 32153890395 --json headSha`.
- Confirmed the `mediadiff-windows-x64` artifact's existence and exact byte size (`1301968`) via the GitHub Actions API directly, not from the SUMMARY.
- Rebuilt the changed translation units from a touched (non-cached) state and ran the full local suite myself: `298/298` passed, `0` failed, `1` skipped, clean under `-Wall -Wextra -Werror` (the rebuild would have failed outright under `-Werror` had there been any warning).
- Independently re-confirmed all 48 Phase-2 requirement IDs are `Complete` in `REQUIREMENTS.md`, and specifically confirmed `CLI-05`/`CLI-09` are Phase-1-owned (not part of Phase 2's 48) by reading their rows directly — resolving the discrepancy between "CLI-01…10" (10 IDs) and the true Phase-2 CLI set (8 IDs, excluding 05 and 09).

Every claim below is labeled by its evidence source: **[LOCAL]** = observed directly on this Linux host in this session; **[CI-32153890395]** = pulled from that specific run's logs/artifacts in this session; **[CARRIED]** = a regression-checked claim from the round-3 verification, re-confirmed as untouched by round 4 rather than re-derived from zero.

## Goal Achievement

### Observable Truths

#### Carried forward from round 3 (regression-checked against the round-4 diff, not re-derived)

| # | Truth | Status | Regression check |
|---|-------|--------|-------------------|
| 1 | Snapshot/compare round-trip, schema_version gate, CI tracked-overwrite gate | ✓ VERIFIED [CARRIED] | `git diff --stat 4f17a19^..f11f807 -- src/core/snapshot.cpp` [LOCAL] confirmed empty except the deliberately-untouched `"wb"` call site was read, not modified. `ctest --test-dir build/x64-linux` [LOCAL]: 298/298 passed. |
| 2 | Policy resolution (profiles, TOML, `--set`/`--tol`, provenance) | ✓ VERIFIED [CARRIED] | No round-4 plan's `files_modified` touches `src/config` or `policy.cpp`; confirmed by the full round-4 file list (`.gitattributes`, `src/cli/main.cpp`, three test files, one new test header, three SUMMARYs, ROADMAP.md). |
| 3a | Four report formats render correctly (logic) | ✓ VERIFIED [CARRIED] | Golden-comparison unit suites (`test_junit.cpp`, `test_markdown_budget.cpp`, `test_tty_render.cpp`) untouched by round 4; now additionally proven byte-identical on Windows for the first time (see truth 6 below). |
| 3b | Colour output renders as real styling in a real Windows console | ⚠️ still human-only, now explicitly DEFERRED TO PHASE 3 | Human decision 2026-08-18, `02-UAT.md round_4_verdict.uat_test_2`, independently confirmed present in the frontmatter and the artifact it depends on (`mediadiff-windows-x64`, 1,301,968 bytes) confirmed to exist [CI-32153890395]. See Deferred Items — not a gap, not counted against this report's score. |
| 4 | `dir` pairing, threads, exit-code contract, `ENG-16` process-control confinement | ✓ VERIFIED [CARRIED] on Linux/macOS; now ALSO ✓ VERIFIED on Windows | `src/cli/dir_pairing.cpp` confirmed untouched across the whole round-4 range [LOCAL]. The Windows exit-code contract gap (G-02-7, test #236) that downgraded this truth in the round-3 report is now closed — see truth 8 below. |
| 5 | Registry/docs enforcement, seven comparison semantics, `skipped != pass` | ✓ VERIFIED [CARRIED] | No round-4 plan touches `src/core/checks.def`, `src/compare/*`, or the registry generator; confirmed by the full round-4 file list. |

#### Round-4 gap-closure truths, newly verified this round

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 6 | G-02-5 closed: mediadiff's byte-level output is identical on Windows and POSIX (goldens match, subprocess bytes match, `--json` stdout equals `--json=<path>` file bytes) | ✓ VERIFIED [LOCAL + CI-32153890395] | `.gitattributes` read directly: `* text=auto eol=lf`, no `filter`/`merge`/`-diff` attribute. `git add --renormalize .` [LOCAL] stages nothing but a pre-existing unrelated edit — confirmed a genuine no-op on tracked content. `src/cli/main.cpp` read directly: `_setmode(_fileno(stdout), _O_BINARY)` and the matching `stderr` line are literally the first two statements of `wmain` (lines 233-234), before `enable_vt_output()` (line 241) and before any output, including the function's own UTF-16-conversion failure path. `tests/integration/test_json_schema.cpp`'s new `TRUST-05` TEST_CASE read directly: reads the file destination in binary via `fopen_utf8(...,"rb")`, asserts no `0x0D` in captured stdout, and asserts full byte equality between stdout and the file. On CI run `32153890395`, job `95766278227`, pulled fresh via `gh run view --job 95766278227 --log`: `grep -c` returns `0` for `C4996`/`C4702`/`C2220`/`golden mismatch`/`85000`/`FAILED`; tests `#67`, `#75`, `#95`, `#201`, `#262` (TRUST-05) all confirmed `Passed` by exact name at their logged line numbers. `tests/process_spawn.h`, `tests/support/golden.cpp`, and the four `fopen_utf8(..., "wb")` file-destination call sites all confirmed untouched (`git diff --stat` empty across the whole round-4 range). |
| 7 | G-02-6 closed: a non-ASCII filename round-trips through mediadiff's path handling unchanged on Windows | ✓ VERIFIED [LOCAL + CI-32153890395] | `tests/support/utf8_path.h` read directly: `path_from_utf8` routes through `mediadiff::utf8_to_wide` on Windows (no wide type named directly, no `u8path`), byte-verbatim elsewhere — mirrors `src/cli/dir_pairing.cpp`'s own two-branch conversion, confirmed by reading both files side by side. `tests/unit/test_dir_pairing.cpp`'s literal `"r\xC3\xA9sum\xC3\xA9.snap.json"` and its `relative_path == non_ascii_name` assertion confirmed unchanged (`grep -c` = 1 each). `src/cli/dir_pairing.cpp` confirmed untouched across the whole round-4 range — the product was never at fault. On CI run `32153890395`, job `95766278227`: test `#36` `unit.dir_pairing: a tree containing a non-ASCII filename pairs correctly` confirmed `Passed`, `grep -c 'sumÃ'` returns `0`. |
| 8 | G-02-7 closed (both halves): mediadiff's documented exit-code contract and `explain_inspect` output hold identically on Windows | ✓ VERIFIED [LOCAL + CI-32153890395] | `src/cli/main.cpp` read directly: `app.allow_windows_style_options(false);` sits immediately after `allow_subcommand_prefix_matching(false)` (line 82), above every `register_*_command` call (first at line 92) — placement is load-bearing per CLI11 2.6.2's parent-to-child INHERITABLE copy, and the ordering was confirmed by line number, not assumed. `src/cli/exit_code.cpp` read directly and confirmed byte-for-byte untouched across the whole round-4 range: `input_open` → `kExitInput` (65), `usage` → `kExitUsage` (64), unconditional, no platform branch, no `default:` arm. `tests/integration/test_exit_codes.cpp` read directly: `CHECK(result.exit_code == 65)` at line 94 is unchanged; the input path `/no/such/file.snap.json` is unchanged; two new stderr assertions (lines 101-102) confirmed present. Locally ran `ctest -R exit_codes` [LOCAL]: all 12 cases pass, including all four `64` cases and both `65` cases. `tests/integration/test_explain_inspect.cpp` confirmed untouched across the whole round-4 range — its file was deliberately never modified so that its Windows pass would be a designed experiment proving downstream causation, not an independent fix. On CI run `32153890395`, job `95766278227`: test `#236` `integration.exit_codes - 65: ...` and test `#247` `integration.explain_inspect - REPORT-07: ...` both confirmed `Passed` by exact name; `grep -c '64 == 65'` returns `0`. |
| 9 | The four Windows tests that passed in round 3 for the wrong reason (CRLF golden matching CRLF stdout) now pass for the RIGHT reason | ✓ VERIFIED [CI-32153890395] | `#225` `dir_mode - the TTY worst-N table...`, `#248` `explain_inspect - REPORT-07: two inspect runs produce byte-identical stdout...`, `#268` `json_schema - two identical compare --json runs...`, `#273` `list_checks - ENG-12: --effective is byte-identical...` — all four independently re-pulled from the raw Windows job log this session and confirmed `Passed` at the exact test numbers `02-19-SUMMARY.md` reports. Both halves of `02-17`'s fix (goldens pinned to LF, stdout set to binary/LF) needed to land together for these four to keep passing at LF == LF instead of the coincidental CRLF == CRLF that made them pass before. |
| 10 | The four required CI merge contexts are green simultaneously, for the first time in the project's history, on the exact commit this report certifies | ✓ VERIFIED [CI-32153890395] | `gh run view 32153890395 --json headSha,conclusion,jobs` [CI-32153890395], pulled fresh this session: `headSha` = `d7274258ad73b61a4927721f0020e66099e65912`, string-equal to local `HEAD` (`git rev-parse HEAD` [LOCAL], confirmed identical). Job conclusions: `lint (ENG-16 boundary)` success, `build (x64-linux)` success, `build (arm64-osx)` success, `build (x64-windows-static-md)` success. `build (x64-osx)` and `build (arm64-linux)` both `failure`, confirmed matching their recorded non-blocking signatures (below). |
| 11 | The local build reproduces the CI result on this host, under the same warnings-as-errors discipline | ✓ VERIFIED [LOCAL] | Touched every round-4-modified translation unit and forced a clean rebuild: `cmake --build --preset x64-linux` succeeded with zero warning/error lines under `-Wall -Wextra -Werror` (a `-Werror` build cannot succeed silently past a warning). `ctest --test-dir build/x64-linux`: `298/298` passed, `0` failed, `1` skipped (`unit.console_vt`, real-console-only, unchanged). All four lint scripts (`lint_eng16.sh`, `lint_check_id_strings.sh`, `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`) exit `0`. |
| 12 | No debt markers or prohibited escape hatches were introduced anywhere in the round-4 diff | ✓ VERIFIED [LOCAL] | `grep -n -E "TBD\|FIXME\|XXX\|TODO\|HACK\|PLACEHOLDER"` across every round-4-touched source/test file → `0` matches. `grep -rn "pragma warning(suppress\|SKIP(\|UPDATE_GOLDENS"` across the same files → `0` matches. |
| 13 | All 48 Phase-2 requirement IDs remain traceable and `Complete` | ✓ VERIFIED [LOCAL] | Re-derived the correct 48-ID set by reading `REQUIREMENTS.md` directly rather than trusting the round-3 report's summary: `CLI-05` and `CLI-09` are `Phase 1`-owned, not Phase 2 (confirmed by their own rows), so Phase 2's true set is `CLI-01,02,03,04,06,07,08,10` (8, not 10) + `ENG-01..16` (16) + `SNAP-01..07` (7) + `REPORT-01..07` (7) + `DIR-01..05` (5) + `TRUST-03,05,08` (3) + `DOC-01,02` (2) = **48**. `grep -c` over exactly those 48 rows for `Complete` → `48`. `ROADMAP.md`'s own Phase-2 `**Requirements**:` line independently lists the identical 48-ID set, cross-confirming the derivation. |

**Score:** 13 directly-numbered truths above, all VERIFIED, covering 22 distinct must-have items across the phase's five roadmap Success Criteria plus the three round-4 gap closures. **0 truths FAILED. 0 behavior-unverified.** The one remaining human-facing item (UAT test 2, real-Windows-console colour rendering) is not counted as open: it was explicitly deferred to Phase 3 by human decision at the round-4 checkpoint, not left unresolved by this verification pass.

### Deferred Items

Not gaps — explicitly deferred by human decision or by the round-4 plans' own scope, and none blocks this phase's goal.

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 1 | UAT test 2 — colour renders as real styling in a Windows console | Deferred to Phase 3 (human decision, 2026-08-18) | `02-UAT.md round_4_verdict.uat_test_2`; artifact `mediadiff-windows-x64` (1,301,968 bytes) confirmed to exist at sha `d727425` via the GitHub Actions API [CI-32153890395]. |
| 2 | `CI-x64-osx` | Deferred, non-blocking, unchanged signature | Job `95766278109` re-pulled this session: `fat file missing arch 'arm64', file has 'x86_64'`. Not a required context. |
| 3 | `CI-arm64-linux` | Deferred, non-blocking, unchanged signature | Job `95766278139` re-pulled this session: fails at `Register vcpkg NuGet feed`, exit 1. Not a required context. |
| 4 | `getenv_utf8` thread-safety contract undocumented | Deferred, non-blocking | `02-UAT.md` Deferred Follow-Ups, deferred 2026-08-16, untouched by round 4. |
| 5 | Dead `<cstdlib>` include in `src/cli/options.cpp` | Deferred, non-blocking | `02-UAT.md` Deferred Follow-Ups, deferred 2026-08-16, untouched by round 4. |
| 6 | Severity colour applies only to the status glyph | Deferred, non-blocking (spec ambiguity, not a defect) | `02-UAT.md` Deferred Follow-Ups, deferred 2026-08-16, untouched by round 4. |
| 7 | No test exercises a non-ASCII path end-to-end through the built `mediadiff` binary (CLI-09's actual promise) | Deferred, non-blocking | `02-18-SUMMARY.md`'s Task 2 Part 3 audit; explicitly recorded rather than added in round 4 to avoid perturbing `02-17`'s count criteria. |

### Required Artifacts (round-4 scope)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `.gitattributes` | pins every text file to LF on checkout, no content-hiding attribute | ✓ VERIFIED [LOCAL] | Read directly; renormalize proven a no-op this session. |
| `src/cli/main.cpp` — `_setmode` binary-mode block | stdout AND stderr binary before any output | ✓ VERIFIED [LOCAL] | Read directly; first two statements of `wmain`, before `enable_vt_output()`. |
| `src/cli/main.cpp` — `allow_windows_style_options(false)` | above every subcommand registration | ✓ VERIFIED [LOCAL] | Read directly; line 82 precedes line 92 (first `register_` call). |
| `tests/integration/test_json_schema.cpp` — TRUST-05 test | asserts stdout bytes == file bytes, no `0x0D` in stdout | ✓ VERIFIED [LOCAL + CI] | Read directly; passed on all three blocking legs [CI-32153890395]. |
| `tests/support/utf8_path.h` | mirrors product's UTF-8 conversion, no wide type named, no `u8path` | ✓ VERIFIED [LOCAL] | Read directly; `grep` confirms no wide-type identifiers, no `u8path`. |
| `tests/unit/test_process_spawn.cpp` | child writes via `sys.stdout.buffer`/`sys.stderr.buffer`, no-CR assertions added | ✓ VERIFIED [LOCAL] | Read directly; `kLineCount`/`kLineBytes` unchanged, `tests/process_spawn.h` untouched. |
| `tests/unit/test_dir_pairing.cpp` — non-ASCII fixture | routed through `path_from_utf8`, literal and assertion unchanged | ✓ VERIFIED [LOCAL] | Read directly. |
| `tests/integration/test_exit_codes.cpp` — 65 case | `== 65` verbatim, stderr classification assertions added | ✓ VERIFIED [LOCAL] | Read directly. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| GitHub Actions Windows runner checkout | bytes of `tests/golden/*.txt` on disk | `.gitattributes`'s `* text=auto eol=lf` | ✓ WIRED [CI-32153890395] | Three in-process golden tests confirmed `Passed`, zero `golden mismatch` in the log. |
| `wmain`'s `_setmode` calls | `std::fputs`/`fwrite` to stdout/stderr in `src/cli/` | fd-level binary mode set before any write | ✓ WIRED [LOCAL + CI-32153890395] | New TRUST-05 test asserts and passes on all three blocking legs. |
| root `CLI::App`'s `allow_windows_style_options(false)` | every subcommand's own argv classification | CLI11 2.6.2 parent-to-child INHERITABLE copy | ✓ WIRED [LOCAL + CI-32153890395] | Placement confirmed above all `register_*_command` calls; test `#236` confirmed `Passed`. |
| `tests/support/utf8_path.h`'s `path_from_utf8` | the on-disk filename `write_file` creates | `mediadiff::utf8_to_wide` (mirrors `src/cli/dir_pairing.cpp`) | ✓ WIRED [LOCAL + CI-32153890395] | Test `#36` confirmed `Passed`. |
| `src/cli/commands/compare.cpp`/`dir.cpp` `fopen_utf8(..., "wb")` file destinations | stdout/stderr (now also binary) | Both now converge on LF/binary, closing the asymmetry the round-3 report flagged as an open human-verification item | ✓ WIRED, CLOSED MECHANICALLY [LOCAL + CI-32153890395] | The round-3 `02-VERIFICATION.md`'s second `human_verification` item (whether `--json` redirected to stdout is byte-identical to the file destination on Windows) is now answered by an automated, CI-enforced test rather than requiring a manual confirmation — human-accepted per `02-UAT.md round_4_verdict.trust_05_human_item`. |

### Requirements Coverage

All 48 Phase-2 requirement IDs — `CLI-01,02,03,04,06,07,08,10` (8; `CLI-05`/`CLI-09` are Phase-1-owned, confirmed by direct read and excluded), `ENG-01…16` (16), `SNAP-01…07` (7), `REPORT-01…07` (7), `DIR-01…05` (5), `TRUST-03,05,08` (3), `DOC-01,02` (2) — are `Complete` in `.planning/REQUIREMENTS.md`, independently re-confirmed by `grep -c` in this session against exactly that 48-row set (`48` matches). `DIR-06` is the one nearby ID that is NOT part of this phase (`Phase 3`, `Pending`) — confirmed by direct read, not assumed. `ROADMAP.md`'s own Phase-2 `**Requirements**:` line lists the identical 48-ID set, cross-confirming the derivation independently of `REQUIREMENTS.md`. `TRUST-05` in particular is now backed by both a mechanical CI-enforced test (this round's `TRUST-05` TEST_CASE) and the pre-existing POSIX byte-identity guarantee, closing the one requirement this round's evidence most directly strengthens.

### Anti-Patterns Found

None. `grep -n -E "TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER"` across every file touched in the round-4 commit range (`.gitattributes`, `src/cli/main.cpp`, `tests/integration/test_json_schema.cpp`, `tests/integration/test_exit_codes.cpp`, `tests/unit/test_process_spawn.cpp`, `tests/unit/test_dir_pairing.cpp`, `tests/support/utf8_path.h`) returned zero matches. `grep -rn "pragma warning(suppress|SKIP(|UPDATE_GOLDENS"` across the same files returned zero matches. No prohibited escape hatch was used anywhere in the round-4 diff.

### Behavioral Spot-Checks

| Behavior | Command | Result | Evidence source |
|----------|---------|--------|--------|
| `_setmode` covers both streams, before any output | Direct read of `src/cli/main.cpp:233-241` | Confirmed: both calls precede `enable_vt_output()` | [LOCAL] |
| `allow_windows_style_options(false)` precedes subcommand registration | `grep -n` line-number comparison | Line 82 < line 92 | [LOCAL] |
| `.gitattributes` renormalizes nothing | `git add --renormalize .` then `git reset` | Stages nothing but a pre-existing unrelated edit | [LOCAL] |
| Prohibited files untouched across round 4 | `git diff --stat 4f17a19^..f11f807 -- <5 files>` | All five diffs empty | [LOCAL] |
| Full local suite | `ctest --test-dir build/x64-linux` after a forced clean rebuild | 298/298 passed, 0 failed, 1 skipped | [LOCAL] |
| `exit_codes` suite specifically | `ctest --test-dir build/x64-linux -R exit_codes` | 12/12 passed (4×64, 2×65, others) | [LOCAL] |
| All four lint scripts | `bash scripts/lint_*.sh` ×4 | All exit 0 | [LOCAL] |
| Windows job's zero-diagnostic claims | `gh run view --job 95766278227 --log \| grep -c` for `C4996`/`C4702`/`C2220`/`FAILED`/`golden mismatch`/`85000`/`64 == 65`/`sumÃ` | All 0 | [CI-32153890395] |
| Windows Test step total | same log | `100% tests passed, 0 tests failed out of 298` | [CI-32153890395] |
| x64-linux / arm64-osx totals | `gh run view --job <id> --log` | `0 tests failed out of 298` both legs | [CI-32153890395] |
| Lint job's four steps | `gh run view --job 95766277880 --log` | All four report "clean" by name | [CI-32153890395] |
| Non-required legs unchanged | `gh run view --job 95766278109/95766278139 --log` | Both match recorded signatures exactly | [CI-32153890395] |
| `mediadiff-windows-x64` artifact | `gh api .../runs/32153890395/artifacts` | `1301968` bytes, exists | [CI-32153890395] |
| Eight round-4-relevant tests by exact name | `gh run view --job 95766278227 --log \| grep -n` | All `Passed` at the exact line/number `02-19-SUMMARY.md` reports | [CI-32153890395] |

### Probe Execution

Not applicable — Phase 2 has no `scripts/*/tests/probe-*.sh` convention (unchanged from the round-3 report). The four required lint scripts function as this project's equivalent gate and are covered under Behavioral Spot-Checks above.

### Human Verification Required

**None outstanding.** The two items open in the prior (round-3) `02-VERIFICATION.md`'s `human_verification` block are both resolved:

1. **Real-Windows-console colour rendering (UAT test 2)** — resolved by explicit human decision to defer to Phase 3 (`02-UAT.md round_4_verdict.uat_test_2`, decided 2026-08-18), not by verification. Recorded under Deferred Items above, not re-opened here.
2. **Whether `--json` redirected to stdout is byte-identical to the file destination on Windows** — resolved mechanically by the new `TRUST-05` test, which asserts and enforces exactly this property in CI on every run going forward, confirmed `Passed` on all three blocking legs in run `32153890395`. Human-accepted as sufficient (`02-UAT.md round_4_verdict.trust_05_human_item`).

### Gaps Summary

**All three round-4 gaps (G-02-5, G-02-6, G-02-7) are closed, independently re-verified against the source and against CI's own raw logs in this session — not accepted on the strength of any SUMMARY's narrative.** This phase took four rounds: round 1 found the environment-portability defects (`getenv`, ctest count parsing) that had never been exercised on Windows/macOS; round 2 confirmed those closed and, by finally letting MSVC and macOS's ctest compile/run further, revealed round 3's genuine toolchain-parity and fixture-portability defects; round 3 closed those and, by finally letting the Windows test suite execute end to end for the first time ever, revealed round 4's three defects — a stdout/checkout byte-identity gap that touched a hard product determinism constraint (TRUST-05), a test-fixture ANSI-codepage mis-decode, and an argv-classification divergence with a real CLI-06 contract consequence. Each round's fix worked exactly as intended and immediately exposed the next layer underneath it; that pattern, stated plainly across all four rounds' UAT records, is the honest shape of this phase's history, not evidence against it.

**What round 4 leaves genuinely open, stated plainly rather than absorbed into "passed":** UAT test 2 (Windows console colour) has still never been run on a real console — it is deferred to Phase 3 by explicit human decision, not verified, and the artifact it needs is real and available. `CI-x64-osx` and `CI-arm64-linux` are still red with unchanged, understood, non-blocking causes. Three cosmetic/documentation follow-ups from earlier rounds remain open and non-blocking. None of these five items is a gap against this phase's goal; all five are recorded above rather than silently dropped.

**What round 4 genuinely, newly proves:** byte-identical `--json` output — a hard project determinism constraint — is now asserted by an automated test on the one platform (Windows) that could previously have silently violated it, closing a real gap between "assumed on POSIX" and "proven everywhere." The four required CI merge contexts are green simultaneously for the first time in this project's history. All 48 Phase-2 requirement IDs trace to `Complete` implementations, independently re-confirmed. The phase goal — a complete compare engine (registry, seven comparison semantics, policy resolution with provenance, snapshots with schema-version gating, all four report formats, `dir` orchestration with the full exit-code contract, `skipped != pass`) working end to end against stub measurements — is achieved and observably verified, on every platform the project's required merge gate covers.

---

_Verified: 2026-08-18T15:44:33Z_
_Verifier: Claude (gsd-verifier)_
