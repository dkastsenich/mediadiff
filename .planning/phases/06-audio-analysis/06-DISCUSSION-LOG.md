# Phase 6: Audio Analysis - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-20
**Phase:** 6-Audio Analysis
**Areas discussed:** Sample-hash identity, Class-2 trust boundary, HE-AAC & stable fixtures, Priming loss & #32

---

## Sample-hash identity

### Which samples does `content.audio.sample_hash` hash?

| Option | Description | Selected |
|--------|-------------|----------|
| Decoder output, untrimmed | Every sample decoded from every packet the demuxer delivers, ignoring sample-level trim signals. MP4/MKV/TS copies of one AAC stream hash equal; trim changes owned by `audio.priming`, `timeline.start`, `container.mp4.edit_list` | ✓ |
| Trimmed playback output | Hash what a player outputs after priming/padding trims; an MP4→TS stream copy then fails `sample_hash` (1,368 extra samples) on an untouched payload | |

**User's choice:** Decoder output, untrimmed → D-01
**Notes:** Grounded in a measurement taken during the discussion: MP4 and its MKV remux decode identically, the TS remux decodes 1,368 more samples, and `-flags2 +skip_manual` makes all three identical.

### What unit does the hash chain step over?

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed sample-count blocks | Regroup decoded PCM into fixed-length blocks in a canonical interleaved byte view; repacketization cannot move the hash | ✓ |
| Per decoded frame | Doc 05 §4's literal per-frame hash; a PCM WAV→MOV remux or a FLAC block-size change fails with identical samples | |

**User's choice:** Fixed sample-count blocks → D-02
**Notes:** One PCM sine in WAV/MOV/MKV/FLAC (two block sizes) decoded to one identical md5 while packet sizes ranged from ~401 to 8192 bytes.

### How should `sample_hash` report where two tracks diverge?

| Option | Description | Selected |
|--------|-------------|----------|
| Block ranges, same everywhere | First divergent block as sample range + time, divergent ranges, total count; identical against media or a snapshot | ✓ |
| Exact sample with media | Lockstep-decode both media for an exact sample; snapshot baselines fall back to block ranges, so evidence differs by baseline type | |

**User's choice:** Block ranges, same everywhere → D-03
**Notes:** Keeps PROJECT.md's "snapshot equivalence holds by construction". SC4's "first divergent sample" is recorded as meaning the first sample of the first divergent block.

### What block size and snapshot encoding should per-block digests use?

| Option | Description | Selected |
|--------|-------------|----------|
| ~100 ms, one hex digest per line | ~6,000 lines (~240 KB) per 10-minute track — doc 06's video budget; readable `git diff` | ✓ |
| ~1 s, one hex digest per line | Ten times smaller, but a short glitch collapses into a one-second range | |
| ~100 ms, one compact binary blob | Doc 06's video approach (~130 KB), but a single line that rewrites wholesale | |

**User's choice:** ~100 ms, one hex digest per line → D-04
**Notes:** `HashChain` has no per-element digests today; Phase 7's video hashing inherits whatever shape this phase builds.

---

## Class-2 trust boundary

### What must two class-2 (float-decoded) hashes share before they're compared?

| Option | Description | Selected |
|--------|-------------|----------|
| Versions + triplet + CPU SIMD set | libav versions, build platform triplet, and the SIMD features libav actually dispatched | ✓ |
| Versions + build triplet | Catches arm64 vs x86_64 and MSVC vs GCC, but not AVX2 vs SSE-only on one triplet | |
| Force SIMD off when decoding | Portable within a triplet, but libav's CPU mask is process-global, risking Phase 7's PERF-02 | |

**User's choice:** Versions + triplet + CPU SIMD set → D-05
**Notes:** Measured during the discussion: with one binary, `aac`, `ac3`, `eac3`, `opus` and `mp3float` all changed output when only SIMD flags changed. Today's signature is library versions only, which are equal on all five CI legs.

### Should hashing auto-prefer the fixed-point `mp3`/`mp2` decoders?

