# Phase 1: Foundation & Toolchain - Context

**Gathered:** 2026-08-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 1 delivers the build system, dependency acquisition, CI matrix and argv plumbing that everything else stands on: a single static `mediadiff` binary that builds reproducibly from a clean checkout on Linux, macOS and Windows, prints `--version`, and does nothing else.

**In scope:** CMake ≥ 3.25 presets, the vcpkg manifest and submodule pin, the decode-only LGPL FFmpeg configuration, static triplets, the 3-OS CI matrix with binary caching, the `libmediadiff`/`mediadiff` target split, Windows UTF-8 argv and path handling, `scripts/gen_corpus` skeleton, and `--version` output (tool version + linked FFmpeg library versions + enabled features).

**Out of scope:** Every other CLI flag and subcommand (CLI-01/02/03/04/06/07/08/10 are Phase 2), the check registry, any comparison semantics, any media parsing. Phase 1 ships a binary that compiles and identifies itself — nothing more. If a task in this phase requires opening a media file, it has escaped the boundary.

**Requirements:** BUILD-01 … BUILD-10, CLI-05, CLI-09 (12 total)

</domain>

<decisions>
## Implementation Decisions

All eight decisions below were auto-resolved under `--auto` using the recommended option. Each records its reasoning so a later phase can overturn it on evidence rather than re-litigating from scratch.

### Dependency Baseline

- **D-01: Pin the FFmpeg baseline to 8.1 "Hoare" for Phase 1; bump to 9.x in a later phase as a deliberate, recorded migration.** — **Reversibility:** costly — a bump moves the vcpkg `builtin-baseline`, invalidates every cached binary across the 3-OS matrix, and changes any decode-path signature recorded in committed test snapshots.

  Research (STACK) found FFmpeg 9.0 shipped 2026-08-04 with 9.0.1 following on 2026-08-12, and the vcpkg port already points at 9.0. Two reasons to hold at 8.1 anyway:

  1. **Phase 1's only job is a green matrix.** Doc 00 §5.1 names "occasional port breakage on bleeding-edge MSVC" as a known vcpkg risk mitigated by pinning. Building the foundation on an 8-day-old major release maximizes the chance that a phase-1 red build is an upstream port problem rather than a mediadiff problem — the single worst time to have ambiguous failures.
  2. **UC2 is literally "FFmpeg 8→9 migration."** If mediadiff is built on 9.0 from the start, that dogfooding opportunity is spent. Pinning 8.1 and bumping later turns our own toolchain bump into a live exercise of TRUST-08 (cross-release idempotence) and TRUST-04 (path-signature guards on perceptual checks) — far stronger validation than starting at 9.0.

  **Known cost:** FFmpeg 9.0 rewrote swscale from float to exact-rational math (PITFALLS). Phase 7's SSIM baselines will shift across the eventual bump. That is the intended behavior — TRUST-04 exists so the bump produces `skipped:` rather than a false fail, and this is exactly the case that proves it.

  **Record the pin as an explicit decision in PROJECT.md Key Decisions with this rationale.** BUILD-10 is satisfied by the recording, not merely by the pin existing.

- **D-02: Use `tl-expected` from vcpkg, aliased behind `mediadiff::expected<T,E>` in a single `src/util/expected.h`.** — **Reversibility:** reversible — the alias is the whole point; swapping to `std::expected` on a future C++23 bump touches one header.

  `std::expected` is C++23; the project targets C++20 (ARCHITECTURE). Hand-rolling means writing and testing the monadic operations (`and_then`, `transform`, `or_else`) that `core/` will lean on constantly — pure cost. `tl-expected` is header-only, tracks the standard proposal closely, and is one manifest line. `expected-lite` is an acceptable substitute if the port is unhealthy at pin time.

  The alias header is the load-bearing part, not the library choice. No file outside `util/expected.h` may name `tl::expected` directly.

### Build Correctness

- **D-03: Assert the LGPL configuration at runtime from the linked library's own self-report, not from the manifest.** — **Reversibility:** reversible.

  BUILD-03 requires "a build-time assertion that no GPL component is linked." A manifest or CMake-time check verifies what was *requested*; a transitive feature, a stale binary-cache entry, or a local vcpkg overlay can make the resolved build differ. The check that tests what actually shipped is `avutil_license()` / `avcodec_license()`.

  Implement as a compiled test run in CI on every platform, and surface the string in `--version` so a user's bug report carries the proof.

  **Gotcha to encode in the test:** `avutil_license()` returns `"LGPL version 2.1 or later"` for LGPL builds and `"GPL version 2 or later"` for GPL builds. A substring test for `"GPL"` matches **both**. The assertion must positively match the LGPL form, never merely assert the absence of "GPL".

  A cheap CMake-time check of the resolved vcpkg feature list is worth adding on top for a faster failure signal, but it does not replace the runtime assertion.

