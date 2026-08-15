# Requirements: mediadiff

**Defined:** 2026-08-12
**Core Value:** A media-aware diff CI can trust — a no-change re-run under the right profile is clean out of the box, every real regression is caught, explained, and actionable. False positives are P0.

Requirements are derived from the seven design documents in `claude_docs/` (00–06), then extended by four parallel research passes (`.planning/research/`). Requirements marked **[R]** originate from research rather than the original doc set; their source is cited.

## v1 Requirements

### Build & Distribution

- [x] **BUILD-01**: Project builds from a clean checkout on Linux (GCC ≥ 12 / Clang ≥ 15), macOS (Xcode 15+), and Windows (VS 2022 v143) via CMake ≥ 3.25 presets — *all three blocking legs green in CI run 31823918842 (MSVC v143 / VS 17.0 confirmed)*
- [x] **BUILD-02**: Dependencies resolve reproducibly through a vcpkg manifest with a pinned `builtin-baseline`, vcpkg itself pinned as a git submodule
- [x] **BUILD-03**: FFmpeg links as a decode-only LGPL subset (`avcodec`, `avformat`, `swscale`, `swresample`, `dav1d`, `zlib`; `default-features: false`), with a build-time assertion that no GPL component is linked
- [x] **BUILD-04**: The release artifact is a single static binary per platform requiring no runtime FFmpeg install (`x64-windows-static-md` on Windows; static triplets elsewhere)
- [x] **BUILD-05**: CI runs a 3-OS matrix with warnings-as-errors (`/W4`, `-Wall -Wextra`) and green status is required to merge — *matrix green in run 31823918842; enforced by repository ruleset "main merge gate" (id 20862843, active) requiring the 3 blocking legs plus the lint job, advisory legs correctly excluded*
- [x] **BUILD-06**: CI caches vcpkg binaries so an incremental build does not rebuild FFmpeg — **[R]** using NuGet/GitHub-Packages or `lukka/run-vcpkg`, **not** the removed `x-gha` backend (research: STACK, corrects doc 00 §5.1)
- [x] **BUILD-07**: **[R]** An `expected<T, E>` implementation is pinned as an explicit dependency, since `std::expected` is C++23 and the project targets C++20 (research: ARCHITECTURE — gap in doc 00 §5.2 vs doc 00 §9)
- [x] **BUILD-08**: `scripts/gen_corpus.{sh,ps1}` synthesizes every test fixture deterministically (`-flags +bitexact -fflags +bitexact`); no media binary is ever committed to git
- [x] **BUILD-09**: Optional `libvmaf` support is gated behind `MEDIADIFF_WITH_VMAF` and absent by default
- [x] **BUILD-10**: **[R]** The FFmpeg major-version baseline is an explicitly recorded decision (9.0 vs 8.1), not an incidental consequence of the vcpkg baseline (research: STACK — FFmpeg 9.0 released 2026-08-04; docs say "7.x/8.x")

### CLI Surface

- [ ] **CLI-01**: User can run `mediadiff <BASELINE> <CANDIDATE>` with two bare positionals and get an implicit compare
- [ ] **CLI-02**: User can invoke the `compare`, `snapshot`, `dir`, `inspect`, `list-checks`, and `explain` subcommands
- [ ] **CLI-03**: User can repeat `--set <glob>=<severity>` and `--tol <check>=<value>`, and later flags override earlier ones in argv order
- [ ] **CLI-04**: User can request reports with `--json[=path]` and repeatable `--report kind=path` for `md` and `junit`
- [x] **CLI-05**: `mediadiff --version` prints the tool version, linked FFmpeg library versions, and enabled features (`vmaf`, `cuda`)
- [ ] **CLI-06**: Exit codes follow the contract: `0` clean, `1` fail findings, `2` warn under `--strict`, `64` usage, `65` unreadable input, `66` decode failure mid-analysis, `70` internal
- [ ] **CLI-07**: On exit code `66`, partial JSON is still emitted so CI can see what was measured before the failure
- [ ] **CLI-08**: Color output auto-disables on `NO_COLOR`, non-TTY stdout, and `CI=true`, but stays enabled when `GITHUB_ACTIONS=true`; `--no-color` and `--ascii` force it manually
- [x] **CLI-09**: **[R]** Windows non-ASCII paths work end to end — UTF-16 args via `CommandLineToArgvW` converted once to UTF-8, all file I/O through a `util/fs.h` shim, VT sequences enabled via `SetConsoleMode` (research: PITFALLS — must be phase 0, not retrofitted)
- [ ] **CLI-10**: A tolerance given in the wrong unit for a check is a usage error (exit `64`) that names the expected unit

### Core Engine

