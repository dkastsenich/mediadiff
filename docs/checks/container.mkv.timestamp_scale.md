# container.mkv.timestamp_scale

## What it measures

The `TimestampScale` element from a Matroska/WebM file's `Info` --
the number of nanoseconds one raw timecode tick represents, at global
scope. Matroska's own default is 1,000,000 (i.e. 1 ms per tick); evidence
records that default so a reader can immediately tell a departure from the
default apart from a change between two already-nonstandard values.

## Why it matters

`TimestampScale` sets the fundamental time RESOLUTION every timestamp in
the file is expressed in. Changing it changes how finely (or coarsely) a
muxer can represent presentation and decode times -- a coarser scale can
introduce or hide sub-tick rounding that surfaces later as timeline jitter
(doc 04's own concern), even though nothing about the actual media samples
changed. A remux or a different muxer version choosing a different scale is
exactly the kind of silent, structural change this project exists to
surface.

## Accept / Tune / Silence

### Accept

If the scale change was intentional (e.g. a deliberate remux with a muxer
that defaults differently), re-run `mediadiff snapshot` on the new
candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the declared scale either matches or
it doesn't.

### Silence

Set `container.mkv.timestamp_scale` to `ignore` in `[severity]` for a
pipeline that only ever compares decoded-domain timing (already covered by
other checks) and treats the container's own declared scale as
uninteresting. Leave it enabled everywhere else -- a silenced check's
difference is still computed and shown under `-v`.
