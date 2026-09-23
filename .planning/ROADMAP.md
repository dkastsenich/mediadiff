# Roadmap: mediadiff

## Overview

mediadiff is specified in full before any code exists: seven cross-referenced design documents in `claude_docs/` (00–06), each carrying its own acceptance section. This roadmap keeps that structure — it is a horizontal-layer build (probe layer first, then analyzer families) because that is what the artifact actually is: an engine with a thin CLI over it, not a set of user-facing vertical slices.

The journey: a static binary that builds on three platforms → a complete compare engine validated against stub measurements → real media entering through one header pass and one packet sweep → analyzer families layered on top (video, timeline, audio) → the video decode path closing v1. Nothing user-visible ships until phase 2, and no analyzer ships until the machinery it plugs into is finished — deliberately, because doc 01 §13's stub-analyzer acceptance is what proves the engine in isolation.

**Numbering note.** The design docs number their phases 0–6. GSD treats phase 0 as a sentinel, so this roadmap numbers 1–7 with a fixed offset: **ROADMAP Phase N = design-doc phase N−1 = `claude_docs/0(N−1)-*.md`**. Each phase detail records its source doc explicitly.

**Research corrections applied** (from `.planning/research/ARCHITECTURE.md` §5, §7 and `SUMMARY.md`):

1. Phase 1 gains the `expected<T,E>` dependency decision (BUILD-07), the FFmpeg 9.0-vs-8.1 baseline decision (BUILD-10), Windows UTF-8 path handling (CLI-09), and the corrected vcpkg binary-caching approach (BUILD-06, `x-gha` removed).
2. `size.*` (SIZE-01) moves out of the final phase into the probe phase — it depends only on PacketScan, which doc 06's own intro concedes.
3. Ordering hazard A resolved structurally, not by reordering: PROBE-10 lands packet-interval statistics as a shared probe-level primitive in phase 3; the video (phase 4) and timeline (phase 5) families both consume it rather than each computing their own.
4. Ordering hazard B covered, not hidden: TIME-10 puts non-zero-priming fixtures in phase 5's acceptance so the `priming: unknown` degrade path is tested when it is the common case, not the edge case.
5. Trust requirements are distributed across five phases (2, 3, 6, 7) rather than collected into a trailing trust phase — each mapped to the earliest phase that can genuinely satisfy it.

**Parallelism.** Phases 5 (timeline) and 6 (audio) both depend only on phase 3's probe layer (phase 5 additionally uses phase 4's parser pass for GOP-adjacent evidence). They are independent of each other and can be executed concurrently.

## Phases

**Phase Numbering:**

- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [x] **Phase 1: Foundation & Toolchain** - Static binary builds and runs on three platforms, with every toolchain decision recorded (completed 2026-08-15)
- [x] **Phase 2: Core Engine** - Registry, semantics, profiles, config, snapshots, reports and `dir` mode working end to end on stub measurements (completed 2026-08-18)
- [x] **Phase 3: Probe Layer, Container & Size** - Real media enters: header pass, packet sweep, raw scanners, all `container.*`/`meta.*`/`size.*` checks (completed 2026-09-06)
- [x] **Phase 4: Video Analysis** - Parser pass plus every `video.*` parameter, GOP, colorimetry and HDR check (completed 2026-09-14)
- [x] **Phase 5: Timeline Analysis** - Every `timeline.*` check and the flagship A/V drift algorithm on integer/rational math (completed 2026-09-19)
- [ ] **Phase 6: Audio Analysis** - Audio decode path, determinism classes in practice, every `audio.*` check plus sample hashing
- [ ] **Phase 7: Content & Quality** - Video decode path, `content.video.*`, opt-in `quality.*` — closes v1

## Phase Details

### Phase 1: Foundation & Toolchain

**Goal**: A single static mediadiff binary builds reproducibly and runs on Linux, macOS and Windows, with every toolchain decision the later phases depend on already made and recorded.
**Depends on**: Nothing (first phase)
**UI hint**: no
**Requirements**: BUILD-01, BUILD-02, BUILD-03, BUILD-04, BUILD-05, BUILD-06, BUILD-07, BUILD-08, BUILD-09, BUILD-10, CLI-05, CLI-09
**Success Criteria** (what must be TRUE):

  1. `mediadiff --version` runs from a clean checkout on Linux (GCC ≥ 12 / Clang ≥ 15), macOS (Xcode 15+) and Windows (VS 2022 v143), printing the tool version, the linked FFmpeg library versions, and the enabled feature set with `vmaf` absent by default.
  2. The release artifact is one static binary per platform that runs on a machine with no FFmpeg installed, and the build fails rather than silently linking any GPL FFmpeg component.
  3. Two machines resolving the same manifest get the same dependency set — the FFmpeg major-version baseline and the `expected<T, Error>` implementation are both explicit, recorded decisions rather than incidental consequences of the vcpkg baseline.
  4. CI is green across the 3-OS matrix with warnings-as-errors, and a repeat run restores vcpkg binaries from cache instead of rebuilding FFmpeg from source.
  5. A path containing non-ASCII characters opens correctly on Windows, with virtual-terminal processing confirmed enabled on a real console handle, and `scripts/gen_corpus` regenerates every fixture deterministically from a tree containing no committed media binary.

  > **Amended 2026-08-15.** As originally written this criterion also required "color output still rendering". That clause was not verifiable in Phase 1 and never could have been: mediadiff emits no styled output at this stage — no ANSI escapes, no `fmt` styling — and `NO_COLOR` is unread, because colour handling is CLI-08, which this roadmap maps to Phase 2. A human asked to confirm the rendering would have been confirming that nothing renders as nothing. The criterion conflated a Phase 1 capability (the Windows VT plumbing) with a Phase 2 one (the styled output that plumbing carries). The plumbing half is retained above and was verified on real hardware — `GetConsoleMode` reports `ENABLE_VIRTUAL_TERMINAL_PROCESSING` set after the call, with no pre-existing console flags disturbed (`conhost.exe`, 3 assertions; see `01-03-SUMMARY.md`). The rendering half moves to Phase 2 criterion 3, where there will be output to render.

**Plans**: 5/5 plans executed in 3 waves
Plans:
**Wave 1**

- [x] 01-01-PLAN.md — Tracer: vcpkg manifest, CMake presets and lib/cli targets build one static binary on x64-linux that prints `--version` and passes the LGPL and `expected<T,E>` unit tests; FFmpeg pin recorded in PROJECT.md

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 01-02-PLAN.md — Integration harness asserting all four `--version` fields, the optional quality-metric feature absent by default, and no FFmpeg shared-library dependency in the shipped executable
- [x] 01-03-PLAN.md — Windows UTF-8 text handling: `util/fs.h` shim, UTF-16 argv conversion at entry, UTF-8 code-page manifest, VT output, non-ASCII round-trip test
- [x] 01-04-PLAN.md — Repository tree, licence/notice/format/ignore files, the ENG-16 library-boundary lint, and the deterministic `gen_corpus` skeleton with recorded generator identity

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 01-05-PLAN.md — 3-OS blocking CI matrix plus 2 non-blocking legs with warnings-as-errors, boundary lint, and vcpkg binary caching via a NuGet feed on GitHub Packages

