# container.track_count

## What it measures

The number of streams of each media type in the container -- video, audio,
subtitle, data and attachment -- as a five-bin histogram in that fixed
order. Every bin is always present, including a zero count: a file with no
subtitle stream at all still carries a `subtitle: 0` bin rather than
omitting the bin entirely.

## Why it matters

Losing a track is one of the most consequential silent regressions a
pipeline can produce -- a subtitle track quietly dropped during a remux, a
caption data track lost during a transcode, an extra audio stream
accidentally duplicated. Because every bin is always present at a fixed
position, a dropped stream shows up as a VALUE difference (the bin's count
changed) rather than a SHAPE difference the histogram comparison would have
to special-case, and two consecutive runs against the same file produce
byte-identical `--json` output regardless of which order libav happened to
enumerate the streams in.

## Accept / Tune / Silence

### Accept

If the track-count change was intentional (e.g. deliberately stripping a
subtitle track for a distribution profile), re-run `mediadiff snapshot` on
the new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the per-type counts either match
exactly or they don't.

### Silence

Set `container.track_count` to `ignore` in `[severity]` only for a pipeline
whose entire purpose is deliberately adding or removing tracks (e.g. a
subtitle-stripping CI leg). Leave it enabled everywhere else -- a silenced
check's difference is still computed and shown under `-v`, so silencing
never hides the fact that a track count changed, only whether it gates the
exit code.
