# video.sar

## What it measures

The **effective** sample aspect ratio (pixel shape) of a video stream, one
Measurement per video stream, kept as an exact `RationalValue` compared by
rational equality -- never rendered to a float. libav exposes two possibly
different values: the container's own claim (`AVStream::sample_aspect_ratio`,
e.g. mp4's `pasp` box) and the bitstream's own claim
(`codecpar->sample_aspect_ratio`, the VUI/VOL-header value). The container's
value is the one compared here, matching libav's own resolution order --
their disagreement, when one exists, is reported separately by
`video.sar.conflict`.

A `0/1` sample aspect ratio in either position means the source declared
nothing at all, which this check treats as `1:1` for comparison purposes.
The stream's own declared value being unset rides in evidence as a boolean,
so a file that explicitly declares `1:1` stays distinguishable from one that
declares nothing at all -- both compare equal, but only one had an opinion.

## Why it matters

A pixel aspect ratio change (without a resolution change) means the frame
displays at a different physical shape even though the encoded pixel grid
is unchanged -- a classic "the video looks stretched" regression that a
pixel-count-only resolution check cannot see.

## Accept / Tune / Silence

### Accept

If the pixel aspect ratio change was intentional (e.g. re-targeting an
anamorphic delivery format), re-run `mediadiff snapshot` on the new
candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any change
in the effective ratio blocks the merge by default.

### Silence

Set `video.sar` to `ignore` in `[severity]` for a pipeline where pixel
aspect ratio is deliberately variable and never a meaningful signal on its
own. A silenced check's difference is still computed and shown under `-v`.
