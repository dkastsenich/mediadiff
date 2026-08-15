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

- [ ] **Phase 1: Foundation & Toolchain** - Static binary builds and runs on three platforms, with every toolchain decision recorded
- [ ] **Phase 2: Core Engine** - Registry, semantics, profiles, config, snapshots, reports and `dir` mode working end to end on stub measurements
- [ ] **Phase 3: Probe Layer, Container & Size** - Real media enters: header pass, packet sweep, raw scanners, all `container.*`/`meta.*`/`size.*` checks
- [ ] **Phase 4: Video Analysis** - Parser pass plus every `video.*` parameter, GOP, colorimetry and HDR check
- [ ] **Phase 5: Timeline Analysis** - Every `timeline.*` check and the flagship A/V drift algorithm on integer/rational math
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

**Plans**: 4/5 plans executed in 3 waves
Plans:
**Wave 1**

- [x] 01-01-PLAN.md — Tracer: vcpkg manifest, CMake presets and lib/cli targets build one static binary on x64-linux that prints `--version` and passes the LGPL and `expected<T,E>` unit tests; FFmpeg pin recorded in PROJECT.md

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 01-02-PLAN.md — Integration harness asserting all four `--version` fields, the optional quality-metric feature absent by default, and no FFmpeg shared-library dependency in the shipped executable
- [ ] 01-03-PLAN.md — Windows UTF-8 text handling: `util/fs.h` shim, UTF-16 argv conversion at entry, UTF-8 code-page manifest, VT output, non-ASCII round-trip test
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

**Plans**: 11/11 plans executed

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

**Plans**: TBD
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
  4. HDR10 and Dolby Vision configuration either survive a round trip or are reported as lost, with the extraction source (stream-level vs first-frame) recorded, and internally incoherent HDR metadata raises a non-gating `info` note even when both files share it.
  5. `video.frame_rate.measured` consumes the shared interval statistics delivered in phase 3 rather than computing its own, and the parser pass measures at under 10% overhead over a plain packet scan on the 10-minute reference file.

**Plans**: TBD
**Source doc**: `claude_docs/03-video-analysis.md` (design-doc phase 3)

### Phase 5: Timeline Analysis

**Goal**: The family mediadiff is judged on — every `timeline.*` check and the A/V drift algorithm — computed on integer/rational math with false positives designed out.
**Depends on**: Phase 3 (Phase 4 for GOP-adjacent evidence)
**UI hint**: no
**Requirements**: TIME-01, TIME-02, TIME-03, TIME-04, TIME-05, TIME-06, TIME-07, TIME-08, TIME-09, TIME-10, TIME-11, DOC-04, PERF-01, PERF-03, PERF-05
**Success Criteria** (what must be TRUE):

  1. A 0.1% audio clock error is reported as `linear-drift` with a rate in ms/min and an end delta; a spliced 100 ms trim is reported as `step` with the step time; a pure offset is reported as `constant-offset` — and each fixture produces exactly the intended finding and **nothing else**, with the full checkpoint trajectory stored in the fingerprint so snapshot comparison keeps the same fidelity.
  2. `timeline.start`, the `timeline.duration` triple (container-declared, stream-declared, computed) with internal disagreement raising an `info` note, `dts_monotonic`, `pts_unique`, `gaps` and `discontinuities` all work on presentation timelines — with `AV_NOPTS_VALUE` treated as a first-class `absent` and MPEG-TS 33-bit wraparound unwrapped rather than mistaken for a backward discontinuity.
  3. A VFR stream is classified VFR and reports `skipped:vfr` on jitter while a CFR stream reports jitter σ and max deviation — using the same shared interval statistics `video.frame_rate.measured` consumes, not a second implementation.
  4. `timeline.av_offset` reports a signed, priming-adjusted offset, and on a fixture with **non-zero encoder priming** — the common case before the audio decode path exists — the finding visibly carries `priming: unknown` with an unadjusted value rather than a confidently wrong number.
  5. `timeline.timecode` reports presence and SMPTE start value including the drop-frame flag; metadata-plus-timeline analysis of the 10-minute 1080p reference file completes in ≤ 3 s and adds under 15% over a plain packet scan, measured in CI with regression tracking over time.

**Plans**: TBD
**Source doc**: `claude_docs/04-timeline-analysis.md` (design-doc phase 4)

### Phase 6: Audio Analysis

**Goal**: The audio decode path and every `audio.*` check, moving the decode-determinism classes from shared vocabulary to a mechanically enforced guarantee.
**Depends on**: Phase 3
**UI hint**: no
**Requirements**: AUDIO-01, AUDIO-02, AUDIO-03, AUDIO-04, AUDIO-05, AUDIO-06, AUDIO-07, AUDIO-08, AUDIO-09, AUDIO-10, TRUST-01, TRUST-02, PERF-04
**Success Criteria** (what must be TRUE):

  1. `mediadiff inspect` renders a complete audio section — codec, profile carrying the HE-AAC SBR signaling mode (implicit vs explicit), sample rate, sample format/bit depth, channel count and canonical layout — and `5.1` vs `5.1(side)` is reported as a regression rather than matching on channel count.
  2. `audio.priming` resolves through `initial_padding` → container mechanism (MP4 elst / iTunSMPB / MKV CodecDelay) → `unknown` and stays stable across an MP4 → MKV → MP4 round trip, closing the `priming: unknown` gap phase 5's `timeline.av_offset`/`av_drift` shipped with.
  3. Integrated loudness matches an `ffmpeg -af ebur128` reference within ±0.1 LU on fixtures, true peak fails asymmetrically when the candidate crosses −1.0 dBTP upward from a baseline that was under it, and introduced leading/trailing silence or an interior dropout is reported as a span.
  4. `content.audio.sample_hash` locates the first divergent sample by index and time; the same file hashed via `aac_fixed` on two different builds compares equal; and a float-decoder hash across differing decode paths reports `skipped:hash_incomparable` with a remediation hint — never a fabricated pass or fail — with decoder name, class, flags and path signature recorded per hashed stream.
  5. Loudness, silence detection and hashing share a single decode sweep per track, and an audio sweep of the 10-minute reference stereo AAC file completes in under 4 s.

**Plans**: TBD
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

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6 → 7. Phases 5 and 6 are mutually independent (both gated only on Phase 3, with Phase 5 additionally using Phase 4's parser pass) and may be executed concurrently.

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Foundation & Toolchain | 4/5 | In Progress|  |
| 2. Core Engine | 11/11 | In Progress|  |
| 3. Probe Layer, Container & Size | 0/TBD | Not started | - |
| 4. Video Analysis | 0/TBD | Not started | - |
| 5. Timeline Analysis | 0/TBD | Not started | - |
| 6. Audio Analysis | 0/TBD | Not started | - |
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
| PROBE-03 | 4 (not 3) | `ParserScan` is built in doc 03 as an extension of the same sweep; it is the video phase's own infrastructure. |
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
