# video.closed_captions

## What it measures

Whether a video stream carries closed captions -- ATSC A/53 Part 4 caption data (the CEA-608 and
CEA-708 services broadcast and streaming workflows embed in the picture data). The value is the
string `a53_cc` when **any decoded frame** of the stream carries caption side data, and a real
`Absent{}` when the stream decoded completely and no frame did. Any frame counts, not only the first:
captions commonly start mid-stream, after an opening title or a silent intro, so a stream whose first
captioned frame is the fourth still reports `a53_cc`.

**Detection needs the decode pass.** The caption bytes travel *inside* the compressed pictures (MPEG-2
user data, H.264 and HEVC SEI messages), so only a decoder that has parsed a picture reports them. The
check is read from the same single decode sweep `content.video.frame_hash` uses -- no second pass over
the file. It is independent of `--sample N`: sampling thins only what is hashed and stored, and every
frame is still decoded and inspected.

**What is recorded.** Only presence, a count and a position: the evidence carries `cc_frame_count`
(how many decoded frames carried captions), `cc_first_frame` (the decode index of the first such
frame, counting from 0) and `source` (`frame_side_data`). The caption text itself never enters the
fingerprint, a snapshot or a report.

**Introduced and removed captions.** The check uses the `presence` semantic: it passes when both sides
have captions or neither does, and fails when one side has them and the other does not. Losing the
captions is a failure, and so is gaining them, because an encode that silently drops (or starts
injecting) the caption stream changes what a viewer with captions enabled sees. The *content* of the
captions is not compared.

**When it does not measure.**

- `skipped:requires_decode` under `--no-content` (and for `inspect`, whose default does not decode),
  or when a decoder for the stream could not be opened.
- `skipped:partial_scan` when the packet scan stopped early, when the stream could not be decoded at
  all, or when the decode stopped early (an error bound or the frame-record budget) **before any
  captioned frame was seen** -- a prefix with no captions proves nothing about the frames it never
  decoded, so it is not reported as absent. If a captioned frame *was* seen in the prefix, presence is
  proven and the value is reported with a `decode_truncation_reason` in the evidence.
- `skipped:insufficient_data` when the decoder produced no frame from an otherwise error-free stream.

Attached pictures (cover art) are not video streams for this purpose and produce no measurement.

A scan of the compressed bitstream for caption messages *without* decoding -- which would lift the
decode requirement -- is a possible later improvement, tracked but not part of this version.

## Why it matters

Captions are an accessibility feature and, in many markets, a legal requirement, and they are one of
the easiest things for a media pipeline to lose without anyone noticing: a transcoder that does not
carry the caption user data, a packager that strips SEI, an encoder upgrade that changes a default. The
picture is identical and every pixel check passes, yet the caption stream is gone. Nothing in ordinary
CI is watching for it. Reporting it as a single, clearly named difference -- separate from any picture
finding, since the pixels did not change -- tells a reviewer exactly what to look at.

## Accept / Tune / Silence

### Accept

If dropping (or adding) captions is intentional -- a pipeline that deliberately converts captions to a
sidecar file, for instance -- re-run `mediadiff snapshot` on the new candidate to make it the new
baseline, so the captionless stream is what is expected from then on.

### Tune

There is nothing to tune: presence is a yes-or-no fact, and the check has no tolerance. Severity overrides still decide whether a difference gates the exit code.

### Silence

Set `video.closed_captions` to `ignore` in `[severity]` for a pipeline whose output legitimately never
carries captions, or one that carries them out of band. A silenced check's difference is still
computed and shown under `-v`.
