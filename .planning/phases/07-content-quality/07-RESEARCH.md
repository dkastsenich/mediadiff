# Phase 7: Content & Quality - Research

**Researched:** 2026-09-30
**Domain:** libav software video decode (one fused sweep), frame-exact hashing, integer SSIM/PSNR, libvmaf, lockstep two-sided decode, process-level watchdog, cross-architecture determinism proofs
**Confidence:** HIGH on decode-path, determinism and performance findings (all measured this session against the linked FFmpeg 8.1 static libs, the pinned 9.0.1 generator, and real CI logs); MEDIUM on cross-architecture claims (qemu-user evidence, not a CI run); LOW only where tagged `[ASSUMED]`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

### Two-file metrics and snapshots (perceptual, psnr, ssim, vmaf)

**Found during this discussion.** Doc 06 §3 contradicts itself. It says "both files are scored
against the same reference (the baseline decoded stream)", so a one-file snapshot can never hold a
two-file score, yet the same passage promises that "scores stored in snapshots are still shown for
trend context". Nothing in the engine models a two-file metric today: `SkipReason::requires_media`
exists (`src/core/model.h:49`) with no producer, and `compare` fingerprints its two inputs one after
the other (`src/cli/commands/compare.cpp:211,217`).

- **D-01: Two-file scores exist only in a live media-vs-media compare, as ordinary measurements on both fingerprints.** The baseline records its self-score (the baseline scored against itself) and the candidate records its score against the baseline, with the baseline's input identity (XXH3-128) in evidence; the existing `±tol` engine then compares the two, which is doc 06 §3's own "candidate-vs-baseline score delta" model. Against any snapshot, on either side, `content.video.perceptual` and every `quality.*` check report `skipped:requires_media`, and `snapshot` never produces a two-file score. CONTENT-10's and ROADMAP SC4's "while still showing stored scores for trend context" is amended in the open, since a one-file snapshot never holds one in v1, recorded against doc 06 §3 the way 06-10 amended doc 01 §11 and never as a silent divergence. **Accepted cost, stated in each check's `--explain`:** under `hw-encoder`, where perceptual is the gating content check, a committed-snapshot baseline turns that gate into `skipped:requires_media`, so that workflow must keep baseline media. SNAP-06 (`snapshot f && compare f f.snap.json` clean) and TRUST-08 are unaffected because these checks skip against snapshots. Rejected: a `snapshot --against <baseline>` mode storing candidate scores (new CLI surface whose only use is trend display) and a per-frame 128-wide luma thumbnail sidecar (about 9 KB per frame of binary beside the text snapshot, against UC7's git-diffable baselines). — **Reversibility:** costly — `requires_media` behaviour and the self-score/candidate-score shape reach the `--json` contract and every two-file check's `--explain` text; adding snapshot-borne scores later is additive.

- **D-02: Frames pair by presentation time, not by decode index.** Each side's timestamps are measured from its own first frame and matched within half a frame interval, in exact rational math with no floating point. A dropped or duplicated frame leaves one frame unpaired, counted in evidence, instead of shifting every later pair; a timebase change (MP4 to Matroska's 1 ms rounding, Phase 5 D-05) cannot misalign; a 60 to 30 fps pair scores only the frames that coincide. When either side lacks usable timestamps, pairing falls back to decode index and the fallback is recorded in evidence. CONTENT-05's "pairs the overlapping prefix" is amended in the open to "pairs the overlapping time range". Rejected: doc 06 §2.2's index pairing, under which one mid-file drop shifts every later pair and makes the rest of the file's scores meaningless. — **Reversibility:** costly — the pairing rule decides every two-file value and its evidence shape.