- [x] **ENG-01**: A single check registry (`src/core/checks.def`) generates the ID enum, the registry, and the docs manifest — the build fails if `docs/checks/<id>.md` is missing for any registered ID
- [x] **ENG-02**: Check IDs match by segment-wise glob (`*` one segment, `**` trailing segments) for `--set` and config, with no regex
- [x] **ENG-03**: A renamed check resolves through `deprecated_alias` at config-parse time with a warning, so existing user configs keep working
- [ ] **ENG-04**: All seven comparison semantics work per spec: `exact`, `±tol`, `set`, `presence`, `hash`, `dist`, `span`
- [ ] **ENG-05**: Time-unit tolerances compare in ticks/samples using rational math, never floats
- [ ] **ENG-06**: Severity resolves through the chain built-in → profile → config globs (file order) → CLI `--set` (argv order), last writer wins, and the resolved chain appears in evidence under `-v`
- [ ] **ENG-07**: `volatile`-flagged checks default to `ignore` in every profile, but their differing values are still computed and shown under `-v`
- [ ] **ENG-08**: All five profiles ship and behave per the normative matrix: `strict-bitexact`, `sw-encoder`, `hw-encoder`, `remux`, `transform`
- [ ] **ENG-09**: Default profile is `sw-encoder` when neither `--profile` nor a TOML `profile=` is given
- [ ] **ENG-10**: The `transform` profile converts affected identity checks into checks against a declared expectation block (`expect.resolution`) instead of baseline equality
- [ ] **ENG-11**: Config merges in precedence order profile → `[severity]`/`[tolerance]` → matching `[override.*]` blocks in file order → CLI
- [ ] **ENG-12**: `mediadiff list-checks --effective` dumps the merged effective policy so a user can debug config surprises
- [ ] **ENG-13**: `mediadiff explain <check.id>` prints that check's documentation, compiled into the binary at build time
- [ ] **ENG-14**: `skipped` is a first-class status carrying a machine-readable reason, always present in JSON, and never rendered as or conflated with `pass`
- [x] **ENG-15**: Errors map by kind to exit codes (`usage`→64, `input_open`/`input_unsupported`→65, `decode`→66, `internal`→70) with no exceptions crossing the `libmediadiff` boundary
- [x] **ENG-16**: `libmediadiff` writes nothing to stdout and never calls `exit()` — all rendering and process control lives in `cli/`

### Fingerprints & Snapshots

- [ ] **SNAP-01**: `mediadiff snapshot <file>` writes a `*.snap.json` fingerprint containing all measurements plus the envelope (schema/tool versions, decode path, sampling state, input identity with XXH3-128)
- [x] **SNAP-02**: `mediadiff compare <file> <file>.snap.json` accepts a snapshot in place of live media and produces the same findings a live compare would
- [ ] **SNAP-03**: Snapshot output is canonical and git-diffable — registry field order, sorted scopes, one value per line, shortest round-trip float formatting
- [ ] **SNAP-04**: Times are stored as `{num, den, tb}` rationals with the float form as a derived convenience field
- [ ] **SNAP-05**: `compare` warns on tool-version skew and refuses with exit `65` on an incompatible `schema_version` major
- [ ] **SNAP-06**: `mediadiff snapshot f && mediadiff compare f f.snap.json` is clean — enforced as a permanent CI test
- [ ] **SNAP-07**: **[R]** `snapshot` refuses to silently overwrite an existing tracked snapshot in CI (`CI=true` / non-TTY) without an explicit `--force`/`--update` flag, so a misinvoked job cannot launder a regression into the committed baseline (research: FEATURES gap 1 — the convention every comparable snapshot tool enforces)

### Reporting

- [ ] **REPORT-01**: JSON output is schema-validated against `docs/schema/report-1.0.json` in CI and byte-identical across identical runs (timing fields excluded)
- [ ] **REPORT-02**: TTY output groups findings in fixed order (container → video → timeline → audio → content → size → meta), shows only non-pass by default, and is width-aware without wrapping value columns
- [ ] **REPORT-03**: Every gating finding prints the accept / tune / silence triple in TTY output
- [ ] **REPORT-04**: Markdown output renders a summary table plus per-group `<details>`, and folds overflow into "N more findings, see JSON artifact"
- [ ] **REPORT-05**: **[R]** The Markdown cap is enforced as a character budget under GitHub's real 65,536-character comment limit, not an ambiguous "60 KB" byte figure (research: PITFALLS — corrects doc 01 §9)
- [ ] **REPORT-06**: JUnit output emits one `<testcase>` per gating-capable finding, one suite per group, so Jenkins/GitLab show results with zero integration work
- [ ] **REPORT-07**: `mediadiff inspect <file>` renders the complete analysis of a single file across every implemented check family

