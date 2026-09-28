# Phase 6: Audio Analysis - Research

**Researched:** 2026-09-20
**Domain:** FFmpeg 8.1 audio decode determinism, HE-AAC/SBR bitstream signaling, EBU R128 loudness, and priming-chain container mechanics — verified directly against the LINKED vcpkg FFmpeg 8.1 build, not the 9.0.1 generator binary.
**Confidence:** HIGH for everything backed by a source read or a runtime probe against `build/x64-linux/vcpkg_installed` (this session); MEDIUM/LOW flagged explicitly where cross-architecture or real-fixture evidence could not be produced in this environment.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

See `.planning/phases/06-audio-analysis/06-CONTEXT.md` `<decisions>` for the full text of D-01 through D-17 (verbatim, not reproduced here to avoid drift between two copies of the same locked text). Summary of what's locked, for quick reference during planning:

- **D-01:** `content.audio.sample_hash` hashes the decoder's untrimmed output (every sample from every packet delivered), ignoring sample-level trim signals.
- **D-02:** The hash chain steps over fixed sample-count blocks in a canonical interleaved byte view, not decoder frames.
- **D-03:** Divergence is reported as block ranges (first divergent block = sample range + time), identically for media and snapshot baselines.
- **D-04:** The fingerprint stores per-block digests at ~100 ms granularity, one hex digest per line; extends `HashChain` (Phase 7's video hashing inherits this shape).
- **D-05:** The class-2 path signature is libav versions + build platform triplet + runtime SIMD feature set (`av_get_cpu_flags()`).
- **D-06:** The auto-preferred fixed-point sibling list extends to `mp3`/`mp2`, pending cross-architecture bit-exactness proof.
- **D-07:** When the preferred fixed-point decoder cannot open a stream (USAC/xHE-AAC on `aac_fixed`), fall back to the default decoder, recorded as class 2 with the fallback reason.
- **D-08:** Decoder selection is a property of the fingerprint (`--hash-decoder`), never of the profile.
- **D-09:** Recoverable decode errors are a gating finding (`meta.decode_errors`), not "could not run"; only a wholly undecodable stream marks the fingerprint partial (exit 66).
- **D-10:** HE-AAC fixtures are hand-written bitstreams emitted by a Python writer under `tools/` (explicit AOT-5 ASC and implicit LC ASC pair).
- **D-11:** The class-1 two-build proof runs on a hand-written, non-silent AAC input with an XXH3 identity self-assertion.
- **D-12:** SBR signaling mode is detected in the header pass by comparing declared ASC/ADTS against `codecpar` after `avformat_find_stream_info`, with a bounded one-packet decode fallback if `codecpar` doesn't carry the doubled rate. **This research confirms the fallback is required** — see Q4 below.
- **D-13:** Loudness/true-peak/silence fixtures are FLAC or PCM, with the `ffmpeg -af ebur128` reference committed as text.
- **D-14:** `unknown` compares as its own value for `audio.priming` — losing (or gaining) priming signaling is a regression.
- **D-15:** The resolver keeps D-09's (Phase 5) verified order and gains a container-mechanism tier (MP4 `elst`/iTunSMPB, MKV `CodecDelay`); highest-precedence source wins, disagreements ride in evidence. **This research substantially narrows this tier's scope** — see Q7 below.
- **D-16:** `WINDOWS.md` #32 is closed by extending Phase 5 D-10's shared-basis rule from offsets to `av_drift`'s checkpoint span.
- **D-17:** Trailing padding rides in `audio.priming`'s evidence, not its own check.

### Claude's Discretion

- The audio check-ID roster (approved at a roster checkpoint, per phases 3/4/5 precedent).
- How the audio decode pass fuses with the existing sweep (new `Pass` member + `ProbeResults` slot vs. riding inside `run_packet_scan`'s loop).
- Wiring `--content`/`--no-content` for `compare`/`snapshot`/`dir`/`inspect`, and adding the missing `--hash-decoder` flag to doc 00 §3.1's list.
- Where the per-hashed-stream record (`TRUST-01`) lives: `Envelope::decode_path` vs measurement evidence; how D-05's signature fits `hash.cpp`'s existing 3-key evidence table without breaking its shape. **This research recommends carrying the full signature as the value of `decode_path_class`** — see Q2 below.
- The exact block length in samples at each rate (D-04 targets ~100 ms; integer-sample, rate-derived), and whether per-block digests are truncated below 128 bits.
- Loudness/true-peak value representation (`double` vs quantized rational).
- `PERF-04` follows Phase 5 D-13/D-14's ratchet-style measurement, not an absolute wall-clock assertion.
- Profile interactions per doc 05 §5 (`remux`/`hw-encoder`/`transform`).
- How existing fixtures' declared finding sets absorb the new audio checks (narrow the fixture, never filter the count).

### Deferred Ideas (OUT OF SCOPE)

- Per-channel digests (report "only the LFE changed").
- Exact-sample divergence via lockstep decode (rejected by D-03; revisit only if Phase 7's video lockstep lands cleanly).
- A priming source-coherence check id (the `video.hdr.coherence` shape) — rejected by D-15 for a condition not yet observed in this corpus.
- A separate trailing-padding check id (rejected by D-17; additive later if needed).
- `EXT-05`'s remaining scope beyond what the demuxer/raw scanners expose.
- Codecs doc 05 §3 does not list (Vorbis, DTS, TrueHD/MLP, WavPack) — stay class 3.
- HE-AAC v2 / Parametric Stereo signaling beyond what `audio.profile`'s value needs.
- Optimizing float decode paths, and `--hwaccel` for audio (v2, `HW-01`…`HW-03`).
- `WINDOWS.md` #29 (the `tol` comparator's misleading delta text) — untouched by this phase.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-------------------|
| AUDIO-01 | Stream-parameter checks: codec, profile, sample_rate, sample_fmt/bit_depth, channels, layout | Q4 findings on what `codecpar` carries after `avformat_find_stream_info` with no decode; Architecture Patterns diagram's header-pass box |
| AUDIO-02 | `audio.layout` distinguishes `5.1` from `5.1(side)`, loss of layout is a regression | Common Pitfall 3 (EBU R128 channel mapping consequence of the same distinction) |
| AUDIO-03 | HE-AAC SBR signaling mode (implicit vs explicit) detected, carried in `audio.profile` | Q4 (D-12) — full source-verified trace of explicit vs implicit ASC parsing and the confirmed need for a bounded decode fallback; Q5 (D-10) fixture-writer container-choice guidance |
| AUDIO-04 | `audio.priming` resolves through the precedence chain, stable across container round-trips | Q7 (D-15) — the phase's highest-value finding: MP4 elst/iTunSMPB and MKV CodecDelay already fold into Phase 5's existing resolver fields |
| AUDIO-05 | `audio.loudness.integrated` via libebur128 EBU R128 mode-I, ±0.1 LU vs `ffmpeg -af ebur128` | Q8 (D-13) — installed `ebur128.h` API shape, mode flags, feed-function dispatch by sample format |
| AUDIO-06 | `audio.loudness.true_peak` reports dBTP, asymmetric −1.0 dBTP fail | Q8 (D-13) — `EBUR128_MODE_TRUE_PEAK`, `ebur128_true_peak()` |
| AUDIO-07 | `audio.silence.edges`/`.dropouts` detect spans | Architecture Patterns diagram (shared decode sweep, sink 2) |
| AUDIO-08 | `content.audio.sample_hash` chains XXH3-128, reports first divergent sample | D-01/D-02/D-03/D-04 already locked in CONTEXT.md; Q6 (D-01) source-verified trim mechanism the hash basis depends on |
| AUDIO-09 | Auto-prefer class-1 fixed-point decoders, `--hash-decoder default` opt-out | Q1 (D-06) — SIMD-stability evidence against the linked 8.1; Q3 (D-07) — USAC rejection point and timing |
| AUDIO-10 | Loudness, silence, hashing share a single decode sweep per track | Q10 (PERF-04/AUDIO-10) — `Pass`/`HashChain` current state and extension points |
| TRUST-01 | Every fingerprint records decoder name, determinism class, flags, (class 2) path signature | Q2 (D-05) — signature composition, `hash.cpp`'s existing evidence-key table |
| TRUST-02 | Class-2 hash comparison across differing decode paths reports `skipped:hash_incomparable` | Q1/Q2 (D-05/D-06) — what makes two decode paths "differing" |
| PERF-04 | Audio sweep of 10-minute reference stereo AAC completes in < 4 s | Q10 — ratchet-style harness extension, mirroring Phase 5 PERF-03's precedent |

</phase_requirements>

## Project Constraints (from CLAUDE.md)

Directives from `.claude/CLAUDE.md` that bind this phase's implementation, verified against this research's own findings:

- **C++20, no modules, no `std::format` (fmt instead), no exceptions across the lib boundary (`expected<T,Error>`)** — the decode pass and its three sinks (loudness/silence/hash) must report libav decode failures through `mediadiff::expected`, never a thrown exception; `avcodec_send_packet`/`avcodec_receive_frame` return codes map to `Error` values at the `src/probe/` boundary, matching the existing `DemuxSession` pattern (`[VERIFIED: src/probe/demux_session.cpp]`).
- **Rational everywhere; floating milliseconds only in rendered output** — loudness/true-peak values from libebur128 are natively `double` (LUFS/dBTP have no natural rational representation, unlike `{value, AVRational}` timestamps); the "Claude's Discretion" item on value representation should default to `double` stored directly (matches PROJECT.md's own carve-out for genuinely continuous physical quantities) rather than forcing a quantized-rational encoding that buys no determinism benefit here, since libebur128 itself computes in `double` internally.
- **Determinism: byte-identical `--json` across identical runs; fixed-K/fixed-ε algorithms** — the decode path's `AV_CODEC_FLAG_BITEXACT` flag (already used by this project's fixture generation, confirmed present in `avcodec.h`) should also be set on every decode-pass `AVCodecContext`, mirroring encoder-side bitexact discipline on the decode side.
- **LGPL decode-only; feature flags audited so GPL code is never silently linked** — no code in this phase touches `vcpkg.json`'s feature list; `aac`/`aac_fixed`/`ac3`/`ac3_fixed`/`mp3`/`mp3float`/`mp2`/`mp2float`/`opus`/`flac` are all core `avcodec` decoders requiring no additional feature flag (`[VERIFIED: avcodec_find_decoder_by_name() success with the project's exact `--features avcodec,avformat,swscale,swresample,dav1d,zlib` build]`).
- **Test data: no media binaries in git; fixtures synthesized bitexact** — D-10's hand-written HE-AAC bitstream writer (`tools/gen_he_aac.py`) and D-13's FLAC/PCM loudness fixtures both comply by construction; no committed binary is introduced by this research's recommendations.
- **Check IDs are forever** — the audio check-ID roster (Claude's Discretion item) and the new `meta.decode_errors`-shaped decode-error check (D-09) both go through the project's existing roster-checkpoint convention before any ID is registered in `src/core/checks.def`.
- **GSD Workflow Enforcement** — this research feeds `/gsd-plan-phase`; no direct file edits were made outside this research investigation (only scratch probes in the session scratchpad, never in the repo tree).

## Summary

This research answers the ten open verification questions 06-CONTEXT.md delegates to research, all against the **actually-linked** FFmpeg 8.1 (`libavcodec 62.28.100` / `libavformat 62.12.100`, vcpkg override `8.1#4`, confirmed via `build/x64-linux/vcpkg_installed/x64-linux/include/libavutil/ffversion.h`), using two methods: (1) reading the real FFmpeg 8.1 source tree vcpkg already unpacked at `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/`, and (2) compiling and running scratch probe programs directly against `build/x64-linux/vcpkg_installed/x64-linux/lib/{libavcodec,libavformat,libavutil}.a`.

Three findings materially change what the planner should build:

1. **D-06 is confirmed exactly, on x86_64, against the linked 8.1 build.** `aac_fixed`, `ac3_fixed`, and the fixed-point decoders registered under the plain names `mp3` and `mp2` are bit-identical across `av_force_cpu_flags(-1|SSE2|0)`; `aac`, `ac3`, `mp3float`, `mp2float` and `opus` are not. Cross-architecture (arm64) bit-exactness — the part D-06 explicitly still requires — could **not** be tested in this sandbox and stays an open item for a CI-leg probe (see Open Questions).
2. **D-07's rejection point is mid-first-frame, not `avcodec_open2`, and is scoped to USAC/xHE-AAC only** — confirmed by reading `libavcodec/aac/aacdec.c:2446-2451` directly. ELD and LD carry no such rejection anywhere in the fixed-point code paths; the `is_fixed` branches there are arithmetic-representation choices (Q31 fixed-point vs float coefficients), not capability gates.
3. **The single most consequential finding for D-15: libavformat already folds MP4 `elst`/iTunSMPB and MKV `CodecDelay` into the exact fields Phase 5's `resolve_priming()` already reads (`skip_samples` packet side data and `codecpar->initial_padding`).** A naïve "container-mechanism tier" that re-reads `elst`/`CodecDelay` from `bmff_scan`/`ebml_scan` for *resolution* purposes would largely be re-deriving what libav has already resolved. The tier's real, non-redundant job is evidence transparency and covering the cases where libav's own resolution doesn't fire (see the D-15 section below for exactly which cases those are).

**Primary recommendation:** build the container-mechanism tier as an **evidence-enrichment and edge-case fallback**, not a parallel resolution path — read `elst`/`CodecDelay` for `--explain` transparency and for the narrow cases (non-empty audio edit lists, fragmented MP4) where libav's own `skip_samples`/`initial_padding` folding doesn't apply — and keep `resolve_priming()`'s existing two-tier order as the dominant path it already is.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Audio stream-parameter checks (AUDIO-01/02/03) | Probe/header pass (`demux_header`, extradata parse) | Decode pass (bounded 1-packet fallback for SBR) | `codecpar` after `avformat_find_stream_info` carries almost everything; only implicit-SBR detection needs a bounded decode (verified below) |
| `audio.priming` (AUDIO-04) | Probe/packet pass (`skip_samples` / `initial_padding`, already shared with Phase 5) | Container-mechanism tier (`bmff_scan`/`ebml_scan`, evidence + edge cases) | libav already resolves the common cases into fields Phase 5 reads; the container tier is a fallback/evidence layer, not the primary source |
| Loudness / true-peak / silence (AUDIO-05/06/07) | New audio decode pass (one sweep) | — | Needs decoded PCM; no way to derive from header metadata |
| `content.audio.sample_hash` (AUDIO-08/09) | Same audio decode pass | Decoder-selection policy (fingerprint-time) | Shares the one-sweep decode with loudness/silence (AUDIO-10/PERF-04) |
| Decode determinism bookkeeping (TRUST-01/02) | `libmediadiff` core (`src/core/model.h`, `src/util/version.cpp`) | — | Path-signature composition must stay libav-opaque per D-07/02-CONTEXT D-07 |
| `mediadiff inspect` audio section (SC1) | CLI (`src/cli/commands/inspect_render.h`) | — | Pure rendering; no new analysis |

## Open Verification Questions — Findings

### 1 (D-06). `mp3`/`mp2` (fixed) and `aac_fixed`/`ac3_fixed` presence and SIMD stability in the linked 8.1

**Presence** `[VERIFIED: build/x64-linux/vcpkg_installed/x64-linux/lib/libavcodec.a, scratch probe this session]` — a probe program linked against the project's own `libavcodec.a`/`libavformat.a`/`libavutil.a` (via `pkg-config --libs --static`, run from `build/x64-linux/vcpkg_installed/x64-linux/lib/pkgconfig`) confirmed `avcodec_find_decoder_by_name()` succeeds for `aac`, `aac_fixed`, `ac3`, `ac3_fixed`, `eac3`, `mp3`, `mp3float`, `mp2`, `mp2float`, `opus`, `flac`, `vorbis`; `libfdk_aac` is absent (confirms D-10's "no HE-AAC encoder anywhere" premise extends to decode too — irrelevant for decode since the native `aac`/`aac_fixed` decoders handle HE-AAC fine, only encode needs fdk).

**Naming nuance worth documenting for the planner:** FFmpeg's fixed-point MP3/MP2 decoders are registered under the *plain* names `"mp3"` / `"mp2"` (symbols `ff_mp3_decoder`, `ff_mp2_decoder`); the float decoders are the *separately-named* `"mp3float"` / `"mp2float"` (symbols `ff_mp3float_decoder`, `ff_mp2float_decoder`). `avcodec_find_decoder(AV_CODEC_ID_MP3)` (the ID-based default lookup libavformat uses when no name is forced) resolves to whichever one is registered first in the codec list — confirmed empirically to be `mp3float`/`mp2` naming pattern below — so `--hash-decoder default` and the auto-preferred path must select **by name**, never by ID, exactly as D-06 anticipates.

**SIMD stability, on this x86_64 host, against the linked 8.1** `[VERIFIED: scratch decode_probe.c this session, see Sources]` — decoded a 2 s 128 kbps mono-tone fixture per codec (synthesized with the pinned 9.0.1 generator, `-flags +bitexact -fflags +bitexact`) through the project's own linked libavcodec/libavformat at three CPU-flag settings (`av_force_cpu_flags(-1)` = auto/AVX-512-capable host, `AV_CPU_FLAG_SSE2`, `0` = pure C), MD5-hashing the concatenated decoded PCM:

| Decoder | auto == sse2 | auto == none | Verdict |
|---|---|---|---|
| `aac` | NO | NO | SIMD-dependent (float) |
| `aac_fixed` | YES | YES | **SIMD-stable** |
| `ac3` | NO | NO | SIMD-dependent (float) |
| `ac3_fixed` | YES | YES | **SIMD-stable** |
| `mp3` (fixed) | YES | YES | **SIMD-stable** |
| `mp3float` | NO | NO | SIMD-dependent (float) |
| `mp2` (fixed) | YES | YES | **SIMD-stable** |
| `mp2float` | NO | NO | SIMD-dependent (float) |

This exactly reproduces the shape of the evidence already gathered against the 9.0.1 generator in 06-CONTEXT.md, now confirmed against the actually-linked 8.1 libraries. **D-06's promotion of `mp3`/`mp2` to class 1 is supported by this evidence on x86_64.**

**What is still NOT verified:** cross-architecture (arm64) bit-exactness. This sandbox has no arm64 target. D-06's own text requires "bit-exact output across architectures, not merely across x86 SIMD levels" before promotion is final — this remains open; see Open Questions. Fixed-point integer C code with no platform-specific intrinsics is *expected* to be architecture-portable (no floating-point rounding-mode or FMA-contraction differences), but this is `[ASSUMED]`, not `[VERIFIED]`, until an arm64-linux/arm64-osx CI leg runs the same probe.

### 2 (D-05). Class-2 path signature composition: `av_get_cpu_flags()` + platform triplet

**`av_get_cpu_flags()` availability and behavior** `[VERIFIED: build/x64-linux/vcpkg_installed/x64-linux/include/libavutil/cpu.h, scratch probe this session]` — declared in the public, installed `libavutil/cpu.h`; a probe call (`av_force_cpu_flags(-1); av_get_cpu_flags()`) on this host returned `0x27fd3db` with `AV_CPU_FLAG_SSE2`/`AVX2`/`AVX512` all set, confirming it reports the real runtime-dispatched feature set, not a static compile-time constant. The header's own doc comment confirms it is "affected by `av_force_cpu_flags()`", i.e., a live, testable code path.

**Important correctness nuance the planner must account for:** the `AV_CPU_FLAG_*` bit values are **not architecture-unique** — `cpu.h` reuses bit `0x1` for `AV_CPU_FLAG_MMX` (x86), `AV_CPU_FLAG_ALTIVEC` (PPC), and `AV_CPU_FLAG_ARMV5TE` (ARM) simultaneously (all defined in the same header, distinguished only by which `#if`-gated block is compiled for the target arch). **The raw integer from `av_get_cpu_flags()` must never be compared or rendered without the platform triplet alongside it** — D-05 already requires the triplet as a separate signature component, so this is not a gap, just a documented reason the two components are inseparable, not merely additive.

**Platform triplet source** `[VERIFIED: CMakeLists.txt:38, CMakePresets.json:16-20]` — `VCPKG_TARGET_TRIPLET` is already a CMake cache variable set per-preset (`x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`, `x64-windows-static-md`) and already consumed in `CMakeLists.txt:38` for library discovery. The existing pattern for exposing a CMake value to `libmediadiff` code is `target_compile_definitions(libmediadiff PRIVATE MEDIADIFF_VERSION="${PROJECT_VERSION}")` (`CMakeLists.txt:285`) — the identical pattern (`target_compile_definitions(libmediadiff PRIVATE MEDIADIFF_VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET}")`) is the natural extension point, consumed only inside `src/util/version.cpp`'s `compose_decode_path_signature()` (the existing seam, `version.cpp:55-67`), preserving `core/`'s "no libav header" rule (D-07 from Phase 2) since the triplet string is not a libav type at all.

**Recommended signature shape:** extend `compose_decode_path_signature()`'s returned string with two more space-separated fields, e.g. `triplet/x64-linux cpuflags/0x27fd3db`, appended after the existing `avcodec/... avformat/... swscale/...` triple — additive to the string format, so `hash.cpp`'s existing 3-key evidence table (`decode_path_class`, `sampling_state`, `normalization`, `hash.cpp:38`) can carry the whole signature as the *value* of `decode_path_class` when class == 2, without adding a 4th key (Claude's Discretion item, resolved here as a recommendation, not a lock).

### 3 (D-07). Which AAC profiles `aac_fixed` cannot decode, and when the failure surfaces

`[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/aac/aacdec.c:2446-2451]` — direct read of the actual FFmpeg 8.1 source tree vcpkg built from:

```c
if (ac->oc[1].m4ac.object_type == AOT_USAC) {
    if (ac->is_fixed) {
        avpriv_report_missing_feature(ac->avctx,
                                      "AAC USAC fixed-point decoding");
        return AVERROR_PATCHWELCOME;
    }
    ...
}
```

**Only `AOT_USAC` (xHE-AAC/USAC, object type 42) is rejected for the fixed-point decoder.** Grep across the whole file for `is_fixed` (`aacdec.c:1167,1178,1318,1629,2448`) shows every other `is_fixed`-gated branch is an arithmetic representation choice (Q30/Q31 fixed-point vs `float` for LTP coefficients and TNS coefficients — `aacdec.c:1318`, `1629`) inside the **shared** GA (General Audio) decode path that also handles `AOT_ER_AAC_LD` and `AOT_ER_AAC_ELD`. **There is no rejection path for LD or ELD under `is_fixed` anywhere in this file** — both decode through the ordinary `decode_frame_ga()` path regardless of `is_fixed`, just with fixed-point arithmetic substituted where the file's own `is_fixed` branches apply. SBR/PS are also fixed-point-capable: `aacsbr_fixed.c`, `aacps_fixed.c`, `aacpsdsp_fixed.c` all exist in the source tree and are compiled into `libavcodec.a` (confirmed present via `nm`/`strings`), so HE-AAC and HE-AACv2 both decode correctly through `aac_fixed`.

**Detection timing — confirmed mid-first-frame, not `avcodec_open2`:** the ASC-parsing path that recognizes `AOT_USAC` (`ff_mpeg4audio_get_config_gb`, `mpeg4audio.c:106-114`) runs during `avcodec_open2`'s `decode_init`, and `ff_aac_usac_config_decode()` (the USAC-specific config parser, `aacdec.c:1083`) is gated on `CONFIG_AAC_DECODER` (the float decoder's compile flag) at **compile time**, not on `ac->is_fixed` at **runtime** — since this build compiles both `aac` and `aac_fixed` from the same translation unit, `CONFIG_AAC_DECODER` is always true, so **`avcodec_open2()` succeeds unconditionally for a USAC stream opened with `aac_fixed`.** The actual `AVERROR_PATCHWELCOME` rejection only fires inside `decode_frame()`, on the **very first packet** sent to `avcodec_send_packet`/`avcodec_receive_frame` — i.e., functionally "first-packet failure," not truly mid-stream, but definitely not open-time.

**Design implication for D-07's resolver:** don't rely on `avcodec_open2()` succeeding as a capability signal — it will always succeed for `aac_fixed` even on USAC content. Two options: (a) attempt-and-catch — open with `aac_fixed`, send the first packet, check for `AVERROR_PATCHWELCOME`, then fall back and re-open with the default decoder (matches D-07's own "decoder is chosen once, before the sweep" if the vetting happens before the real sweep starts); or (b) cheaper — parse the ASC yourself (5-bit object-type field, escape-extended per `get_object_type()`, `mpeg4audio.c:76-81`) and skip `aac_fixed` proactively when `object_type == 42` (`AOT_USAC`), never attempting the doomed open. **Recommendation: (b)** — it's a single 5-13 bit read mediadiff already needs for D-12 (see next section), so it's free once that parser exists, and it avoids a wasted decoder-open-and-discard cycle per stream.

### 4 (D-12). Does `codecpar` carry the SBR-doubled sample rate / HE-AAC profile after `avformat_find_stream_info()` for an implicitly-signaled stream?

**No — confirmed by source read, for both the MP4 demuxer and the decoder itself.**

**MP4/MOV path** `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/isom.c:354-372]`:
```c
if (cfg.object_type == 29 && cfg.sampling_index < 3) // old mp3on4
    st->codecpar->sample_rate = ff_mpa_freq_tab[cfg.sampling_index];
else if (cfg.ext_sample_rate)
    st->codecpar->sample_rate = cfg.ext_sample_rate;
else
    st->codecpar->sample_rate = cfg.sample_rate;
```
`cfg.ext_sample_rate` is populated by `ff_mpeg4audio_get_config_gb()` **only** for explicit signaling: a top-level `AOT_SBR` (5) wrapper object type, or a backward-compatible explicit sync-extension (`mpeg4audio.c:106-114, 133-146`). For a **genuinely implicit** stream (plain `AOT_AAC_LC` ASC, no sync extension at all), `c->sbr` is set to `-1` and `c->ext_sample_rate` stays `0` (`mpeg4audio.c:106`) — so `isom.c` falls through to `st->codecpar->sample_rate = cfg.sample_rate`, the **undoubled base rate**. `codecpar->profile` is not touched by this code path at all.

**Decoder path** `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/aac/aacdec.c:1960-1976, 2375-2393]` — implicit SBR (`m4ac.sbr == -1`) is only discovered when the decoder encounters an `EXT_SBR_DATA` extension payload while actually decoding a raw_data_block:
```c
} else if (ac->oc[1].m4ac.sbr == -1 && ac->oc[1].status == OC_LOCKED) {
    av_log(ac->avctx, AV_LOG_ERROR, "Implicit SBR was found with a first occurrence after the first frame.\n");
    ...
} else if (...) {
    ac->oc[1].m4ac.sbr = 1;
    ac->avctx->profile = AV_PROFILE_AAC_HE;   // or AAC_HE_V2 for PS
}
```
and the doubled `avctx->sample_rate` is only written at `aacdec.c:2381` (`avctx->sample_rate = ac->oc[1].m4ac.sample_rate << multiplier`) **during frame decode**, never during `avcodec_open2`.

**Whether `avformat_find_stream_info()` decodes far enough to trigger this at all is separately doubtful** `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/demux.c:2089-2111]` — `has_codec_parameters()` for `AVMEDIA_TYPE_AUDIO` is satisfied as soon as `avctx->sample_rate` and `avctx->ch_layout.nb_channels` are both non-zero. Since the MP4/MOV demuxer already populates `sample_rate` (the wrong, undoubled value) directly from the ASC with no decode at all, `avformat_find_stream_info()`'s internal `try_decode_frame()` loop can be satisfied and stop probing **before ever decoding a frame** — meaning even a "decode a few frames during probe" behavior is not guaranteed to fire for implicit-SBR AAC. **D-12's literal premise ("no dependence on the decode pass") does not hold against 8.1**, confirming the CONTEXT.md's own fallback clause is the one to build: *mediadiff needs its own bounded one-packet decode per audio stream in the header pass to detect implicit SBR*, exactly as D-12 already anticipates as the fallback.

**A cheaper middle path worth flagging to the planner:** mediadiff can distinguish "explicit ASC-level SBR" from "everything else" **without any decode**, by parsing the ASC itself (object_type == `AOT_SBR` (5) at the top level, or a `0x2b7` sync-extension tail — `mpeg4audio.c:129-146`) — this cleanly separates the "explicit" bucket. What it *cannot* do without a bounded decode is distinguish "implicit SBR present" from "plain AAC-LC, no SBR at all" — both parse identically as a bare `AOT_AAC_LC` ASC. The bounded one-packet decode is therefore only strictly needed to resolve that second ambiguity, and only matters when the ASC-level parse comes back "plain LC" — it can be skipped whenever the ASC-level parse already found explicit signaling.

**API availability caveat** `[VERIFIED: nm build/x64-linux/vcpkg_installed/x64-linux/lib/libavcodec.a]` — `avpriv_mpeg4audio_get_config2` is a defined, linkable symbol in `libavcodec.a`, but its header (`libavcodec/mpeg4audio.h`) is **not installed** by the vcpkg port (confirmed absent from `build/x64-linux/vcpkg_installed/x64-linux/include/libavcodec/`). Calling an `avpriv_`-namespaced symbol with no shipped header is depending on FFmpeg's private ABI, which carries no compatibility guarantee across point releases. **Recommendation: write mediadiff's own minimal ASC/ADTS bit-reader** for the ~30 bits needed (object type, escape extension, sampling-frequency index, sync-extension detection) rather than declaring a hand-written prototype for the private symbol. This mirrors the existing H.264 SPS reader precedent (Phase 4 D-xx, "full correctness for all three `pic_order_cnt_type` branches... to avoid silently misreading real-world streams" — `STATE.md` Phase 04 entry) and the bit-writer pattern already established in `tools/gen_video_fixtures.py` (`BitWriter` class, `u()`/`ue()`/`se()`/`to_bytes()`) which D-10's Python HE-AAC writer should mirror on the encode side.

### 5 (D-10/D-11). Feasibility of a hand-written AAC/SBR bitstream writer

Not independently re-derived in depth this session (would require building and round-trip-decoding a synthesized ASC+SBR payload, which is properly the planner's/executor's implementation task, not a research-phase deliverable) — but the source read above directly informs it:

- **Container choice: prefer a raw ADTS wrapper over an MP4/ASC wrapper for the *explicit* AOT-5 case is NOT possible** — `aacdec.c:2425-2426` explicitly special-cases this: `// USAC can't be packed into ADTS due to field size limitations.` (irrelevant to SBR/AOT-5, but confirms ADTS has real structural limits on which object types it can carry). For the **explicit AOT-5** wrapper specifically, ADTS's own header only encodes a 2-bit `profile` field mapped to `AOT-1` (so ADTS conventionally signals HE-AAC via the *implicit* backward-compatible route — profile=1/LC plus the decoder inferring SBR from bitstream content — rather than a literal AOT-5 tag in the ADTS header itself). **An MP4 `esds`/ASC container is the more direct way to test the explicit-AOT-5 case**, since `isom.c`'s parse (confirmed above) directly branches on `cfg.object_type`/`cfg.ext_sample_rate` from a raw ASC byte string, with no ADTS-specific 2-bit profile field limiting which AOT can be expressed. **Recommendation: build the explicit-vs-implicit pair as two MP4/`esds` fixtures (ASC-only difference), and use ADTS only for the plain non-SBR two-build class-1 proof (D-11), which needs no SBR signaling at all.**
- **Minimum SBR payload shape to target:** a `raw_data_block` containing one `SCE`/`CPE` element followed by a `FIL` (type `ID_FIL`) syntactic element whose `extension_payload()` carries `extension_type == EXT_SBR_DATA` (`aacdec.c:1948` handles this exact case) — the SBR envelope/noise-floor data itself can be the smallest legal payload (single envelope, no PS) since the goal is signaling-mode detection, not audio fidelity.
- Building and round-trip-verifying this bitstream against the linked 8.1 decoders is flagged as an **Open Question** below — it needs an actual decode attempt with a synthesized payload, which is implementation work, not research.

### 6 (D-01). `AV_CODEC_FLAG2_SKIP_MANUAL`, `AV_FRAME_FLAG_DISCARD`, `AV_PKT_DATA_SKIP_SAMPLES`, and `advanced_editlist`

`[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/decode.c:316-401]` — `discard_samples()` is the single function implementing all trim behavior for every generic (`decode_simple_internal`-routed) decoder, AAC/AC-3/MP3/MP2 included:

```c
if ((avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
    if (!side && (avci->skip_samples || discard_padding))
        side = av_frame_new_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES, 10);
    if (side && (avci->skip_samples || discard_padding)) {
        AV_WL32(side->data, avci->skip_samples);
        AV_WL32(side->data + 4, discard_padding);
        ...
        avci->skip_samples = 0;
    }
    return 0;   // <-- returns BEFORE the AV_FRAME_FLAG_DISCARD check below
}
av_frame_remove_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES);
if ((frame->flags & AV_FRAME_FLAG_DISCARD)) {
    avci->skip_samples = FFMAX(0, avci->skip_samples - frame->nb_samples);
    *discarded_samples += frame->nb_samples;
    return AVERROR(EAGAIN);   // <-- frame silently dropped, never reaches the caller
}
```

**With `AV_CODEC_FLAG2_SKIP_MANUAL` set on the `AVCodecContext`:** (1) every decoded sample is delivered to the caller untouched — no trimming, no dropping; (2) the skip/discard-padding amounts the codec would otherwise have applied are instead attached as `AV_FRAME_DATA_SKIP_SAMPLES` frame-side-data (a 10-byte record: `skip_samples`(u32) + `discard_padding`(u32) + `skip_reason`(u8) + `discard_reason`(u8)) so the caller can still see and act on them; (3) this early `return 0` happens **before** the `AV_FRAME_FLAG_DISCARD` check, so a decoder-internal "this whole frame is padding, drop it" signal is **also** neutralized — the frame is delivered rather than swallowed. This exactly reproduces and explains 06-CONTEXT.md's own empirical finding (`-flags2 +skip_manual` makes MP4/MKV/TS decode identically) at the source level.

**Packet-level `AV_PKT_DATA_SKIP_SAMPLES` is untouched by any of this** — that side data lives on the `AVPacket` as delivered by the demuxer (populated by `sti->skip_samples`-driven generic demuxer code, independent of decoder flags), which is exactly what `StreamPacketScan::first_packet_skip_samples` (`packet_scan.h:174`) already reads without decoding, for Phase 5's `resolve_priming()`. **`AV_CODEC_FLAG2_SKIP_MANUAL` only affects the decode-time (frame-level) trim path; it has no bearing on the probe-layer packet-level signal Phase 5 already consumes** — the two are cleanly independent, which is good news for reusing D-15's tiers unchanged.

**`advanced_editlist` default and scope** `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/mov.c:11742-11745]` — `{"advanced_editlist", ..., OFFSET(advanced_editlist), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS}` — **defaults to enabled (1)**. `[VERIFIED: src/probe/demux_session.cpp:179]` — mediadiff calls `avformat_open_input(&ctx, utf8_path.c_str(), nullptr, nullptr)` with a null options dictionary, so **this default is what every mediadiff MP4/MOV open uses today** — no override anywhere in the probe layer. When enabled, `advanced_editlist` causes `mov_fix_index()` (`mov.c:4994`) to rewrite the stream's `AVIndex` according to the edit list **before** any packet is delivered — this is genuinely **whole-packet** exclusion/reordering at the container layer, structurally distinct from the sample-level `skip_samples`/`initial_padding` trim D-01 targets. **Confirms the phase's own framing exactly: whole-packet editlist-driven exclusion is real content the hash should still reflect; only the sample-level priming/padding trim inside a delivered packet's decoded frame is what D-01 says to ignore.** No code change needed here — just don't conflate the two in the sample_hash implementation's mental model.

### 7 (D-15). Where the container-mechanism priming tier gets its inputs — and the redundancy finding

This is the highest-value finding of the whole research pass. Read directly against the FFmpeg 8.1 source:

**MP4 — both `elst` and iTunSMPB already flow through the field Phase 5's resolver reads at tier 1, and `codecpar->initial_padding` for AAC is *always* 0 in this demuxer:**
- `[VERIFIED: vcpkg/.../libavformat/mov.c:5449-5455]` — the `iTunSMPB` custom metadata atom is parsed natively: `if (strcmp(key, "iTunSMPB") == 0) { ... if (priming>0 && priming<16384) sc->start_pad = priming; }`.
- `[VERIFIED: vcpkg/.../libavformat/mov.c:10979-10982]` — for every AAC audio stream, `mov_read_trailer()` unconditionally runs `sti->skip_samples = sc->start_pad;` — folding whatever `start_pad` currently holds (iTunSMPB's value, if no edit list overrode it) into the exact `skip_samples` field that becomes the packet-level `AV_PKT_DATA_SKIP_SAMPLES` side data `StreamPacketScan::first_packet_skip_samples` already reads.
- `[VERIFIED: vcpkg/.../libavformat/mov.c:4341-4351]` — **but** if the track has a genuine non-empty edit list, `sti->skip_samples = msc->start_pad = 0` resets whatever iTunSMPB set, and the edit-list-driven computation inside `mov_build_index()` (the `search_timestamp`/discarded-begin-sample logic starting `mov.c:4355`) takes over instead, ultimately writing its own value back into the same `skip_samples` field (`mov.c:4545`, `msc->start_pad = sti->skip_samples;`). **Either way — iTunSMPB or elst — the resolved value ends up in the same `skip_samples` field, before mediadiff's own code ever runs.**
- `[VERIFIED: vcpkg/.../libavformat/mov.c` — grep for `initial_padding`, only one call site]` — `st->codecpar->initial_padding` is set **only** for Opus tracks (`pre_skip`, line 8594); it is **never** written for AAC. Phase 5's own code comment already independently discovered this (`analyzers.h:485-491`, "MP4's `codecpar->initial_padding` is ZERO"), and this session's source read confirms *why*.

**MKV — `CodecDelay` is written directly into `codecpar->initial_padding` by libav itself:**
- `[VERIFIED: vcpkg/.../libavformat/matroskadec.c:2858-2863]`:
  ```c
  if (track->codec_delay > 0) {
      par->initial_padding = av_rescale_q(track->codec_delay, ...);
      ...
      sti->skip_samples = par->initial_padding;
  }
  ```
  Both `codecpar->initial_padding` **and** `skip_samples` are populated directly from `CodecDelay` — Phase 5's tier-2 fallback (`codecpar_initial_padding`, `analyzers.h:513`) already captures MKV's container mechanism with zero additional code.

**Conclusion for the planner:** `resolve_priming()`'s existing two-tier order (`skip_samples` → `initial_padding` → `unknown`) **already resolves the common MP4-iTunSMPB, MP4-elst, and MKV-CodecDelay cases correctly**, because libav does the folding upstream of mediadiff's probe layer. D-15's "container-mechanism tier" reading raw `elst`/`CodecDelay` from `bmff_scan`/`ebml_scan` is valuable for exactly two things, and the planner should scope it to those:
1. **Evidence transparency** — showing the raw `elst` media_time / raw `CodecDelay` alongside the resolved value in `--explain`/`inspect`, so a disagreement (D-15's own "disagreements ride in evidence" clause) is visible even though it's rare.
2. **Edge cases where the fold doesn't happen** — a genuinely non-empty MP4 audio edit list with `multiple_edits` set combined with `advanced_editlist` producing a value the researcher did not trace to completion here (worth an executor-time empirical check with a real multi-edit fixture), and fragmented MP4 (`mov.c:5231-5236` shows `advanced_editlist` auto-disables itself for fragmented files with no `stts`, a case worth a dedicated fixture).

`ebml_scan.h`'s existing `codec_delay_ns` field (`ebml_scan.h:59`, already populated per Phase 3) can be reused as-is for evidence display; no new probe-layer read is needed for the MKV side at all.

### 8 (D-13/AUDIO-05). libebur128 1.2.6 API shape

`[VERIFIED: build/x64-linux/vcpkg_installed/x64-linux/include/ebur128.h]` (the actual installed header for the linked 1.2.6 port):

- `ebur128_init(unsigned channels, unsigned long samplerate, int mode)` — `mode` is an OR of `EBUR128_MODE_M | EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_SAMPLE_PEAK | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM`; `EBUR128_MODE_I` already implies `EBUR128_MODE_M`, and `EBUR128_MODE_TRUE_PEAK` already implies `EBUR128_MODE_SAMPLE_PEAK | EBUR128_MODE_M` — so `EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK` is the correct, minimal mode mask for AUDIO-05+AUDIO-06 together.
- Feed functions are format-typed, not generic: `ebur128_add_frames_short/int/float/double`. **The chosen decoder's native output format dictates which one to call** — the fixed decoders (`aac_fixed`, `ac3_fixed`, `mp3`, `mp2`) emit `AV_SAMPLE_FMT_S16`/`S32`(planar or packed depending on codec), the float decoders emit `AV_SAMPLE_FMT_FLT`/`FLTP`. Since D-08 already forces one decoder for the whole run, the sink can dispatch once at sweep-start rather than per-frame.
- `ebur128_set_channel(ebur128_state*, unsigned channel_number, int value)` exists for explicit channel-role mapping (`EBUR128_LEFT`, `EBUR128_RIGHT`, `EBUR128_CENTER`, `EBUR128_LEFT_SURROUND`, `EBUR128_RIGHT_SURROUND`, etc., per the header's channel enum) — **not optional for correctness on 5.1/5.1(side) content**, see Common Pitfalls below.
- `ebur128_loudness_global(state, double* out)` and `ebur128_true_peak(state, channel, double* out)` are the read-out calls; both require the corresponding mode bit to have been set at `ebur128_init` time or return `EBUR128_ERROR_INVALID_MODE`.

**Obtaining the `ffmpeg -af ebur128` reference value at corpus-generation time** was not independently re-derived this session (D-13 already locks the fixture choice: FLAC/PCM, reference value committed as text) — this is a straightforward CLI invocation against the pinned 9.0.1 generator (`ffmpeg -i <fixture> -af ebur128=peak=true -f null -`, parsing the `Integrated loudness` and `True peak` lines from stderr) and is implementation work for the fixture-generation script, not a research question.

### 9 (D-16). `av_sync.cpp`'s checkpoint span source

`[VERIFIED: src/analyzers/timeline/av_sync.cpp:640-661, 815-833]` — both the video and audio checkpoint spans are computed identically:
```cpp
const std::optional<std::int64_t> video_declared_duration_ticks =
    demux.stream_info(static_cast<int>(*primary_video)).declared_duration_ticks;
...
if (video_declared_duration_ticks.has_value() && *video_declared_duration_ticks > 0) {
    video_span_ticks = *video_declared_duration_ticks;   // span:declared
} else if (video_pts_span.has_span) {
    video_span_ticks = video_pts_span.span_ticks;         // fallback: raw packet PTS range
}
```
(audio mirrors this exactly, `av_sync.cpp:824-833`.) `declared_duration_ticks` comes from `DemuxSession::stream_info()` — for MP4 this is libav's box-declared, already-priming-adjusted stream duration; for MPEG-TS (which has no box-declared duration field) libavformat *estimates* it from bitrate/PTS range, an estimate that does **not** exclude AAC priming/padding the same way MP4's box duration does. This exactly matches WINDOWS.md #32's documented mechanism.

**D-16's fix point:** the `has_span`/`video_span_ticks`/`audio_span_ticks` selection above is per-file, but the false positive only manifests when the two files being *compared* disagree in priming-knowledge state. This means the fix cannot be entirely local to `av_sync.cpp`'s per-file computation — it needs the same cross-file, evidence-gated mechanism Phase 5 D-10 already built for `av_offset` (per `STATE.md`'s own note: *"05-09: D-10's cross-file basis selection required a Rule 2 extension to `src/compare/tol.cpp` (generic evidence-shape-gated override, never gated on check.id)"*). **The planner should read `src/compare/tol.cpp`'s existing override for `av_offset` as the literal template to mirror for `av_drift`'s span** — this research did not re-derive that mechanism's exact code shape (out of this pass's time budget) and flags it as the concrete next reading task for whoever plans this task.

### 10 (PERF-04/AUDIO-10). Single decode sweep integration

`[VERIFIED: src/probe/pass.h:22-38]` — the `Pass` enum currently has 6 members (`demux_header, packet_scan, parser_scan, bmff_scan, ebml_scan, ts_scan`) with **no decode member**; `PassSet` is a bitset keyed to this enum. `HashChain` (`src/core/value.h:67-72`) is currently `{algorithm, digest, element_count}` with **no per-element digest array** — confirms D-04's premise exactly; the extension (a `std::vector<std::string>` or similar of per-block digests) is new surface, not a refactor of an existing populated field. The existing "no analyzer re-reads the file" invariant (`pass.h`'s own header comment, `PROBE-08`) means the new `Pass::audio_decode` (or similarly named) member must feed a shared `ProbeResults` slot that loudness, silence, and hashing all read from — mirroring the `parser_scan` precedent the phase's own Claude's-Discretion section already names.

`scripts/measure_timeline_perf.sh` and the Phase 5 D-16 reference-file generator (`STATE.md` Phase 05 entries 05-12/05-13, `PERF_BASELINE.txt`, `PERF_RATCHET_TOLERANCE_PERCENT=2`) are the extension points for `PERF-04`'s own ratchet-style ≥ ±2%-tolerance ceiling, not a fresh absolute-time assertion — consistent with how `PERF-03` was ultimately implemented (STATE.md Phase 5, "the gate is a regression check against a committed retired-instruction-count baseline... not the absolute ratio").

## Standard Stack

No new external dependencies are introduced by this phase — every library this phase's requirements touch is already pinned in `vcpkg.json` (`libebur128` 1.2.6, `xxhash`, `tl-expected`, `nlohmann-json`, `fmt`) `[VERIFIED: vcpkg.json]`. **Package Legitimacy Audit is not applicable — no new packages.**

### Core (already pinned, verified present in the linked build)
| Library | Version | Purpose | Evidence |
|---------|---------|---------|----------|
| libebur128 | 1.2.6 (vcpkg) | R128 loudness / true peak (AUDIO-05/06) | `[VERIFIED: build/x64-linux/vcpkg_installed/x64-linux/include/ebur128.h]` |
| FFmpeg (libavcodec/libavformat) | 8.1, `libavcodec 62.28.100` / `libavformat 62.12.100` | audio decode, all AUDIO-* checks | `[VERIFIED: ffversion.h, avcodec_version()/avformat_version() at runtime via mediadiff --version]` |
| xxHash | 0.8.3 (vcpkg, XXH3-128) | `content.audio.sample_hash` chain (AUDIO-08) | Already used elsewhere in the project per PROJECT.md; no phase-specific change |

No Package Legitimacy Audit table is produced — the "no new packages" case; the planner does not need a `checkpoint:human-verify` for any dependency in this phase.

## Architecture Patterns

### System Architecture Diagram

```
                     ┌─────────────────────────────┐
 av_read_frame() ───▶│   audio_decode Pass (NEW)    │
 (existing packet    │   one send/receive loop,     │
  sweep, per stream) │   AV_CODEC_FLAG2_SKIP_MANUAL │
                     │   set, decoder chosen ONCE   │
                     │   (D-07/D-08 policy, before  │
                     │   the sweep starts)          │
                     └──────────────┬───────────────┘
                                    │ decoded PCM frames
                                    │ (untrimmed; skip/discard
                                    │  info riding as frame
                                    │  side data, D-01)
                     ┌──────────────┼───────────────┬───────────────┐
                     ▼              ▼               ▼               │
              ┌───────────┐ ┌─────────────┐ ┌───────────────┐       │
              │ libebur128│ │ silence/edge│ │ XXH3-128 block│       │
              │ sink      │ │ span sink   │ │ hash chain    │       │
              │(AUDIO-05/6)│ │(AUDIO-07)   │ │(AUDIO-08/09)  │       │
              └───────────┘ └─────────────┘ └───────────────┘       │
                     │              │               │               │
                     └──────────────┴───────────────┴───────────────┘
                                    ▼
                       ProbeResults::audio (shared slot)
                                    ▼
                    Analyzers (audio.loudness.*, audio.silence.*,
                    content.audio.sample_hash) read the shared slot —
                    NEVER re-open the file, NEVER re-decode (PROBE-08)

  Header pass (existing, no decode):
  avformat_find_stream_info() ──▶ codecpar (sample_rate/channels/layout, D-12
                                   confirmed NOT SBR-doubled for implicit case)
                                ──▶ mediadiff's own ASC/ADTS bit-parse
                                   (object_type, explicit-SBR detection,
                                   D-12's no-decode fast path)
                                ──▶ bounded 1-packet decode fallback
                                   (only when ASC parse is ambiguous: plain
                                   AOT_AAC_LC with no sync extension)

  Priming resolution (extends Phase 5, unchanged tier order):
  StreamPacketScan::first_packet_skip_samples (tier 1)
     — already carries MP4 elst AND iTunSMPB (D-15 finding)
  → codecpar->initial_padding (tier 2)
     — already carries MKV CodecDelay (D-15 finding); always 0 for MP4/AAC
  → bmff_scan/ebml_scan container-mechanism tier (evidence + edge cases ONLY)
  → unknown
```

### Recommended Project Structure
```
src/analyzers/audio/       # audio.* checks (currently .gitkeep only)
  stream_params.cpp        # AUDIO-01/02/03: codec/profile/sample_rate/layout/SBR
  priming.cpp               # AUDIO-04: extends resolve_priming(), container tier
  loudness.cpp              # AUDIO-05/06: libebur128 sink
  silence.cpp                # AUDIO-07: edge/dropout span detection
src/analyzers/content/      # audio half of content.* (currently .gitkeep only)
  sample_hash.cpp           # AUDIO-08/09: XXH3-128 block chain
src/probe/
  audio_decode.h/.cpp        # NEW Pass member + shared decode sweep + 3 sinks
tools/
  gen_he_aac.py              # D-10's hand-written AAC/SBR bitstream writer
                              # (mirrors gen_video_fixtures.py's BitWriter pattern)
```

### Pattern: bounded ASC-only parse before any decode (D-12's no-decode fast path)
```c
// Illustrative — mediadiff's own bit reader, NOT avpriv_mpeg4audio_get_config2
// (that symbol is unshipped-header private ABI, see D-12 section above).
// Reads only the fields needed to distinguish explicit vs "ambiguous" SBR
// signaling, mirroring mpeg4audio.c's own get_object_type()/get_sample_rate()
// bit layout (5-bit object type, 4-bit sampling index or 24-bit escape,
// 4-bit channel config), verified against mpeg4audio.c:76-114.
int object_type = read_bits(5);
if (object_type == 31 /* AOT_ESCAPE */) object_type = 32 + read_bits(6);
// sampling_index, chan_config reads elided...
bool explicit_sbr = (object_type == 5 /* AOT_SBR */);
// explicit_sbr == true  -> AUDIO-03 reports "explicit" with no decode.
// explicit_sbr == false -> ambiguous; fall back to the bounded 1-packet
//                          decode ONLY in this branch (implicit vs "no SBR
//                          at all" cannot be told apart from the ASC alone,
//                          per aacdec.c:1960-1976 confirmed above).
```

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| EBU R128 loudness / true peak math | A custom K-weighting filter + gating algorithm | `libebur128`'s `ebur128_loudness_global`/`ebur128_true_peak` | Already pinned, already BS.1770/EBU R128 reference-grade (see PROJECT.md's own confirmation), and D-13's ±0.1 LU acceptance criterion is against `ffmpeg -af ebur128` — both consumers of the same standard, hand-rolling risks a two-implementation drift |
| AudioSpecificConfig/ADTS bit parsing via FFmpeg's private API | Declaring a hand-written prototype for `avpriv_mpeg4audio_get_config2` | mediadiff's own small bit-reader over the ~30 bits actually needed | The private symbol's header isn't shipped by the vcpkg port (`[VERIFIED]` above) — depending on it is depending on FFmpeg's internal ABI with no compatibility guarantee, for a well-specified, narrow bitstream this project already has precedent for hand-parsing (H.264 SPS, Phase 4) |
| XXH3-128 chaining | A custom rolling hash | `xxhash`'s `XXH3_128bits`/streaming API (already used elsewhere in the project) | Already pinned; a hand-rolled chain has no reason to exist alongside a correct, fast, already-integrated one |

**Key insight:** every "don't hand-roll" line in this phase resolves to "a library you already have pinned handles this correctly" — the one new hand-rolled piece (the ASC/ADTS bit parser) is a deliberate, narrow exception matching an established project pattern (Phase 4's SPS reader), not a library gap.

## Common Pitfalls

### Pitfall 1: Trusting `avcodec_open2()` success as a capability signal for `aac_fixed`
**What goes wrong:** code that opens `aac_fixed` and treats a successful `avcodec_open2()` as "this decoder can handle this stream" will silently proceed into a first-packet `AVERROR_PATCHWELCOME` failure on USAC/xHE-AAC content.
**Why it happens:** the USAC config-decode branch (`aacdec.c:1083`) is gated on the float decoder's `CONFIG_AAC_DECODER` compile flag, which is always true in a build that also ships `aac`, so `avcodec_open2()` never sees the `is_fixed` capability gate — only `decode_frame()` does (`aacdec.c:2446-2451`).
**How to avoid:** parse the ASC's object type before deciding to prefer `aac_fixed` at all (see D-12/D-07 sections above); never rely on open success alone.
**Warning signs:** a fixture that opens cleanly then fails on the first `avcodec_receive_frame()` call with `AVERROR_PATCHWELCOME`.

### Pitfall 2: Rebuilding a redundant priming resolver for the container-mechanism tier
**What goes wrong:** implementing D-15's container-mechanism tier as a full, independent priming *computation* (re-deriving skip-sample counts from raw `elst`/`CodecDelay`) duplicates work libav already does, and risks disagreeing with the value `resolve_priming()` already reports for the same file.
**Why it happens:** the phase description reads naturally as "add a new tier that computes priming from the container," but the actual libav behavior (traced above) means the container mechanism's output is *already inside* the fields tier 1/2 read.
**How to avoid:** scope the container-mechanism tier to evidence display + the specific edge cases (non-empty multi-edit MP4, fragmented MP4) named in the D-15 section above; verify with a real multi-edit fixture whether the fold still holds before writing a parallel computation.
**Warning signs:** a fixture where the "container mechanism" tier's computed value and `resolve_priming()`'s tier-1/2 value disagree on an ordinary (single edit list or no edit list) file — that disagreement means one of the two computations has a bug, since they should be reading the same underlying fact.

### Pitfall 3: Wrong or default EBU R128 channel mapping on 5.1(side) content
**What goes wrong:** libebur128's channel-role weighting (surround channels get +1.5 dB per BS.1770) depends on `ebur128_set_channel()` being called correctly per channel index; if mediadiff feeds interleaved PCM in `AVChannelLayout` order without mapping to `EBUR128_LEFT_SURROUND`/`EBUR128_RIGHT_SURROUND` for side-vs-back surround channels, the loudness figure will be numerically wrong (not crash-wrong, silently-wrong) on exactly the 5.1 vs 5.1(side) content AUDIO-02 already flags as a distinct case.
**Why it happens:** libebur128 has no built-in knowledge of `AVChannelLayout`; the mapping is the caller's job, and it's easy to assume "5.1 is always L,R,C,LFE,Ls,Rs" and skip the explicit `ebur128_set_channel()` calls.
**How to avoid:** map every channel explicitly from the decoded `AVChannelLayout`'s per-position codes (`AV_CHAN_SIDE_LEFT`/`AV_CHAN_BACK_LEFT` etc.) to the corresponding `EBUR128_*` enum value at sweep-start, for every stream, not just as a default-assumed 5.1 layout.
**Warning signs:** a loudness value that's correct for stereo/mono fixtures but drifts from the `ffmpeg -af ebur128` reference specifically on 5.1(side) fixtures.

### Pitfall 4: Conflating `advanced_editlist`'s whole-packet remap with D-01's sample-level trim target
**What goes wrong:** treating any editlist-driven difference in decoded output as something `content.audio.sample_hash` should ignore (per D-01's "hash the untrimmed essence") is wrong when the editlist causes genuinely different packets to be delivered (a real content difference), versus right when the editlist only expresses sample-level priming/padding.
**Why it happens:** both are "editlist-related" and easy to lump together conceptually.
**How to avoid:** remember `advanced_editlist` (default-on, confirmed above) operates on the `AVIndex` — which packets `av_read_frame()` delivers at all — while D-01's `SKIP_MANUAL` mechanism operates on samples *within* an already-delivered packet's decoded frame. Only the latter is what D-01 says to ignore.
**Warning signs:** a fixture pair that differs by a genuine mid-file edit-list splice hashing equal when it shouldn't.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `aac_fixed`/`ac3_fixed`/`mp3`(fixed)/`mp2`(fixed) are bit-exact across **architectures** (arm64 vs x86), not just across x86 SIMD levels | Q1 (D-06) | If wrong, D-06's class-1 promotion for `mp3`/`mp2` (and the existing AAC/AC-3 class-1 status) would need to degrade to class 2 on arm64 legs specifically — a correctness regression in cross-arch snapshot comparison, exactly the failure mode TRUST-02 exists to catch. Low actual risk (fixed-point integer C code has no known FP-rounding-mode/FMA-contraction architecture dependency), but unverified in this session. |
| A2 | The synthesized HE-AAC bitstream (D-10) will be accepted by the linked 8.1 `aac`/`aac_fixed` decoders on the first attempt | Q5 (D-10) | If the hand-written payload is rejected, D-10's whole fixture strategy needs iteration before any AUDIO-03 fixture pair exists; this is normal bitstream-writer development risk, not a research gap, but no synthesized payload was actually built and decoded in this research pass. |
| A3 | A genuinely non-empty, multi-entry MP4 audio edit list (the `multiple_edits` case in `mov.c:4341-4351`) produces a `skip_samples` value that still correctly reflects real priming, rather than a value the container-mechanism tier must independently correct | Q7 (D-15) | If wrong, the container-mechanism tier is more than an evidence/edge-case layer and needs to be a real correcting computation for this specific case — worth an empirical fixture test before finalizing the tier's scope. |

## Open Questions

1. **Cross-architecture bit-exactness for `aac_fixed`/`ac3_fixed`/`mp3`/`mp2` (D-06)**
   - What we know: bit-exact across all tested x86_64 SIMD levels (auto/SSE2/none) against the linked 8.1 build, this session.
   - What's unclear: arm64 behavior — no arm64 target available in this sandbox.
   - Recommendation: run the same `decode_probe.c`-style probe (or the equivalent `ffmpeg -cpuflags` comparison) on the arm64-linux/arm64-osx CI legs before finalizing D-06's promotion; this is a natural fit for a `checkpoint:human-verify`-gated CI-only verification task in the plan, not something this research pass can close.

2. **Does the hand-written HE-AAC/SBR payload (D-10) actually decode on the linked 8.1 `aac`/`aac_fixed` decoders?**
   - What we know: the exact code paths that must accept it (`EXT_SBR_DATA` extension handling, `aacdec.c:1948-1976`; explicit-AOT-5 top-level parse, `mpeg4audio.c:106-114`).
   - What's unclear: whether a from-scratch bit-level construction round-trips cleanly on the first attempt, or needs iteration (Huffman table coverage, envelope/noise-floor encoding correctness).
   - Recommendation: build it as an early task with its own `--selftest` (mirroring `tools/gen_video_fixtures.py`'s convention) that decodes its own output before any check-roster work depends on it.

3. **Exact `mov.c` behavior for the `multiple_edits` MP4 case, and for fragmented MP4 (`advanced_editlist` auto-disabled, `mov.c:5231-5236`)**
   - What we know: the single-edit and no-edit cases both fold correctly into `skip_samples`, confirmed by source read.
   - What's unclear: whether a genuinely multi-segment edit list produces a `skip_samples` value the resolver can trust as-is, or one that needs the container-mechanism tier to correct.
   - Recommendation: build one multi-edit-list MP4 fixture and one fragmented-MP4-with-editlist fixture early, and diff mediadiff's resolved priming against `ffprobe -show_entries stream=start_time` / manual `elst` inspection before finalizing D-15's tier scope.

4. **`av_sync.cpp`'s cross-file span-basis mechanism (D-16)** — this research identified the edit site and the precedent to mirror (`src/compare/tol.cpp`'s existing `av_offset` override) but did not trace the exact override code. Recommend the planner read `tol.cpp`'s current `av_offset` handling in full before writing the `av_drift` span extension.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Linked FFmpeg (vcpkg) | All AUDIO-* decode work | ✓ | 8.1 (`libavcodec 62.28.100`) | — |
| Pinned generator FFmpeg (fixture synthesis) | Fixture generation, reference loudness values | ✓ | 9.0.1 (`.ffmpeg-pinned/linux-x86_64/ffmpeg`) | — |
| Python 3 (D-10's bitstream writer) | `tools/gen_he_aac.py` | ✓ | 3.12.3 (project floor: ≥3.11) | — |
| libfdk_aac (any HE-AAC encoder) | Would simplify D-10 if present | ✗ | — | D-10's hand-written bitstream writer (already the locked decision, not blocked) |
| arm64 build target | Cross-arch verification of D-06's class-1 promotion | ✗ (this sandbox is x86_64 only) | — | Defer to CI arm64-linux/arm64-osx legs (Open Question 1) |

**Missing dependencies with no fallback:** none — every gap above already has a locked, working fallback (D-10's synthesis path, CI-leg deferral for arm64).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 3.15.3 (already integrated, `tests/integration/`, `tests/unit/`) |
| Config file | `CMakeLists.txt` (CTest integration via `catch_discover_tests`) |
| Quick run command | `ctest --test-dir build/x64-linux -R "audio\|content_audio" --output-on-failure` |
| Full suite command | `ctest --test-dir build/x64-linux --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| AUDIO-01 | codec/profile/sample_rate/sample_fmt/channels/layout parameter checks | integration | `mediadiff_integration_tests -R audio_stream_params` | ❌ Wave 0 |
| AUDIO-02 | 5.1 vs 5.1(side) distinguished, layout loss = regression | integration | `mediadiff_integration_tests -R audio_layout` | ❌ Wave 0 |
| AUDIO-03 | HE-AAC SBR implicit/explicit detection | integration, needs D-10 fixture | `mediadiff_integration_tests -R audio_profile_sbr` | ❌ Wave 0 (blocked on D-10 fixture) |
| AUDIO-04 | priming resolver + container round-trip stability | integration | `mediadiff_integration_tests -R audio_priming` | ❌ Wave 0 |
| AUDIO-05 | loudness ±0.1 LU vs `ffmpeg -af ebur128` reference | integration, golden-text reference | `mediadiff_integration_tests -R audio_loudness` | ❌ Wave 0 |
| AUDIO-06 | true peak asymmetric −1.0 dBTP fail | integration | `mediadiff_integration_tests -R audio_true_peak` | ❌ Wave 0 |
| AUDIO-07 | silence edge/dropout span detection | integration | `mediadiff_integration_tests -R audio_silence` | ❌ Wave 0 |
| AUDIO-08 | sample_hash first-divergent-block report | integration | `mediadiff_integration_tests -R audio_sample_hash` | ❌ Wave 0 |
| AUDIO-09 | fixed-decoder auto-preference + `--hash-decoder default` opt-out | integration | `mediadiff_integration_tests -R audio_hash_decoder` | ❌ Wave 0 |
| AUDIO-10 / PERF-04 | single decode sweep, < 4 s on 10-min reference | perf harness | `scripts/measure_timeline_perf.sh --audio` (extension) | ❌ Wave 0 (extends existing harness) |
| TRUST-01 | per-hashed-stream decode_path record | unit + integration | `mediadiff_unit_tests -R decode_path_record` | ❌ Wave 0 |
| TRUST-02 | class-2 cross-path `skipped:hash_incomparable` | integration | `mediadiff_integration_tests -R hash_class2_skip` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** targeted `ctest -R <new-check-family>` subset
- **Per wave merge:** full `mediadiff_integration_tests` + `mediadiff_unit_tests`
- **Phase gate:** full suite + `scripts/measure_timeline_perf.sh` (extended) green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/integration/test_audio_stream_params.cpp` — AUDIO-01/02/03
- [ ] `tests/integration/test_audio_priming.cpp` — AUDIO-04, extends existing `timeline_findings.h` declared-set harness
- [ ] `tests/integration/test_audio_loudness.cpp` — AUDIO-05/06, needs committed `ffmpeg -af ebur128` text reference (D-13)
- [ ] `tests/integration/test_audio_silence.cpp` — AUDIO-07
- [ ] `tests/integration/test_audio_sample_hash.cpp` — AUDIO-08/09
- [ ] `tools/gen_he_aac.py` with `--selftest` — the D-10 fixture writer itself, blocking AUDIO-03's fixture pair
- [ ] `docs/checks/audio.*.md` and `docs/checks/content.audio.sample_hash.md` files (DOC-01 build-enforced) — none exist yet, 87 registered checks today with zero `audio.*` (`[VERIFIED: STATE.md/06-CONTEXT.md code_context section, src/core/checks.def]`)

## Security Domain

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | no | mediadiff is a local CLI, no auth surface |
| V3 Session Management | no | not applicable |
| V4 Access Control | no | not applicable |
| V5 Input Validation | **yes** | every decoded byte comes from an untrusted media file; the new audio decode pass is the first place this phase feeds attacker-controlled bytes into `avcodec_send_packet`/`avcodec_receive_frame` in bulk. Existing project convention already covers this class: `PROBE-09`'s "unparseable structure degrades to `skipped:unparsed_mechanism`... never a crash" and the fuzz-style byte-flip test precedent (`03-10`, "PRNG-seeded byte-flip test asserts never crashes, stays in bounds") should extend to the new decode pass, not just the header/packet passes. |
| V6 Cryptography | no | XXH3-128 is a non-cryptographic hash used for content-identity comparison, not a security boundary |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|----------------------|
| Malformed AAC/AC-3/MP3 bitstream triggering a decoder crash or hang during the new decode pass | Denial of Service | Reuse `DemuxSession`'s existing wall-clock budget/interrupt-callback pattern (`PROBE-01`) for the decode pass; rely on libav's own decoder-internal bounds checking (already the project's trust boundary for the header/packet passes) rather than re-validating bitstream syntax in mediadiff |
| A crafted hand-written HE-AAC fixture (D-10) accidentally shipping a payload that triggers undefined behavior in the fixed-point SBR path (`aacsbr_fixed.c`) | Denial of Service (self-inflicted, test-only) | Same class as any fuzz-adjacent input; the `--selftest` decode-round-trip (Open Question 2) doubles as a sanity check that the synthesized bitstream is well-formed before it ever reaches CI |

## Sources

### Primary (HIGH confidence — direct source read or runtime probe, this session)
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/aac/aacdec.c` — USAC/`is_fixed` rejection point, implicit-SBR discovery, profile assignment
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/mpeg4audio.c` — ASC parsing, explicit vs implicit `sbr`/`ps` field semantics
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/decode.c` — `discard_samples()`, `AV_CODEC_FLAG2_SKIP_MANUAL`, `AV_FRAME_FLAG_DISCARD` handling
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/isom.c` — MP4 `esds`/ASC → `codecpar` mapping
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/mov.c` — `iTunSMPB` parse, `advanced_editlist` default and behavior, `start_pad`/`skip_samples` folding
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/matroskadec.c` — `CodecDelay` → `codecpar->initial_padding` mapping
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/demux.c` — `has_codec_parameters()` audio-satisfaction gate
- `build/x64-linux/vcpkg_installed/x64-linux/include/{libavutil/cpu.h,ebur128.h,libavutil/ffversion.h}` — installed public API surface
- Scratch probe programs compiled and run this session against `build/x64-linux/vcpkg_installed/x64-linux/lib/{libavcodec,libavformat,libavutil}.a` (decoder presence check; SIMD-level MD5-comparison decode probe across `aac`/`aac_fixed`/`ac3`/`ac3_fixed`/`mp3`/`mp3float`/`mp2`/`mp2float`; `av_get_cpu_flags()` probe) — scratch source at `/tmp/.../scratchpad/{probe.c,decode_probe.c,cpuflags_probe.c}`, not committed to the repo
- Project source: `src/probe/{demux_session.cpp,packet_scan.h,pass.h}`, `src/analyzers/timeline/{av_sync.cpp,analyzers.h}`, `src/compare/hash.cpp`, `src/core/value.h`, `src/util/version.{h,cpp}`, `src/cli/commands/dir.cpp`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`

### Secondary (MEDIUM confidence)
- 06-CONTEXT.md's own already-gathered empirical measurements against the 9.0.1 generator (SIMD dependence table, trim-policy table) — used as the shape to reproduce against 8.1, not as a standalone source

### Tertiary (LOW confidence / not independently re-verified this session)
- D-10/D-11's exact bitstream-construction feasibility (Open Question 2) — informed by source read of the acceptance path, not by an actual constructed-and-decoded payload
- The `multiple_edits`/fragmented-MP4 edge cases' actual resolved `skip_samples` value (Open Question 3) — traced through source but not empirically tested with a real fixture

## Metadata

**Confidence breakdown:**
- Determinism-class findings (D-05/D-06/D-07/D-01): HIGH — direct source read of the linked 8.1 tree plus a same-session runtime probe against the linked libraries
- SBR signaling detection (D-12): HIGH for what `codecpar`/decoder do and don't expose; MEDIUM for the recommended ASC-bit-parser approach (sound reasoning, not yet implemented/tested)
- Priming resolver redundancy (D-15): HIGH for the MP4/MKV common-case fold; MEDIUM for the multi-edit/fragmented-MP4 edge cases (traced, not fixture-tested)
- Loudness API shape (D-13): HIGH (installed header read)
- `av_sync.cpp` span mechanism (D-16): MEDIUM — edit site and precedent identified; exact `tol.cpp` override code not traced in this pass
- Cross-architecture bit-exactness (D-06's arm64 clause): explicitly NOT verified — flagged as Open Question 1, ASSUMED only

**Research date:** 2026-09-20
**Valid until:** effectively pinned to the `ffmpeg` vcpkg override (`8.1#4`) — re-verify if `vcpkg.json`'s override changes to a new FFmpeg version or port revision (30-day estimate otherwise, since FFmpeg internals rarely change on a shorter cadence than that within one major version)
