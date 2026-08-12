# mediadiff — 03 · Video Analysis (Parameters · GOP · Color · HDR)

**Doc set:** [00](00-design-and-requirements.md) · [01](01-core-concepts.md) · [02](02-container-analysis.md) · **[03]** · [04](04-timeline-analysis.md) · [05](05-audio-analysis.md) · [06](06-content-and-size-analysis.md)
**Phase:** 3. Covers all `video.*` checks — stream parameters, GOP structure via a new **parser pass**, and the color/HDR family. Depends on doc 02's DemuxSession/PacketScan; the parser pass built here is reused by doc 04.

---

## 1. New infrastructure: the parser pass

Frame-type and GOP checks need per-access-unit properties without paying for full decode. libav's parser API provides exactly this: feed packet data through `av_parser_parse2` with the stream's `AVCodecParserContext` and read `pict_type`, `key_frame`, `repeat_pict`, `field_order` per parsed unit. Cost: bitstream header parsing only (~100× cheaper than decode).

Design: `ParserScan` runs as an optional extension of PacketScan (same `av_read_frame` sweep — doc 02 §1.2 — so the file is still read once); enabled when any registered check requires `pass:parser`. For H.264/HEVC it additionally records NAL-type sequences per AU (cheap start-code walk over the packet) to classify IDR vs non-IDR I (H.264: NAL 5 vs 1-with-I; HEVC: IDR_W_RADL/IDR_N_LP vs CRA_NUT) — required for open/closed GOP classification below. AV1/VP9/MPEG-2 rely on parser outputs only.

Fallback: codecs without a parser (rare in the v1 matrix; ProRes is all-intra) → GOP checks emit `skipped:no_parser`, frame-type distribution degrades to keyframe-flag granularity from packet flags.

## 2. Checks — stream parameters (`video.*`)

Extraction source is `AVStream.codecpar` unless stated; all comparisons per doc 01 semantics; defaults shown as `severity (profile exceptions)`.

| Check ID | Extraction | Semantic / default | Details & edge cases |
|---|---|---|---|
| `video.codec` | `codecpar->codec_id` → `avcodec_get_name` | `exact` · fail (`transform`: info) | — |
| `video.profile` | `codecpar->profile` → `avcodec_profile_name`; raw int kept in evidence | `exact` · fail | unknown profile (-99) compared as raw int with `unknown` rendering |
| `video.level` | `codecpar->level` raw + codec-specific human string (own table: H.264 `31→3.1`, HEVC `123→4.1` main-tier flagging, AV1 `8→4.0`) | `exact` · fail | tier (HEVC main/high) folded into the rendered string, compared as part of the value |
| `video.resolution` | `width×height` (+ `coded_w/h` in evidence when it differs — cropping tell) | `exact` · fail (`transform`: `±tol` vs declared expectation) | odd dimensions + chroma subsampling mismatch noted in evidence |
| `video.sar` / `video.dar` | `codecpar->sample_aspect_ratio` (0/1 → treated as 1:1 with `unset` evidence); DAR derived rationally | `exact` (rational equality) · fail | container vs bitstream SAR conflict (mp4 `pasp` vs SPS VUI): record both, compare effective (container wins per libav), conflict itself noted `info` in evidence |
| `video.pix_fmt` | `codecpar->format` normalized: **range-fold table** `yuvj420p→(yuv420p, range=full)` etc. before comparison; range delta is then owned solely by `video.color.range` | `exact` · fail | the yuvj trap: two spellings of one intent must not double-report; normalization is the fix |
| `video.frame_rate.declared` | `avg_frame_rate` primary; `r_frame_rate` recorded as evidence | `exact` (rational) · fail | 30000/1001 vs 29.97 float never compared as floats |
| `video.frame_rate.measured` | from doc 04's interval analysis (mode interval → fps; CFR/VFR class) | class `exact` · fail; rate `±tol` (±0.1 %) · warn | declared-vs-measured *internal* mismatch is its own evidence flag on this check |
| `video.frame_count` | **always** counted from PacketScan/ParserScan (never trust `nb_frames`; container value kept as evidence) | `exact` · fail; `±frames` tolerance available | counting rule fixed = snapshots comparable across container types |
| `video.frame_types` | I/P/B proportions from ParserScan | `dist` (max bin Δ, default 5 pp) · warn (`strict/remux`: fail exact) | sudden B disappearance = config regression fingerprint |
| `video.gop.length` | keyframe-to-keyframe distances: min/median/max | `±tol` (median ±10 %) · fail | keyframe = parser `key_frame` |
| `video.gop.idr_interval` | IDR cadence (NAL-classified, §1); open/closed classification (H.264: I-without-IDR after non-IDR = open; HEVC: CRA = open) | interval `±tol` · fail; open/closed `exact` · fail | the segment-alignment killer (UC3) |
| `video.gop.refs` | `refs` from SPS via parser where available, else `codecpar` evidence-only | `exact` · warn | decoder DPB/memory contract |
| `video.interlace` | `codecpar->field_order` cross-checked against parser per-frame field flags; mixed content → `mixed` value + proportions in evidence | `exact` · fail | TFF/BFF flip = judder previews hide |
| `video.closed_captions` | A53/CEA-708 presence: v1 detects during the decode pass (frame side data `AV_FRAME_DATA_A53_CC`); under `--no-content` → `skipped:requires_decode` | `presence` · fail | stretch goal (post-v1): SEI ITU-T T.35 scan in ParserScan to lift the decode requirement — tracked, not promised |