| Option | Description | Selected |
|--------|-------------|----------|
| Extend only where proven | Add `mp3`/`mp2` once cross-architecture bit-exactness is demonstrated; unlisted codecs stay class 3 | ✓ |
| Keep doc 05's table as written | MP3 stays class 2 through `mp3float` | |

**User's choice:** Extend only where proven → D-06
**Notes:** `mp3`/`mp2` were SIMD-stable on x86 here; FFmpeg's default MP3 decoder is `mp3float`, so this is a real promotion.

### If the preferred fixed-point decoder can't open a stream?

| Option | Description | Selected |
|--------|-------------|----------|
| Default decoder, recorded as class 2 | Decoder chosen once before the sweep, fallback reason recorded; mid-stream failure is a decode error | ✓ |
| Float decode, hashing disabled | Loudness/silence still run; `sample_hash` skipped even on the same machine | |

**User's choice:** Default decoder, recorded as class 2 → D-07

### Three corrupt frames that libav recovers from: regression, or "could not run"?

| Option | Description | Selected |
|--------|-------------|----------|
| A gating finding | Recoverable errors counted per stream and compared as a registered check (exit 1); only a wholly undecodable stream marks partial + exit 66 | ✓ |
| Doc 01 §11 as written | Any decode error marks `partial:true` and exits 66 | |

**User's choice:** A gating finding → D-09
**Notes:** Under the doc's rule a baseline with one known-bad frame would exit 66 forever — a permanently muted gate. The amendment is recorded in the open, as Phase 5 D-14 did with PERF-03.

---

## HE-AAC & stable fixtures

### How should the HE-AAC fixtures be produced?

| Option | Description | Selected |
|--------|-------------|----------|
| Hand-written bitstream via a Python tool | Minimal AAC + SBR payload wrapped with explicit (AOT 5) and implicit (LC) ASCs; byte-identical on every leg, no DOC-03 exemption | ✓ |
| Mirror a hash-pinned sample as a release asset | Deterministic and not in git, but not synthesized and needs a licence-clean source | |
| Build on a dev machine with fdk-aac | Doc 05 §6's own fallback; CI can't reproduce it, so DOC-03 would need an exemption list | |

**User's choice:** Hand-written bitstream via a Python tool → D-10
**Notes:** No pin ships libfdk_aac — the Linux/macOS pins list `--enable-nonfree` but have no fdk encoder, and the Windows pin is `win64-lgpl`. Follows Phase 4 D-01/D-02/D-03.

### What input proves "the same file hashed via `aac_fixed` on two different builds compares equal"?

| Option | Description | Selected |
|--------|-------------|----------|
| Hand-written AAC from the same writer | Byte-identical on every leg, decoding to non-trivial samples, with an xxh3 identity guard so a divergent leg fails loudly | ✓ |
| Encode with `-cpuflags 0` | Removes SIMD variance but not compiler/architecture variance | |
| Prove it on the designated leg only | Never tests the across-architecture claim that matters | |

**User's choice:** Hand-written AAC from the same writer → D-11
**Notes:** Native `aac`/`ac3`/`eac3` encoder bytes changed under `-cpuflags 0`; `flac` did not.

### Where does `audio.profile`'s SBR signaling mode come from?

| Option | Description | Selected |
|--------|-------------|----------|
| Header pass: ASC vs post-probe `codecpar` | `find_stream_info` already decodes the first frames; value never depends on the pass set | ✓ |
| One-packet micro-decode in the header pass | Pass-independent by construction, at one bounded decode per audio stream in every mode | |
| From the audio decode pass | Doc 05 §2.1's literal recipe; snapshot says HE-AAC while a no-decode run says LC | |

**User's choice:** Header pass: ASC vs post-probe `codecpar` → D-12
**Notes:** Researcher verifies against the linked FFmpeg 8.1; the micro-decode option is the recorded fallback.

### How is the loudness reference established?

| Option | Description | Selected |
|--------|-------------|----------|
| Lossless fixtures, reference committed as text | FLAC/PCM fixtures are byte-stable, so one committed ebur128 reference is valid on all five legs | ✓ |
| Keep doc 05's AAC recipes, designated leg only | Assertion skips on four legs | |
| Compute the reference at test time | Needs the pinned ffmpeg at test time on every leg | |

