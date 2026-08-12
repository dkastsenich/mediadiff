# Stack Research

**Domain:** Cross-platform C++20 CLI, FFmpeg-based media regression diffing, single static binary, 3-OS CI
**Researched:** 2026-08-12
**Confidence:** MEDIUM-HIGH (version numbers pulled directly from vcpkg's live port registry and Microsoft's own docs; FFmpeg API-history facts cross-checked against multiple independent sources; a few forward-looking judgment calls — e.g. which FFmpeg minor to pin — are opinion informed by the evidence, not a single citable source)

## Headline correction to PROJECT.md

**FFmpeg is not "7.x / 8.x" anymore — it's 9.0.** FFmpeg 9.0 "Lei" shipped **2026-08-04** (patch release 9.0.1 on 2026-08-12, i.e. today), four and a half months after 8.1 "Hoare" (2026-03-16), which followed 8.0 "Huffman" (2025-08). The vcpkg `ffmpeg` port's `vcpkg.json` is already pinned to **`"version": "9.0"`** on the `master` branch of `microsoft/vcpkg`. This is the single most important update the design docs need: doc 00 §4/§5 and the parent design doc's mention of "FFmpeg 7→9 migration" (UC2) should be read as validated foresight, not a hypothetical — a project starting today lands directly on the post-9.0 API surface. Everything below is written against that reality. See `<vcpkg ffmpeg port>` in Sources.

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| FFmpeg (libav*) | **9.0** via vcpkg port `"version": "9.0"` (track vcpkg's pin, do not hand-override) | demux, probe, parse, decode | Only serious cross-platform C/C++ library covering every container/codec surface this tool needs; vcpkg's port is fine-grained enough to build a decode-only LGPL subset; confirms PROJECT.md's "only serious option" call. **Correction:** target 9.0, not "7.x/8.x" — see above. |
| CMake | ≥ 3.25 (use 3.28+ locally; presets schema v6/v9 needs ≥3.23/3.25) | build system, presets | `CMakePresets.json` schema version 6 requires CMake ≥ 3.25; this is also the version where `CMAKE_COLOR_DIAGNOSTICS` and workflow presets stabilized. Confirms PROJECT.md floor exactly. |
| vcpkg | manifest mode, pinned as git submodule, `builtin-baseline` in `vcpkg.json` (or `vcpkg-configuration.json` with an explicit baseline) | dependency acquisition | One workflow across 3 OSes; the FFmpeg port's per-feature toggles are the only practical way to build an audited LGPL decode-only static FFmpeg on Windows without a bespoke build script. Confirms PROJECT.md. |
| C++20 (no modules, no `std::format`) | GCC ≥ 12, Clang ≥ 15, AppleClang (Xcode 15+, i.e. LLVM 15-ish libc++), MSVC v143 (VS2022 17.x) | language standard | Confirmed as the right parity call — see Toolchain Floors below for exactly which C++20 features are safe. |

### Supporting Libraries

All versions below are the **exact `version` field currently pinned in `microsoft/vcpkg` `master`'s port manifest** (fetched live, not from training data) — pin these (or newer, via a baseline bump) in `vcpkg.json`.

| Library | vcpkg version | Purpose | When to Use |
|---------|---------|---------|-------------|
| CLI11 | **2.6.2** (port-version unset) | CLI parsing | As specified. Latest upstream release (2026-02-26) adds optional C++20-modules support (irrelevant here, don't enable) and stays C++11-compatible, so no toolchain-floor conflict. Confirms PROJECT.md. |
| fmt | **12.2.0**, port-version 1 | text formatting, ANSI styling | Confirms PROJECT.md's "fmt instead of `std::format`" call. fmt remains usable from C++11 up — no forcing function toward C++20 that would break the toolchain-parity goal. |
| nlohmann-json | **3.12.0**, port-version 2 | JSON report + snapshot serialization, `ordered_json` | Confirms PROJECT.md. `ordered_json` (an `nlohmann::basic_json` alias with `nlohmann::ordered_map`) is stable API, present since 3.9+; no changes affect the intended usage. |
| tomlplusplus (toml++) | **3.4.0**, port-version 1 | `mediadiff.toml` config parsing | Confirms PROJECT.md. Header-only, TOML 1.0.0 compliant, actively maintained (3.4.0 is a 2024-era release still current as of this port pin). |
| xxHash | **0.8.3** | XXH3-128 hash chains, file identity | Confirms PROJECT.md. 0.8.3 is the version that finalized the XXH3/XXH128 wire format (stable since 0.8.0) — safe to hash-compare across machines/versions. |
| libebur128 | **1.2.6**, port-version 3 | R128 loudness / true peak | Confirms PROJECT.md. Upstream (`jiixyj/libebur128`) hasn't cut a new tag recently, but 1.2.6 itself is still receiving downstream patch/build-system fixes into 2026 (Gentoo, Fedora EPEL) — a "stable, not abandoned" reference implementation, exactly the profile you want for a correctness-critical loudness measurement. Not a red flag; BS.1770 doesn't need a library that ships releases often. |
| Catch2 | **3.15.3** | unit/integration tests + CTest | Confirms PROJECT.md. v3.x header story differs from v2 (now `<catch2/catch_test_macros.hpp>` etc., plus a separate `Catch2::Catch2WithMain` CMake target) — flag this for phase-0 scaffolding so the CMake wiring isn't written against v2-era docs found by an LLM's training data. |
| libvmaf | **3.2.0** | `--vmaf` opt-in quality metric | Confirms PROJECT.md's optional `vmaf` feature. The vcpkg port exposes **no CUDA feature** — confirms PROJECT.md's "CUDA-enabled libvmaf as a manual system build" decision was correct; there is nothing to toggle in the manifest for GPU VMAF. |
| dav1d | (pulled transitively as an `ffmpeg` feature, not a top-level manifest dep) | fast AV1 decode inside FFmpeg | Confirms PROJECT.md — `dav1d` is a valid, currently-listed feature name on the `ffmpeg` port. BSD-2-Clause licensed, so it doesn't disturb the LGPL decode-only story. |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| Ninja | build generator | Universal across the 3-OS matrix; use `"generator": "Ninja"` in every CMake preset. |
| `x-gha` — **do not use** | (was: GitHub Actions Cache binary-caching backend) | **Removed from vcpkg** (tracked in `microsoft/vcpkg-tool` PR #1662, ~April 2025). GitHub's own internal cache API changed underneath it and the vcpkg team was told this was never a supported integration path. Microsoft's own tutorial page for it now carries a live deprecation banner. See "Binary caching" below for the replacement. |
| lukka/run-vcpkg (GitHub Action) | vcpkg setup + binary cache orchestration in CI | Community action that owns `actions/cache` integration directly (not through vcpkg's own `x-gha` provider), still functional and the most common way projects get vcpkg caching working post-`x-gha`-removal. |
| clang-format (LLVM style, 100 cols) | style enforcement | As specified in doc 00 §9; no correction needed. |

## Installation

```bash
# vcpkg.json (manifest mode) — decode-only LGPL subset
{
  "name": "mediadiff",
  "version-string": "0.1.0",
  "builtin-baseline": "<pin to the vcpkg commit SHA you submodule>",
  "dependencies": [
    {
      "name": "ffmpeg",
      "default-features": false,
      "features": ["avcodec", "avformat", "swscale", "swresample", "dav1d", "zlib"]
    },
    "cli11", "fmt", "nlohmann-json", "tomlplusplus", "xxhash",
    "libebur128", "catch2"
  ],
  "features": {
    "vmaf": { "description": "libvmaf quality metric", "dependencies": ["libvmaf"] }
  }
}
```

```bash
# Submodule pin (do this once, then bump deliberately)
git submodule add https://github.com/microsoft/vcpkg.git vcpkg
git -C vcpkg checkout <commit-sha-matching-your-builtin-baseline>

# Configure (per CMakePresets.json — see Architecture note below)
cmake --preset linux-release   # or macos-release / windows-release
cmake --build --preset linux-release
```

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| vcpkg manifest mode | Conan 2 | If the team already standardizes on Conan elsewhere; Conan's C/C++ media-lib recipe coverage (esp. FFmpeg's fine-grained feature flags) is thinner and its Windows static-CRT story is less turnkey. PROJECT.md's rejection stands. |
| vcpkg's `ffmpeg` port | System `pkg-config` FFmpeg / apt/brew packages | Never for this project — irreproducible across 3 OSes, no static-binary story on Linux/Windows, version drift between CI and dev machines. Fine for `scripts/gen_corpus` if you'd rather not build the `ffmpeg` CLI tool feature yourself (see below). |
| NuGet-based vcpkg binary cache (GitHub Packages) | `actions/cache` raw (via lukka/run-vcpkg) | Use `actions/cache`-backed caching (lukka/run-vcpkg) if you don't want to stand up a NuGet feed and are fine with GitHub Actions cache's normal 10GB/repo eviction; use the NuGet/GitHub-Packages route if the FFmpeg build artifact is large enough or long-lived enough that you want it to survive cache eviction and be shareable across branches predictably. |
| Catch2 v3 | doctest | Only if compile-time overhead of Catch2's header becomes a measured problem; not worth the churn given PROJECT.md already specifies Catch2. |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| `x-gha` vcpkg binary-caching provider | Removed from vcpkg tooling (~April 2025); referencing it in `VCPKG_BINARY_SOURCES` now just emits a deprecation warning and silently caches nothing useful | NuGet-based caching via a GitHub Packages feed, or `lukka/run-vcpkg` which wraps `actions/cache` itself without going through vcpkg's removed provider |
| Legacy `uint64_t channel_layout` field / `AV_CH_LAYOUT_*` masks | `FF_API_OLD_CHANNEL_LAYOUT` is fully removed as of FFmpeg 7.0 (Feb 2024) — the field doesn't exist in the headers you'll be building against (9.0) | `AVChannelLayout` struct exclusively: `av_channel_layout_copy`, `av_channel_layout_describe`, `AVCodecParameters.ch_layout` / `AVCodecContext.ch_layout` |
| `avcodec_decode_video2` / `avcodec_decode_audio4` | Removed long before 7.x; will not compile against 9.0 headers at all | `avcodec_send_packet` + `avcodec_receive_frame` (stable since FFmpeg 3.1, 2016 — this is not new, it's just the only path left) |
| Reading HDR static metadata off `AVFrame` side data only | Works, but if you also want it from `AVCodecParameters` (stream-level, no decode required — relevant to your Parser pass in phase 3) that field (`coded_side_data`) only exists from libavcodec 60.30.100 (FFmpeg 6.1, Oct 2023) onward | Since you're building against 9.0 this is moot as a floor, but document `AVCodecParameters.coded_side_data` (not `AVStream.side_data`, which is the pre-6.1 deprecated location) as the read path for `AV_PKT_DATA_MASTERING_DISPLAY_METADATA` / `AV_PKT_DATA_CONTENT_LIGHT_LEVEL` / DOVI config record |
| Catch2 v2-style single header (`catch.hpp`) | The vcpkg port is Catch2 **3.15.3** — v2's amalgamated single header and v2 macro namespace don't exist in the v3 port | `#include <catch2/catch_test_macros.hpp>` etc., and link `Catch2::Catch2WithMain` |
| CUDA feature flag on the `libvmaf` vcpkg port | Doesn't exist — the port has no CUDA toggle to enable | PROJECT.md's own call: document CUDA-accelerated VMAF as a manual/system build, not a vcpkg feature |

## Stack Patterns by Variant

**If pinning FFmpeg for stability over bleeding-edge (recommended for phase 0):**
- Pin `builtin-baseline` to a vcpkg commit that resolves `ffmpeg` to **8.1 "Hoare"** (2026-03-16), not 9.0.
- Because 9.0 is **8 days old** as of this research date (released 2026-08-04, patched 2026-08-12) — every core library crossed a new major SOVERSION (`libavcodec`/`libavformat` → 63.x), which is exactly the kind of churn that produces fresh vcpkg-port breakage on less-common triplets (Windows static, cross-compiled arm64-linux) before the ecosystem catches up. 8.1 is a mature, bug-fixed release four months in. Bump to 9.0 deliberately once the vcpkg port has had a few weeks of community mileage — this is also literally UC2's scenario (an FFmpeg major-version migration exercised as a dir-diff), so treat the 8.1→9.0 bump as a good *test fixture* for the tool once it exists, not a day-1 requirement.
- If you'd rather ship on 9.0 from day one anyway (matching "latest" exactly), budget CI time for chasing any early port breakage — check `microsoft/vcpkg` issues filtered to `[ffmpeg]` before locking the baseline.

**If MSVC v143 build breakage appears (it has a track record on this port):**
- The `[ffmpeg]` tag on `microsoft/vcpkg/issues` has a long, continuing history of Windows-specific build failures (issues opened as recently as Jan/Feb 2026, e.g. `#49367`, `#50197`) — these are typically nasm/yasm toolchain detection, MSVC version-specific codegen bugs in specific codec features, or path-length issues, not fundamental incompatibility. Mitigate by: (1) building with only the minimal feature set your `vcpkg.json` needs (fewer C/asm dependencies = fewer places to break), (2) pinning `builtin-baseline` and bumping only after checking open issues, (3) keeping NASM up to date on the Windows runner image (`FFmpeg` moved fully off `yasm` onto `nasm` as of 8.0).

**If Xcode/AppleClang build breakage appears on macOS runners:**
- Known failure classes are cross-compiling `x64-osx` from an `arm64-osx` host (Apple Silicon CI runner cross-building the Intel artifact) and per-codec sub-dependencies (e.g. `x265`) breaking on newer Xcode point releases before the port's `portfile.cmake` catches up. Since PROJECT.md already commits to **no universal binary, per-arch builds only**, prefer running the `x64-osx` leg on an actual Intel runner (or accept occasional turbulence on cross-builds) rather than fighting `VCPKG_OSX_ARCHITECTURES` cross-compilation edge cases in the FFmpeg port.

**If corpus generation needs an `ffmpeg` CLI (per doc 00 §5.2, `scripts/gen_corpus`):**
- Either add a **separate dev-only manifest** (`vcpkg-configuration.json` overlay or a `vcpkg.json` in a `tools/` subfolder) enabling the port's own `ffmpeg`/`ffprobe` features (this pulls in a second, larger FFmpeg build — don't add these features to the main static-binary manifest), or require a system `ffmpeg` CLI ≥ 6.1 as PROJECT.md already specifies. The vcpkg-built `ffmpeg` tool feature is the more reproducible choice if CI determinism of the *corpus itself* ever becomes suspect; system `ffmpeg` is fine as a v1 shortcut since corpus generation isn't part of the distributed binary's build.

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| `ffmpeg` (vcpkg 9.0) | GCC ≥ 12 / Clang ≥ 15 / AppleClang (Xcode 15+) / MSVC v143 | No known compiler-floor conflict; FFmpeg's own C sources are C11, well below any of these. |
| `ffmpeg` features `avcodec,avformat,swscale,swresample,dav1d,zlib` (no `gpl`, no `nonfree`) | LGPL-only distribution requirement | Confirmed: none of these six features requires `gpl` or `nonfree`. `dav1d` is BSD-2-Clause. `x264`/`x265` are **not** in this feature list and must never be added without also enabling `gpl` (and reconsidering the license story) — worth a CI guard (`grep` the resolved `vcpkg_installed` manifest for forbidden feature names) since "feature flags audited so GPL code is never silently linked" is a hard constraint in PROJECT.md. |
| Catch2 3.15.3 | CMake `find_package(Catch2 3 REQUIRED)` + `include(Catch)` (`CTest` integration via `catch_discover_tests`) | v3-specific CMake helper module name differs from v2 (`ParseAndAddCatchTests.cmake` was the v2-era helper); use the v3 `Catch.cmake`/`catch_discover_tests()` shipped by the port. |
| `x64-windows-static-md` triplet | MSVC v143, `/MD` runtime | Confirmed via the actual triplet file: `VCPKG_CRT_LINKAGE=dynamic`, `VCPKG_LIBRARY_LINKAGE=static` — i.e. every vcpkg library is a static `.lib`, but they all still expect the dynamic MSVC CRT (`vcruntime140.dll`/`ucrtbase.dll`, present on all supported Windows). This is the standard "single static EXE, no static CRT" recipe and matches PROJECT.md exactly. |
| `x64-linux` / `arm64-linux` / `x64-osx` / `arm64-osx` triplets | static by default | Confirmed directly from `microsoft/vcpkg`'s triplet files: all four have `VCPKG_LIBRARY_LINKAGE=static` out of the box (no custom triplet file needed). Note `x64-osx.cmake` currently lives under `triplets/community/` while `arm64-osx.cmake`, `x64-linux.cmake`, `arm64-linux.cmake` are top-level/built-in — a classification difference only, both work identically via `--triplet`. |

## Question-by-question findings

### 1. vcpkg manifest mode, static triplets, binary caching, ffmpeg port health

- **`builtin-baseline` pinning:** still the current, correct mechanism — set it in `vcpkg.json` (or move it to `vcpkg-configuration.json`'s `default-registry.baseline` if you also add custom registries later). Pin the submodule commit to match. No change from PROJECT.md's plan.
- **Binary caching — correction:** PROJECT.md/doc 00 says "GitHub Actions cache or `x-gha`" as if interchangeable. **They are not anymore.** `x-gha` was removed from the vcpkg tool (tracked at `microsoft/vcpkg-tool#1662`); Microsoft's own tutorial for it is now a dead page with a deprecation banner. Use either (a) a NuGet feed hosted on GitHub Packages as the `VCPKG_BINARY_SOURCES` target, or (b) `lukka/run-vcpkg`, which manages `actions/cache` itself and does not depend on vcpkg's removed provider. **Update the CI plan (doc 00 §5.1, §9) accordingly before phase 0 CMake/CI work starts** — this is exactly the kind of stale assumption that would otherwise cost a debugging session in phase 0.
- **`ffmpeg` port feature audit:** confirmed live from the port's `vcpkg.json` — `avcodec`, `avformat`, `swscale`, `swresample`, `dav1d`, `zlib` are all present, current, spelled correctly, and none requires `gpl`/`nonfree`. The manifest in doc 00 §5.2 is **valid as written**, no corrections needed to the feature list itself.
- **MSVC v143 / Xcode breakage:** real, but not exotic — see "Stack Patterns by Variant" above. It's a "budget CI iteration time and pin deliberately" risk, not a blocker.

### 2. FFmpeg/libav API currency

Corrected/confirmed call-by-call:

| API | Status against FFmpeg 9.0 | Version it changed |
|---|---|---|
| `avformat_open_input`, `avformat_find_stream_info`, `av_read_frame` | Stable, unchanged, safe to use as-is | N/A — long-stable |
| `av_parser_parse2` + `AVCodecParserContext` | Stable, still the correct parser-pass API (doc 03's "ParserScan") | N/A — long-stable |
| `avcodec_send_packet` / `avcodec_receive_frame` | The *only* decode API — old `avcodec_decode_video2`/`_audio4` are gone entirely | send/receive introduced FFmpeg 3.1 (2016); old API removed well before 7.x |
| `AVChannelLayout` | **Mandatory** — no legacy mask fallback exists in 9.0 headers | New struct introduced ~5.1/6.0; legacy `FF_API_OLD_CHANNEL_LAYOUT` fully removed in **FFmpeg 7.0** (Feb 2024) |
| `codecpar->coded_side_data` (mastering display / CLL / DOVI config) | Available and correct — read HDR static metadata here for the Parser pass, not per-frame, when you want it without decoding | Added **FFmpeg 6.1** (libavcodec 60.30.100, Oct 2023) — irrelevant as a floor since you're targeting 9.0, but document this as the *minimum* if anyone ever back-ports |
| `AV_FRAME_DATA_A53_CC` | Stable, long-present frame-side-data enum value; unaffected by any of the above churn | N/A — long-stable |
| `av_image_get_linesize` | Stable | N/A — long-stable |
| `av_hwdevice_ctx_create` + `av_hwframe_transfer_data` | Stable hwaccel API surface (this is the same generation of API used since ~FFmpeg 3.4/4.0's unified hwaccel rework); no signature changes affecting this project's usage pattern | N/A — long-stable |
| `av_rescale_q` | Stable | N/A — long-stable |
| `AV_CODEC_FLAG_BITEXACT` | Stable, still the correct flag for bitexact fixture generation/decoding (doc's determinism-class machinery) | N/A — long-stable |

**Minimum version supporting the full call set as designed:** FFmpeg **6.1** is the true technical floor (for `coded_side_data`); FFmpeg **7.0** is the floor if you also want to guarantee the *legacy* channel-layout mask is gone from the headers (forcing everyone on the team onto `AVChannelLayout`, which you want anyway). Since the project is greenfield and vcpkg is already pinned to 9.0, **target 9.0 (or deliberately step back to 8.1 for maturity — see Stack Patterns above)**; there is no reason to design for anything older.

### 3. Supporting libraries

All confirmed current and vcpkg-healthy — see the Supporting Libraries table above for the specific version numbers and the "What NOT to Use" table for the two things that actually need attention going into phase 0: **Catch2 is v3, not v2** (different include paths/CMake integration — matters immediately for any phase-0 test scaffolding), and **libebur128 hasn't tagged a new release recently but is not unmaintained** (it's a "stable reference implementation," which is the correct profile for BS.1770 — no substitute needed).

No supporting library in the plan needs replacing. None is unmaintained-and-risky; none has a materially better alternative that would be worth the migration cost.

### 4. Toolchain floors — C++20 without modules/`std::format`

**Confirmed as the right call.** Safe C++20 features across GCC ≥12 / Clang ≥15 / AppleClang (Xcode 15+ libc++) / MSVC v143, all four:

- Concepts & constraints (`concept`, `requires`)
- Designated initializers
- Three-way comparison (`operator<=>`) for simple aggregate/value types
- `consteval` / `constinit`
- `[[likely]]`/`[[unlikely]]`, `[[no_unique_address]]` (MSVC's ABI-compatible support for the latter lagged historically — verify on the specific MSVC 17.x point release in CI, but v143-era MSVC has it)
- `std::span`, `<bit>` (`std::bit_cast`, `std::popcount`, etc.), `std::source_location`
- Range-`for` with init-statement, `using enum`
- Basic `<ranges>` views (`views::filter`, `views::transform`, `ranges::sort`) — the *core* ranges surface is fine; avoid leaning on more recently standardized ranges-adjacent additions (some C++23 `<ranges>` extensions backported inconsistently) if you want zero toolchain-specific `#ifdef`s

**Avoid for toolchain parity** (confirms the project's own exclusions and extends them):
- `std::format` / `std::print` — already excluded, correctly, in favor of fmt
- C++20 **modules** — already excluded, correctly; MSVC/Clang/GCC module support and, worse, module + CMake + vcpkg interop, are still not uniformly turnkey across this exact 4-toolchain matrix
- **Coroutines** — not currently in the design, and worth actively avoiding: `libc++` (AppleClang's standard library) only moved `std::jthread`-adjacent concurrency primitives out of experimental status relatively recently, and coroutine + exception-free (`-fno-exceptions`-adjacent) interop across all four toolchains is exactly the kind of thing that produces toolchain-specific bugs. Not needed for this project's synchronous pass-based architecture anyway.
- **`std::jthread` / `std::stop_token`** — skip; libc++'s support trailed MSVC STL (drafted 2019) and libstdc++ (2020) by years and its non-experimental status is recent enough that Xcode-bundled libc++ support should be verified per-Xcode-version rather than assumed. Since the project's threading model (§ "bounded parallelism" for `dir` mode) doesn't require cooperative cancellation semantics, a plain `std::thread` + explicit atomic-flag/queue is the safer cross-toolchain choice.

### 5. CMake ≥3.25 presets — idiomatic layout

Current idiomatic pattern (per Microsoft's own vcpkg+CMake integration docs, still accurate):

```jsonc
// CMakePresets.json (committed)
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    { "name": "linux",   "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-linux" } },
    { "name": "macos",   "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "arm64-osx" } },
    { "name": "windows", "inherits": "base", "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-windows-static-md" } }
  ],
  "buildPresets": [
    { "name": "linux",   "configurePreset": "linux" },
    { "name": "macos",   "configurePreset": "macos" },
    { "name": "windows", "configurePreset": "windows" }
  ]
}
```

Notes for phase-0 planning:
- **Vendor the toolchain file path relative to the submodule** (`${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake`) rather than relying on a `VCPKG_ROOT` environment variable — this is more reproducible for a project that pins vcpkg as a submodule specifically to avoid environment drift (matches PROJECT.md's stated rationale for submoduling vcpkg in the first place). Reserve `VCPKG_ROOT`/`CMakeUserPresets.json` overrides for local developer convenience only, not CI.
- Warnings-as-errors (`/W4 /WX` on MSVC, `-Wall -Wextra -Werror` on GCC/Clang) belongs in each triplet-specific preset's `cacheVariables` (e.g. a `CMAKE_CXX_FLAGS` addition) or in `CMakeLists.txt` gated on `if(MSVC)`/`else()`, not baked into the toolchain file.
- CMake preset schema **version 6** requires CMake ≥ 3.25 — matches the floor exactly; no need to go higher (e.g. schema 9/CMake 3.30) unless a later feature is needed.

### 6. Static linking realities per OS

- **Linux (glibc):** Static-linking FFmpeg's C libraries is straightforward (`x64-linux`/`arm64-linux` triplets are static by default, confirmed above); the CLI executable itself will still dynamically link glibc/libstdc++/libgcc_s unless you go out of your way to fully static-link the CRT (`-static`), which vcpkg's triplet does **not** do and PROJECT.md doesn't ask for. "Single static binary" here means statically linked *dependencies* (FFmpeg, fmt, etc.), not a fully static ELF — worth stating explicitly in the design doc's distribution claim so nobody is surprised the binary still needs a normal glibc at runtime (true "zero setup" for any reasonably current Linux, which is the actual goal).
- **macOS (VideoToolbox/CoreMedia frameworks):** Not enabled in the current feature list (no `videotoolbox` feature requested), so this isn't a build concern for v1 — but flag it for future hardware-decode work: enabling FFmpeg's VideoToolbox hwaccel pulls in Apple's system frameworks (`CoreMedia`, `CoreVideo`, `VideoToolbox`) which are always dynamically linked (Apple doesn't ship static versions) — this is normal and doesn't compromise the "static binary" goal since system frameworks are part of the OS, not a redistributable dependency.
- **Windows (`x64-windows-static-md`):** Confirmed the triplet gives static vcpkg libraries + dynamic (`/MD`) CRT. The two realistic failure modes: (1) a dependency's vcpkg port hard-codes `/MT` somewhere in its `portfile.cmake` and produces a linker mismatch (`LNK2038` CRT mismatch) — rare on the libraries in this manifest but worth a CI smoke-check; (2) FFmpeg's Windows build historically wants NASM on `PATH` for assembly-optimized codec paths — ensure the CI Windows runner image has NASM available (GitHub's `windows-latest` image does, but pin/verify rather than assume).

## Explicit confirm/correct ledger (every PROJECT.md Key Decision, dependency-related)

| PROJECT.md decision | Verdict | Detail |
|---|---|---|
| CLI11 for argument parsing | **CONFIRMED** | 2.6.2 current, vcpkg-healthy, no better alternative for this feature set |
| vcpkg manifest mode, pinned submodule, static triplets | **CONFIRMED, with one correction** | Triplet defaults verified exactly as claimed. **Correction:** "GitHub Actions cache or `x-gha`" — `x-gha` is removed; use NuGet/GitHub-Packages caching or `lukka/run-vcpkg` |
| FFmpeg as the only demux/decode dependency, `dav1d` for AV1 | **CONFIRMED** | Feature names valid; `dav1d` is a real, BSD-licensed ffmpeg-port feature |
| Decode-only LGPL subset (`avcodec`,`avformat`,`swscale`,`swresample`,`dav1d`,`zlib`, `default-features:false`) | **CONFIRMED, verbatim** | All six feature names exist on the current port; none pulls `gpl`/`nonfree` |
| Target "FFmpeg 7.x/8.x" (implied by doc's UC2 and general framing) | **CORRECTED** | Current upstream and current vcpkg port pin is **9.0** (released 8 days before this research). Recommend either targeting 9.0 deliberately or deliberately stepping back to 8.1 for maturity — see Stack Patterns above — but "7.x" is stale framing regardless |
| CMake ≥3.25 + presets | **CONFIRMED** | Matches CMakePresets schema-version-6 floor exactly |
| C++20, no modules, no `std::format`, fmt instead | **CONFIRMED** | Right call; see Toolchain Floors for the exact safe-feature subset, plus two additional risky features to avoid (coroutines, `jthread`) that the doc doesn't mention but should be aware of |
| `x64-windows-static-md` triplet | **CONFIRMED** | Verified static-libs/dynamic-CRT split directly from the triplet file |
| fmt, nlohmann-json (`ordered_json`), toml++, xxHash (XXH3-128), libebur128, Catch2, libvmaf | **ALL CONFIRMED** | Current versions listed in Supporting Libraries table; Catch2 v3 API surface flagged as a phase-0 scaffolding detail, not a decision reversal |
| CUDA-enabled libvmaf as manual build, not vcpkg feature | **CONFIRMED** | vcpkg's `libvmaf` port has no CUDA toggle — there was never a vcpkg-native alternative to reject |
| ffmpeg CLI ≥ 6.1 for `scripts/gen_corpus` | **CONFIRMED, unchanged** | No API/behavior reason to raise this floor; 6.1+ still fine for bitexact fixture generation |

## Sources

- `microsoft/vcpkg` `master` branch, live port manifests (`ports/ffmpeg/vcpkg.json`, `ports/cli11/vcpkg.json`, `ports/fmt/vcpkg.json`, `ports/nlohmann-json/vcpkg.json`, `ports/tomlplusplus/vcpkg.json`, `ports/xxhash/vcpkg.json`, `ports/libebur128/vcpkg.json`, `ports/catch2/vcpkg.json`, `ports/libvmaf/vcpkg.json`) — fetched directly, MEDIUM confidence (direct-source webfetch, not model training data)
- `microsoft/vcpkg` `master` branch, live triplet files (`triplets/x64-linux.cmake`, `triplets/arm64-linux.cmake`, `triplets/arm64-osx.cmake`, `triplets/community/x64-osx.cmake`, `triplets/x64-windows-static-md.cmake`) plus the GitHub Contents API directory listings for `triplets/` and `triplets/community/` — fetched directly, MEDIUM confidence
- Microsoft Learn: `vcpkg/consume/binary-caching-github-actions-cache` (carries a live deprecation notice pointing at `microsoft/vcpkg-tool#1662`) and `vcpkg/users/triplets` — MEDIUM confidence
- `microsoft/vcpkg-tool` PR #1662 ("Remove `x-gha` binary cache provider") and `lukka/run-vcpkg` issue #251 — cross-confirmed via WebSearch, MEDIUM confidence
- FFmpeg release coverage (Phoronix, 9to5Linux, UbuntuHandbook, Jean-Baptiste Kempf's blog, LWN) for 8.0/8.1/9.0 release dates and codenames — cross-confirmed across multiple independent outlets, MEDIUM confidence
- FFmpeg `doc/APIchanges` (via Fossies mirror and GitHub) and FFmpeg-devel mailing list/Patchwork threads for `AVChannelLayout` and `coded_side_data` version gating — MEDIUM confidence
- `CLIUtils/CLI11` GitHub Releases page — MEDIUM confidence
- `jiixyj/libebur128` GitHub repo, releases, issues, plus downstream Fedora/Gentoo packaging changelogs (as evidence of continued patch-level activity despite no new upstream tag) — MEDIUM confidence
- General C++20 compiler-support knowledge (cppreference compiler-support tables, libc++/`jthread` status writeups) — used for the Toolchain Floors judgment calls; treat these specific claims as MEDIUM confidence and re-verify the exact MSVC 17.x / Xcode 15.x point releases pinned in CI once phase-0 CI images are chosen

---
*Stack research for: cross-platform C++20 media-analysis CLI (FFmpeg-based)*
*Researched: 2026-08-12*
