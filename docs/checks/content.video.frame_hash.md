# content.video.frame_hash

## What it measures

Whether every video stream's decoded pictures are the same as the baseline's, via a chained XXH3-128
hash over one digest per decoded frame. A frame's digest covers exactly three things, and nothing else:

- **The cropped display pixels.** Per plane, exactly `bytes_per_row(width) x rows` of the decoder's
  cropped display rectangle -- never `linesize`, which includes encoder and allocator padding and
  differs between otherwise identical frames. An odd 54-pixel-wide `yuv420p` picture hashes exactly 54
  bytes per luma row and 27 per chroma row.
- **The pixel format's name, after the `yuvj` fold.** The five deprecated `yuvj*` spellings fold to
  their plain counterpart through the same seam `video.pix_fmt` and `video.color.range` use, so one
  intent is one finding.
- **The display dimensions**, as two explicit 4-byte little-endian integers.

The presentation timestamp is **not** hashed. It is stored beside each digest (`element_ticks`, in the
stream's time base `element_tb`) and used only to locate where two streams diverge. That is what makes
an MP4, its Matroska remux and its MPEG-TS remux of one payload hash equal even though the TS muxer
shifts every PTS by about 1.4 s, and it is why a retimed stream produces one finding (`timeline.*`),
not two.

**Every decoded frame is hashed.** A frame an MP4 edit list marks decode-but-do-not-show is still
hashed: libavcodec would otherwise destroy it before it reaches the hash, and MPEG-TS cannot express
the trim at all, so an untouched remux would diverge at frame 0. The trim itself is owned by
`timeline.start`, `timeline.duration` and `container.mp4.edit_list`. The decoder is also flushed at
end of stream so the last reordered frames are hashed, and a video stream marked as an attached
picture (cover art) is never decoded or reported.

The value is a `hash_chain`: an `algorithm` name, a single top-level `digest` (the XXH3-128 of the
ordered concatenation of every frame's own 32-character lowercase hex digest -- one stable value for
the whole stream, built the same way `content.audio.sample_hash` builds its chain), `element_count`,
`element_stride` of 1, the per-frame `block_digests` array, and the parallel `element_ticks` array
with its `element_tb`. A stream whose frames carry no usable timestamps (a raw elementary stream)
stores no ticks and the evidence says `timestamps: "unusable"`.

**Decoder settings** are pinned and recorded openly in the fingerprint's `decode_path` ledger as the
flags string `bitexact+unaligned;idct=simple;threads=1`: the bit-exact and unaligned decoder flags
(the latter keeps the left crop -- without it a 54-pixel display rectangle hashes as 60), the simple
IDCT that the x86 and aarch64 kernels agree on, and exactly one decoder thread, because corrupt streams
decode non-deterministically at more than one thread even at a fixed count.

**Determinism class.** Every software video decoder is **class 2** until a committed cross-architecture
proof promotes it: a class-2 record carries a `path_signature` (library versions, target triplet, CPU
feature flags) that a same-machine comparison matches and a cross-machine comparison usually does not,
in which case the comparison reports `skipped:hash_incomparable` rather than a fabricated pass or fail.
No decoder is class 1 today. Class 3 (never hashes) is reserved for a decoder that is
non-deterministic on one machine, single-threaded.

**Damaged input is class 2, even for a proven decoder.** A stream with any decode error (a negative
`avcodec_send_packet` or `avcodec_receive_frame` return) or any frame the decoder flags as corrupt
(`AV_FRAME_FLAG_CORRUPT`, or a non-zero `decode_error_flags`) records `class2 <signature>` in its
`decode_path_class` evidence and `class: 2` in its `decode_path` record, even if its decoder is
otherwise class 1. Measured cross-architecture output on damaged input differs -- the pixels a decoder
conceals a broken slice with depend on the CPU's kernels -- so a hash of a damaged stream is comparable
only on one machine class (07-RESEARCH.md Q2). `decode_error_count` and `corrupt_frame_count` ride in
the evidence, and `meta.decode_errors` reports their sum per video stream. A frame the decoder refused
is simply absent from the hashed set, so a candidate that loses a frame to damage also fails here.

**Bounds on hostile input.** A video stream whose *declared* dimensions exceed 8192 x 8192 pixels is
never opened: the check reports `skipped:requires_decode` with `fallback_reason:
"max_pixels_exceeded"` in its evidence, no decode-path record is written for it (no decode happened),
and no frame buffer is allocated. Frame records (one digest and one tick each) are charged against the
same per-file probe memory budget that packet records are; when the budget ends the hashing, the value
is the chain of what was hashed, `sampling_state` is `"truncated"` and `decode_truncation_reason` is
`frame_record_budget_exhausted` (see above). When the budget runs out in the middle of a stream, the
packet scan itself is cut right after it, and every decode-dependent check of that stream then reports
`skipped:partial_scan` instead -- a different, equally deterministic outcome. The decoder is still
drained at end of stream after the budget is exhausted, but nothing further is stored.

**A change of picture size inside one stream does not stop the hash.** Each frame's digest covers its own
dimensions and format, `geometry_change_count` counts the transitions (one resolution change is 1, however
many frames follow it), and `normalization` names the first frame's size.

Every hashed stream writes one record into the `decode_path` array: `stream_index`, `decoder`,
`class`, `flags`, and (class 2 only) `path_signature`. A precondition mismatch between baseline and
candidate (`decode_path_class`, `sampling_state` or `normalization` evidence disagreeing) reports
`skipped:hash_incomparable`; `normalization` is `cropped;fmt=<format>;dims=<width>x<height>` of the
first frame, so a pixel-format or size change between the two sides is reported by `video.pix_fmt` or
`video.resolution` and not a second time here.

`--no-content` (or `dir`'s own opt-in default) disables the decode pass entirely; this check then
reports `skipped:requires_decode` on every video scope, never silence and never a fabricated value. A
stream that decodes to zero frames reports `skipped:insufficient_data`, never an empty-string digest.
`--sample N` will hash and store every Nth frame only -- every frame is still decoded, because
inter-coded video cannot skip frames, so that option buys a smaller snapshot and not a faster decode;
the paragraph describing it is filled in by the plan that adds the option.

**Truncated decodes.** When the decode stops before a stream's own end -- more than 64 consecutive
decode failures (`consecutive_decode_error_limit`), or the per-file record budget running out
(`frame_record_budget_exhausted`) -- the value reported is the chain of what actually decoded, with
`sampling_state` evidence `"truncated"` and a `decode_truncation_reason` key naming why. A truncated
side compared against anything reports `skipped:hash_incomparable`, never `pass` and never `fail`.

## Why it matters

A commit can pass every unit test while the pictures an encoder produces silently change. Hashing the
decoded frames catches exactly that, and the basis above is chosen so that only a real change to the
picture ever reports a difference: an untouched remux (MP4 to MKV, MP4 to MPEG-TS) keeps every pixel,
so it must not fail merely because the target container shifts timestamps or cannot express an
edit-list trim, and encoder row padding must never decide the verdict. A check that cries wolf gets
muted, so the false positives are designed out rather than tuned away.

## Accept / Tune / Silence

### Accept

If the picture genuinely changed on purpose (a new encode, an intentional edit), re-run
`mediadiff snapshot` on the new candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `hash` semantics are exact-or-not by construction. Under the
`transform` profile (an intentional transcode) this check is silenced to `ignore`; under `hw-encoder`
(an intentional hardware re-encode) it is demoted to `info`, so the change is still visible under `-v`
without gating the run.

### Silence

Use `--set content.video.frame_hash=ignore` (or the `transform` / `hw-encoder` profile, which already
do this by default) for any workflow where the picture is expected to change on every run. Leave it
enabled everywhere else -- a silenced check's difference is still computed and shown under `-v`.
