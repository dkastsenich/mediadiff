# Pitfalls Research

**Domain:** Media-aware regression diffing (FFmpeg-based container/video/timeline/audio/content analysis) used as a CI merge gate
**Researched:** 2026-08-12
**Confidence:** MEDIUM-HIGH — grounded in FFmpeg source semantics, trac/mailing-list evidence, spec clauses (ISO 13818-1, ISO 14496-12, ITU-T H.273/SEI), and documented tool war stories; a few decoder-internals claims (slice-threading edge cases, mp3/mp3float default selection) are marked LOWER confidence and should be verified empirically in the phase-1/5/6 CI determinism suites rather than trusted as fact.

Pitfalls are ranked within each section by **likelihood of producing a false positive in production** — the project's declared P0. Each entry maps to one of the seven phases: 0 scaffold, 1 core engine, 2 container, 3 video, 4 timeline, 5 audio, 6 content/size.

---

## Verdict: Is the decode-determinism class system (doc 01 §7) sufficient?

**No — it is necessary but not sufficient.** The three-class model (1 `bitexact-everywhere`, 2 `bitexact-same-path`, 3 `nondeterministic`) is the right *mechanism*: mechanically refusing a `hash` comparison across incompatible paths instead of computing a fake diff is exactly what prevents the worst class of false positive. But it has three concrete blind spots the roadmap should close explicitly, not assume away:

1. **It only guards the `hash` semantic.** The precondition machinery in doc 01 §3 (`hash` — "preconditions: same decode-path class, same sampling, same normalization") has no equivalent for `±tol` semantics. `content.video.perceptual` (SSIM) and `quality.*` (PSNR/SSIM/VMAF) are exactly as fragile to a decode/scale-path change as a hash chain is — a metric computed on frames that decoded or scaled differently is comparing apples to oranges — yet nothing forces those checks to record and compare a path signature the way `hash` does. **This is the single most likely channel for a false positive that the class system was built to prevent, sneaking in through the door it doesn't guard.**
2. **The class-2 "path signature" is defined only for hwaccel.** Doc 01 §7 says class-2 records "a path signature (build id + device/driver when hwaccel)" — but for CPU-only class-2 decoders (float `aac`/`opus`/`mp3`/`eac3`), what is "build id"? If it resolves to mediadiff's own git SHA rather than the linked libavcodec version + compiler + optimization flags, then two mediadiff builds against different vcpkg-pinned FFmpeg minor versions (which happens on every routine dependency bump) will report the *same* path signature while producing *different* float-decode bytes — a silent false positive in exactly the P0 category the project fears. **Fix: the class-2 path signature must include a hash of (libavcodec semver + build config string + compiler ID + optimization flags), not just a device/driver string, and must be non-optional even when hwaccel is absent.**
3. **It is scoped to `libavcodec` decode only; `libavformat` (demux/timeline) drift is entirely outside it.** Doc 04 (timeline) never references the class system. A libavformat version bump that changes edit-list handling, `AVFMT_FLAG_GENPTS` behavior, or `AVStream->start_time` computation can shift `timeline.start` / `timeline.av_offset` with **no mechanical "downgrade to skipped" safety net** — the same category of risk the class system exists to prevent for decode, left unaddressed for demux. Recommend either extending the class-system pattern (record a `libavformat` path signature, degrade timeline `hash`/`exact` comparisons across signature drift) or explicitly documenting which timeline checks are immune (pure integer/rational math on values libavformat *reports*, not values it *computes/repairs* — see Pitfall T1 below) and which are not.

Additionally: swscale itself changed its internal math model in FFmpeg 9.0 — "the constant math driving every conversion moved to a new 64-bit rational type, computed exactly instead of in floating point" (confirmed via FFmpeg 9.0 release notes) — meaning `content.video.perceptual`'s SSIM pipeline (`SWS_AREA` downscale) is not just theoretically but *actually, recently* version-fragile. Given the project's own UC2 use case is "FFmpeg 8→9 migration over a 240-file corpus," mediadiff must not itself be blind to the same class of drift it exists to detect in *user* pipelines. **Recommend: record linked FFmpeg component versions (avcodec, avformat, swscale, swresample, libvmaf) in the fingerprint envelope and wire them into the same precondition-degrade machinery as decode-path class, for every check that touches DecodeSession — not just `hash`.**

---

## Critical Pitfalls

Ordered by estimated false-positive likelihood in production, highest first.

### Pitfall 1: Class-2 path signature under-specified for CPU/toolchain drift (not just hwaccel)

**What goes wrong:** Two mediadiff release builds — say v0.3.0 built against FFmpeg 6.1 and v0.4.0 built against FFmpeg 7.0 after a routine vcpkg bump — decode the same file through the float `aac` decoder and produce different sample bytes (FPU rounding-mode differences, SIMD kernel path selection, or an upstream decoder bugfix). Doc 01 §7's path signature is only specified as "device/driver when hwaccel" — with no hwaccel in play, nothing distinguishes the two builds, so a class-2 hash comparison between them silently *passes the precondition* and produces a fabricated `fail` finding on a file that never actually changed.

**Why it happens:** hwaccel is the intuitively obvious source of cross-machine nondeterminism, so it's the one that gets modeled explicitly; toolchain/library-version drift within a single vendor's decoder is easy to forget because "it's the same code" — except it isn't, once a dependency pin moves.

**How to avoid:** Make the class-2 path signature `hash(libavcodec_version + build_config_string + compiler_id + opt_flags [+ device/driver when hwaccel])`, always populated, never optional. Treat mediadiff's own FFmpeg pin bump as a first-class release event that changes path signatures for every class-2 decoder — document it in the release notes and in the fingerprint envelope's decode-path table.

**Warning signs:** A `hash` comparison against a snapshot fingerprinted by a previous mediadiff release starts failing on files known not to have changed, right after a dependency bump; CI's own idempotence suite (doc 01 §12) only exercises "compare twice within the same run/build," which will never catch this — it needs an explicit "compare snapshot from build N-1 against build N" cross-version test.

**Phase to address:** 1 (path-signature schema in the fingerprint envelope), 5 (audio class-2 decoders), 6 (video hwaccel class-2)

---

### Pitfall 2: Non-hash semantics (perceptual/quality) aren't gated by the class/precondition system at all

**What goes wrong:** `content.video.perceptual` (SSIM) and `quality.*` use `±tol` semantics, which doc 01's precondition table never mentions. If the underlying decode or swscale path differs between the runs producing baseline and candidate (different mediadiff build, different `--hwaccel`, different thread count triggering an unrelated FFmpeg code path), the SSIM/PSNR/VMAF scores can shift by a tolerance-crossing amount even though the *content* did not change — and unlike `hash`, there is no `skipped:hash_incomparable`-style mechanical escape hatch. The check just reports a numeric drop as if it were a real regression.

