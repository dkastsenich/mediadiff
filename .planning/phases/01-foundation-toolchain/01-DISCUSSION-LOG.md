# Phase 1: Foundation & Toolchain - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-12
**Phase:** 1-Foundation & Toolchain
**Areas discussed:** FFmpeg baseline pin, `expected<T,E>` source, GPL-linkage assertion, Windows UTF-8 strategy, vcpkg cache backend, CI matrix breadth, scaffolding depth, corpus generator source
**Mode:** `--auto` — every area auto-resolved with the recommended option, no user prompts issued

---

## FFmpeg Baseline Pin

| Option | Description | Selected |
|--------|-------------|----------|
| Pin 8.1 "Hoare", bump to 9.x later as a recorded migration | Four months of maturity for the foundation phase; the later bump becomes a live TRUST-08 / TRUST-04 exercise and dogfoods UC2 | ✓ |
| Pin 9.0 "Lei" now | Latest; matches where the vcpkg port already points; avoids migration debt | |
| Leave the pin implicit in the vcpkg baseline | Least work now | |

**Choice:** Pin 8.1 (recommended default).
**Notes:** Third option rejected outright — BUILD-10 requires the baseline be an *explicitly recorded* decision, so leaving it implicit fails the requirement regardless of which version lands. Known cost accepted: FFmpeg 9.0's swscale float→exact-rational rewrite will shift Phase 7 SSIM baselines across the eventual bump; TRUST-04 exists so that surfaces as `skipped:` rather than a false fail, and this is the case that proves it.

---

## `expected<T,E>` Source

| Option | Description | Selected |
|--------|-------------|----------|
| `tl-expected` via vcpkg, aliased in `src/util/expected.h` | Header-only, tracks the std proposal, one manifest line, swap to `std::expected` touches one header | ✓ |
| `expected-lite` (Martin Moene) | Also header-only and vcpkg-available | |
| Hand-rolled minimal type | No new dependency | |

**Choice:** `tl-expected` behind an alias (recommended default).
**Notes:** Hand-rolling rejected because `core/` leans on the monadic operations (`and_then`, `transform`, `or_else`) constantly — writing and testing those is pure cost. `expected-lite` retained as an acceptable substitute if the port is unhealthy at pin time. The alias header is the load-bearing decision, not the library choice.

---

## GPL-Linkage Assertion Mechanism

| Option | Description | Selected |
|--------|-------------|----------|
| Runtime assertion on `avutil_license()` / `avcodec_license()`, run in CI, string surfaced in `--version` | Tests what actually linked, not what was requested | ✓ |
| CMake-time check of the resolved vcpkg feature list | Faster failure signal | |
| CI grep of `vcpkg.json` | Cheapest | |

**Choice:** Runtime assertion (recommended default), with the CMake-time check added on top as a fast-fail.
**Notes:** Manifest and CMake checks verify the *request*; a transitive feature, stale binary-cache entry, or local vcpkg overlay can make the resolved build differ. Recorded gotcha: `avutil_license()` returns `"LGPL version 2.1 or later"` vs `"GPL version 2 or later"` — a substring test for `"GPL"` matches both, so the assertion must positively match the LGPL form rather than assert absence of "GPL".

---

## Windows UTF-8 Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Wide-API path per doc 00 §3.2, plus a UTF-8 active-code-page manifest | `CommandLineToArgvW` + `util/fs.h` shim + `SetConsoleMode`; manifest covers any narrow-API path missed | ✓ |
| Wide-API path only | Exactly what doc 00 §3.2 specifies | |
| UTF-8 manifest only | One file, no shim | |

**Choice:** Both (recommended default).
**Notes:** Manifest-only rejected — requires Windows 10 1903+ and does not reliably cover the command line. An open verification item was recorded rather than an assumption: libavformat *appears* to convert UTF-8 paths to UTF-16 internally on Windows, which would mean `avformat_open_input` accepts our UTF-8 paths directly. This must be confirmed empirically in Phase 1 against the pinned build with a genuinely non-ASCII filename — if it does not hold, a custom `AVIOContext` over a wide-opened handle becomes Phase 1 scope rather than a Phase 3 surprise.

---

## vcpkg Binary Cache Backend

