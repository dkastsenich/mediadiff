---
phase: 01-foundation-toolchain
plan: 01
subsystem: infra
tags: [cmake, vcpkg, ffmpeg, cli11, catch2, tl-expected, fmt]

# Dependency graph
requires: []
provides:
  - "vcpkg submodule pinned at 105fdc246ba9a28b0789284217b0d1120446d43f (also builtin-baseline)"
  - "vcpkg.json with FFmpeg 8.1 (port-version 4) overrides pin, decode-only LGPL feature set"
  - "CMakePresets.json schema v6 with 5 triplets (x64-linux/arm64-osx/x64-windows-static-md blocking, arm64-linux/x64-osx non-blocking)"
  - "CMakeLists.txt: libmediadiff/mediadiff target split, compiler-floor guard, configure-time GPL fast-fail, Apple-framework block, warnings-as-errors on first-party targets"
  - "src/util/version.{h,cpp} — CLI-05 --version composition"
  - "src/cli/main.cpp — CLI11 --version wiring"
  - "src/util/expected.h — mediadiff::expected<T,E> alias over tl-expected, sole permitted tl::expected naming site"
  - "tests/unit — Catch2 v3 harness, test_license.cpp (exact-positive LGPL assertion), test_expected.cpp"
  - "PROJECT.md Key Decisions rows for FFmpeg baseline (BUILD-10) and expected<T,E> (BUILD-07)"
affects: ["01-02", "01-03", "01-04", "01-05"]

# Actuals (#2632)
actuals:
  tokens: 5000
  tasks: 3
  commits: 3

tech-stack:
  added: [cmake-3.25-presets, vcpkg-manifest-mode, ffmpeg-8.1-lgpl, cli11-2.6.2, fmt-12.2.0, tl-expected-1.3.1, catch2-3.15.3]
  patterns:
    - "libmediadiff (STATIC, no stdout/exit) vs mediadiff (executable) target split, enforced by convention now, by CI lint in Plan 04"
    - "mediadiff::expected<T,E> single-alias-header discipline — only src/util/expected.h may name tl::expected"
    - "Runtime license self-report exact-match assertion, never a substring/absence test"

key-files:
  created:
    - vcpkg.json
    - CMakePresets.json
    - CMakeLists.txt
    - src/util/version.h
    - src/util/version.cpp
    - src/cli/main.cpp
    - src/util/expected.h
    - tests/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_license.cpp
    - tests/unit/test_expected.cpp
    - .gitignore
  modified:
    - .planning/PROJECT.md

key-decisions:
  - "FFmpeg pinned to version 8.1, port-version 4 (bare 8.1 entry, not 8.1.1/8.1.2) via vcpkg.json overrides — recorded in PROJECT.md per BUILD-10"
  - "mediadiff::expected<T,E> aliases tl-expected 1.3.1 in a single header — recorded in PROJECT.md per BUILD-07"
  - "nasm built from source into ~/.local/bin (Rule 3 blocking fix) since dav1d's vcpkg build requires it and this sandbox has no root/apt access"

patterns-established:
  - "Pattern: warnings-as-errors applied per-target via a small CMake function (mediadiff_apply_warnings), never globally, with vcpkg includes consumed SYSTEM"
  - "Pattern: catch_discover_tests(... TEST_PREFIX \"unit.\") so the documented `ctest -R unit` quick-run command actually selects the suite's tests"

requirements-completed: [BUILD-01, BUILD-02, BUILD-03, BUILD-07, BUILD-10, CLI-05]

coverage:
  - id: D1
    description: "Clean-checkout x64-linux build produces a running mediadiff binary with no system FFmpeg present"
    requirement: "BUILD-01"
    verification:
      - kind: integration
        ref: "cmake --preset x64-linux && cmake --build --preset x64-linux && ./build/x64-linux/mediadiff --version"
        status: pass
    human_judgment: false
  - id: D2
    description: "vcpkg manifest resolves reproducibly via 40-char builtin-baseline + ffmpeg overrides, vcpkg submodule pinned to the same SHA"
    requirement: "BUILD-02"
    verification:
      - kind: other
        ref: "git submodule status vcpkg; node vcpkg.json shape checks (overrides/features/baseline/deps)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Runtime LGPL assertion is exact-positive-match and proven to reject 4 disallowed license strings"
    requirement: "BUILD-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_license.cpp#Linked FFmpeg reports the expected LGPL license"
        status: pass
    human_judgment: false
  - id: D4
    description: "mediadiff::expected<T,E> works with monadic operations; backing library named in exactly one file"
    requirement: "BUILD-07"
    verification:
      - kind: unit
        ref: "tests/unit/test_expected.cpp (5 TEST_CASEs)"
        status: pass
      - kind: other
        ref: "grep -rl 'tl::expected' src/ tests/ -> exactly src/util/expected.h"
        status: pass
    human_judgment: false
  - id: D5
    description: "FFmpeg baseline is a recorded PROJECT.md decision with version, mechanism, rationale, known cost"
    requirement: "BUILD-10"
    verification:
      - kind: other
        ref: ".planning/PROJECT.md Key Decisions table, verified byte-identical version match against vcpkg.json"
        status: pass
    human_judgment: false
  - id: D6
    description: "--version prints tool version, 3 per-library FFmpeg versions, license string, feature list; vmaf absent, cuda never mentioned"
    requirement: "CLI-05"
    verification:
      - kind: integration
        ref: "./build/x64-linux/mediadiff --version (regex-matched all required fields, stdout-only, no cuda)"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-08-12