**Why it happens:** The precondition machinery was designed around `hash`'s exact-match nature; `±tol` semantics look "already tolerant" so it's easy to assume the tolerance absorbs path variance — it absorbs *noise*, not systematic *bias* from an algorithm change (e.g., swscale's FFmpeg 9.0 float→exact-rational rewrite is a constant, directional shift, not noise).

**How to avoid:** Record a `scale_path_signature` (swscale version/config, `SWS_AREA` flag set, dst dimensions) and a `decode_path_signature` in every fingerprint that runs DecodeSession, and apply the *same* skip-not-fake-fail rule to `±tol` checks whose signatures mismatch across baseline/candidate as already applies to `hash`. At minimum, surface signature mismatch as a visible caveat (`evidence.path_mismatch: true`) attached to the finding, so a human reviewing a borderline SSIM drop can see "this may be a tool-version artifact, not a content regression" instead of trusting the number blind.

**Warning signs:** SSIM/VMAF scores drift by a small-but-tolerance-crossing amount correlated with mediadiff version bumps rather than with any change to the corpus; VMAF's own FAQ explicitly warns scores are only comparable "same model, same tool version, same frames, same resolution" — the same caveat applies transitively to mediadiff's own build.

**Phase to address:** 6 (perceptual/quality checks), with the signature-recording plumbing designed in 1 alongside the class system

---

### Pitfall 3: `AVFMT_FLAG_GENPTS` (or equivalent demux "repair") silently mutates the timeline being measured

**What goes wrong:** If mediadiff's `DemuxSession`/`PacketScan` opens files with PTS-generation or other libavformat "repair" flags enabled (a common default in tools built to "just play the file"), libavformat will synthesize or overwrite presentation timestamps from DTS+reordering heuristics *before* doc 04's analysis ever sees the raw stream. Doc 04 is explicit that "raw values are preserved in evidence" and treats `AV_NOPTS_VALUE` as first-class `absent` — but that discipline is worthless if the demuxer has already replaced missing/ambiguous PTS values with its own guesses upstream of the probe. Two files that differ only in whether their PTS needed repair (a real, diagnosable difference) become indistinguishable; worse, libavformat's repair heuristic itself has version-dependent behavior, so identical raw bitstreams demuxed by two mediadiff builds linking different FFmpeg versions can yield different "raw" timelines despite both being called "raw."

**Why it happens:** GENPTS-style flags are usually turned on by default in general-purpose players/transcoders because *playback* wants a best-effort continuous timeline, not a diagnostic one. A tool whose entire value proposition is "show me exactly what changed" inherits that default at its own peril if nobody audits it.

**How to avoid:** Explicitly do **not** set `AVFMT_FLAG_GENPTS` (or any other repair flag) on the probe/scan passes; verify via an integration test that a file with genuinely missing PTS reports `absent`, not a synthesized value, through `DemuxSession`. If GENPTS-equivalent repair is ever needed for a specific check, gate it behind an explicit, recorded flag in the fingerprint envelope (never silent).

**Warning signs:** `timeline.start`/`timeline.discontinuities` findings that appear or disappear correlated with an FFmpeg dependency bump rather than with any change to input files; `AV_NOPTS_VALUE` never showing up as `absent` in fingerprints even for known-pathological fixtures (raw H.264 elementary streams, some malformed TS captures).

**Phase to address:** 2 (DemuxSession flag policy), verified in 4 (timeline fixtures)

---

### Pitfall 4: `linesize` vs cropped display dimensions — a second allocator-padding trap beyond the one already mitigated

**What goes wrong:** Doc 06 §2.1 correctly hashes `bytes_per_row(width) × height`, never `linesize`, which kills the classic allocator-padding false positive. But there is a sibling trap the doc doesn't mention: since FFmpeg 4.4, `AVFrame` carries separate `crop_top/bottom/left/right` fields distinct from `width`/`height` (the *coded* dimensions), used for HEVC/VP9/AV1 conformance-window cropping. If the hashing code uses the coded `width`/`height` instead of calling `av_frame_apply_cropping` (or computing the cropped display rectangle) before hashing, two encodes that are visually identical but use different macroblock/superblock alignment padding (extremely common — e.g., an HEVC encoder using 64×64 CTUs padding 1080 up to 1088 vs a re-encode using 32×32 padding differently) will hash *different pixel data* (garbage padding rows) even though every displayed pixel is bit-identical.

**Why it happens:** Coded-vs-display dimension handling is one of the most commonly mis-implemented details in hand-rolled FFmpeg-based tools; the API only gained a first-class cropping field relatively recently, and a lot of example code and older tutorials still hash/compare raw coded buffers.

**How to avoid:** Always hash the *cropped display* rectangle (apply crop before computing `bytes_per_row`/height for the hash), and record both coded and display dimensions in evidence so a genuine coded-dimension change (which *is* diagnostically interesting, e.g. a padding/alignment regression) is still visible as its own finding rather than corrupting the content hash.

**Warning signs:** `content.video.frame_hash` fails on an encoder-parameter change (CTU size, GOP structure affecting padding) that produces visually identical output; divergent-frame ranges that span the *entire* file rather than a localized corruption region are a tell that the hash is picking up structural padding, not content.

**Phase to address:** 6 (video decode/hash path)

---

### Pitfall 5: `unspecified` colorimetry treated as a wildcard instead of its own value

**What goes wrong:** H.273/AVCOL fields (`color_primaries`, `color_trc`, `color_space`/matrix) commonly report enum value 2, `unspecified`. A naive comparator treats `unspecified == unspecified` as "no information on either side, nothing to compare" and skips — but `bt709 → unspecified` is a real, often serious regression (colorimetry metadata was dropped somewhere in the pipeline, and a player will now guess, frequently guessing wrong for SD vs HD content). Conversely, always gating any `unspecified` value as a hard fail produces relentless false positives on legitimate sources that never carried the tag to begin with (raw camera feeds, certain screen recordings, older encodes).

**Why it happens:** `unspecified` looks like "null"/"absent" semantically, but per spec it is a distinct, valid enum value, not the same information-theoretic state as the field being missing entirely (which is a different, container-level condition — presence vs value).

**How to avoid:** Treat `unspecified` as a first-class value under `exact` semantics (baseline `bt709` vs candidate `unspecified` is a real diff, reported as such) but let profiles tune severity — e.g. `sw-encoder`/`transform` might downgrade `X → unspecified` to `warn` rather than `fail` since some legitimate transforms strip tags, while `strict-bitexact`/`remux` should `fail` it. Never silently collapse it into a skip.

**Phase to address:** 3 (video colorimetry checks)

---

### Pitfall 6: MDCV luminance unit convention differs between HEVC SEI and Matroska/WebM native metadata

**What goes wrong:** HEVC's `mastering_display_colour_volume` SEI encodes `max_display_mastering_luminance` in units of 0.0001 cd/m² and primaries/white-point chromaticity in units of 0.00002 (both fixed-point integers per the spec), while Matroska's native `MasteringMetadata` elements (`LuminanceMax`/`LuminanceMin`, primaries) store plain floating-point cd/m² and chromaticity values directly — no fixed-point unit scaling at all. A remux from MP4 (HEVC SEI-carried HDR10 static metadata) to Matroska, or the reverse, requires FFmpeg's muxer/demuxer glue to convert unit conventions correctly; historical bugs in this exact conversion path are a known class of FFmpeg issue. If mediadiff extracts these values through two different code paths per container (or if FFmpeg's own conversion has an off-by-a-constant-factor bug in a given version), comparing MDCV luminance across an MP4↔MKV remux pair will show a value "off by 10000×" or similar — a spurious, glaring false positive on a check meant to catch real HDR tag loss (UC1-adjacent).