- **D-03: Perceptual gates on its worst frame; the quality checks gate on the sequence mean.** `content.video.perceptual`'s compared value is the minimum per-frame SSIM, so one frame below the 0.985 threshold blocks, which is what doc 06 §2.2's "first frame below threshold" implies; the mean, the first frame below threshold (index and PTS) and the worst-10 list ride in evidence. `quality.psnr` and `quality.ssim` gate on the arithmetic mean and `quality.vmaf` on the harmonic mean (doc 06 §3's "vmaf_drop: 0.5" example), with minima in evidence. Rejected: worst frame everywhere (one hard frame, such as a scene cut or flash, fails PSNR/VMAF on a healthy encode, which is the P0 class) and mean everywhere (a 5-frame glitch in a 10-minute file vanishes, and on a decode that cannot hash nothing else catches it). — **Reversibility:** costly — the gated aggregate is the compared value in every report and part of each check's `--explain` contract.

- **D-04: TRUST-04's precondition on two-file checks compares build-level paths, never codecs.** Every perceptual and quality measurement records, for both sides, the scaler setup (algorithm, flags, target geometry, swscale version) and the build's decode-path signature (Phase 6 D-05: libav versions, triplet, runtime SIMD flags). A mismatch produces a skip with a remediation hint, never a fabricated fail. Under D-01 both sides are always produced by one build in one run, so the two are identical by construction today; the guard bites only when a future hwaccel or scaler option puts the sides on different paths. Different decoders on the two sides, such as an H.264 baseline against an HEVC candidate (the codec-migration case), still score. ROADMAP SC3's "refuse to compare across differing decode/scaler paths" is recorded as meaning build or device paths, not codecs. PROJECT.md's FFmpeg 8.1 pin anticipated TRUST-04 guarding stored SSIM baselines across the 9.0 swscale rewrite; under D-01 no two-file score is ever stored, so that bump cannot move a compared value at all. `src/compare/tol.cpp` carries no preconditions today (only `src/compare/hash.cpp:159`'s `kPreconditionKeys` does), so this is new plumbing, not wiring. — **Reversibility:** reversible — a precondition table; widening it later only turns comparisons into skips.

### What the video frame hash covers

- **D-05: A frame's hash covers its cropped display pixels, its pixel format after yuvj range folding, and its dimensions, but not its timestamp.** Per plane, the hash covers exactly `bytes_per_row(width) × height` of the cropped display rectangle, never `linesize` and never encoder padding rows (PITFALLS Pitfall 4). The pixel format is folded exactly as `video.pix_fmt` and `video.color.range` fold it (VIDEO-03: one intent, one finding) and recorded by name. The presentation timestamp is stored per frame beside the digest and used only to locate divergence. Timing stays owned by `timeline.*`, so an untouched MP4 to MKV or TS remux hashes equal under `remux` (UC5) and a retimed stream produces one finding, not two. This amends doc 06 §2.1's "‖ presentation PTS (ticks) ‖ pix_fmt id" in the open. Rejected: a normalized timestamp in the hash (it survives the TS muxer's 1.4 s shift but not Matroska's 1 ms rounding) and doc 06's raw ticks (every container change fails every frame on identical pixels). — **Reversibility:** costly — the hashed basis is the compared value in every stored fingerprint, so changing it invalidates every video hash ever written.

- **D-06: Every decoded frame is hashed, including frames the container marks decode-but-don't-show.** This carries Phase 6 D-01 to video: the hash covers every frame decoded from every packet the demuxer delivers, ignoring edit-list discard, so it is the same whether or not the target container can express the trim (MPEG-TS cannot), and an untouched remux cannot diverge at frame 0 with every later frame shifted. The trim itself is owned by `timeline.start`, `timeline.duration` and `container.mp4.edit_list`, which is Phase 5 D-02's mechanism-versus-effect layering. The researcher confirms how the pinned FFmpeg 8.1 surfaces edit-list-discarded video frames (packet `AV_PKT_FLAG_DISCARD` and frame `AV_FRAME_FLAG_DISCARD`) and how to keep them. — **Reversibility:** costly — same reason as D-05.

- **D-07: The divergence report lines frames up by presentation time.** It uses the same matching as D-02 (from each side's first frame, within half an interval, falling back to index when timestamps are unusable). A dropped frame reports as "frame N missing from candidate" and later frames still line up, so only genuinely changed frames count as differing; ranges merge at 1-frame gaps and the total is reported (CONTENT-02). Both sides' per-frame digests and timestamps are stored, so a live compare and a snapshot compare produce identical evidence and Phase 6 D-03's snapshot-equivalence constraint holds by construction. The verdict does not change, only the report. Rejected: doc 06 §2.1's index alignment, under which one dropped frame at 813 reports "frames 813–14999 differ". — **Reversibility:** reversible — evidence shape only; the stored arrays support either alignment.

- **D-08: `--sample N` is an honest stride.** Every frame is still decoded (inter-coded video cannot skip frames), and frozen/black detection, closed captions and the HDR first-frame arm still see every frame, so their values never depend on sampling (Phase 6 D-12's rule). `--sample N` hashes and stores every Nth frame and scores every Nth pair. That buys smaller snapshots and cheaper native-resolution PSNR/SSIM, not a faster decode, and `--help` and `--explain` say so plainly. CONTENT-03's `sampled:N` marking and `skipped:sampling_mismatch` work as specified; `quality.vmaf` refuses sampling with `skipped:sampling_conflict` (CONTENT-09). `sampling_conflict` is not yet a `SkipReason` (`src/core/model.h:39`) nor in `docs/schema/report-1.0.json`, so it is an additive enum value. Rejected: keyframe-only decode (a real speedup, but the sampled set then depends on each file's GOP, so encodes with different GOPs cannot compare hashes, and detectors and captions cannot run while sampling). — **Reversibility:** costly — `sampled:N` and the stride semantics enter the snapshot envelope and CONTENT-03's contract.

### Which decoders hash by default (determinism classes)

**Found during this discussion.** Doc 01 §7 lists H.264, HEVC, VP9, AV1 and MPEG-2 as class 1
"with `AV_CODEC_FLAG_BITEXACT`" and treats anything unlisted as class 3, meaning no hashing. The
corpus is mostly MPEG-4 Part 2: 104 of the 126 video-encoder invocations in
`scripts/gen_corpus.sh` are `mpeg4`, with 18 `mpeg2video`, 3 `mjpeg` and 1 `huffyuv`, and doc 01
omits all but MPEG-2. On x86, FFmpeg's default IDCT selection for MPEG-2/MPEG-4 can pick SIMD
paths that are not guaranteed bit-identical with other architectures. GitHub's x64 runners also
vary in SIMD support, so a class-2 hash compared against a committed snapshot can skip from one
run to the next (Phase 6 D-05's signature includes `av_get_cpu_flags()`). The audio precedent in
`src/probe/audio_decode.cpp:320` keeps doc-listed `ac3_fixed` at class 1 while its cross-arch
status is unmeasured (`WINDOWS.md` #39), which is the risk D-09 declines for video.

- **D-09: A video decoder is class 1 only after a cross-architecture proof in CI; until then every software video decoder is class 2.** Class 2 means hashes compare within one machine class (Phase 6 D-05's signature) and report `skipped:hash_incomparable` with a hint across classes, never a fabricated fail. Promotion uses `aac_fixed`'s method (Phase 6 D-11: a snapshot taken on the designated leg is compared on every other leg, with the fixture's own XXH3 asserted first), and any decoder setting the proof needed, such as a pinned IDCT, is recorded in the fingerprint's per-stream decoder flags (TRUST-01). This applies to doc-listed codecs and to the corpus codecs alike; doc 01 §7's list becomes the order in which proofs are attempted, not a grant. Class 3 (hashing disabled) is reserved for a decoder that is not deterministic even on one machine, single-threaded. This amends doc 01 §7 in the open and extends PROJECT.md's locked "decoder determinism classes are proven, not assumed" to video. Rejected: trusting the doc list from day one, whether with unlisted codecs at class 3 (the 104 MPEG-4 Part 2 invocations would hash nothing until proven) or at class 2; either way a wrong class-1 claim is a fail-severity false positive on every cross-machine compare. — **Reversibility:** costly — the class and decoder flags are written into every fingerprint; a later promotion is additive, while a demotion turns stored comparisons into skips.

- **D-10: Proof streams for codecs the LGPL corpus cannot encode on every leg are encoded once on the designated leg and handed to the other legs as CI artifacts.** The designated `x64-linux` leg's pinned FFmpeg carries `libx264` and `libx265` (plus BSD VP9/AV1 encoders where present). It encodes the proof streams, and every other leg decodes those exact files and must reproduce the designated leg's frame hashes. The generator is never linked into or shipped with mediadiff (AR-03, `03-SECURITY.md`), nothing GPL enters the binary, nothing binary enters git, and the fixtures are still synthesized (BUILD-08). This lifts Phase 4 D-01's never-libx264 corpus convention for proof inputs only; the regular corpus stays LGPL-encodable on every leg. Accepted cost: these proofs run only in CI, through cross-job artifact plumbing, not in a local `ctest`. Rejected: mirroring public conformance streams as release assets (not synthesized, and each codec needs its own licence check) and leaving H.264/HEVC unproven in v1 (the most common codec would then hash only within one machine class). — **Reversibility:** reversible — a CI proof mechanism; what enters fingerprints is the class it establishes (D-09).

- **D-11: A decoder whose output changes with thread count is pinned to one thread.** TRUST-07's suite decodes fixtures at 1, 4 and 16 threads and asserts identical chains. A decoder that fails keeps hashing but always decodes single-threaded, with the setting recorded in the fingerprint's decoder flags, and its slowdown is measured against PERF-02 and reported, not hidden. Rejected: dropping it to class 3 (it keeps its speed but loses frame-exact comparison for that codec). — **Reversibility:** reversible — a per-decoder setting, recorded in flags.

### Decode watchdog (T-06-34, AR-6-05, `WINDOWS.md` #43)

Carried from Phase 6's security acceptance: one watchdog shared by the audio sweep and the video
decode, and a trip surfaces as a could-not-run Error and an exit, never as a changed value (06-18
/ CR-05's determinism rule). libav consults `AVIOInterruptCB` only for I/O, and the open budget is
disarmed right after open (`src/probe/demux_session.cpp:250`), so nothing can interrupt a call from
inside. The watchdog is therefore a separate thread in `cli/` that notices a call has stopped
returning.

- **D-12: The watchdog trips when any single libav call after open runs longer than a fixed limit.** It covers every call in the sweep (packet reads, parser calls, and audio and video decoder send/receive), not only decode calls. The limit is a fixed named constant (Phase 5 D-08), generous enough for valgrind's instruction-count runs and slow runners: minutes, not seconds, with the value set by the planner. A healthy decode of any length never trips, and machine speed cannot turn a good file into a could-not-run. Rejected: a configurable limit (one more knob, and a trip never changes a value either way) and a whole-file budget scaled from duration (it ties the outcome to runner load, so a loaded runner could report could-not-run on a healthy file). — **Reversibility:** reversible — a constant and a monitoring thread.

- **D-13: In `dir`, a hung file is abandoned and reported, and the rest of the corpus finishes.** The hung file becomes a could-not-run error for that file, following `dir`'s existing rule that one file's failure never discards another file's result (`src/cli/commands/dir.cpp:466`). The other files finish on the remaining workers, the report is written, and the process exits 66 without joining the stuck thread. Single-file `compare`, `snapshot` and `inspect` exit 66 at once. If the stuck thread holds a lock other workers need, their calls stall and trip in turn, which is reported, never silent. Rejected: ending the whole run on the first trip (it discards every other file's result, the opposite of `dir`'s rule) and a subprocess per file (the most robust option, but process spawning, fingerprint IPC and Windows UTF-8 argv on three OSes would take a large share of the phase). — **Reversibility:** reversible — CLI orchestration; subprocess isolation can be added later.

### Claude's Discretion

These defaults were stated during the discussion and accepted. The planner may refine them but
must not reverse them silently.

**Two-file metrics**
- `quality.*` checks are switched on only by doc 00 §3.1's `--psnr`, `--ssim` and `--vmaf` flags,
  with no config key in v1. When not requested, each reports a new `skipped:not_requested` reason
  (additive to `SkipReason` and the report schema). `--vmaf` on a build without
  `MEDIADIFF_WITH_VMAF` is a usage error (exit 64) that names the build option, never a silent skip.
  `quality.vmaf` stays registered on every build, so configs and snapshots that name it never break.
- `quality.*` needs equal display dimensions on both sides (native resolution, doc 06 §3) and
  otherwise skips with a reason. `content.video.perceptual` works across resolutions (both sides
  downscaled to width 128, height by aspect, rounded to even) when the aspect ratio matches, which
  is the `transform` profile's AI-upscaler case (UC6); otherwise it skips with a reason.
- How the baseline self-score is obtained: computed by scoring the baseline against itself, or
  taken as the metric's defined maximum. PSNR of identical frames is infinite, so the planner
  defines a documented cap or an explicit identical state that keeps the `±tol` delta well-defined;
  VMAF's score for identical input under `vmaf_v0.6.1` must be measured, not assumed.
- How lockstep is driven (two threads with a one-frame handoff, or a pull-style decoder), without
  coroutines or `std::jthread` (REQUIREMENTS out-of-scope table). The invariants: one sweep per side
  (no analyzer re-reads the file; `read_frame_call_count`, `src/probe/packet_scan.h:282`), one frame
  in flight per side, and never two decoded sequences resident (CONTENT-11). `dir --content` runs
  each pair in lockstep inside the existing per-in-flight-file memory budget (DIR-06).
- Bit-depth normalization before perceptual and quality scoring, so an 8-bit baseline against a
  10-bit candidate still scores. SSIM/PSNR arithmetic stays integer or fixed-point where possible so
  `--json` remains byte-identical across identical runs (TRUST-05); dB appears only at the report
  layer (doc 06 §3).
- The skip reason for a D-04 precondition mismatch (reuse an existing one or add one), and how
  `inspect` reports two-file checks for a single file, both settled at the roster checkpoint.

**Frame hash**
- Per-frame storage follows Phase 6 D-04: one hex digest per line in `HashChain::block_digests`
  (`src/core/value.h:81`) with `element_stride` of one frame, plus a per-frame timestamp array for
  D-07's locator. Truncating digests below 128 bits is the planner's call, as it was in Phase 6.
- High-bit-depth frames hash their native sample bytes.
- The video hash's precondition evidence mirrors `content.audio.sample_hash`'s keys
  (`decode_path_class`, `sampling_state`, `normalization`;
  `src/analyzers/content/sample_hash.cpp:175-190`), so `src/compare/hash.cpp`'s table applies
  unchanged.

**Decoders**
- Proofs are attempted for every decoder the designated leg can encode a fixture for: the doc list
  plus MPEG-4 Part 2, MJPEG and HuffYUV. A failed proof stays class 2 with its evidence recorded,
  following the `mp3`/`mp2` precedent (06-13).
- `--hash-decoder` stays audio-only. Video always uses FFmpeg's default software decoder for the
  codec, with `AV_CODEC_FLAG_BITEXACT` set (doc 06 §1).
- `--threads` keeps meaning `dir`'s worker-pool size (`src/cli/commands/dir.cpp:148`). Decoder
  threading stays automatic by default, with a control that TRUST-07's 1/4/16 test can reach (the
  mechanism is the planner's call). Doc 06 §1's `thread_count = --threads` is amended accordingly.
- Recoverable video decode errors count in `meta.decode_errors` exactly as audio's do (Phase 6
  D-09); only a wholly undecodable video stream marks the fingerprint partial and exits 66.

**Watchdog**
- A trip exits 66. The JSON carries an error entry naming the file, the stream and the last
  position reached, marked `partial`, with no measurements from that file, because another thread
  still owns its half-built fingerprint. That envelope meets CLI-07's "partial JSON is still
  emitted".
- The watchdog lives in `cli/`, and `libmediadiff` only reports progress through a heartbeat, so
  the library still never calls `exit()` (ENG-16).
- The trip path is proven by a test that simulates a stalled call, so the gate cannot pass without
  ever firing.
- `--probe-timeout`'s help text calls it a "per-file wall-clock probe budget" (`src/cli/options.cpp:262`),
  but it bounds only the open. The watchdog plan should make `--help` distinguish the two.

**Carried forward, not re-discussed**
- The check-ID roster is approved at a roster checkpoint in the phase's first plan, as in phases 3
  to 6. New IDs this phase: `content.video.frame_hash`, `content.video.perceptual`,
  `content.video.frozen_runs`, `content.video.black_runs`, `quality.psnr`, `quality.ssim`,
  `quality.vmaf`, and `video.closed_captions`. `video.closed_captions` is **not yet registered**:
  `src/core/checks.def` has 91 IDs and none of these. ROADMAP's cross-cutting table says Phase 4
  registered it, but Phase 4's roster (`04-CHECK-ROSTER.md:158`) held it for Phase 7.
- Frozen and black detectors follow doc 06 §2.3, with thresholds as fixed named constants (Phase 5
  D-08) and black detection normalized by range and bit depth (CONTENT-06). The researcher must
  measure, on real fixtures, whether frozen runs should use exact frame-hash equality or the
  near-identical perceptual threshold, and whether detected spans stay stable across GOP boundaries
  on lossy re-encodes (an I-frame refresh can break an exact-equality run into pieces). A detector
  whose spans move with GOP structure is a false-positive source to raise, not to tolerate.
- `video.closed_captions` uses the `presence` semantic from doc 03: A53/CEA-708 detected through
  `AV_FRAME_DATA_A53_CC` during the decode pass, and `skipped:requires_decode` under `--no-content`.
  How to build its fixture without GPL encoders is for the researcher, for example by inserting
  MPEG-2 user data from a Python writer beside `tools/gen_video_fixtures.py`.
- The HDR first-frame arm fills Phase 4 D-08's seam (`src/analyzers/video/hdr.cpp:211-280`,
  `source` evidence). Stream-level `coded_side_data` keeps precedence, first-frame side data fills
  in when the stream level is absent, and `source` records which one fired. Without decode the value
  stays `skipped:requires_decode`, never a different value (Phase 6 D-12; PITFALLS Pitfall 14).
- PERF-02 follows Phase 5 D-13/D-14/D-15 and Phase 6's PERF-04. An instruction-count ratchet in
  `tests/golden/PERF_BASELINE.txt` gates regressions, and the ≥4× realtime wall-clock figure is
  measured and printed, never asserted. The reference input is the 10-minute 1080p30 mpeg4
  generator in `scripts/measure_timeline_perf.sh` (Phase 5 D-16). Whether the ratchet decodes the
  full ten minutes under valgrind or a shorter slice is the planner's call.
- Fixtures are LGPL-encodable on every leg, except D-10's proof streams. Hand-written bitstreams
  come from Python under `tools/` where encoders cannot express a case (Phase 4 D-01…D-03, Phase 6
  D-10). Every fixture declares its complete expected finding set (Phase 5 D-01/D-02), and DOC-03's
  registry-enumerated gate (`tests/integration/test_doc03_coverage.cpp`) runs with no exemption list.

### Deferred Ideas (OUT OF SCOPE)

- **`--first-divergence`:** listed in doc 00 §3.1's flags, but defined nowhere and tied to no requirement.
- **Snapshot-borne two-file scores (`snapshot --against`):** rejected by D-01; adding them later is additive.
- **Quality scored against a user-supplied source reference** (both sides scored against the source): this would make scores meaningful for a single file and storable in snapshots, but it changes `quality.*` semantics and ROADMAP SC4, so it belongs in its own phase.
- **A per-frame luma thumbnail sidecar** so perceptual works against snapshots: rejected by D-01.
- **A `mediadiff.toml` key to enable `quality.*`:** flags only in v1.
- **Subprocess isolation per `dir` file:** rejected by D-13.
- **A configurable watchdog limit:** rejected by D-12.
- **Time-aligned divergence for audio blocks:** Phase 6 D-03 aligns by index; audio could adopt D-07's rule, but that is outside this phase.
- **`--hwaccel cuda` / NVDEC** and its class-2 path signature (HW-01…03, v2), including doc 06 §6's NVDEC-vs-software fixture pair.
- **An SEI ITU-T T.35 caption scan in ParserScan** (EXT-01), which would lift the decode requirement from `video.closed_captions`.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VIDEO-11 | `video.closed_captions` detects A53/CEA-708 presence during the decode pass; `skipped:requires_decode` under `--no-content` | Q6: MPEG-2 user-data injection and an H.264 I_PCM writer both decode to `AV_FRAME_DATA_A53_CC` on the linked libavcodec (measured); Q7 shares the same writer |
| VIDEO-09 (first-frame arm) | HDR MDCV/CLL from first-frame side data when the stream level is absent | Q7: H.264 SEI 137/144 exports `Mastering display metadata` + `Content light level metadata` on frame 0 only (measured); `could_carry_frame_level_hdr` must learn `h264`; decode.c:1575 maps stream-level side data onto every frame, so precedence matters |
| CONTENT-01 | `content.video.frame_hash` hashes exactly `bytes_per_row × height` per plane | Q1/Q2: decoder settings (clear `AV_PKT_FLAG_DISCARD`, `AV_CODEC_FLAG_UNALIGNED`, `idct=simple`, `thread_count=1`); hash cost measured (0.26 ms/frame at 1080p) |
| CONTENT-02 | Mismatch reports first divergent frame, ranges merged at 1-frame gaps, total count | Fixture recipes measured (HuffYUV one-frame corruption -> exactly frame 40; MJPEG/HuffYUV packet drop -> time alignment needed); `HashChain` needs an additive timestamp array |
| CONTENT-03 | `--sample N` marks `sampled:N`; mismatch -> `skipped:sampling_mismatch` | `sampling_mismatch` exists in `SkipReason` with no producer; `compare_hash` currently maps every precondition mismatch to `hash_incomparable` (hash.cpp:191-199), so a dedicated branch is required |
| CONTENT-04 | `content.video.perceptual` SSIM on 128-wide luma, pinned swscale flags | Q9: swscale flags proven necessary (`SWS_ACCURATE_RND|SWS_BITEXACT`), integer 8x8 SSIM formulation with int64-only arithmetic, cost measured |
| CONTENT-05 | Frame-count mismatch handled (amended to overlapping time range, D-02) | Two-pointer time merge with one frame in flight per side (Q10) |
| CONTENT-06 | `content.video.frozen_runs` / `black_runs`, black normalized by range and depth | Q5: exact-hash frozen detection is GOP-unstable (measured); hysteresis SSIM rule gives identical spans on 12/12 lossy encodes; black measured tv mean 16.0 / pc mean 0.0 |
| CONTENT-07 | Hashing, perceptual, detectors inside one decode sweep | Q10: fused `Pass::video_decode` sinks + FrameTap rendezvous |
| CONTENT-08 | `quality.psnr` / `quality.ssim` in-tree at native resolution, min and mean | Q9: exact uint64 SSE, libvmaf's PSNR cap convention `(6*bpc)+12`, SSIM cost at 1080p measured |
| CONTENT-09 | `quality.vmaf` behind `MEDIADIFF_WITH_VMAF`, model pinned, refuses `--sample` | Q8: libvmaf 3.2.0 API facts, built-in `vmaf_v0.6.1`, vcpkg port is `!windows`, identical-input self-score measured (not constant) |
| CONTENT-10 | `quality.*` against snapshot -> `skipped:requires_media` (amended by D-01) | No stored scores; amendment table in this file |
| CONTENT-11 | Lockstep, one frame in flight per side | Q10: one-sided fused sweeps on two `std::thread`s + single-slot rendezvous; pull-style alternative described |
| TRUST-04 | `±tol` perceptual/quality carry path-signature preconditions | Q10/Q9: shared precondition helper; guard needs a synthetic-evidence unit test because D-01 makes it unreachable in live runs |
| TRUST-07 | 1/4/16 thread identical chains | Q4: holds for clean streams on all 9 codecs; does NOT hold for corrupt streams (non-deterministic even at fixed N for mpeg4/mpeg2/h264/hevc) -> production decode must pin 1 thread |
| PERF-02 | Full content pass >= 4x realtime software decode | Q12: 22.6x realtime single-threaded on the actual D-16 reference (measured), 286.7e9 instructions, ratchet slice recommendation |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

Directives from `.claude/CLAUDE.md` that bind this phase (treated as locked):

- **C++20, no modules, no `std::format` (fmt instead), no exceptions across the lib boundary (`expected<T, Error>`).** The video decode pass, lockstep driver and scorers report failures through `mediadiff::expected`; `avcodec_*`/`sws_*`/`vmaf_*` return codes map to `Error` at the `src/probe/` boundary. No coroutines, no `std::jthread` (REQUIREMENTS out-of-scope; CLAUDE.md "avoid" list).
- **Rational time everywhere; floating milliseconds only in rendered output.** Frame timestamps are `{int64 ticks, AVRational tb}`; D-02/D-07 matching is exact rational math. Scores are stored as quantized `RationalValue` (the `tol` comparator supports `rational`/`int64` only; `src/probe/audio_decode.h` `kLoudnessQuantiserDen` precedent).
- **Determinism:** byte-identical `--json` across identical runs; fixed-K/fixed-epsilon algorithms; integer inputs. A check that jitters is a bug, not a tolerance problem. (Directly drives the single-thread decode finding below.)
- **LGPL decode-only, no GPL in the binary.** No new vcpkg FFmpeg feature may be added; the proof-stream encoders live only in the pinned generator, never in `vcpkg.json` (AR-03).
- **Test data: no media binaries in git; fixtures synthesized with `-flags +bitexact -fflags +bitexact`, requiring ffmpeg CLI >= 6.1 at generation time.** Hand-written bitstreams come from Python (`tools/`).
- **CI: 3-OS matrix, warnings-as-errors (`/W4`, `-Wall -Wextra`).** `__int128` is unavailable on MSVC; `src/core/exact_int.h` documents that `core/rational.h`'s MSVC Int128Accum arm is a separate implementation that only the Windows leg executes. New hot-loop arithmetic must be int64-only.
- **Check IDs are forever:** the eight new IDs are approved at a roster checkpoint before registration in `src/core/checks.def`.
- **Error handling:** CLI maps `Error.kind` to exit codes; watchdog trip = could-not-run exit 66.
- **GSD workflow enforcement:** this research made no repository edits; every experiment lives in the session scratchpad (`/tmp/claude-1000/-home-dzka-projects-mediadiff/.../scratchpad/`).
- **Memory-derived project lessons (MEMORY.md, apply here):** regenerating fixtures silently rewrites CI-runner hashes (corpus digest rewrite trap); the five byte-exact goldens are designated-leg-only and CI hard-codes `EXPECTED_EXCLUDED_COUNT=5`; run the `ci.yml` lint scripts after every plan; `WINDOWS.md` JSON is the source of truth; secure-phase runs before phase.complete; CI green does not wake the monitor.

## Summary

The phase is mostly well-understood plumbing (a fused `Pass::video_decode` sink set beside the existing `Pass::audio_decode`), but the measurements taken this session overturn or sharpen six assumptions the CONTEXT and design docs rest on. Every finding below was measured against the **actually linked** FFmpeg 8.1 (`libavcodec 62.28.100`, `libswscale 9.5.100`, dav1d 1.5.4, `[VERIFIED: build/x64-linux/vcpkg_installed/.../ffversion.h, dav1d.pc, libswscale.pc]`), against the pinned 9.0.1 generators (x86_64 native, aarch64 under qemu-user), or against real CI logs.

**Findings that change what the planner builds:**

1. **Production video decode must be single-threaded (`thread_count = 1`).** Decoding at 1/4/16 threads gives identical chains on every *clean* fixture (9 codecs, TRUST-07 passes), but on a stream with a single corrupted frame (the SC1 fixture class) frame/slice threading is *non-deterministic run to run*: 10 distinct chains in 10 runs for MPEG-4 and H.264 frame threads, 6 for MPEG-2 slice threads, 3 for HEVC slice threads. Single-thread output is stable (20/20). This contradicts CONTEXT's Claude's-Discretion default "Decoder threading stays automatic" and needs user confirmation (Open Question 1). Cost measured: still 22.6x realtime with the whole content pass on the D-16 reference.
2. **D-09's worry is real and measured: default IDCT selection differs across architectures.** pinned x86_64 vs aarch64 (NEON, qemu-user), identical MPEG-4/MPEG-2/MJPEG streams: default `idct` **differs**; `idct=simple` makes all 20 tested streams (MPEG-4, MPEG-2, MJPEG, HuffYUV, FFV1, H.264 8/10-bit and MBAFF, HEVC 8/10-bit, VP9 incl. tiles, AV1 via dav1d/libaom/SVT-AV1) **bit-identical**. The same setting also stops an XviD-tagged MPEG-4 stream silently switching to the XviD IDCT (measured: a 12-byte user-data insertion changed the chain). Pin `AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_UNALIGNED` plus `idct_algo = FF_IDCT_SIMPLE`, single thread.
3. **Cross-arch bit-exactness only holds for conforming streams.** Four corrupted streams (MPEG-4, H.264, HEVC, VP9) decode differently on x86_64 vs aarch64 even single-threaded with `idct=simple` and even with `-ec 0`. A stream with decode errors must not claim class 1 (recommend: `decode_error_count > 0` degrades the evidence to class 2 + signature).
4. **Exact-hash frozen detection is GOP-unstable; a hysteresis SSIM rule is stable.** On 12 lossy encodes of one frozen-segment source, exact frame-hash equality yields 0 spans (MPEG-2, x264 without I-frames in range), or spans fragmented at every I-frame. The doc's single 0.9995 SSIM threshold is broken by I-frame refresh in 3 of 12 (dips to 0.9984). Enter at SSIM > 0.9995, continue while > 0.995, minimum 3 frames, gave the identical span on 12/12.
5. **Two libav facts the hash depends on, both verified in source and by experiment:** `AV_CODEC_FLAG2_SKIP_MANUAL` does nothing for video (audio-only, decode.c:336); edit-list-discarded video frames are dropped *inside* libavcodec (decode.c:451, :632) unless the **packet's** `AV_PKT_FLAG_DISCARD` is cleared before `avcodec_send_packet` (87 -> 88 frames, chain equals the MKV remux of the same packets). And without `AV_CODEC_FLAG_UNALIGNED`, `av_frame_apply_cropping` silently skips the left crop (frame.c:797-817): a 64x64 stream with a declared 54x54 display decoded as 60x54.
6. **libvmaf's identical-input score is not a constant.** `vmaf_v0.6.1` on identical 50-frame input: per-frame min 97.428378 (frame 0) .. max 100.0, mean 99.948568, harmonic mean 99.947251. Taking 100 as the baseline "defined maximum" would false-positive on short clips (a 1-frame clip scores 97.43). Compute the self-score. Also: the vcpkg `libvmaf` port is `"supports": "!windows"`; `--vmaf` cannot exist on the Windows leg.

**Primary recommendation:** Build `Pass::video_decode` as a per-stream sink bundle fused into `run_packet_scan` (hash, 128-wide luma thumbnail feeding frozen/black/perceptual, A53 and first-frame HDR capture), decode with the pinned settings above, drive lockstep as two unmodified one-sided sweeps on two `std::thread`s feeding a single-slot rendezvous consumed by a scorer on the calling thread, and deliver the watchdog as a per-job heartbeat observed by a `cli/` thread that ends the process with `std::_Exit(66)` after flushing the report.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Video decode, per-frame hash, thumbnail, frozen/black, A53, HDR first-frame | `src/probe/` (new `video_decode.{h,cpp}`, fused in `packet_scan.cpp`) | `src/analyzers/content/`, `src/analyzers/video/` (emit Measurements from the result slot) | Only `src/probe/` may call `avcodec_*`; analyzers read `ProbeResults::video_decode` (PROBE-08, PROBE-10) |
| Two-file scoring (perceptual, psnr, ssim, vmaf) | New pair driver (`src/probe/lockstep.{h,cpp}`) | `src/analyzers/content/` (measurement assembly) | Needs both sides' frames at once; cannot be an `AnalyzerSpec::run(const ProbeResults&)` |
| Frame pairing by time, divergence locator | `src/compare/` (extend `hash.cpp`) for the locator; pair driver for live scoring | — | Locator runs identically on live and snapshot chains (D-07); scoring pairing must run during the sweep |
| Path-signature preconditions for `tol` checks | `src/compare/` (shared helper beside `kPreconditionKeys`) | analyzers record the keys in evidence | TRUST-04 |
| Watchdog thread, `_Exit(66)`, dir abandon logic | `src/cli/` | `src/probe/heartbeat.h` (library side: counters only) | ENG-16: library never exits or writes streams |
| Skip reasons, schema enum, `--explain` docs | `src/core/model.h`, `docs/schema/report-1.0.json`, `docs/checks/*.md` | `src/report/junit.cpp` | Additive enum values appear in 4+ switch sites |
| Proof streams + cross-arch proof | `scripts/` + `.github/workflows/ci.yml` (new job + artifact) | `tests/integration/` + `tests/golden/` | D-10 |
| Perf ratchet | `scripts/measure_video_perf.sh` (sibling of the audio/timeline scripts) + `tests/golden/PERF_BASELINE.txt` | `tools/bench/` | Phase 5 D-13/14, Phase 6 PERF-04 pattern |

## Open Verification Questions — Findings

Provenance: `[VERIFIED: ...]` = tool/Read this session; `[CITED: ...]` = official source fetched this session; `[ASSUMED]` = unverified.

### Q1 (D-06). How 8.1 surfaces edit-list-discarded video frames, and how to keep them

**Mechanism** `[VERIFIED: Read of n8.1 source this session]`:

- `libavformat/mov.c:4443`: `flags |= AVINDEX_DISCARD_FRAME;` for a sample whose CTS falls before the edit-list start; `mov.c:11291-11293` (`mov_finalize_packet`): `if (sample->flags & AVINDEX_DISCARD_FRAME) { pkt->flags |= AV_PKT_FLAG_DISCARD; }`. The packet is **still delivered** by `av_read_frame`; only the flag is set. (`mov.c:11450` drops discard packets only inside the IAMF path.)
- `libavcodec/packet.h:650` `#define AV_PKT_FLAG_DISCARD   0x0004`; `libavutil/frame.h:646` `#define AV_FRAME_FLAG_DISCARD   (1 << 2)`; `libavcodec/avcodec.h:368` `#define AV_CODEC_FLAG2_SKIP_MANUAL    (1 << 29)`.
- `libavcodec/decode.c:1557-1558`: `if (pkt->flags & AV_PKT_FLAG_DISCARD) { frame->flags |= AV_FRAME_FLAG_DISCARD; }` (in `ff_decode_frame_props`). Then for **video** `decode.c:451`: `ret = (!got_frame || frame->flags & AV_FRAME_FLAG_DISCARD) ? AVERROR(EAGAIN) : 0;` and `decode.c:632`: `if (ret == AVERROR(EAGAIN) || (frame->flags & AV_FRAME_FLAG_DISCARD)) { av_frame_unref(frame); continue; }` -- the frame is destroyed inside libavcodec and never reaches `avcodec_receive_frame`.
- `AV_CODEC_FLAG2_SKIP_MANUAL` is consulted only inside `discard_samples()` (decode.c:336), which is called only for `AVMEDIA_TYPE_AUDIO` (decode.c:453, :629). **It has no effect on video**; there is no video analogue. Phase 6's audio trick does not carry over.

**How to keep every frame** `[VERIFIED: experiment]`: clear the flag on the `AVPacket` before `avcodec_send_packet`: `pkt->flags &= ~AV_PKT_FLAG_DISCARD;` (the struct is the sweep's own scratch packet, `ScratchPacket` in packet_scan.cpp; `make_packet_record` has already captured the original flags, so the timeline analyzers still see the raw flag). Measured on `ffmpeg -ss 0.5 -i src_bf.mp4 -c copy trim.mp4` (mpeg4, `-bf 2`; first 3 packets carry `flags=0x4`, pts -512/-1536/-1024):

| input | default | DISCARD cleared |
|---|---|---|
| `trim.mp4` (edit list) | 87 frames, chain `62f96014...` | 88 frames, chain `e26bcdab...` |
| `trim.mkv` (same packets remuxed, no edit list) | 88 frames, chain `e26bcdab...` | 88 frames, chain `e26bcdab...` |

The recovered frame has a *negative* pts (-512 ticks). So with the flag cleared an MP4 trim and its MKV remux hash equal (D-06 satisfied), and D-02's "timestamps measured from each side's own first frame" must tolerate a negative first pts. `[VERIFIED: vdump probe, scratchpad]`

Also note: leading B-frames with missing references (pts -1536/-1024 here) decode to nothing in the MPEG-4 decoder; HEVC RASL skipping after a CRA at stream start is decoder-internal and container-independent. Neither depends on the container, so neither breaks remux equality.

### Q2 (D-09). Decoder settings for cross-architecture bit-exactness, and the two-build proof for video

**Source facts** `[VERIFIED: Read of n8.1 source]`:

- `libavcodec/idctdsp.c:267-286`: `if (avctx->idct_algo == FF_IDCT_INT) {...} else if (avctx->idct_algo == FF_IDCT_XVID) { ff_xvid_idct_init(c); } else { // accurate/default ... ff_simple_idct_int16_8bit }`. The architecture hook then overrides it.
- `libavcodec/x86/idctdsp_init.c:76-141`: SIMD simple IDCT selected when `idct_algo` is `FF_IDCT_AUTO`, `SIMPLEAUTO`, `SIMPLEMMX` **or `FF_IDCT_SIMPLE`** (lines 92, 108, 119).
- `libavcodec/aarch64/idctdsp_init_aarch64.c:37-41`: NEON simple IDCT selected when `idct_algo == FF_IDCT_AUTO || FF_IDCT_SIMPLEAUTO || FF_IDCT_SIMPLENEON` -- **not** `FF_IDCT_SIMPLE`. So `idct=simple` = x86 SIMD-simple vs aarch64 C-simple; `auto` = x86 SIMD-simple vs aarch64 NEON.
- `libavcodec/mpeg4videodec.c:3611-3618, 3739-3741`: `if (ctx->xvid_build >= 0 && avctx->idct_algo == FF_IDCT_AUTO && !h->c.studio_profile) { switch_to_xvid_idct(avctx, h); }` -- an "XviD0050" user-data string flips the IDCT under AUTO.
- `AV_CODEC_FLAG_BITEXACT` is consumed by `mpeg4videodec.c:4011-4013` (`ff_mpv_unquantize_init`), `h263dec.c:117`, `x86/hpeldsp_init.c:89-93` (exact vs approximate no-rnd half-pel SIMD), `vp9.c:313`/`x86/vp9dsp_init.c:270` (`if (!bitexact)`). It does *not* choose the IDCT, and H.264/HEVC ignore it.
- FFmpeg's own FATE uses `-flags +bitexact -idct simple` for MPEG-2/MPEG-4 decode references (`tests/fate/video.mak:286-290`, `tests/fate/mpeg4.mak`), `[CITED: n8.1 tests/fate]`.

**Measured, x86_64, linked 8.1 libs** `[VERIFIED: scratch vdump harness]`: 20 distinct streams (mpeg4 incl. qpel/noise, mpeg2 incl. interlaced/noise, mjpeg 420/422, huffyuv, ffv1, h264 8/10-bit/MBAFF, hevc 8/10-bit, vp9 incl. 4-tile row-mt, av1 via dav1d from libaom and SVT-AV1) decode to the same chain under `av_force_cpu_flags` none / MMX / SSE2 / AVX2 / auto, thread counts 1/4/16 and both thread types, with and without `BITEXACT`. The IDCT choice itself is observable: on one noisy MPEG-4 stream `idct=simple` = `fc3500cf...`, `xvid` = `fa97e14a...`, `int` = `6d71fdb8...`; a stream with an inserted `XviD0050` user-data block gave `fa97e14a...` under AUTO and `fc3500cf...` under `idct=simple`.

**Measured, cross-architecture** `[VERIFIED: pinned 9.0.1 ffmpeg, x86_64 native vs aarch64 under qemu-user-static 8.2.2, `-flags +bitexact -threads 1 -f framemd5`, md5 column only]`:

| stream set | default idct | `-idct simple` |
|---|---|---|
| mpeg4 (B-frames), mpeg2, mpeg2 noise, mjpeg | DIFFERENT | SAME |
| mpeg4 noise, mjpeg noise, huffyuv, ffv1, h264, hevc, vp9, av1 | SAME | SAME |
| mpeg4 qpel, mpeg2 interlaced, h264 10-bit/MBAFF/noise, hevc 10-bit, vp9 tiles, av1 libaom | not run | SAME |

20 streams total, all SAME under `idct=simple`; `idct=int` also SAME across arches (C on both). Caveats: qemu-user is an emulator (repeat runs under it were stable, 3/3); the generators are 9.0.1 (libavcodec 63), the linked libs are 8.1 (62) -- the relevant kernels (`idctdsp_init_aarch64.c`, `x86/idctdsp_init.c`) are the ones read above, but the CI proof is what D-09 demands. `[ASSUMED]` that 8.1 == 9.0.1 for these decoders.

**Corrupt streams are not portable** `[VERIFIED]`: one-frame-corrupted MPEG-4, H.264, HEVC and VP9 (noise BSF, see fixtures) decode to different hashes on x86_64 vs aarch64 with `-threads 1 -idct simple`, and still differ with `-ec 0` (H.264: x86 `1e31a20f` vs arm `732c4c45`). x86 results were SIMD-flag invariant, the arm result was stable across runs, so this is architecture-dependent handling of out-of-spec data, not noise. Consequence: a class-1 claim is valid only for streams decoded with zero errors; recommend `decode_path_class = "class2 <sig>"` whenever the stream's `decode_error_count > 0` (extends D-09; needs confirmation, Open Question 3).

**Recommended decoder settings (recorded verbatim in the fingerprint's per-stream flags string, TRUST-01):** `flags = BITEXACT | UNALIGNED`, `idct_algo = FF_IDCT_SIMPLE` (set `avctx->idct_algo` or `av_opt_set(ctx, "idct", "simple", AV_OPT_SEARCH_CHILDREN)`), `thread_count = 1`, `max_pixels` bounded (see Security), decoder chosen by `avcodec_find_decoder(codecpar->codec_id)` (AV1 resolves to `libdav1d`).

**What the two-build proof needs for video (the `test_audio_hash_decoder.cpp` Test 2 method, extended):**

1. Proof-stream bytes must be *identical on every leg* -- they cannot be regenerated per leg. Measured: the same encoder command on pinned x86_64 vs aarch64 (qemu) gives different bytes for x264, x265, mpeg4, mpeg2video, mjpeg, huffyuv (only libvpx-vp9 matched). So **MPEG-4/MPEG-2/MJPEG/HuffYUV proof streams must also travel as artifacts**, not just the GPL-encoder ones D-10 names.
2. Identity assertion first: a manifest `{stream: XXH3-128}` travels with the artifact and each leg recomputes it before decoding anything (the `assert_aac_handwritten_input_identity` pattern, `tests/support/aac_handwritten_identity.h`).
3. Expected values are committed: `tests/golden/VIDEO_PROOF_CHAINS.txt` with `stream=<name> xxh3=<hex> frames=<n> chain=<hex> decoder=<name> flags=<string>`, transcribed by a human from the designated leg's printed pasteable lines (CORPUS_DIGEST / PERF_BASELINE precedent; "CI measures, humans update").
4. A class-1 name table (`determinism_class_for_video_decoder`) starts empty (all class 2, per D-09) and a table-driven test asserts every class-1 name has a proof row (enumerated-gate style). Promotion is a later, evidence-backed plan.
5. A hand-written H.264 I_PCM stream (Q7) is byte-identical on every leg with no artifact, but exercises no transform/MC/deblock; it proves the plumbing, not the codec.

### Q3 (D-10). Encoders in the four pinned builds; CI artifact handoff

`[VERIFIED: scripts/ffmpeg_pin.json; `-encoders` run on x86_64 natively and on aarch64 via qemu-user; `--prefix` configure string extracted with `strings` from the downloaded macOS and Windows binaries; Windows zip sha256 matches the manifest `084874559d...`]`:

| pin (`scripts/ffmpeg_pin.json`) | libx264 | libx265 | libvpx-vp9 | libaom | libsvtav1 | libopenh264 | native mpeg2video/mpeg4/mjpeg/huffyuv/ffv1 |
|---|---|---|---|---|---|---|---|
| linux-x86_64 (martin-riedl 9.0.1, `--enable-gpl`) | yes | yes | yes | yes | yes | yes | yes (`-encoders`) |
| linux-aarch64 (same provider) | yes | yes | yes | yes | yes | yes | yes (`-encoders` via qemu) |
| macos-arm64 (same provider) | yes (configure) | yes (configure) | yes (configure) | yes | yes | yes | not executed |
| windows-x86_64 (BtbN LGPL mirror) | **no** (`--disable-libx264`) | **no** (`--disable-libx265`) | yes | yes | yes | yes (+ libkvazaar, librav1e) | install script asserts them |

So libx264/libx265 exist on three of four legs but are absent on Windows, and outputs differ by architecture (Q2), which is why a single producer is required.

**Handoff design for `.github/workflows/ci.yml`** (today a single matrix job `build`, ci.yml:22-84; the byte-exact goldens are scoped by `DESIGNATED_LEG="x64-linux"`, ci.yml:527-531, `EXPECTED_EXCLUDED_COUNT=5`):

- Add a cheap job `video-proof-streams` (`ubuntu-24.04`, no vcpkg): install the pinned ffmpeg via `scripts/install_pinned_ffmpeg.sh`, run a new `scripts/gen_video_proof.sh` (all proof encodes with `-threads 1`, bitexact flags, and each encoder's own single-thread parameters), write `MANIFEST.json`, `actions/upload-artifact@v4` (already used at ci.yml:711 `mediadiff-windows-x64`).
- In `build`: `needs: video-proof-streams`; `actions/download-artifact@v4` into `build/video-proof/`; export `MEDIADIFF_VIDEO_PROOF_DIR`; the proof test asserts identity then chains against the committed golden. `[ASSUMED]` download-artifact@v4 semantics match upload-artifact@v4 (same major, not exercised in this repo yet).
- Gate-that-stops-gating guard: locally (no env) the test SKIPs; in CI the Test step must set `MEDIADIFF_REQUIRE_VIDEO_PROOF=1` and fail if the proof test appears under "did not run" (mirror the designated-golden skip check, ci.yml:535-556).
- Every new designated-leg-only golden (e.g. proof chains, fixture-derived frame-hash goldens) must be added to `EXCLUDED_TEST_REGEX` and `EXPECTED_EXCLUDED_COUNT` in the same commit, or the non-designated legs fail the count guard.
- On the designated leg the test may regenerate the streams locally when `MEDIADIFF_VIDEO_PROOF_DIR` is unset and the pinned ffmpeg has the encoders (dev-loop verification, matching the "regenerate under `TZ=UTC taskset`" lesson in MEMORY.md).
- Reproducibility of the producer on one runner class is measured: all nine proof encoders gave identical bytes across 4 runs and across `taskset` 1/2/8 CPUs on this host. Not measured: variation across GitHub runner CPU generations (x265 and SVT-AV1 select asm by CPU). `[ASSUMED]` x264/libvpx stable; treat x265/SVT-AV1 as the ones most likely to need re-transcription, and prefer libaom for the AV1 stream.
- Bootstrapping order: first CI run prints the pasteable golden lines and leaves all decoders class 2; promotion to class 1 is a separate later plan after all legs are green against the committed golden.

### Q4 (TRUST-07 / D-11). Thread invariance and how to test it

`[VERIFIED: scratch vdump against the linked 8.1 libs; thread options `AVCodecContext::thread_count`/`thread_type`, active type read back]`

- Which threading actually engages (`active_thread_type`): mpeg4 frame (slice: none); mpeg2video slice (frame: none); mjpeg none (always 1 thread); huffyuv frame; ffv1 frame+slice; h264/hevc/vp9 frame or slice; AV1 is `libdav1d` with its own `n_threads` (libdav1d.c:238 `s.n_threads = FFMIN(threads, DAV1D_MAX_THREADS)`), `active_thread_type=0`.
- **Clean streams:** 20 distinct streams x {auto, 1, 4 frame, 16 frame, 4 slice, 16 slice, 16 both}: one chain each per stream. Repeat runs: 20/20 identical at 4-16 threads (mpeg4 frame, mpeg2 slice). TRUST-07 as written passes.
- **Corrupt streams (a packet-40 byte flip via `-bsf:v noise`):** not invariant. Repeat runs at a *fixed* thread count: mpeg4 frame x4 -> 10 distinct chains / 10 runs; h264 frame x4 -> 10/10 distinct; hevc slice x4 -> 3 distinct / 10; mpeg2 slice x4 -> 6 distinct / 10; vp9 stable. At 1 thread: 20/20 identical for mpeg4/mpeg2/h264. With `ec=0` the multi-thread runs become repeat-stable (10/10) but still differ from the single-thread chain, so disabling concealment does not rescue threading.
- Because the SC1 fixture class *is* a corrupted stream, the locator test would flake under any threaded default for the corpus codec itself (MPEG-4).

**Recommendation:** production `thread_count = 1` for every video decoder, recorded as `threads=1` in the flags string (D-11 already permits recording a per-decoder pin; this finding extends the pin to all decoders for error-bearing input). Add `ProbeOptions::video_decode_threads` (0 = production default = 1; any other value sets `thread_count` and `thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE`) reachable from unit tests that call `fingerprint_input(path, registry, options)` with 1/4/16 on the clean fixtures; a second test runs the *production default* on the corrupt fixture N times and asserts a single digest. `dir`'s `--threads` worker pool already provides coarse parallelism; lockstep (Q10) gives 2x per compare. The measured slowdown is the D-11-required report (Q12).

### Q5. Frozen-run detector: exact hash equality vs near-identical SSIM

Method `[VERIFIED: scratch prototype vsc.c against the linked libs; frames decoded single-threaded with the Q2 settings; thumbnail = Y plane wrapped as GRAY8, `sws_scale` AREA to 128 wide; SSIM = integer 8x8 window, stride 4, C1=26634, C2=239708 (see Q9)]`: one source (`testsrc2` 352x288, 150 frames) with frames 51..100 replaced by frame 51 (`freezeframes` via `-filter_complex "[0:v]split[a][b];[a][b]freezeframes=first=51:last=100:replace=51[v]"`), encoded 12 ways. Truth: frozen frames are indices 51..100.

| encode | exact-equality runs (frame ranges) | SSIM>0.9995 single threshold | hysteresis 0.9995 enter / 0.995 continue |
|---|---|---|---|
| mpeg4 g12 bf2 | (63-69)(75-81)(87-93) | (51-100) | (51-100) |
| mpeg4 g12 bf0 | (54-59)(61-71)(73-83)(85-95)(97-100) | (51-59)(60-100) | (51-100) |
| mpeg4 g250 bf2 | (60-99) | (51-100) | (51-100) |
| mpeg2 g12 / g15 | **none** | (51-100) | (51-100) |
| x264 g25 | (63-65)(67-69)(71-73) | (51-74)(75-99) | (51-100) |
| x264 g250 (no I-frame in range) | **none** (0/49 equal pairs) | (51-100) | (51-100) |
| x264 g48 bf0 | (57-94) | (51-95)(96-100) | (51-100) |
| x265 g25 | (64-67)(68-71)(76-79)(84-96) | (51-100) | (51-100) |
| vp9 g25 | (52-55)(56-59)(60-74)(77-99) | (51-100) | (51-100) |
| av1 (SVT) g25 | (51-65)(66-74)(75-99) | (51-100) | (51-100) |
| mjpeg | (51-100) | (51-100) | (51-100) |

- **Exact equality is unusable on lossy encodes**: spans fragment at I-frames and P-frame residual refinement, vanish entirely for MPEG-2 and x264-without-refresh, and move with GOP length. It is also class-dependent (it could only ever run where hashes are comparable). This is exactly the "span that moves with GOP structure" CONTEXT says to raise, not tolerate.
- **The doc's single 0.9995 threshold is marginal**: I-frame refresh inside a frozen region drops the consecutive-frame SSIM to 0.998778 (mpeg4 g12 bf0 at 60), 0.998395 (x264 g48 bf0 at 96), 0.999471 (x264 g25 at 75) -- below 0.9995 in 3 of 12 encodes. P-frames in the region were >= 0.999655 in every encode.
- **Hysteresis gives the same span for all 12 encodes**: enter when a consecutive-frame pair scores > 0.9995, stay while > 0.995, emit runs of >= 3 frames. Margins: worst in-run dip 0.9984 (2.3x above 0.995 in distance-from-1 terms); the moving regions scored 0.887-0.940 (testsrc2) and 0.984-0.997 (mandelbrot zoom, 1 of 99 pairs above 0.995).
- **Honest limit**: a near-static smooth source is indistinguishable from frozen at this resolution (`gradients` speed 0.005: 99/99 pairs above 0.995, median 0.99985). Both sides of a real compare flag the same content identically, and the `span` semantic compares introduced/removed runs, so this is not a false-positive source by itself; it is why the two thresholds are fixed named constants with `--explain` text. Validated on synthetic content only `[ASSUMED]` for natural video.
- **Consequence for class independence**: the detector must not branch on hash-comparability (doc 06 §2.3's "exact if class 1, SSIM if class 2" would make spans depend on the machine); use the SSIM rule unconditionally, on the same 128-wide thumbnail the perceptual check needs (cost already paid; Q9).

**Black detection** `[VERIFIED]`: a 25-frame black segment in tv-range and pc-range encodes (`scale=out_range=tv|pc`) gives thumbnail mean **16.0** (tv) and **0.0** (pc), variance 0.0 in both, exactly frames 25..49. A range-unaware `mean <= 18` flags pc-range dark grey (Y 16-18); a range-unaware `mean <= 2` misses every tv-range black. Use doc 06 §2.3's rule on the 8-bit thumbnail with `black_point = 16` for limited and `0` for full (after the same `yuvj` fold as `detail::fold_pix_fmt_range`, `src/analyzers/video/analyzers.h:104-130`), threshold `black_point + 2`, variance < 4. The thumbnail is bit-depth-normalized to 8 bits (see Q9), so depth needs no separate handling for black.

### Q6. `video.closed_captions` fixture without GPL encoders

Two verified routes; the first needs only bytes inserted into an encoder-produced ES, the second needs no encoder at all.

**Route A: MPEG-2 user data** `[VERIFIED: scratch Python + linked libavcodec]`. Encode an ES with the native encoder (`ffmpeg -f lavfi -i testsrc2=size=352x288:rate=25:duration=2 -c:v mpeg2video -g 12 -bf 0 -b:v 2M -flags +bitexact -fflags +bitexact -f mpeg2video base.m2v`). The ES start codes are `B3` (sequence), `B5` (sequence ext), `B8` (GOP), `00` (picture), `B5` (picture coding ext), `01..AF` (slices). Insert `00 00 01 B2 "GA94" 03 <0x40|cc_count> FF <cc_count x (FC, d1, d2)> FF` immediately before the first slice start code of each picture. Decoded by the linked libavcodec: every tagged frame carries side data `ATSC A53 Part 4 Closed Captions` (`AV_FRAME_DATA_A53_CC`, read in `libavcodec/mpeg12dec.c:1281`), and the frame hashes are **identical** with and without the insertion (chain `cb3f7797...` both), so the pair differs in exactly the captions. Raw `.m2v` frames carry `pts = AV_NOPTS_VALUE` (measured), which also exercises D-02's index-fallback path.

**Route B: decodable H.264 from a Python I_PCM writer** `[VERIFIED: scratch writer, 70 lines]`. Every macroblock is `mb_type ue(25)` (I_PCM: raw samples after byte alignment), deblocking disabled, so there is no transform, no entropy coding of residuals and no encoder; frames are bit-exact by construction. A NAL type 6 SEI with payload type 4 (user_data_registered_itu_t_t35: `B5 0031 "GA94" 03 ...`) decodes to `AV_FRAME_DATA_A53_CC` on the frame that carries it (frame 0 only when the SEI is only in the first access unit). The same writer produces the HDR first-frame fixture (Q7). Extends `tools/gen_video_fixtures.py`'s existing SPS/PPS/slice-header writers (which today emit parse-only, non-decodable slices). Essential writer skeleton is in Code Examples.

Detection contract: `video.closed_captions` value = presence of `AV_FRAME_DATA_A53_CC` on any decoded frame of the stream (any frame, not only the first; captions commonly start mid-stream). A53 also exports from HEVC (`h2645_sei.c:826`), AV1 (`av1dec.c:990`, `libdav1d.c:387`). Under `--no-content` the pass is cleared and the check reports `skipped:requires_decode`.

### Q7. HDR first-frame arm: codecs that carry MDCV/CLL only as frame side data, and a fixture

`[VERIFIED: Read of n8.1 source + experiment]`

- Decoders that export mastering-display/content-light from the bitstream: HEVC and H.264 via the shared `libavcodec/h2645_sei.c` (`ff_h2645_sei_message_decode`, called with `AV_CODEC_ID_H264` at `h264_sei.c:284` and `AV_CODEC_ID_HEVC` at `hevc/sei.c:243`; export at `h2645_sei.c:643-721`), AV1 via `libdav1d.c:524,546` and `av1dec.c`, plus PNG/QSV/APV (irrelevant). `mpeg2video`/`mpeg4` cannot (correct `not_applicable`).
- **`detail::could_carry_frame_level_hdr` (`src/analyzers/video/hdr.cpp:144`) lists only `hevc` and `av1`; H.264 must be added.** Verified on the Q6 writer: an H.264 stream with SEI 137 + 144 in the first access unit decodes with frame 0 side data `[Mastering display metadata, Content light level metadata]` and frames 1.. side data empty.
- **Stream-level data is also mapped onto every frame**: `decode.c:1575` `side_data_map(frame, avctx->coded_side_data, ... ff_sd_global_map)`. So for a file whose container has `mdcv`/`clli` (every existing `video_hdr_*` fixture) the first frame's side data is *also* populated. D-08's precedence (stream first, first-frame only when stream level is absent, `source` records which fired) is what avoids double-counting; the first-frame arm must only fill in when `StreamInfo::mdcv_present`/`cll_present` is false.
- Fixture: Route B writer with `--mdcv --cll` (primaries order in the SEI is G, B, R, each `(x, y)` u16 in 0.00002 units, then white point, then max/min luminance u32 in 0.0001 cd/m2; decode reorders to R,G,B, `h2645_sei.c:653-664`). LGPL-legal, byte-identical on every leg, no artifact. Optional second fixture for AV1 (dav1d export): libaom/SVT-AV1 are BSD and are present on all four pins, but using them in the regular corpus widens Phase 4 D-01's encoder policy by the same step `libopus` already took; leave to the roster checkpoint.
- The analyzer wiring: `video_hdr_analyzer()` currently declares `required_passes = {Pass::demux_header}`; it must additionally declare `Pass::video_decode` (removed from the union under `--no-content`), read `ProbeResults::video_decode->per_stream[i].first_frame_hdr`, and keep emitting `skipped:requires_decode` when the slot is `nullopt`. With decode available and neither arm present the value becomes a real `Absent{}` (presence semantic), never a skip.

### Q8 (D-01). VMAF: identical-input score, linking, model pin

`[VERIFIED: pinned ffmpeg `libvmaf` filter reports `version 3.2.0` in its JSON log, the same version as the vcpkg port; `[CITED: Netflix/vmaf v3.2.0 raw headers fetched this session]`]`

- **Identical input** (`vmaf_v0.6.1`, 352x288, 50 frames, ffv1 vs itself): per-frame `vmaf` min **97.428378** (frame 0; its motion feature differs), max 100.0, pooled mean **99.948568**, harmonic mean **99.947251**. A distorted pair (x264 crf 28 vs lossless): mean 95.19021, harmonic 95.18474, min 93.756662. Results are identical at `n_threads` 1/4/8 (sum of per-frame scores 4759.510517 in all three).
- Therefore the baseline self-score **must be computed, not assumed 100**: with "100" as the baseline, a one-frame clip scores 97.43 against 100 on *identical* media (2.57 drop vs `vmaf_drop: 0.5`), a 10-frame identical clip ~99.74. D-01's own "baseline scored against itself" is correct. Cost: a second libvmaf context fed `(B_i, B_i)` for every paired frame, so `--vmaf` costs about 2x one VMAF pass; optimization (skip the second feed when frame hashes are equal) is optional.
- PSNR/SSIM self-scores *are* constants by construction and may be taken analytically (SSIM exactly 1 by the integer formula, verified for identical windows; PSNR = the cap below) with a unit test that the analytic value equals the computed one. PSNR per-frame cap `[CITED: libvmaf integer_psnr.c]`: `psnr = MIN(10 * log10(peak*peak / MAX(mse, 1e-16)), psnr_max)` with default `psnr_max = (6 * bpc) + 12` (60 dB at 8-bit, 72 at 10-bit). Adopt this cap, record `identical_frames` in evidence, so identical frames contribute the cap to the mean and baseline-vs-identical-candidate deltas are exactly 0.
- **Model pin mechanism** `[CITED: libvmaf/model.h]`: `vmaf_model_load(VmafModel **model, VmafModelConfig *cfg, const char *version)` with `cfg = {name, flags = VMAF_MODEL_FLAGS_DEFAULT}` and `version = "vmaf_v0.6.1"`; built-in models are compiled in when `built_in_models` (meson default **true**; the vcpkg portfile passes only `-Denable_tests=false -Denable_docs=false`, so built-ins stay on and `enable_float` stays false, which `vmaf_v0.6.1` does not need: its features are `VMAF_integer_feature_*`, strings present in the pinned binary). Record in the fingerprint: model name, `libvmaf` version (`vmaf_version()`), and the vcpkg port version; `[CITED: vcpkg/ports/libvmaf/vcpkg.json]` pins 3.2.0.
- **API** `[CITED: libvmaf.h v3.2.0]`: `vmaf_init(&ctx, VmafConfiguration{log_level, n_threads, n_subsample, cpumask, gpumask})`, `vmaf_use_features_from_model`, per pair `vmaf_read_pictures(ctx, ref, dist, index)`, flush with `vmaf_read_pictures(ctx, NULL, NULL, 0)`, `vmaf_score_pooled(ctx, model, VMAF_POOL_METHOD_HARMONIC_MEAN | _MIN | _MEAN, &score, lo, hi)`. Pictures: `vmaf_picture_alloc(&pic, VMAF_PIX_FMT_YUV420P|422P|444P|400P, bpc, w, h)` (copy rows in; libvmaf needs the same pix_fmt/bpc for ref and dist, so promote the lower bit depth by an exact left shift, and convert other formats or skip). Use `n_threads = 0/1` (synchronous, one pair in flight) and `n_subsample = 1`; `--sample` is refused for VMAF (CONTENT-09).
- **Linking / platform** `[VERIFIED: CMakeLists.txt:22, :332-334, vcpkg.json:25-28, vcpkg/ports/libvmaf/vcpkg.json:7]`: `MEDIADIFF_WITH_VMAF` today only adds a compile definition (`target_compile_definitions(libmediadiff PRIVATE MEDIADIFF_WITH_VMAF)`); nothing finds or links libvmaf. The manifest feature `"vmaf": { "dependencies": ["libvmaf"] }` has no platform filter and the port declares `"supports": "!windows"`. Needed: `find_package(PkgConfig)`/`pkg_check_modules(libvmaf)` (the port installs a pkg-config file, `vcpkg_fixup_pkgconfig()` in the portfile) linked only when the option is on; a documented Windows exclusion (`--vmaf` there is the same exit-64 usage error naming the option and the platform); a **separate** designated-leg build directory (`-DVCPKG_MANIFEST_FEATURES=vmaf -DMEDIADIFF_WITH_VMAF=ON`) with its own test run, since the default build must keep `vmaf` absent (`integration.vmaf_absent`). Build cost: meson + nasm. `[ASSUMED]` libvmaf builds through vcpkg on macOS legs (no CI evidence).
- Determinism: libvmaf's scores are doubles; quantize to a `RationalValue` (`kQualityQuantiserDen = 1000`, ties away from zero, loudness precedent) before they reach `tol.cpp`. `[ASSUMED]` libvmaf's integer extractors are bit-exact across SIMD/arch (Netflix asserts this; not measured here); cross-machine VMAF compares never occur under D-01 (both sides one build, one run).

### Q9. SSIM/PSNR determinism, swscale flags, identical frames

**swscale flags are required, and were the only thing standing between SIMD and C** `[VERIFIED: linked libswscale 9.5.100, 4 inputs, `av_force_cpu_flags(0)` vs auto]`: with `SWS_AREA` alone the 128-wide thumbnail chain differs between SIMD and pure C on all three tested inputs (`95fb80d3...` vs `b33041fb...` etc.); with `SWS_AREA | SWS_ACCURATE_RND | SWS_BITEXACT` auto == none on every input (8-bit GRAY8, and GRAY10LE/GRAY16LE from 10-bit H.264/HEVC where flags made no difference on x86 but are still the pin). Record `algorithm=area;flags=accurate_rnd+bitexact;dst=128xH;swscale=9.5.100` as the D-04 scaler record. Not measured: aarch64 swscale (CI proof). Scale the **Y plane only, wrapped as a GRAY8/GRAY10LE/GRAY12LE/GRAY16LE frame** (a yuv->gray conversion would apply range conversion; the gray->gray path does not), height `th = ((128 * h / w) + 1) & ~1` (even), then normalize to 8 bits with `(v + (1 << (b-9))) >> (b-8)` so an 8-bit baseline vs a 10-bit candidate scores.

**Integer SSIM, 8x8 window, stride 4, int64-only** `[VERIFIED: prototype; overflow bounds derived here]`: with `N = 64`, `Sx, Sy, Sxx, Syy, Sxy` window sums, `vx = N*Sxx - Sx^2`, `vy`, `cxy = N*Sxy - Sx*Sy`, constants scaled by `N^2 = 4096`: `C1 = round((0.01*255)^2 * 4096) = 26634`, `C2 = round((0.03*255)^2 * 4096) = 239708`; `n1 = 2*Sx*Sy + C1`, `n2 = 2*cxy + C2`, `d1 = Sx^2 + Sy^2 + C1`, `d2 = vx + vy + C2`; `ssim = (n1*n2) / (d1*d2)`. Each factor is < 2^30 (n1 <= 5.33e8, n2 <= 5.4e8), each product < 2^60, so everything fits `int64_t`. **Do not use `__int128`** (MSVC; `src/core/exact_int.h` header comment) -- reduce the division with `frac = ((r >> s) << 24) / (den >> s)` where `q = num / den`, `r = num % den`, `s = max(0, bitlen(den) - 28)` (error < 1e-8 per window, deterministic). Identical windows give `num == den`, hence exactly 1 (observed: every exact-equal frame pair printed `ssim_prev=1.000000`). Per-frame score = mean of window Q24 values; store as `RationalValue{round(score * 1e6), 1000000}`-style quantized integers. Cost (single thread, -O2 scalar C) `[VERIFIED]`: 128-wide SSIM 1.0 s / 18000 frames (0.05 ms/frame); **native 1080p luma SSIM 10.3 ms/frame** (601 frames in 6.2 s = 3.2x realtime for luma only), so `quality.ssim` on the 10-minute reference would take ~3 minutes scalar; a per-row sliding sum reduces that several-fold (the doc's "cheaper native PSNR/SSIM" under `--sample N` is real but optional). Opt-in flag, not part of PERF-02.

**PSNR** `[design; ASSUMED arithmetic]`: exact `uint64` SSE per plane (8-bit: max 255^2 * pixels fits easily; 16-bit: 65535^2 * 8294400 = 3.6e16 < 2^63), gate on luma+chroma combined MSE = `(SSE_y+SSE_u+SSE_v) / (N_y+N_u+N_v)` sample-count weighted with per-plane values in evidence. dB from an integer fixed-point `log2` (iterative squaring, no libm) to keep cross-libm bytes identical, quantized to milli-dB; or accept `std::log10` with the loudness-precedent quantization.

### Q10 (CONTENT-11). Lockstep architecture, `compare.cpp`, `dir --content`, memory budget

Today `run_packet_scan` (`src/probe/packet_scan.cpp`) is one monolithic `av_read_frame` loop with all per-stream state local to it, `compare.cpp:211-217` calls `fingerprint_input` twice sequentially, `dir.cpp:403-408` does the same per worker job, and `WorkerPool` (`src/cli/worker_pool.{h,cpp}`) is plain `std::thread` plus an atomic counter that joins everything. All verified by Read.

**Recommended (A): two unchanged one-sided sweeps on two `std::thread`s, one single-slot rendezvous per side, scorer on the calling thread.**

- `ProbeOptions` gains a `FrameTap*` (nullable). When set, `VideoDecodeState` publishes each decoded frame *after* its own one-sided sinks ran (hash, thumbnail, frozen/black, A53, HDR) and blocks in `publish()` until the consumer calls `release()`. Exactly one frame is in flight per side (plus libav's internal DPB, which is not ours to bound) and two decoded sequences are never resident.
- New `probe/lockstep.{h,cpp}`: `fingerprint_pair(baseline, candidate, registry, options) -> expected<PairResult, Error>` runs `run_probe` for each input on its own `std::thread` with a tap, runs the scorer on the calling thread, joins both, then appends the two-file Measurements (D-01: baseline self-score, candidate score with the baseline's XXH3-128 identity in evidence) to both fingerprints. `compare.cpp` uses it only when neither input is a snapshot and content decode is enabled; a snapshot side keeps today's short-circuit and the two-file checks emit `skipped:requires_media` on both sides.
- The scorer does the D-02 two-pointer time merge: hold one `(A_cur, B_cur)`; `t = pts - first_pts` as exact rationals; if `|tA - tB| <= half_interval` pair and advance both, else advance the earlier side and count it unpaired. `half_interval` from `frame->duration` if set else the stream's `avg_frame_rate` (`AVFrame::duration`, `best_effort_timestamp` exist: `libavutil/frame.h:698, 775`). Missing/`AV_NOPTS_VALUE` timestamps on either side (measured for raw ES) -> index pairing, recorded in evidence. Negative first pts (Q1) is fine.
- Determinism by construction: the consumer, not thread timing, decides which side advances; each producer's own measurements depend only on its file. Verified precedent: the library is already exercised concurrently (one `fingerprint_input` per `dir` worker thread).
- Cancellation without `jthread`: `close()` on the slots sets an atomic flag and notifies the condition variable; a blocked `publish()` returns `false`; producers finish their own sweep without tapping. No exceptions cross the boundary; results come back through `expected`.
- `dir --content`: each worker job runs `fingerprint_pair`, so peak threads = `--threads` x 3 (two producers busy, one consumer mostly waiting) and the per-side packet-store cap must halve: `set_default_packet_scan_max_bytes(derive_per_file_cap_bytes(budget, 2 * threads))` (DIR-06; `packet_scan.cpp:43-48` `derive_per_file_cap_bytes`). Also account per-frame records (digest string + timestamp, about 64 bytes each) against the same `accounted_bytes` ceiling, otherwise a 24 h 30 fps file adds ~160 MB outside the budget `[ASSUMED estimate]`.
- Pairing streams: pair the first non-`AV_DISPOSITION_ATTACHED_PIC` video stream of each side (cover art is a one-packet video stream); skip two-file scoring with a named reason when either side has none.

**Alternative (B): pull-style `PacketSweep` class** (turn the loop body into `process_next_packet()`, expose `next_video_frame()`): single-threaded and trivially deterministic, but rewrites the monolithic function and all its local state, touches every scan flag, and forfeits the 2x parallelism. Reject unless threads are vetoed.

### Q11 (D-12/D-13). Watchdog: heartbeat, exit without joining, test hook

**Heartbeat (library side, `src/probe/heartbeat.h`, counters only)**: `struct Heartbeat { std::atomic<std::uint64_t> seq; std::atomic<int> in_call; std::atomic<std::int64_t> last_pts, stream_index; }` plus `thread_local Heartbeat* t_heartbeat` with `ScopedHeartbeatBinding` (set by the cli job before `fingerprint_input`/`fingerprint_pair`; the lockstep driver copies the pointer into its two producer threads; `in_call` is a counter so several producers can share one per-job slot). RAII `LibavCall` at each libav call site (`av_read_frame`, `av_parser_parse2`, `avcodec_send_packet/receive_frame`, the drain, `sws_scale`, libvmaf calls): `++in_call; ++seq;` on entry, `--in_call; ++seq;` on exit. No clock reads on the hot path; a blocked rendezvous is *not* a libav call so it never false-trips.

**Watchdog (cli side, `src/cli/watchdog.{h,cpp}`)**: one thread sampling every second; a slot trips when `in_call > 0` and `seq` has not moved for `kDecodeWatchdogLimit` (fixed constant). Value recommendation `[ASSUMED]`: **300 s**. Evidence for the floor: the audio and timeline ratchets run the whole sweep under cachegrind in 1 m 49 s and 1 m 24 s wall on the designated runner (`gh run view 35735099865` steps: "Audio instruction-count ratchet" 13:49:00-13:50:49, timeline 13:47:31-13:48:55), so no single legitimate call comes near minutes even under valgrind.

**Exit semantics** `[VERIFIED: scratch program wd.cpp, Linux]`: worker threads stuck in `sleep_for` and in a busy loop were `detach()`ed after the healthy worker was joined; `std::printf(report); std::fflush(stdout); std::_Exit(66)` exited 66 with the report flushed in all three cases (file, pipe, both stuck kinds). `std::exit(66)` also worked in the trivial program but runs static destructors and `atexit` handlers, which can block on locks the stuck thread holds; use `_Exit`/`quick_exit` after an explicit flush. `[ASSUMED]` `std::_Exit`/`std::quick_exit` available in MSVC v143's UCRT (declared in `<cstdlib>`; verify on the Windows leg), and `lint_eng16.sh` must keep scanning `src/probe` without seeing either token (they live only in `src/cli`).

**`dir` abandonment (D-13)**: `WorkerPool::run_indexed` joins all workers, so a stuck worker hangs it forever. Change it to wait on a condition variable for `finished + abandoned == job_count`; the watchdog marks the stuck job's outcome (`hard_error` = could-not-run naming file, stream, last pts) and increments `abandoned`; afterwards join finished threads and `detach()` abandoned ones. A detached thread still references the job closure, `results`/`outcomes` vectors and the registry: heap-allocate that shared state and intentionally leak it (or `shared_ptr` it), because the process ends in `_Exit` and ASan builds must not report a use-after-free. Exit path: write the corpus report (`dir.cpp:466`-area aggregation already keeps other files' results), `fflush`, `_Exit(66)`. Single-file `compare/snapshot/inspect`: trip -> partial JSON envelope with an `error` entry and no measurements from that file, `_Exit(66)` immediately. `--probe-timeout` help text (`options.cpp:262`) must say it bounds only the open, and the new limit is fixed.

**Test hook** (precedent: `MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR`, read through `getenv_utf8` at `dir.cpp:369`, exercised through `run_cli(args, &env)` in `tests/integration/test_exit_codes.cpp:147`): `MEDIADIFF_TEST_STALL_LIBAV_CALL=<site>:<n>` makes the `n`-th `LibavCall` at that site block forever *inside* the scope (so `in_call` stays set), and `MEDIADIFF_TEST_WATCHDOG_LIMIT_MS=<ms>` shrinks the limit for that run only. The spawned real `mediadiff` must exit 66, JSON valid and containing the error entry. Add a `dir` variant: 3 files, file 2 stalls, assert files 1 and 3 still appear in the report and the exit is 66. Both variables are inert unless set and must go through the getenv shim (MSVC C4996 lint).

### Q12 (PERF-02). Content-pass cost, ratchet feasibility, slice choice

All numbers `[VERIFIED: scratch prototype vsc.c on the real D-16 reference `.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4` (mpeg4, 122,914,409 bytes, 18000 frames), single thread, linked 8.1 libs, i7-1185G7; instructions via `perf_event_open`, user space)`:

| configuration | wall | x realtime | retired instructions |
|---|---|---|---|
| decode only, 1 thread | 16.8 s | **35.7x** | 175.5e9 |
| decode + XXH3-128 frame hash (4.7 s) + 128-wide area thumbnail (3.7 s) + integer SSIM-vs-prev (1.0 s), 1 thread | 26.5-27.0 s | **22.6x** | **286.7e9** |
| same, automatic threads | 15.2 s | 39x | not measured |
| 60 s slice (first 1800 frames), same pass | 2.5 s | 24x | 28.65e9 |
| 20 s slice | 0.89 s | 22x | 9.54e9 |

Per frame: hash 0.26 ms (12 GB/s), thumbnail 0.20 ms, SSIM128 0.05 ms, decode 0.93 ms. Instructions scale linearly (0.477e9 per video second). **PERF-02 (>= 4x) is met with 5.6x margin on the gating reference while pinned to one thread**; D-11's "slowdown measured and reported" is the automatic-thread row (1.7x faster) versus the pinned row. Codec spread, decode only, 20 s 1080p testsrc2 clips (easy content): mpeg4 26.6x (1 thread) / 66.8x (auto), mpeg2 34.7x / 36x, x264 veryfast 10.1x / 34.8x, x265 ultrafast 7.6x / 26x; real H.264/HEVC content and 4K will be slower single-threaded `[ASSUMED]`, which is an honest cost of D-11/Finding 1, not hidden.

**Ratchet feasibility** `[VERIFIED: gh run view 35735099865]`: the audio ratchet (audio_full 47.3e9 instructions + plain leg) took 1 m 49 s on the designated runner (~0.45e9 instr/s under cachegrind) and the timeline ratchet 1 m 24 s; valgrind is installed by an existing CI step (ci.yml:604-609) and is **not** installed locally (`which valgrind` empty), so the local `--instructions` path cannot be reproduced here. The full ten minutes would be ~287e9 instructions = ~10.6 min under cachegrind (`[ASSUMED]` from the 0.45e9/s figure). **Recommend a 60-second slice** (first 1800 frames of the same generator, ~28.7e9 instructions, ~65 s), with the full-file wall-clock printed natively and never asserted (D-13). Implementation: `scripts/measure_video_perf.sh` mirroring `measure_audio_perf.sh` (plain = packet scan only, full = full content pass) and a bench target beside `mediadiff_audio_sweep`; new `PERF_BASELINE.txt` lines `video_plain_instructions` / `video_full_instructions`, transcribed from the designated leg by a human (first run prints the pasteable lines). The 1800-frame slice limit must be an explicit bench option (frame-count cap in the bench driver), not a different file, so the reference generator stays the D-16 one.

## Standard Stack

No new runtime dependency is required by the always-on content pass. One optional dependency (libvmaf) already exists as a manifest feature.

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| FFmpeg libavcodec/libavformat/libavutil | 8.1 (libavcodec 62.28.100) `[VERIFIED: ffversion.h, version.h]` | software video decode, demux, A53/HDR side data | Already linked; decode-only LGPL subset fixed by `vcpkg.json` (`avcodec, avformat, swscale, swresample, dav1d, zlib`) |
| libswscale | 9.5.100 `[VERIFIED: libswscale.pc]` | 128-wide luma thumbnail (`SWS_AREA|SWS_ACCURATE_RND|SWS_BITEXACT`) | Already a manifest feature (today used only for its version string, `src/util/version.cpp`) |
| dav1d | 1.5.4 `[VERIFIED: dav1d.pc]` | AV1 decode (`libdav1d` decoder) | Existing manifest feature; the only AV1 software decoder in the build |
| xxHash | 0.8.3 `[VERIFIED: libxxhash.pc]` | XXH3-128 per-frame digest via the streaming API (`XXH3_128bits_reset/update/digest`) | Already used for audio blocks; 12 GB/s measured |
| Catch2 | 3.15.3 `[VERIFIED: catch2.pc]` | unit + integration tests, CTest discovery | Project standard |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| libvmaf | 3.2.0 (vcpkg port, `supports: !windows`) `[VERIFIED: vcpkg/ports/libvmaf/vcpkg.json]` | `quality.vmaf`, model `vmaf_v0.6.1` | Only under `MEDIADIFF_WITH_VMAF` + manifest feature `vmaf`; Linux/macOS only |
| Python stdlib | 3.12.3 here; project floor 3.11 | `tools/gen_video_fixtures.py` H.264 I_PCM writer and MPEG-2 user-data inserter | Fixtures no encoder can express |
| Pinned generator ffmpeg | 9.0.1 (`scripts/ffmpeg_pin.json`) | corpus + proof-stream encodes, `noise`/`freezeframes` recipes | Generation time only; never linked or shipped (AR-03) |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Two producer threads + rendezvous (recommended) | Pull-style resumable `PacketSweep` | Single-threaded and simple to reason about, but rewrites the monolithic fused loop and forfeits 2x parallelism |
| SSIM-hysteresis frozen detector | Exact frame-hash equality | Measured unstable across GOP/encoder (Q5) |
| `idct=simple` pin | default AUTO | AUTO measured to differ x86 vs aarch64 for MPEG-2/4/MJPEG (Q2) |
| Integer int64 SSIM | `double` with `-ffp-contract=off`, or `__int128` | `double` risks FMA contraction on arm64/MSVC; `__int128` does not exist on MSVC (`exact_int.h` header comment) |
| Committed proof golden + artifact streams | Two-stage workflow shipping per-leg `mediadiff` binaries to compare against the designated leg's in-run output | Avoids committed hashes but uploads ~100 MB x 5 binaries and serializes legs; heavier than the project's existing CI-measures/human-transcribes convention |
| In-process watchdog thread + `_Exit` | Subprocess per file | Rejected by D-13 |

**Installation:** nothing new for the default build. VMAF build: `cmake -S . -B build/x64-linux-vmaf --preset x64-linux -DVCPKG_MANIFEST_FEATURES=vmaf -DMEDIADIFF_WITH_VMAF=ON` (the preset's binaryDir is `build/<presetName>`; use a distinct `-B`).

**Version verification:** all versions above were read from the installed headers/pkg-config files or the vcpkg port in this session; no registry lookup applies (vcpkg ecosystem).

## Package Legitimacy Audit

This phase installs **no new external package**. The only optional dependency, `libvmaf`, is already declared in `vcpkg.json` (feature `vmaf`) and its port fetches Netflix/vmaf `v3.2.0` with a pinned SHA512 (`vcpkg/ports/libvmaf/portfile.cmake`). `gsd-tools query package-legitimacy check` supports npm/pypi/crates only, so it does not apply to vcpkg.

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| libvmaf 3.2.0 | vcpkg port (SHA512-pinned GitHub release) | long-established (Netflix) | n/a | github.com/Netflix/vmaf | not checked by the seam (ecosystem unsupported) | Approved: already in the manifest; source pinned by hash |

**Packages removed due to [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none
Research-only tooling (qemu-user-static 8.2.2 and aarch64 cross-libc downloaded with `apt-get download` into the session scratchpad and unpacked with `dpkg-deb -x`; nothing installed system-wide) is not a project dependency.

## Architecture Patterns

### System Architecture Diagram

```
                 compare A B (both media, content on)              snapshot / inspect --content / dir (one side)
                              |                                                  |
            +-----------------+------------------+                               |
            | fingerprint_pair (probe/lockstep)  |                     fingerprint_input (one sweep)
            |  thread A          thread B        |                               |
            |  run_probe(A)      run_probe(B)    |                               |
            +---------|---------------|----------+                               |
                      v               v                                          v
        +-------------------------------------------------------------------------------+
        | run_packet_scan: ONE av_read_frame loop per file (read_frame_call_count == 1) |
        |  per packet: record -> parser -> audio_decode -> VIDEO_DECODE (new)           |
        |                                                                               |
        |  VIDEO_DECODE per video stream (skip ATTACHED_PIC):                           |
        |   clear AV_PKT_FLAG_DISCARD -> avcodec_send_packet -> receive_frame* -> drain |
        |   per decoded frame (ours, one at a time):                                    |
        |     1 hash: XXH3-128 over bytes_per_row x height x planes ‖ fmt ‖ dims        |
        |            (every Nth if --sample N) + ticks array                            |
        |     2 thumbnail: Y plane -> sws AREA 128xH (8-bit)  -> frozen / black state   |
        |     3 A53 presence, 4 first-frame HDR side data, 5 decode-error count         |
        |     6 (lockstep only) FrameTap::publish(frame)  ... blocks until released     |
        +-------------------------------|-----------------------------------------------+
                                        v
        ProbeResults::video_decode  ->  analyzers emit Measurements (hash chain, spans,
                                        presence, HDR first-frame) -> Fingerprint
                                        |
   lockstep consumer (calling thread): two-pointer time merge of (A_cur, B_cur) ->
     perceptual SSIM (thumbnail), psnr/ssim (native planes), vmaf (libvmaf ctx pair + self ctx)
     -> two-file Measurements appended to BOTH fingerprints (D-01)
                                        |
                                        v
            compare_fingerprints: hash (+ time-aligned locator, D-07) / span / tol (+ path preconditions, TRUST-04)
                                        |
                                        v
                       report (TTY / JSON / Markdown / JUnit)  -> exit code

 cli-side: Watchdog thread samples per-job Heartbeat{seq,in_call}; trip -> error entry, flush, _Exit(66)
```

### Recommended Project Structure
```
src/probe/video_decode.{h,cpp}     # VideoDecodeState: open flags, feed/drain, per-frame sinks, StreamVideoDecode result
src/probe/heartbeat.h              # Heartbeat counters + thread_local binding + RAII LibavCall (library side only)
src/probe/lockstep.{h,cpp}         # fingerprint_pair, FrameTap/rendezvous, time-merge scorer driver
src/probe/pass.h                   # + Pass::video_decode, ProbeResults::video_decode
src/analyzers/content/             # video_hash.cpp, video_detectors.cpp (frozen/black), perceptual.cpp, quality.cpp
src/analyzers/video/closed_captions.cpp ; hdr.cpp (first-frame arm)
src/util/ssim_int.h                # int64-only window SSIM + PSNR/log helpers (unit-testable in isolation)
src/cli/watchdog.{h,cpp}           # sampler thread, trip action, _Exit(66)
src/cli/worker_pool.cpp            # wait for finished+abandoned, detach abandoned workers
src/compare/hash.cpp               # time-aligned video locator, sampling_mismatch branch
src/compare/semantics.h|tol.cpp    # shared precondition helper + path-signature check (TRUST-04)
tools/gen_video_fixtures.py        # + H.264 I_PCM writer (SEI: MDCV, CLL, A53), MPEG-2 user-data inserter
scripts/gen_video_proof.sh, scripts/measure_video_perf.sh, tools/bench/ video sweep target
tests/golden/VIDEO_PROOF_CHAINS.txt, tests/golden/PERF_BASELINE.txt (+ video lines)
docs/checks/<8 new ids>.md ; docs/schema/report-1.0.json (+ skip reasons)
```

### Pattern 1: Fused per-stream sink bundle (mirror `AudioDecodeState`)
**What:** `ensure_initialized(codecpar)` once per stream, `feed_packet(pkt)`, `finalize()`; every sink consumes the frame and discards it; the result holds sink outputs only, never pixels. Decoder chosen once before the sweep; attempt flag; consecutive-error bound (audio precedent `kMaxAudioDecodeErrorsPerStream = 64`, `audio_decode.h`); per-frame re-validation of geometry/pix_fmt with a latched stop token (audio `kDecodeStopChannelsChanged` precedent) so a mid-stream resolution change cannot corrupt the single-valued `normalization` precondition.
**When to use:** every video stream except attached pictures.

### Pattern 2: One-sided measurements are snapshot-safe; two-file measurements are live-only
One-file values (`frame_hash`, `frozen_runs`, `black_runs`, `closed_captions`, HDR arm) are ordinary Measurements with per-frame arrays so `compare A B` and `compare A.snap.json B` produce identical evidence (Phase 6 D-03). Two-file values never enter a snapshot.

### Pattern 3: Precondition evidence, not new comparators
`content.video.frame_hash` evidence mirrors the audio keys (`decode_path_class`, `sampling_state`, `normalization`). Suggested values: `decode_path_class` = `class1` or `class2 <compose_decode_path_signature()>` (and class 2 whenever `decode_error_count > 0`); `sampling_state` = `full` | `sampled:N` | the existing truncated token; `normalization` = `cropped;fmt=<folded pix_fmt>;dims=<w>x<h>`. `compare_hash` must map a `sampling_state` mismatch between `full`/`sampled:N` (neither truncated) to `SkipReason::sampling_mismatch` (exists in the enum, schema and JUnit, produced nowhere today; hash.cpp:191-199 sends every mismatch to `hash_incomparable`).

### Pattern 4: Time-aligned locator (D-07) in `compare/hash.cpp`
Extend `compute_divergence` for video chains carrying a timestamp array: same two-pointer merge as the live scorer, reporting `missing_from_candidate` / `extra_in_candidate` frames separately from `differs`, ranges merged at 1-frame gaps, total count. Needs an additive `HashChain` field for per-element ticks and time base (serializer.cpp:279-291 and :458-483 read/write `block_digests`/`element_stride` optionally; add the new arrays the same way so no pre-existing golden changes). Snapshot size: 32-hex digests at 15,000 frames is ~0.6 MB of text; truncating per-frame digests to 64 bits halves it (chain digest stays 128-bit) -- planner's call as in Phase 6.

### Anti-Patterns to Avoid
- **Reading `linesize`, or hashing decoder padding** (Pitfall 4): use `av_image_get_linesize(fmt, width, plane)` per row and the cropped frame dimensions.
- **Branching the frozen rule on decode class** (spans would depend on the machine).
- **Taking the VMAF baseline self-score as 100**, or PSNR as infinity (`nlohmann::json` cannot round-trip an infinity; cap it).
- **Letting libav choose threads or IDCT** (Findings 1-2).
- **Calling `std::exit` with a stuck worker alive**, or joining it.
- **A `tol` precondition that is unreachable and untested**: under D-01 live runs can never mismatch, so the gate needs a synthetic-evidence unit test or it is a gate that never fired.
- **Regenerating fixtures and committing the rewritten corpus digest** (MEMORY.md trap); new fixtures append digest lines, and `lint_corpus_digest_provenance.sh` guards rewrites.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| 128-bit hashing of frame bytes | own hash | xxHash `XXH3_128bits_*` streaming over rows | already linked, 12 GB/s, used for audio |
| bytes per row / plane geometry | manual `width * bps / 8` tables | `av_image_get_linesize`, `AVPixFmtDescriptor` (`log2_chroma_*`) | subsampling and packed formats |
| display crop | manual crop arithmetic | `AV_CODEC_FLAG_UNALIGNED` so the decoder's own `av_frame_apply_cropping` is exact (decode.c:765) | left-crop alignment rounding (frame.c:797-817) |
| downscale | own area filter | `sws_scale` with pinned flags | only the flags need pinning; result SIMD-invariant (measured) |
| A53 / HDR extraction | SEI parsing in ParserScan | decoder side data `AV_FRAME_DATA_A53_CC`, mastering/CLL side data | EXT-01 is deferred |
| VMAF | own model | libvmaf built-in `vmaf_v0.6.1` | requires the exact trained model |
| H.264 encoding for fixtures | a real encoder | I_PCM-only writer (raw samples) | no GPL, no entropy/transform, bit-exact |
| Process exit with stuck threads | custom signal tricks | flush + `std::_Exit` | verified |

**Key insight:** SSIM and PSNR *are* hand-rolled by requirement (in-tree, CONTENT-08), and that is the one place a custom implementation is justified -- keep it int64-only, isolated in a header with its own known-answer tests (identical windows -> exactly 1, the flat-window floor, a hand-computed window).

## Common Pitfalls

### Pitfall 1: Missing decoder drain at EOF
**What goes wrong:** the last DPB-depth frames (B-frame reorder, frame-threading latency) are never hashed; chains of two equal files still match, so nothing flags it.
**How to avoid:** at `AVERROR_EOF` send `avcodec_send_packet(ctx, nullptr)` and receive until `AVERROR_EOF`, also on a truncated (partial) sweep. Test: frame count equals the demuxer's packet count for a B-frame fixture.

### Pitfall 2: Forgetting to clear `AV_PKT_FLAG_DISCARD`
**What goes wrong:** a trimmed MP4 loses leading frames (87 vs 88 measured) and never equals its own remux. **How to avoid:** clear on the scratch packet before send (Q1); test with an edit-list fixture and its MKV/remux copy.

### Pitfall 3: Decoder crop rounding
**What goes wrong:** without `AV_CODEC_FLAG_UNALIGNED` the left crop is dropped and the hashed rectangle is wider than the display rectangle (measured 60x54 instead of 54x54). **How to avoid:** set the flag; add an H.264 I_PCM fixture with all four crops non-zero and assert `dims=54x54`.

### Pitfall 4: Default thread count / IDCT / XviD switch
See Findings 1-2. **Warning signs:** a golden that passes on the dev box and fails on a runner with a different core count; MPEG-2/4 hashes that change after `idct` alone.

### Pitfall 5: Class-1 claim on an error-bearing stream
Decode errors void cross-arch conformance (Q2). Degrade to class 2 when `decode_error_count > 0`.

### Pitfall 6: Sampling mismatch reported as `hash_incomparable`
`compare_hash` has no `sampling_mismatch` branch today; CONTENT-03's `skipped:sampling_mismatch` will silently come out wrong unless added. Also keep the truncated-sampling check first (hash.cpp:152-170 ordering).

### Pitfall 7: CI exclusion-count guard
`EXPECTED_EXCLUDED_COUNT=5` (ci.yml:531) and `EXCLUDED_TEST_REGEX` (ci.yml:530) name the designated-leg-only goldens exactly. Adding fixture-derived frame-hash goldens without updating both breaks every non-designated leg; and the designated-leg "skipped test" guard (ci.yml:535-556) must cover them too.

### Pitfall 8: libvmaf defaults and platforms
Self-score is not constant (Q8); the port is `!windows`; the default build must keep `vmaf` absent (`integration.vmaf_absent`, test #1250); `vmaf_v0.6.1` needs `enable_float` off only.

### Pitfall 9: Watchdog lifetime hazards
Detached workers outliving the stack/heap state they reference; `WorkerPool::run_indexed` joining a stuck thread; `exit()` instead of `_Exit`; writing the report after `_Exit`. Heap-allocate and leak the shared job state (Q11). A blocked rendezvous must not count as a stalled libav call.

### Pitfall 10: Lockstep edge cases
One side has no video stream or an undecodable one; one side ends first (tail frames unpaired, counted); consumer error while producers are blocked (`close()` must release them); `--sample` interplay (pairs scored on every Nth *pair*); byte-identical JSON regardless of which thread finishes first (assemble measurements after both joins, in registry order).

### Pitfall 11: Raw ES timestamps
`.h264`/`.m2v` elementary streams deliver `pts = AV_NOPTS_VALUE` (measured) -> D-02/D-07 index fallback must be exercised by a fixture, and the evidence must record the fallback.

### Pitfall 12: Attached pictures and multi-video files
Cover art is a one-packet video stream (`AV_DISPOSITION_ATTACHED_PIC`); hashing/scoring it as the video is wrong. Skip it.

### Pitfall 13: `inf`/NaN in JSON
Cap PSNR; libvmaf can return -HUGE_VAL-style sentinels in edge cases (audio precedent `kNonFiniteLoudnessReadoutSentinel`).

## Code Examples

Patterns below are verified by the scratch prototypes named; the libav identifiers were all read from the linked headers.

### Decoder open and feed (verified in `vdump.c` / `vsc.c`)
```cpp
// Source: scratch probes against build/x64-linux/vcpkg_installed (FFmpeg 8.1)
AVCodecContext* cc = avcodec_alloc_context3(codec);       // codec = avcodec_find_decoder(par->codec_id)
avcodec_parameters_to_context(cc, par);
cc->pkt_timebase = stream->time_base;
cc->flags |= AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_UNALIGNED;
cc->thread_count = 1;                                      // Finding 1 (ProbeOptions::video_decode_threads overrides in tests)
av_opt_set(cc, "idct", "simple", AV_OPT_SEARCH_CHILDREN);  // FF_IDCT_SIMPLE == 2 (avcodec.h:1541)
cc->max_pixels = kMaxVideoPixels;                          // hostile-dimension bound (Security)
// per packet:
pkt->flags &= ~AV_PKT_FLAG_DISCARD;                        // after make_packet_record(), before send
avcodec_send_packet(cc, pkt);
while (avcodec_receive_frame(cc, fr) >= 0) { /* sinks */ av_frame_unref(fr); }
// EOF: avcodec_send_packet(cc, nullptr); drain until AVERROR_EOF
```

### Per-frame hash (XXH3 streaming, cropped rows)
```cpp
XXH3_state_t* st = XXH3_createState(); XXH3_128bits_reset(st);
for (int p = 0; p < planes && fr->data[p]; ++p) {
  const int ph = (p == 1 || p == 2) ? AV_CEIL_RSHIFT(fr->height, desc->log2_chroma_h) : fr->height;
  const int bpr = av_image_get_linesize(static_cast<AVPixelFormat>(fr->format), fr->width, p);
  for (int y = 0; y < ph; ++y) XXH3_128bits_update(st, fr->data[p] + size_t(y) * fr->linesize[p], bpr);
}
XXH3_128bits_update(st, folded_pix_fmt_name.data(), folded_pix_fmt_name.size());
const int32_t dims[2] = {fr->width, fr->height}; XXH3_128bits_update(st, dims, sizeof dims);
```
(D-05: no timestamp in the hash; timestamp goes to the parallel array.)

### Integer 8x8 SSIM window (int64-only)
```cpp
// Source: scratch vsc.c (verified); constants = round((0.01*255)^2*4096), round((0.03*255)^2*4096)
constexpr int64_t kC1 = 26634, kC2 = 239708, N = 64;
int64_t vx = N*sxx - sx*sx, vy = N*syy - sy*sy, cxy = N*sxy - sx*sy;
int64_t n1 = 2*sx*sy + kC1, n2 = 2*cxy + kC2, d1 = sx*sx + sy*sy + kC1, d2 = vx + vy + kC2;
int64_t num = n1*n2, den = d1*d2;                  // each < 2^60
int64_t q = num / den, r = num % den;              // r >= 0 for num >= 0; handle num < 0 with sign-symmetric floor
int s = std::max(0, bit_length(den) - 28);
int64_t frac_q24 = ((r >> s) << 24) / (den >> s);  // deterministic, error < 1e-8
int64_t ssim_q24 = (q << 24) + frac_q24;           // identical windows: num == den -> exactly 1<<24
```

### Thumbnail context (verified SIMD == C)
```cpp
SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_GRAY8 /* Y plane wrapped */, 128, th, AV_PIX_FMT_GRAY8,
                                 SWS_AREA | SWS_ACCURATE_RND | SWS_BITEXACT, nullptr, nullptr, nullptr);
const uint8_t* src[4] = {fr->data[0]}; int ss[4] = {fr->linesize[0]};
uint8_t* dst[4] = {thumb}; int ds[4] = {128};
sws_scale(sws, src, ss, 0, h, dst, ds);            // th = ((128*h/w)+1) & ~1; >8-bit: GRAY10LE/12LE/16LE then shift to 8-bit
```

### Frozen detector (hysteresis, fixed constants)
```cpp
inline constexpr int64_t kFrozenEnterQ = 999500;   // SSIM > 0.9995 (micro units) starts a run
inline constexpr int64_t kFrozenContinueQ = 995000;// SSIM > 0.995 continues it (absorbs I-frame refresh dips to 0.9984)
inline constexpr int kFrozenMinFrames = 3;         // doc 06 §2.3
// state: run_start (frame index or none); per consecutive pair (i-1, i): if !in_run && s > enter: start = i-1;
// if in_run && s <= continue: close [start, i-1] if length >= min. Spans carry frame ticks via the timestamp array.
```

### H.264 I_PCM writer skeleton (verified decodable by the linked libavcodec; frames bit-exact)
```python
# Source: scratch h264pcm.py (scratchpad), to be folded into tools/gen_video_fixtures.py
def sps(wmb, hmb, crop=None):  # profile 66, poc type 2, frame_mbs_only=1, optional frame_cropping (left,right,top,bottom in crop units)
    b = BW(); b.u(8,66); b.u(8,0); b.u(8,30)
    b.ue(0); b.ue(0); b.ue(2); b.ue(1); b.u(1,0); b.ue(wmb-1); b.ue(hmb-1); b.u(1,1); b.u(1,1)
    if crop: b.u(1,1); [b.ue(v) for v in crop]
    else: b.u(1,0)
    b.u(1,0); b.trailing(); return nal(7, 3, b.bytes())
# PPS: CAVLC, deblocking_filter_control_present=1. IDR slice: first_mb ue0, slice_type ue7, pps ue0, frame_num u4=0, idr_pic_id ue(f%2),
#   no_output_of_prior_pics=0, long_term_ref=0, slice_qp_delta se0, disable_deblocking_filter_idc ue1; per macroblock: ue(25), byte-align, 384 raw bytes.
# SEI NAL type 6: payload 137 (mastering display: G,B,R (x,y) u16 each, WP (x,y) u16, max u32, min u32), 144 (max CLL u16, max FALL u16),
#   4 (B5 0031 "GA94" 03 <0x40|n> FF n*(FC d1 d2) FF).  nal(): insert 0x03 after two zeros when next byte <= 3.
```

### MPEG-2 closed-caption insertion (verified)
```python
# before the first slice start code (00 00 01 01..AF) following each picture header (00 00 01 00):
ud = b'\x00\x00\x01\xb2' + b'GA94' + b'\x03' + bytes([0x40 | n]) + b'\xff' + b''.join(bytes([0xFC, d1, d2]) for ...) + b'\xff'
```

### Fixture recipes (verified with the pinned ffmpeg 9.0.1, `-flags +bitexact -fflags +bitexact`)
```sh
# one-frame corruption (intra-only -> exactly one frame differs)
ffmpeg -f lavfi -i testsrc2=size=320x240:rate=25:duration=4 -c:v huffyuv hf.mkv
ffmpeg -i hf.mkv -c copy -bsf:v "noise=amount='if(eq(n,40),50,0)'" hf_corrupt.mkv     # frames 40-40 differ
# MPEG-4 (-bf 0 -g 25): same recipe -> frames 40-49 differ (to next I-frame)
# dropped frame (time-aligned locator proof): -bsf:v "noise=drop='eq(n,40)'"            # MJPEG/HuffYUV: index alignment says 40-98, time alignment says frame 40 missing
# frozen segment:
ffmpeg -f lavfi -i testsrc2=size=352x288:rate=25:duration=6 -filter_complex "[0:v]split[a][b];[a][b]freezeframes=first=51:last=100:replace=51[v]" -map "[v]" -c:v mpeg4 -q:v 5 -g 12 frozen.mp4
# black in both ranges: concat testsrc2 / color=c=black / testsrc2, then scale=out_range=tv|pc,format=yuv420p, -color_range tv|pc
# edit-list trim (D-06): ffmpeg -ss 0.5 -i src_bf.mp4 -c copy trim.mp4  (src: mpeg4 -bf 2 -g 12), remux copy: -c copy trim.mkv
```
(`amount=-1` in the `noise` BSF, the default when unset, corrupts *every* packet; use `0` for "none".)

### Heartbeat guard and rendezvous (design sketch, not measured)
```cpp
struct LibavCall { Heartbeat* hb; explicit LibavCall(Heartbeat* h):hb(h){ if(hb){hb->in_call.fetch_add(1); hb->seq.fetch_add(1);} }
                   ~LibavCall(){ if(hb){hb->in_call.fetch_sub(1); hb->seq.fetch_add(1);} } };
struct FrameSlot { std::mutex m; std::condition_variable cv; AVFrame* frame=nullptr; bool consumed=true, closed=false;
  bool publish(AVFrame* f){ std::unique_lock l(m); frame=f; consumed=false; cv.notify_all(); cv.wait(l,[&]{return consumed||closed;}); return !closed; } };
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Frame-level hash equality as "frozen" | SSIM hysteresis on 128-wide luma thumbnail | this phase (doc 06 §2.3 amended) | spans identical across encoders/GOPs |
| Decoder `thread_count=auto` | `thread_count=1` for the hashing decode | this phase | only setting that is deterministic on corrupt streams |
| `AV_CODEC_FLAG_OUTPUT_CORRUPT` / default discard of edit-list-trimmed frames | clear `AV_PKT_FLAG_DISCARD` on the packet | FFmpeg >= 7.x `discard` flag semantics | trimmed MP4 == its remux |
| vcpkg `x-gha` binary cache | artifact upload/download of proof streams; vcpkg cache via lukka/run-vcpkg | project CLAUDE.md | the proof design uses plain `actions/upload-artifact@v4` |

**Deprecated/outdated:**
- CONTEXT/ROADMAP note that `video.closed_captions` needs GPL x264: stale, an MPEG-2 user-data insert and an I_PCM H.264 writer both work with no encoder.
- Doc 06 §1 "auto threads": superseded by the determinism requirement.

## Recorded Amendments (ROADMAP / REQUIREMENTS / claude_docs)

The planner must include a docs task that records each of these (same style as PERF-03/04 amendments):

| Target | Text now | Amend to | Driver |
|--------|----------|----------|--------|
| REQUIREMENTS CONTENT-10, ROADMAP SC4 | `quality.*` vs snapshot skipped:requires_media, stored scores | live two-file only; snapshot vs media -> `skipped:requires_media`; no stored scores | D-01 |
| REQUIREMENTS CONTENT-05, ROADMAP SC2 | frame-count mismatch | overlapping time range, pairing by time with index fallback, unpaired tails counted | D-02 |
| ROADMAP SC3 | (per D-04) | as CONTEXT D-04 | D-04 |
| REQUIREMENTS CONTENT-01 | `bytes_per_row x height` | + per-frame timestamp array, decoder flags (UNALIGNED, idct=simple, 1 thread, DISCARD cleared) | D-05, Q1-Q4 |
| claude_docs/06 §2.1, 01 §7 | frame hash / determinism prose | match the pinned decoder settings and class-2-default rule | D-09 |
| claude_docs/06 §1 | auto threads | pinned to 1 | Q4 |
| claude_docs/06 §2.3 | exact-hash frozen rule | hysteresis SSIM (enter > 0.9995, continue > 0.995, min 3 frames) | Q5 |
| ROADMAP (VIDEO-11 note) | closed_captions needs GPL fixtures | fixtures built without GPL | Q6 |
| REQUIREMENTS CONTENT-09 | `quality.vmaf` | Linux/macOS only (`!windows` port); Windows reports `skipped:unsupported_platform`-style reason (planner names it) | Q8 |
| REQUIREMENTS PERF-02 | >= 4x realtime | measured baseline 22.6x on the D-16 reference; ratchet on a bounded slice | Q12 |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | qemu-user aarch64 (8.2.2) executing the pinned arm64 ffmpeg reproduces native arm64 decode output | Q2 | the local cross-arch evidence is weaker; CI proof streams remain the authoritative arbiter |
| A2 | FFmpeg 8.1 (linked) and 9.0.1 (pinned generator) decode identically for the nine pinned codecs | Q2/Q3 | goldens derived from the generator decode could differ from the linked decoder; CI golden catches it |
| A3 | `actions/download-artifact@v4` behaviour (per-job artifact names, cross-job within one run) | Q3 | CI handoff needs rework |
| A4 | `std::_Exit` on MSVC terminates without joining/destructors as on glibc | Q11 | Windows watchdog test would hang; needs a Windows-leg check |
| A5 | cachegrind instruction count scales consistently with wall time across runners | Q12 | ratchet threshold tuning |
| A6 | x265 / SVT-AV1 encodes are asm-deterministic across runner CPUs | Q3 | proof-stream inputs differ per leg -> false golden mismatch; mitigated by hashing the produced bitstream md5 first |
| A7 | libvmaf 3.2.0 is bit-exact across x86 SIMD/aarch64 NEON for `vmaf_v0.6.1` | Q8 | `quality.vmaf` value differs by leg; mitigate by quantizing (planner chooses precision) and testing on designated leg only |
| A8 | libvmaf builds on macOS via the vcpkg port | Q8 | optional feature only; no impact on default build |
| A9 | Frozen thresholds (0.9995 / 0.995) generalize beyond synthetic testsrc2 content | Q5 | false frozen spans on real low-motion video (a P0 class); the thresholds must stay configurable in `mediadiff.toml` or be reviewed against a real-content sample |
| A10 | Excluding `AV_DISPOSITION_ATTACHED_PIC` video streams is the right product behaviour | Pitfall 12 | cover-art-only files report no video content |
| A11 | 300 s is an adequate default watchdog stall bound | Q11 | spurious trips on slow media, or too slow to recover |
| A12 | The per-frame memory estimate (one decoded frame + 128-wide thumbnail + 2 slots) stays under the budget at 8K | Q10 | OOM on huge inputs; `max_pixels` bound mitigates |
| A13 | `gsd_run`/CI shell tooling (valgrind) installed by CI only | Environment | local ratchet reproduction impossible without install |
| A14 | Decode-error counting as class-2 trigger is the desired policy (Open Question 3) | Pitfall 5 | error-bearing streams get weaker determinism claims |

## Open Questions

1. **Thread default vs CONTEXT discretion.**
   - Known: CONTEXT leaves thread count to discretion; measured corrupt-stream non-determinism at fixed N >1.
   - Unclear: whether the user accepts a fixed 1-thread production decode (22.6x realtime measured, so PERF-02 is unaffected).
   - Recommendation: pin 1; keep `video_decode_threads` in `ProbeOptions` for TRUST-07 tests only.
2. **Range normalization in perceptual.** Compare limited vs full range frames raw (report a real difference) or normalize? Recommendation: do not normalize; a range flip is a finding (VIDEO-xx already reports it), and normalizing would hide it from the perceptual score.
3. **Error-bearing streams and class.** Recommend `decode_error_count > 0` forces class 2 (A14).
4. **Skip-reason naming.** `not_requested`, `sampling_conflict`, optionally `path_incomparable` are additive `SkipReason` members touching model.h, junit.cpp, schema, test_measurement_provenance.cpp. Planner to confirm names with the user-facing docs.
5. **Final frozen/black thresholds.** Synthetic-only evidence (A9); recommend shipping the measured constants and recording a tuning pass as a follow-up.
6. **VMAF on Windows.** Accept `skipped` with an explicit platform reason, or pursue a custom port? Recommendation: skip with reason, document.
7. **PSNR weighting.** Overall PSNR as mean of per-plane PSNR vs libvmaf's luma-only convention; recommend luma + per-plane min/mean exposed, cap `(6*bpc)+12`.
8. **AV1 HDR fixture policy.** libsvtav1 availability differs per pin; recommend the H.264 I_PCM SEI fixture as the required HDR-arm proof and AV1 as optional.
9. **Watchdog constant.** 300 s assumed (A11); confirm or make `MEDIADIFF_WATCHDOG_SECONDS` test-hook only.
10. **Proof-golden sequencing.** `VIDEO_PROOF_CHAINS.txt` can only be filled after the first CI run measures the five legs (human transcription, per the project convention); plan must order: land proof job in report-only mode, transcribe, then promote to a failing gate.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| gcc/g++ | build | yes | 13.3.0 | -- |
| cmake | build | yes | 3.28.3 | -- |
| ninja | build | yes | present | -- |
| Python 3 | fixture writers, scripts | yes | 3.12.3 | -- |
| Pinned ffmpeg (linux) | corpus + proof streams | yes | 9.0.1 | `scripts/install_pinned_ffmpeg.sh` |
| nasm | vcpkg FFmpeg build | yes | 2.16.01 (~/.local/bin) | -- |
| meson | dav1d/libvmaf builds | no | -- | vcpkg provisions its own meson tool |
| valgrind | PERF ratchet (cachegrind) | no (local) | -- | installed by CI (ci.yml:604-609); cannot reproduce ratchet locally without `apt install valgrind` |
| perf binary | profiling | no | -- | `perf_event_open` via Python was used for research timing |
| qemu-user | local cross-arch evidence | no (scratch copy only) | 8.2.2 | CI proof job is authoritative |
| docker | optional | yes | present | -- |
| gh CLI | CI step timings | yes | present | -- |
| libvmaf | `quality.vmaf` | not built locally | 3.2.0 via port | build with `-DVCPKG_MANIFEST_FEATURES=vmaf` (Linux/macOS) |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** valgrind (CI installs), meson (vcpkg-provisioned), libvmaf (optional feature).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 3.15.3 via CTest |
| Config file | `CMakeLists.txt` (`catch_discover_tests`), `CMakePresets.json` |
| Quick run command | `ctest --test-dir build/x64-linux -R '<regex>' --output-on-failure` |
| Full suite command | `ctest --preset x64-linux --output-on-failure` (1251 tests, ~23 s measured) |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CONTENT-01 | hash over cropped rows; DISCARD cleared; drain; crop fixture dims 54x54 | integration | `ctest --test-dir build/x64-linux -R video_hash` | Wave 0 |
| CONTENT-02 | first divergent frame 40, ranges merged at 1-frame gaps, total count | integration | `-R video_locator` | Wave 0 |
| CONTENT-03 | `sampled:N` evidence; mismatch -> `sampling_mismatch` | unit | `-R sampling_mismatch` | Wave 0 |
| CONTENT-04 | thumbnail SSIM identical -> exactly 1; swscale flags pinned | unit + integration | `-R perceptual` | Wave 0 |
| CONTENT-05 | unequal frame counts pair by time; raw ES index fallback | integration | `-R lockstep_pairing` | Wave 0 |
| CONTENT-06 | frozen span identical across encodes; black tv/pc | integration | `-R video_detectors` | Wave 0 |
| CONTENT-07 | one sweep (`read_frame_call_count == 1`) with video+audio+hash | integration | `-R single_sweep` | extend existing |
| CONTENT-08 | SSIM/PSNR known answers, PSNR cap | unit | `-R ssim_int` | Wave 0 |
| CONTENT-09 | vmaf absent in default build; `--sample` refusal; present build score | integration | `-R vmaf` | extend `integration.vmaf_absent` |
| CONTENT-10 | quality vs snapshot -> `requires_media` | integration | `-R quality_snapshot` | Wave 0 |
| CONTENT-11 | one frame in flight per side; close releases producers; byte-identical JSON | integration/stress | `-R lockstep` | Wave 0 |
| VIDEO-11 | A53 present on MPEG-2 and H.264 fixtures; `requires_decode` under `--no-content` | integration | `-R closed_captions` | Wave 0 |
| VIDEO-09 (first frame) | SEI MDCV/CLL first-frame arm; stream-level wins | integration | `-R hdr_first_frame` | Wave 0 |
| TRUST-04 | precondition mismatch -> explicit skip (synthetic evidence) | unit | `-R tol_path_signature` | Wave 0 |
| TRUST-07 | chains identical at 1/4/16 threads for clean streams | integration | `-R video_thread_invariance` | Wave 0 |
| PERF-02 | full pass >= 4x realtime; ratchet within threshold | script + CI | `scripts/measure_video_perf.sh` | Wave 0 |
| D-12/D-13 | watchdog trips, flushes, exits 66 with stuck worker | integration (test hook) | `-R watchdog` | Wave 0 |

### Sampling Rate
- **Per task commit:** the matching `ctest -R` regex plus lint scripts named in ci.yml (see MEMORY: run CI lints per plan)
- **Per wave merge:** `ctest --preset x64-linux --output-on-failure`
- **Phase gate:** full suite green, DOC-03 gate green, designated-leg goldens verified via the differential recipe in MEMORY.md, CI green on all five legs

### Wave 0 Gaps
- [ ] `tests/unit/test_ssim_int.cpp`, `test_sampling_mismatch.cpp`, `test_tol_path_signature.cpp`, `test_gen_video_fixtures.cpp` (Python writer tests, pattern of `tests/unit/test_gen_he_aac.cpp`)
- [ ] `tests/integration/test_video_hash.cpp`, `test_video_locator.cpp`, `test_video_detectors.cpp`, `test_lockstep.cpp`, `test_video_thread_invariance.cpp`, `test_closed_captions.cpp`, `test_hdr_first_frame.cpp`, `test_watchdog.cpp`
- [ ] New fixtures appended to `tests/fixtures/GENERATOR_MANIFEST.json` and `CORPUS_DIGEST` (append only; do not regenerate existing lines)
- [ ] `tests/golden/VIDEO_PROOF_CHAINS.txt` and PERF_BASELINE video lines (human-transcribed after first CI run)
- [ ] `docs/checks/*.md` for the eight new check IDs (DOC-03 gate) and schema additions (`docs/schema/report-1.0.json`)
- [ ] `ci.yml`: `video-proof-streams` job, updated `EXCLUDED_TEST_REGEX` and `EXPECTED_EXCLUDED_COUNT`, `MEDIADIFF_REQUIRE_VIDEO_PROOF` guard
- [ ] Framework install: none (Catch2 already present)

## Security Domain

Applicable: security enforcement is on by default; mediadiff parses untrusted media, so the decode path is the attack surface. ASVS L1 is the assumed level `[ASSUMED]`.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | -- (local CLI) |
| V3 Session Management | no | -- |
| V4 Access Control | no | -- |
| V5 Input Validation | yes | bounded `max_pixels`, frame/packet caps, consecutive-error bound, per-frame geometry revalidation, libav return-code checking via `expected<T,Error>` |
| V6 Cryptography | no (hashes are identity, not security) | XXH3 only for fingerprinting; document non-cryptographic |
| V12 Files / Resources | yes | memory budget accounting for one frame in flight per side; watchdog for decoder hangs |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Decode bomb (huge dimensions / frame count) | DoS | `cc->max_pixels`, frame cap, existing budget; thumbnail and hash streaming avoid frame copies |
| Decoder hang / pathological stream | DoS | heartbeat watchdog, flush + `_Exit(66)` (D-12/D-13) |
| Hostile SEI / user-data (A53, MDCV, CLL) | Tampering | read only fixed-size libav-parsed side data; bounds-check `size`; never parse raw SEI ourselves |
| Mid-stream resolution / pix_fmt change | Tampering / crash | latched stop token; single-valued normalization evidence |
| Test-hook env vars (`MEDIADIFF_WATCHDOG_*`, thread override) abused in production | Elevation | hooks inert unless a test-only build/compile flag or documented env shim, per the `dir.cpp:369` precedent; never widen budgets |
| Detached stuck worker touching freed state | Memory safety | leak heap-allocated shared job state on trip; process exits immediately via `_Exit` |
| libvmaf input handling | DoS | only built-in model; frames passed as validated pictures of equal dimension |

## Sources

### Primary (HIGH confidence)
- FFmpeg n8.1 source under `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/` (decode.c, frame.c, h264 SEI, mpeg12dec, avcodec.h) read this session
- Installed headers and `.pc` files under `build/x64-linux/vcpkg_installed/` (versions above)
- Repo files read this session: `src/probe/*`, `src/compare/hash.cpp`, `src/core/{value,model,exact_int}.h`, `src/analyzers/video/hdr.cpp`, `src/cli/commands/{compare,dir}.cpp`, `.github/workflows/ci.yml`, `scripts/ffmpeg_pin.json`, `scripts/install_pinned_ffmpeg.sh`, `tests/golden/PERF_BASELINE.txt`, `vcpkg/ports/libvmaf/*`, `claude_docs/06-content-and-size-analysis.md`
- Scratch experiments in the session scratchpad (vdump, vsc, h264pcm, timing, watchdog `_Exit` tests) -- all measured values in Q1-Q12

### Secondary (MEDIUM confidence)
- CI step timings via `gh` for run durations used in the proof-job sizing

### Tertiary (LOW confidence)
- Items in the Assumptions Log (A1-A14)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies; versions read from installed files
- Architecture: MEDIUM-HIGH -- lockstep and watchdog designs are prototyped in parts (rendezvous and `_Exit` semantics measured) but not implemented end to end
- Pitfalls: HIGH -- most are reproduced by measurement in this session
- Cross-arch determinism: MEDIUM -- local evidence via qemu; the authoritative proof is the new CI job

**Research date:** 2026-09-30
**Valid until:** 2026-10-30 (vcpkg/FFmpeg baselines are pinned; revisit if the baseline or ffmpeg pin moves)

