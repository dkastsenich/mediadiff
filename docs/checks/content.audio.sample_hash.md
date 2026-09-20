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
  codec (bit-exact by construction), `flac`, `alac`, and the fixed-point siblings `aac_fixed`,
  `ac3_fixed`, `mp3`, `mp2` -- selected BY NAME (`avcodec_find_decoder_by_name`), never by codec
  ID, because the fixed-point and float variants of MP3/MP2 share one codec ID and only the
  name distinguishes them. **The `mp3`/`mp2` promotion to class 1 is PROVISIONAL**: it is proven
  bit-exact on x86_64 only as of this writing; an arm64 CI leg either confirms cross-architecture
  identity or forces a documented demotion back to class 2 (06-RESEARCH.md A1).
- **Class 2 -- hashes, but only comparable within one machine class.** The native (non-fixed)
  decoders: `aac`, `ac3`, `eac3`, `opus`, `mp3float`, `mp2float`, and any fixed-point-sibling
  stream that fell back to its default decoder (e.g. a USAC stream, which `aac_fixed` cannot
  open). A class-2 record carries a `path_signature` (library versions, target triplet, CPU
  feature flags) that a same-machine comparison matches and a cross-machine comparison usually
  does not.
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