**Why it happens:** Two mature-but-independent metadata models (ISO SEI fixed-point vs Matroska float) evolved separately; the semantic field is the same, the wire encoding is not, and the conversion logic lives in muxer/demuxer code that's easy to get subtly wrong (unit factor confusion between 0.0001 and 0.00001, or between the primaries' 0.00002 and luminance's 0.0001 unit).

**How to avoid:** Normalize MDCV values to a single canonical unit (cd/m² as a rational) at extraction time regardless of source container, and add a cross-check fixture that remuxes an HDR10 file MP4→MKV→MP4 and asserts the normalized values are unchanged — this both validates mediadiff's own extraction and would have caught the underlying FFmpeg bug class historically seen in this conversion path.

**Warning signs:** MDCV/MaxCLL findings that are off by a suspiciously round factor (10×, 10000×, 65535/1) across a remux pair; values that are technically nonsensical (e.g., `max_luminance: 0.1 cd/m²` for what's supposed to be a 1000-nit master) sailing through as a "different but valid" value instead of being flagged as implausible.

**Phase to address:** 3 (HDR static-metadata extraction/normalization)

---

### Pitfall 7: Encoder-delay/priming signaling disagreement across MP4 elst / Matroska CodecDelay / iTunSMPB is a false-diff magnet for `timeline.av_offset`

**What goes wrong:** The same encoder-delay concept ("N silent priming samples the decoder must discard") is signaled three structurally different ways depending on container: MP4 edit lists (`elst` with a nonzero/negative `media_time`), Matroska `CodecDelay`/`SeekPreRoll` elements, and the informal `iTunSMPB` comment atom used by iTunes-encoded AAC (which is *not* read by most non-Apple tooling, including much of the open-source stack, unless specifically implemented). A remux or re-mux-through-a-different-muxer that preserves audio bit-for-bit but expresses priming through a different one of these three mechanisms — or drops the mechanism entirely because the target muxer doesn't support it — changes the *computed* presentation start of the audio stream even though no audio sample actually moved. Since `timeline.av_offset`/`av_drift` consume "audio priming-adjusted" timestamps (doc 04 §1.3, §4), any bug or container-specific gap in priming extraction directly corrupts the flagship A/V-drift check with a false reading.

**Why it happens:** There is no single universal encoder-delay signaling mechanism across containers; each ecosystem invented its own, and open-source demux/mux code has historically had incomplete or version-inconsistent coverage of all three (elst negative-`media_time` handling in particular has had interop disagreements even between FFmpeg/AOSP and other major implementations, per public issue trackers).

**How to avoid:** Extract priming from all three mechanisms where applicable, record which one was used (and its raw value) in evidence per doc 04's mechanism-citation rule, and treat `priming: unknown` as a visible, distinct state (already specified in doc 04 §4) rather than defaulting to zero. Add fixtures that specifically remux an AAC file with known priming through MP4→MKV and MKV→MP4 and assert `av_offset` is unchanged within tolerance.

**Warning signs:** `timeline.av_offset` shifting by a value suspiciously close to a common priming constant (1024 or 2112 samples at the stream's sample rate, ≈21–48 ms) exactly at a container-format boundary in a comparison pair.

**Phase to address:** 5 (priming extraction), consumed by 4 (av_offset/av_drift)

---

### Pitfall 8: HE-AAC implicit SBR signaling makes stream-declared sample rate unreliable without decode

**What goes wrong:** Backward-compatible HE-AAC streams often omit explicit SBR signaling in `AudioSpecificConfig`, relying on the decoder to detect SBR *implicitly* during actual decode (a well-known AAC-in-the-wild ambiguity). A parameter check computed from container/`ParserScan`-level metadata alone (no decode) will report the AAC-LC core sample rate (e.g., 24 kHz), while a full-decode-based check will report the SBR-doubled rate (48 kHz) for the *same* stream — not because anything changed, but because the two passes have different visibility into the same ambiguous signal. If baseline and candidate happen to be evaluated by different passes (e.g., one profile runs decode, another doesn't; or a future engine change moves `audio.sample_rate` between `requires_pass` tiers), the check produces a fabricated diff.

**Why it happens:** Implicit SBR signaling exists specifically for decoder backward-compatibility; it was never designed to be introspectable without running the actual decode path, which conflicts with the project's layered no-decode-until-needed architecture.

**How to avoid:** Explicitly document `audio.sample_rate` (and any HE-AAC-adjacent check) as `requires_pass: decode` when the stream's `AudioSpecificConfig` doesn't explicitly signal extension (i.e., don't trust `ParserScan` alone for AAC sample rate — confirm via decode probe), and add a fixture using implicit-signaling HE-AAC specifically (not just explicit HE-AACv2) to prove the check is stable regardless of which pass ran.

**Warning signs:** `audio.sample_rate` findings that flip between exactly 1× and 2× of each other on AAC streams, correlated with encoder/muxer changes rather than actual sample-rate changes.

**Phase to address:** 5 (audio parameter checks)

---

### Pitfall 9: Channel layout `5.1` vs `5.1(side)` is a routine false-diff, not an edge case

**What goes wrong:** AC-3/DTS conventionally place 5.1 surrounds at the *side* position; AAC/ALAC/Opus/WMA conventionally place them at the *back*. FFmpeg's own channel-layout model distinguishes these as genuinely different layouts (`5.1` vs `5.1(side)`), and encoders/muxers routinely disagree on which one a "5.1 file" actually is — this is common enough that FFmpeg's AAC encoder outright refuses side-channel 5.1 input without an explicit layout override, per its own documented behavior. Any codec conversion between AC3/DTS-family and AAC-family audio (an extremely common, entirely benign real-world operation) will trip a naive `exact` channel-layout comparison even though the audible surround configuration is unchanged in intent.

**Why it happens:** The two conventions are historical artifacts of different codec families' bitstream design, not a real perceptual difference most of the time — but they are, bit-for-bit, different enum values in every layout API.

**How to avoid:** Normalize channel layout comparison to treat back/side positional variants of the same channel *count and role set* as equivalent under default profiles (an explicit normalization rule, analogous to doc 03's pix_fmt range-folding), while still surfacing the raw positional difference in evidence for profiles that care (e.g., a broadcast-strict profile might legitimately want to gate this).

**Warning signs:** `audio.channel_layout` failing on essentially every AC3/DTS→AAC transcode fixture; a "regression" that reproduces on literally every file in a corpus that changes audio codec family, which is a strong tell it's a systematic normalization gap, not real per-file signal.

**Phase to address:** 5 (audio parameter checks)

---

### Pitfall 10: True-peak conflated with sample-peak if libebur128's true-peak path isn't actually enabled/verified

**What goes wrong:** BS.1770 true-peak requires 4× oversampling reconstruction to catch inter-sample overs; measuring peak directly from PCM samples ("sample-peak") is a different, systematically lower-reading metric. The project correctly chose libebur128 over hand-rolling (Key Decision, and a well-known trap per BS.1770 implementers), but libebur128's true-peak mode has historically been an optional/separate code path (`ebur128_true_peak` functions, requires the library built with its interpolator) rather than automatic — if the vcpkg feature/build configuration doesn't actually enable it, or the code accidentally calls the sample-peak API instead of the true-peak API, `audio.true_peak` silently becomes `audio.sample_peak` under a misleading name. This wouldn't even show up as a "false positive" in the traditional sense — it's worse: a systematically wrong, quietly-always-passing measurement that misses real inter-sample-clipping regressions (a false *negative*, arguably more dangerous for a tool whose entire premise is "catch what changed").

**Why it happens:** Peak and true-peak share a name-adjacent API surface; it's an easy copy-paste/config mistake, and unlike most of this document's pitfalls it fails silently rather than loudly (no crash, no obviously wrong number — just a slightly-too-low peak reading that looks plausible).

**How to avoid:** Add a fixture with a known engineered inter-sample overshoot (a full-scale sine near Nyquist reconstructs above 0 dBFS between samples) and assert `audio.true_peak` reports > 0 dBTP on it while sample-peak would report ≤ 0 dBFS — this is the only reliable way to prove the true-peak code path is actually exercised, not just present in the dependency graph.

**Phase to address:** 5 (audio loudness/true-peak checks)

---

### Pitfall 11: Continuity-counter and packet-size assumptions in a hand-rolled `ts_scan` are a classic false-positive source

**What goes wrong:** Per ISO/IEC 13818-1 §2.4.3.3, the continuity counter must **not** be treated as erroneous for: (a) packets with no payload (adaptation-field-only), (b) packets under a set `discontinuity_indicator`, or (c) an exact duplicate of the immediately preceding packet on the same PID (which should be silently dropped, not flagged). A hand-rolled scanner — which the project has explicitly chosen over TSDuck — that naively increments/compares CC on every packet regardless of these three carve-outs will manufacture continuity-error findings on every null-padded or duplicate-packet-legitimate stream, which is common in broadcast-origin and remuxed TS captures. Separately, M2TS (Blu-ray, 192-byte packets with a 4-byte timecode prefix) and DVB-ASI-with-FEC (204-byte, 16-byte Reed-Solomon trailer) streams will be silently misparsed by a scanner hardcoded to 188-byte sync-and-stride, producing garbage CC/PCR data rather than a clean "unsupported variant" skip.

**Why it happens:** The 188-byte TS packet is the overwhelmingly common case, so it's the one implementers test against first; the CC exception rules are a small, easy-to-miss paragraph in a large spec that most tutorials never mention.

**How to avoid:** Implement all three CC carve-outs explicitly with a unit test per rule (this is squarely testable in isolation, no media file needed); detect packet size (188/192/204) from sync-byte stride at the start of the scan rather than assuming 188, and treat 204-byte FEC trailers as data to skip, not parse.

**Warning signs:** Continuity-error counts on TS fixtures that are known-clean per a reference tool (TSDuck, kept as the project's own test-reference implementation per its Key Decisions — exactly the check to run here); any M2TS/Blu-ray fixture producing wall-to-wall garbage instead of a clean parse or explicit unsupported-variant skip.

**Phase to address:** 2 (`ts_scan` raw scanner)

---

### Pitfall 12: TS mux-rate-estimated duration is an estimate, not ground truth — don't gate it as tightly as computed duration

**What goes wrong:** For MPEG-TS, "container-declared duration" in doc 04's duration triple is frequently derived (by libavformat) from an estimated mux rate rather than an authoritative header field (TS has no `moov`-style duration atom) — the project already correctly scopes PCR-accuracy-vs-ideal-clock out of v1, but the *duration triple check itself* (`timeline.duration`, `±tol` ±1 frame fail) still compares this estimate against a computed value pairwise across files. A TS remux that changes multiplexing pattern (bitrate padding, null-packet insertion rate) without changing any actual media content can shift the *estimated* container-declared duration by more than 1 frame while the computed duration (from actual presentation timestamps) stays identical — producing a fail on the wrong side of the triple for a file that didn't regress.

**Why it happens:** The duration triple's uniform ±1-frame tolerance implicitly assumes all three duration sources are equally authoritative; for TS specifically, one of them (container-declared) is a statistical estimate by construction, not a measurement.

**How to avoid:** Give TS's container-declared duration a wider, explicitly documented tolerance (or downgrade internal-triple-disagreement severity for TS specifically) rather than reusing the same ±1-frame default used for MP4/MKV where container-declared duration is an authoritative header value; document the asymmetry in the check's `--explain` text so a user isn't confused why TS behaves differently.

**Warning signs:** `timeline.duration` internal-incoherence `info` notes (or outright fails) appearing on essentially every TS fixture but rarely on MP4/MKV fixtures — a strong signal the default tolerance is miscalibrated for the container, not that TS files are unusually broken.

**Phase to address:** 4 (duration-triple check, per-container tolerance)

---

### Pitfall 13: `strict/remux` profile's ±1-tick `timeline.start` tolerance can itself become a false-positive source on legitimate remuxes

**What goes wrong:** Doc 04 sets `timeline.start` to ±1 tick fail specifically under `strict/remux` — the profile whose entire purpose is "assert nothing changed." But a legitimate, payload-untouched remux that changes the *container's stored timescale* (e.g., re-muxing MP4 with a different `movie timescale` in `mvhd`, common when switching muxer tools) can shift the *rescaled* first-PTS by a fraction of a tick purely from rounding in `av_rescale_q`, even though the underlying media timestamp is unchanged. A profile whose stated purpose is catching payload mutation would then fail-gate a purely cosmetic timescale change — precisely the kind of "diff tool cries wolf on the strictest, most-trusted profile" failure mode that erodes confidence fastest, since `strict/remux` users are by definition the least tolerant of any false alarm.

**Why it happens:** Rational-everywhere math (a good design choice, doc's own constraint) still has to cross a rescale boundary somewhere when tick rates differ between baseline and candidate, and ±1-tick tolerance was presumably chosen based on same-timescale intuition, not cross-timescale-remux rounding.

**How to avoid:** Add a fixture explicitly covering a timescale-changing, payload-preserving remux under the `remux` profile before shipping this tolerance as default; if the fixture fails, either widen the tolerance for cross-timescale comparisons specifically or perform the rescale at maximum-precision (LCM timescale) rather than rounding to either side's native tick rate.

**Warning signs:** `remux` profile failing on a fixture pair generated by two different (but both lossless, payload-preserving) muxer tools — this is the profile's core promise being broken by its own tolerance choice, not by a real regression.

**Phase to address:** 4 (timeline.start tolerance, remux-profile fixture coverage)

---

### Pitfall 14: HDR static metadata visible at stream-level vs frame-level depending on which pass runs — a pass-order-dependent flakiness risk unique to this project's layered architecture

**What goes wrong:** HDR10 static metadata (MDCV/MaxCLL/MaxFALL) can be muxed as stream-level side data (set once, applies to the whole stream) or repeated as per-frame side data, depending on the encoder/muxer. `ParserScan` (phase 3, no full decode) can plausibly only see stream-level side data if it's attached there, while a full `DecodeSession` (phase 6) sees frame-level repeats if the encoder chose that path instead. Because doc 01's architecture explicitly declares "no analyzer ever re-reads the file" and each analyzer declares which passes it needs, if the HDR-presence check is wired to whichever pass happens to run for a given profile (some profiles skip decode entirely), the *same* check can report different presence/values depending purely on which profile invoked it — not on any actual content difference.

**Why it happens:** This is a genuine architectural interaction, not a decoder bug: the project's own strength (bounded, declarative per-pass analysis) creates this specific risk because HDR static metadata's storage location is itself encoder-dependent and ambiguous relative to the pass model.

**How to avoid:** Pick one authoritative source for HDR static-metadata truth (recommend: `ParserScan`/stream-level, since it's available in every profile and full decode isn't) and explicitly document that frame-level repetition is not independently diffed as a presence signal — only as a `content.*`-scoped consistency check if/when decode does run, kept separate from the phase-3 `video.*` presence check so results never silently change based on profile.

**Warning signs:** `video.hdr.mastering_display` presence/value flipping between two runs of the same file under different profiles (e.g. `sw-encoder` vs `hw-encoder` where one triggers decode and the other doesn't) — the strongest possible tell of a pass-order dependency bug.

**Phase to address:** 3 (HDR presence check pass assignment), cross-checked in 6

---

### Pitfall 15: Report-size cap must be a byte cap under real margin, not a "60 KB" hand-wave against GitHub's 65,536-character API limit

**What goes wrong:** GitHub's PR/issue comment API hard-limits body length to 65,536 characters (confirmed: multiple tool issue trackers report the literal API error "Body is too long (maximum is 65536 characters)" when this is exceeded, including from mature GitHub Actions used by large projects). Doc 01 §9 sets Markdown report cap at "60 KB" — reasonable margin numerically, but "KB" is ambiguous (1000 vs 1024) and, more importantly, the constraint is a **character** count in GitHub's API, not a byte count; heavy use of non-ASCII in messages (e.g., rendered Unicode symbols, non-ASCII filenames surfaced in evidence) inflates UTF-8 byte count relative to character count, so a byte-based 60 KB cap is actually safer than it looks, but only if implemented as bytes, not naively as "character count of a UTF-8-encoded buffer" which could differ if any multi-byte sequences are involved. Get this backwards and a corpus with heavily-populated evidence (many findings, long hint text) can silently exceed the real GitHub limit despite believing itself under the tool's own cap, causing the CI *comment-posting step itself* to fail — which, per Pitfall on exit-code conflation, must not be confused with an actual regression.

**Why it happens:** "60 KB" reads as safely under 65,536 at a glance, but the units and counting method (bytes vs Unicode characters vs UTF-16 code units, which some tooling layers use) are exactly the kind of detail that's correct by luck rather than by design unless explicitly tested.

**How to avoid:** Implement the cap as a byte-counted budget with an explicit safety margin (e.g., 60,000 bytes, matching the pattern used by mature PR-comment actions that reserve ~4 KB of headroom below 65,536), and add a golden test with a synthetically oversized finding set that asserts the renderer truncates *before* reaching the real GitHub limit, not just before its own nominal cap.

**Warning signs:** A markdown report that passes mediadiff's internal size check but is rejected by the CI platform's comment-posting step with a "body too long" API error — a symptom that looks like a CI integration bug but is actually a units mismatch in the cap.

**Phase to address:** 1 (Markdown report renderer)

---

### Pitfall 16: "Crying wolf" is not hypothetical — it is the documented, measured failure mode of every CI quality gate that doesn't earn trust by default

**What goes wrong:** Documented industry pattern (not unique to media tooling): quality gates that produce too many low-value or incorrect findings train developers to reflexively dismiss/merge-anyway, and by the time a real issue appears nobody is looking — reported concretely in linting/AI-code-review contexts as 70–90% of findings being ignored as false positives, with PR merge times measurably degrading as trust erodes. mediadiff's own PROJECT.md already identifies this as the central risk ("false positives are P0 bugs... a muted gate is worth nothing") — the pitfall here is specifically about the mechanisms by which trust is lost *incrementally*, which are easy to miss because no single false positive looks catastrophic in isolation.

**Why it happens:** Each individual false positive is locally justifiable ("well, technically the hash did differ") even when it's globally wrong (the difference was allocator padding, a toolchain version, a legitimate transform); trust erosion is a cumulative, not a single-event, phenomenon, so it's easy for a team to ship several individually-defensible-but-collectively-corrosive false positives before noticing the gate has been muted.

**How to avoid:** Every pitfall in this document is, functionally, a specific instance of this one. Structurally: (1) default to the least-alarm profile (`sw-encoder`, already chosen); (2) make `skipped` visible and distinct from `pass` everywhere (already chosen); (3) require every `fail` to carry an accept/tune/silence hint (already required); (4) treat the idempotence guarantee (identical re-run under the right profile is clean) as a release-blocking CI gate, not an aspiration (already planned) — and add, on top of what's already planned, a **cross-version idempotence check**: fingerprint a stable reference corpus with the *previous* mediadiff release and assert the *current* release's compare against those old snapshots is still clean. This is the single test most directly aimed at Pitfalls 1–3 above and is not currently listed in any phase's acceptance criteria.

**Warning signs:** Any internal bug report of the form "this check is technically correct but nobody wants it gating" — treat that sentence, whenever it's said internally during development, as a signal to downgrade default severity or add a normalization rule, not to explain away.

**Phase to address:** 1 (cross-cutting guarantee: add cross-release idempotence to the CI release-blocker suite), reinforced in every phase's fixture acceptance criteria

---

### Pitfall 17: GPL-component linkage can silently reappear through a vcpkg feature default change, invisibly to the build

**What goes wrong:** The project's LGPL decode-only distribution requirement (explicit constraint) depends on FFmpeg being configured without `--enable-gpl` and without any GPL-only filter/component reachable at link time. vcpkg's FFmpeg port feature defaults can change across port version bumps (a routine, low-visibility event in `vcpkg.json`/`builtin-baseline` updates) — if a future port revision changes a default feature flag such that a GPL-only component becomes reachable, nothing in a normal build will fail; it will simply link successfully and change the license story of the shipped binary without anyone noticing until an audit or a user's legal team catches it.

**Why it happens:** License compliance for a transitively-vendored native dependency is not something compilers or linkers check; it is purely a configuration-string property (`--enable-gpl` and its downstream feature gates) that has to be actively verified, and dependency bumps are exactly the kind of low-drama PR that doesn't get that level of scrutiny by default.

**How to avoid:** Add an automated CI check (not a manual review step) that inspects the built binary's linked FFmpeg configuration string (`avutil_configuration()` / `ffmpeg -buildconf` equivalent baked into the binary) and fails the build if `--enable-gpl` or any denylisted GPL-only component name appears — run this on every CI build, not just at release time, so a vcpkg bump PR fails fast rather than shipping.

**Warning signs:** A vcpkg baseline/port-version bump PR that touches FFmpeg feature flags without a corresponding change to the license-audit allowlist; binary size jumps unexpectedly after a dependency bump (a rough proxy for "new component got linked").

**Phase to address:** 0 (vcpkg manifest + license-audit CI step scaffolded from the start, not bolted on later)

---

### Pitfall 18: Windows non-ASCII path/argv handling breaks on real-world corpora even when the "happy path" CI fixtures are all ASCII

**What goes wrong:** Windows `main()`/`argv` is not UTF-8 by default; without `wmain`+`CommandLineToArgvW` conversion or an app-manifest `ActiveCodePage=UTF-8` opt-in (available since Windows 10 1903), any file path containing non-ASCII characters (extremely common in real media corpora — Cyrillic/CJK production names, accented characters, emoji in modern camera-app filenames) will fail to open on Windows even though the identical fixture works fine on Linux/macOS. Because the project's own fixture corpus is entirely synthetic and generator-controlled (explicit Out of Scope note: no binary media in git, everything from `scripts/gen_corpus`), it's easy for the whole CI fixture set to be ASCII-only by construction, meaning this class of bug has zero natural test coverage and will only surface when a real user runs mediadiff on their real (non-ASCII) corpus.

**Why it happens:** The gap between "CI is green on 3 OSes" and "works on real-world files" is exactly where a synthesized-fixtures-only test strategy is weakest — synthetic generators default to convenient ASCII names.

**How to avoid:** Explicitly add non-ASCII filenames/paths to `gen_corpus`'s naming convention (even for otherwise-trivial fixtures) so every OS's CI matrix exercises this path on every run, not as a one-off manual test; implement Windows entry via `wmain` or the UTF-8 manifest opt-in from day one (phase 0), not retrofitted later once path-handling assumptions are baked into `cli/`.

**Warning signs:** Any Windows-only bug report involving "file not found" for a file that demonstrably exists — check the filename for non-ASCII characters first.

**Phase to address:** 0 (CLI entry point / Windows console + path handling), enforced via 0's fixture-naming convention for all subsequent phases

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|-----------------|------------------|
| Skip class-2 path-signature detail (device/driver only, no toolchain hash) | Faster phase-1 delivery of the class system | Silent false positives across mediadiff's own dependency bumps (Pitfall 1) — exactly the P0 category | Never past phase 1; must be fixed before any class-2 check ships in phase 5/6 |
| Wire `hash` precondition machinery only, defer `±tol` path-signature gating | Smaller phase-1 surface | `content.video.perceptual`/`quality.*` inherit false-positive risk with no mechanical guard (Pitfall 2) | Acceptable to sequence *after* hash, never to skip entirely — must land before phase 6 ships |
| Reuse the same duration tolerance across all containers | One less config knob | TS's estimate-based duration false-alarms (Pitfall 12) | Never — container-specific tolerance is cheap to add once the mechanism exists |
| Treat non-ASCII path handling as a "later" Windows polish item | Ships CLI skeleton faster | Retrofitting `wmain`/manifest changes after `cli/` argument-parsing assumptions are baked in is expensive | Never — cheap to do correctly in phase 0, expensive to redo later |
| Skip cross-mediadiff-release idempotence testing (only same-build compare-twice) | Simpler phase-1 test suite | Misses exactly the toolchain-drift false positives this project is built to prevent (Pitfall 16) | Acceptable only until the first post-v1 dependency bump; must exist before that bump ships |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|-----------------|-------------------|
| vcpkg FFmpeg build | Single cache key covering both app code and FFmpeg build → any trivial commit invalidates the 15–40 min FFmpeg build cache | Separate cache keys: one for the vcpkg-built FFmpeg (keyed on `vcpkg.json`/baseline only), one for mediadiff's own code |
| MSVC static/dynamic CRT (`x64-windows-static-md`) | Mixing `/MT` and `/MD` dependencies causes ABI crashes at link or runtime, often only on Windows CI, rarely locally | Pin the triplet consistently across every vcpkg dependency; verify with a CI smoke test that links and runs the binary, not just that it compiles |
| libvmaf via vcpkg/system build | Assuming the model string `vmaf_v0.6.1` alone guarantees comparability across libvmaf versions | Record and pin libvmaf's own version/build alongside the model name in the fingerprint |
| libebur128 true-peak | Silently falling back to sample-peak if the true-peak/oversampling path isn't correctly wired (Pitfall 10) | Fixture-test the true-peak path with an engineered inter-sample overshoot |
| TSDuck as "reference implementation, not a dependency" | Drifting from cross-checking against it once the hand-rolled `ts_scan` "seems to work" | Keep TSDuck cross-check tests in CI permanently, not just during initial `ts_scan` development |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|-----------------|
| Per-frame hash arrays stored uncompressed | Fingerprint size balloons on long/high-fps content | Doc 06 already budgets 16 B/frame (~240 KB for a 10-min 25fps file) — verify this scales linearly and doesn't regress with 4K/60fps corpora before shipping `dir` mode at scale | Multi-hour or high-fps (60/120) reference files in a large `dir` corpus |
| `dir` mode decode-on-demand (`--content`) applied indiscriminately to large corpora | Corpus-speed CI runs balloon from "packet scan only" to full decode time across every file | Keep the documented default (header+packet scan only; decode opt-in) strictly enforced, and surface a corpus-size warning if `--content` is combined with a large file count | Corpora in the hundreds-of-files range (project's own UC2: 240-file corpus) |
| Windowed bitrate checks (`size.peak_bitrate`, 1s/100ms step) on very long files | O(n) sliding-window computation cost scales with file duration × step granularity | Confirm the sliding-window implementation is O(n) not O(n·window/step) via a streaming accumulator, not a naive re-sum per step | Multi-hour broadcast-length TS captures |

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| GPL component silently re-linked via vcpkg feature-default drift (Pitfall 17) | Distributed binary's license terms change without anyone noticing; potential legal exposure for downstream users of the static binary | Automated CI license-audit step reading the linked FFmpeg configuration string on every build |
| Malformed/adversarial media files crashing the decode path in CI (untrusted corpora fed to a CI gate) | A crafted file could DoS a CI runner or, in the worst case, exploit a decoder memory-safety bug — FFmpeg decoders are a large, historically CVE-rich attack surface | Sandbox/resource-limit the decode process invoked by `dir`/`compare` when run against untrusted input; treat decode errors as `partial:true` + exit 66 (already the design) rather than crashing the whole process |

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-------------------|
| Exit code conflating "found a regression" with "tool couldn't run" | CI treats a crash/misconfiguration as a merge-blocking regression (or worse, silently treats it as green) — erodes trust fast | Already addressed by design (`<3` vs `≥64` split) — verify with integration tests per finding category, not just the happy path |
| Tolerance policy with 5 layers of precedence (builtin→profile→config→override→CLI) becoming undebuggable | Users can't figure out why a check has the severity/tolerance it does | `list-checks --effective` already planned — make sure it's exercised in real support-scenario tests during phase-1 acceptance, not just unit-tested in isolation |
| Check IDs renamed without alias | Every downstream team's `mediadiff.toml` silently stops matching, and severity/tolerance overrides silently fall back to defaults with no warning | Already forbidden by policy (alias + deprecation) — add a build-time test that asserts removing/renaming an ID without an alias fails the build, not just a documented convention |
| Markdown report silently exceeding the real GitHub API limit (Pitfall 15) | The comment-posting CI step fails with an opaque platform error unrelated to any real regression | Byte-budgeted cap with margin, tested against a golden oversized-report fixture |

## "Looks Done But Isn't" Checklist

- [ ] **Class-2 path signature:** Often "done" once hwaccel device/driver is recorded — verify it also captures libavcodec version + compiler ID + opt flags for *non*-hwaccel class-2 decoders (Pitfall 1)
- [ ] **`hash` precondition machinery:** Often assumed to "cover determinism" broadly — verify `±tol` perceptual/quality checks have an equivalent path-signature guard, not just `hash` (Pitfall 2)
- [ ] **Raw scanner CC handling:** Often tested only against clean 188-byte fixtures — verify the three ISO 13818-1 §2.4.3.3 carve-outs (no-payload, discontinuity-flagged, duplicate) are unit-tested in isolation, and 192/204-byte packet sizes are detected, not assumed (Pitfall 11)
- [ ] **HDR static metadata presence check:** Often "done" against whichever pass was implemented first — verify the same file under a decode-triggering profile and a non-decode profile report identical presence/values (Pitfall 14)
- [ ] **Non-ASCII path handling on Windows:** Often "done" because CI is green — verify with an explicit non-ASCII fixture filename, not just ASCII fixtures (Pitfall 18)
- [ ] **GPL license audit:** Often "done" once at initial FFmpeg configuration — verify it's a *standing* CI check that runs on every vcpkg bump, not a one-time setup step (Pitfall 17)
- [ ] **Report size cap:** Often "done" as a nominal KB number in a config constant — verify it's byte-budgeted against the real GitHub 65,536-character API limit with margin, via a golden oversized fixture (Pitfall 15)

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|----------------|------------------|
| Class-2 path signature under-specified (Pitfall 1) | MEDIUM | Extend the fingerprint schema with a toolchain hash field (schema-version bump, `compare` already handles schema-major-version mismatch by refusing per doc 01 §8); backfill is not required — old snapshots simply degrade to `skipped:hash_incomparable` once the new field is required, which is the *correct* fail-safe behavior the class system was designed to produce |
| Non-hash semantics ungated (Pitfall 2) | MEDIUM | Same schema-extension path as above; existing `±tol` findings don't need retroactive correction, just forward-looking signature recording |
| MDCV unit-convention bug caught late (Pitfall 6) | LOW | Isolated to one extraction function; add the normalization + remux-roundtrip fixture and fix in place — no architectural change needed |
| Check ID renamed without alias, already shipped (Pitfall in UX table) | HIGH | Requires a deprecation release: reintroduce the old ID as an alias, document the break, and add the missing build-time guard so it can't recur |
| Report size cap miscalibrated in production (Pitfall 15) | LOW | Pure renderer fix; no fingerprint/schema impact, ship as a patch release |

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|-------------------|----------------|
| 1. Class-2 path signature under-specified | 1 (schema), 5/6 (decoders) | Cross-mediadiff-release idempotence test: compare current build against a snapshot taken by the previous release |
| 2. Non-hash semantics ungated | 1 (schema), 6 (perceptual/quality) | Fixture: identical content, two different mediadiff builds (different linked FFmpeg) → perceptual score must be flagged path-mismatch, not fail |
| 3. GENPTS-style demux repair mutating the timeline | 2 | Fixture: file with genuinely missing PTS → fingerprint reports `absent`, not a synthesized value |
| 4. linesize vs cropped display dimensions | 6 | Fixture: two encodes, identical display pixels, different CTU/macroblock alignment padding → `content.video.frame_hash` clean |
| 5. `unspecified` colorimetry as wildcard | 3 | Fixture: `bt709 → unspecified` reports as a real diff, not a skip |
| 6. MDCV unit convention MP4 SEI vs MKV | 3 | Fixture: HDR10 file remuxed MP4→MKV→MP4, MDCV values unchanged within normalization tolerance |
| 7. Encoder-delay signaling disagreement across containers | 5 (extraction), 4 (consumption) | Fixture: AAC with known priming, remuxed MP4↔MKV, `timeline.av_offset` unchanged |
| 8. HE-AAC implicit SBR sample-rate ambiguity | 5 | Fixture: implicit-signaling HE-AAC stream, sample-rate check stable regardless of profile/pass |
| 9. Channel layout back vs side | 5 | Fixture: AC3→AAC transcode, `audio.channel_layout` clean under default profile |
| 10. True-peak silently degraded to sample-peak | 5 | Fixture: engineered inter-sample overshoot, asserts true-peak > sample-peak reading |
| 11. Continuity-counter / packet-size false errors | 2 | Unit tests per ISO 13818-1 §2.4.3.3 carve-out; M2TS/204-byte fixture parses cleanly or explicitly skips |
| 12. TS estimated duration over-tight tolerance | 4 | Fixture: TS remux with changed mux pattern, unchanged content → `timeline.duration` clean |
| 13. `remux` profile ±1-tick timescale-rounding false fail | 4 | Fixture: cross-timescale, payload-preserving remux under `remux` profile → clean |
| 14. HDR metadata pass-order dependency | 3 (assignment), 6 (cross-check) | Fixture: same file compared under a decode-triggering and a non-decode profile → identical HDR presence/values |
| 15. Report size cap vs real GitHub limit | 1 | Golden oversized-report fixture asserts truncation under the real 65,536-char boundary with margin |
| 16. Crying wolf / trust erosion | 1 (cross-cutting), every phase's fixtures | Cross-release idempotence added to the CI release-blocker suite alongside the existing same-build idempotence suite |
| 17. GPL relinkage via vcpkg drift | 0 | CI step asserting the linked FFmpeg configuration string excludes `--enable-gpl` and denylisted components, on every build |
| 18. Windows non-ASCII path handling | 0 | `gen_corpus` includes non-ASCII-named fixtures exercised on every OS in the CI matrix |

## Sources

- FFmpeg Trac #6088 — h264 decoder producing deprecated pixel format (`yuvj*` / `color_range` double-reporting) — https://trac.ffmpeg.org/ticket/6088
- FFmpeg-user mailing list — "deprecated pixel format used, make sure you did set range correctly" — https://ffmpeg.org/pipermail/ffmpeg-user/2023-June/056488.html
- FFmpeg `AVFormatContext`/`AVStream` Doxygen reference — `start_time`, `AV_NOPTS_VALUE` semantics — https://ffmpeg.org/doxygen/trunk/structAVStream.html
- ffmpeg-devel — genpts/AV_NOPTS_VALUE truncation handling discussion — https://ffmpeg-devel.ffmpeg.narkive.com/MMUo2e08/patch-ffmpeg-bug-catch-av-nopts-value-before-it-gets-rescaled
- Fora Soft — PTS/DTS and 33-bit wraparound explainer (26.5-hour period) — https://www.forasoft.com/learn/audio-for-video/articles-audio/pts-dts-elementary-stream-timestamps
- ExoPlayer Issue #10503 — `elst` box negative `media_time` handling divergence between implementations — https://github.com/google/ExoPlayer/issues/10503
- Doom9 Forum — MP4 delay: `start_pts` vs `edts/elst` media time — https://forum.doom9.org/showthread.php?t=173536
- ffmpeg-devel — headers for gapless playback (elst emission support) — https://www.mail-archive.com/ffmpeg-devel@ffmpeg.org/msg95677.html
- Apple Developer Documentation — Audio priming / encoder delay in AAC (QuickTime file format Appendix G) — https://developer.apple.com/documentation/quicktime-file-format/appendix_g_audio_priming_handling_encoder_delay_in_aac
- Apple TN2258 — AAC Audio: Encoder Delay and Synchronization — https://developer.apple.com/library/ios/technotes/tn2258/_index.html
- Bugzilla (Mozilla) #1321249 — Properly handle AAC/MP4 encoder delay — https://bugzilla.mozilla.org/show_bug.cgi?id=1321249
- Netflix/vmaf `faq.md` and `models.md` — model-version comparability, "same model/tool version/frames/resolution" rule — https://github.com/Netflix/vmaf/blob/master/resource/doc/faq.md , https://github.com/Netflix/vmaf/blob/master/resource/doc/models.md
- Netflix/vmaf Issue #778 — model loading/version confusion — https://github.com/Netflix/vmaf/issues/778
- GitHub Community Discussion — comment body 65,536-character API limit — https://github.com/orgs/community/discussions/27190
- mshick/add-pr-comment Issue #93 — "body is too long (maximum is 65536 characters)" — https://github.com/mshick/add-pr-comment/issues/93
- renovatebot/renovate Issue #14551 — same 65,536-char limit hit in practice — https://github.com/renovatebot/renovate/issues/14551
- tsduck/tsduck Issue #256 — continuity counter increment edge cases — https://github.com/tsduck/tsduck/issues/256
- ISO/IEC 13818-1 §2.4.3.3 (continuity-counter carve-outs: no-payload, discontinuity-flagged, duplicate packet) — summarized via community technical references, cross-checked against tsduck issue discussion
- jbkempf.com — "FFmpeg 9.0" release summary — swscale rewrite to exact 64-bit-rational math, ABI-major bump across all libraries, `SWS_BITEXACT`/SPIR-V `NoContraction` — https://jbkempf.com/blog/2026/ffmpeg-9.0/
- VideoHelp Forum — AAC 5.1/7.1 channel layout changes on encode (side vs back channels) — https://forum.videohelp.com/threads/393684-Converting-to-AAC-5-1-7-1-with-ffmpeg-changes-channel-layout/page2
- blog.travisflix.com — FFmpeg AAC "Using a PCE to encode channel layout 5.1(side)" error — https://blog.travisflix.com/ffmpeg-aac-0x806362f00-using-a-pce-to-encode-channel-layout-5-1side/
- Anchore blog — false positives/negatives in scanning tools, trust erosion pattern — https://anchore.com/blog/false-positives-and-false-negatives-in-vulnerability-scanning/
- Qlty Software blog — developer-experience gaps of CI linting, "cried wolf" dynamic — https://qlty.sh/blog/developer-experience-gaps-of-linting-on-ci
- cubic.dev blog — "The false positive problem: why most AI code reviewers fail" — 70–90% false-positive dismissal rate, merge-time degradation — https://www.cubic.dev/blog/the-false-positive-problem-why-most-ai-code-reviewers-fail-and-how-cubic-solved-it
- NVIDIA/cuDNN documentation and general GPU-nondeterminism literature — bitwise reproducibility not guaranteed across driver/library versions, FMA ordering variance — cited generally, MEDIUM confidence (no NVDEC-specific trac ticket found in this pass; recommend the project's own CI cross-driver test as the authoritative source)
- Project's own design docs (`.planning/PROJECT.md`, `claude_docs/01,04,06`) — cross-referenced for gap analysis against the class-system claims

---
*Pitfalls research for: media-aware regression diffing / CI merge gate (mediadiff)*
*Researched: 2026-08-12*
