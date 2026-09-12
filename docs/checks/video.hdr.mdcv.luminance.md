# video.hdr.mdcv.luminance

## What it measures

The mastering display's maximum luminance (cd/m²), from the same
stream-level `AV_PKT_DATA_MASTERING_DISPLAY_METADATA` side data
`video.hdr.mdcv` reads, compared under a five percent tolerance rather
than the plain presence check that sibling performs. Split into its own
id from `video.hdr.mdcv` because a `presence` check never compares
values -- `src/compare/presence.cpp`'s own documented rule is that a
value-level comparison belongs on a separate `tol` check over the same
extraction, or a mastering-display payload whose luminance was halved
would still report a clean `pass`.

The minimum mastering-display luminance rides alongside in evidence, for
context, but is never itself compared -- doc 03 section 4 tolerates only
the maximum.

When there is nothing to measure (mastering-display data absent at the
stream level, present but missing its own `has_luminance` flag, or
carrying an unusable luminance rational), this check emits a named skip
rather than `Absent` -- the tolerance comparator treats an absent value
as an error, which is a worse report than an honest skip. Evidence on a
skip records the codec and whether it could, in principle, carry
frame-level HDR metadata a future decode pass might surface (VIDEO-09-E1)
-- see `video.hdr.mdcv`'s own doc for the full could/could-not
distinction, which this check's skip evidence echoes without itself
splitting into two different skip reasons.

## Why it matters

A pipeline that re-derives or re-encodes HDR content can legitimately
shift the mastering luminance by a small amount without any real
regression -- the five percent tolerance absorbs that. A LARGER shift, or
a value that moves in a direction inconsistent with any legitimate
re-grade, changes how a display maps highlights: a display expecting
1000 nits of mastering headroom that instead receives a stream declaring
400 nits will tone-map differently, visibly compressing highlight detail
that was never meant to be compressed.

## Accept / Tune / Silence

### Accept

If the luminance change reflects an intentional re-grade or re-encode,
re-run `mediadiff snapshot` on the new candidate.

### Tune

Override the five percent default with `[tolerance] video.hdr.mdcv.luminance = "N%"`
for a pipeline whose re-derivation process is known to drift by more than
five percent, or tighten it for a strict bit-exact remux pipeline that
should never see any drift at all.

### Silence

Set `video.hdr.mdcv.luminance` to `ignore` in `[severity]` for a pipeline
with no luminance-sensitive downstream consumer. Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
