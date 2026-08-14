---
quick_id: 260814-js4
subsystem: build
tags: [cmake, ninja, msvc, vcpkg, ci, github-actions]

# Dependency graph
requires:
  - phase: 01-05 (CI matrix authoring)
    provides: .github/workflows/ci.yml 5-leg matrix, first run that reached Build (31787938349)
provides:
  - Windows CI leg initialises MSVC v143 before vcpkg bootstrap and CMake configure
  - base CMake preset builds Release (all 5 triplet presets inherit it)
affects: [phase-1-BUILD-01, phase-1-BUILD-04, phase-1-BUILD-05, phase-1-BUILD-06, phase-5-7-perf-requirements]

actuals:
  tokens: 850
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Windows CI legs using Ninja + vcpkg must run ilammy/msvc-dev-cmd (or vcvars64.bat) before both vcpkg bootstrap and cmake --preset, since Ninja does no MSVC toolchain discovery on its own"
    - "Third-party GitHub Actions pinned to full 40-char commit SHA with a trailing version comment (ASVS V10 supply-chain)"

key-files:
  created: []
  modified:
    - .github/workflows/ci.yml
    - CMakePresets.json

key-decisions:
  - "Used ilammy/msvc-dev-cmd@7defe9254715b088f12dddd9942945a3d75cdad5 (v1.9.0, latest tag as of this task) over the dependency-free vcvars64.bat + $GITHUB_ENV route — the plan judged either acceptable; the action is the standard mechanism and is now SHA-pinned per ASVS V10"
  - "CMAKE_BUILD_TYPE=Release added only to the base preset's cacheVariables, touching no other line of CMakePresets.json per the plan's explicit remit boundary (file owned by plan 01-01)"

requirements-completed: []

coverage:
  - id: D1
    description: "Windows CI leg initialises MSVC x64 dev environment before vcpkg bootstrap and cmake configure, third-party action SHA-pinned"
    verification:
      - kind: other
        ref: "python3 -c yaml.safe_load(...) on .github/workflows/ci.yml; grep-based step-order check (Checkout < Initialise MSVC < Bootstrap vcpkg < Configure); grep for 40-char SHA in the uses: line"
        status: pass
    human_judgment: true
    rationale: "The fix cannot be exercised from this Linux sandbox — no MSVC toolchain exists here. YAML validity, step ordering, and SHA pinning are machine-verified; whether the Windows leg actually goes green is unverified pending a real GitHub Actions run, and must be confirmed by a human watching that run."
  - id: D2
    description: "base CMake preset sets CMAKE_BUILD_TYPE=Release; x64-linux reconfigures and rebuilds in place, still 10/10 tests, still warnings-as-errors clean, linked against release (not debug) vcpkg dependencies"
    verification:
      - kind: other
        ref: "grep CMAKE_BUILD_TYPE build/x64-linux/CMakeCache.txt; full recompile of all first-party sources (all 6 .cpp files touched) via cmake --build --preset x64-linux, 0 warnings; ctest --test-dir build/x64-linux --output-on-failure -> 10/10; grep of build.ninja mediadiff link line -> exclusively vcpkg_installed/x64-linux/lib/*.a"
        status: pass
    human_judgment: false

duration: ~15min
completed: 2026-08-14
status: complete
---

# Quick Task 260814-js4: MSVC on Windows CI, Release build type Summary

**Windows CI leg now initialises MSVC v143 (SHA-pinned action) before vcpkg/configure, and the `base` CMake preset builds Release — verified locally on x64-linux with a clean warnings-as-errors relink and 10/10 tests against release-tree dependencies; the Windows fix itself is unproven until a real CI run.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-08-14 (session start)
- **Completed:** 2026-08-14T12:18Z
- **Tasks:** 2/2 completed
- **Files modified:** 2

## Accomplishments

