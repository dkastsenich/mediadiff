# video.frame_rate.declared

## What it measures

The stream's own declared frame rate, `AVStream::avg_frame_rate`, kept as an
exact `RationalValue` with the stream's timebase, compared by rational
equality -- never rendered to a float for comparison. `30000/1001` and
`29.97` must never be compared as floating-point numbers; this check never
converts either side to one. `r_frame_rate` (libav's own "smallest frame
rate this stream could be" guess, which for a genuinely variable-rate
stream can differ from `avg_frame_rate`) rides in evidence alongside.

## Why it matters

The declared rate is what the container and its muxer told every downstream
consumer the frame rate is -- a change here usually means an intentional
re-encode at a different rate, a muxer misconfiguration, or a corrupted
header. It is compared independently of `video.frame_rate.measured` (the
actually-observed cadence) precisely so a mismatch **between** the two,
surfaced as `video.frame_rate.measured`'s own evidence flag, is itself a
distinct, diagnosable signal.

## Accept / Tune / Silence

### Accept

If the declared frame rate change was intentional (a genuine re-encode at a
different rate), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any change
in the declared rational blocks the merge by default.

### Silence

Set `video.frame_rate.declared` to `ignore` in `[severity]` for a pipeline
where the container's declared rate is not itself a meaningful signal (for
example, a genuinely variable-rate source where only the measured cadence
matters). A silenced check's difference is still computed and shown under
`-v`.
