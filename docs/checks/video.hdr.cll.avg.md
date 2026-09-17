# video.hdr.cll.avg

## What it measures

MaxFALL (maximum frame-average light level, cd/m²) -- `AVContentLightMetadata`'s
other plain unsigned integer field, from the same stream-level side data
`video.hdr.cll`/`video.hdr.cll.max` read. Compared as an `int64` under a
five percent tolerance, the same shape as `video.hdr.cll.max`.

This is deliberately its OWN check id, never evidence riding on
`video.hdr.cll.max`. Doc 03 section 4 names both MaxCLL and MaxFALL under
one design-doc row, but folding MaxFALL into `video.hdr.cll.max`'s
evidence would mean a pipeline that halved MaxFALL alone -- leaving
MaxCLL untouched -- reports a clean `pass` on the only check watching
this data. That is exactly the class of false negative this project
treats as a P0 defect (a diff tool that misses a real regression is worse
than one that over-reports). Giving MaxFALL its own id makes it
independently attributable: a MaxFALL-only regression shows up as a
MaxFALL-only finding, not silently folded into its sibling's evidence.

When there is nothing to measure, this check emits the same named skip
its sibling `video.hdr.cll.max` does (never `Absent`), sharing the
identical extraction and codec-capability decision.

## Why it matters

MaxFALL tells a downstream tone-mapper the brightest a FULL FRAME averages
to, distinct from MaxCLL's single-pixel peak -- together they bound both
the transient and sustained brightness a display's tone-mapping curve
needs to handle. A regression in MaxFALL alone (without a corresponding
MaxCLL change) is a real, distinct signal: sustained brightness handling
has changed even though peak-pixel handling has not, which a display's
tone-mapper responds to differently than a peak-only change.

## Accept / Tune / Silence

### Accept

If the MaxFALL change reflects an intentional re-grade or a legitimate
content-light re-measurement, re-run `mediadiff snapshot` on the new
candidate.

### Tune

Override the five percent default with
`[tolerance] video.hdr.cll.avg = "N%"` for a pipeline whose re-derivation
process is known to drift by more than five percent, or tighten it for a
strict bit-exact remux pipeline that should never see any drift.

### Silence

Set `video.hdr.cll.avg` to `ignore` in `[severity]` for a pipeline with no
tone-mapping-sensitive downstream consumer. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