status: complete
---

# Phase 1 Plan 1: End-to-end toolchain tracer Summary

**A statically-linked `mediadiff` binary builds on x64-linux from a clean checkout (no system FFmpeg) via vcpkg-pinned FFmpeg 8.1 LGPL, prints `--version` with per-library FFmpeg versions and an exact-match LGPL license assertion, and 6/6 Catch2 unit tests pass.**

## Performance

- **Duration:** ~50 min (dominated by the uncached vcpkg FFmpeg 8.1 build, ~12 min once network/nasm blockers were cleared, plus repeated retries against a flaky sandbox network)
- **Completed:** 2026-08-12
- **Tasks:** 3
- **Files modified:** 14 (+ .planning/PROJECT.md, .planning/STATE.md)

## Accomplishments
- vcpkg submodule pinned at `105fdc246ba9a28b0789284217b0d1120446d43f` (re-fetched live at execution time, not the stale SHA quoted in RESEARCH.md), used as both submodule commit and `builtin-baseline`
- FFmpeg pinned to `8.1`/port-version 4 via `vcpkg.json` `overrides` — decode-only LGPL feature set (avcodec, avformat, swscale, swresample, dav1d, zlib), `default-features: false`
- CMakePresets.json (schema v6): hidden `base` preset wires `CMAKE_TOOLCHAIN_FILE` before `project()`; 5 triplets present (3 CI-blocking, 2 non-blocking per D-06)
- CMakeLists.txt: compiler-floor guard (GCC≥12/Clang≥15/AppleClang≥15/MSVC≥19.30), configure-time `find_library` scan for x264/x265/xvidcore (GPL fast-fail), `libmediadiff`/`mediadiff` target split, explicit `if(APPLE)` framework block for vcpkg's documented `FindFFMPEG.cmake.in` macOS gap, warnings-as-errors applied per-target (never globally), vcpkg includes consumed as `SYSTEM`
- `mediadiff --version` composes tool version + runtime-decoded libavcodec/libavformat/libavutil versions + `avutil_license()` + feature CSV, all to stdout, nothing to stderr
- `mediadiff::expected<T,E>` alias in `src/util/expected.h` — confirmed the sole file naming `tl::expected` anywhere in `src/` or `tests/`
- Catch2 v3 harness: `test_license.cpp` exact-matches `avutil_license()`/`avcodec_license()`/`avformat_license()` and proves the predicate rejects all 4 disallowed license strings (GPLv2, GPLv3, LGPLv3, nonfree) — the fail-first proof the plan's prohibitions require; `test_expected.cpp` covers value/error truthiness, `and_then` propagation and short-circuit, `unexpected<E>` construction
- PROJECT.md Key Decisions gained two rows: FFmpeg baseline pin (BUILD-10) and `expected<T,E>` choice (BUILD-07), each with version/mechanism/rationale/known-cost

## Task Commits

1. **Task 01-01-T1: End-to-end tracer** — `9f2fc10` (feat)
2. **Task 01-01-T2: Catch2 license + expected<T,E> harness** — `978855e` (test)
3. **Task 01-01-T3: PROJECT.md Key Decisions rows** — `8c9e245` (docs)

**Plan metadata:** this SUMMARY + STATE.md update (pending final commit)