**Source doc**: `claude_docs/00-design-and-requirements.md` (design-doc phase 0)

### Phase 2: Core Engine

**Goal**: The complete compare engine — registry, comparison semantics, policy resolution, snapshots, all four report formats and `dir` orchestration — works end to end against stub measurements, so every analyzer that follows plugs into finished machinery.
**Depends on**: Phase 1
**UI hint**: no
**Requirements**: CLI-01, CLI-02, CLI-03, CLI-04, CLI-06, CLI-07, CLI-08, CLI-10, ENG-01, ENG-02, ENG-03, ENG-04, ENG-05, ENG-06, ENG-07, ENG-08, ENG-09, ENG-10, ENG-11, ENG-12, ENG-13, ENG-14, ENG-15, ENG-16, SNAP-01, SNAP-02, SNAP-03, SNAP-04, SNAP-05, SNAP-06, SNAP-07, REPORT-01, REPORT-02, REPORT-03, REPORT-04, REPORT-05, REPORT-06, REPORT-07, DIR-01, DIR-02, DIR-03, DIR-04, DIR-05, TRUST-03, TRUST-05, TRUST-08, DOC-01, DOC-02
**Success Criteria** (what must be TRUE):

  1. `mediadiff snapshot f && mediadiff compare f f.snap.json` is clean by construction, the `*.snap.json` reads cleanly in `git diff` with times stored as rationals, its envelope carries a decode-path signature that already includes the libavcodec/libavformat/swscale toolchain versions, `compare` refuses an incompatible `schema_version` major with exit 65, and `snapshot` refuses to overwrite a tracked baseline in CI without an explicit `--force`.
  2. A user can drive policy end to end — `--profile`, `mediadiff.toml`, path overrides, repeatable `--set`/`--tol` in argv order — across all five shipped profiles, and `mediadiff list-checks --effective` shows exactly the policy that was applied, with the resolved severity chain visible under `-v` and a wrong-unit tolerance rejected as exit 64 naming the expected unit.
  3. All four report formats render the same findings — TTY grouped in fixed order with the accept/tune/silence triple under each gating finding, schema-validated JSON that is byte-identical across identical runs and against a snapshot from a previous release, Markdown budgeted under GitHub's 65,536-character comment limit, and JUnit that Jenkins/GitLab display with zero integration work. Colour output renders as styling rather than literal escape sequences in a real Windows console, and auto-disables on `NO_COLOR`, non-TTY stdout and `CI=true` while staying enabled for `GITHUB_ACTIONS` (CLI-08 — the Windows VT plumbing landed in Phase 1 and was verified there; this is the first phase that emits anything for it to carry, so this is where rendering becomes checkable).
  4. `mediadiff dir a b` pairs a corpus by relative path in deterministic order under a `--threads`-bounded pool, reports unpaired files, and honours the exit-code contract (0/1/2 for regression signals, 64/65/66/70 for could-not-run) with partial JSON still emitted on 66 — all process control living in `cli/`, with the library writing nothing to stdout and never calling `exit()`.
  5. Every registered check ID resolves to documentation the build enforces: `mediadiff explain <check.id>` prints what it measures, why it matters and how to accept/tune/silence it; `mediadiff inspect` renders every implemented check family; `skipped` carries a machine-readable reason and is never conflated with `pass`; and all seven comparison semantics behave per spec with time tolerances compared in ticks rather than floats.

