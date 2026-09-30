# mediadiff — 06 · Content & Size Analysis

**Doc set:** [00](00-design-and-requirements.md) · [01](01-core-concepts.md) · [02](02-container-analysis.md) · [03](03-video-analysis.md) · [04](04-timeline-analysis.md) · [05](05-audio-analysis.md) · **[06]**
**Phase:** 6 (final). Video decode path, `content.video.*`, `quality.*`, and `size.*`. The `size.*` checks depend only on PacketScan and may be implemented any time after phase 2; they are specified here because they share this doc's "whole-stream measurement" character.

---

## 1. Video decode path

`DecodeSession(video)`: software decode default, `AV_CODEC_FLAG_BITEXACT` always set (pins bit-exact DSP paths — the mechanism behind class-1 determinism for H.264/HEVC/VP9/AV1/MPEG-2). Threading: frame/slice threading **enabled** (`thread_count = --threads`) — libav software decoders produce identical frames regardless of threading; output is consumed in presentation order, so the hash chain is thread-invariant. This claim is enforced, not assumed: the CI determinism suite decodes fixtures at 1, 4, and 16 threads and asserts identical chains.

`--hwaccel cuda`: NVDEC via `av_hwdevice_ctx_create` + `av_hwframe_transfer_data` to system memory before hashing. NVDEC output is **class 2** (deterministic per device+driver): the fingerprint records a path signature (`cuda:<gpu>:<driver>`), and cross-path hash comparisons auto-degrade per doc 01 §7 (`skipped:hash_incomparable` + hint) instead of lying. Default remains software decode precisely so hashes are class 1 out of the box; hwaccel is the acceleration story for perceptual/quality paths where tolerance absorbs device variance.

## 2. Checks — decoded video content (`content.video.*`)

### 2.1 `content.video.frame_hash` — `hash` · fail (`hw-encoder`: info · `transform`: ignore)

Normative hashing rule: per frame, per plane, hash **exactly `bytes_per_row(width) × height`** rows — never `linesize` (allocator padding differs across platforms/decoders; hashing it would be a self-inflicted class-3). Row width via `av_image_get_linesize(pix_fmt, width, plane)`. Frame hash = XXH3-128 over plane hashes ‖ presentation PTS (ticks) ‖ pix_fmt id ‖ dims. Chain: `H_i = XXH3(H_{i-1} ‖ frame_hash_i)`; fingerprint stores the final chain digest **and** a per-frame hash array (compressed: 16 B × frames; 10-min 25 fps ≈ 240 KB — acceptable, enables divergence location against snapshots).