- Added an `Initialise MSVC x64 developer environment (Windows)` step to `.github/workflows/ci.yml`, gated to `runner.os == 'Windows'`, placed after Checkout and before both vcpkg bootstrap and Configure — fixes CI run 31787938349's Windows Build failure (MinGW linking MSVC-mangled symbols, `__CxxFrameHandler4` undefined reference).
- Pinned `ilammy/msvc-dev-cmd` to full 40-char commit SHA `7defe9254715b088f12dddd9942945a3d75cdad5` (tag `v1.9.0`, the latest tag on the upstream repo as of this task) per ASVS V10 supply-chain guidance from `01-RESEARCH.md` § Security Domain.
- Added `"CMAKE_BUILD_TYPE": "Release"` to the `base` configure preset in `CMakePresets.json` — all five triplet presets inherit `base`, so one edit covers every CI leg and local development. No other line of the file was touched (it is owned by plan 01-01).
- Reconfigured and rebuilt `x64-linux` **in place** (did not delete `build/x64-linux`): configure took ~1.0s (relink, not a ~40-minute FFmpeg rebuild, since vcpkg already had both debug and release variants installed), full recompile of all first-party `.cpp` files took ~5.5s, both clean under `-Wall -Wextra -Werror` at `-O3` (no `-Wmaybe-uninitialized`/`-Wstringop-overflow`-class regressions surfaced).
- Confirmed `CMAKE_BUILD_TYPE:STRING=Release` in `build/x64-linux/CMakeCache.txt`.
- Confirmed the `mediadiff` executable's link line in `build.ninja` references only `vcpkg_installed/x64-linux/lib/*.a` (release tree) — no `debug/lib` entries — for every project dependency (avformat, avcodec, avutil, swresample, swscale, z, fmt, dav1d, CLI11).
- `ctest --test-dir build/x64-linux --output-on-failure` — 10/10 passed (7 unit + 3 integration), 0.04s total.

## Task Commits

Each task was committed atomically:

1. **Task 1: initialise the MSVC environment on the Windows leg** - `b21401a` (fix)
2. **Task 2: build Release** - `3fb21a9` (fix)

_No TDD tasks — both are direct fixes to CI/build config with automated + manual-run verification, not unit-testable code._

## Files Created/Modified

- `.github/workflows/ci.yml` - Added Windows-only MSVC x64 dev-env init step (SHA-pinned `ilammy/msvc-dev-cmd@7defe9254715b088f12dddd9942945a3d75cdad5 # v1.9.0`), placed before vcpkg bootstrap and Configure, with an inline comment explaining why removing it silently reintroduces the MinGW link failure.
- `CMakePresets.json` - Added `"CMAKE_BUILD_TYPE": "Release"` to the `base` preset's `cacheVariables`; no other change.

## Decisions Made

- Chose the `ilammy/msvc-dev-cmd` action over hand-rolled `vcvars64.bat` + `$GITHUB_ENV` export — it is the standard, widely-used mechanism for Ninja+MSVC in GitHub Actions and, once SHA-pinned, carries no meaningfully different supply-chain risk than a hand-written script that would itself need auditing.
- Used the latest available tag (`v1.9.0`, resolved via `git ls-remote --tags`) rather than an older pinned release, since there was no prior pin to preserve and no reason to start on a stale version.

## Deviations from Plan

None - plan executed exactly as written. Both tasks matched their `<action>`/`<verify>`/`<done>` specs; no Rule 1-4 triggers encountered.

## Issues Encountered

None. The reconfigure/rebuild went exactly as the plan's `<environment>` block predicted (relink from warm vcpkg install, not a full FFmpeg rebuild), and no `-O3`-only warnings appeared under `-Wall -Wextra -Werror`.

## User Setup Required

None - no external service configuration required. No GitHub secrets, tokens, or dashboard changes involved in either task.

## Next Phase Readiness

**BUILD-01/BUILD-05/BUILD-06 remain unproven pending a real CI run.** Everything verifiable from this Linux sandbox has been verified: `.github/workflows/ci.yml` parses as valid YAML, the MSVC init step is correctly gated and ordered before vcpkg bootstrap and Configure, the third-party action reference is a full 40-character commit SHA, and `CMakePresets.json`'s only change is the intended `CMAKE_BUILD_TYPE` addition. **What is NOT proven:** whether `cl.exe` is actually selected on the `windows-2022` runner, whether the Windows Build step now succeeds, and whether the Windows leg's `ctest`/corpus-generator steps pass. This can only be confirmed by pushing and watching a real GitHub Actions run on the `x64-windows-static-md` leg — flagged here explicitly per the plan's critical reminder #6, and should be the first thing checked when Phase 1 execution resumes or before BUILD-01/04/05/06 are marked complete. The two advisory legs (`arm64-linux`, `x64-osx`) were untouched, as scoped.

---
*Quick task: 260814-js4*
*Completed: 2026-08-14*

## Self-Check: PASSED

- FOUND: .github/workflows/ci.yml
- FOUND: CMakePresets.json
- FOUND: 260814-js4-SUMMARY.md
- FOUND: commit b21401a (Task 1)
- FOUND: commit 3fb21a9 (Task 2)
