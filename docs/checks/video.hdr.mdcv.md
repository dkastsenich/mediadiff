# video.hdr.mdcv

## What it measures

Whether a video stream carries HDR10 mastering-display metadata (SMPTE
2086) -- the three display-primary chromaticities, the white point, and
the min/max mastering-display luminance.

This check reads that metadata from two places, in this order, and records
which one fired in the evidence `source`:

1. `"stream"` -- `codecpar->coded_side_data`
   (`AV_PKT_DATA_MASTERING_DISPLAY_METADATA`), populated by the demuxer at
   OPEN time -- for example, an MP4 `mdcv` box attached by libav's own
   `mov.c` the moment the file is opened, with no frame decoded.
2. `"frame"` -- the mastering-display side data libavcodec attached to the
   FIRST decoded frame (an SEI message in the bitstream of a stream whose
   container carries no box of its own). This arm exists once the video
   decode pass runs; it reads the first decoded frame only, whatever
   `--sample N` is.

Stream-level metadata keeps precedence. A container's entry is mapped by
libavcodec onto every decoded frame too, so a stream that has one is never
re-read from a frame and reports `source: "stream"` with or without
decoding. Both arms convert the payload through one shared, size-guarded
reader, so the rationals cannot differ between them, and the arm is
evidence only -- never part of the measurement's identity, so a file whose
metadata moves from the bitstream into the container on a remux compares
equal values from different sources.

Which codecs can carry frame-level metadata at all is a closed table:
`hevc`, `av1` and `h264` (libavcodec's H.264 and HEVC decoders share one
SEI export). When neither arm found the metadata the result depends on
whether it could have been found:

- `mpeg4`, `mpeg2video` and any other codec outside the table cannot carry
  it at frame level: an absence is a genuine, permanent absence, reported
  as ordinary `Absent`.
- A table codec with the decode pass available and a first frame read that
  carried nothing is a REAL `Absent` (no skip reason), with
  `decode_available: true` in the evidence.
- A table codec under `--no-content` (no decode pass) is
  `skipped:requires_decode` -- a value never depends on which passes ran,
  only on whether it could be measured.
- A table codec whose decode never reached a first frame is a named skip:
  `skipped:partial_scan` when it was cut short or the stream could not be
  decoded (the evidence says which, and carries `decode_truncation_reason`
  when truncated), `skipped:insufficient_data` when the decode completed
  without producing a frame.

Dolby Vision (`video.hdr.dovi`) is not part of this arm: it is a
configuration record rather than per-frame static metadata, so it stays
stream-level only.

Evidence, when present, carries the `source` (`"stream"` or `"frame"`),
libav's own `has_primaries`/`has_luminance` flags (so a partially populated
payload is visible rather than silently rendered as complete), and every
raw chromaticity/luminance rational. When absent, evidence carries the codec
name, whether it could carry frame-level metadata and, once a decode ran,
`decode_available`.

## Why it matters

Mastering-display metadata tells an HDR display or a tone-mapper what
color volume the content was graded for. Losing it during a remux or
transcode silently degrades an HDR file to an unlabeled one -- players
fall back to a generic default mapping, and the intentional highlight/
shadow detail the mastering engineer graded for is no longer honored. A
diff tool that cannot tell "this file never had HDR metadata" apart from
"this file's HDR metadata just vanished" is not useful for catching that
regression.

## Accept / Tune / Silence

### Accept

If the HDR metadata change was intentional (a deliberate SDR downconvert,
a re-grade, or a corrected mastering declaration), re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

There is no tolerance to tune for presence itself -- `presence` semantics
mean the check passes iff both sides are absent or both are present. The
sibling checks `video.hdr.mdcv.luminance` and `video.hdr.mdcv.primaries`
carry the value-level tolerances.

### Silence

Set `video.hdr.mdcv` to `ignore` in `[severity]` for a pipeline with no
HDR-aware downstream consumer at all. Leave it enabled everywhere else --
a silenced check's difference is still computed and shown under `-v`.
