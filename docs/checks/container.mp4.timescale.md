# container.mp4.timescale

## What it measures

The `mvhd` box's own movie timescale, at global scope, plus each track's
own `mdhd` media timescale, at that track's own scope. Every timestamp in
the file is expressed in ticks of one of these timescales, so a timescale
change alone (with no change to the tick VALUES) rescales every duration
and timestamp the track carries.

## Why it matters

A timescale is a rounding-drift mechanism: a track re-muxed at a
coarser timescale (e.g. `12800` instead of `90000`) loses precision on
every timestamp it carries, which surfaces downstream as accumulated
timing jitter (`timeline.jitter`, elsewhere in this project's check
family) even when the source frames themselves are bit-identical. Catching
the timescale change directly, at the mechanism level, makes root-causing
a later jitter regression far faster than starting from the jitter
symptom alone.

## Accept / Tune / Silence

### Accept

If the timescale change was intentional (e.g. deliberately switching to a
muxer default that uses a different timescale), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the timescale either matches exactly
or it doesn't.

### Silence

Set `container.mp4.timescale` to `ignore` in `[severity]` for a pipeline
where timescale differences are expected and downstream jitter is
independently monitored via `timeline.jitter`. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
