# Phase 7: Content & Quality - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-28 to 2026-09-30
**Phase:** 07-content-quality
**Areas discussed:** Two-file metrics vs snapshots, What the frame hash covers, Which decoders hash by default, Decode watchdog

The user selected all four proposed areas. Also offered but not selected: the frozen/black detector
rules, what `--threads` means for decoding, and the closed-caption/HDR fixtures. Those became
Claude's-discretion defaults or research questions in CONTEXT.md.

---

## Two-file metrics vs snapshots

### Where should perceptual and quality scores live, when the baseline is a snapshot?

| Option | Description | Selected |
|--------|-------------|----------|
| Live compare only (Recommended) | Baseline records a self-score and candidate records its score against the baseline, with the baseline's identity in evidence; the tol engine compares them. Against any snapshot these checks report skipped:requires_media, and CONTENT-10's "stored scores for trend context" is amended in the open. Cost: hw-encoder's perceptual gate goes dark against a committed-snapshot baseline. | ✓ |
| Candidate snapshots carry scores | Same, plus a way to snapshot a candidate against a named baseline (e.g. `snapshot out.mp4 --against ref.mp4`), storing its scores for trend display. Meets CONTENT-10 literally; adds a flag. | |
| Thumbnail sidecar | Snapshots also write per-frame 128-wide luma to a binary sidecar, so perceptual works against snapshots. About 9 KB/frame (~140 MB for 10 min at 25 fps); quality.* stays media-only. | |

**User's choice:** Live compare only.

### How should baseline and candidate frames be paired?

| Option | Description | Selected |
|--------|-------------|----------|
| By presentation time (Recommended) | Timestamps from each side's first frame, matched within half an interval in rational math; drops and duplicates are left unpaired and counted; index fallback when timestamps are unusable; CONTENT-05's wording amended. | ✓ |
| By decode index (doc 06 §2.2) | Frame i with frame i over the overlapping prefix. Simple, but one mid-file drop shifts every later pair. | |

**User's choice:** By presentation time.

### Which aggregate should gate the two-file metrics?

| Option | Description | Selected |
|--------|-------------|----------|
| Split by purpose (Recommended) | Perceptual gates on its worst frame (one frame below 0.985 blocks); quality.* gates on the sequence mean (harmonic mean for VMAF). | ✓ |
| Worst frame everywhere | Every metric gates on its minimum; one hard frame can fail PSNR/VMAF on a healthy encode. | |
| Mean everywhere | Robust to outliers, but a 5-frame glitch in 10 minutes vanishes. | |

**User's choice:** Split by purpose.

### What should TRUST-04's path-signature precondition compare?

| Option | Description | Selected |
|--------|-------------|----------|
| Build-level path (Recommended) | Scaler setup plus the build's decode-path signature; identical by construction within one run; different codecs on the two sides still score; SC3 read as build/device paths. | ✓ |
| Also decoder identity | Literal SC3 reading: skip whenever the two sides used different decoders, so every cross-codec comparison goes dark. | |

**User's choice:** Build-level path.

**Area close-out:** the user moved on, accepting the stated defaults: flags-only enabling
(`--psnr`/`--ssim`/`--vmaf`), a new `skipped:not_requested` reason when off, `--vmaf` without
libvmaf as exit 64, equal display size for quality.*, and perceptual across resolutions at
matching aspect ratio.

---

## What the frame hash covers

### What should a frame's hash cover?

| Option | Description | Selected |
|--------|-------------|----------|
| Pixels, format, size (Recommended) | Cropped display pixels, pixel format after yuvj range folding, and dimensions; PTS stored beside the hash, for locating only. An untouched remux hashes equal. | ✓ |
| Normalized timestamp included | PTS from the first frame in a fixed rational base; survives TS's 1.4 s shift but not Matroska's 1 ms rounding. | |
| Doc literal: raw PTS ticks | Any container or timebase change fails every frame on identical pixels. | |

**User's choice:** Pixels, format, size.

### Which frames get hashed when the container hides some?

| Option | Description | Selected |
|--------|-------------|----------|
| Every decoded frame (Recommended) | Phase 6 D-01 carried to video: every frame from every delivered packet, including edit-list-hidden ones; the trim is owned by timeline.* and container.mp4.edit_list. | ✓ |
| Only presented frames | Matches playback, but the hashed basis then depends on each container's trim support. | |

**User's choice:** Every decoded frame.

### How should the divergence report line frames up?

| Option | Description | Selected |
|--------|-------------|----------|
| By presentation time (Recommended) | Same matching as two-file pairing; a dropped frame reads "frame 813 missing" and later frames still line up; identical for media and snapshot baselines. | ✓ |
| By index (doc 06 §2.1) | One drop at 813 reports "frames 813–14999 differ". | |

**User's choice:** By presentation time.

### What should `--sample N` sample?

| Option | Description | Selected |
|--------|-------------|----------|
| Honest stride (Recommended) | Every frame decoded; detectors, captions and HDR see every frame; hash, store and score every Nth frame, which buys smaller snapshots and cheaper native-resolution metrics but not a faster decode, stated plainly. | ✓ |
| Keyframes only | Real speedup, but the sampled frames depend on GOP structure, detectors can't run, and N stops meaning a stride. | |

**User's choice:** Honest stride.

