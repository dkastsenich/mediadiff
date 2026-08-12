# mediadiff — 05 · Audio Analysis

**Doc set:** [00](00-design-and-requirements.md) · [01](01-core-concepts.md) · [02](02-container-analysis.md) · [03](03-video-analysis.md) · [04](04-timeline-analysis.md) · **[05]** · [06](06-content-and-size-analysis.md)
**Phase:** 5. All `audio.*` checks plus the audio half of `content.*` (sample hashing lives here because its determinism story is audio-specific). Introduces the audio decode path and puts doc 01 §7's decode-determinism classes into practice. Audio regressions ship disproportionately often because QA culture is visual — this family is cheap insurance with outsized catch value.

---

## 1. Audio decode path

`DecodeSession(audio)`: `avcodec_send_packet`/`receive_frame`, `AV_CODEC_FLAG_BITEXACT` always set, **decoder selection by determinism policy** (§3): when a fixed-point sibling decoder exists and hashing is requested, open by name (`aac_fixed`, `ac3_fixed`) instead of the default float decoder. Output frames are consumed sample-accurately; no resampling in the analysis path (`swresample` linked but used only if a future check needs canonical layouts — v1 does not resample, it records).

## 2. Checks — stream parameters

| Check ID | Extraction | Semantic / default | Details |
|---|---|---|---|
| `audio.codec` | `codecpar->codec_id` | `exact` · fail | — |
| `audio.profile` | `codecpar->profile` (`FF_PROFILE_AAC_LOW/HE/HE_V2`) **plus SBR signaling mode** (§2.1) as part of the value | `exact` · fail | LC↔HE and implicit↔explicit signaling are distinct regressions; both must be visible |
| `audio.sample_rate` | `codecpar->sample_rate`; for HE-AAC, both core and effective rates recorded | `exact` · fail | — |
| `audio.sample_fmt` / `audio.bit_depth` | `codecpar->format`, `bits_per_raw_sample` (PCM) | `exact` · fail | float↔s16 pipeline drift tell |
| `audio.channels` | `ch_layout.nb_channels` | `exact` · fail | — |
| `audio.layout` | `av_channel_layout_describe` canonical string (new `AVChannelLayout` API only — no legacy masks) | `exact` · fail | `5.1` vs `5.1(side)` is the headline war story: same count, different speakers; unspecified layout compares as its own value (loss of layout = regression) |
| `audio.priming` | precedence: `codecpar->initial_padding` → container mechanism (mp4 elst media_time / iTunSMPB tag, mkv CodecDelay — doc 02 evidence) → `unknown` | `±samples` (±0 fail in `strict/remux`; ±32 warn elsewhere) · fail | trailing padding recorded when knowable (iTunSMPB), else `unknown`; `unknown` propagates visibly into doc 04's A/V math |

### 2.1 HE-AAC SBR signaling detection

Explicit signaling: extradata/ASC declares SBR (profile reads HE directly). Implicit: ASC says LC but the decoder discovers SBR — detect by comparing `codecpar->sample_rate` before decode with the first decoded frame's `sample_rate` (doubling ⇒ implicit SBR); record `sbr: implicit` into `audio.profile`'s value. Why it matters: implicit-only streams decode as LC at half bandwidth on non-SBR-aware decoders — muffled audio that "measures fine" everywhere else.

## 3. Determinism classes applied (normative table for v1)

| Decoder situation | Class | `content.audio.sample_hash` behavior |
|---|---|---|
| PCM, FLAC, ALAC | 1 | hash valid everywhere |
| AAC via `aac_fixed`, AC-3 via `ac3_fixed` | 1 | hash valid everywhere; fingerprint records decoder name |
| AAC/AC-3 via float decoders, E-AC-3, Opus, MP3 (`mp3float`) | 2 | hash valid only when both fingerprints share the decode-path signature; else `skipped:hash_incomparable` + hint: "use loudness/silence checks or same-path snapshots" |
| anything unclassified | 3 | hash disabled |

Rule: hashing **prefers** class-1 siblings automatically; `--hash-decoder default` opts out (records class 2). The report never claims bit-difference through a float decoder across differing paths — that lie is how diff tools die.

## 4. Checks — content & measurement

| Check ID | Definition | Semantic / default | Details |
|---|---|---|---|
| `content.audio.sample_hash` | XXH3-128 chain over decoded PCM per track: per-frame hash of channel-canonical byte view (native layout order, native sample format — no conversion; format recorded as hash precondition) | `hash` · fail (`hw-encoder`: info) | preconditions per doc 01 §3; first divergent sample index + time reported |
| `audio.loudness.integrated` | libebur128, mode `EBUR128_MODE_I`, fed frame-by-frame; layout mapped via `ebur128_set_channel` from `AVChannelLayout` | `±LU` (±0.5 warn/±1.0 fail; `transform` ±1.0) | do not hand-roll BS.1770; < −70 LUFS gating floor → value `silent` |
| `audio.loudness.true_peak` | `EBUR128_MODE_TRUE_PEAK`, max over channels, dBTP | `±dB` (±0.3) · warn; **crossing −1.0 dBTP upward when baseline was under ⇒ fail** | the asymmetric rule matches the real risk: headroom loss → clipping after downstream lossy encode |
| `audio.silence.edges` | leading/trailing spans where sample peak < −60 dBFS with 5 ms hysteresis (exact thresholds are constants in one header, echoed in `--explain`) | `±ms` (±5) · fail | introduced leading silence = sync/gapless bug |
| `audio.silence.dropouts` | interior spans: sliding 100 ms RMS window < −70 dBFS, min span 150 ms | `span` (introduced gates) · fail | classic buffer-handling regression |

Measurement pass note: loudness + silence + hash share one decode sweep per track; the analyzer is a single consumer with three sinks — the file is decoded once (00 §6 rule: no analyzer re-reads).

## 5. Profile interactions

`remux`: sample hash `fail`, priming `±0`, loudness `exact-ish` (±0.1 LU float-noise allowance). `hw-encoder`: hash `info` (hardware audio paths are rare but the profile is consistent), loudness/silence stay gating. `transform`: hash `ignore`, loudness ±1.0 LU gating — an enhancement stage may not silently re-level the mix.

## 6. Fixtures & acceptance

Recipes (lavfi, `+bitexact`): `sine=frequency=440` mono/stereo/5.1 and a `5.1(side)` remap pair (channelmap) → layout finding; AAC-LC vs HE-AAC pair, explicit vs implicit SBR (fdk in test-gen if licensable on the dev machine, else pre-generated cached fixtures — never committed); priming pair via container round-trips (mp4 → mkv → mp4) asserting `audio.priming` stability while doc 02 mechanisms differ; known-loudness fixture (sine at computed gain) cross-checked against `ffmpeg -af ebur128` output in the generator; `adelay=250` → leading-silence finding; volume-gated dropout fixture (`volume=enable='between(t,3,3.4)':volume=0`).

Acceptance: every check triggered + cleaned by fixtures; class rules proven by a three-way test (same file hashed via `aac_fixed` twice across two builds = equal; via float decoder with mismatched recorded paths = `skipped:hash_incomparable`, never fail); loudness matches the ffmpeg ebur128 reference within ±0.1 LU on fixtures; audio sweep of the 10-minute reference stereo AAC completes < 4 s (decode-bound target); idempotence suite extended with an audio-only pipeline.
