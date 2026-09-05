---
phase: 03-probe-layer-container-size
verified: 2026-09-05T21:15:00Z
status: gaps_found
score: 4/5 roadmap success criteria fully verified; 1/5 (SC5) still failed under real CI evidence
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "4/5 fully clean; SC5 gap-bearing"
  gaps_closed:
    - "WINDOWS.md #9 (ebml_scan.cpp NOMINMAX/std::max C2059 on MSVC) — closed on observed evidence: ebml_scan.cpp now compiles cleanly under MSVC in real CI run 33990099158 (x64-windows-static-md leg gets past this file; a DIFFERENT, later defect in main.cpp now blocks that leg — see gaps_remaining)."
    - "WINDOWS.md #10 (x64-linux golden drift vs CI's apt ffmpeg) — closed: 03-16 pinned fixture-synthesis ffmpeg by URL+SHA-256 and re-baselined goldens against the pinned build's real x64-linux CI output; x64-linux is now fully green (build (x64-linux): success, run 33990099158) with both trust06_idempotence cases Passed."
    - "WINDOWS.md #15 (macOS bash-3.2 check_corpus.sh mapfile crash, exit 127) — closed: 03-18's portable while-read rewrite is proven at runtime in run 33990099158; arm64-osx's 'Generate media fixture corpus' and 'Verify the fixture corpus is complete' steps both concluded success."
    - "Cross-leg corpus byte-identity ambiguity resolved (not closed as 'uniform', but resolved as 'designated'): 03-19 measured real per-leg digests (run 33983460934) showing arm64-osx diverges from x64-linux/x64-windows-static-md on 76/80 fixtures, and implemented the 'designated' policy (x64-linux only for byte-exact fixture-derived goldens, named+counted exclusion elsewhere, EXPECTED_EXCLUDED_COUNT=5) as a standing, non-silent CI gate. This is a scope narrowing accepted by the developer at 03-19's checkpoint, not a defect."
  gaps_remaining:
    - "SC5 is STILL FAILED. Independently re-ran `gh run view 33990099158` and `gh run view 33990099158 --log-failed` this round (not re-quoting 03-20-SUMMARY.md): of the 3 blocking legs (x64-linux, arm64-osx, x64-windows-static-md), only x64-linux concludes success. arm64-osx fails at Build: `tests/unit/test_ebml_scan.cpp:89:25: error: unused variable 'kClusterId' [-Werror,-Wunused-const-variable]` (WINDOWS.md #13, open) — confirmed the offending line is still present verbatim in the current working tree (`constexpr std::uint64_t kClusterId = 0x1F43B675;` at test_ebml_scan.cpp:89, never referenced elsewhere in the file). x64-windows-static-md fails at Build: `src/cli/main.cpp(297): error C3861: 'report_cli_error': identifier not found` (WINDOWS.md #16, open) — confirmed still present verbatim in the current working tree (line 297 calls `report_cli_error(...)` unqualified, inside `wmain`, which is outside `namespace mediadiff` closed at line 209; line 292 immediately above correctly qualifies the analogous call as `mediadiff::wide_to_utf8(...)`). Both defects are one-line fixes, both are explicitly out of every gap-closure plan's declared files_modified (03-20 deliberately did not touch source files), and both remain unfixed as of the current HEAD (aa57af4), which contains no source changes since the CI run's head commit (1b684de) — confirmed via `git diff --stat 1b684de aa57af4` (docs/ledger files only)."
    - "Only 2 of the 4 trust06_idempotence 'Passed' result lines this round's own must-haves require exist in the run log (both on x64-linux only) — independently confirmed via `gh run view 33990099158 --log --job <x64-linux job id> | grep trust06_idempotence`. arm64-osx's Test step never runs because its Build step fails first."
  regressions: []
