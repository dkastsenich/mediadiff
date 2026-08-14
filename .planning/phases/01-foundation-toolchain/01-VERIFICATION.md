---
phase: 01-foundation-toolchain
verified: 2026-08-15T00:00:00Z
status: human_needed
score: 4/5 criteria fully verified (1 partially satisfied — well-posed concern)
behavior_unverified: 0
overrides_applied: 0
human_verification:
  - test: "Criterion 5, color-rendering clause: run a colorized `mediadiff` command in Windows Terminal and cmd.exe once CLI-08 (Phase 2) ships fmt/ANSI styling, and confirm NO_COLOR/--no-color/--ascii behave as specified."
    expected: "This cannot be judged in Phase 1: the codebase emits zero styled/ANSI output today (`grep` for `NO_COLOR`, `fmt::style`, `fmt::color`, and raw ANSI escapes in `src/` all return empty). The VT-enablement mechanism itself (`SetConsoleMode`) is mechanically verified — see criterion 5 detail below — but there is nothing to render yet."
    why_human: "Product decision, not a code-verification question: either accept that Phase 1 satisfies only the non-ASCII-path half of criterion 5 and the roadmap wording is broader than the phase's own scoped decision D-04/CLI-09 (see 01-CONTEXT.md 'Out of scope'), or hold criterion 5 open until Phase 2's CLI-08 ships and re-verify then."
  - test: "BUILD-06 fork-PR cache-read behavior — open a PR from an actual fork and confirm the vcpkg cache is read-only via `github.token` while `VCPKG_PAT_TOKEN`-gated write is skipped."
    expected: "Per the workflow's own fork-guard expression (`.github/workflows/ci.yml` cache_gate step), a fork-originated `pull_request` run should register the NuGet feed read-only and the write step should not execute."
    why_human: "No fork-originated PR exists yet against dkastsenich/mediadiff; 01-05-SUMMARY.md itself records this as unproven in-session. This is a real gap in *evidence*, not in code — the guard logic is present and statically correct, but has never actually executed the branch it exists for."
---

# Phase 1: Foundation & Toolchain Verification Report

**Phase Goal:** A single static `mediadiff` binary builds reproducibly and runs on Linux, macOS and Windows, with every toolchain decision the later phases depend on already made and recorded.
**Verified:** 2026-08-15
**Status:** human_needed
**Re-verification:** No — initial verification

## Summary

Four of the five roadmap success criteria are fully verified against the live codebase and real CI evidence (not SUMMARY.md narration). The fifth is genuinely and honestly **not fully testable in Phase 1 as currently scoped** — half of it (non-ASCII paths) is verified on real Windows hardware, the other half (color rendering) has literally no code to observe because color output is CLI-08, explicitly deferred to Phase 2. This is a finding about the roadmap criterion's phrasing, not a code defect, and is routed to human judgment rather than a generous pass. One structurally-blind-check anti-pattern was found and is documented below (not blocking — its safety property still holds today, but its diagnostic is factually wrong).

## Criterion-by-Criterion Verdict

### 1. `mediadiff --version` runs from a clean checkout on Linux/macOS/Windows, printing tool version, linked FFmpeg library versions, and enabled feature set with `vmaf` absent by default

**Status: ✓ VERIFIED**

Evidence, gathered independently (not from SUMMARY claims):

- **Live rebuild on this host** (x64-linux, GCC 13.3.0, satisfies "GCC ≥ 12"):
  ```
  $ cmake --preset x64-linux && cmake --build --preset x64-linux
  ... All requested installations completed successfully ...
  [1/2] Building CXX object CMakeFiles/mediadiff.dir/src/cli/main.cpp.o
  [2/2] Linking CXX executable mediadiff
  $ ./build/x64-linux/mediadiff --version
  mediadiff 0.1.0
  libavcodec 62.28.100 (built against 62.28.100)
  libavformat 62.12.100 (built against 62.12.100)
  libavutil 60.26.100 (built against 60.26.100)
  license: LGPL version 2.1 or later
  features:
  ```
  `features:` is empty — `vmaf` correctly absent by default (BUILD-09). No `cuda` mentioned anywhere (v2 requirement, correctly never surfaced).

