# container.mp4.faststart

## What it measures

Whether the `moov` box (the file's index) appears BEFORE or AFTER the
`mdat` box (the media payload) in an MP4/MOV file's top-level layout,
determined by comparing the two boxes' own byte offsets from the same
bounded box walk. The value is one of `moov_before_mdat` (commonly called
"faststart" or "web-optimized") or `moov_after_mdat` (the layout an
un-flagged single-pass mux typically produces).

This check does not apply to a fragmented MP4 (one whose top-level box
list contains at least one `moof`): a fragmented file's playback-start
behavior is governed by its init-segment layout instead, not by a single
`moov`/`mdat` ordering. It auto-skips as `skipped:not_applicable_container`
there.

## Why it matters

A player that must download the entire file before it can start playback
(because `moov` is at the end) cannot begin streaming progressively. This
is invisible in every frame-level or bitrate comparison -- the encoded
media is identical either way -- which is exactly why it needs its own
check: a remux, a re-mux tool upgrade, or a CDN transcoding step can
silently flip this layout without touching a single sample.

## Accept / Tune / Silence

### Accept

If the layout change was intentional (e.g. deliberately dropping
`-movflags +faststart` for an archival copy that is never streamed),
re-run `mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

None -- this check has no tolerance; the layout either matches or it
doesn't.

### Silence

Set `container.mp4.faststart` to `ignore` in `[severity]` for a pipeline
that never serves files progressively (e.g. local-only batch processing).
Leave it enabled everywhere a file might be streamed -- a silenced check's
difference is still computed and shown under `-v`.