| Option | Description | Selected |
|--------|-------------|----------|
| NuGet feed on GitHub Packages | Storage quota separate from the Actions cache | ✓ |
| `lukka/run-vcpkg` | Convenient wrapper around `actions/cache` | |
| Plain `actions/cache` over the vcpkg archives directory | Simplest | |
| `x-gha` backend | Named in doc 00 §5.1 | ✗ unavailable |

**Choice:** NuGet on GitHub Packages (recommended default).
**Notes:** `x-gha` was removed from vcpkg-tool (~April 2025, `microsoft/vcpkg-tool#1662`) — doc 00 §5.1's reference is stale and must not be followed. The other two options draw on the 10 GB per-repo Actions cache quota with LRU eviction; three static FFmpeg builds pressure that hard, and the failure mode is a silent cache miss that quietly reintroduces the 15–40 minute build.

---

## CI Matrix Breadth

| Option | Description | Selected |
|--------|-------------|----------|
| Three host triplets block; `arm64-linux` and `x64-osx` non-blocking | `x64-linux`, `arm64-osx`, `x64-windows-static-md` gate the phase | ✓ |
| All five triplets block | Maximum coverage from day one | |

**Choice:** Three blocking, two advisory (recommended default).
**Notes:** Doc 00 §4 mentions aarch64 Linux and per-arch macOS but does not make them foundation gates. Adding two more full FFmpeg builds to the blocking matrix doubles cache pressure and wall-clock for a phase whose entire job is "green," while non-blocking jobs still prove buildability. Release-artifact architecture coverage treated as a distribution question separate from the CI gate.

---

## Scaffolding Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Both CMake targets + full doc 00 §7 tree + CI lint enforcing the library boundary | `libmediadiff` / `mediadiff` split structural from day one | ✓ |
| Full tree with placeholder headers | Shows intended shape | |
| Minimal — only what `--version` needs | Least scaffolding | |

**Choice:** Targets + tree + lint (recommended default).
**Notes:** ENG-16 (library writes nothing to stdout, never calls `exit()`) is free to enforce against near-empty targets and painful to retrofit once Phase 2's 48 requirements land. A ~5-line CI grep for `printf|std::cout|std::cerr|exit(` in `libmediadiff` sources makes the invariant structural rather than aspirational — the entire reason for splitting targets in Phase 1 instead of Phase 2. Placeholder headers explicitly rejected: empty directories with `.gitkeep` are honest, invented APIs rot and mislead Phase 2's planner.

---

## Corpus Generator ffmpeg Source

| Option | Description | Selected |
|--------|-------------|----------|
| System `ffmpeg` ≥ 6.1, with generator version recorded into a corpus manifest | Doc 00 §5.2's default, plus provenance | ✓ |
| vcpkg-built ffmpeg CLI via a dev-manifest feature | More reproducible | |

**Choice:** System ffmpeg with a version manifest (recommended default).
**Notes:** A vcpkg-built ffmpeg tool would add a second large FFmpeg build to every developer machine for no Phase 1 benefit — BUILD-08 only requires the script to exist and be deterministic. The version manifest is not bookkeeping: `-flags +bitexact` makes an encoder deterministic *for a given encoder version*, not across versions, so recording the generator's identity is what makes the determinism claim checkable, and it is the prerequisite for pinning the generator later without a churn of unexplained fixture diffs.

---

## Claude's Discretion

Under `--auto` no area was explicitly delegated by the user. The planner retains normal latitude on CMake preset naming and layout within the doc 00 §7 tree, specific GitHub Actions runner images (verify current MSVC 17.x / Xcode 15.x point releases at planning time per STACK's open question), `--version` output formatting given the required content, and Catch2 v3 CMake wiring details.

## Deferred Ideas

- Bumping FFmpeg to 9.x — deliberately out of Phase 1 per D-01; belongs where it can serve as a live TRUST-08 / TRUST-04 exercise
- Promoting `arm64-linux` and `x64-osx` to blocking gates — when release artifacts for those architectures are published
- Pinning the corpus generator's ffmpeg — the version manifest is the prerequisite that makes this clean later
- A custom `AVIOContext` over wide-opened Windows handles — only if D-04's verification item shows libavformat does not convert UTF-8 internally; do not build speculatively
- Universal macOS binary — already recorded as v2 (DIST-01)
