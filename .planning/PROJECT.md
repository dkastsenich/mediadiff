# mediadiff

## What This Is

mediadiff is a cross-platform CLI that compares two media artifacts — or an artifact against a stored fingerprint — and reports every difference that could matter to someone who ships media software, classified by severity under a tolerance policy. It is the media-aware diff gate that plugs into any CI as a single static binary, with zero setup.

Its audience is teams building encoders, transcoders, packagers, players, cameras, conferencing and AI-video pipelines — all of whom share one failure mode: a commit passes unit tests while the media output silently changes (color range flips, A/V drifts 0.8 ms/min, an HDR tag vanishes, files grow 11%). Ordinary CI is media-blind.

The contract: **"Tell me what changed about this output, whether it was probably intentional, and whether it should block the merge — in one command, with zero setup."**

## Core Value

**A media-aware diff CI can trust:** a no-change re-run under the right profile is clean out of the box, every real regression is caught, explained, and actionable. False positives are P0 bugs — a diff tool that cries wolf gets muted, and a muted gate is worth nothing.

## Requirements

### Validated

(None yet — ship to validate)

### Active

**Foundation (phase 0)**
- [ ] Repo, CMake + vcpkg manifest, 3-OS CI matrix green, CLI skeleton with `--version`, empty check registry

**Core engine (phase 1)**
- [ ] Check registry as single source of truth (`checks.def`), build fails on any undocumented check ID
- [ ] Fingerprint I/O — canonical `*.snap.json` that diffs cleanly in git; `snapshot f && compare f f.snap.json` is clean by construction
- [ ] Seven comparison semantics: `exact`, `±tol`, `set`, `presence`, `hash`, `dist`, `span`
- [ ] Five shipped profiles: `strict-bitexact`, `sw-encoder`, `hw-encoder`, `remux`, `transform`
- [ ] Config precedence merge (profile → TOML → path overrides → CLI `--set`/`--tol`), inspectable via `list-checks --effective`
- [ ] Four report formats: TTY, JSON (schema-validated), Markdown, JUnit
- [ ] `dir` mode orchestration for corpus diffs, deterministic file order, bounded parallelism
- [ ] Exit-code contract: `0/1/2` regression signals vs `64/65/66/70` could-not-run signals
- [ ] `--explain` for every check, sourced from `docs/checks/<id>.md` compiled into the binary

**Container & topology (phase 2)**
- [ ] Probe layer: `DemuxSession` (header pass) + `PacketScan` (scan pass, no decode)
- [ ] Raw scanners `bmff_scan` / `ebml_scan` / `ts_scan` — read-only mechanism observation libav abstracts away
- [ ] All `container.*` and `meta.*` checks, per-container namespaces auto-`skipped` off-format

**Video (phase 3)**
- [ ] Parser pass (`ParserScan`) — per-access-unit properties without full decode
- [ ] All `video.*` stream-parameter, GOP, colorimetry and HDR checks

**Timeline (phase 4)**
- [ ] All `timeline.*` checks on pure integer/rational math
- [ ] The A/V drift algorithm — rate in ms/min, end delta, pattern class (`constant-offset`/`linear-drift`/`step`)

**Audio (phase 5)**
- [ ] Audio decode path with determinism-class-aware decoder selection
- [ ] All `audio.*` parameter, loudness, true-peak and silence checks, plus `content.audio.sample_hash`

**Content & size (phase 6)**
- [ ] `DecodeSession` video path; `content.video.*` frame hashing, perceptual, frozen/black runs
- [ ] Opt-in `quality.*` (PSNR, SSIM, VMAF behind a build flag)
- [ ] `size.*` rate-economics checks

**Cross-cutting guarantees**
- [ ] Idempotence guarantee: identical inputs produce byte-identical reports; enforced as a CI release blocker
- [ ] Three-passes-strictly-layered: no analyzer ever re-reads the file
- [ ] `libmediadiff` has no stdout and no `exit()` — the CLI is a renderer over an embeddable engine
- [ ] Performance targets met on the 10-minute 1080p reference file (release blockers)

### Out of Scope