- **D-04: Windows text handling uses the wide-API path from doc 00 §3.2, plus a UTF-8 active-code-page manifest as belt-and-braces.** — **Reversibility:** costly — retrofitting path handling after phases 2–7 exist touches every file-opening site, which is precisely why research (PITFALLS) insists this lands in the foundation phase.

  `GetCommandLineW` / `CommandLineToArgvW` → convert to UTF-8 once → feed CLI11; all internal strings UTF-8; mediadiff's own file I/O through the `util/fs.h` shim; `SetConsoleMode` for VT sequences. The UTF-8 manifest (Windows 10 1903+) costs one file and covers any narrow-API path the shim misses.

  **Open verification item for planning, not a settled claim:** libavformat appears to convert UTF-8 paths to UTF-16 internally on Windows (it carries a `win32_utf8_open`-style wrapper for exactly this). If that holds, `avformat_open_input` accepts our UTF-8 paths directly and `util/fs.h` only needs to cover mediadiff's own I/O. **Confirm this empirically in Phase 1 against the pinned FFmpeg build with a genuinely non-ASCII filename** — if it does *not* hold, a custom `AVIOContext` over a wide-opened handle becomes phase-1 scope rather than a phase-3 surprise. Do not assume either way from documentation alone.

### CI & Distribution

- **D-05: Cache vcpkg binaries in a NuGet feed on GitHub Packages.** — **Reversibility:** reversible.

  Research (STACK) confirms the `x-gha` backend named in doc 00 §5.1 was removed (~April 2025, `microsoft/vcpkg-tool#1662`) — that reference is stale and must not be followed.

  Prefer NuGet/GitHub Packages over `lukka/run-vcpkg` or plain `actions/cache`: both of the latter draw on the Actions cache quota (10 GB per repo, LRU eviction across all caches). Three static FFmpeg builds will pressure that hard, and the failure mode is a silent cache miss that quietly reintroduces the 15–40 minute build. GitHub Packages storage is a separate quota.

- **D-06: Three host triplets gate Phase 1; `arm64-linux` and `x64-osx` build as non-blocking jobs.** — **Reversibility:** reversible.

  Blocking: `x64-linux`, `arm64-osx`, `x64-windows-static-md`. Doc 00 §4 mentions aarch64 Linux and per-arch macOS but does not make them foundation gates. Adding two more full FFmpeg builds to the blocking matrix doubles cache pressure and wall-clock for the phase whose entire job is "green," while non-blocking jobs still prove buildability.

  Release-artifact architecture coverage is a distribution question, separate from the phase-1 CI gate; BUILD-04 is satisfied per-platform by the three blocking triplets.

### Scaffolding

- **D-07: Create both CMake targets — `libmediadiff` and `mediadiff` — with the full doc 00 §7 directory tree, and enforce the library boundary in CI from day one.** — **Reversibility:** costly — retrofitting the lib/cli split after Phase 2's 48 requirements land means relocating code between targets and untangling stdout calls from engine logic.

  ENG-16 (the library writes nothing to stdout and never calls `exit()`) is an architectural invariant that is free to enforce against near-empty targets and painful to impose later. Create the directories with `.gitkeep` where empty.

  Add a CI lint — roughly five lines — that greps `libmediadiff` sources for `printf|std::cout|std::cerr|exit(` and fails the build. That makes ENG-16 structural rather than aspirational, which is the entire reason for splitting the targets in phase 1 instead of phase 2.

  **Do not write placeholder headers with invented APIs.** Empty directories are honest; fake interfaces rot and mislead Phase 2's planner.

- **D-08: `scripts/gen_corpus` uses a system `ffmpeg` ≥ 6.1, and records the exact ffmpeg version and build configuration into a manifest beside the generated corpus.** — **Reversibility:** reversible.

  Requiring a vcpkg-built ffmpeg CLI would add a second large FFmpeg build to every developer machine for no phase-1 benefit — BUILD-08 only requires the script to exist and be deterministic.

  The version manifest is not bookkeeping. `-flags +bitexact` makes an encoder deterministic *for a given encoder version*, not across versions, so recording the generator's identity is what makes the determinism claim checkable at all — and it is the prerequisite for ever pinning the generator without a churn of unexplained fixture changes.

### Claude's Discretion

Under `--auto` every gray area was resolved with the recommended option, so nothing was explicitly delegated by the user. The planner retains normal latitude on:

- CMake preset naming and directory layout within the doc 00 §7 tree
- Which specific GitHub Actions runner images to target (verify current MSVC 17.x / Xcode 15.x point releases at planning time per STACK's open question)
- How `--version` formats its output, provided it contains tool version, FFmpeg library versions, the license string (D-03), and the enabled-feature list
- Test framework wiring details, noting STACK's flag that Catch2 v3 requires different CMake integration than v2

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source of truth
- `claude_docs/00-design-and-requirements.md` — this phase's source doc (ROADMAP Phase 1 = design-doc phase 0). §4 build requirements, §5 dependency evaluation and the proposed `vcpkg.json`, §7 repo layout, §9 conventions. **§5.1's mention of the `x-gha` cache backend is stale — see D-05.**
- `claude_docs/01-core-concepts.md` §13 — Phase-2 acceptance, which defines what "the engine plugs into finished scaffolding" must mean. Read for the boundary, not for scope.

### Research (validates and corrects the design docs)
- `.planning/research/STACK.md` — verified dependency versions against the live vcpkg registry, FFmpeg API-currency table, CMake preset template, static-linking notes per OS, and an explicit confirm/correct ledger against every PROJECT.md dependency decision. **The highest-value document for this phase.**
- `.planning/research/PITFALLS.md` — build and distribution pitfalls (§7 of its brief): vcpkg cache invalidation, GPL relinkage via feature drift, Windows UTF-8 path handling.
- `.planning/research/ARCHITECTURE.md` §4 — lib/cli split precedent (libclang, libgit2) and why `expected<T,E>`-in-return-value beats libgit2's thread-local last-error given the bounded worker pool in `dir` mode.
- `.planning/research/SUMMARY.md` — cross-cutting synthesis and the user-decision list.

### Project-level
- `.planning/PROJECT.md` — constraints and the Key Decisions table. D-01 and D-02 must be appended to that table during this phase.
- `.planning/REQUIREMENTS.md` — BUILD-01…10, CLI-05, CLI-09 verbatim.
- `.planning/ROADMAP.md` — Phase 1 success criteria (5), and the numbering-offset note.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

None — greenfield. `src/` does not exist. The repository currently contains only `claude_docs/` and `.planning/`.

### Established Patterns

No code patterns yet. The patterns this phase *establishes* become binding on all later phases:

- **Target boundary:** `libmediadiff` (no stdout, no `exit()`) vs `mediadiff` (rendering and process control). Enforced by CI lint per D-07.
- **Error handling:** `mediadiff::expected<T, Error>` aliased in `src/util/expected.h` per D-02; no exceptions cross the library boundary.
- **String encoding:** UTF-8 internally everywhere; wide APIs confined to the Windows edge in `src/util/fs.h` per D-04.

### Integration Points

- `src/cli/main.cpp` is the only entry point and the only place process exit codes are produced.
- `src/util/` is the sole home for the fs shim, the expected alias, and TTY capability detection — all three are consumed by every later phase.

</code_context>

<specifics>
## Specific Ideas

- **`--version` should be usable as a bug-report artifact.** It carries tool version, linked FFmpeg library versions, the LGPL license string from D-03, and the enabled-feature list (`vmaf`, `cuda`) — enough that a maintainer reading a pasted `--version` block can reproduce the exact binary.
- **`git branch -m master main`** — the repository is currently on `master` while doc 00 §9 specifies trunk-based development on `main`. Worth doing before phase-1 commits accumulate.
- **`claude_docs/` is untracked.** The roadmap cites all seven files by path as each phase's source of truth; they should be committed so those references resolve for anyone cloning the repo.

</specifics>

<deferred>
## Deferred Ideas

- **Bumping FFmpeg to 9.x** — deliberately deferred out of Phase 1 per D-01. Belongs in a later phase where it can serve as a live TRUST-08 / TRUST-04 exercise rather than a foundation risk.
- **`arm64-linux` and `x64-osx` as blocking CI gates** — non-blocking in Phase 1 per D-06; promote when release artifacts for those architectures are actually published.
- **Pinning the corpus generator's ffmpeg** — deferred per D-08; the version manifest is the prerequisite that makes this a clean change later rather than a churn of unexplained fixture diffs.
- **A custom `AVIOContext` over wide-opened Windows handles** — only becomes scope if D-04's verification item shows libavformat does *not* convert UTF-8 paths internally. Do not build it speculatively.
- **Universal macOS binary** — already recorded as v2 (DIST-01), not a Phase 1 concern.

</deferred>

---

*Phase: 1-Foundation & Toolchain*
*Context gathered: 2026-08-12*
