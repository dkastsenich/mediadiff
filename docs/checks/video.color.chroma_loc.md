# video.color.chroma_loc

## What it measures

The video stream's chroma sample location (`av_chroma_location_name`,
e.g. `"left"`, `"center"`, or `"unspecified"` -- note this field's own
UNSPECIFIED spelling is `"unspecified"`, distinct from the other three
colorimetry checks' `"unknown"`) -- one Measurement per video stream,
compared as an exact string against libav's own raw
`codecpar->chroma_location` field, with no fold and no special case.

`"unspecified"` compares exactly like any other value in both directions
(VIDEO-08, VIDEO-07-E1) -- a change to or from it is a real, reported
difference, never a wildcard match. An unspecified-versus-`"left"` pair
is not treated as equivalent.

## Why it matters

Chroma sample location tells a scaler exactly where the subsampled
chroma samples sit relative to the luma grid. A mismatch here is
subtle -- it does not change any stored pixel value -- but it changes how
a correct scaler/upsampler should interpolate chroma, which shows up as
a soft chroma-alignment shift most visible on sharp colour edges. It is
the kind of drift a scaler-chain change (a different filter, a different
library, a different resize implementation) tends to introduce silently.

## Accept / Tune / Silence

### Accept

If the chroma location change was intentional (e.g. deliberately
re-targeting a different chroma siting convention), re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

There is no tolerance to tune -- `exact` semantics means any change in
the declared chroma location is reported. Severity is `warn`, not `fail`,
deliberately: a chroma-location drift is a real, worth-noticing scaler-
chain signal, but on its own it is rarely severe enough to block a merge
outright the way a colour range or matrix flip is -- promote it to `fail`
in `[severity]` for a pipeline where sub-pixel chroma alignment is a hard
requirement.

### Silence

Set `video.color.chroma_loc` to `ignore` in `[severity]` for a pipeline
where chroma sample location is deliberately variable and never a
meaningful signal on its own. A silenced check's difference is still
computed and shown under `-v`.
