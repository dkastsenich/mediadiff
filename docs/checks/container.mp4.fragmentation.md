# container.mp4.fragmentation

## What it measures

Whether the file is muxed as `progressive` (a single `moov` describing the
whole media, `moof_count == 0`) or `fragmented` (one or more `moof` boxes,
each describing one movie fragment -- the layout CMAF/DASH/HLS-fMP4
delivery requires). `evidence` carries the exact `moof_count` and whether
an `sidx` (segment index) box is present.

This is the MECHANISM half of doc 02's fragmentation row; the median
fragment DURATION is a separate check,
`container.mp4.fragment_duration`, since the two need independent
semantics (exact-match mode here, a tolerance-bounded duration there).

## Why it matters

A CMAF/low-latency delivery pipeline REQUIRES fragments; a progressive
file silently substituted for a fragmented one (or vice versa) will fail
to play correctly in a segment-based delivery chain even though the
encoded media itself is unchanged. This is a structural property no
frame-level comparison can see.

## Accept / Tune / Silence

### Accept

If the fragmentation mode change was intentional (e.g. switching a
pipeline stage from progressive to fragmented output), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the mode either matches or it
doesn't.

### Silence

Set `container.mp4.fragmentation` to `ignore` in `[severity]` for a
pipeline stage that is known to legitimately alternate between progressive
and fragmented output. Leave it enabled everywhere else -- a silenced
check's difference is still computed and shown under `-v`.
