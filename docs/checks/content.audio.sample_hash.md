# content.audio.sample_hash

## What it measures

Whether every audio stream's decoded sample data is byte-for-byte the same as the baseline's,
via a chained XXH3-128 hash over fixed-length blocks of the decoder's UNTRIMMED output --
every sample decoded from every packet the demuxer delivers, ignoring sample-level trim
signals (`skip_samples`, edit-list priming, `CodecDelay`). Decoded PCM is regrouped into
fixed-length blocks (roughly 100 ms, exactly `sample_rate / 10` samples per stream, integer
division) independent of the decoder's own frame size and independent of container
packetization, so an MP4, its MKV remux and its MPEG-TS remux of one payload hash equal, and
so does the same PCM essence packaged as WAV, MOV, or FLAC at any block size.

The value is a `hash_chain`: an `algorithm` name, a single top-level `digest` (the XXH3-128 of
the ordered concatenation of every block's own digest -- one stable value for the whole
stream), and the per-block `block_digests` array itself (one 32-character lowercase hex string
per fixed-length block, the final entry the trailing partial block when the stream's sample
count is not an exact multiple of the block length -- never zero-padded, never dropped).

Decoder selection happens once per stream, before decoding starts, and follows a three-class
determinism table (never influenced by `--profile` -- decoder choice is a property of the
fingerprint, not the policy under which it is later evaluated):

