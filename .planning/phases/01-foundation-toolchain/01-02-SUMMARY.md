---
phase: 01-foundation-toolchain
plan: 02
subsystem: testing
tags: [catch2, ctest, cmake, ffmpeg, posix_spawn, ldd]

# Dependency graph
requires:
  - phase: 01-foundation-toolchain (plan 01)
    provides: "mediadiff/libmediadiff CMake targets, src/util/version.{h,cpp} composing --version, tests/CMakeLists.txt + tests/unit Catch2 v3 harness"
provides:
  - "tests/integration — Catch2 v3 integration suite (version_output, vmaf_absent) plus a standalone CTest test (static_link)"
  - "tests/integration/cli_harness.h — reusable mediadiff::test::run_cli() process-spawn harness for every later phase's CLI-level test"
  - "tests/integration/check_static_link.cmake — platform-dispatched (ldd/otool/dumpbin) BUILD-04 dynamic-dependency inspection, proven to discriminate via a negative control"
affects: ["01-03", "01-04", "01-05"]

# Actuals (#2632)
actuals:
  tokens: 5000
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "mediadiff::test::run_cli() — spawns the real binary via posix_spawn (POSIX) / CreateProcess (Windows), draining stdout/stderr concurrently via select()/dual threads to avoid pipe-buffer deadlock; the one platform-specific function is isolated to detail::spawn_and_capture"
    - "Standalone cmake -P verification scripts registered as CTest add_test (not build steps) so they participate in the sampling loop without slowing incremental builds — check_static_link.cmake follows lint_eng16.sh's established shape from RESEARCH.md"
    - "Rendered-output assertions, not CMake-option assertions, for optional-feature absence (BUILD-09) — a default-off option that nonetheless linked the library would pass the weaker check"

key-files:
  created:
    - tests/integration/CMakeLists.txt
    - tests/integration/cli_harness.h
    - tests/integration/test_version_output.cpp
    - tests/integration/check_static_link.cmake
  modified:
    - tests/CMakeLists.txt

key-decisions:
  - "Negative control for BUILD-04 built locally rather than found on the host: no system binary in this sandbox links the FFmpeg shared libraries (the system /usr/local/bin/ffmpeg is itself statically built). Compiled a trivial C program with `gcc -Wl,--no-as-needed -l:libavcodec.so.60` against the apt-installed libavcodec60/libavutil58/libswresample4 to force a genuine DT_NEEDED dependency, confirming the script's discrimination without touching any committed file (throwaway binary in the session scratchpad, not in the repo)."
  - "Catch2 test names for this suite are NOT given a TEST_PREFIX (unlike tests/unit's 'unit.' prefix from Plan 01) — the plan's own acceptance criteria require -R version_output / -R vmaf_absent to match directly, which they do because those substrings appear literally in each TEST_CASE's name."

patterns-established:
  - "Pattern: header-only test harness (cli_harness.h, all functions `inline`) so later phases can #include it from multiple integration test translation units without a second .cpp to wire into CMake"

requirements-completed: [BUILD-04, BUILD-09, CLI-05]

coverage:
  - id: D1
    description: "Integration suite spawns the real mediadiff binary and independently asserts all four CLI-05 fields (tool version, 3x libav library version, license string, features line) plus stderr-empty and reduced-PATH invariance"
    requirement: "CLI-05"
    verification:
      - kind: integration
        ref: "tests/integration/test_version_output.cpp#version_output - CLI-05 fields present in real binary output"
        status: pass
    human_judgment: false
  - id: D2
    description: "Default build's rendered features line omits vmaf and the full output never mentions cuda, asserted against the captured stdout rather than the CMake option's default"
    requirement: "BUILD-09"
    verification:
      - kind: integration
        ref: "tests/integration/test_version_output.cpp#vmaf_absent - BUILD-09 optional feature not advertised by default"
        status: pass
    human_judgment: false
  - id: D3
    description: "Shipped mediadiff executable declares no dynamic dependency on any FFmpeg shared library on x64-linux; the platform-dispatched check FATAL_ERRORs rather than silently passing when its inspection tool is missing, and is proven to discriminate against a known-dynamic binary"
    requirement: "BUILD-04"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux -R static_link (cmake -P tests/integration/check_static_link.cmake)"
        status: pass
      - kind: other
        ref: "cmake -DMEDIADIFF_BINARY=<locally-built dynamic probe> -P tests/integration/check_static_link.cmake — negative control, confirmed nonzero exit"
        status: pass
    human_judgment: false

duration: ~25min
completed: 2026-08-13
status: complete
---

# Phase 1 Plan 2: Integration test harness — CLI-05, BUILD-09, BUILD-04 Summary