gaps:
  - truth: "Encoding a fixture twice with identical settings and comparing under sw-encoder comes back clean as a CI release blocker (ROADMAP SC5)."
    status: failed
    reason: >
      The underlying test (tests/integration/test_trust06_idempotence.cpp) is real, substantive, and
      passes both locally (independently re-run this round: `ctest -R trust06_idempotence` → 2/2 Passed)
      and on the one CI leg that reaches it (x64-linux, run 33990099158, both cases Passed in the run
      log). The corpus-generation, ffmpeg-pinning, and cross-platform-digest infrastructure built across
      03-14/03-16/03-18/03-19 all work as designed and are proven on real, non-simulated CI runs this
      round independently re-queried via `gh run view` and `gh run view --log-failed` (not re-derived
      from any SUMMARY's narration).

      But SC5's own text — "comes back clean as a CI release blocker" — requires the release-blocking CI
      matrix itself to be green, and it is not. Of the 3 legs 03-20's own must_haves designate as
      blocking (x64-linux, arm64-osx, x64-windows-static-md), only 1 concludes success in the latest real
      run (33990099158, head 1b684de, current HEAD unchanged since):

        (a) arm64-osx fails at Build: `tests/unit/test_ebml_scan.cpp:89:25: error: unused variable
            'kClusterId' [-Werror,-Wunused-const-variable]` under AppleClang. GCC on the Linux legs does
            not flag this the same way. WINDOWS.md #13, open. Confirmed the unused constant is still
            present in the current working tree.

        (b) x64-windows-static-md fails at Build: `src/cli/main.cpp(297): error C3861: 'report_cli_error':
            identifier not found` — `report_cli_error` is declared in `namespace mediadiff`
            (src/cli/diagnostics.h:43); `main.cpp` closes that namespace at line 209, and `wmain`
            (lines 222-312) calls it unqualified at line 297, one line below a correctly-qualified sibling
            call (`mediadiff::wide_to_utf8(...)` at line 292). This defect only became reachable once
            03-17 fixed the earlier NOMINMAX/C2059 error that used to abort the Windows build before this
            line was ever compiled. WINDOWS.md #16, open. Confirmed still present verbatim.

      Both defects are one-token/one-line fixes, both are Phase-3 code (03-06's ebml_scan test file;
      main.cpp's wmain block, both #ifdef-gated so GCC/Clang legs never compile them), and both were
      deliberately left unfixed by 03-20, whose own declared files_modified were WINDOWS.md and
      03-VERIFICATION.md only (an intentional, honest scope boundary, not an oversight — 03-20-SUMMARY.md
      says so explicitly and this verification agrees that decision was correct plan hygiene). But their
      net effect on this verification's job is unambiguous: **SC5 is still false.** A release blocker
      that cannot build on 2 of its 3 required legs is not "wired into CI as a clean release blocker,"
      regardless of how solid the underlying test, the corpus pin, and the cross-platform digest
      machinery all are.

      This is real, measurable progress from the prior round (which had 0 of 3 blocking legs green and 2
      entirely different root causes — NOMINMAX and ffmpeg-version golden drift, both now genuinely fixed
      and closed in WINDOWS.md #9/#10) but the success criterion's truth value has not changed: it was
      false last round and it is false this round, for a different, narrower, and now well-understood
      reason.
    artifacts:
      - path: tests/unit/test_ebml_scan.cpp
        issue: "Line 89: `constexpr std::uint64_t kClusterId = 0x1F43B675;` is declared but never used in the file, tripping AppleClang's `-Werror,-Wunused-const-variable` on the arm64-osx (and x64-osx) legs and aborting the Build step before Test ever runs (WINDOWS.md #13, open)."
      - path: src/cli/main.cpp
        issue: "Line 297: `report_cli_error(...)` is called unqualified from inside `wmain`, which is outside `namespace mediadiff` (closed at line 209). MSVC's C3861 fires because the unqualified name cannot be found; GCC/Clang never compile this `#ifdef _WIN32` block so the defect was invisible on every non-Windows leg (WINDOWS.md #16, open)."
    missing:
      - "Fix tests/unit/test_ebml_scan.cpp:89 — either use kClusterId in a test case (there is likely a EBML Cluster-ID scenario this file's own header comment implies it was meant for) or remove the unused constant, so the arm64-osx/x64-osx AppleClang builds stop failing at Build."
      - "Fix src/cli/main.cpp:297 — qualify the call as `mediadiff::report_cli_error(...)`, matching the sibling call one line above, so the x64-windows-static-md leg's Build step succeeds."
      - "Re-run CI after both one-line fixes land and confirm: (1) all 3 blocking legs (x64-linux, arm64-osx, x64-windows-static-md) conclude Build success and reach Test; (2) all 4 trust06_idempotence result lines (2 cases x 2 of the legs that run tests, per 03-20's own must_haves — x64-linux and arm64-osx) are observed Passed in the run log, not merely absent from a failure list; (3) x64-windows-static-md's Test step also runs to completion (it has never yet been observed doing so on real CI, since Build has failed on every run to date for one reason or another)."
deferred: []
---

# Phase 3: Probe Layer, Container & Size Verification Report

**Phase Goal:** Real media enters the engine — one header pass and one packet sweep feed every
container, metadata and size check, plus the shared primitives that later phases consume instead
of recomputing.
**Verified:** 2026-09-05T21:15:00Z
**Status:** gaps_found
**Re-verification:** Yes — after gap-closure round 2 (plans 03-16 through 03-20)

> **Prior correction note preserved (03-20, 2026-09-05, source: real CI run 33951407521):** the
> verification report from gap-closure round 1 originally overstated corpus-verification cleanliness
> and misidentified CI's apt ffmpeg version. Both errors were corrected in that round's report body
> and are preserved in this document's git history; this round's evidence (run 33990099158) supersedes
> both the original claim and the round-1 correction, since the underlying defects (WINDOWS.md #9, #10)
> are now genuinely closed. See the round-1 correction note in this file's git history if the prior
> wording is needed for audit purposes.

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---|---|---|
| 1 | `mediadiff inspect` on MP4/MOV, MKV/WebM, MPEG-TS renders a complete container section | ✓ VERIFIED (unchanged, no regression from gap-closure round 2) | No round-2 plan touched the rendering path. Local test suite includes the container-family unit/integration tests; unaffected by round-2's ffmpeg-pinning, MSVC/bash portability, and CI-ledger scope. |
| 2 | Cross-container migration demotes cleanly; truncated/garbage input degrades to `skipped:unparsed_mechanism` with byte offset or exits 65; never crashes or silently passes | ✓ VERIFIED (closed in round 1, unaffected by round 2) | CR-01/CR-02 fix (03-13) unaffected by any of 03-16..03-20's declared files_modified; no regression risk. |
| 3 | `size.file`/`size.stream_bitrate`/`size.peak_bitrate`/`size.overhead` report rate economics from the packet scan alone, DTS-in-ticks windowing, cross-platform-identical | ✓ VERIFIED (closed in round 1, unaffected by round 2) | CR-04 fix (03-12) unaffected by round 2's scope. Note: "cross-platform-identical" here refers to the size.* *computation* (DTS-tick windowing, unaffected by which ffmpeg build synthesized the fixture); the separate, newly-discovered fact that the *fixture bytes themselves* are not byte-identical across CI legs (WINDOWS.md #12, #17) is a corpus-generation property, not a size.* computation defect, and is handled by the "designated" golden-scope policy, not by this SC. |
| 4 | Each file read exactly once; PROBE-10 packet-interval statistics shared as one probe-level primitive; peak memory per in-flight file bounded and asserted so `--threads N` is an honest memory knob | ✓ VERIFIED (closed in round 1, unaffected by round 2) | `pass_union`/`packet_budget` tests unaffected by round 2. |
| 5 | Encode-twice-and-compare comes back clean as a CI release blocker (TRUST-06); every check has both a triggering and a clean fixture pair (DOC-03); `ts_scan` cross-checked against TSDuck (TRUST-09) | ✗ FAILED — root cause narrowed and shifted again, SC still unmet | `ctest -R trust06_idempotence` (2/2, independently re-run this round, both locally and confirmed identical in the CI log), `ctest -R doc03` and `ctest -R ts_scan_golden` all pass locally. The ffmpeg-pinning and CI-corpus-wiring defects from round 1 (WINDOWS.md #9, #10) are genuinely closed this round, proven on real CI run 33990099158 (independently re-queried this round via `gh run view` and `gh run view --log-failed`, not re-quoted from any SUMMARY). But of the 3 legs 03-20's own must-haves designate as blocking, only x64-linux concludes success; arm64-osx fails at Build on an unused-const-variable AppleClang warning-as-error (WINDOWS.md #13) and x64-windows-static-md fails at Build on an unqualified `report_cli_error` call newly reachable now that the earlier NOMINMAX defect is fixed (WINDOWS.md #16). Both confirmed still present in the current working tree. |

**Score:** 4/5 roadmap success criteria fully verified this round (SC1-SC4, unchanged from round 1); SC5 remains unmet, for a narrower, well-diagnosed, and well-documented reason than either prior round.

### Gap-Closure Verification (Prior Round's Remaining Gap: SC5)

| Sub-issue | Prior Status | This Round | Evidence |
|---|---|---|---|
| ffmpeg supply-chain integrity (D-GAP-01) | not addressed | ✓ **CLOSED** | 03-16: `scripts/ffmpeg_pin.json` (30 lines, 4 `sha256` entries) + `scripts/install_pinned_ffmpeg.sh` (314 lines, checksum-verifying, no unpinned fallback) wired into all 5 CI legs (`grep -c install_pinned_ffmpeg.sh .github/workflows/ci.yml` = 2, install + PATH export). |
| WINDOWS.md #9 (MSVC NOMINMAX/C2059) | open | ✓ **CLOSED** | 03-17: `CMakeLists.txt:273` `target_compile_definitions(${target} PRIVATE NOMINMAX)` applied via `mediadiff_apply_platform_definitions` to all 4 first-party targets; `src/probe/ebml_scan.cpp:3` now includes `<algorithm>` explicitly. Confirmed in current working tree. Real CI run 33990099158 shows `ebml_scan.cpp.o` compiling cleanly on x64-windows-static-md — the Build step now fails at a *different, later* file (main.cpp:297), proving this specific defect is gone rather than merely masked. |
| WINDOWS.md #10 (x64-linux golden drift) | open | ✓ **CLOSED** | 03-16 re-baselined goldens against the pinned build's real x64-linux CI output. Confirmed: x64-linux leg of run 33990099158 concludes `success`, with 616-617/622 (`trust06_idempotence`) both `Passed` observed directly in the run log this round. |
| WINDOWS.md #15 (macOS bash-3.2 `mapfile` crash) | open (discovered mid-round) | ✓ **CLOSED** | 03-18's while-read rewrite (`scripts/lint_bash4_builtins.sh`, 239 lines, permanent lint gate) proven at runtime: arm64-osx's corpus-generation and corpus-verification steps both conclude success in run 33990099158. |
| Cross-leg corpus byte-identity (D-GAP-01 follow-on question) | unknown/unmeasured | ✓ **RESOLVED as "designated"** | 03-19 measured real per-leg digests (run 33983460934): arm64-osx diverges from x64-linux/x64-windows-static-md on 76/80 fixtures. Developer chose the `designated` policy at the plan's own Task 2 checkpoint (locked decision, not this verifier's call to relitigate): byte-exact fixture-derived goldens run on x64-linux only; every other leg names the 5 excluded tests and asserts `EXPECTED_EXCLUDED_COUNT=5` (confirmed at `.github/workflows/ci.yml:370-382`) so the exclusion cannot silently widen. `tests/golden/CORPUS_DIGEST.txt` (81 lines) committed as the designated leg's drift gate. This narrows test *coverage*, never assertion *strength* — WINDOWS.md #17, open, tracked as an accepted, visible limitation, not a defect. |
| WINDOWS.md #13 (AppleClang unused-const-variable, arm64-osx/x64-osx) | not discovered until this round | ✗ **STILL OPEN** | Confirmed present in current working tree (`tests/unit/test_ebml_scan.cpp:89`). Blocks arm64-osx's Build step, which is one of 03-20's own 3 designated blocking legs. |
| WINDOWS.md #16 (unqualified `report_cli_error`, x64-windows-static-md) | not discovered until this round (newly reachable after #9's fix) | ✗ **STILL OPEN** | Confirmed present in current working tree (`src/cli/main.cpp:297`). Blocks x64-windows-static-md's Build step, the third of 03-20's 3 designated blocking legs. |

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `scripts/ffmpeg_pin.json` | Pinned ffmpeg build set: URL + SHA-256 per runner key | ✓ VERIFIED | 30 lines, 4 `sha256` fields present. |
| `scripts/install_pinned_ffmpeg.sh` | Checksum-verifying downloader, no unpinned fallback | ✓ VERIFIED | 314 lines (min_lines: 80 satisfied). WR-02 (code review) notes a latent, currently-unreached path-traversal gap in its dead `tar.xz` branch — not a blocker, all 4 current pin entries use `"archive": "zip"`, which CPython sanitizes natively. |
| `scripts/corpus_digest.sh` | Deterministic per-fixture SHA-256 listing | ✓ VERIFIED | 102 lines (min_lines: 40 satisfied). |
| `scripts/lint_bash4_builtins.sh` | Permanent bash-3.2 portability gate | ✓ VERIFIED, ⚠️ 2 bypasses documented | 239 lines (min_lines: 90 satisfied); wired into the required lint job. Code review (03-REVIEW.md WR-01, this round's own numbering) confirms by direct AWK-matcher execution that `shopt -s <other> globstar` and split-token `declare -r -A` both bypass detection — a real gap in a gate built specifically to prevent silent misses of this class, but not a Phase-3-goal blocker (no such construct exists in the repo today). |
| `tests/golden/CORPUS_DIGEST.txt` | Committed per-fixture digest listing, designated-leg drift gate | ✓ VERIFIED | 81 lines, `CORPUS_DIGEST_SUMMARY=` present. |
| `CMakeLists.txt` (NOMINMAX) | Windows macro suppression applied once, to all first-party targets | ✓ VERIFIED | Line 273, via `mediadiff_apply_platform_definitions`, not per-file. |
| `src/probe/ebml_scan.cpp` | Compiles under MSVC | ✓ VERIFIED (this specific defect) | `#include <algorithm>` present at line 3; confirmed compiling cleanly in CI run 33990099158 (a later, unrelated file now blocks the same leg). |
| `src/report/junit.cpp` (`xml_escape`) | Backslash-doubling matching `sanitize_for_display` | ✓ VERIFIED | Code review (03-REVIEW.md) confirms lines 122-130 add `case '\\': out += "\\\\"; break;`, with 2 new targeted regression tests in `tests/unit/test_junit.cpp`. Round-1's WR-01 (junit.cpp) finding is closed and not re-raised. |
| `tests/unit/test_ebml_scan.cpp` (kClusterId) | Should not trip `-Wunused-const-variable` on any first-party toolchain | ✗ STILL BROKEN | Line 89's `kClusterId` constant remains genuinely unused; blocks arm64-osx/x64-osx Build under AppleClang. |
| `src/cli/main.cpp` (wmain) | Every call inside `wmain` should be reachable/well-qualified for MSVC | ✗ STILL BROKEN | Line 297's `report_cli_error(...)` call remains unqualified; blocks x64-windows-static-md Build under MSVC. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `scripts/ffmpeg_pin.json` | `scripts/install_pinned_ffmpeg.sh` | URL + SHA-256 verification before any corpus generation | ✓ WIRED | Confirmed: `install_pinned_ffmpeg.sh` reads the pin file, verifies checksum, exports `MEDIADIFF_FFMPEG`; no rolling-channel fallback found in `.github/workflows/ci.yml`. |
| `.github/workflows/ci.yml` (all 5 legs) | `scripts/check_corpus.sh` / `scripts/corpus_digest.sh` | Unconditional pre-Configure corpus generation + verification + digest steps | ✓ WIRED, PROVEN AT RUNTIME | Real CI run 33990099158: x64-linux and arm64-osx both show these steps concluding success (arm64-linux fails downstream at NuGet feed registration — a separate, non-blocking, pre-existing infra issue, WINDOWS.md #11 — not at corpus generation). |
| `tests/golden/CORPUS_DIGEST.txt` | The 5 byte-exact fixture-derived golden tests | Named+counted CTest `-E` exclusion on non-designated legs | ✓ WIRED | `.github/workflows/ci.yml:370-382`: `EXPECTED_EXCLUDED_COUNT=5` asserted against unfiltered-vs-filtered `ctest -N` totals; confirmed present in the workflow file. |
| `scripts/gen_corpus.sh` → `tests/fixtures/*` → `ctest` | The `Test` step's exit code → the required status check | 3-step unconditional CI group before `Configure`, now on a checksum-verified ffmpeg | ⚠️ WIRED ON x64-linux ONLY, DOWNSTREAM RED ON THE OTHER 2 BLOCKING LEGS | x64-linux: Test runs, 616/617 `trust06_idempotence` both Passed. arm64-osx, x64-windows-static-md: Build fails before Test ever runs, for reasons (#13, #16) unrelated to the corpus/ffmpeg-pin chain itself. |

### Behavioral Spot-Checks (this round, independently re-run against real CI and the current working tree — not re-quoting any SUMMARY)

| Behavior | Command | Result | Status |
|---|---|---|---|
| Real CI run's per-job conclusions (blocking-leg status) | `gh run view 33990099158 --repo dkastsenich/mediadiff --json headSha,status,conclusion,jobs` | `headSha=1b684de`, overall `conclusion=failure`; `build (x64-linux)`: success; `build (arm64-osx)`: failure; `build (x64-windows-static-md)`: failure; `build (x64-osx)`: failure (non-blocking); `build (arm64-linux)`: failure (non-blocking, NuGet feed); `lint (ENG-16 boundary)`: success | ✓ CONFIRMS only 1/3 blocking legs green |
| x64-windows-static-md Build failure root cause | `gh run view 33990099158 --log-failed --job <id> \| grep -iE error` | `src/cli/main.cpp(297): error C3861: 'report_cli_error': identifier not found` | ✓ CONFIRMS WINDOWS.md #16 verbatim |
| arm64-osx Build failure root cause | `gh run view 33990099158 --log-failed --job <id> \| grep -iE error` | `tests/unit/test_ebml_scan.cpp:89:25: error: unused variable 'kClusterId' [-Werror,-Wunused-const-variable]` | ✓ CONFIRMS WINDOWS.md #13 verbatim |
| arm64-linux failure is infra, not corpus/build | `gh run view 33990099158 --json jobs -q '.jobs[] \| select(.name=="build (arm64-linux)") \| .steps[] \| select(.conclusion=="failure")'` | `"Register vcpkg NuGet feed (read-write, trusted runs only)"` — pre-Configure infra step | ✓ CONFIRMS non-blocking classification |
| Both defect lines still present in current working tree (not fixed since the CI run) | `sed -n '297p' src/cli/main.cpp`; `sed -n '89p' tests/unit/test_ebml_scan.cpp` | `report_cli_error(...)` still unqualified; `constexpr std::uint64_t kClusterId = 0x1F43B675;` still present, unreferenced | ✓ CONFIRMS defects remain unfixed at current HEAD |
| No source changed between the CI run's head and current HEAD | `git diff --stat 1b684de aa57af4` | Only `.planning/*` docs/ledger files changed (ROADMAP.md, STATE.md, WINDOWS.md, 03-*-SUMMARY.md, 03-REVIEW*.md, 03-VERIFICATION.md) | ✓ CONFIRMS CI evidence at 1b684de still applies to current code |
| `trust06_idempotence` passes on the one leg that reaches it, independently re-run locally | `ctest --test-dir build/x64-linux -R trust06_idempotence` | 2/2 Passed | ✓ PASS (matches CI log) |
| `trust06_idempotence` Passed-line count in the real run log | `gh run view 33990099158 --log --job <x64-linux id> \| grep trust06_idempotence` | 2 `Passed` lines found (both on x64-linux); 0 on arm64-osx (Test step never reached) | ✓ CONFIRMS only 2/4 required lines exist, matching 03-20's own honest self-assessment |
| Code review WR-01/WR-02 findings still open in current tree | Read `03-REVIEW.md` frontmatter + body | `critical: 0, warning: 2, info: 4` | ✓ CONFIRMS review's own count, cross-checked against file content |
| Requirements coverage: all 23 Phase-3 IDs present | `grep -E "Phase 3" .planning/REQUIREMENTS.md` | All 23 IDs (`PROBE-01/02/04-10, CONT-01-09, SIZE-01, DIR-06, TRUST-06/09, DOC-03`) marked `Complete` | ⚠️ SEE REQUIREMENTS COVERAGE NOTE BELOW — REQUIREMENTS.md marks TRUST-06 "Complete" despite SC5 remaining false; a discrepancy worth flagging, not silently accepting |

### Probe Execution

No `scripts/*/tests/probe-*.sh`-style probes declared by this phase; N/A — skipped.

### Requirements Coverage

All 23 requirement IDs declared across the phase's 20 plans (`PROBE-01/02/04/05/06/07/08/09/10,
CONT-01…09, SIZE-01, DIR-06, TRUST-06/09, DOC-03`) are present in REQUIREMENTS.md's Phase-3 mapping.
Cross-referenced against `.planning/ROADMAP.md`'s Phase-3 requirement list — identical 23-id set, no
orphans, no omissions.

| Requirement | Status | Evidence |
|---|---|---|
| PROBE-01, 02, 04, 05, 06, 07, 08 | ✓ SATISFIED (unchanged) | Untouched by gap-closure round 2. |
| PROBE-09 | ✓ SATISFIED | Unaffected by round 2 (CR-01/CR-02 fix from round 1 stands). |
| PROBE-10 | ✓ SATISFIED (unchanged) | `pass_union` tests unaffected. |
| CONT-01, 07, 08, 09 | ✓ SATISFIED (unchanged) | Untouched by round 2. |
| CONT-02, CONT-03, CONT-04, CONT-06 | ✓ SATISFIED | 03-17 touched these for the JUnit backslash-doubling fix and Windows macro suppression; both confirmed present and tested this round (test_junit.cpp's 2 new cases). |
| CONT-05 | ✓ SATISFIED (unchanged) | Round-1 CR-01/CR-02 fix stands, untouched by round 2. |
| SIZE-01, DIR-06 | ✓ SATISFIED (unchanged) | Round-1 CR-04 fix stands, untouched by round 2. |
| TRUST-06 | ⚠️ **REQUIREMENTS.md says "Complete"; actual CI evidence says NOT wired as a green release blocker.** | REQUIREMENTS.md's Phase-3 table marks TRUST-06 `Complete` (a pre-existing categorization, not something round 2 changed), but this verification's own SC5 finding directly contradicts that: the CI matrix that would make TRUST-06 a functioning release blocker is red on 2 of 3 blocking legs as of the latest real run (33990099158) and current HEAD. Flagging rather than silently accepting REQUIREMENTS.md's checkbox — the underlying test and corpus infrastructure are real and substantive, but "wired into CI as a release blocker" (the requirement's own operative claim, mirrored in SC5) is not yet true end-to-end. |
| TRUST-09 | ✓ SATISFIED locally, same CI-matrix caveat | `ts_scan_golden` passes on x64-linux (the designated leg); excluded-by-name-and-count elsewhere per the accepted `designated` policy (WINDOWS.md #17). |
| DOC-03 | ✓ SATISFIED locally, same CI-matrix caveat | 30/30 checks covered locally; the coverage gate itself is real and registry-driven, unaffected by the 2 open Build-step defects. |

No ORPHANED requirements found.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| `tests/unit/test_ebml_scan.cpp` | 89 | `constexpr std::uint64_t kClusterId` declared, never used | 🛑 Blocker (for SC5 / arm64-osx & x64-osx build parity) | Confirmed present in current working tree; confirmed as the exact AppleClang `-Werror` failure in real CI run 33990099158 (WINDOWS.md #13, open). |
| `src/cli/main.cpp` | 297 | `report_cli_error(...)` called unqualified from a scope outside `namespace mediadiff` | 🛑 Blocker (for SC5 / x64-windows-static-md build parity) | Confirmed present in current working tree; confirmed as the exact MSVC C3861 failure in real CI run 33990099158 (WINDOWS.md #16, open). Only reachable now that 03-17 fixed the earlier NOMINMAX defect that used to abort the build first. |
| `scripts/lint_bash4_builtins.sh` | 133, 153 | `shopt -s <other> globstar` and split-token `declare -r -A` both bypass the gate's own detection regexes | ⚠️ Warning | Confirmed by direct AWK-matcher execution in 03-REVIEW.md (WR-01, this round's numbering). Not exploited anywhere in the current repo; a real gap in a gate purpose-built to prevent exactly this class of silent miss. |
| `scripts/install_pinned_ffmpeg.sh` | 206-211 | `tarfile.extractall()` on the `tar.xz` branch has no path-traversal member validation | ⚠️ Warning | Confirmed dead code today (all 4 pin entries use `"archive": "zip"`, which CPython's `zipfile` sanitizes natively); live, reachable code for a future archive kind (03-REVIEW.md WR-02). |
| `src/cli/options.cpp` | 309-361 | `resolve_probe_timeout_ms`/`resolve_probe_memory_budget_mb` re-parse CLI11-already-validated text with `std::stoll` | ℹ️ Info | Pre-existing, carried forward verbatim from round-1 review's own IN-01; untouched by round 2. |
| `scripts/lint_bash4_builtins.sh` | 128-131 | `mapfile`/`readarray` check can false-positive on a quoted string literal mentioning those words | ℹ️ Info | Confirmed by direct test in 03-REVIEW.md (IN-02). Not presently triggered by any file under `scripts/`. |
| `scripts/lint_bash4_builtins.sh` | 161-217 | Self-test control clause exercises only 1 of the script's 6 flagged-construct checks | ℹ️ Info | Confirmed in 03-REVIEW.md (IN-03); the two WR-01 gaps above are invisible to the self-test as written. |
| `scripts/install_pinned_ffmpeg.sh`, `scripts/corpus_digest.sh` | various | `compute_sha256` and its preflight tool-check duplicated verbatim between the two scripts | ℹ️ Info | Confirmed in 03-REVIEW.md (IN-04); deliberate per both scripts' own header comments, but a future-drift risk. |

**Debt-marker gate:** No `TBD`/`FIXME`/`XXX` found in any file touched by round-2's 5 gap-closure plans
(checked this round via direct grep against each plan's declared `key-files`) — clean.

### Human Verification Required

None. Every finding above — including the two still-open, CI-blocking defects — was resolved
programmatically: by independently re-querying the real CI run and its failure logs via `gh run view`
/ `gh run view --log-failed` (a different invocation than any prior round used, targeting job-level
step failures directly rather than the whole-run summary), by reading the exact source lines those
logs cite in the current working tree, and by confirming no source has changed between the CI run's
head commit and current HEAD via `git diff --stat`.

### Gaps Summary

Gap-closure round 2 delivered real, substantial, independently-verified progress on SC5. Two
significant, previously-open defects (WINDOWS.md #9 — MSVC NOMINMAX; #10 — x64-linux golden drift) are
genuinely closed this round, each confirmed via a *specific, observed CI step conclusion*, not a
committed-change inference: `ebml_scan.cpp` now compiles cleanly under MSVC, and x64-linux's Build and
Test steps both conclude success with `trust06_idempotence`'s two cases directly observed `Passed` in
the run log. A third, mid-round-discovered defect (WINDOWS.md #15 — macOS bash-3.2 `mapfile` crash) is
also closed and proven at runtime. The cross-platform corpus-identity question the pinning work raised
is resolved with a deliberate, developer-approved, non-silent "designated" scope narrowing (WINDOWS.md
#17) rather than left open or quietly assumed.

**But SC5 itself is still false, and this round's own evidence says so plainly — 03-20-SUMMARY.md
reports this itself, and this verification independently confirms it via a separate `gh` query this
round.** Of the 3 legs the gap-closure work itself designates as blocking, only x64-linux is green.
Two new (to this specific investigation depth) defects — both one-line fixes, both outside any
gap-closure plan's declared scope, both confirmed present in the current working tree — keep the other
2 blocking legs red:

1. **`tests/unit/test_ebml_scan.cpp:89`** — an unused `constexpr` fails AppleClang's
   `-Werror,-Wunused-const-variable`, blocking arm64-osx's Build step (WINDOWS.md #13).
2. **`src/cli/main.cpp:297`** — an unqualified `report_cli_error` call fails MSVC's name lookup,
   blocking x64-windows-static-md's Build step (WINDOWS.md #16). This defect was invisible until 03-17's
   NOMINMAX fix let the Windows build reach this line for the first time.

Neither defect touches TRUST-06's own logic, the ffmpeg pin, the corpus generation/digest machinery, or
any of round 2's actual deliverables — they are pre-existing Phase-3 source defects (03-06's test file;
main.cpp's Windows-only `wmain` path) that real CI has now progressed far enough to expose, exactly as
WINDOWS.md #9/#10 were before them. This is the third consecutive round in which fixing one blocking-CI
defect has revealed the next one behind it — a pattern worth naming explicitly for whoever plans the
next round: **this phase's CI matrix has never yet had all 3 blocking legs reach and pass Test in the
same run.** x64-windows-static-md in particular has never once been observed reaching its Test step at
all across all three real CI runs referenced in this phase's verification history
(33951407521, 33983460934, 33990099158).

**What must happen before Phase 3 can pass:**
1. Fix `tests/unit/test_ebml_scan.cpp:89` (use or remove the unused `kClusterId` constant).
2. Fix `src/cli/main.cpp:297` (qualify as `mediadiff::report_cli_error(...)`).
3. Re-run CI and confirm, from the run log directly: all 3 blocking legs conclude Build success and
   reach Test; all 4 required `trust06_idempotence` Passed lines are observed (2 on x64-linux, 2 on
   arm64-osx); and — since this has never yet happened on any real run — x64-windows-static-md's Test
   step itself concludes and its own `trust06_idempotence` (or platform-equivalent) results are
   observed, not merely inferred from Build succeeding.

Both fixes are small and low-risk; neither requires reopening any of round 2's actual work (ffmpeg
pinning, bash-3.2 portability, corpus-digest policy, JUnit escaping) or round 1's work (CR-01/CR-02,
CR-04). No override is suggested for SC5 — the gap is real, current, independently reproducible via
`gh run view`, and its two remaining causes are now named precisely enough that the next round should
be able to close it in one plan.

---

_Verified: 2026-09-05T21:15:00Z_
_Verifier: Claude (gsd-verifier)_