## 3. Checks — color & colorimetry (`video.color.*`)

Extraction: `codecpar->color_range / color_primaries / color_trc / color_space / chroma_location`. Rendering uses `av_color_*_name`. `unspecified` compares as its own value (a change **to** unspecified is a regression — metadata loss — not a wildcard).

| Check ID | Semantic / default | Details |
|---|---|---|
| `video.color.range` | `exact` · **fail in all profiles, no exceptions** | after pix_fmt range-folding (§2) this check owns all range deltas; message names the practical effect ("washed-out/crushed downstream") |
| `video.color.primaries` | `exact` · fail | — |
| `video.color.transfer` | `exact` · fail | PQ (`smpte2084`) and HLG (`arib-std-b67`) called out by name; SDR↔HDR transitions get an amplified message |
| `video.color.matrix` | `exact` · fail | 601↔709 delta message: "global color shift" |
| `video.color.chroma_loc` | `exact` · warn | left/center/topleft; scaler-chain drift tell |

## 4. Checks — HDR metadata (`video.hdr.*`)

Extraction precedence (recorded in evidence as `source: stream | first_frame`):
1. **Stream-level:** `codecpar->coded_side_data` (FFmpeg ≥ 6.1) — `AV_PKT_DATA_MASTERING_DISPLAY_METADATA`, `AV_PKT_DATA_CONTENT_LIGHT_LEVEL`, `AV_PKT_DATA_DOVI_CONF`.
2. **Fallback:** first decoded frame's side data (`AV_FRAME_DATA_MASTERING_DISPLAY_METADATA`, `AV_FRAME_DATA_CONTENT_LIGHT_LEVEL`) when the decode pass runs; else `skipped:requires_decode` if stream-level absent but the codec could carry frame-level (HEVC/AV1).

| Check ID | Extraction | Semantic / default | Details |
|---|---|---|---|
| `video.hdr.mdcv` | `AVMasteringDisplayMetadata`: 8 chromaticity rationals + min/max luminance | `presence` · fail; values `±tol` · fail (luminance ±5 %, chromaticities ±0.0002 absolute) | rationals compared rationally; the 0.00002-unit encoding convention of HEVC SEI documented in `--explain` |
| `video.hdr.cll` | `AVContentLightMetadata` MaxCLL/MaxFALL | `presence` · fail; values `±tol` (±5 %) · fail | re-derivation by pipelines legitimately shifts values — tolerance, not exactness |
| `video.hdr.dovi` | `AVDOVIDecoderConfigurationRecord`: profile, level, RPU/EL/BL flags | `presence` · fail; fields `exact` · fail | v1 = configuration record only; per-frame RPU diffing is out of scope |

Consistency guard: `video.hdr.mdcv` present while `video.color.transfer` is SDR (or vice-versa for PQ without MDCV) raises an *internal-consistency* `info` note on the finding — mediadiff reports incoherence it notices even when both files share it (visible in `inspect`, non-gating in compare).

## 5. Profile interactions specific to this doc

`transform`: identity checks (`codec/profile/level/pix_fmt/resolution`) become declared-expectation checks (doc 01 §5); **colorimetry stays `fail`** — preserving color intent through an intentional transformation is precisely the invariant the profile exists to protect. `hw-encoder`: GOP tolerances stay gating (UC3 is the canonical catch); `video.frame_types` widens to ±10 pp (hardware rate control legitimately redistributes).

## 6. Fixtures & acceptance

Recipes (all lavfi-synthesized, `+bitexact`): range pair (`-color_range tv` vs `pc`, plus a `yuvj420p` spelling of the same intent — must produce **one** finding, on `video.color.range`, not two); 601 vs 709 matrix pair; level pair (`-level 41` vs `50`); GOP pair (`-g 48` vs `-g 96`, x264 in test-gen); open vs closed GOP; interlaced pair (`tinterlace` TFF vs BFF); HDR10 pair via libx265 test-gen with `master-display`/`max-cll` on and off (if libx265 unavailable in the dev manifest, fixtures are pre-generated by the corpus script on a machine that has it and cached — still never committed); frame-type distribution pair (`-bf 0` vs `-bf 3`).

Acceptance: every check triggered and cleaned by fixture pairs; the yuvj single-finding property tested explicitly; parser pass overhead measured < 10 % over plain PacketScan on the 10-minute reference file; `inspect` renders a complete, correct video section for every fixture; all `--explain` docs written (build enforces).