- **GPL FFmpeg features** — the LGPL decode-only subset is a distribution requirement; enabling `gpl` would change the license story of a shipped binary
- **Encoding or muxing anything** — mediadiff observes, never mutates; raw scanners are read-only by design
- **TSDuck as a linked dependency** — very large surface for ~300 lines of need; retained as the *reference implementation* tests cross-check against, not a dep
- **Universal macOS binary in v1** — per-arch builds only (`arm64`, `x86_64`)
- **C++20 modules and `std::format`** — toolchain parity across GCC 12 / AppleClang / MSVC v143 is worth more; fmt used instead
- **Per-frame Dolby Vision RPU diffing** — v1 covers the configuration record only
- **PCR accuracy/jitter vs an ideal clock** — deferred until it can meet the idempotence guarantee without false alarms
- **Committed media binaries** — every fixture is synthesized deterministically by `scripts/gen_corpus`; nothing binary enters git
- **Conan 2 / system packages / FetchContent for FFmpeg** — evaluated and rejected (reproducibility, Windows story, static-binary requirement)
- **CUDA-enabled libvmaf as a vcpkg feature** — documented as a manual system build instead

## Context

**Source of truth.** This project is specified in full before any code exists. `claude_docs/` holds seven cross-referenced design documents, each mapping to exactly one phase:

| Doc | Phase | Scope |
|---|---|---|
| `00-design-and-requirements.md` | 0 | Problem, CLI surface, build/deps, architecture, repo layout, conventions |
| `01-core-concepts.md` | 1 | Object model, registry, semantics, profiles, config, snapshots, reports, `dir` mode, errors |
| `02-container-analysis.md` | 2 | Probe layer, raw scanners, `container.*` / `meta.*` |
| `03-video-analysis.md` | 3 | Parser pass, `video.*` incl. color and HDR |
| `04-timeline-analysis.md` | 4 | `timeline.*`, the A/V drift algorithm |
| `05-audio-analysis.md` | 5 | Audio decode path, determinism classes, `audio.*` |
| `06-content-and-size-analysis.md` | 6 | Video decode path, `content.*`, `quality.*`, `size.*` |

Each doc carries its own per-phase acceptance section. Ship gate for v1 is the parent design doc §10 acceptance criteria.

**Architecture.** A thin `cli/` (parse → Config → dispatch) over `libmediadiff` (`probe/`, `analyzers/`, `core/`, `compare/`, `report/`, `util/`). Everything is a fingerprint comparison: `compare A B` = fingerprint(A) × fingerprint(B), and a `.snap.json` baseline simply short-circuits fingerprinting — which is why snapshot equivalence holds *by construction* rather than by testing.

**Three passes, strictly layered.** Header probe (milliseconds) → packet scan (I/O-bound, no decode) → decode scan (opt-in for `dir`, default for `compare`). Analyzers declare which passes they need; the orchestrator runs the union once per file.

**Determinism vocabulary.** Decode-determinism classes (1 `bitexact-everywhere` / 2 `bitexact-same-path` / 3 `nondeterministic`) are recorded per hashed stream in every fingerprint. The compare engine enforces hash preconditions mechanically — a class-2 cross-path comparison degrades to `skipped:hash_incomparable` with a hint, never a fabricated fail. This is where the idempotence guarantee is *engineered* rather than hoped for.

**Use cases driving the design.** UC1 silent color-range flip caught pre-merge · UC2 FFmpeg 8→9 migration over a 240-file corpus · UC3 NVENC driver upgrade under `hw-encoder` · UC4 lip-sync drift diagnosed by rate (+0.83 ms/min) · UC5 remux payload untouchability · UC6 AI-upscaler invariants under `transform` · UC7 git-native baseline lifecycle via snapshots · UC8 `inspect` as single-file archaeology.

**Three non-negotiable properties.** Trustworthy by default (false positives are P0). Explains itself (no undocumented check ever ships — the build enforces it). Actionable output (every gating finding prints the accept / tune / silence triple).

## Constraints