### Directory Mode

- [ ] **DIR-01**: `mediadiff dir <a> <b>` pairs files by relative path and reports unpaired files as `meta.missing_candidate` (fail) / `meta.extra_candidate` (warn)
- [ ] **DIR-02**: `dir` defaults to header + packet passes and only decodes when `--content` is passed
- [ ] **DIR-03**: `dir` output rolls up per-file summaries, corpus totals, and a worst-N table in TTY, with a `files[]` layer in JSON using the same finding schema
- [ ] **DIR-04**: File processing order is deterministic (sorted relative paths) so reports diff cleanly across runs
- [ ] **DIR-05**: `--threads N` bounds a worker pool across files while analyzer code stays single-file-synchronous
- [ ] **DIR-06**: **[R]** Peak memory per in-flight file is bounded and asserted, since `--threads` is simultaneously the concurrency and the memory knob (research: ARCHITECTURE)

### Probe Layer

- [ ] **PROBE-01**: `DemuxSession` opens any supported input with `AVFMT_FLAG_GENPTS` **off**, a hard wall-clock budget via interrupt callback, and captures libav warnings into fingerprint diagnostics
- [ ] **PROBE-02**: `PacketScan` performs one `av_read_frame` sweep with no decode, recording per-stream `{pts, dts, duration, size, flags, pos}` and byte totals, capping at 5M packets/stream with `partial:true` beyond
- [ ] **PROBE-03**: `ParserScan` extends the same sweep to record per-access-unit `pict_type`, `key_frame`, `repeat_pict`, `field_order`, plus NAL-type sequences for H.264/HEVC, at under 10% overhead over plain PacketScan
- [ ] **PROBE-04**: `bmff_scan` reads MP4/MOV top-level box order and offsets, `ftyp` brands, `mvhd`/`mdhd` timescales, `elst` entries, and `moof`/`sidx` presence without loading payloads
- [ ] **PROBE-05**: `ebml_scan` reads Matroska/WebM element offsets (SeekHead, Info, Tracks, first Cluster, Cues), `TimestampScale`, `Duration` presence, and per-track `CodecDelay`/`SeekPreRoll`
- [ ] **PROBE-06**: `ts_scan` resyncs on 0x47 with 188/192/204 autodetect and extracts per-PID counts, continuity-counter state, PCR values, PAT/PMT parsing with version tracking, and null-packet counts
- [ ] **PROBE-07**: **[R]** `ts_scan` implements the ISO 13818-1 §2.4.3.3 continuity-counter carve-outs explicitly — increment only on payload-carrying packets, one duplicate allowed, `discontinuity_indicator` resets counted separately (research: PITFALLS — hand-rolled CC logic false-alarms without these)
- [ ] **PROBE-08**: Every analyzer declares which passes it needs and the orchestrator runs the union exactly once per file — no analyzer re-reads the file
- [ ] **PROBE-09**: Unparseable structure degrades to `skipped:unparsed_mechanism` with a byte offset in evidence — never a crash, never a silent pass; truncated and garbage inputs exit `65` cleanly
- [ ] **PROBE-10**: **[R]** Packet-interval statistics are computed once as a shared probe-level primitive consumed by both `video.frame_rate.measured` and `timeline.*`, resolving the phase-3-depends-on-phase-4 inversion (research: ARCHITECTURE hazard A)

### Container & Metadata Checks

- [ ] **CONT-01**: Container-agnostic topology checks work: `container.format`, `track_count`, `track_types`, `track_order`, `chapters`
- [ ] **CONT-02**: A cross-container migration demotes cleanly — `container.<fmt>.*` on both sides becomes `skipped:cross_container` and comparison proceeds at the semantic layer
- [ ] **CONT-03**: `meta.tags` compares as a set with a volatile ignore list (`creation_time`, `encoder`, `handler_name`, `encoding_tool`), showing ignored-but-differing values under `-v`
- [ ] **CONT-04**: `meta.tags.language` treats `und` and absent as equal, since that divergence is a muxer artifact and not a regression
- [ ] **CONT-05**: MP4/MOV checks work: `faststart`, `brands`, `fragmentation`, `edit_list`, `timescale`
- [ ] **CONT-06**: Matroska/WebM checks work: `cues_placement`, `codec_delay`, `timestamp_scale`, `duration_element`
- [ ] **CONT-07**: MPEG-TS checks work: `cc_errors`, `pcr_interval`, `psi_interval`, `null_ratio`
- [ ] **CONT-08**: Multi-program TS emits program-scoped measurements paired by `program_number`, with unpaired programs reported as a topology failure
- [ ] **CONT-09**: **[R]** Subtitle and caption track presence is explicitly covered and tested, not merely assumed to fall out of generic stream-presence checks (research: FEATURES gap 4)

