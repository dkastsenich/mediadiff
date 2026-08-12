# mediadiff — 04 · Timeline & Timing Analysis

**Doc set:** [00](00-design-and-requirements.md) · [01](01-core-concepts.md) · [02](02-container-analysis.md) · [03](03-video-analysis.md) · **[04]** · [05](05-audio-analysis.md) · [06](06-content-and-size-analysis.md)
**Phase:** 4. All `timeline.*` checks. Consumes PacketScan (doc 02) and ParserScan (doc 03). Pure integer/rational math — no decode, no floats until rendering. This family is where mediadiff earns its reputation; treat every definition here as normative.

---

## 1. Foundations

### 1.1 Timestamp domain

All math on `{int64 value, AVRational tb}`; conversions via `av_rescale_q` at comparison boundaries only. `AV_NOPTS_VALUE` is a first-class state (`absent`), never coerced. Rendering to ms happens in the report layer with round-half-even.

### 1.2 MPEG-TS 33-bit unwrap

Per elementary stream, before any other analysis: given consecutive raw PTS/DTS in 90 kHz, if `delta < -2^32` (half range), add `2^33` to the running unwrap offset; symmetric guard for backward jumps > half range (treated as genuine discontinuity, not wrap). Unwrapped values feed everything downstream; raw values are preserved in evidence. Wrap events themselves are recorded (`info`) — a candidate that wraps where baseline didn't usually means a start-offset change upstream.

### 1.3 The presentation timeline

Two timelines per stream, both retained:
- **Raw:** packet timestamps as demuxed (with unwrap).
- **Presentation:** edit-adjusted — libav applies MP4 edit lists by default; `AVStream->start_time` and packet timestamps reflect them. The *mechanism* (elst / CodecDelay / origin) is doc 02's business; this doc asserts the *effects* on the presentation timeline, with the mechanism echoed into evidence per the §3.5 rule.

Frame end = `pts + duration`; missing packet duration is reconstructed as the delta to the next PTS in presentation order (last frame: mode interval), with `duration_source: reconstructed` in evidence.

## 2. Checks