**Plans**: 19/19 plans complete — 16/16 executed (11 original, 2 round-2 gap-closure closing G-02-1/G-02-2, 3 round-3 gap-closure closing G-02-3/G-02-4 per CI run 31946964023), 3 round-4 gap-closure plans pending (UAT gaps G-02-5, G-02-6, G-02-7 — the 7 Windows test failures from the suite's first-ever execution)

Plans:
**Wave 1**

- [x] 02-01-PLAN.md — Contract freeze checkpoint + tracer: `compare a.snap.json b.snap.json --json` end to end

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 02-02-PLAN.md — Registry generator completion, glob matcher, aliases, build-enforced check docs

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 02-03-PLAN.md — Fail-first infrastructure: test registry, coverage gate, canary, golden and determinism harnesses

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 02-04-PLAN.md — Tolerance grammar and the seven comparison semantics

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 02-05-PLAN.md — Five profiles, severity resolution with provenance, volatile and transform behaviour

**Wave 6** *(blocked on Wave 5 completion)*

- [x] 02-06-PLAN.md — `mediadiff.toml`, the four-layer precedence merge, and `list-checks --effective`

**Wave 7** *(blocked on Wave 6 completion)*

- [x] 02-07-PLAN.md — Canonical serializer, snapshot envelope, safe write, idempotence harnesses

**Wave 8** *(blocked on Wave 7 completion)*

- [x] 02-08-PLAN.md — Shared report model, JSON plus shipped schema, Markdown budget, JUnit

**Wave 9** *(blocked on Wave 8 completion)*

- [x] 02-09-PLAN.md — TTY renderer, colour policy, accept/tune/silence triple

**Wave 10** *(blocked on Wave 9 completion)*

- [x] 02-10-PLAN.md — CLI surface completion: implicit compare, exit-code contract, `explain` and `inspect`

**Wave 11** *(blocked on Wave 10 completion)*

- [x] 02-11-PLAN.md — `dir` orchestration: pairing, bounded worker pool, corpus rollup

**Gap closure — Wave 1** *(both blocker gaps from 02-UAT.md; independent, no file overlap, run in parallel)*

- [x] 02-12-PLAN.md — G-02-1: `getenv_utf8` shim in `src/util/fs.h`, all six env reads routed through it, three false sole-reader comments corrected (unblocks the `x64-windows-static-md` C4996 build failure)
- [x] 02-13-PLAN.md — G-02-2: POSIX-portable ctest count parse at `ci.yml:246` (unblocks the `arm64-osx` test-count guard)

**Gap closure round 3 — Wave 1** *(successor gaps revealed behind the round-2 fixes; independent, no file overlap, run in parallel)*

- [x] 02-14-PLAN.md — G-02-3: `block_for` restructured to a single reachable exit (ends MSVC C4702 → C2220 at Windows build step [60/97]) plus a permanent scan gate for the FAIL()-then-statement shape
- [x] 02-15-PLAN.md — G-02-4: case-collision-free byte-order fixture with an explicit expected order and a self-proving teeth assertion (ends the `4 == 5` failure of arm64-osx test #40) plus a permanent fixture case-collision lint

**Gap closure round 3 — Wave 2** *(blocked on both round-3 wave-1 plans)*

- [x] 02-16-PLAN.md — Wire both portability lints into the required `lint` job, push, and read the new CI run per required status-check context; human checkpoint decides certify-or-iterate

**Gap closure round 4 — Wave 1** *(the 7 Windows test failures; independent, no file overlap, run in parallel)*

- [x] 02-17-PLAN.md — G-02-5 byte identity: `.gitattributes` pinning checkout to LF plus Windows stdout binary mode in `wmain`, landed together because either alone moves the failures rather than fixing them; new TRUST-05 test comparing `--json` stdout bytes against `--json=<path>` file bytes
- [x] 02-18-PLAN.md — G-02-5 (#95) and G-02-6 (#36): the process-spawn fixture's Python child writes through the binary stream, and the non-ASCII fixture path is constructed via a new `tests/support/utf8_path.h` UTF-8 helper instead of `std::filesystem::path`'s ACP narrow constructor

**Gap closure round 4 — Wave 2** *(blocked on both round-4 wave-1 plans; shares `src/cli/main.cpp` with 02-17)*

- [x] 02-19-PLAN.md — G-02-7 (#236): `allow_windows_style_options(false)` so a `/`-rooted path is not reclassified as an option on Windows (restores the CLI-06 65-not-64 contract), then push all three plans and read the run test by test; human checkpoint decides certify-or-iterate

**Source doc**: `claude_docs/01-core-concepts.md` (design-doc phase 1)

### Phase 3: Probe Layer, Container & Size

**Goal**: Real media enters the engine — one header pass and one packet sweep feed every container, metadata and size check, plus the shared primitives that later phases consume instead of recomputing.
**Depends on**: Phase 2
**UI hint**: no
**Requirements**: PROBE-01, PROBE-02, PROBE-04, PROBE-05, PROBE-06, PROBE-07, PROBE-08, PROBE-09, PROBE-10, CONT-01, CONT-02, CONT-03, CONT-04, CONT-05, CONT-06, CONT-07, CONT-08, CONT-09, SIZE-01, DIR-06, TRUST-06, TRUST-09, DOC-03
**Success Criteria** (what must be TRUE):

  1. `mediadiff inspect` on an MP4/MOV, a Matroska/WebM and an MPEG-TS file renders a complete container section — generic topology including subtitle and caption track presence, per-format mechanisms (faststart, brands, edit lists, Cues placement, CodecDelay, CC errors, PCR/PSI intervals, null ratio), per-program measurements on multi-program TS, and metadata tags compared as a set with volatile keys ignored but still shown under `-v`.
  2. A cross-container migration demotes cleanly — `container.<fmt>.*` on both sides becomes `skipped:cross_container` and comparison proceeds at the semantic layer — while truncated or garbage input degrades to `skipped:unparsed_mechanism` with a byte offset, or exits 65 cleanly, and never crashes or silently passes.
  3. `size.file`, `size.stream_bitrate`, `size.peak_bitrate` and `size.overhead` report rate economics from the packet scan alone, with peak windowing defined on DTS in ticks so results are identical across platforms.
  4. Each file is read exactly once: analyzers declare the passes they need, the orchestrator runs the union, packet-interval statistics are computed once as a shared probe-level primitive available to both the video and timeline families, and peak memory per in-flight file is measured and bounded so `--threads N` is an honest memory knob.
  5. Encoding a fixture twice with identical settings and comparing under `sw-encoder` comes back clean as a CI release blocker; every check above has both a triggering fixture pair and a clean one; and `ts_scan`'s output agrees with TSDuck's analysis of the same fixtures through a manual jig.

**Plans**: 22/22 plans executed (20/22 executed; 2 further gap-closure plans added after gap-closure round 2's re-verification left ROADMAP SC5 open on two one-line source defects)

Plans:
**Wave 1**

- [x] 03-01-PLAN.md — Core-model prerequisites: extend `SkipReason` across all four consuming sites, add `Measurement.estimated` and `Finding.evidence`, add `checked_div`, approve the 27-id check roster

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 03-02-PLAN.md — TRACER: `DemuxSession` + pass seam + `fingerprint_input`, `container.format` end-to-end through all four CLI commands

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 03-03-PLAN.md — `PacketScan` sweep, D-01 memory budget divided by threads, PROBE-10 shared primitive, `--threads` ceiling

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 03-04-PLAN.md — Topology expansion (`track_count`/`track_types`/`track_order`/`chapters`) plus `meta.tags` and `meta.tags.language`

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 03-05-PLAN.md — `bmff_scan` bounded box walk and the six `container.mp4.*` checks

**Wave 6** *(blocked on Wave 5 completion)*

- [x] 03-06-PLAN.md — `ebml_scan` vint walk, the four `container.mkv.*` checks, and CONT-02 cross-container demotion

**Wave 7** *(blocked on Wave 6 completion)*

- [x] 03-07-PLAN.md — `ts_scan`: stride autodetect, bounded PID table, PCR/PSI parsing, mux-rate estimate, ISO 13818-1 continuity carve-outs

**Wave 8** *(blocked on Wave 7 completion)*

- [x] 03-08-PLAN.md — The six `container.ts.*` checks, D-03's estimated marker, and CONT-08 program-number scoping

**Wave 9** *(blocked on Wave 8 completion)*

- [x] 03-09-PLAN.md — The four `size.*` checks with DTS-in-ticks windowing and D-02's truncated-scan refusal

**Wave 10** *(blocked on Wave 9 completion)*

- [x] 03-10-PLAN.md — PROBE-09 deterministic degradation smoke across every scanner, plus TRUST-09's TSDuck goldens

**Wave 11** *(blocked on Wave 10 completion)*

- [x] 03-11-PLAN.md — Close T-2-33, render the `inspect` container section, TRUST-06 release blocker, DOC-03 coverage gate

**Wave 12** *(gap closure — blocked on Wave 11; TRACER: one gap closed end-to-end before expansion)*

- [x] 03-12-PLAN.md — GAP 2: bound and `checked_mul` the probe budget/timeout so an ordinary flag value can never silently blank every `size.*` check (SIZE-01, DIR-06)

**Wave 13** *(gap closure — blocked on Wave 12; three independent plans, zero file overlap)*

- [x] 03-13-PLAN.md — GAP 1: remove the reachable UB in `container.mp4.fragment_duration` (checked deltas, a total order that cannot overflow) plus an extreme-DTS regression test (CONT-05, PROBE-09)
- [x] 03-14-PLAN.md — GAP 3: generate and verify the media corpus before the Test step on every CI leg, so TRUST-06 can actually run as a release blocker (TRUST-06, DOC-03)
- [x] 03-15-PLAN.md — Complete the T-2-33 choke point: control-byte-safe JUnit XML, one sanitizing CLI diagnostic helper, lint scan list corrected (CONT-03, CONT-04)

**Wave 14** *(gap closure round 2 — blocked on Wave 13; TRACER: one blocking leg proven green end-to-end on real CI before expansion)*

- [x] 03-16-PLAN.md — SC5/D-GAP-01: pin the fixture-synthesis ffmpeg by URL + SHA-256 on all five legs, re-baseline the fixture-derived goldens once against that build, prove x64-linux green on a real run with TRUST-06 observed passing (TRUST-06, TRUST-09, DOC-03)

**Wave 15** *(gap closure round 2 — blocked on Wave 14; two independent plans, zero file overlap)*

- [x] 03-17-PLAN.md — D-GAP-02 (a)+(d): suppress the Windows min/max macros for every first-party target so the MSVC leg builds, add the standard header ebml_scan actually needs, and make the JUnit escaper's control-byte output unambiguous (CONT-06, CONT-02, CONT-03, CONT-04)
- [x] 03-18-PLAN.md — D-GAP-02 (c): remove the last bash-4-only builtins from `scripts/` and add a permanent bash-3.2 portability gate to the required lint job (TRUST-06, DOC-03)

**Wave 16** *(gap closure round 2 — blocked on Wave 15)*

- [x] 03-19-PLAN.md — Prove or disprove cross-platform corpus byte-identity under the pin from real per-leg digests, then make the confirmed policy a standing CI gate (TRUST-06, TRUST-09, DOC-03)

**Wave 17** *(gap closure round 2 — blocked on Wave 16)*

- [x] 03-20-PLAN.md — Close SC5 on observed run-log evidence (three blocking legs green, TRUST-06 passing on x64-linux and arm64-osx) and correct the stale WINDOWS.md / 03-VERIFICATION.md records (TRUST-06, DOC-03)

**Wave 18** *(gap closure round 3 — TRACER: the two blocking-leg build defects proven end-to-end from source edit to observed CI Build conclusions before any expansion)*

- [x] 03-21-PLAN.md — Remove the unused `kClusterId` constant's AppleClang `-Werror` failure by giving it the untested unknown-size-Cluster case it was meant for, qualify `report_cli_error` inside `wmain` for MSVC, close code-review WR-01 (two lint-gate bypasses plus per-check self-test fixtures) and WR-02 (tar.xz path-traversal validation), then drive all three blocking legs to Build success and a reached Test step (TRUST-06, PROBE-05)

**Wave 19** *(gap closure round 3 — blocked on Wave 18)*

- [x] 03-22-PLAN.md — Close SC5 on observed run-log evidence (four `trust06_idempotence` Passed lines on x64-linux and arm64-osx, and the x64-windows-static-md Test step concluding for the first time in this phase's history), then reconcile the WINDOWS.md defect ledger and the REQUIREMENTS.md TRUST-06 status against that evidence (TRUST-06, TRUST-09, DOC-03)

**Source doc**: `claude_docs/02-container-analysis.md` (design-doc phase 2), plus `size.*` from `claude_docs/06-content-and-size-analysis.md` §4

### Phase 4: Video Analysis

**Goal**: Every `video.*` fact — stream parameters, GOP structure, colorimetry and HDR metadata — is measured from a parser pass that costs a fraction of full decode.
**Depends on**: Phase 3
**UI hint**: no
**Requirements**: PROBE-03, VIDEO-01, VIDEO-02, VIDEO-03, VIDEO-04, VIDEO-05, VIDEO-06, VIDEO-07, VIDEO-08, VIDEO-09, VIDEO-10, VIDEO-12
**Success Criteria** (what must be TRUE):

  1. A silent color-range flip produces exactly **one** finding, on `video.color.range`, whether it was spelled as a `yuvj420p` pix_fmt or as a range flag; primaries, transfer, matrix and chroma location compare alongside it; and a change **to** `unspecified` is reported as metadata loss rather than treated as a wildcard match.
  2. `mediadiff inspect` renders a complete video section — codec, profile, level, resolution, SAR/DAR, pix_fmt, declared frame rate, and a frame count always counted from the packet/parser scan rather than trusted from `nb_frames` — with a container-vs-VUI SAR conflict recording both values and flagging the conflict itself as `info`.
  3. GOP structure compares meaningfully: length, IDR interval with open/closed classification from NAL types, refs, I/P/B distribution, and interlace field order cross-checked against per-frame parser flags with `mixed` reported by proportion — while a codec with no available parser degrades to `skipped:no_parser` instead of failing.
  4. HDR10 and Dolby Vision configuration either survive a round trip or are reported as lost, with the extraction source recorded as Phase 4's stream-level `coded_side_data` arm — the first-frame side-data arm arrives with the decode pass in Phase 7 (Human Decision 1, 2026-09-13) — and internally incoherent HDR metadata raises a non-gating `info` note even when both files share it.
  5. `video.frame_rate.measured` consumes the shared interval statistics delivered in phase 3 rather than computing its own, and the fused parser pass ships a harness that measures its overhead against a plain packet scan on the 10-minute reference file and records the result as evidence; the under-10% overhead target itself is gated in Phase 5 under PERF-03 and PERF-05 (Human Decision 2, 2026-09-13).

**Plans**: 21/21 plans executed in 11 waves; 9 gap-closure plans added in 6 further waves (21 total)

Plans:
**Wave 1**

- [x] 04-01-PLAN.md — Tracer: ParserScan fused into the existing sweep, carrying `video.gop.length` end to end (+ check-id roster gate)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 04-02-PLAN.md — Encoder-based video fixtures and the `mjpeg`/`ffv1` pinned-build preflight
- [x] 04-03-PLAN.md — Parser-overhead measurement harness, recorded not gated (D-11/D-12)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 04-04-PLAN.md — HDR MDCV/CLL and coherence fixtures via codec-independent metadata options (D-09)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 04-05-PLAN.md — Hand-constructed H.264/HEVC Annex-B streams, the `dvcC` box and the SAR-conflict patch (D-01/D-02/D-03)

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 04-06-PLAN.md — Stream parameters: codec, profile, level, resolution, frame_count (VIDEO-01/02)

**Wave 6** *(blocked on Wave 5 completion)*

- [x] 04-07-PLAN.md — Shared cadence derivation (D-05/D-06/D-07), SAR/DAR/conflict and frame rate (VIDEO-01/04)

**Wave 7** *(blocked on Wave 6 completion)*

- [x] 04-08-PLAN.md — pix_fmt range fold and colorimetry, with the exactly-one-finding signature test (VIDEO-03/07/08)

**Wave 8** *(blocked on Wave 7 completion)*

- [x] 04-09-PLAN.md — GOP family: IDR cadence, open/closed from NAL types, refs, frame types, no-parser degradation (VIDEO-05/12)

**Wave 9** *(blocked on Wave 8 completion)*

- [x] 04-10-PLAN.md — `video.interlace`, declared field order cross-checked against per-frame flags (VIDEO-06)

**Wave 10** *(blocked on Wave 9 completion)*

- [x] 04-11-PLAN.md — HDR mastering-display and content-light with the D-08 precedence seam (VIDEO-09)

**Wave 11** *(blocked on Wave 10 completion)*

- [x] 04-12-PLAN.md — Dolby Vision configuration, the D-10 coherence guard, and the corpus-wide inspect section test (VIDEO-09/10)

**Gap closure** *(planned 2026-09-13 from 04-VERIFICATION.md's six gaps and its Human Decisions table; executed with `/gsd-execute-phase 4 --gaps-only`)*

- [x] 04-13-PLAN.md — Wave 1: restore main's designated-leg corpus digest lines, name the provisional ones, and make the no-rewrite rule an executable lint (BUILD-08)
- [x] 04-14-PLAN.md — Wave 2: the `resolve_sar` non-positive-denominator guard (WR-01), the AV1 level bound (IN-01) and `video.sar.md`'s unset rule (IN-02) (VIDEO-01/04)
- [x] 04-15-PLAN.md — Wave 2: `video.interlace`'s disagreement evidence compares field-order class, not raw ordinal, with both directions proven on real fixtures (VIDEO-06)
- [x] 04-16-PLAN.md — Wave 2: a fixture pair whose two colorimetric spellings survive distinctly, replacing the vacuous yuvj mirror test and the byte-identical DOC-03 clean pair (VIDEO-03)
- [x] 04-17-PLAN.md — Wave 3: scope or remove the file-wide `-Wmaybe-uninitialized` suppressions in all six video analyzers, enforced by a lint (WR-03, BUILD-05)
- [x] 04-18-PLAN.md — Wave 3: correct what a `state`-semantic pass means (WR-02) and the registry comment behind it, and decide the inspect test's scope outside its own output (VIDEO-01/02/10)
- [x] 04-19-PLAN.md — Wave 4: PROBE-03 and VIDEO-09 set to `Deferred` with amended SC4/SC5 and placement rows, and VIDEO-03's text corrected to its tested behaviour (PROBE-03, VIDEO-03/09)
- [x] 04-20-PLAN.md — Wave 5: confirm, push, open a DRAFT PR, and capture the designated x64-linux leg's corpus digest listing (BUILD-05/08, checkpoints)
- [x] 04-21-PLAN.md — Wave 6: transcribe that listing, push behind a confirmation, and confirm the designated leg green including its five leg-only goldens (BUILD-05/08, checkpoints)

**Source doc**: `claude_docs/03-video-analysis.md` (design-doc phase 3)

### Phase 5: Timeline Analysis

**Goal**: The family mediadiff is judged on — every `timeline.*` check and the A/V drift algorithm — computed on integer/rational math with false positives designed out.
**Depends on**: Phase 3 (Phase 4 for GOP-adjacent evidence)
**UI hint**: no
**Requirements**: TIME-01, TIME-02, TIME-03, TIME-04, TIME-05, TIME-06, TIME-07, TIME-08, TIME-09, TIME-10, TIME-11, DOC-04, PERF-01, PERF-03, PERF-05
**Success Criteria** (what must be TRUE):

  1. A 0.1% audio clock error is reported as `linear-drift` with a rate in ms/min and an end delta; a spliced trim with a timestamp discontinuity is reported as a non-pass `irregular` pattern with its residual max, and a seamlessly re-timestamped trim is documented as undetectable from timestamps alone until Phase 6; a pure offset is reported as `constant-offset` — and each fixture produces exactly the intended finding and **nothing else**, with the full checkpoint trajectory stored in the fingerprint so snapshot comparison keeps the same fidelity. (amended 2026-09-18, UD-1)
  2. `timeline.start`, the `timeline.duration` triple (container-declared, stream-declared, computed) with internal disagreement raising an `info` note, `dts_monotonic`, `pts_unique`, `gaps` and `discontinuities` all work on presentation timelines — with `AV_NOPTS_VALUE` treated as a first-class `absent` and MPEG-TS 33-bit wraparound unwrapped rather than mistaken for a backward discontinuity.
  3. A VFR stream is classified VFR and reports `skipped:vfr` on jitter while a CFR stream reports jitter σ and max deviation — using the same shared interval statistics `video.frame_rate.measured` consumes, not a second implementation.
  4. `timeline.av_offset` reports a signed, priming-adjusted offset, and on a fixture with **non-zero encoder priming** — the common case before the audio decode path exists — the finding visibly carries `priming: unknown` with an unadjusted value rather than a confidently wrong number.
  5. `timeline.timecode` reports presence and SMPTE start value including the drop-frame flag; metadata-plus-timeline analysis of the 10-minute 1080p reference file completes in ≤ 3 s (recorded, never asserted, D-13) and is measured in CI with regression tracking over time via a committed instruction-count baseline ratchet (`tests/golden/PERF_BASELINE.txt`), not an asserted under-15% ratio (amended, 05-12-PLAN.md, 2026-09-17: measured overhead was 33% against the original <15% target, an absolute cost of 32-50 ms wall-clock — see the amended `PERF-03` text in REQUIREMENTS.md for the full evidence and measurement basis).

**Plans**: 23/25 plans executed (12 gap-closure plans added 2026-09-18 from 05-VERIFICATION.md)

Plans:
**Wave 1**

- [x] 05-01-PLAN.md — Wave 1: the 16-id check roster checkpoint and the `timeline.start` tracer (global origin + per-stream relative, D-03), plus the shared DOC-04 no-others harness (TIME-01/03, DOC-04)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 05-02-PLAN.md — Wave 2: the arithmetic primitives — a 128-bit-safe accumulator in `core/rational.h` and doc 04 §1.2's 33-bit TS unwrap as a pure function (TIME-01/02)
- [x] 05-03-PLAN.md — Wave 2: D-05's grid-conformance CFR/VFR rule and the span-derived measured rate, fixing a shipped `video.frame_rate.measured` false positive on an NTSC MP4→MKV remux (TIME-05)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 05-04-PLAN.md — Wave 3: the `timeline.duration` triple with per-member absent state, and `timeline.duration.coherence` firing even when both files share the incoherence (TIME-01/03, DOC-04)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 05-05-PLAN.md — Wave 4: `timeline.dts_monotonic` and `timeline.pts_unique`, sentinels excluded rather than counted (TIME-01/04, DOC-04)

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 05-06-PLAN.md — Wave 5: `timeline.gaps` and `timeline.wrap_events`, with the mid-file 33-bit wrap fixture proving no false gap and no false discontinuity (TIME-02/04, DOC-04)

**Wave 6** *(blocked on Wave 5 completion)*

- [x] 05-07-PLAN.md — Wave 6: `timeline.discontinuities` split from `timeline.discontinuities.flagged` via a new bounded `discontinuity_indicator` offset seam on `ts_scan` (TIME-02/04, DOC-04)

**Wave 7** *(blocked on Wave 6 completion)*

- [x] 05-08-PLAN.md — Wave 7: `timeline.jitter` (integer-sqrt fixed-point sigma) and `timeline.vfr_profile` (D-06 grid-relative bins, comparable across timebases) (TIME-05, DOC-04)

**Wave 8** *(blocked on Wave 7 completion)*

- [x] 05-09-PLAN.md — Wave 8: in-sweep priming capture, the shared `resolve_priming` primitive, and `timeline.av_offset` with D-10's raw/adjusted dual storage and D-11's unsoftened severity (TIME-06/09/10, DOC-04)

**Wave 9** *(blocked on Wave 8 completion)*

- [x] 05-10-PLAN.md — Wave 9: the flagship `timeline.av_drift` / `timeline.av_drift.pattern` split, doc 04 §3's 32-checkpoint least-squares fit with 128-bit sums, and the stored trajectory (TIME-07/08, DOC-04)

**Wave 10** *(blocked on Wave 9 completion)*

- [x] 05-11-PLAN.md — Wave 10: `timeline.timecode` / `timeline.timecode.value` from the header pass alone, with the two unreachable sources reported honestly (TIME-11, DOC-04)

**Wave 11** *(blocked on Wave 10 completion)*

- [x] 05-12-PLAN.md — Wave 11: the instruction-count perf harness, the committed baseline ratchet, the designated-leg CI step, and the visible `PERF-03` / SC5 amendments (PERF-01/03/05)

**Wave 12** *(blocked on Wave 11 completion)*

- [x] 05-13-PLAN.md — Wave 12: capture the designated `x64-linux` leg's digest listing and first real perf baseline, transcribe both, and confirm every leg-only gate actually ran (DOC-04, PERF-05)

**Gap closure** *(from 05-VERIFICATION.md, 2026-09-18; sequential on one branch)*

**Wave 13**

- [x] 05-14-PLAN.md — Gap 3 and Gap 6: priming samples converted through the sample rate at both av_sync call sites, and the one-entry `sorted_pts_with_span` out-of-bounds read fixed and unit-tested via `detail::` (TIME-06/07/09/10)
- [x] 05-15-PLAN.md — Gap 4, data half: a bounded PES-header PTS/DTS seam in `ts_scan`, and the pure `apply_container_dts` join (TIME-04)
- [x] 05-16-PLAN.md — Gap 2, part 1: the promoted `TimelinePacketView` (33-bit unwrap plus cross-stream epoch), consumed by timeline.start/duration, frame_rate.measured and size bitrate (TIME-01/02/03)

**Wave 14** *(blocked on 05-14, 05-16)*

- [x] 05-17-PLAN.md — Gap 2, part 2: correct declared durations on a wrapping TS via an overflow-corrected re-probe, or withhold them (TIME-02/03)

**Wave 15** *(blocked on 05-14, 05-16, 05-17)*

- [x] 05-18-PLAN.md — Gap 2, part 3: av_sync and jitter_vfr on the views, the wrap pair asserted whole-report, and WINDOWS #26/#27/#30 closed (TIME-02/05/06/07, DOC-04)

**Wave 16** *(blocked on 05-18)*

- [x] 05-19-PLAN.md — Gap 5 (WINDOWS #28): sub-tick-quantization-aware vfr_profile bins and jitter sigma (UD-2), plus the NTSC MP4/MKV whole-report assertion (TIME-05/06/09, DOC-04)
- [x] 05-21-PLAN.md — Gap 1, research: a calibrated scratch harness evaluating piecewise checkpoint mappings, then a blocking-human decision to adopt a design or narrow the vocabulary (UD-1) (TIME-07)

**Wave 17** *(blocked on 05-15, 05-17, 05-18, 05-19)*

- [x] 05-20-PLAN.md — Gap 4, consumer half: container-truth DTS substituted once in the orchestrator for every MPEG-TS DTS consumer; the declared sets that enshrined the inferred tie are corrected (TIME-01/04, DOC-04)

**Wave 18** *(blocked on 05-20, 05-21)*

- [x] 05-22-PLAN.md — Gap 1, code: implement the decided SC1 branch and span source in av_sync, with unit and whole-report SC1 coverage (TIME-07/08, DOC-04)

**Wave 19** *(blocked on 05-22)*

- [x] 05-23-PLAN.md — Gap 1, contract: compiled-in docs for timestamp-only limits, the vocabulary amendments where decided, and the residual MP4-to-TS drift ledger record (TIME-07, DOC-04)

**Wave 20** *(blocked on 05-23)*

- [x] 05-24-PLAN.md — Designated-leg confirmation: local pre-flight, a blocking-human push, and log capture with any required transcription (DOC-04, PERF-05)

**Wave 21** *(blocked on 05-24)*

- [x] 05-25-PLAN.md — A blocking-human second push (keep or drop any perf-baseline commit) and the confirmed-green designated leg (DOC-04, PERF-05)

**Source doc**: `claude_docs/04-timeline-analysis.md` (design-doc phase 4)

### Phase 6: Audio Analysis

**Goal**: The audio decode path and every `audio.*` check, moving the decode-determinism classes from shared vocabulary to a mechanically enforced guarantee.
**Depends on**: Phase 3
**UI hint**: no
**Requirements**: AUDIO-01, AUDIO-02, AUDIO-03, AUDIO-04, AUDIO-05, AUDIO-06, AUDIO-07, AUDIO-08, AUDIO-09, AUDIO-10, TRUST-01, TRUST-02, PERF-04
**Success Criteria** (what must be TRUE):

  1. `mediadiff inspect` renders a complete audio section — codec, profile carrying the HE-AAC SBR signaling mode (implicit vs explicit), sample rate, sample format/bit depth, channel count and canonical layout — and `5.1` vs `5.1(side)` is reported as a regression rather than matching on channel count.
  2. `audio.priming` resolves through `initial_padding` → container mechanism (MP4 elst / iTunSMPB / MKV CodecDelay) → `unknown`, stays stable across an MP4 → MKV remux, and closes the `priming: unknown` gap phase 5's `timeline.av_offset`/`av_drift` shipped with wherever both sides expose a priming basis — with the two cases that cannot carry one, an MP4 → MKV → MP4 round trip and any MPEG-TS side, reported as measured non-passes rather than silently absorbed. (amended 2026-09-22, phase 6 verification)

  > **Amended 2026-09-22.** As originally written this criterion required priming to stay stable across a full MP4 → MKV → MP4 round trip, and to close the phase 5 gap outright. Both halves are contradicted by measurement, and neither is a defect in this project's code.
  >
  > **The round trip.** Matroska stores priming as `CodecDelay` in **nanoseconds**. Converting 1024 samples at 44100 Hz to ns and back to an MP4 edit-list `media_time` is lossy: `mediadiff compare tests/fixtures/audio_prime_base.mp4 tests/fixtures/audio_prime_roundtrip2.mp4` reports `1024` vs `1014`, reproduced three times. D-14 makes `audio.priming` `semantic=exact` over a string, so no tolerance can absorb ~10 samples, and relaxing D-14 to admit them would blunt the check on the real priming changes it exists to catch. A single MP4 → MKV hop **is** stable (`tests/integration/test_audio_priming.cpp`, passing). The round-trip residual is now an asserted expected non-pass in that same harness, not an unexplained failure. Ledger: `.planning/WINDOWS.md` #36, open.
  >
  > **The phase 5 gap.** D-16's shared-basis span (`src/analyzers/timeline/av_sync.cpp`) verifiably closes the gap for MP4-vs-MP4 priming pairs — `span_basis=adjusted` on both sides, zero drift. It cannot close it for the two MP4-to-TS pairs `.planning/WINDOWS.md` #32 names, because MPEG-TS carries no priming mechanism at all: no `skip_samples`, no `initial_padding`, and no edit list survives the remux. The shared-basis rule correctly declines to fabricate a basis and falls back to raw-to-raw per D-11, which still reports a residual. Closing that needs decode-based priming detection for TS, filed as a follow-up on #32, which stays open.
  >
  > Evidence: `.planning/phases/06-audio-analysis/06-VERIFICATION.md` (commit `8b7488e`).
  3. Integrated loudness matches an `ffmpeg -af ebur128` reference within ±0.1 LU on fixtures, true peak fails asymmetrically when the candidate crosses −1.0 dBTP upward from a baseline that was under it, and introduced leading/trailing silence or an interior dropout is reported as a span.
  4. `content.audio.sample_hash` locates the first divergent sample by index and time; the same file hashed via `aac_fixed` on two different builds compares equal; and a float-decoder hash across differing decode paths reports `skipped:hash_incomparable` with a remediation hint — never a fabricated pass or fail — with decoder name, class, flags and path signature recorded per hashed stream.
  5. Loudness, silence detection and hashing share a single decode sweep per track, and an audio sweep of the 10-minute reference stereo AAC file completes in under 4 s.

**Plans**: 18/20 plans executed. 06-01..06-13 were executed in 13 waves. 06-14..06-20 are gap-closure plans (VERIFICATION.md gap 3 plus 06-13's pending CI confirmation) in 7 further waves. All waves are sequential: nearly every plan touches `src/core/checks.def`, `src/probe/orchestrator.cpp`, `CMakeLists.txt` and `tests/integration/test_doc03_coverage.cpp`, and the gap plans share one working tree, one build directory and `src/probe/audio_decode.*`, so no two plans share a wave.

Plans:
**Wave 1**

- [x] 06-01-PLAN.md — Roster checkpoint (14 ids, the D-05 signature format, the D-09 id) plus the TRACER: `Pass::audio_decode`, the untrimmed fixed-block XXH3-128 chain, extended `HashChain` through the snapshot contract, and `content.audio.sample_hash` end to end (AUDIO-08/10, TRUST-01)

**Wave 2** *(blocked on Wave 1)*

- [x] 06-02-PLAN.md — Fixture foundry: `tools/gen_he_aac.py` with a decode-round-trip selftest (D-10/D-11), the lossless loudness/true-peak/silence fixtures with their committed `ffmpeg -af ebur128` text reference (D-13), and the layout, parameter and priming fixtures including the multi-edit and fragmented MP4 edge cases (AUDIO-02/03/04/05/06/07)

**Wave 3** *(blocked on Wave 2)*

- [x] 06-03-PLAN.md — The six header-pass parameter checks, `5.1` vs `5.1(side)` as a layout regression, and the corpus-wide declared-set re-baselining they cause (AUDIO-01/02)

**Wave 4** *(blocked on Wave 3)*

- [x] 06-04-PLAN.md — `audio.profile` carrying the SBR signaling mode: the no-decode ASC fast path plus the bounded one-packet fallback, both in the header pass (AUDIO-01/03, D-12)

**Wave 5** *(blocked on Wave 4)*

- [x] 06-05-PLAN.md — `--hash-decoder`, the determinism-class table with D-06's `mp3`/`mp2` promotion, the per-hashed-stream `decode_path` record, and the three-way class proof (AUDIO-08/09, TRUST-01/02)

**Wave 6** *(blocked on Wave 5)*

- [x] 06-06-PLAN.md — `audio.priming`: the resolver extended in place with the container-mechanism tier, `unknown` as a comparable value, trailing padding in evidence (AUDIO-04, D-14/D-15/D-17)

**Wave 7** *(blocked on Wave 6)*

- [x] 06-07-PLAN.md — D-16: the priming-state-gated `av_drift` checkpoint span through a generalised evidence-shape-gated override; MP4-vs-MP4 priming pairs now share the trimmed basis and stay clean, but the MP4-to-TS `WINDOWS.md` #32 pairs stay open (TS priming confirmed genuinely unrecoverable after remux) (AUDIO-04)

**Wave 8** *(blocked on Wave 7)*

- [x] 06-08-PLAN.md — libebur128 loudness and true peak in the shared sweep, ±0.1 LU against the committed reference, and the asymmetric −1.0 dBTP ceiling escalation (AUDIO-05/06/10)

**Wave 9** *(blocked on Wave 8)*

- [x] 06-09-PLAN.md — `audio.silence.edges` / `.dropouts` as spans, with every threshold a named constant echoed in `--explain` (AUDIO-07/10)

**Wave 10** *(blocked on Wave 9)*

- [x] 06-10-PLAN.md — D-09: `meta.decode_errors` as a counted gating check at exit 1, the narrowed exit-66 path, the amendment recorded in two places, and the decode pass brought inside the byte-flip fuzz smoke (AUDIO-08/10)

**Wave 11** *(blocked on Wave 10)*

- [x] 06-11-PLAN.md — SC1's `inspect` audio section, registry-enumerated so a later id cannot ship invisible, plus the corpus-wide clean sweep (AUDIO-01/02/03)

**Wave 12** *(blocked on Wave 11)*

- [x] 06-12-PLAN.md — `PERF-04`'s harness: wall clock measured and printed, the instruction-count ratchet gating, wired into the designated CI leg (PERF-04, AUDIO-10)

**Wave 13** *(blocked on Wave 12)*

- [x] 06-13-PLAN.md — Designated-leg round trip: transcribe the digest and perf baseline from the real run, and close D-06's cross-architecture claim on an arm64 measurement — confirmed or demoted (AUDIO-09, TRUST-01, PERF-04)

**Wave 14** *(gap closure; blocked on Wave 13)*

- [x] 06-14-PLAN.md — WR-02: a decode stopped by the error limit is labelled `truncated` and degrades to `skipped:hash_incomparable`; level checks skip `partial_scan` with the stop reason; the single stop-reason vocabulary; the deferred review findings (AUDIO-08, TRUST-02)

**Wave 15** *(blocked on Wave 14)*

- [x] 06-15-PLAN.md — CR-01: sinks configured from the decoded frame's rate, proven in-process on the real divergent codecpar state. CR-02: every frame re-validated against the recorded configuration, so no mismatched shape reaches a sink (AUDIO-08, AUDIO-10)

**Wave 16** *(blocked on Wave 15)*

- [x] 06-16-PLAN.md — CR-03: non-finite or out-of-range float PCM stops level measurement and normalize is bounded. WR-03: receive-side failures count toward the consecutive bound. Plus a corpus differential and the audio ratchet (AUDIO-05, AUDIO-07)

**Wave 17** *(blocked on Wave 16)*

- [x] 06-17-PLAN.md — CR-04: a 0.010 dB rise deadband on the -1.0 dBTP escalation, with SC3's in-tolerance material crossing still failing (AUDIO-06)

**Wave 18** *(blocked on Wave 17)*

- [x] 06-18-PLAN.md — CR-05: `audio.profile` never depends on host timing (a deterministic probe; open failure is an Error), decode-observed rate evidence for every implicit_decoded resolution, and WR-09's stale comment corrected (AUDIO-03)

**Wave 19** *(blocked on Wave 18)*

- [ ] 06-19-PLAN.md — WR-07: `span_basis` "adjusted" only when the trim was actually reconstructed; the amended SC2 assertions re-run unchanged (AUDIO-04)

**Wave 20** *(blocked on Wave 19)*

- [ ] 06-20-PLAN.md — Final designated-leg CI round trip behind a human-approved push, folding in 06-13's pending confirmation (PERF-04, AUDIO-09, TRUST-01)

**Source doc**: `claude_docs/05-audio-analysis.md` (design-doc phase 5)

### Phase 7: Content & Quality

**Goal**: The video decode path closes v1 — frame-exact content comparison, perceptual scoring and opt-in full-reference quality metrics, all inside one decode sweep and under the same trust guarantees as everything before it.
**Depends on**: Phase 4, Phase 5, Phase 6
**UI hint**: no
**Requirements**: VIDEO-11, CONTENT-01, CONTENT-02, CONTENT-03, CONTENT-04, CONTENT-05, CONTENT-06, CONTENT-07, CONTENT-08, CONTENT-09, CONTENT-10, CONTENT-11, TRUST-04, TRUST-07, PERF-02
**Success Criteria** (what must be TRUE):

  1. A one-frame bitstream corruption is located exactly — first divergent frame index and PTS, contiguous divergent ranges merged at 1-frame gaps, total differing count — from a hash chain computed over exactly `bytes_per_row(width) × height` per plane, never `linesize`.
  2. `content.video.perceptual` reports SSIM min, mean, first frame below threshold and a worst-10 list, pairing the overlapping prefix with truncation noted in evidence when frame counts differ; frozen and black runs are detected as spans, with black detection normalized by color range and bit depth so a range flip does not false-alarm the detector.
  3. Perceptual and `quality.*` checks refuse to compare across differing decode/scaler paths, carrying the same path-signature preconditions as `hash` checks; `--sample N` marks the fingerprint and only equal-N fingerprints compare, with mismatches reporting `skipped:sampling_mismatch`.
  4. `quality.psnr` and `quality.ssim` report min and mean in-tree at native resolution, `quality.vmaf` runs behind `MEDIADIFF_WITH_VMAF` with model `vmaf_v0.6.1` pinned and recorded in the fingerprint and refuses `--sample` as `skipped:sampling_conflict`, and against a snapshot all three report `skipped:requires_media` while still showing stored scores for trend context.
  5. One decode sweep per side feeds hashing, perceptual scoring, frozen/black detection and A53/CEA-708 closed-caption presence, with `compare` running baseline and candidate in lockstep at one frame in flight per side; the full content pass runs at ≥ 4× realtime on software decode and produces identical hash chains at 1, 4 and 16 threads.

**Plans**: TBD
**Source doc**: `claude_docs/06-content-and-size-analysis.md` (design-doc phase 6), minus `size.*` (moved to Phase 3)
**Cross-cutting note**: Phase 7 also completes VIDEO-09's first-frame HDR side-data extraction arm, deferred from Phase 4 with the decode pass (Human Decision 1, 2026-09-13) — see Cross-cutting requirement placements below.

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6 → 7. Phases 5 and 6 are mutually independent (both gated only on Phase 3, with Phase 5 additionally using Phase 4's parser pass) and may be executed concurrently.

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Foundation & Toolchain | 5/5 | Complete | 2026-08-15 |
| 2. Core Engine | 19/19 | Complete   | 2026-08-18 |
| 3. Probe Layer, Container & Size | 22/22 | Complete    | 2026-09-06 |
| 4. Video Analysis | 21/21 | Complete    | 2026-09-14 |
| 5. Timeline Analysis | 25/25 | Complete    | 2026-09-19 |
| 6. Audio Analysis | 18/20 | In Progress|  |
| 7. Content & Quality | 0/TBD | Not started | - |

## Coverage

All 138 v1 requirements map to exactly one phase. No orphans, no duplicates.

| Phase | Requirements | Count |
|-------|--------------|-------|
| 1. Foundation & Toolchain | BUILD-01…10, CLI-05, CLI-09 | 12 |
| 2. Core Engine | CLI-01/02/03/04/06/07/08/10, ENG-01…16, SNAP-01…07, REPORT-01…07, DIR-01…05, TRUST-03/05/08, DOC-01/02 | 48 |
| 3. Probe Layer, Container & Size | PROBE-01/02/04/05/06/07/08/09/10, CONT-01…09, SIZE-01, DIR-06, TRUST-06/09, DOC-03 | 23 |
| 4. Video Analysis | PROBE-03, VIDEO-01…10, VIDEO-12 | 12 |
| 5. Timeline Analysis | TIME-01…11, DOC-04, PERF-01/03/05 | 15 |
| 6. Audio Analysis | AUDIO-01…10, TRUST-01/02, PERF-04 | 13 |
| 7. Content & Quality | VIDEO-11, CONTENT-01…11, TRUST-04/07, PERF-02 | 15 |
| **Total** | | **138** |

### Cross-cutting requirement placements (and why)

These requirements do not sit in the phase their ID prefix suggests. Each is placed in the earliest phase that can *genuinely* satisfy it rather than the phase that owns its namespace.

| Requirement | Placed in | Reason |
|---|---|---|
| SIZE-01 | 3 (not 7) | Depends only on PacketScan; doc 06's own intro concedes this. Research recommends the move; it also shrinks the heaviest phase. |
| PROBE-10 | 3 | Shared interval-statistics primitive must exist in the phase that builds PacketScan, so phases 4 and 5 consume rather than duplicate it (hazard A). |
| PROBE-03 | 4 (not 3) | `ParserScan` is built in doc 03 as an extension of the same sweep; it is the video phase's own infrastructure. Its under-10% overhead target is gated in Phase 5 alongside PERF-03/PERF-05, not asserted as a Phase 4 success criterion (Human Decision 2, 2026-09-13). |
| VIDEO-09 | 4 (first-frame arm in 7) | HDR checks (`hdr.mdcv`/`hdr.cll`/`hdr.dovi`) ship in Phase 4 via the stream-level `coded_side_data` extraction source; the first-frame side-data source needs a decoded frame, which the decode pass does not deliver until phase 7 — mirroring VIDEO-11's split (Human Decision 1, 2026-09-13). |
| DIR-06 | 3 (not 2) | Per-file peak memory can only be asserted for real once PacketScan's packet arrays exist; phase 2 still delivers the `--threads` pool bound (DIR-05). |
| VIDEO-11 | 7 (not 4) | `video.closed_captions` detects during the decode pass, which does not exist until phase 7. Phase 4 registers the check and ships the `skipped:requires_decode` path; phase 7 makes detection real. **Flagged as a judgment call beyond the four mandated corrections.** |
| TRUST-03 | 2 | The path-signature composition (must include libav* toolchain versions) is verifiable as engine work before any decoder exists — and must be right before phases 6/7 write signatures. |
| TRUST-04 | 7 | The checks that must carry the preconditions (`content.video.perceptual`, `quality.*`) only exist in phase 7; phase 2 delivers the generalized precondition plumbing under ENG-04. |
| TRUST-05, TRUST-08 | 2 | Determinism harness and cross-release idempotence job are engine-level and testable against stub/canned fingerprints. |
| TRUST-06 | 3 | The encode-twice-and-compare release blocker becomes real the moment the first real analyzers exist. |
| TRUST-01, TRUST-02 | 6 | The first genuinely hashed stream is `content.audio.sample_hash`; doc 05 §3 is the normative determinism-class table. |
| TRUST-07 | 7 | Thread-count hash invariance is a property of the video decode path (doc 06 §1). |
| DOC-01, DOC-02 | 2 | Build-enforced doc existence and the explain-text standard ship with the registry; every later phase adds documents under that gate. |
| DOC-03 | 3 | The fixture-pair convention (one triggering, one clean) is established with the first real checks; every later phase's acceptance repeats it. |
| DOC-04 | 5 | The *no-others* clause is a timeline-specific acceptance property — timeline is where false positives breed. |
| PERF-01, PERF-03, PERF-05 | 5 | The first perf targets land with timeline; the CI perf-tracking harness ships with them rather than at the end. |
