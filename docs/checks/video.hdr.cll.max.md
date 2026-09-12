# video.hdr.cll.max

## What it measures

MaxCLL (maximum content light level, cd/m²) -- `AVContentLightMetadata`'s
own plain unsigned integer field, read from the same stream-level side
data `video.hdr.cll` reads. Unlike the mastering-display family's
chromaticity/luminance rationals, MaxCLL arrives from libav as a plain
integer already in cd/m² -- no rational wrapping, no quantisation grid;
this check compares it directly as an `int64` under a five percent
tolerance.

Split into its own id from `video.hdr.cll` for the same reason
`video.hdr.mdcv.luminance` was split from `video.hdr.mdcv`: a `presence`
check never compares values.

When there is nothing to measure, this check emits the same named skip
`video.hdr.mdcv.luminance` does (never `Absent`), sharing the identical
extraction and codec-capability decision.

## Why it matters

MaxCLL tells a downstream tone-mapper the single brightest pixel value
the content ever reaches, which the mapper uses to decide how aggressively
to compress highlights for a display with less peak brightness than the
content was mastered for. A pipeline that re-derives content-light values
by actually measuring the decoded pixels (rather than copying the
original declaration) will legitimately land close to, but not bit-exact
with, the original value -- the five percent tolerance absorbs that
measurement noise so it is not reported as a false regression. A larger
shift means the tone-mapper's headroom assumption has genuinely changed.

## Accept / Tune / Silence

### Accept

If the MaxCLL change reflects an intentional re-grade or a legitimate
content-light re-measurement, re-run `mediadiff snapshot` on the new
candidate.

### Tune

Override the five percent default with
`[tolerance] video.hdr.cll.max = "N%"` for a pipeline whose re-derivation
process is known to drift by more than five percent, or tighten it for a
strict bit-exact remux pipeline that should never see any drift.

### Silence

Set `video.hdr.cll.max` to `ignore` in `[severity]` for a pipeline with no
tone-mapping-sensitive downstream consumer. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