| Check ID | Definition (normative) | Semantic / default | Notes |
|---|---|---|---|
| `timeline.start` | first presentation PTS per stream | `±tol` (±5 ms warn / ±20 ms fail; `strict/remux`: ±1 tick fail) | scope per stream; evidence: mechanism (`mp4: elst[0]…`, `mkv: CodecDelay=…`) |
| `timeline.duration` | the **triple**: container-declared (`AVFormatContext->duration`), stream-declared (`AVStream->duration`), computed (last presentation end − first PTS) — compared pairwise across files *and* cross-checked internally | `±tol` (±1 frame fail) | internal triple disagreement > 1 frame raises an `info` incoherence note even when both files share it |
| `timeline.dts_monotonic` | count of `dts[i] ≤ dts[i-1]` per stream (post-unwrap) | count `exact 0` · fail | first violation's index/offset in evidence |
| `timeline.pts_unique` | duplicate presentation PTS count per stream | count `exact 0` · fail | — |
| `timeline.gaps` | spans where interval > max(2 × nominal, declared duration + 1 tick); nominal = mode interval | `span` (introduced gates) · fail | span list stored in fingerprint for delta algebra (doc 01 §3) |
| `timeline.jitter` | CFR streams only: σ and max|dev| of intervals vs nominal, in ms | `±tol` (σ: warn > 0.5 ms, fail > 2 ms) | VFR streams → `skipped:vfr`; classification below |
| `timeline.vfr_profile` | interval histogram, bins in ticks, proportions | `dist` (max bin Δ 2 pp) · warn | **CFR/VFR rule:** ≥ 99.5 % of intervals equal the mode interval ⇒ CFR; feeds `video.frame_rate.measured` (doc 03) |
| `timeline.av_offset` | first audible sample presentation time − first visible frame presentation time (audio side priming-adjusted using doc 05's priming value) | `±tol` (±5 ms warn / ±20 ms fail) | signed; positive = audio late |
| `timeline.av_drift` | §3 algorithm — offset trajectory over checkpoints; reported: rate (ms/min), end delta (ms), pattern class | rate `±tol` (fail > 0.2 ms/min default) | **the flagship check**; pattern class in message: `constant-offset / linear-drift / step` |
| `timeline.discontinuities` | jumps > 250 ms (config) in presentation time not explained by container structure; TS: packets under `discontinuity_indicator=1` are *flagged* structure → `info`, unflagged → gating | `span` · fail | wrap-aware via §1.2 |
| `timeline.timecode` | presence + start value: MOV/MP4 `tmcd` track, `AV_PKT_DATA_S12M_TIMECODE` / GOP timecode (MPEG-2) | `presence` · info (broadcast configs raise to fail via one `--set`) | rendered as SMPTE string; drop-frame flag part of the value |

## 3. The A/V drift algorithm (normative)

Inputs: presentation timelines of the primary video stream and each audio stream (audio priming-adjusted). For K = 32 checkpoints at video timeline fractions `k/K`:

1. `t_v(k)` = presentation time of the nearest video frame start; `t_a(k)` = presentation time of the nearest audio *sample boundary* (packet start + sample-accurate offset within the packet at the stream rate).
2. `offset(k) = t_a_aligned(k) − t_v(k)` where alignment picks the audio time covering the same media position (nearest-sample; audio granularity ≪ 1 ms makes interpolation unnecessary).
3. Least-squares line over `(t_v(k), offset(k))` → slope = **rate** (ms/min), intercept ≈ `timeline.av_offset`.
4. Pattern classification: residual max < ε (2 ms) → `constant-offset` if |slope| below tolerance else `linear-drift`; any single residual step > 3× ε with stable plateaus on both sides → `step` (report the step time). Otherwise `irregular` (rendered with the residual max).
5. Cross-file comparison gates on |rate_candidate − rate_baseline| and end-delta difference; the trajectory (K offsets) is stored in the fingerprint so `compare` against a snapshot retains full fidelity.

Determinism: integer/rational inputs, fixed K, fixed ε ⇒ byte-identical results across platforms — this check must never itself jitter (idempotence guarantee).

## 4. Cross-family effects

- `timeline.start`/`timeline.duration` cite doc 02 mechanism evidence — a finding that says *what* changed and *which box did it*.
- Audio priming (doc 05) is an input to `timeline.av_offset`/`av_drift`; if priming is `unknown`, offset is computed unadjusted and the finding carries `priming: unknown` — visible uncertainty beats hidden precision.
- `video.frame_rate.measured` (doc 03) consumes this doc's interval statistics; single computation, two views.

## 5. Fixtures & acceptance

Fixture recipes (`gen_corpus`, all synthesized, all `+bitexact`):
- **Offset:** `-itsoffset 0.042` on audio → `av_offset` +42 ms, pattern `constant-offset`.
- **Linear drift:** `asetrate=48048,aresample=48000` (0.1 % clock error) → rate ≈ +60 ms/min; also the classic 1000/1001 fixture: video at 30 vs audio timed for 29.97 → ≈ 3.6 s/hour, the NTSC war story in test form.
- **Step:** concat two segments with a 100 ms audio trim at the join.
- **Jitter:** re-mux with a timestamp-perturbing bitstream filter in the generator (`setts` expressions) — ±1-tick noise vs clean.
- **Gaps/dupes/non-monotonic DTS:** `setts` crafted sequences.
- **TS wrap:** `-output_ts_offset` starting ~30 s before 2^33/90000 s so the wrap occurs mid-file; assert clean unwrap and no false discontinuity.
- **VFR:** `mpdecimate`-thinned stream vs CFR original → classification + `skipped:vfr` on jitter.

Acceptance: every fixture produces exactly the intended findings and no others (the *no-others* clause is the point — timeline is where false positives breed); drift algorithm unit tests on synthetic timelines hit rate/pattern classification exactly; idempotence: fingerprinting the same file twice yields identical timeline sections byte-for-byte; the 10-minute reference file's full timeline analysis adds < 15 % over plain PacketScan.