- **Class 1 -- hashes everywhere, compares across any two machines or builds.** Every PCM
  codec (bit-exact by construction), `flac`, `alac`, and the fixed-point siblings `aac_fixed`
  and `ac3_fixed` -- selected BY NAME (`avcodec_find_decoder_by_name`), never by codec ID.
  **06-13-PLAN.md's cross-architecture round trip (CI run 35735099865, commit e5a1677, a real
  arm64-osx leg):**
  - **`aac_fixed` is CONFIRMED class 1**, measured: the D-11 class-1 two-build proof
    (`tests/integration/test_audio_hash_decoder.cpp`'s "class proof Test 2") compares a snapshot
    taken from one build against a fresh measurement on the SAME hand-written, byte-identical-by-
    construction fixture (`audio_aac_handwritten.mp4`, D-10) -- it ran, and passed, on the real
    arm64-osx leg. That is real cross-architecture evidence, not an assumption.
  - **`mp3` and `mp2` are DEMOTED to class 2, not confirmed.** D-06's own text required bit-exact
    output across architectures (arm64 as well as x86 SIMD levels), not proof on one x86 host.
    No trustworthy cross-architecture two-build proof exists for either: `mp2`'s only real corpus
    fixture (`audio_mp2_base.mpg`) is ffmpeg-encoder output, and this pinned ffmpeg build's lossy
    encoders are already documented (`.planning/WINDOWS.md` #12) as NOT producing
    architecture-stable bytes even under `-flags +bitexact` -- a two-build proof built on it would
    need D-11's own fixture-identity assertion to run first, and that assertion would very likely
    fail on arm64 before ever reaching the decode comparison, testing fixture instability rather
    than decoder instability. `mp3` has no real corpus fixture at all in this LGPL decode-only
    pin. Building a trustworthy proof for either needs a D-10-style hand-written, architecture-
    stable elementary stream, which does not exist and was out of this plan's scope. Per this
    plan's own must-have ("either CONFIRMED... or DEMOTED... never closed on assumption"), the
    honest outcome, absent proof, is demotion: `determinism_class_for_decoder()`
    (`src/probe/audio_decode.h`/`.cpp`) now returns class 2 for both. "auto" still SELECTS the
    fixed-point `mp3`/`mp2` decoders by name (selection and classification are independent,
    D-06's own T-06-15 rule) -- they still hash, just compared only within one machine class via
    the class-2 `path_signature` path, exactly like any other class-2 decoder.
  - **`ac3_fixed`'s cross-architecture bit-exactness is ALSO unmeasured by any real
    fixture-level test** (no real AC-3 corpus fixture exists in this LGPL decode-only pin, same
    gap as `mp3`) -- but its class-1 status predates D-06's own reopening and is not the literal
    subject of this plan's confirm-or-demote must-have (scoped to the `mp3`/`mp2` promotion), so
    it is left unchanged here. This gap is recorded openly, not silently, at
    `.planning/WINDOWS.md` #39.
- **Class 2 -- hashes, but only comparable within one machine class.** The native (non-fixed)
  decoders: `aac`, `ac3`, `eac3`, `opus`, `mp3float`, `mp2float` -- and, since 06-13-PLAN.md Task
  2, the fixed-point `mp3`/`mp2` decoders themselves (demoted above) -- plus any fixed-point-
  sibling stream that fell back to its default decoder (e.g. a USAC stream, which `aac_fixed`
  cannot open). A class-2 record carries a `path_signature` (library versions, target triplet,
  CPU feature flags) that a same-machine comparison matches and a cross-machine comparison
  usually does not.
- **Class 3 -- never hashes.** A codec this table does not list. Hashing is DISABLED for that
  stream rather than producing a digest nobody has proven trustworthy: the measurement reports
  `skipped:hash_disabled`, carrying the decoder name and class but no digest.

`--hash-decoder <auto|default|NAME>` is the one input to selection. `auto` (the default) prefers
the class-1 sibling when one exists and can open the stream; `default` opts out unconditionally
and always records class 2 for a stream that would otherwise have promoted to class 1; a decoder
NAME forces that exact decoder (falling back to the codec's own default, recorded as class 2 with
a `fallback_reason`, if the named decoder cannot open the stream -- decoder choice is still
locked in once per stream, before the sweep, never re-selected mid-decode).

Every hashed stream (classes 1 and 2) writes one record into the fingerprint's `decode_path`
array: `stream_index`, `decoder`, `class`, `flags`, and (class 2 only) `path_signature`. A
precondition mismatch between baseline and candidate (`decode_path_class`, `sampling_state` or
`normalization` evidence disagreeing) reports `skipped:hash_incomparable` with a remediation
hint -- never a fabricated pass or fail, and this holds even when the two digests happen to be
numerically equal: a coincidental match computed under different preconditions is still not
proof of anything. When the chains genuinely differ, the finding's evidence names the first
divergent block as a sample range (and an approximate time when the sample rate could be
recovered from the `normalization` evidence), the full list of divergent block ranges, and the
total divergent-block count -- derived identically whether the baseline was real media or a
stored `*.snap.json`.

`--no-content` (or `dir`'s own opt-in default) disables the underlying decode pass entirely;
this check then reports `skipped:requires_decode` on every audio scope, never silence and never
a fabricated value. A stream that decodes to zero samples reports `skipped:insufficient_data`,
never an empty-string digest.

## Why it matters

Container remuxing (MP4 to MKV, MP4 to MPEG-TS), a lossless transcode (WAV to FLAC), or a
packetization change should never alter the audio a listener actually hears. Hashing the
decoder's *trimmed* output would make an untouched remux fail this check purely because the
target container cannot express the same trim mechanism as the source -- exactly the class of
false positive this project treats as P0. Hashing over decoder frames instead of fixed blocks
would make an ordinary repacketization (a different FLAC block size, a WAV-to-MOV remux) fail
even though every sample is identical. This check's basis is chosen so that only a REAL change
to the audio essence -- a different encode, a dropped or corrupted sample range, a channel
swap -- ever reports a difference.

## Accept / Tune / Silence

### Accept

If the audio essence genuinely changed on purpose (a new transcode, an intentional edit), re-run
`mediadiff snapshot` on the new candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `hash` semantics are exact-or-not by construction. Under the
`transform` profile (an intentional transcode), this check is silenced to `ignore`; under
`hw-encoder` (an intentional hardware re-encode), it is demoted to `info` so the change is
still visible under `-v` without gating the run.

`--hash-decoder default` (or an explicit decoder NAME) trades class-1 cross-machine
comparability for a specific decoder's output -- useful when investigating whether a class-2
skip is caused by the fixed-point/native decoder split itself. It never changes what counts as
a difference, only which decoder produced the samples being compared.

### Silence

Use `--set content.audio.sample_hash=ignore` (or the `transform`/`hw-encoder` profile, which
already do this by default) for any workflow where the audio essence is expected to change on
every run. Leave it enabled everywhere else -- a silenced check's difference is still computed
and shown under `-v`.