### Video Checks

- [ ] **VIDEO-01**: Stream-parameter checks work: `codec`, `profile`, `level`, `resolution`, `sar`/`dar`, `pix_fmt`, `frame_rate.declared`, `frame_rate.measured`, `frame_count`
- [ ] **VIDEO-02**: `video.frame_count` is always counted from the packet/parser scan, never taken from `nb_frames`, so counts are comparable across container types
- [ ] **VIDEO-03**: A `yuvj420p` → `yuv420p` + full-range change produces exactly **one** finding, on `video.color.range`, because pix_fmt range-folding runs before comparison
- [ ] **VIDEO-04**: Container SAR and bitstream VUI SAR conflicts record both values, compare the effective one, and flag the conflict itself as `info`
- [ ] **VIDEO-05**: GOP checks work: `gop.length`, `gop.idr_interval` with open/closed classification via NAL types, `gop.refs`, `frame_types` distribution
- [ ] **VIDEO-06**: `video.interlace` cross-checks declared field order against per-frame parser flags and reports `mixed` with proportions when content is mixed
- [ ] **VIDEO-07**: Colorimetry checks work: `color.range` (fail in every profile, no exceptions), `color.primaries`, `color.transfer`, `color.matrix`, `color.chroma_loc`
- [ ] **VIDEO-08**: A change **to** `unspecified` is reported as a regression (metadata loss), not treated as a wildcard match
- [ ] **VIDEO-09**: HDR checks work: `hdr.mdcv`, `hdr.cll`, `hdr.dovi`, with extraction precedence from stream-level `coded_side_data` then first-frame side data, recording which source was used
- [ ] **VIDEO-10**: MDCV/CLL internal incoherence (HDR metadata with an SDR transfer, or PQ without MDCV) raises a non-gating `info` note even when both files share it
- [ ] **VIDEO-11**: `video.closed_captions` detects A53/CEA-708 presence during the decode pass and reports `skipped:requires_decode` under `--no-content`
- [ ] **VIDEO-12**: A codec with no available parser degrades to `skipped:no_parser` for GOP checks and falls back to keyframe-flag granularity for frame types

### Timeline Checks

- [ ] **TIME-01**: All timeline math runs on `{int64, AVRational}` with `AV_NOPTS_VALUE` as a first-class `absent` state, never coerced
- [ ] **TIME-02**: MPEG-TS 33-bit PTS wraparound is unwrapped correctly, preserving raw values in evidence and distinguishing a wrap from a genuine backward discontinuity
- [ ] **TIME-03**: `timeline.start` and `timeline.duration` work, with the duration triple (container-declared, stream-declared, computed) cross-checked internally and disagreement raising an `info` note
- [ ] **TIME-04**: Structural integrity checks work: `dts_monotonic`, `pts_unique`, `gaps`, `discontinuities`
- [ ] **TIME-05**: `timeline.jitter` and `timeline.vfr_profile` work, with CFR classified as ≥ 99.5% of intervals equal to the mode interval, and jitter reporting `skipped:vfr` on VFR streams
- [ ] **TIME-06**: `timeline.av_offset` reports the signed offset between first audible sample and first visible frame, priming-adjusted
- [ ] **TIME-07**: `timeline.av_drift` implements the normative 32-checkpoint least-squares algorithm and reports rate (ms/min), end delta, and pattern class (`constant-offset` / `linear-drift` / `step` / `irregular`)
- [ ] **TIME-08**: The drift trajectory (all K offsets) is stored in the fingerprint so snapshot comparison retains full fidelity
- [ ] **TIME-09**: When audio priming is unknown, the offset is computed unadjusted and the finding carries `priming: unknown` rather than hiding the uncertainty
- [ ] **TIME-10**: **[R]** Fixtures cover the non-zero-priming path, not only the priming-unknown degrade path, since phase 4 ships before the audio decode path that supplies priming (research: ARCHITECTURE hazard B)
- [ ] **TIME-11**: `timeline.timecode` detects presence and start value from `tmcd` tracks and S12M/GOP timecode, rendered as a SMPTE string including the drop-frame flag

### Audio Checks

