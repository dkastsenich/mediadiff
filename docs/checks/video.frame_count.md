# video.frame_count

## What it measures

The number of frames on the video stream, one Measurement per video stream
at `Scope{video, N}`, kept as an `int64` under a zero-magnitude absolute
tolerance (`0frames`, not `exact` -- see Tune below). The count is **always
counted** from the shared packet scan (or, when a parser scan ran, from
its per-access-unit count, which is the more precise figure for codecs
where one packet is not one access unit) -- **never** read from the
container's own `AVStream::nb_frames` claim. The source used (`packet_scan`
or `parser_scan`) rides in evidence, alongside the container's own declared
count and a boolean recording whether the two agree.

This counting rule is fixed, not a convenience default: a count taken from
the container is a muxer-reported number whose accuracy and even
*meaning* varies by container and muxer, so two files in different
container formats with the same true frame count could report different
`nb_frames` values for reasons that have nothing to do with the media
itself. Counting from the scan makes the number comparable across
container types, which is the entire point of this check (VIDEO-02).

When the packet or parser scan was truncated (`partial_scan`), this check
skips rather than reporting a count derived from an incomplete sweep -- a
count from a truncated scan is a confidently wrong number, not a
conservative estimate.

## Why it matters

An unexpected frame-count change usually means dropped or duplicated
frames, a truncated encode, or a frame-rate conversion nobody intended --
each a distinct failure with a distinct fix, all visible through this one
number.

## Accept / Tune / Silence

### Accept

If the frame-count change was an intentional trim, frame-rate conversion,
or duration change, re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The default `0frames` tolerance behaves as exact equality while keeping
`--tol video.frame_count=2frames` (or a `[check.tolerance]` entry in
`mediadiff.toml`) meaningful for a pipeline whose encoder legitimately pads
or trims by a frame or two at open/close boundaries -- `exact` would not
allow this override at all.

### Silence

Set `video.frame_count` to `ignore` in `[severity]` for a pipeline where
frame count is deliberately variable (e.g. live capture with irregular
duration) and never a meaningful signal on its own.