**Amended 2026-09-30 (07-01, D-05/D-06/A4).** The sentence above no longer holds in three respects; `docs/checks/content.video.frame_hash.md` is the user-facing statement. (1) *Hashed basis:* a frame's digest covers the **cropped display rows** (per plane, `bytes_per_row(width) × rows`, never `linesize`), the **pixel-format name after the `yuvj` fold** (`detail::fold_pix_fmt_range`, the one seam `video.pix_fmt` and `video.color.range` share) and the **display dimensions** (two 4-byte little-endian integers) — and **not** the presentation timestamp. The PTS is stored beside each digest (`HashChain::element_ticks`, in `element_tb`) and used only to locate divergence, so an untouched MP4 → MKV/TS remux hashes equal (the TS muxer alone shifts every PTS by ~1.4 s) and a retime is `timeline.*`'s one finding rather than a second one here. (2) *Every decoded frame* is hashed, with edit-list discard ignored (`AV_PKT_FLAG_DISCARD` is cleared on the sweep's scratch packet, because libavcodec otherwise destroys the frame before it reaches the hash), so the digest is the same whether or not the target container can express the trim. (3) *Chain:* the top-level digest is **XXH3-128 over the ordered concatenation of the per-frame 32-hex digests**, exactly as `content.audio.sample_hash` builds its chain (A4) — not the iterated `H_i = XXH3(H_{i-1} ‖ frame_hash_i)` above. Equality semantics are identical and there is one chain rule for the whole project. Per-frame digests stay full width (32 hex characters): about 0.6 MB of text per 15,000-frame snapshot, recorded as a cost rather than truncated.

Report on mismatch: first divergent frame (index + PTS), contiguous divergent ranges (merged at 1-frame gaps), total differing count — "frames 813–819 differ" is the product moment; the chain digest alone would waste it.

Preconditions (doc 01 §3): same decode-path class/signature, same sampling, same pix_fmt+dims. `--sample N` hashes every Nth frame and marks the fingerprint `sampled:N`; only equal-N fingerprints compare, others → `skipped:sampling_mismatch`.

**Amended (07-04, D-08).** `--sample N` does not skip decoding and is not keyframe-only sampling: inter-coded video cannot skip frames, so **every frame is still decoded**, and everything that reads decoded frames (decode-error and corrupt-frame counts, geometry changes, the frozen/black/caption/HDR detectors, `meta.decode_errors`) still sees every one. Only the hash and the stored per-frame arrays are thinned, to the frames whose decode index is a multiple of N (stored frame `k` is decode frame `k × N`). The option buys a smaller snapshot and cheaper quality scoring, not a faster decode. The fingerprint records it twice: `sampling_state` = `sampled:N` on this check and `sampling.video_frame_stride` = N in the envelope (neither present for N = 1, so a `--sample 1` fingerprint is byte-identical to a full one). `frame_interval` evidence is multiplied by N so the time-aligned locator's half-interval window matches the stored spacing. A `sampled:N` side against any side with a different `sampling_state` is `skipped:sampling_mismatch` (never `hash_incomparable`); a truncated side keeps precedence and stays `hash_incomparable`. Audio hashing is not sampled.

### 2.2 `content.video.perceptual` — `±tol` · fail in `hw-encoder`/`transform`, info in `sw-encoder`, ignore in `strict/remux`

v1 metric (freezes parent-doc open question 1): **SSIM on downscaled luma** — `swscale` `SWS_AREA` to width 128 (height by aspect, even), luma plane only, 8×8 window, C1/C2 per the standard constants; per-frame score in [0,1]. Chosen because it is cheap (≪ decode cost), monotonic with visible change, threshold-explainable, and deterministic (fixed-point input, one swscale algorithm pinned; swscale flags recorded in the fingerprint as a precondition). Aggregation: min score, mean, first frame below threshold, worst-10 list (frame + PTS + score). Default threshold 0.985 (the config-example value — kept consistent).

Alignment rule: frames pair by index after both sides confirm equal frame counts; if counts differ, that's already a `video.frame_count` fail and perceptual pairs the overlapping prefix, noting truncation in evidence — never silent misalignment.

### 2.3 `content.video.frozen_runs` / `content.video.black_runs` — `span` (introduced gates) · fail

Frozen: runs ≥ 3 frames where consecutive frame hashes are equal (class-1 path) or perceptual score > 0.9995 (class-2 path; detector notes which). Black: mean luma ≤ black-point + 2 **after range normalization** (limited: bp 16 at 8-bit scaled by depth; full: bp 0 — a range-unaware black detector false-alarms on exactly the files where range flipped, which would be a poetic own goal) and luma variance < 4. Spans stored in the fingerprint; comparison is introduced/removed delta algebra (doc 01 §3). Detectors run inside the same decode sweep as hashing/perceptual — one decode, N sinks (00 §6 rule).

## 3. Checks — full-reference quality (`quality.*`, all opt-in flags)

| Check ID | Definition | Notes |
|---|---|---|
| `quality.psnr` | in-tree, per-frame luma+chroma PSNR at native resolution; aggregate min/mean | trivial math, no dependency; MSE in integers, dB at render |
| `quality.ssim` | in-tree single-scale SSIM at native resolution (same core as §2.2 without downscale) | — |
| `quality.vmaf` | libvmaf behind `MEDIADIFF_WITH_VMAF`; **model pinned `vmaf_v0.6.1` and recorded in the fingerprint** — unpinned VMAF scores are incomparable across runs, so the pin is a correctness feature; harmonic-mean + min reported; interaction with `--sample` refused (VMAF needs the full sequence — `skipped:sampling_conflict`) | CUDA libvmaf is a system-build option documented in README, not a vcpkg feature |

Comparison semantics for all three: candidate-vs-baseline *score delta* under `±tol` (e.g., `vmaf_drop: 0.5`); both files are scored against the same reference (the baseline decoded stream), so `quality.*` requires the compare mode with decodable baseline media — against snapshots, `skipped:requires_media` (scores stored in snapshots are still shown for trend context).

## 4. Checks — size & rate economics (`size.*`; PacketScan only)

| Check ID | Definition | Semantic / default |
|---|---|---|
| `size.file` | container byte size | `±%` (warn 3 / fail 8 in encoder profiles; ±0.5 fail in `strict/remux`) |
| `size.stream_bitrate` | per stream: Σ packet bytes × 8 / computed duration (doc 04's) | `±%` (warn 3 / fail 10) |
| `size.peak_bitrate` | max over 1 s sliding window, 100 ms step, on (dts, size) | `±%` (warn 5 / fail 15) — buffer/VBV compatibility tell |
| `size.overhead` | (file − Σ payload) / file | `±%` · info — muxer efficiency drift |

Windowing is defined on DTS in ticks with rational window bounds — byte-identical results across platforms (idempotence, again).

## 5. Performance & memory (targets are release blockers, parent doc §10)

Metadata+timeline on the 10-min 1080p reference: ≤ 3 s. Full content pass: ≥ 4× realtime software (achievable: decode dominates; hashing ≈ GB/s-class), ≥ 20× with `--hwaccel cuda` on the perceptual path. Memory: streaming one frame in flight per side + per-frame hash arrays; **never** two full decoded sequences resident. `compare` decodes baseline and candidate in lockstep (two sessions, frame-paired) so quality metrics and perceptual need no buffering beyond the pair.

## 6. Fixtures & acceptance

Recipes: identical-encode pair (clean under `sw-encoder` — the idempotence fixture); one-frame corruption pair (bitstream byte flip in generator → hash divergence at known frame; assert exact frame/PTS located); frozen-run pair (`loop` filter segment); black-run pair (`color=black` insert) in both tv and pc range variants (the range-aware detector proof); size pair (CRF 20 vs 23 → file/bitrate deltas); peak pair (single-pass vs constrained VBV in test-gen); sampled-vs-full snapshot pair → `skipped:sampling_mismatch` proof; NVDEC-vs-SW pair on a CUDA runner → `skipped:hash_incomparable` + hint proof (CI job optional, gated on runner availability).

Acceptance: divergence locator exact on the corruption fixture; thread-count determinism suite green (1/4/16 threads, identical chains); perf targets measured in CI on the reference file with regression tracking (mediadiff's own perf tested by trend — dogfooding the philosophy); every check `--explain`ed; full v1 acceptance list (parent doc §10) now runnable end-to-end — this phase closes v1.
