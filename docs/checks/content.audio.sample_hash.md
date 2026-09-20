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

Decoder selection happens once per stream, before decoding starts, from a fixed-point sibling
table for codecs proven bit-exact across CPU architectures (`aac`->`aac_fixed`,
`ac3`->`ac3_fixed`) plus every PCM codec (bit-exact by construction, no algorithm to diverge).
Every other successfully-opened codec still produces a real, comparable hash on the SAME
machine and build -- decoder class only governs whether a hash is safe to compare ACROSS two
different machines or builds (see the `decode_path_class` evidence key below), never whether it
is computed at all.

A precondition mismatch between baseline and candidate (`decode_path_class`, `sampling_state`
or `normalization` evidence disagreeing) reports `skipped:hash_incomparable` with a remediation
hint -- never a fabricated pass or fail. When the chains genuinely differ, the finding's
evidence names the first divergent block as a sample range (and an approximate time when the
sample rate could be recovered from the `normalization` evidence), the full list of divergent
block ranges, and the total divergent-block count -- derived identically whether the baseline
was real media or a stored `*.snap.json`.

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

### Silence

Use `--set content.audio.sample_hash=ignore` (or the `transform`/`hw-encoder` profile, which
already do this by default) for any workflow where the audio essence is expected to change on
every run. Leave it enabled everywhere else -- a silenced check's difference is still computed
and shown under `-v`.