- [ ] **AUDIO-01**: Stream-parameter checks work: `codec`, `profile`, `sample_rate`, `sample_fmt`/`bit_depth`, `channels`, `layout`
- [ ] **AUDIO-02**: `audio.layout` distinguishes `5.1` from `5.1(side)` using the modern `AVChannelLayout` API only, and treats loss of layout as a regression
- [ ] **AUDIO-03**: HE-AAC SBR signaling mode (implicit vs explicit) is detected and carried as part of `audio.profile`'s value
- [ ] **AUDIO-04**: `audio.priming` resolves through the precedence chain `initial_padding` → container mechanism (MP4 elst / iTunSMPB / MKV CodecDelay) → `unknown`, and stays stable across container round-trips
- [ ] **AUDIO-05**: `audio.loudness.integrated` uses libebur128 EBU R128 mode-I and matches an `ffmpeg -af ebur128` reference within ±0.1 LU on fixtures
- [ ] **AUDIO-06**: `audio.loudness.true_peak` reports dBTP and fails asymmetrically when the candidate crosses −1.0 dBTP upward from a baseline that was under it
- [ ] **AUDIO-07**: `audio.silence.edges` and `audio.silence.dropouts` detect introduced leading/trailing silence and interior dropouts as spans
- [ ] **AUDIO-08**: `content.audio.sample_hash` chains XXH3-128 over decoded PCM per track and reports the first divergent sample index and time
- [ ] **AUDIO-09**: Hashing automatically prefers class-1 fixed-point decoder siblings (`aac_fixed`, `ac3_fixed`) when available, and `--hash-decoder default` opts out while recording class 2
- [ ] **AUDIO-10**: Loudness, silence detection, and hashing share a single decode sweep per track

### Content, Quality & Size Checks

- [ ] **SIZE-01**: `size.file`, `size.stream_bitrate`, `size.peak_bitrate`, and `size.overhead` work, with peak windowing defined on DTS in ticks and rational bounds for cross-platform identity
- [ ] **CONTENT-01**: `content.video.frame_hash` hashes exactly `bytes_per_row(width) × height` per plane — never `linesize` — chained with PTS, pix_fmt and dimensions
- [ ] **CONTENT-02**: A hash mismatch reports the first divergent frame (index + PTS), contiguous divergent ranges merged at 1-frame gaps, and the total differing count
- [ ] **CONTENT-03**: `--sample N` marks the fingerprint `sampled:N` and only equal-N fingerprints compare; mismatched sampling reports `skipped:sampling_mismatch`
- [ ] **CONTENT-04**: `content.video.perceptual` computes SSIM on downscaled luma (SWS_AREA to width 128, 8×8 window) with pinned swscale flags recorded as a precondition, reporting min, mean, first frame below threshold, and a worst-10 list
- [ ] **CONTENT-05**: When frame counts differ, perceptual comparison pairs the overlapping prefix and notes truncation in evidence rather than silently misaligning
- [ ] **CONTENT-06**: `content.video.frozen_runs` and `content.video.black_runs` detect spans, with black detection normalized by color range and bit depth so a range flip does not false-alarm the detector
- [ ] **CONTENT-07**: Hashing, perceptual scoring, and the frozen/black detectors all run inside one decode sweep
- [ ] **CONTENT-08**: `quality.psnr` and `quality.ssim` compute in-tree at native resolution, reporting min and mean
- [ ] **CONTENT-09**: `quality.vmaf` runs behind `MEDIADIFF_WITH_VMAF` with model `vmaf_v0.6.1` pinned and recorded in the fingerprint, reporting harmonic mean and min, and refusing `--sample` as `skipped:sampling_conflict`
- [ ] **CONTENT-10**: `quality.*` against a snapshot reports `skipped:requires_media` while still showing stored scores for trend context
- [ ] **CONTENT-11**: `compare` decodes baseline and candidate in lockstep with one frame in flight per side — two full decoded sequences are never resident

### Trust & Determinism Guarantees