- **Language**: C++20, no modules, `std::format` avoided in favor of fmt — toolchain parity across GCC ≥ 12, Clang ≥ 15, AppleClang (Xcode 15+), MSVC v143
- **Build**: CMake ≥ 3.25 with presets, ninja, git; ~10 GB disk for the vcpkg FFmpeg build
- **Platforms**: Linux (primary CI, also `aarch64`), macOS (per-arch), Windows (VS 2022, `/utf-8`, VT via `SetConsoleMode`)
- **Dependencies**: vcpkg manifest mode, pinned as a git submodule, static triplets (`x64-windows-static-md` on Windows) — the single-static-binary distribution requirement drives this
- **Licensing**: FFmpeg configured decode-only LGPL; feature flags audited so GPL code is never silently linked
- **Distribution**: one static binary, zero setup at the user's end — no runtime FFmpeg install, no config file required
- **Error handling**: `expected<T, Error>`-style in core, no exceptions across the lib boundary; the CLI maps `Error.kind` to exit codes
- **Time representation**: rational everywhere (`{int64 value, AVRational tb}`); floating milliseconds appear only in rendered output
- **Determinism**: byte-identical `--json` across identical runs; fixed-K/fixed-ε algorithms; integer/rational inputs — a check that jitters is a bug, not a tolerance problem
- **Test data**: no media binaries in git; all fixtures synthesized with `-flags +bitexact -fflags +bitexact`, requiring an ffmpeg CLI ≥ 6.1 at corpus-generation time
- **CI**: 3-OS matrix, warnings-as-errors (`/W4`, `-Wall -Wextra`), vcpkg binary caching to amortize the 15–40 min uncached FFmpeg build
- **Check IDs are forever**: additions fine, renames only via alias + deprecation

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| CLI11 for argument parsing | First-class subcommands, repeatable options (`--set`, `--tol`), option groups, good Windows behavior; cxxopts lacks subcommands, boost::program_options is heavy, hand-rolling rots on the subcommand × repeatable-flag matrix | — Pending |
| vcpkg manifest mode, pinned submodule, static triplets | One workflow across three OSes; `builtin-baseline` gives reproducible versions in-repo; fine-grained FFmpeg features build exactly the LGPL decode-only subset; static triplets solve single-binary distribution | — Pending |
| FFmpeg (libav*) as the only demux/decode dependency | The only serious option; `dav1d` for fast AV1 decode | — Pending |
| Everything is a fingerprint comparison | Makes snapshot equivalence hold by construction rather than by testing | — Pending |
| Three passes, strictly layered; no analyzer re-reads the file | Bounds I/O cost and makes pass requirements declarative per analyzer | — Pending |
| Hand-rolled raw scanners (`bmff_scan`, `ebml_scan`, `ts_scan`) | libav abstracts away exactly the mechanisms doc 02 must observe (moov position, Cues offset, PCR spacing); we only observe structure, never mutate — TSDuck/libmatroska/GPAC are large deps for ~300 lines of need | — Pending |
| TSDuck kept as test reference implementation, not linked | Gets the cross-check value without the dependency surface | — Pending |
| lib/cli split — `libmediadiff` has no stdout and no `exit()` | Makes the engine unit-testable and later embeddable (CI runner, bindings) | — Pending |
| Rational time everywhere until the report layer | Eliminates a whole class of float-comparison false positives (30000/1001 vs 29.97) | — Pending |
| nlohmann `ordered_json` with one-value-per-line serialization | Insertion-order preservation → canonical output; snapshots must be pleasant in `git diff`, which is load-bearing for the serverless baseline workflow (UC7) | — Pending |
| xxHash XXH3-128 for hash chains and file identity | GB/s-class; threat model is accidental collision, not adversarial — crypto is not required | — Pending |
| libebur128 for R128 loudness and true peak | Reference-grade; hand-rolling BS.1770 is a known trap | — Pending |
| toml++ for `mediadiff.toml` | Header-only, TOML 1.0, good diagnostics; CLI11's own config hooks deliberately unused so we control precedence | — Pending |
| Default profile is `sw-encoder`, not `strict-bitexact` | Least likely to false-alarm on real pipelines; strictness is opt-in by intent | — Pending |
| pix_fmt range-folding (`yuvj420p` → `yuv420p` + `range=full`) | Two spellings of one intent must produce exactly one finding, owned by `video.color.range` | — Pending |
| v1 perceptual metric = SSIM on downscaled luma (width 128, 8×8 window) | Cheap, monotonic with visible change, threshold-explainable, deterministic with pinned swscale flags | — Pending |
| Hash exactly `bytes_per_row × height`, never `linesize` | Allocator padding differs across platforms/decoders; hashing it would be a self-inflicted class-3 nondeterminism | — Pending |
| Prefer fixed-point decoder siblings (`aac_fixed`, `ac3_fixed`) when hashing | Promotes class-2 float decoders to class-1, so hashes stay valid across machines | — Pending |
| VMAF model pinned to `vmaf_v0.6.1` and recorded in the fingerprint | Unpinned VMAF scores are incomparable across runs — the pin is a correctness feature, not a convenience | — Pending |
| Range-aware black detection (black point scaled by range and depth) | A range-unaware detector would false-alarm on exactly the files where range flipped |  — Pending |
| Exit-code split `<3` vs `≥64` | A CI contract distinguishing "regression" from "could not run" | — Pending |
| `skipped ≠ pass`, always present in JSON | Trust never requires faith; silently passing an inapplicable check is how diff tools lose credibility | — Pending |
| Apache-2.0, trunk-based on `main`, releases built by CI from `v0.x.y` tags | — | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-08-12 after initialization*