**User's choice:** Lossless fixtures, reference committed as text → D-13

---

## Priming loss & #32

### MP4 (priming 1024) vs its TS stream copy (unknown): what does `audio.priming` report?

| Option | Description | Selected |
|--------|-------------|----------|
| Unknown is its own value: a regression | Mirrors doc 05's layout rule; per-side state/source/samples in evidence; the profile decides gating | ✓ |
| Skip when either side is unknown | No false positives, but the check goes dark on the MP4→TS packaging case | |
| Split value from state | Keeps both signals at the cost of a second permanent check id | |

**User's choice:** Unknown is its own value → D-14
**Notes:** Accepted cost — when a later build learns priming from a new source, an old snapshot's `unknown` becomes a finding on unchanged media; visible, one-time churn covered by SNAP-05's tool-version skew warning.

### When two priming sources disagree?

| Option | Description | Selected |
|--------|-------------|----------|
| First in precedence wins, disagreement in evidence | Deterministic resolver, conflicts visible in `inspect`; no new id | ✓ |
| A coherence check at `info` | The `video.hdr.coherence` shape; costs a forever id, an explain doc and a disagreeing fixture | |

**User's choice:** First in precedence wins → D-15

### Does Phase 6 close WINDOWS #32?

| Option | Description | Selected |
|--------|-------------|----------|
| Close it with a shared-basis span | Extend Phase 5 D-10 from offsets to `av_drift`'s span: raw spans when either side's priming/padding is unknown | ✓ |
| Leave it waived as a follow-up | Phase 6 touches no Phase 5 code; the false fail stays in the ledger | |

**User's choice:** Close it with a shared-basis span → D-16
**Notes:** A false positive on a lossless remux is the P0 class this project exists to prevent, and ROADMAP SC2 scopes the priming-gap closure to this phase. Cost: editing verified Phase 5 code and re-baselining those pairs' declared sets.

### Where does trailing padding live?

| Option | Description | Selected |
|--------|-------------|----------|
| Evidence on `audio.priming` | Matches doc 05, which defines no padding id; consumed by the shared-basis span | ✓ |
| Its own check id | Padding changes gate on their own, at the cost of a permanent id | |

**User's choice:** Evidence on `audio.priming` → D-17

---

## Claude's Discretion

- The audio check-ID roster, approved at a roster checkpoint in the first plan (phases 3–5 precedent), including the decode-error check id D-09 introduces.
- How the audio decode pass fuses with the existing sweep, under the unchanged one-read invariant.
- Wiring `--content`/`--no-content` across `compare`, `snapshot`, `dir` and `inspect`, and adding `--hash-decoder` to doc 00 §3.1's flag list.
- Where the per-hashed-stream record (TRUST-01) lives: envelope `decode_path` versus measurement evidence, without breaking `hash.cpp`'s precondition table.
- The exact integer-sample block length per rate, and digest width.
- Loudness/true-peak value representation (double versus quantized rational).
- PERF-04 measured under Phase 5 D-13/D-14 — wall-clock recorded, ratchet gates.
- Profile interactions per doc 05 §5.
- How existing fixtures' declared finding sets absorb the new audio checks (Phase 5 D-01/D-02 unchanged).

## Deferred Ideas

- Per-channel digests (which channel diverged, L/R swap detection).
- Exact-sample divergence via lockstep decode — possibly revisited with Phase 7's video lockstep.
- A priming source-coherence check id.
- A separate trailing-padding check id.
- EXT-05's remaining scope (priming beyond what the demuxer and raw scanners expose).
- Classifying codecs doc 05 §3 doesn't list (Vorbis, DTS, TrueHD/MLP, WavPack).
- HE-AAC v2 / Parametric Stereo signaling beyond what `audio.profile` needs.
- Float decode optimisation and `--hwaccel` for audio (v2).
- WINDOWS #29 (misleading `tol` delta text) — open, unrelated to audio.
