# video.color.matrix

## What it measures

The video stream's YCbCr conversion matrix (`av_color_space_name`, e.g.
`"bt709"`, `"smpte170m"`, `"bt2020nc"`, or `"unknown"` when unspecified)
-- one Measurement per video stream, compared as an exact string against
libav's own raw `codecpar->color_space` field (libav calls this field/
enum `color_space`/`AVColorSpace`; this project's own id and doc 03 call
the identical concept "matrix", since that is what the field actually
encodes -- the YCbCr-to-RGB conversion coefficients, not a colour space
in the CIE sense), with no fold and no special case.

`"unknown"` compares exactly like any other value in both directions
(VIDEO-08) -- a change to or from it is a real, reported difference,
never a wildcard match.

## Why it matters

The matrix defines the coefficients used to convert the stored YCbCr
samples back to RGB for display. A matrix change without a genuine source
change (e.g. the common **bt601-to-bt709** ("smpte170m" to "bt709" in
libav's own names) mismatch between SD and HD content) produces a
**global colour shift** across the entire picture -- greens and reds
shift together in a way that looks like a colour-grading error, even
though every stored sample value is unchanged.

## Accept / Tune / Silence

### Accept

If the matrix change was intentional (e.g. deliberately re-targeting a
different colour space's own matrix), re-run `mediadiff snapshot` on the
new candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any
change in the declared matrix blocks the merge by default.

### Silence

Set `video.color.matrix` to `ignore` in `[severity]` for a pipeline where
the matrix is deliberately variable and never a meaningful signal on its
own. A silenced check's difference is still computed and shown under
`-v`.