- [ ] **TRUST-01**: Every fingerprint records, per hashed stream, the decoder name, determinism class, flags, and (class 2) a path signature
- [ ] **TRUST-02**: A class-2 hash comparison across differing decode paths reports `skipped:hash_incomparable` with a remediation hint — never a fabricated pass or fail
- [ ] **TRUST-03**: **[R]** The class-2 path signature includes a toolchain component (libavcodec/libavformat/swscale versions at minimum), not only device/driver, so a dependency bump cannot silently produce a hash mismatch (research: PITFALLS — highest-value gap found; doc 01 §7 specifies driver only)
- [ ] **TRUST-04**: **[R]** `±tol` perceptual and `quality.*` checks carry the same path-signature preconditions as `hash` checks, since SSIM/VMAF are equally fragile to decode and scaler path drift (research: PITFALLS — FFmpeg 9.0's swscale float→rational rewrite makes this concrete, and UC2 is an FFmpeg major-version migration)
- [x] **TRUST-05**: Running `compare` twice on the same inputs produces byte-identical `--json` output
- [ ] **TRUST-06**: Encoding a fixture twice with identical settings and comparing under `sw-encoder` produces a clean result — wired into CI as a release blocker
- [ ] **TRUST-07**: Decoding a fixture at 1, 4, and 16 threads produces identical hash chains
- [ ] **TRUST-08**: **[R]** A cross-release idempotence test compares the current build against a snapshot taken by the previous release, catching toolchain-drift false positives that same-build compare-twice cannot (research: PITFALLS)
- [ ] **TRUST-09**: `ts_scan` output is cross-checked against TSDuck's analysis of the same fixtures via a manual test jig, without linking TSDuck

### Documentation & Explainability

- [x] **DOC-01**: Every registered check has a `docs/checks/<id>.md` file, enforced by the build rather than by review discipline
- [x] **DOC-02**: Every check's `--explain` text states what the check measures, why it matters, and how to accept, tune, or silence it
- [ ] **DOC-03**: Every check is demonstrated by at least one fixture pair that triggers it and one that comes back clean
- [ ] **DOC-04**: Timeline fixtures assert the *no-others* clause — the intended finding fires and nothing else does

### Performance

- [ ] **PERF-01**: Metadata plus timeline analysis of the 10-minute 1080p reference file completes in ≤ 3 s
- [ ] **PERF-02**: A full content pass runs at ≥ 4× realtime with software decode
- [ ] **PERF-03**: The parser pass adds < 10% over plain PacketScan, and full timeline analysis adds < 15%
- [ ] **PERF-04**: An audio sweep of the 10-minute reference stereo AAC completes in < 4 s
- [ ] **PERF-05**: Performance targets are measured in CI on the reference file with regression tracking over time

## v2 Requirements

Acknowledged but deferred. Tracked, not in the current roadmap.

### Acceleration

- **HW-01**: `--hwaccel cuda` decodes via NVDEC with a `cuda:<gpu>:<driver>` path signature and auto-degrading hash comparison
- **HW-02**: Content pass reaches ≥ 20× realtime with `--hwaccel cuda` on the perceptual path
- **HW-03**: NVDEC-vs-software fixture pair proves `skipped:hash_incomparable` on a CUDA-equipped CI runner

### Extended Analysis

- **EXT-01**: SEI ITU-T T.35 scan in ParserScan lifts the decode requirement for `video.closed_captions`
- **EXT-02**: PCR accuracy and jitter measured against an ideal clock (deferred until it can meet the idempotence guarantee)
- **EXT-03**: Per-frame Dolby Vision RPU diffing beyond the configuration record
- **EXT-04**: `transform` profile gains `expect.frame_rate` alongside `expect.resolution`
- **EXT-05**: Lightweight audio priming extraction at probe level, ahead of the full audio decode path — **[R]** requires a feasibility spike first (research: ARCHITECTURE open question)

### Distribution

- **DIST-01**: Universal macOS binary combining `arm64` and `x86_64`
- **DIST-02**: Language bindings over the embeddable `libmediadiff` engine

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Encoding, muxing, or any mutation of input files | mediadiff observes builds, it does not own artifact correctness; auto-correction (as shipped by Vidchecker) destroys the "did this build change" signal — a permanent anti-feature, not a "not yet" |
| GPL FFmpeg components | The LGPL decode-only subset is a distribution requirement; enabling `gpl` changes the license story of a shipped binary |
| Broadcast delivery-spec compliance (DPP, IMF/RDD-59, Netflix, CableLabs templates) | Different product with a different pass condition — fixed external spec vs baseline-relative delta — and a large, continuously-changing surface against entrenched incumbents. If ever wanted, it belongs in a separate consumer of the fingerprint format, not a mediadiff profile |
| Hosted dashboard, account, or SaaS backend | A CI gate requiring network access or a subscription on every commit is at odds with "one static binary, zero setup," and conflicts with offline reproducibility |
| Dynamic linking against a system FFmpeg | Reintroduces exactly the decode nondeterminism the class system exists to prevent, and breaks zero-setup distribution |
| GitHub Actions native `::error`/`::warning` annotations as the primary CI surface | GitHub silently caps annotations at 10 errors + 10 warnings per step and 50 per run; a corpus `dir` run would lose most of its output. Markdown + JUnit carry the full result |
| SARIF output | A narrow static-analysis convention with little overlap with this domain's actual CI consumers; JUnit and Markdown already cover the ground |
| Visual diff artifact / heatmap for perceptual findings | **[R]** Intentional CLI-only boundary consistent with the no-server stance: mediadiff reports evidence coordinates (frame index, PTS, score), and users needing visual triage decode the referenced frame themselves (research: FEATURES gap 3 — recorded as a decision so it does not read as an oversight) |
| TSDuck as a linked dependency | Very large surface for ~300 lines of need; retained as the reference implementation tests cross-check against |
| C++20 modules, `std::format`, coroutines, `std::jthread`/`std::stop_token` | Toolchain parity across GCC 12 / AppleClang / MSVC v143 is worth more; **[R]** the coroutine and jthread exclusions come from research, as libc++ trailed on cooperative-cancellation primitives (research: STACK) |
| Universal macOS binary in v1 | Per-arch builds only; deferred to v2 |
| Committed media binaries | Every fixture is synthesized deterministically by `scripts/gen_corpus`; nothing binary enters git |
| Conan 2, system packages, or FetchContent for FFmpeg | Evaluated and rejected on reproducibility, Windows support, and the static-binary requirement |
| CUDA-enabled libvmaf as a vcpkg feature | Documented as a manual system build instead |
| Check ID renames without an alias | IDs are forever post-v1; a rename breaks every user's config silently |

## Traceability

Which phases cover which requirements. Every v1 requirement maps to exactly one phase.

Phase numbering note: ROADMAP phases are 1-7; the design docs number their phases 0-6.
ROADMAP Phase N = design-doc phase N-1 = `claude_docs/0(N-1)-*.md`.

| Requirement | Phase | Status |
|-------------|-------|--------|
| BUILD-01 | Phase 1 | Complete |
| BUILD-02 | Phase 1 | Complete |
| BUILD-03 | Phase 1 | Complete |
| BUILD-04 | Phase 1 | Complete |
| BUILD-05 | Phase 1 | Complete |
| BUILD-06 | Phase 1 | Complete |
| BUILD-07 | Phase 1 | Complete |
| BUILD-08 | Phase 1 | Complete |
| BUILD-09 | Phase 1 | Complete |
| BUILD-10 | Phase 1 | Complete |
| CLI-01 | Phase 2 | Pending |
| CLI-02 | Phase 2 | Pending |
| CLI-03 | Phase 2 | Pending |
| CLI-04 | Phase 2 | Pending |
| CLI-05 | Phase 1 | Complete |
| CLI-06 | Phase 2 | Pending |
| CLI-07 | Phase 2 | Pending |
| CLI-08 | Phase 2 | Pending |
| CLI-09 | Phase 1 | Complete |
| CLI-10 | Phase 2 | Pending |
| ENG-01 | Phase 2 | Complete |
| ENG-02 | Phase 2 | Complete |
| ENG-03 | Phase 2 | Complete |
| ENG-04 | Phase 2 | Pending |
| ENG-05 | Phase 2 | Pending |
| ENG-06 | Phase 2 | Pending |
| ENG-07 | Phase 2 | Pending |
| ENG-08 | Phase 2 | Pending |
| ENG-09 | Phase 2 | Pending |
| ENG-10 | Phase 2 | Pending |
| ENG-11 | Phase 2 | Pending |
| ENG-12 | Phase 2 | Pending |
| ENG-13 | Phase 2 | Pending |
| ENG-14 | Phase 2 | Pending |
| ENG-15 | Phase 2 | Complete |
| ENG-16 | Phase 2 | Complete |
| SNAP-01 | Phase 2 | Pending |
| SNAP-02 | Phase 2 | Complete |
| SNAP-03 | Phase 2 | Pending |
| SNAP-04 | Phase 2 | Pending |
| SNAP-05 | Phase 2 | Pending |
| SNAP-06 | Phase 2 | Pending |
| SNAP-07 | Phase 2 | Pending |
| REPORT-01 | Phase 2 | Pending |
| REPORT-02 | Phase 2 | Pending |
| REPORT-03 | Phase 2 | Pending |
| REPORT-04 | Phase 2 | Pending |
| REPORT-05 | Phase 2 | Pending |
| REPORT-06 | Phase 2 | Pending |
| REPORT-07 | Phase 2 | Pending |
| DIR-01 | Phase 2 | Pending |
| DIR-02 | Phase 2 | Pending |
| DIR-03 | Phase 2 | Pending |
| DIR-04 | Phase 2 | Pending |
| DIR-05 | Phase 2 | Pending |
| DIR-06 | Phase 3 | Pending |
| PROBE-01 | Phase 3 | Pending |
| PROBE-02 | Phase 3 | Pending |
| PROBE-03 | Phase 4 | Pending |
| PROBE-04 | Phase 3 | Pending |
| PROBE-05 | Phase 3 | Pending |
| PROBE-06 | Phase 3 | Pending |
| PROBE-07 | Phase 3 | Pending |
| PROBE-08 | Phase 3 | Pending |
| PROBE-09 | Phase 3 | Pending |
| PROBE-10 | Phase 3 | Pending |
| CONT-01 | Phase 3 | Pending |
| CONT-02 | Phase 3 | Pending |
| CONT-03 | Phase 3 | Pending |
| CONT-04 | Phase 3 | Pending |
| CONT-05 | Phase 3 | Pending |
| CONT-06 | Phase 3 | Pending |
| CONT-07 | Phase 3 | Pending |
| CONT-08 | Phase 3 | Pending |
| CONT-09 | Phase 3 | Pending |
| VIDEO-01 | Phase 4 | Pending |
| VIDEO-02 | Phase 4 | Pending |
| VIDEO-03 | Phase 4 | Pending |
| VIDEO-04 | Phase 4 | Pending |
| VIDEO-05 | Phase 4 | Pending |
| VIDEO-06 | Phase 4 | Pending |
| VIDEO-07 | Phase 4 | Pending |
| VIDEO-08 | Phase 4 | Pending |
| VIDEO-09 | Phase 4 | Pending |
| VIDEO-10 | Phase 4 | Pending |
| VIDEO-11 | Phase 7 | Pending |
| VIDEO-12 | Phase 4 | Pending |
| TIME-01 | Phase 5 | Pending |
| TIME-02 | Phase 5 | Pending |
| TIME-03 | Phase 5 | Pending |
| TIME-04 | Phase 5 | Pending |
| TIME-05 | Phase 5 | Pending |
| TIME-06 | Phase 5 | Pending |
| TIME-07 | Phase 5 | Pending |
| TIME-08 | Phase 5 | Pending |
| TIME-09 | Phase 5 | Pending |
| TIME-10 | Phase 5 | Pending |
| TIME-11 | Phase 5 | Pending |
| AUDIO-01 | Phase 6 | Pending |
| AUDIO-02 | Phase 6 | Pending |
| AUDIO-03 | Phase 6 | Pending |
| AUDIO-04 | Phase 6 | Pending |
| AUDIO-05 | Phase 6 | Pending |
| AUDIO-06 | Phase 6 | Pending |
| AUDIO-07 | Phase 6 | Pending |
| AUDIO-08 | Phase 6 | Pending |
| AUDIO-09 | Phase 6 | Pending |
| AUDIO-10 | Phase 6 | Pending |
| SIZE-01 | Phase 3 | Pending |
| CONTENT-01 | Phase 7 | Pending |
| CONTENT-02 | Phase 7 | Pending |
| CONTENT-03 | Phase 7 | Pending |
| CONTENT-04 | Phase 7 | Pending |
| CONTENT-05 | Phase 7 | Pending |
| CONTENT-06 | Phase 7 | Pending |
| CONTENT-07 | Phase 7 | Pending |
| CONTENT-08 | Phase 7 | Pending |
| CONTENT-09 | Phase 7 | Pending |
| CONTENT-10 | Phase 7 | Pending |
| CONTENT-11 | Phase 7 | Pending |
| TRUST-01 | Phase 6 | Pending |
| TRUST-02 | Phase 6 | Pending |
| TRUST-03 | Phase 2 | Pending |
| TRUST-04 | Phase 7 | Pending |
| TRUST-05 | Phase 2 | Complete |
| TRUST-06 | Phase 3 | Pending |
| TRUST-07 | Phase 7 | Pending |
| TRUST-08 | Phase 2 | Pending |
| TRUST-09 | Phase 3 | Pending |
| DOC-01 | Phase 2 | Complete |
| DOC-02 | Phase 2 | Complete |
| DOC-03 | Phase 3 | Pending |
| DOC-04 | Phase 5 | Pending |
| PERF-01 | Phase 5 | Pending |
| PERF-02 | Phase 7 | Pending |
| PERF-03 | Phase 5 | Pending |
| PERF-04 | Phase 6 | Pending |
| PERF-05 | Phase 5 | Pending |

**Coverage:**

- v1 requirements: 138 total
- Mapped to phases: 138
- Unmapped: 0

| Phase | Name | Requirements |
|-------|------|--------------|
| 1 | Foundation & Toolchain | 12 |
| 2 | Core Engine | 48 |
| 3 | Probe Layer, Container & Size | 23 |
| 4 | Video Analysis | 12 |
| 5 | Timeline Analysis | 15 |
| 6 | Audio Analysis | 13 |
| 7 | Content & Quality | 15 |
| **Total** | | **138** |

Requirements placed outside the phase their ID prefix suggests (SIZE-01, PROBE-03, PROBE-10,
DIR-06, VIDEO-11, all TRUST-*, all DOC-*, all PERF-*) are justified individually in
`.planning/ROADMAP.md` under "Cross-cutting requirement placements".

---
*Requirements defined: 2026-08-12*
*Last updated: 2026-08-12 after roadmap creation (traceability populated)*
