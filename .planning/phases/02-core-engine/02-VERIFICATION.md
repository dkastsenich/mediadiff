---
phase: 02-core-engine
verified: 2026-08-18T09:30:00Z
status: gaps_found
score: 14/17 truths verified directly (3 open blockers carried from round-4 human UAT, none newly introduced by this verification pass)
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "11/12 truths verified directly; 1 truth (actual CI-green confirmation) unverifiable without a pushed CI run"
  gaps_closed:
    - truth: "G-02-3 — block_for()'s FAIL()-then-dead-code shape trips MSVC C4702 -> C2220 under /W4 /WX"
      evidence: "Read tests/unit/test_report_model.cpp:59-65 directly on this host: block_for() now does std::find_if + INFO + REQUIRE + one reachable `return *it;`. grep -c 'FAIL(' on the file returns 0. grep for pragma warning(suppress, std::abort, std::terminate all return 0 — no compiler-specific escape hatch. Independently confirmed against CI run 31946964023 job 95164409600 via `gh run view --job 95164409600 --log`: Build step conclusion success, zero C4702/C2220/C4996 in the log."
    - truth: "G-02-4 — test_dir_pairing.cpp's fixture collided under case-insensitive APFS (4 vs 5 files)"
      evidence: "Read tests/unit/test_dir_pairing.cpp:113-152 directly: fixture is now {zeta, alpha, Beta, Zulu, 1}.snap.json — five names, pairwise distinct under case folding, confirmed by independently computing sorted(names) vs sorted(names, key=str.lower) in Python: they differ in 4 of 5 positions (strictly stronger than the original single case-pair). Independently confirmed via `gh run view --job 95164409605 --log`: test #40 'unit.dir_pairing: the returned order is byte-wise sorted' -> Passed, '100% tests passed out of 297'."
  gaps_remaining:
    - "G-02-5 — Windows byte-level output divergence (newline/text-mode), affecting tests #95, 67, 75, 201 — see gaps below"
    - "G-02-6 — non-ASCII filename mis-decodes on Windows (test #36) — see gaps below"
    - "G-02-7 — exit-code and inspect-output behavioral divergence on Windows (tests #236, #247) — see gaps below"
  regressions: []
  gap_closure_caused_regressions: false
gaps:
  - gap_id: G-02-5
    truth: "mediadiff's byte-level output is identical on Windows and POSIX — goldens match, and captured subprocess bytes are exactly what the child wrote"
    status: failed
    severity: blocker
    reason: "Confirmed open. Independently re-pulled CI run 31946964023 job 95164409600's raw log via `gh run view --job 95164409600 --log`: test_process_spawn.cpp(42) FAILED with 85000 (0x14c08) == 82500 (0x14244) (delta exactly 2500 bytes); three golden mismatches at golden.cpp(119) (junit_basic, markdown_basic, tty golden), all 'at line 1'."
    artifacts:
      - path: "tests/unit/test_process_spawn.cpp"
        issue: "line 42 — byte-count mismatch consistent with LF->CRLF pipe-read translation, unconfirmed on this Linux host"
      - path: "tests/support/golden.cpp"
        issue: "line 119 — three golden mismatches reported 'at line 1' with visually identical expected/actual"
    missing:
      - "Diagnose on real Windows hardware/CI, per G-02-5's own missing[] in 02-UAT.md — not re-derivable from this Linux host."
  - gap_id: G-02-6
    truth: "A non-ASCII filename round-trips through mediadiff's path handling unchanged on Windows"
    status: failed
    severity: blocker
    reason: "Confirmed open via `gh run view --job 95164409600 --log`: test_dir_pairing.cpp(204) FAILED — mojibake 'rÃ©sumÃ©.snap.json' vs 'résumé.snap.json', a UTF-8-bytes-read-as-Latin-1 signature."
    artifacts:
      - path: "tests/unit/test_dir_pairing.cpp"
        issue: "line 204 — non-ASCII filename mis-decodes on Windows"
    missing:
      - "Diagnose on real Windows hardware/CI, per G-02-6's own missing[] in 02-UAT.md."
  - gap_id: G-02-7
    truth: "mediadiff's documented exit-code contract and inspect output hold identically on Windows"
    status: failed
    severity: blocker
    reason: "Confirmed open via `gh run view --job 95164409600 --log`: test_exit_codes.cpp(94) FAILED 64 == 65; test_explain_inspect.cpp(112) FAILED found_meta_no_measurements -> false."
    artifacts:
      - path: "tests/integration/test_exit_codes.cpp"
        issue: "line 94 — nonexistent input path exits 64 on Windows, contract requires 65"
      - path: "tests/integration/test_explain_inspect.cpp"
        issue: "line 112 — may be downstream of G-02-5's newline handling; not yet split apart"
    missing:
      - "Diagnose on real Windows hardware/CI, per G-02-7's own missing[] in 02-UAT.md."
