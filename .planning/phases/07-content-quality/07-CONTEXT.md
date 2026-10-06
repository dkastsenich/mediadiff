# Phase 7: Content & Quality - Context

**Gathered:** 2026-09-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 7 delivers the video decode path and closes v1 (ROADMAP Phase 7; source doc
`claude_docs/06-content-and-size-analysis.md`, minus `size.*`, which shipped in Phase 3).
Requirements: VIDEO-11, CONTENT-01…11, TRUST-04, TRUST-07, PERF-02. Two carried obligations ride
with them: VIDEO-09's first-frame HDR side-data arm (Human Decision 1, 2026-09-13) and the decode
watchdog owed by Phase 6's security acceptance (T-06-34 / AR-6-05 / `WINDOWS.md` #43).

The phase produces two kinds of measurement:

- **One-file measurements, stored in fingerprints and snapshots:** `content.video.frame_hash`,
  `content.video.frozen_runs`, `content.video.black_runs`, `video.closed_captions` presence, and
  the first-frame arm of `video.hdr.*`. One video decode sweep per file feeds all of them, fused
  into the existing sweep the way `Pass::audio_decode` already is.
- **Two-file measurements, live compare only:** `content.video.perceptual` (SSIM on 128-wide luma)
  and the opt-in `quality.psnr`, `quality.ssim` and `quality.vmaf`. They are computed with baseline
  and candidate decoded in lockstep, one frame in flight per side (CONTENT-11).

Also in scope: `--sample N` (CONTENT-03), TRUST-04's path preconditions on `±tol` checks,
TRUST-07's 1/4/16-thread determinism suite, PERF-02's content-pass measurement, and the watchdog.

Not in this phase: `--hwaccel cuda` / NVDEC (HW-01…03, v2), per-frame Dolby Vision RPU diffing
(EXT-03), an SEI T.35 caption scan in ParserScan (EXT-01), and `expect.frame_rate` (EXT-04).

</domain>

<decisions>
## Implementation Decisions

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

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` — Phase 7: goal, SC1–SC5, and the cross-cutting placements of VIDEO-09's first-frame arm, VIDEO-11, TRUST-04 and TRUST-07.
- `.planning/REQUIREMENTS.md` — VIDEO-09, VIDEO-11, CONTENT-01…11, TRUST-04, TRUST-07 and PERF-02, plus the PERF-03/PERF-04 ratchet amendments as precedent for recording amendments in the open.
- `.planning/PROJECT.md` — Key Decisions (decoder classes proven, not assumed; VMAF pin; hashing `bytes_per_row × height`; SSIM on 128-wide luma; range-aware black detection; the FFmpeg 8.1 pin and its swscale note) and Conventions (fail-first fixtures, DOC-03, the canary pair).

### Design docs (source of truth, amended where noted)
- `claude_docs/06-content-and-size-analysis.md` — §1 decode path, §2 `content.video.*`, §3 `quality.*`, §5 performance and memory, §6 fixtures and acceptance. Amended here by D-01, D-02, D-05, D-07 and the `--threads` note.
- `claude_docs/01-core-concepts.md` — §3 semantics (hash preconditions, span merge gap), §7 determinism classes (amended by D-09), §8 snapshot format, §11 error taxonomy with the 06-10 amendment.
- `claude_docs/00-design-and-requirements.md` — §3.1 flags (`--sample`, `--ssim`/`--psnr`/`--vmaf`, `--threads`, `--hash-decoder`, `--content`) and §6 architecture (three passes; everything is a fingerprint comparison).
- `claude_docs/03-video-analysis.md` — the `video.closed_captions` row and the HDR checks.
- `claude_docs/00` cites a "parent design doc §10" as the v1 ship gate, but that document is not in the repository. ROADMAP SC1–SC5 and REQUIREMENTS.md stand in for it.

### Prior phase decisions this phase builds on
- `.planning/phases/06-audio-analysis/06-CONTEXT.md` — D-01…D-04 (hash basis, and the block shape video inherits), D-05 (class-2 signature), D-06 (prove before promoting), D-08, D-09 (decode errors), D-11 (two-build proof), D-12 (a value never depends on which passes ran).
- `.planning/phases/05-timeline-analysis/05-CONTEXT.md` — D-01/D-02 (declared finding sets), D-05 (Matroska's 1 ms rounding), D-08 (fixed constants), D-13…D-16 (performance ratchet and reference file).
- `.planning/phases/04-video-analysis/04-CONTEXT.md` — D-01…D-03 (hand-written bitstreams) and D-08 (the HDR source seam).
- `.planning/phases/04-video-analysis/04-CHECK-ROSTER.md` — line 158: `video.closed_captions` held for Phase 7.

### Security and ledger
- `.planning/phases/06-audio-analysis/06-SECURITY.md` — T-06-34 / AR-6-05 (the watchdog obligation) and T-06-01 (the consecutive-error bound).
- `.planning/phases/03-probe-layer-container-size/03-SECURITY.md` — AR-03 (the generator is never linked or shipped), which is the basis for D-10.
- `.planning/WINDOWS.md` — #43 (watchdog), #39 (`ac3_fixed` unmeasured), #12 (encoder output varies with SIMD level).

### Research
- `.planning/research/PITFALLS.md` — Pitfall 2 (the perceptual/quality precondition gap behind TRUST-04), Pitfall 4 (cropped vs coded dimensions, behind D-05), Pitfall 14 (HDR pass-order dependence), Pitfall 16 (crying wolf).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/probe/pass.h` — `Pass::audio_decode`, fused inside `run_packet_scan`'s `av_read_frame` loop (`src/probe/packet_scan.cpp:147`), and its `ProbeResults::audio_decode` slot: the template for the video decode pass and its result slot.
- `src/probe/audio_decode.{h,cpp}` — once-per-stream decoder selection, `determinism_class_for_decoder()` (`audio_decode.cpp:320`), the consecutive-error DoS bound (`audio_decode.h:112-129`), and decode-error counting for `meta.decode_errors`.
- `src/core/value.h:69` — `HashChain{algorithm, digest, element_count, block_digests, element_stride}`, the shape video inherits (Phase 6 D-04).
- `src/compare/hash.cpp` — `kPreconditionKeys` (`:159`), the truncated-sampling rule (`:173`), and the `compute_divergence` block locator, which D-07's time-aligned frame locator extends.
- `src/util/version.cpp:65` — `compose_decode_path_signature()` (libav versions, triplet, cpuflags): the build-level path used by D-04 and D-09.
- `src/analyzers/content/sample_hash.cpp` — precondition evidence keys and the per-stream decode-path record, the pattern for `content.video.frame_hash`.
- `src/analyzers/video/hdr.cpp:211-280` — the `HdrSourceKind::requires_decode` seam and the `source` evidence waiting for the first-frame arm.
- `src/core/model.h:39` — `SkipReason`. It has `requires_media`, `sampling_mismatch`, `hash_incomparable` and `hash_disabled`, and lacks `sampling_conflict` and `not_requested`.
- `scripts/measure_timeline_perf.sh`, `scripts/measure_audio_perf.sh` and `tests/golden/PERF_BASELINE.txt` — the ratchet harness and the 10-minute 1080p reference generator.
- `tools/gen_video_fixtures.py` and `tools/gen_he_aac.py` — the Python bitstream-writer precedent.
- `tests/integration/test_audio_hash_decoder.cpp` — Phase 6 D-11's two-build class proof, the template for D-09/D-10's video proofs.
- `tests/integration/test_doc03_coverage.cpp`, `test_determinism.cpp` and `test_idempotence.cpp` — the DOC-03 gate and the determinism and idempotence harnesses.

