# video.color.primaries

## What it measures

The video stream's colour primaries (`av_color_primaries_name`, e.g.
`"bt709"`, `"smpte170m"`, `"bt2020"`, or `"unknown"` when unspecified) --
one Measurement per video stream, compared as an exact string against
libav's own raw `codecpar->color_primaries` field, with no fold and no
special case of any kind.

`"unknown"` (libav's own rendering of `AVCOL_PRI_UNSPECIFIED`) compares
exactly like any other value. A change to `"unknown"`, or a change away
from it, is a real, reported difference in both directions -- never
treated as a wildcard or an absent-value match.

## Why it matters

Colour primaries define which real-world colours the stream's red, green
and blue components actually correspond to. A primaries change without a
matching transfer/matrix change means colours are being decoded and
displayed through the wrong mapping -- reds shift, skin tones shift,
saturated content in particular can look visibly wrong even though every
pixel value in the file is byte-identical to before.

## Accept / Tune / Silence

### Accept

If the primaries change was intentional (e.g. deliberately re-targeting a
different colour gamut), re-run `mediadiff snapshot` on the new candidate
to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any
change in the declared primaries blocks the merge by default.

### Silence

Set `video.color.primaries` to `ignore` in `[severity]` for a pipeline
where colour primaries are deliberately variable and never a meaningful
signal on their own. A silenced check's difference is still computed and
shown under `-v`.
