# Phase 1: Foundation & Toolchain - Research

**Researched:** 2026-08-12
**Domain:** CMake + vcpkg build system, FFmpeg static linking, 3-OS CI, CLI11 argv wiring
**Confidence:** MEDIUM-HIGH — every load-bearing claim below was fetched live from an authoritative source this session (FFmpeg source tree, vcpkg port files, Microsoft Learn) rather than pulled from training data. Two items (macOS framework linkage gap, `overrides`-based FFmpeg pin) are genuinely new findings not present in `.planning/research/STACK.md` and materially change how Phase 1 should be built.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01: Pin the FFmpeg baseline to 8.1 "Hoare" for Phase 1; bump to 9.x in a later phase as a deliberate, recorded migration.** Reversibility: costly. Record the pin as an explicit decision in PROJECT.md Key Decisions with this rationale. BUILD-10 is satisfied by the recording, not merely by the pin existing.
- **D-02: Use `tl-expected` from vcpkg, aliased behind `mediadiff::expected<T,E>` in a single `src/util/expected.h`.** Reversibility: reversible. No file outside `util/expected.h` may name `tl::expected` directly.
- **D-03: Assert the LGPL configuration at runtime from the linked library's own self-report, not from the manifest.** Reversibility: reversible. Implement as a compiled test run in CI on every platform, and surface the string in `--version`. Gotcha: `avutil_license()` returns `"LGPL version 2.1 or later"` for LGPL builds and `"GPL version 2 or later"` for GPL builds — a substring test for `"GPL"` matches both. The assertion must positively match the LGPL form. A cheap CMake-time check of the resolved vcpkg feature list is worth adding on top, but does not replace the runtime assertion.
- **D-04: Windows text handling uses the wide-API path from doc 00 §3.2, plus a UTF-8 active-code-page manifest as belt-and-braces.** Reversibility: costly. `GetCommandLineW`/`CommandLineToArgvW` → convert to UTF-8 once → feed CLI11; all internal strings UTF-8; mediadiff's own file I/O through the `util/fs.h` shim; `SetConsoleMode` for VT sequences. **Open verification item, resolved by this research (see Summary/Pattern 4):** libavformat converts UTF-8 paths to UTF-16 internally on Windows via `avpriv_open`/`win32_open` — confirmed against the pinned-baseline-era FFmpeg source. A custom `AVIOContext` does NOT become Phase 1 scope.
- **D-05: Cache vcpkg binaries in a NuGet feed on GitHub Packages.** Reversibility: reversible. The `x-gha` backend is removed (~April 2025, `microsoft/vcpkg-tool#1662`) — do not follow doc 00 §5.1's stale reference. Prefer NuGet/GitHub Packages over `lukka/run-vcpkg` or plain `actions/cache` — both draw on the Actions cache's 10 GB/repo LRU-eviction quota; GitHub Packages storage is separate.
- **D-06: Three host triplets gate Phase 1; `arm64-linux` and `x64-osx` build as non-blocking jobs.** Reversibility: reversible. Blocking: `x64-linux`, `arm64-osx`, `x64-windows-static-md`.
- **D-07: Create both CMake targets — `libmediadiff` and `mediadiff` — with the full doc 00 §7 directory tree, and enforce the library boundary in CI from day one.** Reversibility: costly. ENG-16 (the library writes nothing to stdout and never calls `exit()`) must be enforced structurally from Phase 1. Add a CI lint — roughly five lines — that greps `libmediadiff` sources for `printf|std::cout|std::cerr|exit(` and fails the build. Create empty directories with `.gitkeep`. Do NOT write placeholder headers with invented APIs.
- **D-08: `scripts/gen_corpus` uses a system `ffmpeg` ≥ 6.1, and records the exact ffmpeg version and build configuration into a manifest beside the generated corpus.** Reversibility: reversible. `-flags +bitexact` makes an encoder deterministic for a given encoder version, not across versions — recording the generator's identity is what makes the determinism claim checkable.

### Claude's Discretion

Under `--auto` every gray area was resolved with the recommended option, so nothing was explicitly delegated by the user. The planner retains normal latitude on:

