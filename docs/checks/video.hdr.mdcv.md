# video.hdr.mdcv

## What it measures

Whether a video stream carries HDR10 mastering-display metadata (SMPTE
2086) -- the three display-primary chromaticities, the white point, and
the min/max mastering-display luminance.

This phase reads that metadata from exactly one place:
`codecpar->coded_side_data` (`AV_PKT_DATA_MASTERING_DISPLAY_METADATA`),
populated by the demuxer at OPEN time -- for example, an MP4 `mdcv` box
attached by libav's own `mov.c` the moment the file is opened, with no
frame decoded. This is the FIRST source VIDEO-09 names, and the only one
available without a decode pass.

VIDEO-09 also names a second source: metadata attached to the first
DECODED frame (HEVC's own mastering-display SEI message, for a codec whose
container-level box is absent but whose bitstream still carries the data).
That second arm requires a decode pass, which does not exist until Phase
7. Rather than silently ignoring it, this check reports the absence
honestly and distinguishes WHY:

- If this stream's codec has no mechanism for carrying frame-level HDR
  metadata at all (e.g. `mpeg4`, `mpeg2video`), an absent stream-level
  value is a genuine, permanent absence -- reported as ordinary `Absent`.
- If this stream's codec COULD carry frame-level HDR metadata (`hevc`,
  `av1`) but the stream-level source was empty, the value is reported as
  `skipped:requires_decode` -- a real possibility this phase simply cannot
  check yet, never presented as if it had been checked and found absent.

Evidence, when present, carries the `source` (always `"stream"` in this
phase), libav's own `has_primaries`/`has_luminance` flags (so a partially
populated payload is visible rather than silently rendered as complete),
and every raw chromaticity/luminance rational. When absent, evidence
carries the codec name and whether it could carry frame-level metadata.

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
