# container.format

## What it measures

The container family libav resolved for the file -- the first
comma-delimited token of `AVInputFormat.name` (e.g. `"mov"` for an MP4/MOV
file, `"matroska"` for a Matroska or WebM file, `"mpegts"` for an MPEG
transport stream). This is doc 02 section 2's own extraction rule: libav
reports MP4 as `"mov,mp4,m4a,3gp,3g2,mj2"`, and only the first token is
kept.

## Why it matters

A baseline and candidate that disagree on container family are almost
never a benign difference -- remuxing MP4 to Matroska (or the reverse)
changes which downstream tooling can even open the file, which metadata
survives, and which of this project's own container-specific checks
(`container.mp4.*`, `container.mkv.*`, `container.ts.*`) apply at all. This
is also the phase's own tracer check: it is the first real measurement
that flows from an actual media file, through the probe layer, into a
rendered report, and every later container check builds on the same
`DemuxSession` this one already proves works end to end.

## Accept / Tune / Silence

### Accept

If the remux was intentional (e.g. a deliberate MP4-to-MKV pipeline
change), re-run `mediadiff snapshot` on the new candidate to establish it
as the new baseline, after confirming the remux didn't also silently drop
metadata this project's other container checks would have caught.

### Tune

None -- this check has no tolerance; two container families either match
exactly or they don't.

### Silence

Set `container.format` to `ignore` in `[severity]` only for a pipeline
whose entire purpose is cross-container remuxing (e.g. a CI leg that
specifically tests MP4-to-MKV conversion). Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`, so silencing never hides the fact that the container changed, only
whether it gates the exit code.
