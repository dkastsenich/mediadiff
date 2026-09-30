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

**Every decoded frame is hashed** (unless `--sample N` thins the hashing, see below). A frame an MP4 edit list marks decode-but-do-not-show is still
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

**`--sample N` hashes and stores every Nth frame.** Available on `compare`, `snapshot`, `dir` and
`inspect`. Every frame is still decoded, because inter-coded video cannot skip frames, and everything
else that looks at the decoded frames (error and corrupt-frame counts, geometry changes, and the
frozen, black, caption and HDR detection of later checks) still sees every one of them. Only the
hashing and the stored per-frame arrays are thinned: a frame is hashed and stored when its decode
index is a multiple of N, so stored frame `k` is decode frame `k x N`, and the chain digest covers only
the stored frames. The option therefore makes a snapshot roughly N times smaller and quality scoring
cheaper, but it is not a faster decode. `--sample 1` is the same as no `--sample`: the fingerprint is
byte-identical.

The fingerprint says it was sampled. This check's `sampling_state` evidence is `sampled:N` (instead of
`full`), and the snapshot's envelope records `"sampling": {"video_frame_stride": N}`; both are absent
for a full fingerprint, so no existing snapshot changes. `frame_interval` evidence is the stream's frame
interval times N, which is what keeps the divergence report's frame pairing correct, and a divergence
report on a sampled pair adds `sample_stride` so a frame number reads as a stored-frame index, not as
the Nth decoded frame.

**Only fingerprints taken with the same N compare.** Any other pairing with a sampled side -- sampled
against full, or two different strides -- reports `skipped:sampling_mismatch` and tells you to re-run
both sides with the same `--sample`; it is never `hash_incomparable` and never a pass. A truncated side
still reports `skipped:hash_incomparable`, because truncation outranks sampling. Audio is not sampled:
`content.audio.sample_hash` is identical with and without `--sample`.

`--sample` with a zero, negative or non-integer value, or together with `--no-content`, is a usage
error (exit 64) naming the flag.

**Truncated decodes.** When the decode stops before a stream's own end -- more than 64 consecutive
decode failures (`consecutive_decode_error_limit`), or the per-file record budget running out
(`frame_record_budget_exhausted`) -- the value reported is the chain of what actually decoded, with
`sampling_state` evidence `"truncated"` and a `decode_truncation_reason` key naming why. A truncated
side compared against anything reports `skipped:hash_incomparable`, never `pass` and never `fail`.

### Reading the divergence report

When the digests differ, the finding names where. Frames are **lined up by presentation time**, not by
decode index: each file's times are measured from its own first frame, and two frames pair when their
times differ by strictly less than half the frame interval of the finer file (exactly half does not
pair). The arithmetic is exact rational arithmetic, so a timebase rounding such as MP4's 1/12800 against
Matroska's 1 ms cannot misalign, and a 60 fps file against a 30 fps file pairs only the frames that
coincide.

- **A dropped frame reads as missing.** `frame 40 missing from candidate` (the `missing_from_candidate`
  ranges and their total) and every later frame still lines up, so only genuinely changed frames count as
  differing. Index alignment would instead report everything after the drop as different. A frame present
  only in the candidate is listed the same way under `extra_in_candidate`.
- **Differing frames are merged into ranges.** `divergent_ranges` holds each contiguous run as `first`,
  `last` (baseline frame indices), `differing` (how many frames in it differ) and `start_time` /
  `end_time` (exact seconds from the baseline's first frame, as a `{num, den}` pair). Two differing
  frames with exactly one matching frame between them form one range; two or more matching frames end
  it. `first_divergent_frame` gives the first differing frame's baseline and candidate index, its PTS in
  the baseline's own time base and its exact time. `differing_frame_count` and `divergent_range_count`
  are always exact totals.
- **The lists are bounded.** Each list keeps its first 64 entries; `locator_truncated` is `true` when a
  list was cut, and the totals still count everything.
- **Milliseconds appear only in the message**, rendered from the exact time. The evidence carries no
  inexact value, so the report is byte-identical run to run, and a snapshot baseline produces the same
  evidence as the live file it was taken from.
- **Raw streams pair by decode order.** When either file has no usable timestamps (a raw elementary
  stream) or no known frame interval (MPEG-TS reports none), frames pair by position instead;
  `pairing` is then `index` and `pairing_fallback` names the side and the reason, for example
  `baseline_interval_unknown`. No time is stated in that case.

The pass or fail verdict is unchanged by any of this; only the report says more.

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

`--sample N` trades locator resolution for snapshot size: a change confined to frames the stride skips
is not seen, so use a small N (or none) when a one-frame corruption must be caught. Compare only equal
N -- a snapshot taken with `--sample 4` is compared against media probed with `--sample 4`, never
against a full one.

### Silence

Use `--set content.video.frame_hash=ignore` (or the `transform` / `hw-encoder` profile, which already
do this by default) for any workflow where the picture is expected to change on every run. Leave it
enabled everywhere else -- a silenced check's difference is still computed and shown under `-v`.
