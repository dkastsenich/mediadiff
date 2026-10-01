# video.hdr.cll

## What it measures

Whether a video stream carries HDR content-light metadata (CTA-861.3) --
MaxCLL (maximum content light level) and MaxFALL (maximum frame-average
light level), the two values needed to transmit HDR over HDMI.

Read from the identical two-arm extraction as `video.hdr.mdcv`, with the
same precedence and the same evidence `source`: `"stream"` for
`codecpar->coded_side_data` (`AV_PKT_DATA_CONTENT_LIGHT_LEVEL`, populated at
demux time), `"frame"` for the content-light side data libavcodec attached to
the first decoded frame once the decode pass runs (HEVC, AV1 and H.264; the
first frame only, whatever `--sample N` is). Stream-level metadata wins, both
arms share one size-guarded payload reader, and the could/could-not-carry,
`--no-content`, truncated-decode and zero-frame outcomes are exactly
`video.hdr.mdcv`'s (a real `Absent`, `skipped:requires_decode`,
`skipped:partial_scan` and `skipped:insufficient_data` respectively). See
`video.hdr.mdcv`'s own doc for the full reasoning -- this check shares the
exact same extraction seam and codec-capability decision, never a second,
independently-written copy.

Evidence, when present, carries MaxCLL, MaxFALL and the `source` tag.

## Why it matters

Content-light metadata tells an HDMI-connected HDR display how bright a
single pixel and a full frame can get, which the display uses to set its
own tone-mapping curve. Losing it during a remux silently strips the
signal a TV needs to tone-map correctly -- without it, many displays fall
back to a generic default that can either clip highlights the content
never intended to clip, or under-utilize the display's own peak
brightness.

## Accept / Tune / Silence

### Accept

If the content-light metadata change was intentional (an SDR downconvert,
a re-grade, or a corrected declaration), re-run `mediadiff snapshot` on
the new candidate.

### Tune

There is no tolerance to tune for presence itself. The sibling checks
`video.hdr.cll.max` and `video.hdr.cll.avg` carry the value-level
tolerances.

### Silence

Set `video.hdr.cll` to `ignore` in `[severity]` for a pipeline with no
HDMI/HDR-display-aware downstream consumer. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
