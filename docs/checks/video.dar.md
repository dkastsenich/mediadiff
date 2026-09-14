# video.dar

## What it measures

The display aspect ratio, derived **rationally** from `width * sar_num` over
`height * sar_den` (the effective sample aspect ratio `video.sar` also
reports), reduced by the greatest common divisor -- every step through this
project's checked integer helpers, never a float division. Kept as an exact
`RationalValue`, compared by rational equality.

A stream whose width, height, or effective sample-aspect-ratio denominator
is zero skips as `insufficient_data` rather than reporting a degenerate or
zero-denominator rational.

## Why it matters

The display aspect ratio is what a player actually renders on screen --
combining resolution and pixel shape into the one number a viewer would
notice changed. A change here with no corresponding `video.resolution` or
`video.sar` finding would be a contradiction (this project's serializer
never produces one); when `video.dar` changes alongside either of those, it
confirms the on-screen shape actually moved.

## Accept / Tune / Silence

### Accept

If the display aspect ratio change was intentional (a re-target to a
different delivery shape), re-run `mediadiff snapshot` on the new candidate
to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any change
in the derived ratio blocks the merge by default.

### Silence

Set `video.dar` to `ignore` in `[severity]` for a pipeline where display
aspect ratio is deliberately variable. A silenced check's difference is
still computed and shown under `-v`.