human_verification:
  - test: "Colour renders as real styling in a Windows console (mediadiff compare/dir/TTY output in cmd.exe or Windows Terminal, ENABLE_VIRTUAL_TERMINAL_PROCESSING path exercised)"
    expected: "ANSI-interpreted styling, not literal escape-sequence bytes"
    why_human: "Reachable for the first time in the phase's history — run 31946964023's Windows Build step succeeded and produced mediadiff.exe — but has not been run. Requires a real Windows console; this sandbox is Linux-only. Carried forward from 02-UAT.md test 2, unchanged in substance. Record the mediadiff build sha with the result: if G-02-5 (newline handling) is closed after this check runs, the console bytes this check observes could change."
  - test: "Confirm whether mediadiff's own --json/--report stdout output is actually byte-identical on Windows, independent of the seven known test failures"
    expected: "Either (a) mediadiff.exe run on Windows with --json piped/redirected produces byte-for-byte the same content as the POSIX build (no \\r\\n insertion), confirming the determinism contract holds in the product itself and not just in in-process unit tests; or (b) it does not, which would be a product-level defect distinct from and more consequential than any of G-02-5/6/7 as currently scoped (those are entirely test-harness/test-fixture failures, not confirmed product-output failures)."
    why_human: "This verification pass found a plausible, unconfirmed mechanism for a POSIX-invisible defect that no currently-open gap covers: src/cli/commands/compare.cpp and src/cli/commands/dir.cpp write --json/TTY output to stdout via `std::fputs(report.c_str(), stdout)`, and grep across the whole of src/ for `_setmode`/`_O_BINARY`/`SetConsoleMode`-adjacent binary-mode guards on stdout returns zero matches (only src/util/fs.h:262's unrelated `SetConsoleMode(..., ENABLE_VIRTUAL_TERMINAL_PROCESSING)` VT call exists). File-destination writes (`write_report_file`, both compare.cpp and dir.cpp) DO use `fopen_utf8(path, \"wb\")` — explicit binary mode — so that path looks safe. But the C runtime's documented default text-mode behavior for `stdout` on Windows (CRLF-translating every `\\n` written through it, unless the stream's mode is explicitly set to binary) cannot be exercised or confirmed from this Linux sandbox. If real, this would mean any Windows user piping `mediadiff --json > out.json` gets \\r\\n-terminated JSON while the same run on Linux/macOS produces \\n-terminated JSON — a direct violation of the project's own stated 'byte-identical --json across identical runs' determinism requirement, and one none of G-02-5's four currently-affected tests (all test-harness or golden-fixture failures) directly exercises. This is reported as an open question, not a confirmed defect — the code inspection is real, but the runtime behavior needs a Windows host to confirm."
---

# Phase 2: Core Engine Verification Report (Round-3 Re-Verification, Superseding Prior Report)

**Phase Goal:** The complete compare engine — registry, comparison semantics, policy resolution, snapshots, all four report formats and `dir` orchestration — works end to end against stub measurements, so every analyzer that follows plugs into finished machinery.

**Verified:** 2026-08-18T09:30:00Z
**Status:** gaps_found
**Re-verification:** Yes — round 3, superseding the prior `02-VERIFICATION.md` (2026-08-16T14:05:00Z, `gaps_found`, `gaps_remaining: [G-02-3, G-02-4]`)

## Why this report supersedes the prior one

The prior report's `gaps_remaining` listed G-02-3 and G-02-4 as open, discovered from CI run `31943688186` (headSha `1e065bb`). Plans `02-14` and `02-15` closed those two gaps at the code level; plan `02-16` wired two new portability lints into the required `lint (ENG-16 boundary)` CI job, pushed, and read the resulting run — **CI run `31946964023`, headSha `8d4aa4ac99c024c89e9816f843eb0b540e152a4a`**. A human reviewed that run on 2026-08-18, confirmed both closures, and opened three round-4 gaps (G-02-5, G-02-6, G-02-7) against the Windows test suite's first-ever execution.

**This verification independently re-derived every claim below rather than trusting `02-UAT.md`, `02-16-SUMMARY.md`, or either gap-closure SUMMARY.** Concretely:

- Read `tests/unit/test_report_model.cpp` and `tests/unit/test_dir_pairing.cpp` directly.
- Re-computed the byte-wise-vs-case-folded ordering claim independently in Python rather than trusting the SUMMARY's assertion that they differ.
- Ran both new lint scripts locally, then deliberately broke them (empty scan dir, injected known-bad fixtures) to confirm their self-tests and guards actually fire, not just that they report clean on the current tree.
- Ran `git show`/`git diff --unified=0` on commit `8d4aa4a` myself to confirm the `ci.yml` change is additions-only with the required-check name intact.
- **Pulled CI run `31946964023`'s raw job logs myself via `gh run view --job <id> --log`**, independently of the SUMMARY's transcription, for both the Windows Test step and the arm64-osx Test step.
- Built and ran the local Linux test suite myself (297/297) rather than citing the SUMMARY's number.
- Went beyond the assigned re-read to trace the product's own stdout-writing code path for a POSIX-invisible risk the CI read did not surface — see the new human-verification item below.

Every claim in this report is labeled by its evidence source: **[LOCAL]** = observed directly on this Linux host in this session; **[CI-31946964023]** = pulled from that specific run's logs in this session (not from a SUMMARY's transcription of it); **[CARRIED]** = a regression-checked claim from the prior verification round, not re-derived from zero this round.

## Goal Achievement

### Observable Truths

#### Carried forward from prior rounds (regression-checked, not re-derived)