- **"Clean checkout" reading.** On this exact question, CI evidence is stronger than a local rebuild: `.github/workflows/ci.yml:72-78` runs `actions/checkout@v4` with `submodules: true` and full history on every job, on a fresh ephemeral GitHub-hosted runner, for every push. That genuinely *is* a clean-checkout build — not merely "builds in CI from a fresh runner" as a weaker echo of the phrase, but the literal clean-checkout-and-build the criterion asks for, repeated on every commit. Run [31849289102](https://github.com/dkastsenich/mediadiff/actions/runs/31849289102) (current `main` HEAD, `f90fbdc`) is green on all three required platforms:
  - `build (x64-linux)` — success, `ubuntu-24.04`, GCC
  - `build (arm64-osx)` — success, `macos-15`, AppleClang
  - `build (x64-windows-static-md)` — success, `windows-2022`, MSVC v143 (VS 17.0 pinned per 01-05-SUMMARY.md)

  All three legs run the `integration.version_output` test, which independently regex-asserts all four required `--version` fields against the real spawned binary (`tests/integration/test_version_output.cpp`), and it passes on all three:
  ```
  build (x64-windows-static-md) Test  9/11: integration.version_output ... Passed
  build (arm64-osx)             Test  9/11: integration.version_output ... Passed
  build (x64-linux)             (confirmed locally, see above)
  ```
  This is a materially stronger claim than "runs from a clean checkout on Linux only, trust the summaries for the rest" — the exact same assertion is independently exercised on all three required toolchains by real, currently-green CI.

- **BUILD-09 (`vmaf` absent by default) asserted against rendered output, not the CMake option**: `tests/integration/test_version_output.cpp`'s `vmaf_absent` case reads the actual `features:` line from stdout and fails if `vmaf` or `cuda` appear — passes on all three platforms in the same run.

**Verdict: fully satisfied**, with the "clean checkout" phrasing interpreted as CI's own fresh-runner checkout, which is the correct reading — no developer machine has done a from-scratch `git clone --recurse-submodules` build outside CI, but that is not what the criterion requires; CI's checkout is not weaker evidence, it is the same claim satisfied more times and on more platforms than a single local run could be.

### 2. Release artifact is one static binary per platform, runs with no FFmpeg installed, and the build fails rather than silently linking GPL FFmpeg

**Status: ✓ VERIFIED**

- **No FFmpeg shared-library dependency**, confirmed directly on this host:
  ```
  $ ldd build/x64-linux/mediadiff
      linux-vdso.so.1
      libstdc++.so.6 => ...
      libgcc_s.so.1 => ...
      libc.so.6 => ...
      libm.so.6 => ...
      /lib64/ld-linux-x86-64.so.2
  $ ldd build/x64-linux/mediadiff | grep -Ei 'libav|libsw'
  NONE FOUND (good)
  ```
  Matches the project's own documented definition of "static binary" (statically linked FFmpeg/deps, dynamic CRT/libstdc++ — the standard, honest reading per this project's own research notes, not a fully-static ELF).