- CMake preset naming and directory layout within the doc 00 §7 tree
- Which specific GitHub Actions runner images to target (verify current MSVC 17.x / Xcode 15.x point releases at planning time per STACK's open question — see Environment Availability below)
- How `--version` formats its output, provided it contains tool version, FFmpeg library versions, the license string (D-03), and the enabled-feature list
- Test framework wiring details, noting STACK's flag that Catch2 v3 requires different CMake integration than v2

### Deferred Ideas (OUT OF SCOPE)

- **Bumping FFmpeg to 9.x** — deliberately deferred out of Phase 1 per D-01.
- **`arm64-linux` and `x64-osx` as blocking CI gates** — non-blocking in Phase 1 per D-06.
- **Pinning the corpus generator's ffmpeg** — deferred per D-08.
- **A custom `AVIOContext` over wide-opened Windows handles** — this research resolves D-04's open verification item in the negative (not needed); do not build it. See Summary and Architecture Patterns → Pattern 4.
- **Universal macOS binary** — already recorded as v2 (DIST-01), not a Phase 1 concern.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-------------------|
| BUILD-01 | Clean-checkout build on Linux/macOS/Windows via CMake ≥3.25 presets | Architecture Patterns → Pattern 1 (CMakePresets shape, 3 blocking + 2 non-blocking triplets per D-06) |
| BUILD-02 | Reproducible dependency resolution via vcpkg manifest + pinned `builtin-baseline`, vcpkg as git submodule | Standard Stack → Installation (`vcpkg.json` with live-verified `builtin-baseline` SHA and submodule pin instructions) |
| BUILD-03 | Decode-only LGPL FFmpeg subset with build-time GPL assertion | Architecture Patterns → Pattern 3 (exact `avutil_license()` strings, correct positive match, configure-time pre-check) |
| BUILD-04 | Single static binary per platform, no runtime FFmpeg install required | Architecture Patterns → Pattern 2 (link wiring per OS); Validation Architecture (smoke test shape) |
| BUILD-05 | 3-OS CI matrix, warnings-as-errors, green required to merge | Architecture Patterns → Pattern 1; Environment Availability (runner image notes) |
| BUILD-06 | vcpkg binary caching so incremental builds don't rebuild FFmpeg | Common Pitfalls → Pitfall 3; Sources (full NuGet/GitHub Packages workflow, live-fetched) |
| BUILD-07 | `expected<T,E>` pinned as explicit dependency (C++20, not C++23) | Standard Stack (`tl-expected` 1.3.1, live-verified); Don't Hand-Roll |
| BUILD-08 | `scripts/gen_corpus.{sh,ps1}` deterministic fixture synthesis, no committed media binaries | Code Examples → `gen_corpus` skeleton; Common Pitfalls → Pitfall 5 |
| BUILD-09 | Optional `libvmaf` gated behind `MEDIADIFF_WITH_VMAF`, absent by default | Package Legitimacy Audit; Validation Architecture (BUILD-09 test row) |
| BUILD-10 | FFmpeg major-version baseline explicitly recorded as a decision | Standard Stack (D-01 restated with live-verified vcpkg version data); Assumptions Log A3 |
| CLI-05 | `--version` prints tool version, linked FFmpeg library versions, enabled features | Code Examples → CLI11 minimal `--version`-only wiring |
| CLI-09 | Windows non-ASCII paths work end to end (UTF-16 argv, `util/fs.h` shim, VT sequences) | Architecture Patterns → Pattern 4 (RESOLVED — source-traced `avpriv_open`/`win32_open` chain) |

</phase_requirements>

## Summary

This document answers the nine open HOW questions `.planning/research/STACK.md` deliberately left for Phase 1 planning. Three findings are significant enough to change the plan shape, not just fill in detail:

1. **The Windows UTF-8 open question (D-04) is now RESOLVED, not merely researched.** Reading `libavutil/file_open.c`, `libavutil/wchar_filename.h`, and `libavformat/file.c` directly confirms the chain `avformat_open_input` → (file protocol) → `file_open()` → `avpriv_open()` → `win32_open()` (Windows) → `get_extended_win32_path()` → `utf8towchar()` → `MultiByteToWideChar(CP_UTF8,...)` → `_wsopen()`. **libavformat does convert UTF-8 filenames to UTF-16 internally on Windows, for the default `file:` protocol.** The custom `AVIOContext` fallback in the Deferred Ideas list can stay deferred — do not build it in Phase 1.

2. **Pin FFmpeg to 8.1 via `vcpkg.json` `overrides`, not by hunting for a `builtin-baseline` commit whose HEAD happened to be on 8.1.** vcpkg's `overrides` array pins one package to an exact version *independent* of `builtin-baseline`, which is the documented, supported mechanism for holding one dependency back while everything else in the manifest tracks a current baseline. This is materially better than what STACK.md and the phase description implied ("find the baseline SHA that resolves to 8.1") — that approach would freeze *every* other dependency's version too.

3. **macOS static linking has a real, unconditional gap vcpkg does not paper over.** `ports/ffmpeg/portfile.cmake` unconditionally appends `--enable-appkit --enable-avfoundation --enable-coreimage --enable-audiotoolbox --enable-videotoolbox` whenever the target is macOS — regardless of which vcpkg *features* are requested in `vcpkg.json`. But `ports/ffmpeg/FindFFMPEG.cmake.in`'s system-library pass-through list (`append_dependencies()`) only recognizes Windows system libs and `pthread`/`atomic`/`m` — **it contains no Apple-framework handling at all.** `mediadiff`'s own `CMakeLists.txt` must explicitly link the Apple frameworks on macOS; relying on `${FFMPEG_LIBRARIES}` alone is a documented gap in the vcpkg module, confirmed by reading its source, not inferred.

**Primary recommendation:** Use vcpkg's own `find_package(FFMPEG REQUIRED)` / `FFMPEG_INCLUDE_DIRS` / `FFMPEG_LIBRARY_DIRS` / `FFMPEG_LIBRARIES` module (it already handles Windows system-lib linkage correctly), add an explicit `if(APPLE)` framework block for the gap above, pin FFmpeg via `overrides`, and treat the GPL assertion as two layers (a best-effort CMake-configure check plus the mandatory runtime `avutil_license()` check) exactly as D-03 specifies.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Dependency acquisition & pinning (vcpkg manifest, submodule, overrides) | Build/CI | — | Resolved once at configure time; no runtime component |
| Static linking & system-lib wiring (per-OS) | Build/CI | — | CMake `target_link_libraries` shape, evaluated at link time |
| GPL/license assertion | Backend (libmediadiff, compiled-in check) | Build/CI (configure-time pre-check) | Runtime self-report is the source of truth (D-03); CMake check is a fast-fail convenience layer only |
| Windows UTF-8 argv/path handling | CLI (`cli/main.cpp` argv conversion) + Backend (`util/fs.h` shim) | — | argv conversion is CLI-only; file I/O shim is shared library code any analyzer will call later |
| `--version` output composition | CLI (`cli/`) | Backend (`libmediadiff` exposes version/license/feature accessors) | CLI renders; library reports facts — matches the lib/cli split (D-07) |
| CI matrix + binary caching | Build/CI | — | Entirely GitHub Actions + vcpkg/NuGet, no application code |
| `scripts/gen_corpus` | Build/CI (dev tooling) | — | Runs on a developer/CI machine against a system `ffmpeg`, never linked into the binary |
| ENG-16 stdout/exit lint | Build/CI (CI step or CTest test) | — | Static analysis over `libmediadiff` sources, not a runtime concern |

## Standard Stack

### Core
(Unchanged from STACK.md — repeated here only where Phase 1 adds a concrete version STACK.md did not pin.)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| FFmpeg | **8.1 "Hoare"**, pinned via `vcpkg.json` `overrides` (not baseline) | decode-only LGPL demux/decode | D-01 locked decision; overrides is the correct vcpkg mechanism — see Standard Stack → Installation below |
| tl-expected | **1.3.1** `[VERIFIED: vcpkg registry — ports/tl-expected/vcpkg.json, fetched live]` | `expected<T,E>` backing D-02 | CC0-1.0, header-only, vcpkg-current; aliased behind `mediadiff::expected` per D-02 |
| Catch2 | **3.15.3** (confirmed in STACK.md) | unit/integration tests | `find_package(Catch2 3 REQUIRED)` + `Catch2::Catch2WithMain` — v3 API, not v2 |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| CLI11 | 2.6.2 (STACK.md) | argv parsing | `--version` only in Phase 1; subcommands added Phase 2 |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `overrides` for the FFmpeg pin | Pin `builtin-baseline` to an old vcpkg commit | Freezes every dependency (CLI11, fmt, nlohmann-json, …) to that commit's versions too — loses the ability to track current versions for everything else while holding only FFmpeg back. `overrides` is strictly better for a single-package pin. |
| `expected-lite` | `tl-expected` | D-02 names `expected-lite` as acceptable "if the port is unhealthy at pin time" — it is not; `tl-expected` is current and healthy, use it. |

**Installation:**
```json
// vcpkg.json
{
  "name": "mediadiff",
  "version-string": "0.1.0",
  "builtin-baseline": "e90cc0982b7cfae62447f1f3bed1fbca0bc8f6be",
  "overrides": [
    { "name": "ffmpeg", "version": "8.1", "port-version": 4 }
  ],
  "dependencies": [
    {
      "name": "ffmpeg",
      "default-features": false,
      "features": ["avcodec", "avformat", "swscale", "swresample", "dav1d", "zlib"]
    },
    "cli11", "fmt", "nlohmann-json", "tomlplusplus", "xxhash",
    "libebur128", "catch2", "tl-expected"
  ],
  "features": {
    "vmaf": { "description": "libvmaf quality metric", "dependencies": ["libvmaf"] }
  }
}
```

`[VERIFIED: microsoft/vcpkg versions/f-/ffmpeg.json, fetched live 2026-08-12]` — the `git-tree` for `"version": "8.1", "port-version": 4` is `3c613502bfbafe7fc10b7504e5bea2e8fb37775e`; this is the *newest* patch of the port at exactly FFmpeg 8.1 (not 8.1.1/8.1.2, which are later FFmpeg point releases — confirm with the team at plan time whether "8.1 Hoare" per D-01 means the literal `8.1` port entry or the latest `8.1.x` patch line; both exist in vcpkg's version DB with distinct `git-tree` hashes). `builtin-baseline` above is vcpkg `master`'s HEAD commit as of this research session `[VERIFIED: api.github.com/repos/microsoft/vcpkg/commits/master, fetched live 2026-08-12]` — **the planner must re-fetch this baseline SHA at execution time**, it will be stale by then; only the `overrides` entry for `ffmpeg` needs to stay pinned to 8.1.

```bash
# Submodule pin (once)
git submodule add https://github.com/microsoft/vcpkg.git vcpkg
git -C vcpkg checkout <the same commit SHA used as builtin-baseline above>
```

**Version verification performed live this session:**
- `tl-expected` current vcpkg version: `1.3.1` `[VERIFIED: raw.githubusercontent.com/microsoft/vcpkg/master/ports/tl-expected/vcpkg.json]`
- FFmpeg 8.1 exact port-version entries and git-tree hashes: `[VERIFIED: raw.githubusercontent.com/microsoft/vcpkg/master/versions/f-/ffmpeg.json]`
- vcpkg `master` HEAD SHA `e90cc0982b7cfae62447f1f3bed1fbca0bc8f6be` dated `2026-08-12T16:54:38Z` `[VERIFIED: GitHub REST API, live]`

## Package Legitimacy Audit

> `gsd-tools` (the `package-legitimacy check` seam) was not available in this execution environment — it is not installed/resolvable in this project's toolchain. The audit below was performed manually against each package's live vcpkg port page and upstream GitHub repository, per the documented fallback ("packages... not verified against an authoritative source are tagged `[ASSUMED]`" only applies to packages *not* checked against an authoritative source — these were).

All packages below were already version-verified in `.planning/research/STACK.md` against the live vcpkg registry; this table adds the legitimacy signals (age, source repo, adoption) that STACK.md did not capture.

| Package | Registry | Age (upstream repo) | Source Repo | Verdict | Disposition |
|---------|----------|----------------------|-------------|---------|-------------|
| ffmpeg | vcpkg (`ports/ffmpeg`) | FFmpeg project, 20+ yrs | github.com/FFmpeg/FFmpeg | OK | Approved |
| cli11 | vcpkg | CLIUtils/CLI11, 9+ yrs | github.com/CLIUtils/CLI11 | OK | Approved |
| fmt | vcpkg | fmtlib/fmt, 10+ yrs | github.com/fmtlib/fmt | OK | Approved |
| nlohmann-json | vcpkg | nlohmann/json, 10+ yrs | github.com/nlohmann/json | OK | Approved |
| tomlplusplus | vcpkg | marzer/tomlplusplus, 6+ yrs | github.com/marzer/tomlplusplus | OK | Approved |
| xxhash | vcpkg | Cyan4973/xxHash, 10+ yrs | github.com/Cyan4973/xxHash | OK | Approved |
| libebur128 | vcpkg | jiixyj/libebur128, 10+ yrs, stable (see STACK.md) | github.com/jiixyj/libebur128 | OK | Approved |
| catch2 | vcpkg | catchorg/Catch2, 10+ yrs | github.com/catchorg/Catch2 | OK | Approved |
| libvmaf (opt-in feature) | vcpkg | Netflix/vmaf, 8+ yrs | github.com/Netflix/vmaf | OK | Approved (gated behind `MEDIADIFF_WITH_VMAF`, D-…/BUILD-09) |
| tl-expected | vcpkg | TartanLlama/expected, 7+ yrs, v1.3.1 current | github.com/TartanLlama/expected | OK | Approved |

**Packages removed due to `[SLOP]` verdict:** none.
**Packages flagged as suspicious `[SUS]`:** none — every dependency is a long-established, high-adoption C/C++ library with a public source repo, matching STACK.md's own confirm/correct ledger.

*Note: the manual-fallback verdicts above are `[CITED: vcpkg port registry + upstream GitHub]`, not `[VERIFIED: gsd-tools]` — the planner should re-run the automated seam if it becomes available before execution, but there is no material risk signal in any of these packages.*

## Architecture Patterns

### System Architecture Diagram — Phase 1 build/link flow

```
 developer / CI runner
        │
        ▼
 [git clone + submodule init] ──► vcpkg/ (pinned commit)
        │
        ▼
 [cmake --preset <os>-<triplet>]
        │  CMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
        │  (set via CMakePresets cacheVariables, BEFORE CMakeLists.txt's project())
        ▼
 [vcpkg manifest resolve] ── vcpkg.json (overrides: ffmpeg=8.1) + builtin-baseline
        │
        ├─ cache hit? ──► restore from NuGet/GitHub-Packages feed (VCPKG_BINARY_SOURCES)
        └─ cache miss ──► build FFmpeg 8.1 from source (15–40 min), upload to feed
        ▼
 [CMake configure] ── project() ── find_package(FFMPEG REQUIRED), find_package(CLI11/fmt/...)
        │                          optional: configure-time forbidden-feature grep (fast-fail GPL check)
        ▼
 [CMake build] ── libmediadiff (no stdout/exit — ENG-16 lint) ── mediadiff (cli/main.cpp)
        │                                                              │
        │                                                     app.manifest embedded (UTF-8 codepage)
        ▼
 [CTest] ── Catch2 unit tests (incl. runtime avutil_license() assertion) ── ENG-16 lint test
        ▼
 [mediadiff --version] ── prints: tool version | FFmpeg lib versions | license string | feature flags
```

### Recommended Project Structure
```
mediadiff/
├─ CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  LICENSE
├─ vcpkg/                       # submodule, pinned commit
├─ app.manifest                 # Windows UTF-8 active code page (CLI-09)
├─ src/
│  ├─ cli/                      # main.cpp only in Phase 1: argv→UTF-8, CLI11 --version, tty color/VT setup
│  ├─ core/                     # .gitkeep (empty — Phase 2)
│  ├─ config/                   # .gitkeep
│  ├─ probe/                    # .gitkeep
│  ├─ analyzers/                # .gitkeep
│  ├─ compare/                  # .gitkeep
│  ├─ report/                   # .gitkeep
│  └─ util/                     # expected.h (D-02), fs.h (D-04 shim), version.h/.cpp (--version data)
├─ docs/checks/                 # .gitkeep
├─ tests/
│  ├─ unit/                     # Catch2: license assertion, expected<> alias smoke test
│  └─ integration/               # drives the real `mediadiff` binary: --version output shape
├─ scripts/gen_corpus.{sh,ps1}  # skeleton only — system ffmpeg version check + manifest emission
└─ .github/workflows/ci.yml     # 3-OS blocking matrix + 2 non-blocking (D-06), NuGet cache (D-05)
```

### Pattern 1: CMakePresets with vcpkg toolchain wired before `project()`

**What:** `CMAKE_TOOLCHAIN_FILE` is a *cache variable*, set by `cmake --preset` on the command line before CMake ever parses `CMakeLists.txt`. The trap is only hit if `CMakeLists.txt` itself calls `set(CMAKE_TOOLCHAIN_FILE ...)` before `project()`, which overrides the cache value — never do that.

**Example (confirms and extends STACK.md's template with the 3 blocking + 2 non-blocking triplets from D-06):**
```jsonc
// CMakePresets.json — Source: pattern confirmed against Microsoft's vcpkg+CMake integration docs (STACK.md), version=6 schema
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
  "configurePresets": [
    {
      "name": "base", "hidden": true, "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    { "name": "x64-linux",           "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-linux" } },
    { "name": "arm64-osx",           "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "arm64-osx" } },
    { "name": "x64-windows-static-md","inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-windows-static-md" } },
    { "name": "arm64-linux",         "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "arm64-linux" } },
    { "name": "x64-osx",             "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-osx" } }
  ],
  "buildPresets": [
    { "name": "x64-linux", "configurePreset": "x64-linux" },
    { "name": "arm64-osx", "configurePreset": "arm64-osx" },
    { "name": "x64-windows-static-md", "configurePreset": "x64-windows-static-md" },
    { "name": "arm64-linux", "configurePreset": "arm64-linux" },
    { "name": "x64-osx", "configurePreset": "x64-osx" }
  ]
}
```
Per D-06: `x64-linux`, `arm64-osx`, `x64-windows-static-md` are CI-blocking; `arm64-linux`, `x64-osx` run as non-blocking jobs (`continue-on-error: true` in the workflow matrix entry).

### Pattern 2: FFmpeg link wiring — use vcpkg's `FindFFMPEG.cmake`, then patch its macOS gap

**What:** vcpkg's `ffmpeg` port ships a **module-mode** `FindFFMPEG.cmake` (not a CMake config-file package — there is no `FFmpeg::avcodec` imported target). It defines `FFMPEG_FOUND`, `FFMPEG_INCLUDE_DIRS`, `FFMPEG_LIBRARY_DIRS`, `FFMPEG_LIBRARIES`. `[VERIFIED: raw.githubusercontent.com/microsoft/vcpkg/master/ports/ffmpeg/FindFFMPEG.cmake.in, fetched live]`

Its `append_dependencies()` function resolves each of FFmpeg's `.pc`-declared link dependencies and — critically — has a hard-coded pass-through list for known system libraries:
```cmake
# Source: ports/ffmpeg/FindFFMPEG.cmake.in, append_dependencies(), verbatim
set(pass_through
    ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES}
    advapi32 bcrypt crypt32 gdi32 mfuuid ncrypt ole32 oleaut32 psapi secur32 shlwapi strmiids user32 uuid vfw32 ws2_32 usp10 cfgmgr32 rpcrt4
    -pthread -pthreads pthread atomic m
)
```
This is why the Windows failure mode described in the phase brief (needing to manually add `bcrypt`/`ws2_32`/`secur32`) **does not actually occur** if you consume `FFMPEG_LIBRARIES` as-is — the module already resolves and appends them. Same for Linux `-pthread`/`-lm` (present); `-ldl` is *not* in this list — add it defensively only if the linker reports undefined `dlopen`/`dlsym` (unlikely with this decode-only feature set, since no hwaccel/plugin-loading FFmpeg features are enabled).

**The macOS gap (new finding, not in STACK.md):** `[VERIFIED: raw.githubusercontent.com/microsoft/vcpkg/master/ports/ffmpeg/portfile.cmake, fetched live]` — the portfile unconditionally does:
```cmake
elseif(VCPKG_TARGET_IS_OSX)
    string(APPEND OPTIONS " --target-os=darwin --enable-appkit --enable-avfoundation --enable-coreimage --enable-audiotoolbox --enable-videotoolbox")
```
regardless of the `features` array in `vcpkg.json` — this is a platform default, not a toggle. `FindFFMPEG.cmake.in` was searched exhaustively for `APPLE`/`framework`/`CoreMedia`/`CoreVideo`/`VideoToolbox`/`AudioToolbox`/`AppKit`/`AVFoundation`/`CoreImage`/`Security`/`CoreFoundation` — **zero matches.** The module's `pass_through` list is Windows/pthread-only. Whether the frameworks end up in `FFMPEG_LIBRARIES` therefore depends entirely on whether FFmpeg's own generated `.pc` `Libs.private` lines get passed through the `@FFMPEG_DEPENDENCIES_RELEASE@` substitution unmodified (uncertain from the module alone) — **do not rely on this working by accident.** Add the frameworks explicitly:

```cmake
# CMakeLists.txt — Source: derived from ports/ffmpeg/portfile.cmake's unconditional macOS OPTIONS,
# cross-checked against FindFFMPEG.cmake.in's confirmed absence of framework handling
find_package(FFMPEG REQUIRED)

add_library(libmediadiff STATIC ${LIBMEDIADIFF_SOURCES})
target_include_directories(libmediadiff PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_link_directories(libmediadiff PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(libmediadiff PUBLIC ${FFMPEG_LIBRARIES})

if(APPLE)
  target_link_libraries(libmediadiff PUBLIC
    "-framework AppKit" "-framework AVFoundation" "-framework CoreImage"
    "-framework AudioToolbox" "-framework VideoToolbox"
    "-framework CoreMedia" "-framework CoreVideo" "-framework CoreFoundation" "-framework Security"
  )
endif()

add_executable(mediadiff src/cli/main.cpp)
if(WIN32)
  target_sources(mediadiff PRIVATE app.manifest)   # MSVC auto-embeds via /manifest:embed
endif()
target_link_libraries(mediadiff PRIVATE libmediadiff)
```

**Windows CRT note:** `x64-windows-static-md` gives static vcpkg `.lib`s against the *dynamic* CRT (`/MD`) `[VERIFIED, STACK.md — triplet file read directly]`. The one realistic failure mode not covered by the FindFFMPEG module is a *third-party* dependency's portfile hard-coding `/MT` and producing `LNK2038` — none of this manifest's ports are known to do that; treat any `LNK2038` at execution time as "check that specific port's portfile," not a systemic issue.

### Pattern 3: GPL runtime assertion — the correct positive match

**What:** `[VERIFIED: raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/version.c, fetched live]` — `avutil_license()` is:
```c
const char *avutil_license(void)
{
#define LICENSE_PREFIX "libavutil license: "
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
}
```
`FFMPEG_LICENSE` is a `configure`-generated macro in `config.h`. The exact strings — `[CITED: FFmpeg configure script logic + ffmpeg.org/legal.html, cross-checked]` — are:

| Configure flags | `FFMPEG_LICENSE` value |
|---|---|
| default (no `--enable-gpl`) | `"LGPL version 2.1 or later"` |
| `--enable-version3` (no gpl) | `"LGPL version 3 or later"` |
| `--enable-gpl` | `"GPL version 2 or later"` |
| `--enable-gpl --enable-version3` | `"GPL version 3 or later"` |
| `--enable-nonfree` | `"nonfree and unredistributable"` |

**The correct test** (per D-03's own gotcha, restated precisely): a substring test for `"GPL"` matches every row above except the default one — `"LGPL version 2.1 or later"` contains `"GPL"` as a substring (`L`+`GPL`). The assertion must be a **positive, exact match** against the one allowed string:

```cpp
// tests/unit/test_license.cpp — Source: derived from confirmed FFMPEG_LICENSE value table above
TEST_CASE("Linked FFmpeg is the expected LGPL build") {
    REQUIRE(std::string_view(avutil_license()) == "LGPL version 2.1 or later");
    // Do NOT write: REQUIRE(std::string_view(avutil_license()).find("GPL") == npos);
    // — that assertion PASSES on a GPL v2/v3 build too, because "LGPL..." also contains "GPL".
}
```
Cross-check `avcodec_license()` / `avformat_license()` the same way — all three are generated from the same `FFMPEG_LICENSE` macro, so they will always agree, but asserting all three in the test catches a hypothetical future FFmpeg build where per-library licensing diverges (it doesn't today, but the assertion is cheap).

**`avutil_configuration()`** `[VERIFIED: same source file]` returns the raw `FFMPEG_CONFIGURATION` string (the literal `./configure ...` invocation, e.g. containing `--disable-gpl` or `--enable-gpl`) — surface this in `--version` under `-v`/verbose as supporting evidence, but it is not the assertion itself (D-03 requires the license string, not the configure line, as the check).

**Configure-time pre-check (belt-and-braces, not a replacement):** vcpkg's manifest-mode install does **not** reliably produce the classic dpkg-style `installed/vcpkg/status` file `[CITED: microsoft/vcpkg-tool issue #38236 — "status file is not generated after install in manifest mode"]`, so do not depend on parsing that file. A more robust configure-time fast-fail is to check for the *absence* of GPL-only codec artifacts that would only exist if a `gpl` feature were accidentally resolved:
```cmake
# CMakeLists.txt — configure-time fast-fail (supplementary to the mandatory runtime test)
foreach(forbidden_lib x264 x265 xvidcore)
  find_library(_FORBIDDEN_${forbidden_lib} NAMES ${forbidden_lib}
               PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" NO_DEFAULT_PATH)
  if(_FORBIDDEN_${forbidden_lib})
    message(FATAL_ERROR "GPL-only codec '${forbidden_lib}' resolved into the vcpkg install tree — check vcpkg.json features/overrides.")
  endif()
endforeach()
```

### Pattern 4: Windows UTF-8 path handling — the resolved chain

**RESOLVED, with source citations** (this was CONTEXT.md D-04's explicit open verification item):

```
avformat_open_input(ctx, "C:\Users\日本語\clip.mp4", ...)  [filename is UTF-8 in mediadiff's own strings]
    → libavformat routes unprefixed paths to the "file" protocol (libavformat/file.c)
    → file_open(): fd = avpriv_open(filename, access, 0666)     [VERIFIED: file.c, fetched live]
    → avpriv_open() → #define open win32_open on Windows        [VERIFIED: libavutil/file_open.c]
    → win32_open(filename_utf8, ...):
          get_extended_win32_path(filename_utf8, &filename_w)   [VERIFIED: libavutil/wchar_filename.h]
              → utf8towchar() → MultiByteToWideChar(CP_UTF8, ...)
          _wsopen(filename_w, ...)
```
**Conclusion:** `avformat_open_input` accepts mediadiff's UTF-8 paths directly on Windows — no wrapping `AVIOContext` needed. `src/util/fs.h` only needs to cover **mediadiff's own** file I/O (writing `--json`/report files, reading `mediadiff.toml`, the `snapshot`/`compare` commands' own file access) — exactly as D-04 hoped, now confirmed rather than assumed. **The `AVIOContext` fallback item stays in Deferred Ideas — do not build it.**

`util/fs.h` shim shape for mediadiff's own I/O:
```cpp
// src/util/fs.h
#ifdef _WIN32
#include <windows.h>
inline std::wstring utf8_to_wide(std::string_view s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}
inline FILE* fopen_utf8(std::string_view path, const wchar_t* mode) {
    return _wfopen(utf8_to_wide(path).c_str(), mode);
}
#else
inline FILE* fopen_utf8(std::string_view path, const char* mode) { return fopen(path.data(), mode); }
#endif
```

argv conversion in `cli/main.cpp` (Windows only):
```cpp
#ifdef _WIN32
int wmain(int, wchar_t**) {
    int argc_w;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    std::vector<std::string> argv_utf8;
    for (int i = 0; i < argc_w; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, s.data(), len, nullptr, nullptr);
        argv_utf8.push_back(std::move(s));
    }
    LocalFree(argv_w);
    // build char* argv[] from argv_utf8, hand to CLI11's app.parse(argc, argv)
#else
int main(int argc, char** argv) {
#endif
```
CLI11 itself is encoding-agnostic — it just needs `argv` to already be UTF-8 `char*`, which the above guarantees, so **no CLI11-side changes are needed for UTF-8**, only the pre-parse conversion.

**UTF-8 active code page manifest** `[CITED: learn.microsoft.com/windows/apps/design/globalizing/use-utf8-code-page — official MS doc]`:
```xml
<!-- app.manifest -->
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1"
          xmlns:asmv3="urn:schemas-microsoft-com:asm.v3">
    <asmv3:application>
        <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">
            <activeCodePage>UTF-8</activeCodePage>
        </asmv3:windowsSettings>
    </asmv3:application>
</assembly>
```
Attach via `target_sources(mediadiff PRIVATE app.manifest)` — MSVC's default `/manifest:embed` linker behavior embeds `.manifest` source files automatically `[CITED: cmake.org mailing list + learn.microsoft.com/cpp/build/understanding-manifest-generation-for-c-cpp-programs]`. Requires Windows 10 1903+ (per D-04) — this is not a new requirement, just confirming the mechanism.

**VT sequences:**
```cpp
#ifdef _WIN32
HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
DWORD mode = 0;
if (GetConsoleMode(h, &mode))
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
```
This is well-established Windows API usage `[ASSUMED — standard MSDN pattern, not re-verified via live fetch this session; low risk, unchanged API for a decade]`.

### Anti-Patterns to Avoid
- **Assuming `FFMPEG_LIBRARIES` covers macOS frameworks because it covers Windows system libs.** It does the latter, verified; it does not do the former, also verified (absence confirmed by reading the source). Treat these as two independent facts, not one.
- **Substring-matching `"GPL"` for the license assertion.** Matches the LGPL string too — this is D-03's own named gotcha; the fix is an exact-match test against `"LGPL version 2.1 or later"`, not `find(...) == npos`.
- **Pinning FFmpeg via an old `builtin-baseline` commit.** Freezes every other manifest dependency's resolvable version to that same historical point. Use `overrides` instead.
- **Building a custom `AVIOContext` wide-path wrapper preemptively.** The verification this session shows it is unnecessary — `avformat_open_input` already handles UTF-8 on Windows via the `file:` protocol's `avpriv_open`/`win32_open` chain.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| `expected<T,E>` with monadic ops | Hand-rolled variant/error wrapper | `tl-expected` behind `mediadiff::expected` (D-02) | `and_then`/`transform`/`or_else` are exactly the kind of "small but easy to get subtly wrong" surface not worth owning |
| UTF-8/UTF-16 conversion on Windows | Custom codepage-detection heuristics | `MultiByteToWideChar`/`WideCharToMultiByte` with `CP_UTF8` directly, as FFmpeg itself does internally | FFmpeg's own solution (confirmed by source read) is the exact same primitive — no reason to invent something different |
| GPL/LGPL detection | Parsing `.pc` files, license header scraping, or vcpkg feature-list inspection as the *sole* check | `avutil_license()` exact-match runtime assertion (D-03) | The manifest and CMake-time checks test what was *requested*; only the linked library's own self-report tests what actually shipped |
| vcpkg binary cache orchestration | Custom S3/artifact-upload scripting | NuGet feed on GitHub Packages via `VCPKG_BINARY_SOURCES` (D-05) | vcpkg's own tooling (`vcpkg fetch nuget`) already speaks NuGet; this is the documented, Microsoft-supported path post-`x-gha` removal |

**Key insight:** every "hand-roll" temptation in this phase (UTF-8 conversion, license detection, cache orchestration) already has a first-party, source-verified solution one layer down (FFmpeg's own file_open.c, FFmpeg's own license macros, vcpkg's own NuGet integration). The research task here was almost entirely "read the actual implementation," not "evaluate alternatives."

## Common Pitfalls

### Pitfall 1: Trusting the FFmpeg-supplied CMake module uniformly across OSes
**What goes wrong:** `target_link_libraries(mediadiff PRIVATE ${FFMPEG_LIBRARIES})` links cleanly on Linux and Windows, then fails with dozens of undefined Objective-C/Core* symbols on macOS only.
**Why it happens:** `FindFFMPEG.cmake.in`'s system-library pass-through list has no Apple-framework branch (confirmed by direct source read), while the portfile unconditionally enables AppKit/AVFoundation/CoreImage/AudioToolbox/VideoToolbox on macOS.
**How to avoid:** Add the explicit `if(APPLE)` framework block from Pattern 2 above regardless of whether `FFMPEG_LIBRARIES` happens to already contain them — it's a no-op if redundant, load-bearing if not.
**Warning signs:** Linker errors referencing `_OBJC_CLASS_$_...`, `CMSampleBufferRef`, `VTDecompressionSession`, or similar Core*/AV* symbols only on the `arm64-osx` CI job.

### Pitfall 2: The `"GPL"` substring test
**What goes wrong:** A CI assertion like `REQUIRE(license.find("GPL") == std::string::npos)` passes on every build, including an accidentally GPL-linked one.
**Why it happens:** `"LGPL version 2.1 or later"` and `"GPL version 2 or later"` both contain the literal substring `"GPL"`.
**How to avoid:** Exact-match the full expected string (Pattern 3).
**Warning signs:** The assertion never fails even when you deliberately flip `default-features`/add a `gpl` feature to test it — that's the tell that the test is not discriminating.

### Pitfall 3: `GITHUB_TOKEN` alone does not grant NuGet package write in practice
**What goes wrong:** CI is configured per Microsoft's own tutorial with a `permissions: packages: write` block and `${{ secrets.GITHUB_TOKEN }}`, and the first cache-write silently fails or 403s.
**Why it happens:** `[CITED: learn.microsoft.com/vcpkg/consume/binary-caching-github-packages]` — the tutorial's own closing note states: *"The default `GITHUB_TOKEN`... does **not** have the required permissions to upload or download cached packages... use a Personal Access Token (PAT) instead"* with `packages:read`+`packages:write` scopes, stored as a repo secret (e.g. `VCPKG_PAT_TOKEN`).
**How to avoid:** Use a PAT from day one, not the ambient `GITHUB_TOKEN`, despite the `permissions:` block existing in the same doc (it grants `GITHUB_TOKEN` scope for *reading* packages/other repo operations, not for this specific NuGet-push flow).
**Warning signs:** First CI run "succeeds" (falls back to a full 15–40 min FFmpeg rebuild) but never gets faster on subsequent runs — that's the signature of a cache that's readable-but-not-writable, or not writable at all.

### Pitfall 4: vcpkg manifest-mode installs don't produce the classic `status` file
**What goes wrong:** A configure-time GPL pre-check script tries to parse `installed/vcpkg/status` (the dpkg-style file vcpkg has historically used) and finds nothing, silently no-oping the check.
**Why it happens:** `[CITED: github.com/microsoft/vcpkg-tool issue #38236]` — the status file is not reliably generated in manifest mode.
**How to avoid:** Use the `find_library`-based forbidden-codec-artifact check (Pattern 3) instead of parsing any vcpkg-internal state file; it works against the resolved install tree directly and doesn't depend on an internal format that isn't guaranteed present.

### Pitfall 5: Corpus determinism claims that don't record the generator identity
**What goes wrong:** `-flags +bitexact -fflags +bitexact` fixtures diverge across two developers' machines or across a system-ffmpeg upgrade, and nobody can tell whether it's a real regression or an environment drift.
**Why it happens:** bitexact flags make an *encoder* deterministic for a *given encoder build* — not across FFmpeg versions (per D-08's own reasoning).
**How to avoid:** `scripts/gen_corpus` must emit a manifest recording the generating `ffmpeg -version` output (version string + `configuration:` line) beside every fixture, from day one — see Code Examples below.

## Code Examples

### CLI11 minimal `--version`-only wiring (CLI-05, will not need rework at Phase 2)
```cpp
// src/cli/main.cpp — Source: pattern confirmed against CLIUtils/CLI11 examples/simple.cpp and
// the CLI11 book's flags chapter (set_version_flag takes a std::string OR std::function<std::string()>)
#include <CLI/CLI.hpp>
#include "util/version.h"   // mediadiff::compose_version_string() — lives in libmediadiff

int run(int argc, char** argv) {   // called from main()/wmain() after the UTF-8 argv conversion
    CLI::App app{"media-aware regression diff", "mediadiff"};
    app.require_subcommand(0, 1);   // Phase 2 adds compare/snapshot/dir/inspect/list-checks/explain here;
                                     // require_subcommand(0,1) already tolerates "0 subcommands fired" for
                                     // the future implicit-compare positional trick (CLI-01) — set it now
                                     // so Phase 2 doesn't have to touch this line.

    // Lazily computed: only calls into libavutil/libavcodec/libavformat version APIs if --version is passed.
    app.set_version_flag("--version", []() { return mediadiff::compose_version_string(); });

    // No other flags/subcommands in Phase 1 — CLI-02/03/04/06/07/08/10 are Phase 2 scope.
    CLI11_PARSE(app, argc, argv);
    return 0;
}
```
```cpp
// src/util/version.h / .cpp — lives in libmediadiff so it's unit-testable without spawning the CLI binary
#include <fmt/format.h>
extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace mediadiff {
std::string compose_version_string() {
    // Compile-time macros (from the headers mediadiff was built against) vs runtime values
    // (from the linked library) can legitimately differ if the DLL/so at runtime isn't the
    // one built against — for a static binary they will always match, but report both anyway
    // for bug-report robustness; runtime is authoritative.
    return fmt::format(
        "mediadiff {}\n"
        "  libavcodec  {} (built {})\n"
        "  libavformat {} (built {})\n"
        "  libavutil   {} (built {})\n"
        "  license: {}\n"
        "  features: {}\n",
        MEDIADIFF_VERSION,                                   // from CMake project(VERSION ...) via configure_file
        av_version_info(), AV_STRINGIFY(LIBAVCODEC_VERSION),
        avformat_version(), AV_STRINGIFY(LIBAVFORMAT_VERSION),
        avutil_version(), AV_STRINGIFY(LIBAVUTIL_VERSION),
        avutil_license(),                                    // exact-match tested separately, see Pattern 3
        enabled_features_csv()                                // "vmaf" if MEDIADIFF_WITH_VMAF, else "" — never "cuda" in v1 (v2 scope)
    );
}
}
```
Note: `AV_VERSION_INFO`/`av_version_info()` returns a build-identifying string (e.g. `"n8.1"` or a git describe), not per-library numeric versions — use `avcodec_version()`/`avformat_version()`/`avutil_version()` (runtime, `AV_VERSION_MAJOR/MINOR/MICRO` macros to decode the packed int) for the authoritative per-library numbers `[ASSUMED — standard FFmpeg version-API usage, consistent with STACK.md's confirmed-stable API table, not independently re-fetched this session]`.

### ENG-16 lint (D-07) — CI-step shape, not build-blocking by default
```bash
#!/usr/bin/env bash
# scripts/lint_eng16.sh — Source: shape derived from D-07's "roughly five lines" intent;
# word-boundary regex to reduce false positives on identifiers like "myprintf_helper"
set -euo pipefail
HITS=$(grep -RnE '(^|[^A-Za-z0-9_])(printf|std::cout|std::cerr|exit\()' src/core src/config src/probe src/analyzers src/compare src/report 2>/dev/null \
       | grep -vE '^\s*//' || true)
if [ -n "$HITS" ]; then
    echo "ENG-16 violation: libmediadiff source writes to stdout/stderr or calls exit():"
    echo "$HITS"
    exit 1
fi
echo "ENG-16: clean."
```
**Known limitation, stated explicitly rather than hidden:** this is a line-based grep, not a tokenizer — it will not catch `exit(` inside a multi-line comment block (`/* ... */`) correctly (only `//`-prefixed lines are excluded) and will false-positive on a string literal that happens to contain the word `printf`. Given D-07's own "roughly five lines" framing, this tradeoff is intentional; if it ever produces a real false positive, the fix is a `// eng16-allow` marker line convention, not a rewrite into a full tokenizer.
**Wiring:** register as a CTest test (`add_test(NAME lint.eng16 COMMAND bash scripts/lint_eng16.sh)`), not a CMake build step — this keeps it in the `ctest` sampling loop (Validation Architecture below) without slowing every incremental `cmake --build`.

### `scripts/gen_corpus` skeleton (BUILD-08) — Phase 1 scope only, no fixture recipes yet
```bash
#!/usr/bin/env bash
# scripts/gen_corpus.sh — Source: version-check convention and manifest requirement per D-08
set -euo pipefail
MIN_MAJOR=6; MIN_MINOR=1
FFMPEG_VERSION_LINE=$(ffmpeg -version | head -n1)   # "ffmpeg version 7.0.2 Copyright (c) ..."
FFMPEG_CONFIG_LINE=$(ffmpeg -version | grep '^configuration:' || true)
VER=$(echo "$FFMPEG_VERSION_LINE" | sed -E 's/^ffmpeg version ([0-9]+)\.([0-9]+).*/\1 \2/')
MAJOR=$(echo "$VER" | cut -d' ' -f1); MINOR=$(echo "$VER" | cut -d' ' -f2)
if [ "$MAJOR" -lt "$MIN_MAJOR" ] || { [ "$MAJOR" -eq "$MIN_MAJOR" ] && [ "$MINOR" -lt "$MIN_MINOR" ]; }; then
    echo "gen_corpus requires system ffmpeg >= ${MIN_MAJOR}.${MIN_MINOR}, found: $FFMPEG_VERSION_LINE" >&2
    exit 1
fi

OUT_DIR="tests/fixtures"
mkdir -p "$OUT_DIR"
MANIFEST="$OUT_DIR/GENERATOR_MANIFEST.json"
cat > "$MANIFEST" <<EOF
{
  "generator": "$FFMPEG_VERSION_LINE",
  "configuration": "$FFMPEG_CONFIG_LINE",
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

# Phase 1: no fixture recipes yet (later phases add them here). Convention every recipe follows:
#   ffmpeg -flags +bitexact -fflags +bitexact -y -f lavfi -i <source-filter> ... "$OUT_DIR/<name>.mp4"
echo "gen_corpus: manifest written to $MANIFEST. No fixtures generated in Phase 1 (skeleton only)."
```
`gen_corpus.ps1` mirrors this in PowerShell (`ffmpeg -version` parsing via `-split`, same manifest JSON shape) — not reproduced here for brevity, same logic.

### Catch2 v3 + CTest wiring (STACK.md flagged v3 ≠ v2)
```cmake
# tests/unit/CMakeLists.txt — Source: catchorg/Catch2 devel/docs/cmake-integration.md, fetched live
find_package(Catch2 3 REQUIRED)   # vcpkg-provided 3.15.3, NOT the v2-era single-header catch.hpp
add_executable(mediadiff_unit_tests test_license.cpp test_expected.cpp)
target_link_libraries(mediadiff_unit_tests PRIVATE libmediadiff Catch2::Catch2WithMain)

include(CTest)
include(Catch)               # ships with the vcpkg catch2 port; provides catch_discover_tests()
catch_discover_tests(mediadiff_unit_tests)
```
`#include <catch2/catch_test_macros.hpp>` in test files — **not** `<catch2/catch.hpp>` (removed in v3).

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `x-gha` vcpkg binary cache provider | NuGet feed (GitHub Packages) or `lukka/run-vcpkg` | ~April 2025, `microsoft/vcpkg-tool#1662` | Doc 00 §5.1's "GitHub Actions cache or x-gha" framing is stale — D-05 already corrects this; this research just supplies the exact workflow YAML |
| Pin one dependency by choosing an old `builtin-baseline` commit | Pin via `vcpkg.json` `overrides` array | Documented vcpkg versioning feature, stable for years | Materially better for this phase — avoids freezing unrelated dependencies |
| `Catch2` v2 single-header (`catch.hpp`) | Catch2 v3, `<catch2/catch_test_macros.hpp>` + `Catch2::Catch2WithMain` | Catch2 3.0 (2022) | Any test scaffolding written from v2-era memory/training data will not compile against the vcpkg-provided 3.15.3 |

**Deprecated/outdated:**
- `x-gha` binary cache backend — removed from vcpkg tooling, not merely discouraged; referencing it emits a deprecation warning and caches nothing.
- Manual `builtin-baseline`-SHA-hunting for a single-package pin — works, but `overrides` is the tool actually built for this.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|----------------|
| A1 | `SetConsoleMode`/`ENABLE_VIRTUAL_TERMINAL_PROCESSING` usage pattern is correct as shown | Architecture Patterns → Pattern 4 | Low — this is a decade-stable Win32 API; if wrong, VT color sequences render as raw escape codes on Windows, caught immediately by the CLI-08/CLI-09 manual verification pass |
| A2 | `av_version_info()`/`AV_VERSION_MAJOR` decode pattern for per-library version numbers | Code Examples → CLI11 wiring | Low — API table already confirmed stable in STACK.md; only the exact macro-decoding idiom wasn't independently re-verified this session |
| A3 | `overrides` `"port-version": 4` for `"version": "8.1"` is the intended pin, not one of the `8.1.1`/`8.1.2` point releases also present in vcpkg's version DB | Standard Stack → Installation | Medium — if the team actually meant "the latest 8.1.x patch," the git-tree hash differs; the planner/executor should confirm which literal version string D-01 intends before locking the `overrides` entry, since both are valid vcpkg entries with different hashes |
| A4 | `-ldl` is not required for this decode-only feature set (no hwaccel/dlopen-based plugin loading enabled) | Architecture Patterns → Pattern 2 | Low — if wrong, it surfaces immediately as an undefined-symbol linker error in CI, trivially fixed by adding `dl` to the link line |

## Open Questions

1. **Which exact "8.1" — the bare `8.1` port-version-4 entry, or a later `8.1.1`/`8.1.2` patch?**
   - What we know: all three exist in vcpkg's version DB as independent, addressable entries with distinct `git-tree` hashes (quoted in Standard Stack → Installation).
   - What's unclear: D-01 says "8.1 'Hoare'" which is FFmpeg's *minor*-release codename; FFmpeg itself may have cut `8.1.1`/`8.1.2` patch releases under the same codename convention. This research did not determine whether those later vcpkg port entries correspond to genuinely different FFmpeg source tags or merely vcpkg port-file fixes against the same 8.1 source.
   - Recommendation: default to the bare `"version": "8.1", "port-version": 4` entry (matches D-01's literal wording most closely); the planner should have the executor verify this against FFmpeg's own release notes if precision matters before locking `overrides`.

2. **Does `FFMPEG_LIBRARIES` end up containing the macOS framework flags via the `.pc`-to-`append_dependencies` pipeline anyway, making the explicit `if(APPLE)` block redundant?**
   - What we know: `FindFFMPEG.cmake.in` has no framework-aware pass-through logic; the actual value of `@FFMPEG_DEPENDENCIES_RELEASE@` is substituted by the portfile at vcpkg-install time from FFmpeg's own generated `.pc` files, whose exact `Libs.private` token format for `-framework X` pairs wasn't traced end-to-end in this session.
   - What's unclear: whether that substitution mangles `-framework CoreMedia` into something `append_dependencies`'s `elseif(EXISTS "${lib_name}")`/`find_library` fallback mishandles.
   - Recommendation: keep the explicit `if(APPLE)` framework block regardless (Pattern 2) — it's a safe no-op if `FFMPEG_LIBRARIES` already has them (duplicate `-framework` flags to the linker are harmless), and load-bearing if it doesn't. Do not spend further research/planning time resolving this ambiguity; the defensive block dominates either way.

## Environment Availability

| Dependency | Required By | Available (this dev sandbox) | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | BUILD-01 | ✓ | 3.28.3 | — |
| git | BUILD-02 (submodule) | ✓ | 2.43.0 | — |
| Ninja | CMakePresets generator | ✓ | 1.11.1 | — |
| GCC | BUILD-01 Linux toolchain (≥12 required) | ✓ | 13.3.0 | — |
| Clang | BUILD-01 Linux toolchain (≥15 required) | ✓ | 18.1.3 | — |
| Docker | not required by this phase | ✓ (present) | — | — |
| NASM | FFmpeg Windows asm codepaths (D-…, STACK.md) | not checked (no Windows target in this sandbox) | — | Confirmed present on GitHub's `windows-latest`/`windows-2022` image per STACK.md; pin/verify on the actual CI runner at execution time |
| mono | NuGet caching on Linux/macOS CI runners | not checked (this sandbox isn't the CI runner) | — | `ubuntu-24.04`-based runners (current `ubuntu-latest`) do **not** ship mono preinstalled `[CITED: learn.microsoft.com/vcpkg/consume/binary-caching-github-packages]` — CI workflow must `apt install mono-complete` (Linux) / `brew install mono` (macOS) explicitly |

**Missing dependencies with no fallback:** none identified for this dev sandbox; this table intentionally does not stand in for probing the actual 3 CI runner images, which the planner should do at execution time (see "Claude's Discretion" in CONTEXT.md re: verifying current MSVC 17.x / Xcode 15.x point releases).

**Missing dependencies with fallback:** `mono` on `ubuntu-latest`/`macos-latest` runners — install explicitly in the CI workflow, do not assume preinstalled.

**Runner image note** `[CITED: WebSearch, GitHub Actions runner-images release notes and changelog, 2026]`: `windows-2022` still maps to Visual Studio 2022 as of this research date; `macos-14` is being deprecated (unsupported after 2026-11-02) — prefer `macos-15` for the `arm64-osx` blocking job unless there's a specific reason to pin `macos-14`. Re-verify at execution time per CONTEXT.md's own discretion note.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 3.15.3 (vcpkg) + CTest |
| Config file | none yet — `tests/unit/CMakeLists.txt` is Wave 0 scope |
| Quick run command | `ctest --preset <triplet> -R unit --output-on-failure` |
| Full suite command | `ctest --preset <triplet> --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|--------------------|-------------|
| BUILD-01 | Clean-checkout build succeeds on 3 OSes | CI job (not a unit test) | `cmake --preset <triplet> && cmake --build --preset <triplet>` | ❌ Wave 0 |
| BUILD-02 | vcpkg manifest resolves reproducibly | CI (two-run comparison) | second `cmake --preset` run restores identical package set from lockfile/baseline | ❌ Wave 0 |
| BUILD-03 | Runtime LGPL assertion, exact-match | unit | `ctest -R test_license` (Catch2, Pattern 3) | ❌ Wave 0 |
| BUILD-04 | Static binary runs with no system FFmpeg | integration/smoke | run binary in a minimal container/PATH-stripped shell; Linux: `ldd build/mediadiff \| grep -i avcodec` expected empty | ❌ Wave 0 |
| BUILD-05 | Warnings-as-errors, 3-OS CI green | CI config (not testable in isolation) | CI job status | n/a |
| BUILD-06 | Binary cache restores on repeat run | CI (timing/log assertion) | compare 2nd-run wall-clock or NuGet restore log lines | ❌ Wave 0 |
| BUILD-07 | `expected<T,E>` pinned, single alias header | unit (compile-time smoke) | `ctest -R test_expected` — instantiate `mediadiff::expected<int,Error>`, exercise `and_then` | ❌ Wave 0 |
| BUILD-08 | `gen_corpus` deterministic, manifest emitted | integration | run script twice, diff `GENERATOR_MANIFEST.json` byte-identical (except timestamp field) | ❌ Wave 0 |
| BUILD-09 | `vmaf` absent by default | integration | default build `--version` output does NOT contain `"vmaf"` in feature list | ❌ Wave 0 |
| BUILD-10 | FFmpeg baseline recorded as explicit decision | doc check (manual, not automatable) | PROJECT.md Key Decisions table contains D-01 entry | n/a |
| CLI-05 | `--version` prints tool+FFmpeg+license+features | integration | run binary `--version`, regex-match all 4 required fields present | ❌ Wave 0 |
| CLI-09 | Non-ASCII path opens correctly on Windows; VT color renders | integration (Windows-only CI job) | `util/fs.h`'s `fopen_utf8` opens a fixture with a non-ASCII filename created by the test itself; VT check is a manual/visual step (see below) | ❌ Wave 0 |

**Note on CLI-09's automatable scope in Phase 1:** the full success criterion #5 ("a path containing non-ASCII characters opens correctly... with color output still rendering") mixes an automatable file-I/O assertion (`util/fs.h` round-trip) with a VT-rendering check that's inherently visual. Phase 1 should automate the file-I/O half (`fs.h` open/read/write round-trip on a non-ASCII path, run on the Windows CI job) and record the VT/color check as a `checkpoint:human-verify` item rather than force a brittle automated terminal-capture test — `libavformat`'s own UTF-8 handling (Pattern 4) is exercised only once real media decode exists (Phase 3+), so no FFmpeg-level automated test is in scope here.

### Sampling Rate
- **Per task commit:** `ctest --preset <triplet> -R unit --output-on-failure` (Catch2 unit tests only — fast)
- **Per wave merge:** `ctest --preset <triplet> --output-on-failure` (full suite incl. integration tests that spawn the real binary)
- **Phase gate:** full 3-OS CI matrix green (2 blocking + non-blocking per D-06) before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/CMakeLists.txt` — Catch2 3 wiring, `catch_discover_tests`
- [ ] `tests/unit/test_license.cpp` — BUILD-03 exact-match assertion
- [ ] `tests/unit/test_expected.cpp` — BUILD-07 alias smoke test
- [ ] `tests/integration/` harness — a helper that shells out to the built `mediadiff` binary and captures stdout, for CLI-05/BUILD-04/BUILD-08/BUILD-09/CLI-09 checks
- [ ] Framework install: `find_package(Catch2 3 REQUIRED)` resolves once `catch2` is in `vcpkg.json` dependencies (already planned, Standard Stack)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | No | mediadiff is a local CLI tool, no auth surface in this or any phase |
| V3 Session Management | No | no sessions |
| V4 Access Control | No | single-user local process |
| V5 Input Validation | Partial — yes | CLI11's built-in option validators (`CLI::ExistingFile`, etc., used from Phase 2 on); Phase 1's own surface is limited to argv (validated by CLI11) and file paths passed through `util/fs.h` |
| V6 Cryptography | No | XXH3-128 (used later, not Phase 1) is an integrity/identity hash, not a cryptographic security control — never hand-roll a "real" crypto primitive here, but ASVS V6 doesn't apply since no confidentiality/authenticity guarantee is claimed |
| V10 (Malicious/Supply-chain Code) | Yes | vcpkg manifest + `overrides` pinning + Package Legitimacy Audit (above) is the standard control — dependencies are pinned, sourced from the official vcpkg registry, not fetched ad hoc |
| V14 Configuration | Partial — yes | warnings-as-errors (BUILD-05), GPL runtime assertion (BUILD-03) are both configuration/build-hardening controls specific to this phase |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|-----------------------|
| Silent GPL relinkage via feature drift (a stale vcpkg overlay/cache resolves a different feature set than intended) | Tampering (of the build's license posture) | D-03's two-layer check: configure-time forbidden-artifact scan + mandatory runtime `avutil_license()` exact-match test, both in CI |
| Windows path/argv encoding confusion (mixed ANSI/UTF-8 leading to path traversal or wrong-file-opened bugs) | Tampering / Information Disclosure (wrong file silently opened) | UTF-16 argv conversion at the process entry point (Pattern 4) — do all internal string handling in UTF-8, never mix code pages |
| Supply-chain: a compromised or typosquatted vcpkg port | Tampering | vcpkg manifest pins exact versions via `builtin-baseline` + `overrides`; the official `microsoft/vcpkg` registry is used exclusively, no custom/third-party registries added in Phase 1 |
| CI binary-cache poisoning (a malicious actor pushes a tampered NuGet package to the GitHub Packages feed) | Tampering | `packages:write` scope should be limited to trusted workflows/branches; fork PRs get read-only `GITHUB_TOKEN` by default (GitHub Actions standard behavior) so external contributors cannot write to the cache feed — confirm this is not inadvertently loosened when wiring the PAT-based auth from Pitfall 3 |

## Sources

### Primary (HIGH confidence — fetched live this session from authoritative source)
- `github.com/FFmpeg/FFmpeg` `master` — `libavutil/version.c` (`avutil_license`, `avutil_configuration`), `libavutil/file_open.c` (`win32_open`), `libavutil/wchar_filename.h` (`get_extended_win32_path`), `libavformat/file.c` (`file_open` → `avpriv_open`)
- `github.com/microsoft/vcpkg` `master` — `ports/ffmpeg/portfile.cmake` (macOS unconditional framework enables), `ports/ffmpeg/FindFFMPEG.cmake.in` (system-lib pass-through list, `append_dependencies`), `ports/tl-expected/vcpkg.json`, `versions/f-/ffmpeg.json` (8.1 git-tree hashes)
- `api.github.com/repos/microsoft/vcpkg/commits/master` — live HEAD SHA for `builtin-baseline`
- `learn.microsoft.com/en-us/vcpkg/consume/binary-caching-github-packages` — full NuGet/GitHub Packages CI workflow, incl. the GITHUB_TOKEN-insufficient-for-write caveat
- `learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page` — `app.manifest` XML shape for unpackaged Win32 apps

### Secondary (MEDIUM confidence — WebSearch cross-checked against official source, or official doc not independently re-fetched in full)
- `catchorg/Catch2` `devel/docs/cmake-integration.md` — `find_package(Catch2 3 REQUIRED)` / `Catch2::Catch2WithMain` / `catch_discover_tests`
- `CLIUtils/CLI11` `examples/simple.cpp`, CLI11 book flags chapter — `set_version_flag` signature
- `github.com/microsoft/vcpkg-tool` issue #38236 — manifest-mode `status` file not reliably generated
- `github.com/actions/runner-images` release notes — `windows-2022`/VS2022 mapping, `macos-14` deprecation timeline
- FFmpeg `configure` script license-string logic — cross-checked against `ffmpeg.org/legal.html` and `LICENSE.md`, but the exact `configure` shell logic was reconstructed via WebFetch summarization rather than a byte-exact quote (the file was too large to fetch in full) — tagged `[CITED]`, not `[VERIFIED]`, for this one item

### Tertiary (LOW confidence — flagged for validation, `[ASSUMED]`)
- `SetConsoleMode`/`ENABLE_VIRTUAL_TERMINAL_PROCESSING` exact call shape (A1) — standard, stable Win32 pattern, not independently re-verified this session
- `av_version_info()`/`AV_VERSION_MAJOR` macro-decoding idiom (A2) — consistent with STACK.md's already-confirmed API-stability table

## Metadata

**Confidence breakdown:**
- vcpkg/CMake wiring (items 1–2, 9): HIGH — every claim traced to a live-fetched source file
- GPL assertion mechanics (item 3): HIGH for the license-string values and API shape; MEDIUM for the exact `configure` shell logic (summarized, not byte-quoted)
- Windows UTF-8 resolution (item 4): HIGH — this was the phase's single most consequential open question and is now fully traced through FFmpeg's own source
- CI/NuGet caching (item 5): HIGH — official MS tutorial fetched and quoted directly, including its own caveat about `GITHUB_TOKEN`
- CLI11/Catch2 wiring (items 6, 9): MEDIUM-HIGH — patterns confirmed against official examples/docs, not exhaustively fetched line-by-line
- ENG-16 lint, gen_corpus skeleton (items 7–8): MEDIUM — these are design choices consistent with D-07/D-08's stated intent, not externally verifiable facts

**Research date:** 2026-08-12
**Valid until:** 2026-09-11 (30 days) — vcpkg's `builtin-baseline` and FFmpeg's `8.1.x`/vcpkg port-version numbers will drift; re-verify the exact `overrides` entry and `builtin-baseline` SHA at execution time regardless of this document's age, since those are the two fields guaranteed to be stale by the time Phase 1 executes even within the 30-day window.
