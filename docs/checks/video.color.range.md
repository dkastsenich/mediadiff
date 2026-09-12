# video.color.range

## What it measures

The video stream's effective colour range (`av_color_range_name`: `"tv"`
for limited/studio-swing range, `"pc"` for full-swing range, `"unknown"`
when the source declared nothing) -- one Measurement per video stream,
compared as an exact string.

The value is the range AFTER `video.pix_fmt`'s own yuvj-fold has run: a
file declaring one of the five deprecated `yuvj*` pixel formats always
resolves to `"pc"` here regardless of what its own `color_range` field
happened to say, because those formats' own name IS the full-range
declaration (FFmpeg's own pixel-format enum comments: "deprecated in
favor of [the plain format] and setting color_range"). A file declaring
an ordinary (non-`yuvj`) pixel format reports its own declared range
verbatim, unmodified by the fold.

## Why it matters

A colour range flip changes how every decoder maps the file's stored
sample values to displayed luminance: limited-to-full washes highlights
and shadows out toward flat white/black, full-to-limited crushes the top
and bottom of the range into banding. This is one of the most visually
obvious classes of regression a media pipeline can produce, and it is
also one of the easiest to introduce silently -- a single wrong
`-color_range` flag, or an encoder defaulting differently than expected.

## Accept / Tune / Silence

### Accept

If the colour range change was genuinely intentional (e.g. deliberately
re-targeting a full-range delivery profile), re-run `mediadiff snapshot`
on the new candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any
change in the effective colour range blocks the merge by default.

### Silence

**This check is not one to silence.** `video.color.range` deliberately
carries no severity or tolerance override in ANY profile, including
`transform` -- where most other identity checks (e.g. `video.codec`,
`video.pix_fmt`) demote to `info` because an intentional transcode is
expected to change them. A colour range flip is different: preserving
colour intent through an intentional transformation is exactly the
invariant the `transform` profile exists to protect, per VIDEO-07. If a
pipeline genuinely needs this check silenced, that is a signal the
pipeline's own colour handling should be re-examined first, not that this
check is wrong.