- **Runs with FFmpeg absent from PATH**: `env -i PATH=/usr/bin:/bin ./build/x64-linux/mediadiff --version` exits 0 with full output (01-02-SUMMARY.md, reproduced in spirit here via the `static_link` CTest, which passed: `ctest --preset x64-linux -R static_link` → 100% passed, 0 failed).
- **`static_link` check is proven to discriminate, not just to pass vacuously**: 01-02-SUMMARY.md documents a negative control — a trivial binary force-linked against `libavcodec.so.60` was run through the same script and it correctly `FATAL_ERROR`'d, naming the three transitively-pulled FFmpeg libraries. I did not re-run this negative control myself (it requires building a throwaway probe binary), but the check's source (`tests/integration/check_static_link.cmake`) does `FATAL_ERROR` on any `libav*`/`libsw*` match rather than a narrower single-library check, which is consistent with the documented negative-control result.
- **GPL fast-fail is real, not aspirational**, verified by reading the actual mechanism (not trusting the description):
  - Configure-time: `CMakeLists.txt:24-36` does `find_library` for `x264`/`x265`/`xvidcore` in the resolved vcpkg install tree and `FATAL_ERROR`s if any resolve.
  - Runtime (the primary, load-bearing check per D-03): `tests/unit/test_license.cpp` exact-matches `avutil_license()`/`avcodec_license()`/`avformat_license()` against `"LGPL version 2.1 or later"` and is fail-first proven — it `REQUIRE_FALSE`s against `"GPL version 2 or later"`, `"GPL version 3 or later"`, `"LGPL version 3 or later"`, and `"nonfree and unredistributable"`, explicitly the case that a naive substring-`"GPL"` check would wrongly accept. This test passes: `unit."Linked FFmpeg reports the expected LGPL license"` → Passed, confirmed in a live re-run on this host.

**Verdict: fully satisfied**, both halves (no-FFmpeg-runtime and GPL-fail-fast) verified against actual mechanism, not description.

### 3. Two machines resolving the same manifest get the same dependency set; FFmpeg baseline and `expected<T,E>` are explicit recorded decisions

**Status: ✓ VERIFIED**

- `.planning/PROJECT.md` Key Decisions table (read directly, not summarized) contains both required rows verbatim:
  - FFmpeg baseline pinned to `version: "8.1"`, `port-version: 4`, via `vcpkg.json overrides`, with rationale (avoid ambiguous red builds on an 8-day-old major release; save the 8→9 bump as a deliberate TRUST-08/TRUST-04 exercise) and known cost (swscale float→rational rewrite will shift Phase-7 SSIM baselines at the eventual bump) — satisfies BUILD-10 as a *recorded decision*, not an incidental vcpkg-baseline consequence.
  - `mediadiff::expected<T,E>` aliasing `tl-expected` 1.3.1 in a single header (`src/util/expected.h`), with rationale (`std::expected` is C++23, project targets C++20) — satisfies BUILD-07.
- **Reproducibility across machines** is structurally proven, not merely asserted: `vcpkg.json`'s `builtin-baseline` plus the `ffmpeg` `overrides` entry pin every dependency to an exact vcpkg-tree commit; the vcpkg submodule itself is pinned to the same SHA (`105fdc246ba9a28b0789284217b0d1120446d43f`, confirmed via `git submodule status` on this host). The strongest available evidence that this actually produces an identical package set across machines is that three structurally different OS/runner images (ubuntu-24.04, macos-15, windows-2022) each independently resolve and restore **exactly 18 packages** from the shared NuGet cache in the same CI run (see criterion 4 below) — if the manifest resolution were non-deterministic across machines, this cross-platform package-count agreement would not hold.

**Verdict: fully satisfied.**

### 4. CI is green across the 3-OS matrix with warnings-as-errors, and a repeat run restores vcpkg binaries from cache instead of rebuilding FFmpeg

**Status: ✓ VERIFIED**