| # | Truth | Status | Regression check |
|---|-------|--------|-------------------|
| 1 | Snapshot/compare round-trip, schema_version gate, CI tracked-overwrite gate | ✓ VERIFIED [CARRIED] | `ctest --test-dir build/x64-linux` [LOCAL]: 297/297 passed, 0 failed — same count and same pass rate as the round-2 verification's regression check; no file under `src/core/snapshot*` appears in any of the three round-3 plans' diffs. |
| 2 | Policy resolution (profiles, TOML, `--set`/`--tol`, provenance) | ✓ VERIFIED [CARRIED] | None of `02-14`, `02-15`, `02-16`'s file lists touch `src/config` or `policy.cpp`. |
| 3 | Four report formats render correctly; colour auto-disable logic | ✓ VERIFIED [CARRIED] (logic); real-Windows-console rendering ⚠️ still human-only | `render_markdown`/`render_junit`/`render_tty`'s own unit suites unaffected — `02-15`'s only test-file touch is `test_dir_pairing.cpp` (an unrelated `dir` test). See "New finding" below: the round-3 work surfaced new information about how the four formats reach stdout that bears on this truth and is now routed to human verification. |
| 4 | `dir` pairing, threads, exit-code contract, `ENG-16` process-control confinement | ✓ VERIFIED [CARRIED] on Linux/macOS; ⚠️ Windows exit-code contract now has an OPEN, CONFIRMED failure (G-02-7, test #236) | `src/cli/dir_pairing.cpp` untouched by any round-3 plan (confirmed via `git diff --name-only` in 02-15's own evidence and independently by `grep -rn dir_pairing.cpp` against the three plans' `key-files`). The exit-code CONTRACT truth is downgraded here relative to the prior report because G-02-7 is new, confirmed, Windows-specific information the prior report did not have. |
| 5 | Registry/docs enforcement, seven comparison semantics, `skipped != pass` | ✓ VERIFIED [CARRIED] | No round-3 plan touches `src/core/checks.def`, `src/compare/*`, or the registry generator. |

#### New truths verified this round (G-02-3/G-02-4 closure and its CI confirmation)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 6 | G-02-3 closed: `block_for()` has a single reachable exit; no compiler-specific suppression was used | ✓ VERIFIED [LOCAL] | Read `tests/unit/test_report_model.cpp:59-65` directly: `std::find_if` + `INFO` + `REQUIRE` + one `return *it;`. `grep -n "FAIL("` on the file → 0 matches (exit 1, no output). `grep -n "pragma warning\|std::abort\|std::terminate"` → 0 matches. All 11 `TEST_CASE("report_model...")` cases still present (`grep -c` → 11) — none deleted, skipped, or platform-guarded, satisfying `02-14-PLAN.md`'s three prohibitions verbatim. |
| 7 | G-02-3 closed on the real Windows target, not just locally | ✓ VERIFIED [CI-31946964023] | `gh run view --job 95164409600 --log` (job for `build (x64-windows-static-md)`), pulled fresh in this session: the Build step's log contains zero `C4702`, zero `C2220`, zero `C4996`. Build step conclusion `success`. This is independent of `02-16-SUMMARY.md`'s transcription — I ran the same query myself and got the same result. |
| 8 | G-02-4 closed: the byte-wise-order fixture is case-collision-free AND the teeth assertion is genuinely stronger, not just claimed to be | ✓ VERIFIED [LOCAL] | Read `tests/unit/test_dir_pairing.cpp:113-152` directly. Fixture: `{"zeta.snap.json","alpha.snap.json","Beta.snap.json","Zulu.snap.json","1.snap.json"}`. Independently computed in Python (not trusting the SUMMARY's "differ in four of five positions" claim): `sorted(names)` vs `sorted(names, key=str.lower)` — confirmed to differ at 4 of 5 positions (`1`,`Beta`,`Zulu`,`alpha`,`zeta` byte-wise vs `1`,`alpha`,`Beta`,`zeta`,`Zulu` case-folded). This is strictly stronger than the original single Beta/beta pair. The `expected_order` literal in the file matches my independently-computed byte-wise order exactly. Case-fold uniqueness confirmed: `{1, beta, zulu, alpha, zeta}` — five distinct lowercased stems, no collision. |
| 9 | G-02-4 closed on the real macOS target, not just locally | ✓ VERIFIED [CI-31946964023] | `gh run view --job 95164409605 --log` (job for `build (arm64-osx)`), pulled fresh: `Test #40: unit.dir_pairing: the returned order is byte-wise sorted ... Passed`; `100% tests passed out of 297`. Independent of the SUMMARY's transcription. |
| 10 | The two new lint scripts have real teeth — they fail loudly when their input is genuinely bad, not merely when told to | ✓ VERIFIED [LOCAL] | Ran both scripts against the current tree: both exit 0, "clean," scanning 49 files each. Then deliberately broke each: (a) pointed each script's `SCAN_DIR(S)` at an empty directory — both correctly refuse to report clean and exit 1 with a "zero files" diagnostic; (b) injected a real known-bad file (a `FAIL()`-then-`static`-then-`return` function; two case-colliding literals `"Case.snap.json"`/`"case.snap.json"`) into an isolated scan tree — both scripts correctly flag the violation by file:line and exit 1. Neither script can be made to report clean over an empty or unreadable scan. |
| 11 | The `ci.yml` change wiring the two lints in is additions-only, preserves the required-check name, and adds no escape hatch | ✓ VERIFIED [LOCAL] | `git show 8d4aa4a -- .github/workflows/ci.yml` → `+12 lines, 0 deletions`, confined to the `lint` job. `git diff --unified=0 8d4aa4a~1 8d4aa4a -- .github/workflows/ci.yml \| grep -c '^-[^-]'` → 0. `grep -c "name: lint (ENG-16 boundary)"` → exactly 1. `continue-on-error` search scoped to the `lint:` job block → 0 matches (the one file-wide match is the pre-existing, unrelated `build` job's `continue-on-error: ${{ !matrix.blocking }}`, present before this commit). |
| 12 | Local Linux build/test suite unaffected by the round-3 work | ✓ VERIFIED [LOCAL] | `cmake --build --preset x64-linux` → `ninja: no work to do` (already built at current HEAD `4057254`). `ctest --test-dir build/x64-linux` → `100% tests passed, 0 tests failed out of 297`. Independently cross-checked against `build (x64-linux)`'s own CI conclusion for the same headSha via `gh run view 31946964023 --json jobs` → `{"name":"build (x64-linux)","conclusion":"success"}`. Local and CI agree. |
| 13 | No debt markers or anti-patterns introduced in the round-3 diff | ✓ VERIFIED [LOCAL] | `grep -n -E "TBD\|FIXME\|XXX\|TODO\|HACK\|PLACEHOLDER"` across `tests/unit/test_report_model.cpp`, `tests/unit/test_dir_pairing.cpp`, both new lint scripts, and `ci.yml` → 0 matches. |
| 14 | All 48 Phase-2 requirement IDs remain traceable | ✓ VERIFIED [LOCAL] | `grep -cE` over `REQUIREMENTS.md`'s Phase 2 rows for the 48-ID list → 48 matches, all `Complete`. `02-14`/`02-16` declare `requirements: [BUILD-01, BUILD-05]`; `02-15` declares `[BUILD-01, BUILD-05, DIR-04]` — DIR-04 IS one of Phase 2's own 48 IDs (byte-wise dir-pairing ordering), correctly claimed since 02-15 is the plan that repairs its test coverage. BUILD-01/BUILD-05 are Phase-1-owned IDs being *restored* (the prior verification's framing, confirmed correct here — not re-litigated). No orphans found. |

#### Truths NOT verified this round — still open, confirmed blockers (round-4 gaps)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 15 | mediadiff's byte-level test output is identical on Windows and POSIX (goldens match; captured subprocess bytes match exactly) | ✗ FAILED — G-02-5, open | `gh run view --job 95164409600 --log` [CI-31946964023], pulled fresh: `test_process_spawn.cpp(42): FAILED: 85000 (0x14c08) == 82500 (0x14244)` (delta exactly 2500 bytes); three `golden.cpp(119): FAILED:` lines for junit_basic/markdown_basic/the tty golden. All confirmed present in the raw log, not just in the SUMMARY's transcription. |
| 16 | A non-ASCII filename round-trips through mediadiff's path handling unchanged on Windows | ✗ FAILED — G-02-6, open | `gh run view --job 95164409600 --log`: `test_dir_pairing.cpp(204): FAILED:` — confirmed present in the raw log. |
| 17 | mediadiff's documented exit-code contract and inspect output hold identically on Windows | ✗ FAILED — G-02-7, open | `gh run view --job 95164409600 --log`: `test_exit_codes.cpp(94): FAILED:` and `test_explain_inspect.cpp(112): FAILED:` — confirmed present in the raw log. `98% tests passed, 7 tests failed out of 297` matches the sum of all seven distinct failure lines counted in the raw log (2 golden lines were double-checked as belonging to two of the three reported golden mismatches; total distinct FAILED lines in the Test step = 7). |

**Score:** 14/17 truths verified directly against the codebase or against CI's own raw logs (11 code-level + CI-confirmed truths, plus 3 carried-forward regression checks not separately numbered above). 3 truths remain FAILED — all three are the round-4 gaps a human already opened against this exact CI run, none newly discovered here, and none is a regression caused by the round-3 gap-closure work (all three are pre-existing Phase-2 defects in code the Windows leg had literally never executed before this run, per G-02-3's own scope_caveat).

### Required Artifacts (round-3 scope)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `tests/unit/test_report_model.cpp` | `block_for()` restructured to single reachable exit | ✓ VERIFIED [LOCAL] | Read directly; matches spec, all 11 tests intact. |
| `scripts/lint_dead_code_after_fail.sh` | self-testing lint, FAIL()-then-statement shape, zero-file guard | ✓ VERIFIED [LOCAL] | Ran clean, then broke deliberately (see truth #10); both guards fire correctly. |
| `tests/unit/test_dir_pairing.cpp` | case-collision-free byte-wise-order fixture with teeth assertion | ✓ VERIFIED [LOCAL] | Read directly; independently recomputed the ordering claim (see truth #8). |
| `scripts/lint_fixture_case_collisions.sh` | self-testing lint, case-fold-collision shape, zero-file guard | ✓ VERIFIED [LOCAL] | Ran clean, then broke deliberately; both guards fire correctly. |
| `.github/workflows/ci.yml` | two new lint steps wired in, additions-only, required-check name preserved | ✓ VERIFIED [LOCAL] | `git show`/`git diff --unified=0` confirm +12/-0, name intact, no `continue-on-error` added. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `tests/unit/test_report_model.cpp::block_for` | `mediadiff::group_to_string` | `INFO(...)` diagnostic on miss | ✓ WIRED [LOCAL] | Confirmed at line 62; the diagnostic names the missing group rather than the function crashing or silently returning a default. |
| `.github/workflows/ci.yml` `lint` job | `scripts/lint_dead_code_after_fail.sh` | `run: bash scripts/...` step | ✓ WIRED [CI-31946964023] | `gh run view --job 95164409561 --log`-class confirmation not separately re-pulled this session, but `02-16-SUMMARY.md`'s claim that all four lint steps ran by name is consistent with the job's `success` conclusion pulled via `gh run view 31946964023 --json jobs`. |
| `.github/workflows/ci.yml` `lint` job | `scripts/lint_fixture_case_collisions.sh` | `run: bash scripts/...` step | ✓ WIRED [CI-31946964023] | Same job, same conclusion. |
| `src/cli/commands/compare.cpp` / `dir.cpp` | `stdout` (for `--json`, TTY, and any report NOT sent to `--report`/`--json` file path) | `std::fputs(..., stdout)` | ⚠️ WIRED but binary-mode-unconfirmed on Windows | See new finding below and the corresponding human-verification item — routed to human, not asserted as broken. |
| `src/cli/commands/compare.cpp::write_report_file` / `dir.cpp` equivalent | file destinations (`--report`, `--json <path>`) | `fopen_utf8(path, "wb")` | ✓ WIRED, binary-mode confirmed [LOCAL] | Read directly: explicit `"wb"` mode at the one shared `write_report_file` helper compare.cpp uses, and the equivalent call site in dir.cpp. This path is NOT implicated in the newline-translation risk the stdout path is. |

### New finding — beyond the CI read (task item 6)

The task asked whether any of the seven Windows test failures have a POSIX-invisible analogue already latent in the code that no currently-open gap would catch. Two candidate mechanisms were found by tracing the code, neither confirmed on a real Windows host (this sandbox is Linux-only), both reported as open questions rather than defects:

**1. Missing `.gitattributes` — plausible explanation for the golden-file mismatches (G-02-5's three golden failures).** `cat .gitattributes` [LOCAL] → file does not exist. `git check-attr text eol -- tests/golden/*.txt` [LOCAL] → `text: unspecified`, `eol: unspecified` for every golden file. `golden.cpp` opens both the golden file (`fopen_utf8(path, "rb"/"wb")`) and compares against an in-memory string built entirely by `render_markdown`/`render_junit`/`render_tty` (pure string builders, no file I/O, confirmed by reading `test_markdown_budget.cpp:226`, `test_junit.cpp:219`, `test_tty_render.cpp:258`). With no `.gitattributes` pinning these files to LF, a Windows checkout under GitHub Actions' documented default `core.autocrlf=true` would materialize `tests/golden/*.txt` with CRLF line endings on disk, while the in-memory rendered string uses plain `\n` — producing exactly the observed symptom ("mismatch at line 1, expected and actual render identically in the log," since a trailing `\r` is invisible when printed). This is a distinct mechanism from the pipe-capture hypothesis 02-UAT.md's G-02-5 already favors for test #95 (which goes through an actual OS pipe, not a git-checked-out file) — both could be simultaneously true, explaining different subsets of the four affected tests. Not confirmed; would need a Windows checkout to verify the golden files' actual on-disk line endings.

**2. No binary-mode guard on `stdout` anywhere in `src/` — a possible product-level defect the current gaps do not cover.** `grep -rn "_setmode\|_O_BINARY\|O_BINARY" src/` [LOCAL] → 0 matches anywhere in the first-party source tree. Both `src/cli/commands/compare.cpp` and `src/cli/commands/dir.cpp` write `--json` and TTY report output to `stdout` via `std::fputs(report.c_str(), stdout)` — the C runtime's `stdout` stream, not an explicitly binary-mode handle. By contrast, every file-destination write in the same two files goes through `fopen_utf8(path, "wb")` — explicit binary mode, confirmed safe. Windows' C runtime is documented to default `stdout` to text mode, translating every `\n` written through it to `\r\n`, unless the stream's mode is explicitly set to binary (`_setmode`). **If this holds on real Windows hardware, `mediadiff --json > out.json` (or any pipe consumer) would receive CRLF-terminated JSON on Windows and LF-terminated JSON on Linux/macOS for byte-identical input — a direct, product-level violation of the project's own stated determinism contract ("byte-identical `--json` across identical runs"), and a defect none of the seven currently-failing tests directly exercises** (all seven are either in-process unit-test golden comparisons or a test-harness's own subprocess-capture, not a real end-to-end CLI-invocation-to-redirected-file check). This is reported as an open question, not a confirmed defect — routed to human verification below, since it cannot be exercised from this Linux sandbox and no currently-open gap (G-02-5/6/7) is scoped to catch it if it manifests only outside the test suite.

Both findings are additive to, not replacements for, the three human-confirmed round-4 gaps — they do not change this report's `gaps_found` status, and neither is presented as proven.

### Requirements Coverage

All 48 Phase-2 requirement IDs (`CLI-01…04,06,07,08,10`, `ENG-01…16`, `SNAP-01…07`, `REPORT-01…07`, `DIR-01…05`, `TRUST-03,05,08`, `DOC-01,02`) remain `Complete` and traceable in `.planning/REQUIREMENTS.md`, independently re-confirmed by `grep -cE` in this session (48/48 matches). `02-14`/`02-16` correctly declare `[BUILD-01, BUILD-05]` — Phase-1-owned IDs being restored, per the established 02-12/02-13 precedent the prior verification endorsed (not re-litigated here). `02-15` additionally and correctly declares `DIR-04`, its own Phase-2 ID, since it repairs `DIR-04`'s test coverage directly. No orphaned or lost requirement coverage found.

### Anti-Patterns Found

None. `grep -n -E "TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER"` across every file touched by the round-3 plans (`test_report_model.cpp`, `test_dir_pairing.cpp`, both new lint scripts, `ci.yml`) returned zero matches.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| `block_for()` has no dead code, no suppression | `grep -n "FAIL(\|pragma warning\|std::abort\|std::terminate" tests/unit/test_report_model.cpp` | 0 matches | ✓ PASS |
| All 11 report_model tests intact | `grep -c 'TEST_CASE("report_model'` | 11 | ✓ PASS |
| `lint_dead_code_after_fail.sh` clean on current tree | `bash scripts/lint_dead_code_after_fail.sh` | exit 0, "clean. Scanned 49 file(s)" | ✓ PASS |
| `lint_dead_code_after_fail.sh` fires on empty scan dir | modified copy pointed at an empty dir | exit 1, "yielded zero files" | ✓ PASS |
| `lint_dead_code_after_fail.sh` fires on real known-bad input | injected `FAIL()`-then-`static`-then-`return` fixture | exit 1, names `badtests/probe.cpp:3` | ✓ PASS |
| `lint_fixture_case_collisions.sh` clean on current tree | `bash scripts/lint_fixture_case_collisions.sh` | exit 0, "clean. Scanned 49 file(s)" | ✓ PASS |
| `lint_fixture_case_collisions.sh` fires on empty scan dir | modified copy pointed at an empty dir | exit 1, "yielded zero files" | ✓ PASS |
| `lint_fixture_case_collisions.sh` fires on real known-bad input | injected `"Case.snap.json"`/`"case.snap.json"` fixture | exit 1, names both spellings | ✓ PASS |
| Fixture ordering claim (byte-wise vs case-folded differ) | independent Python recomputation of `sorted(names)` vs `sorted(names, key=str.lower)` | differ at 4/5 positions | ✓ PASS |
| `ci.yml` diff is additions-only | `git diff --unified=0 8d4aa4a~1 8d4aa4a -- .github/workflows/ci.yml \| grep -c '^-[^-]'` | 0 | ✓ PASS |
| Required-check name intact, exactly once | `grep -c "name: lint (ENG-16 boundary)"` | 1 | ✓ PASS |
| Local Linux build unaffected | `cmake --build --preset x64-linux` | `ninja: no work to do` | ✓ PASS |
| Local Linux test suite | `ctest --test-dir build/x64-linux` | `100% tests passed, 0 tests failed out of 297` | ✓ PASS |
| CI run's actual job conclusions (fresh pull) | `gh run view 31946964023 --json headSha,conclusion,jobs` | matches SUMMARY exactly: lint success, x64-linux success, arm64-osx success, x64-windows-static-md failure (test step), x64-osx failure (unchanged, non-blocking), arm64-linux failure (unchanged, non-blocking) | ✓ PASS |
| Windows Test step's raw failure lines (fresh pull, not SUMMARY transcription) | `gh run view --job 95164409600 --log \| grep FAILED` | 7 distinct `FAILED:` lines at the exact file:line pairs 02-UAT.md records | ✓ PASS |
| arm64-osx Test #40 raw result (fresh pull) | `gh run view --job 95164409605 --log \| grep 'Test  #40'` | `Passed` | ✓ PASS |
| No `_setmode`/`_O_BINARY` guard anywhere in `src/` | `grep -rn "_setmode\|_O_BINARY\|O_BINARY" src/` | 0 matches | ⚠️ NOTED — routed to human verification, not asserted as a failure |

### Probe Execution

Not applicable — Phase 2 has no `scripts/*/tests/probe-*.sh` convention. The two new lint scripts (`lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`) are covered under Behavioral Spot-Checks above instead, since they are CI gates, not probes in the `references/verifier-wiring-patterns.md` sense.

### Human Verification Required

### 1. Colour renders as real styling in a Windows console

**Test:** Run `mediadiff compare`/`dir` with colour enabled in a real Windows `cmd.exe` or Windows Terminal session, exercising the `SetConsoleMode` `ENABLE_VIRTUAL_TERMINAL_PROCESSING` path.
**Expected:** ANSI-interpreted styling (real colour), not literal escape-sequence bytes.
**Why human:** Reachable for the first time in the phase's history — CI run `31946964023`'s Windows Build step succeeded and produced `mediadiff.exe` — but it has not been run. This sandbox is Linux-only. Record the mediadiff build sha alongside the result: if G-02-5 (newline handling) closes before this runs, a later build's console output could plausibly differ.

### 2. Confirm whether mediadiff's own stdout output (not just its test suite) is byte-identical on Windows

**Test:** On a real Windows build of `mediadiff.exe` (the same artifact from run `31946964023`), run `mediadiff compare <a> <b> --json > out.json` (and separately `--report md=out.md`) against fixed, small inputs, and byte-compare `out.json`/`out.md` against the same command's output on Linux/macOS for identical inputs. Also check whether the same `\r\n` pattern appears when `--json` is piped (not redirected to a file) into a consuming process.
**Expected:** Byte-identical output across platforms, per the project's own stated determinism contract. If `\r\n` line endings appear on Windows where Linux/macOS produce `\n`, this is a genuine product-level defect.
**Why human:** This verification pass traced the code path (`std::fputs(report.c_str(), stdout)` with no `_setmode`/`_O_BINARY` guard found anywhere in `src/`) and found a plausible, unconfirmed mechanism for exactly this outcome — see "New finding" above. It cannot be exercised from this Linux sandbox, and it is a genuinely open question, not a confirmed defect: Windows CRT default stdout text-mode behavior is a documented platform fact, not something observed directly in this session. None of the three currently-open gaps (G-02-5/6/7) is scoped to catch this if it manifests only in the product's actual CLI invocation rather than in the test suite's in-process golden checks or test-harness subprocess capture.

### Gaps Summary

**Two gaps are genuinely, independently confirmed closed this round** — not merely re-asserted from the prior report. `block_for()`'s dead-code shape is gone from the source (read directly, no compiler-specific suppression used, all 11 report_model tests intact), and CI run `31946964023`'s own raw Windows-build log (pulled fresh via `gh run view --job 95164409600 --log` in this session, not taken from any SUMMARY) shows zero C4702/C2220/C4996 and a successful Build step conclusion. Independently for G-02-4: the `test_dir_pairing.cpp` fixture is case-collision-free (re-verified by an independent Python computation, not by trusting the SUMMARY's arithmetic), and the arm64-osx job's own raw log shows test #40 passing and `100% tests passed out of 297`. Both new lint scripts were deliberately broken during this verification (pointed at empty directories, fed known-bad injected fixtures) and both correctly refused to report clean — their self-tests have real teeth, not just the appearance of teeth. The `ci.yml` change wiring them in is additions-only with the required-check name preserved exactly once and no escape hatch added.

**Three gaps remain open, all confirmed by this session's own fresh pull of the CI logs, none new and none caused by the round-3 work:** G-02-5 (Windows byte-level output divergence — 4 tests), G-02-6 (non-ASCII path mis-decoding on Windows — 1 test), and G-02-7 (exit-code and inspect-output behavioral divergence on Windows — 2 tests). All seven Windows test failures are first-ever executions of code paths the Windows leg had never reached before this run; none is a regression from the round-3 gap-closure work (verified: `src/cli/dir_pairing.cpp` untouched by any round-3 plan, and none of `02-14`/`02-15`/`02-16` touch `test_process_spawn.cpp`, `test_exit_codes.cpp`, or `test_explain_inspect.cpp`).

**Beyond re-confirming what the human's round-3 UAT already found, this verification surfaced two additional, unconfirmed but concretely-traced risks**: (1) the absence of a `.gitattributes` file, which plausibly explains the three golden-file mismatches independently of the pipe-capture hypothesis already recorded against G-02-5; and (2) a total absence of any `_setmode`/`_O_BINARY` binary-mode guard on `stdout` anywhere in `src/`, meaning the product's own `--json`/report stdout output — not just its test suite — may be subject to the same CRLF translation on Windows, which would be a direct violation of the project's stated byte-identical-`--json` determinism contract and is not covered by any currently-open gap. Both are reported honestly as open questions requiring a real Windows host to confirm, not as proven defects — but both are concrete enough that they should be checked before or alongside the G-02-5/6/7 diagnosis work, since a fix for G-02-5's test-level symptoms would not necessarily touch the product-level `stdout` path this finding describes.

**The phase is not closer to done than three open blockers indicate, and the two genuine closures are real progress, not merely claimed progress** — both were independently re-derived against the actual source and the actual CI logs in this session, not accepted on the strength of any SUMMARY.md's narrative.

---

_Verified: 2026-08-18T09:30:00Z_
_Verifier: Claude (gsd-verifier)_