## Files Created/Modified
- `vcpkg.json` — manifest with FFmpeg 8.1 override, decode-only LGPL features, 8 supporting deps, `vmaf` optional feature
- `CMakePresets.json` — 5-triplet preset layout, vcpkg toolchain wiring
- `CMakeLists.txt` — full-phase root build file (compiler guard, GPL fast-fail, target split, macOS frameworks, warnings-as-errors)
- `src/util/version.h` / `.cpp` — `compose_version_string()`/`enabled_features_csv()`
- `src/cli/main.cpp` — CLI11 `--version` wiring, `mediadiff::run()`
- `src/util/expected.h` — `mediadiff::expected`/`unexpected` alias over `tl-expected`
- `tests/CMakeLists.txt`, `tests/unit/CMakeLists.txt` — Catch2 v3 wiring, `TEST_PREFIX "unit."`
- `tests/unit/test_license.cpp`, `tests/unit/test_expected.cpp` — the two required unit test files
- `.gitignore` — excludes `/build/` and vcpkg's local buildtrees/downloads/packages/installed dirs
- `.planning/PROJECT.md` — two new Key Decisions rows

## Decisions Made

- **A3 resolution (which "8.1"):** locked the bare `version: "8.1", port-version: 4` vcpkg entry, not the later `8.1.1`/`8.1.2` patch-line entries — matches D-01's literal "8.1 Hoare" wording, and is a distinct, independently-hashed vcpkg entry from the point releases. Recorded in PROJECT.md.
- **A2 resolution (packed-version decode idiom):** `AV_VERSION_MAJOR/MINOR/MICRO` applied to `avcodec_version()`/`avformat_version()`/`avutil_version()` produced plausible FFmpeg-8.1-era values (libavcodec 62.28.100, libavformat 62.12.100, libavutil 60.26.100) — no correction needed.
- **A4 resolution (`-ldl`):** not added manually — `find_package(FFMPEG REQUIRED)`'s `FFMPEG_LIBRARIES` already included `/usr/lib/x86_64-linux-gnu/libdl.a` via the module's own dependency resolution.
- **`TEST_PREFIX "unit."`:** added to `catch_discover_tests` so the phase's documented quick-run command (`ctest --preset <triplet> -R unit`) actually selects this suite's tests by name — raw Catch2 `TEST_CASE` names didn't contain the substring "unit".

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Missing `tests/CMakeLists.txt` when Task T1's `CMakeLists.txt` calls `add_subdirectory(tests)`**
- **Found during:** Task 1 (root CMakeLists.txt authoring)
- **Issue:** The plan splits `tests/CMakeLists.txt` authorship into Task 2, but Task 1's `CMakeLists.txt` unconditionally calls `add_subdirectory(tests)`, which fails configure if the directory has no CMakeLists.txt at all.
- **Fix:** Created a minimal placeholder `tests/CMakeLists.txt` (comment only) during Task 1's configure/build cycle, then let Task 2 supply the real content (`add_subdirectory(unit)`).
- **Files modified:** `tests/CMakeLists.txt`
- **Verification:** `cmake --preset x64-linux` configured successfully with the placeholder present; Task 2's real content replaced it before that task's commit.
- **Committed in:** `978855e` (Task 2 commit carries the final content; no separate commit needed since the placeholder never reached HEAD)