- **Green matrix, current HEAD**: run [31849289102](https://github.com/dkastsenich/mediadiff/actions/runs/31849289102) on commit `f90fbdc` (current `main` tip) — `lint (ENG-16 boundary)`, `build (x64-linux)`, `build (arm64-osx)`, `build (x64-windows-static-md)` all `success`; `build (x64-osx)` and `build (arm64-linux)` `failure`, exactly as decision D-06 designs (2 advisory, non-blocking legs). Confirmed identically on two prior runs (`e4d8f15`, `51d01e5`) — this is a stable pattern, not a one-off.
- **Merge gate correctly scoped to the blocking set only**: `gh api repos/dkastsenich/mediadiff/rules/branches/main` returns a `required_status_checks` ruleset (id `20862843`, active) naming exactly `build (arm64-osx)`, `build (x64-windows-static-md)`, `build (x64-linux)`, `lint (ENG-16 boundary)` — the two advisory legs are absent from the required list, matching D-06 and BUILD-05's own note verbatim.
- **Warnings-as-errors is live, not merely declared**: `.github/workflows/ci.yml` never sets `-Wall`/`-Werror`/`/W4`/`/WX` itself (`grep -Ec` returns 0, confirmed) — it is inherited from `CMakeLists.txt`'s per-target `mediadiff_apply_warnings()`, and the CI build step actually compiles first-party sources on every green run (`Building CXX object CMakeFiles/mediadiff.dir/src/cli/main.cpp.o` present in the run 31849289102 log), so warnings-as-errors was live during a real, successful compile, not skipped by an incremental cache hit.
- **Cache restore, not rebuild, directly evidenced from real vcpkg output** (this is the strongest evidence in the whole report — it is vcpkg's own restore-output line, exactly what BUILD-06's own recorded assumption requires, not a wall-clock inference):
  ```
  build (x64-linux)            Restored 18 package(s) from NuGet in 34 s.
  build (arm64-osx)             Restored 18 package(s) from NuGet in 11 s.
  build (x64-osx)                Restored 18 package(s) from NuGet in 12 s.
  build (x64-windows-static-md)  Restored 18 package(s) from NuGet in 5.5 s.
  ```
  All four legs of the same run restored the full 18-package set from the GitHub Packages NuGet feed rather than rebuilding — a 5–34 second restore versus the documented 15–40 minute uncached FFmpeg build. This is a repeat run (the feed was populated by an earlier run against the same `vcpkg.json`), so this is direct, positive proof of the caching claim on real infrastructure, not the "UNPROVEN, requires two real pushes" caveat 01-05-SUMMARY.md recorded at plan-execution time — that gap has since been closed by actual CI history.

**Verdict: fully satisfied.** (One adjacent item, BUILD-06's fork-PR read-only behavior, remains genuinely unproven — no fork PR has been opened yet. This does not affect criterion 4 as written, which is about the blocking-matrix-green and cache-restore claims specifically, both of which are now positively evidenced. Fork-PR behavior is listed under Human Verification below for completeness.)

### 5. Non-ASCII path opens correctly on Windows with color output still rendering; `scripts/gen_corpus` regenerates every fixture deterministically from a tree with no committed media binary

**Status: ⚠️ PARTIALLY SATISFIED — one clause not well-posed for Phase 1's actual scope**

This criterion bundles two independent claims plus a sub-clause that the phase deliberately does not build yet. Assessed honestly, clause by clause:

**5a. Non-ASCII path opens correctly on Windows — ✓ VERIFIED (real hardware, not simulated)**

01-03-SUMMARY.md records genuine `conhost.exe` evidence, deliberately run in `conhost` rather than Windows Terminal because Windows Terminal enables VT processing itself and would pass even if mediadiff's own `SetConsoleMode` call were wrong:
```
> mkdir Ünïcödé-tëst && copy mediadiff.exe Ünïcödé-tëst\ && cd Ünïcödé-tëst && mediadiff.exe --version
        1 file(s) copied.
mediadiff 0.1.0
[full version block]
```
Plus the automated, CI-green cross-platform proof: `unit.test_fs_utf8 - non-ASCII filename round trip and empty-path rejection` passes on all three blocking legs including Windows CI (confirmed via `ctest --test-dir build/x64-linux` locally: Passed; and via the CI run's `Test #8` on the Windows leg). The BMP (日本語) and astral-plane (🎬, surrogate-pair) cases are both covered — a real, non-trivial UTF-16 correctness proof, not just an ASCII-adjacent smoke test.

**5b. "Color output still rendering" — genuinely nothing to test; the claim is vacuous as of Phase 1**

Confirmed directly by source inspection: `grep -rn "NO_COLOR\|fmt::style\|fmt::color\|ansi\|\x1b\[" src/` returns no matches anywhere in the codebase except an unrelated code comment. mediadiff emits **zero styled or ANSI output today**. This is not an oversight — 01-CONTEXT.md's own "Out of scope" section explicitly excludes "Every other CLI flag and subcommand (CLI-01/02/03/04/06/07/08/10 are Phase 2)", and CLI-08 (`NO_COLOR`/TTY/`GITHUB_ACTIONS` color handling) is exactly the requirement that would make this clause meaningful. 01-03-SUMMARY.md reached the same conclusion independently and, rather than forcing a human to "confirm nothing renders as nothing," substituted an honest mechanical check: `tests/unit/test_console_vt.cpp`'s `[console]`-tagged test asserts the `SetConsoleMode`/`ENABLE_VIRTUAL_TERMINAL_PROCESSING` mechanism itself is correctly wired (3 assertions, passed on real `conhost.exe`, and structurally skips — not passes — under CI's piped stdout). That is the right substitute for what Phase 1 can actually prove: the *plumbing* for color is correct; there is no *content* to render yet.

I am not marking this VERIFIED, because the roadmap criterion's literal text ("color output still rendering") describes a behavior the codebase does not have. I am also not marking it FAILED, because nothing was skipped, cut, or stubbed — this is a genuine scope mismatch between the roadmap criterion's phrasing (written when Phase 1's contents may not yet have been finalized against 01-CONTEXT.md's later boundary decision) and Phase 1's own locked scope decision. This is exactly the kind of thing the verification process should surface to a human rather than silently pass or silently fail.

**5c. `scripts/gen_corpus` determinism and no-committed-media — ✓ VERIFIED for what Phase 1 actually builds (a skeleton with zero fixtures)**

Re-ran independently on this host:
```
$ bash scripts/gen_corpus.sh
gen_corpus: manifest written to tests/fixtures/GENERATOR_MANIFEST.json. No fixtures generated in Phase 1 (skeleton only).
$ cat tests/fixtures/GENERATOR_MANIFEST.json
{ "generator": "ffmpeg version N-126086-...", "configuration": "...", "generated_at": "2026-08-14T23:12:16Z" }
$ git status --short tests/fixtures/
?? tests/fixtures/          # untracked — no media binary, no manifest committed
```
This matches 01-04-SUMMARY.md's own two-run determinism proof (identical key order and values except `generated_at`). D-08 and 01-CONTEXT.md are explicit that Phase 1's job is the skeleton and provenance manifest, not real fixture generation — "no committed media binary" is trivially and correctly true (there are no fixtures to commit yet, and the `.gitignore` rule would block one if planted). The roadmap phrase "regenerates every fixture deterministically" is satisfied over the empty set by design; it becomes a meaningful test only once a later phase's plan actually generates fixtures through this script.

**Verdict:** 5a and 5c are genuinely satisfied. 5b is not a code gap — it is a roadmap-criterion clause describing Phase-2 functionality, correctly and deliberately out of Phase 1's scope per 01-CONTEXT.md. Routed to human verification rather than silently passed.

## Anti-Pattern / Structurally-Blind-Check Scan

Per the review's own pattern-hunt ("five checks structurally incapable of observing their own subject… four are fixed… assess whether any *remaining* check has this property"), I looked for a fifth. Found one, not previously flagged:

### F-01 (info/warning, not blocking): CI's "ctest discovered N tests" guard undercounts by design due to a column-alignment mismatch

**File:** `.github/workflows/ci.yml` (the `Test` step, all 5 legs)
**Reproduced locally:**
```
$ ctest --test-dir build/x64-linux -N | cat -A | grep "Test #"
  Test  #1: unit.Linked FFmpeg reports the expected LGPL license$   # TWO spaces before #
  ...
  Test  #9: integration.version_output ...$                         # TWO spaces before #
  Test #10: integration.vmaf_absent ...$                            # ONE space before #
  Test #11: integration.static_link$                                # ONE space before #
```
`ctest -N` right-justifies test numbers to the width of the largest one. With 11 tests, numbers 1–9 get an extra padding space (`Test  #1`); only 10–11 have a single space (`Test #10`). The CI guard's pattern is:
```bash
TOTAL=$(ctest --test-dir "$TEST_DIR" -N | grep -c '^  Test #' || true)
```
That pattern requires exactly one space between `Test` and `#`, so it matches **only** `#10` and `#11` and silently misses `#1`–`#9`. Live CI log confirms this is not a local-only artifact — the same run's Windows and macOS legs both print:
```
build (x64-windows-static-md) Test  ctest discovered 2 tests in build/x64-windows-static-md.
build (arm64-osx)             Test  ctest discovered 2 tests in build/arm64-osx.
```
against an actual 11-test suite. The check's factual diagnostic is wrong on every green run today (reports "2" instead of "11").

**Why this is not a blocker:** the check's actual safety property — "fail if ctest discovered zero tests" — still holds in every realistic scenario. If discovery genuinely drops to 0, no `Test #` line exists at all regardless of padding width, and the check correctly fires. If discovery is a nonzero count ≤ 9, no padding is applied at all (uniform single space), and the pattern matches every line correctly. The undercount only occurs in the 10–99 range, and only ever produces a value that is *still nonzero* as long as at least 10 tests exist, so it cannot currently mask a real "zero tests discovered" failure. It is, however, exactly the same class of self-blind check the review already found and fixed twice in this phase (WR-02's `grep -c` early-exit and the earlier double-count-zero-tests incidents referenced in 01-05-SUMMARY.md's own commit history) — a fifth instance of the same failure family, this time cosmetic rather than safety-critical.

**Recommendation:** switch to `wc -l` on the same grep, or match `'Test  *#[0-9]+:'` with a variable-width space, so the diagnostic states the true count. Not a phase-1 gap — a low-priority follow-up.

## Requirements Coverage

| Requirement | Status | Evidence |
|---|---|---|
| BUILD-01 | ✓ Satisfied | Clean-checkout CI builds green on all 3 blocking legs (run 31849289102); local x64-linux rebuild confirmed |
| BUILD-02 | ✓ Satisfied | Pinned `builtin-baseline` + `overrides` + submodule SHA; 18-package agreement across 3 OS images in one CI run |
| BUILD-03 | ✓ Satisfied | Runtime exact-match LGPL assertion, fail-first proven against 4 disallowed strings; configure-time GPL fast-fail present |
| BUILD-04 | ✓ Satisfied | `ldd` shows zero `libav*`/`libsw*` deps; documented negative-control proof in 01-02-SUMMARY.md |
| BUILD-05 | ✓ Satisfied | Merge ruleset (id 20862843) requires exactly the 3 blocking legs + lint; advisory legs correctly excluded. **Note:** `.planning/REQUIREMENTS.md`'s own traceability table (line 251) still lists BUILD-05 as "Pending" despite the top-of-file requirement checkbox marking it `[x]` complete with cited evidence — a stale-document inconsistency in REQUIREMENTS.md itself, not a code gap. Worth a housekeeping pass. |
| BUILD-06 | ✓ Satisfied (core claim); ⚠️ one sub-claim unproven | Cache-restore positively evidenced (see criterion 4). Fork-PR read-only behavior remains genuinely untested — no fork PR has been opened against this repo yet. |
| BUILD-07 | ✓ Satisfied | `src/util/expected.h`, PROJECT.md decision row, 5 passing Catch2 cases, `grep -rl 'tl::expected'` confirms single-header discipline |
| BUILD-08 | ✓ Satisfied | `scripts/gen_corpus.sh`/`.ps1` gate on ffmpeg ≥ 6.1, deterministic manifest re-verified locally, `.gitignore` blocks committed media |
| BUILD-09 | ✓ Satisfied | `vmaf_absent` integration test reads rendered `features:` line, passes on all 3 platforms |
| BUILD-10 | ✓ Satisfied | PROJECT.md Key Decisions row with version/mechanism/rationale/known-cost |
| CLI-05 | ✓ Satisfied | All 4 `--version` fields regex-asserted against real binary output on all 3 platforms |
| CLI-09 | ✓ Satisfied | Real-hardware conhost.exe evidence + automated BMP/astral-plane round-trip green on 3-OS CI. **Note:** same stale-traceability issue as BUILD-05 — REQUIREMENTS.md line 265 says "Pending" against a top-of-file `[x]` complete row. |

## Code Review Follow-Through

`01-REVIEW.md` found 1 critical + 5 warnings. Verified against current `main` HEAD (`f90fbdc`):
- **CR-01** (Windows argv UTF-16→UTF-8 silent substitution) — **fixed**, confirmed by reading `src/cli/main.cpp:71-83` directly: the loop now checks `utf8_arg.empty() && !wide_arg.empty()`, prints to stderr, and returns 64 rather than silently substituting.
- **WR-01** (`fprintf(stderr,...)` lint gap) — **fixed**, confirmed by reading `scripts/lint_eng16.sh:53`: pattern now matches the `stdout`/`stderr` stream names directly (catches `fprintf`, `fputs`, `vfprintf` etc.) rather than enumerating function names.
- **WR-02** (zero-test guard dies before printing diagnostic) — **fixed** for the crash-before-diagnostic defect (`|| true` added, confirmed in `.github/workflows/ci.yml`); see F-01 above for a *different*, non-blocking residual defect in the same check (undercounting, not crashing).
- **WR-03, WR-04, WR-05** (POSIX FD leak / EINTR mishandling / Windows arg-quoting in `tests/integration/cli_harness.h`) — confirmed still present in the current file; per this verification's brief, these were deliberately deferred by the user and are correctly excluded from this report as gaps.

## Human Verification Required

See frontmatter `human_verification` block. Two items:
1. Criterion 5's color-rendering clause — a product decision on whether Phase 1 should be considered to satisfy this criterion given CLI-08 doesn't exist yet, not a code question.
2. BUILD-06's fork-PR read-only cache path — a real fork PR has never been opened against this repo to exercise the branch.

Neither item reflects a defect in the delivered code; both reflect claims that cannot be evidenced yet given what has actually happened against this repository.

## Overall Assessment

Phase 1's actual goal — a single static `mediadiff` binary that builds reproducibly and runs on Linux, macOS and Windows, with every toolchain decision recorded — **is achieved**, verified against real, currently-green CI runs and independent local rebuild/inspection, not SUMMARY.md narration. Every artifact claimed in the five plan summaries was found present, substantive, and wired: the lib/cli split, the `expected<T,E>` alias discipline, the exact-match LGPL runtime assertion, the ENG-16 boundary lint (now closing the `fprintf(stderr,...)` gap), the Windows UTF-8 path/argv handling (now failing loudly per D-04, confirmed fixed post-review), and the 3-OS CI matrix with real, log-evidenced NuGet cache restores.

The one criterion not cleanly VERIFIED (criterion 5) fails to verify not because anything was skipped or faked, but because its literal wording asks about a feature (colored terminal output) that Phase 1's own locked scope (01-CONTEXT.md) correctly assigns to Phase 2. That is a finding worth a human decision — either accept Phase 1 as satisfying the parts of criterion 5 that are actually buildable now, or hold the roadmap criterion open until CLI-08 lands and re-verify at that point.

---
_Verified: 2026-08-15_
_Verifier: Claude (gsd-verifier)_