**A reusable `run_cli()` process-spawn harness plus three new CTest tests (`version_output`, `vmaf_absent`, `static_link`) that assert against the real built `mediadiff` binary's stdout and link map — not the build description — that all four `--version` fields are present, the optional `vmaf` feature is absent by default, and no FFmpeg shared library is a runtime dependency.**

## Performance

- **Duration:** ~25 min (warm build tree, incremental compile only — no FFmpeg rebuild)
- **Completed:** 2026-08-13
- **Tasks:** 2
- **Files modified:** 5 (4 created, 1 modified)

## Accomplishments
- `tests/integration/cli_harness.h`: header-only `mediadiff::test::run_cli()` spawning the real `mediadiff` binary (path injected via the `MEDIADIFF_BINARY` compile definition, resolved from `$<TARGET_FILE:mediadiff>`), capturing stdout/stderr independently via `posix_spawn` + `select()`-multiplexed pipe draining on POSIX (avoids a pipe-buffer deadlock on either stream), and `CreateProcess` + dual reader threads on Windows. The one platform difference lives entirely in `detail::spawn_and_capture`. An `EnvVars` override lets a caller replace the child's environment wholesale, used for the reduced-PATH case.
- `tests/integration/test_version_output.cpp`: `version_output` TEST_CASE independently regex-matches all four CLI-05 fields (tool version, `libavcodec`/`libavformat`/`libavutil` each on its own line with both runtime and built-against numbers), asserts stderr is empty, and re-invokes with `PATH=/usr/bin:/bin` to prove no external FFmpeg install is consulted. `vmaf_absent` TEST_CASE reads the actual `features: ` line out of stdout (not the `MEDIADIFF_WITH_VMAF` CMake option) and asserts it omits `vmaf`, plus a case-insensitive whole-output check that `cuda` never appears.
- `tests/integration/check_static_link.cmake`: standalone `cmake -P` script, platform-dispatched (`ldd` on Linux, `otool -L` on macOS, `dumpbin /dependents` on Windows), matching on the `libav*`/`libsw*` family of prefixes rather than a single library name. `FATAL_ERROR`s — never silently passes — when its inspection tool is missing, and lets the tool's own nonzero exit propagate rather than swallowing it into a default value.
- `tests/integration/CMakeLists.txt`: mirrors `tests/unit`'s Catch2 v3 wiring (`find_package(Catch2 3 REQUIRED)`, `Catch2::Catch2WithMain`, `mediadiff_apply_warnings`), injects `MEDIADIFF_BINARY` via the `$<TARGET_FILE:mediadiff>` generator expression, adds an explicit `add_dependencies(mediadiff_integration_tests mediadiff)`, and registers `static_link` as a separate `add_test` invoking the cmake script — kept out of the build graph so it doesn't slow incremental `cmake --build`.
- `tests/CMakeLists.txt` gained `add_subdirectory(integration)`.
- All 9 CTest tests pass (6 from Plan 01's unit suite + 3 new): `ctest --preset x64-linux --output-on-failure` → 100% passed.

## Task Commits

1. **Task 01-02-T1: Integration harness and the CLI-05 / BUILD-09 output assertions** — `a57ecde` (test)
2. **Task 01-02-T2: BUILD-04 static-link assertion — no FFmpeg shared-library dependency** — `6a9e6b3` (test)

**Plan metadata:** this SUMMARY + STATE.md update (pending final commit)

## Files Created/Modified
- `tests/integration/cli_harness.h` — `CliResult`, `EnvVars`, `run_cli()`; platform-isolated `detail::spawn_and_capture`
- `tests/integration/test_version_output.cpp` — `version_output` and `vmaf_absent` TEST_CASEs
- `tests/integration/CMakeLists.txt` — integration target wiring, `MEDIADIFF_BINARY` injection, `static_link` CTest registration
- `tests/integration/check_static_link.cmake` — BUILD-04 platform-dispatched dynamic-dependency check
- `tests/CMakeLists.txt` — `add_subdirectory(integration)`

## Decisions Made

- **Catch2 test naming:** did not apply Plan 01's `TEST_PREFIX "unit."` convention to this suite. The plan's own acceptance criteria require `ctest -R version_output` / `-R vmaf_absent` to match directly; naming each `TEST_CASE` with that literal substring (`"version_output - CLI-05 fields present..."`, `"vmaf_absent - BUILD-09 optional feature..."`) satisfies the filter without a prefix, confirmed by `ctest --preset x64-linux -N -R 'version_output|vmaf_absent'` listing exactly 2 tests.
- **BUILD-04 negative control, built not found:** the sandbox's `/usr/local/bin/ffmpeg` is itself statically built (its `ldd` shows no `libav*`/`libsw*` entries), so no naturally-occurring dynamically-linked-against-FFmpeg binary existed to point the script at. Compiled one on the spot: `gcc dyn_probe.c -o dyn_probe -Wl,--no-as-needed -l:libavcodec.so.60 -L/lib/x86_64-linux-gnu` against the apt-installed `libavcodec60`/`libavutil58` packages already present on the host — `--no-as-needed` forces a real `DT_NEEDED` entry even though the trivial `main(){return 0;}` body never calls into the library. This lives only in the session scratchpad, never in the repo.
- **Regex-based field assertions over hardcoded literals:** used `std::regex` with `^...$` (multiline) patterns for the semantic-version-shaped lines instead of hardcoding `"mediadiff 0.1.0"` verbatim, so the test keeps asserting the *shape* CLI-05 requires rather than pinning to today's exact version string.

## Deviations from Plan

None - plan executed exactly as written. All artifacts, CMake targets, compile definitions, CTest test names, and C++ symbols match the plan's `<artifacts_produced>` block exactly (`CliResult`, `run_cli`, `MEDIADIFF_BINARY`, `mediadiff_integration_tests`, `version_output`/`vmaf_absent`/`static_link`).

## Issues Encountered

None. The build tree from Plan 01 was warm; `cmake --preset x64-linux && cmake --build --preset x64-linux` for this plan's additions completed in seconds (incremental compile of two new translation units, no FFmpeg rebuild).

## User Setup Required

None - no external service configuration required.

## Captured Verification Evidence

**Full `--version` output (later phases assert against this shape):**
```
mediadiff 0.1.0
libavcodec 62.28.100 (built against 62.28.100)
libavformat 62.12.100 (built against 62.12.100)
libavutil 60.26.100 (built against 60.26.100)
license: LGPL version 2.1 or later
features: 
```
(The blank line after `features: ` is CLI11's own trailing newline after the returned string, which itself already ends in `\n` — not a rendering bug.)

**Static-link negative control — exact command and result:**
```
$ cmake -DMEDIADIFF_BINARY=/tmp/.../scratchpad/dyn_probe -P tests/integration/check_static_link.cmake
CMake Error at tests/integration/check_static_link.cmake:57 (message):
  BUILD-04 violation:
  /tmp/.../scratchpad/dyn_probe dynamically depends on an FFmpeg shared library:
    libavcodec.so.60 => /lib/x86_64-linux-gnu/libavcodec.so.60 (...)
    libswresample.so.4 => /lib/x86_64-linux-gnu/libswresample.so.4 (...)
    libavutil.so.58 => /lib/x86_64-linux-gnu/libavutil.so.58 (...)

Exit code: 1
```
The script caught not just the directly-linked `libavcodec` but its transitive `libswresample`/`libavutil` dependencies too — proof the prefix-family match (not a single hardcoded name) works as intended.

**Positive control (the real, statically-linked artifact):**
```
$ ldd build/x64-linux/mediadiff | grep -Ec 'libavcodec|libavformat|libavutil|libswscale|libswresample'
0
$ ctest --preset x64-linux -R static_link --output-on-failure
100% tests passed, 0 tests failed out of 1
```

**Reduced-PATH run:** no adjustment beyond the two standard directories was needed — `env -i PATH=/usr/bin:/bin ./build/x64-linux/mediadiff --version` exits 0 with the full version block on the first attempt, confirming D-04's UTF-8/no-external-FFmpeg premise holds for the version path specifically (full CLI-09 Windows path handling remains out of this plan's scope).

## Next Phase Readiness

- `tests/integration/cli_harness.h`'s `run_cli()` is ready for reuse by every later phase's CLI-level integration test — spawn logic and assertion logic are kept in separate files per the plan's own discipline.
- Plan 01-03 (Windows UTF-8 / `util/fs.h`) and Plan 01-04/01-05 (ENG-16 lint, CI matrix) can build on `tests/CMakeLists.txt`'s now-complete `add_subdirectory(unit)` + `add_subdirectory(integration)` structure without further scaffolding changes.
- No blockers. `CMakeLists.txt`, `src/cli/main.cpp`, `src/util/fs.h`, and `tests/unit/CMakeLists.txt` were not touched, honoring Plan 01-03's ownership of those files.

---
*Phase: 01-foundation-toolchain*
*Completed: 2026-08-13*

## Self-Check: PASSED

All 5 created/modified files verified present on disk; both task commit hashes (`a57ecde`, `6a9e6b3`) verified present in `git log`.