### Established Patterns
- One sweep per file. Analyzers declare the passes they need and the orchestrator runs the union once (PROBE-08), which `read_frame_call_count` pins.
- `--content`/`--no-content` resolve per command (`resolve_content_enabled`): `compare` decodes, `snapshot` always decodes, and `dir` and `inspect` opt in.
- Designated-leg goldens, with the fixture-identity assertion first (Phase 6 D-11). CI measures and humans update baselines (Phase 5 D-15).
- Departures from the design docs and requirements are recorded in the open, never silently.

### Integration Points
- `src/cli/commands/compare.cpp:211-217` — the sequential `fingerprint_input` calls. Lockstep (CONTENT-11) restructures this for media-vs-media compares, while a snapshot side keeps its short-circuit.
- `src/probe/orchestrator.h:32` — `ProbeOptions` grows sampling and a test-reachable decoder-thread control.
- `src/cli/commands/dir.cpp` — the worker pool and per-file outcome and exit aggregation (`:466`), where D-13's abandoned-file outcome lands.
- `src/cli/options.cpp` — the new flags `--sample`, `--psnr`, `--ssim` and `--vmaf`. The existing `--probe-timeout` (`:262`) bounds the open only and is not the watchdog.
- `docs/schema/report-1.0.json` — the skip-reason enum additions; `docs/checks/<id>.md` for every new ID (DOC-01/DOC-02).
- `src/probe/demux_session.cpp:250` — where the open budget is disarmed; the watchdog covers everything after this point.
- `.github/workflows/ci.yml` — the designated leg, and the cross-job artifact handoff D-10 needs.

</code_context>

<specifics>
## Specific Ideas

- Doc 06 §2.1 calls "frames 813–819 differ" the product moment. D-07 keeps that report sharp when a frame is dropped, instead of letting it collapse into "every frame after 813 differs".
- Every class-1 claim is backed by a real cross-architecture CI run, the way `aac_fixed`'s was (CI run 35735099865), not by the design doc's word.
- The user consistently chose the option that makes a false positive structurally impossible over one that hides it with tolerance, and wants every departure from the design docs recorded in the open.

</specifics>

<deferred>
## Deferred Ideas

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

</deferred>

---

*Phase: 07-content-quality*
*Context gathered: 2026-09-30*
