# video.pix_fmt

## What it measures

The video stream's pixel format, as libav names it (`av_get_pix_fmt_name`) --
one Measurement per video stream, compared as an exact string.

Before comparison, the five pixel formats FFmpeg's own headers mark
deprecated for encoding full-range colour into the format name itself
(`yuvj420p`, `yuvj422p`, `yuvj444p`, `yuvj440p`, `yuvj411p`) are folded to
their plain counterpart (`yuv420p`, `yuv422p`, `yuv444p`, `yuv440p`,
`yuv411p` respectively) before this check's value is computed. A file
declaring `yuvj420p` and an equivalent file declaring `yuv420p` with an
explicit full-range colour range are the SAME intent spelled two different
ways, and this check reports them as identical -- the range difference
itself, when there is one, is `video.color.range`'s job alone.

## Why it matters

The pixel format determines chroma subsampling and bit depth -- a change
here (that survives the fold above) means the decoded picture's own sample
layout changed, which is almost always an unintentional pipeline
misconfiguration rather than a deliberate creative choice.

## Accept / Tune / Silence

### Accept

If the pixel format change was intentional (e.g. deliberately re-encoding
to a different chroma subsampling), re-run `mediadiff snapshot` on the new
candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity (`info` under
`transform`, where an intentional transcode is expected to change the
pixel format) means any change blocks the merge by default outside that
profile.

### Silence

Set `video.pix_fmt` to `ignore` in `[severity]` for a pipeline where pixel
format is deliberately variable and never a meaningful signal on its own.
A silenced check's difference is still computed and shown under `-v`.