**Area close-out:** the user moved on, accepting the stated defaults: Phase 6 D-04 per-frame
storage plus a per-frame timestamp array, digest truncation left to the planner, and native-byte
hashing for high bit depth.

---

## Which decoders hash by default

### What should it take for a video decoder to hash across machines?

| Option | Description | Selected |
|--------|-------------|----------|
| Proof required, class 2 meanwhile (Recommended) | Class 1 only after a cross-architecture CI proof (aac_fixed's method), with any needed setting recorded; class 2 until then, for doc-listed and corpus codecs alike; doc 01 §7 amended in the open. | ✓ |
| Doc list trusted, others proven | H.264/HEVC/VP9/AV1/MPEG-2 class 1 from day one, as with ac3_fixed; unlisted codecs class 3 until proven, so the MPEG-4 Part 2 corpus hashes nothing. | |
| Doc list trusted, others class 2 | Listed codecs class 1 on the doc's word; everything else class 2 until proven. | |

**User's choice:** Proof required, class 2 meanwhile.

### How should the proof get the same file onto every leg, for codecs the LGPL corpus can't encode?

| Option | Description | Selected |
|--------|-------------|----------|
| Designated leg encodes (Recommended) | The x64-linux leg encodes proof streams with its pinned libx264/libx265 and hands them to the other legs as CI artifacts; the generator is never linked or shipped (AR-03); fixtures are still synthesized (BUILD-08); proofs run only in CI. | ✓ |
| Mirror public sample files | Licence-clean conformance streams as release assets; not synthesized, and each needs its own licence check. | |
| No proof for them in v1 | H.264/HEVC stay class 2 through v1, recorded as open gaps. | |

**User's choice:** Designated leg encodes.

### What happens to a decoder that fails TRUST-07's thread check?

| Option | Description | Selected |
|--------|-------------|----------|
| Pin it to one thread (Recommended) | Keeps hashing, decodes single-threaded, setting recorded in decoder flags, slowdown measured against PERF-02. | ✓ |
| Stop hashing it (class 3) | Keeps speed, loses frame-exact comparison for that codec. | |

**User's choice:** Pin it to one thread.

**Area close-out:** the user moved on, accepting the stated defaults: proofs attempted for every
decoder the designated leg can encode; failed proofs stay class 2 with evidence; `--hash-decoder`
stays audio-only; `--threads` keeps meaning dir's worker pool, with automatic decoder threading
and a control for the TRUST-07 test; recoverable video decode errors go to `meta.decode_errors`.

---

## Decode watchdog

### What should count as "hung"?

| Option | Description | Selected |
|--------|-------------|----------|
| One call too long, fixed (Recommended) | Any single libav call after open (reads, parser, decoders) running past a fixed, generous limit (minutes, sized for valgrind); a fixed constant, not a flag. | ✓ |
| One call too long, configurable | Same trigger, plus a flag and a TOML key. | |
| Whole-file time budget | Scaled from duration; ties the outcome to runner load. | |

**User's choice:** One call too long, fixed.

### When one file hangs during a `dir` run, what happens to the rest?

| Option | Description | Selected |
|--------|-------------|----------|
| Abandon it, finish the rest (Recommended) | The hung file is reported as could-not-run, per dir's existing per-file rule (dir.cpp:466); the others finish; the process exits 66 without joining the stuck thread. | ✓ |
| Whole run stops | The first trip ends everything and discards every other file's result. | |
| Subprocess per file | Most robust; a large cross-platform change. | |

**User's choice:** Abandon it, finish the rest.

**Area close-out:** the user wrapped up, accepting the stated defaults: exit 66 with an error-only
partial JSON entry; the watchdog lives in `cli/` with a heartbeat from the library; a
simulated-stall test proves the trip path.

---

## Claude's Discretion

- Enabling quality.* by flags only; the `skipped:not_requested` reason; `--vmaf` without libvmaf as exit 64; dimension and aspect-ratio rules for the two-file metrics; how the self-score is obtained (PSNR's infinite value for identical frames needs a defined representation).
- The lockstep mechanism (threads with a one-frame handoff, or a pull decoder), under the one-sweep-per-side and one-frame-in-flight invariants.
- Bit-depth normalization and integer or fixed-point SSIM/PSNR arithmetic.
- The skip reason for a D-04 mismatch, and how `inspect` shows two-file checks.
- Per-frame digest storage and truncation; native-byte hashing for high bit depth.
- The proof roster for decoders; `--hash-decoder` staying audio-only; the decoder-thread control for TRUST-07.
- The watchdog's limit value, trip envelope and test hook; clarifying `--probe-timeout`'s help text.
- For the researcher to measure: whether frozen-run detection uses exact hash equality or the near-identical threshold, and span stability across GOP boundaries; closed-caption fixture construction.

## Deferred Ideas

- `--first-divergence`: in doc 00's flag list with no definition or requirement.
- Snapshot-borne two-file scores (`snapshot --against`).
- Quality scored against a user-supplied source reference (its own phase).
- A per-frame luma thumbnail sidecar for perceptual against snapshots.
- A `mediadiff.toml` key to enable quality.*.
- Subprocess isolation per `dir` file.
- A configurable watchdog limit.
- Time-aligned divergence for audio blocks.
- `--hwaccel cuda` / NVDEC (v2), and the SEI T.35 caption scan (EXT-01).