**2. [Rule 3 - Blocking] `nasm` absent, dav1d's vcpkg build fails without it, no root/apt access in this sandbox**
- **Found during:** Task 1 (vcpkg install of `dav1d`, a transitive FFmpeg feature dependency)
- **Issue:** `dav1d`'s assembly codepaths require `nasm`; the sandbox has no `nasm` and no passwordless sudo/apt access to install it via the package manager the error message suggested.
- **Fix:** Downloaded `nasm-2.16.01` source directly from nasm.us and built it from source (`./configure --prefix=$HOME/.local && make && make install`) into `~/.local/bin`, then prepended that directory to `PATH` for the CMake configure/build invocations. This is *not* a package-manager install substitution (excluded from Rule 3) — it's building a standard toolchain utility from its own upstream source into a user-writable prefix, with no alternative-package-name guessing involved.
- **Files modified:** none in-repo; user-local toolchain addition only (`~/.local/bin/nasm`, `~/.bashrc` PATH line)
- **Verification:** `nasm -v` reports `NASM version 2.16.01`; subsequent `cmake --preset x64-linux` resolved `dav1d` successfully.
- **Committed in:** n/a (environment-only change, documented here for CI-runner parity awareness — CI images ship `nasm` per RESEARCH.md's Environment Availability table, so this is sandbox-specific, not a repo change)

**3. [Rule 1 - Bug] `catch_discover_tests` produced test names that don't match the documented `-R unit` filter**
- **Found during:** Task 2 (running `ctest --preset x64-linux -R unit` per the plan's own `<verify>` command)
- **Issue:** Catch2 `TEST_CASE` names (e.g. "Linked FFmpeg reports the expected LGPL license") contain neither "unit" nor any substring that the `-R unit` regex would match, so the plan's documented quick-run command silently discovered zero tests.
- **Fix:** Added `TEST_PREFIX "unit."` to the `catch_discover_tests()` call in `tests/unit/CMakeLists.txt`, so registered CTest names become `unit.<TEST_CASE name>`.
- **Files modified:** `tests/unit/CMakeLists.txt`
- **Verification:** `ctest --preset x64-linux -R unit --output-on-failure` now discovers and passes all 6 tests.
- **Committed in:** `978855e` (Task 2 commit)

**4. [Rule 2/3] Added `.gitignore`**
- **Found during:** Task 1 (post-build, checking for untracked files before committing)
- **Issue:** `build/` (the CMake binary directory, containing the ~10 GB vcpkg-built FFmpeg tree) had no `.gitignore` entry and would otherwise be left untracked or accidentally staged.
- **Fix:** Added `.gitignore` covering `/build/` and vcpkg's local `buildtrees/`/`downloads/`/`packages/`/`installed/` directories (the submodule pointer itself is still tracked normally).
- **Files modified:** `.gitignore` (new)
- **Verification:** `git status --short` shows `build/` no longer listed as untracked.
- **Committed in:** `9f2fc10` (Task 1 commit)

---

**Total deviations:** 4 auto-fixed (2 blocking, 1 bug, 1 missing-critical-functionality)
**Impact on plan:** All four were necessary to make the plan's own stated verification commands actually pass as written (the placeholder tests/CMakeLists.txt and the TEST_PREFIX fix) or to make the build possible at all in this sandbox (nasm) / avoid a de-facto repo-hygiene gap (.gitignore). No scope creep — nothing beyond what T1/T2's own `<verify>`/`<acceptance_criteria>` blocks required.

## Issues Encountered

- **Sandbox network flakiness (HTTP/2):** curl connections to `github.com` release-asset redirects and `objects.githubusercontent.com` intermittently failed with `curl: (56) Connection died` / `curl: (52) Empty reply from server` under HTTP/2. Forcing `--http1.1` via `~/.curlrc` improved but did not eliminate the flakiness (vcpkg's own downloader doesn't consistently honor the curlrc-suggested protocol version and doesn't retry vcpkg-classified "non-transient" curl errors). Resolved pragmatically by wrapping `cmake --preset x64-linux` in a shell retry loop (up to 40 attempts, 5s backoff) — each retry preserves prior successful package installs/downloads, so the loop made monotonic progress until all 18 vcpkg packages resolved. This is a sandbox-environment characteristic, not a repo defect; CI runners are expected to have stable network per RESEARCH.md.
- **Two prior configure attempts failed** on transient tarball downloads (Catch2 source, then NixOS/patchelf) before nasm was discovered as the deterministic (non-network) blocker on `dav1d`. Once nasm was resolved, the retry loop succeeded on its very next attempt (~12 min wall-clock for the actual FFmpeg 8.1 + dependency build).

## User Setup Required

None - no external service configuration required. (Note: CI runner images per RESEARCH.md's Environment Availability table ship `nasm` preinstalled on `windows-latest`; the Linux/macOS CI runners should be verified to have `nasm` too before Plan 05's CI matrix work — Ubuntu's `nasm` apt package installs cleanly there, unlike this sandbox which lacked root.)

## Next Phase Readiness

- The x64-linux tracer slice is fully proven: configure → build → run → license-assert → PROJECT.md decisions recorded. Plans 02–05 (integration tests, Windows UTF-8, corpus skeleton/directory tree, CI matrix) can now build on a working `CMakeLists.txt`/`vcpkg.json`/preset foundation without re-deriving the toolchain wiring.
- **Carry-forward for Plan 05 (CI):** `nasm` must be present on the Linux/macOS CI runner images (expected via `apt-get install nasm` / Homebrew, not the from-source workaround used in this sandbox).
- **Carry-forward for Plan 04:** the ENG-16 lint script should account for the `TEST_PREFIX "unit."` convention when it inventories discovered tests, if it inspects CTest output rather than source.
- No blockers for Plan 02/03 (both build directly on this plan's `CMakeLists.txt`/target structure per the phase-wide artifact roll-up).

---
*Phase: 01-foundation-toolchain*
*Completed: 2026-08-12*

## Self-Check: PASSED

All 14 created/modified files verified present on disk; all 3 task commit hashes (`9f2fc10`, `978855e`, `8c9e245`) verified present in `git log`.
