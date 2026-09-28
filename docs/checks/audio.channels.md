# audio.channels

## What it measures

The audio stream's channel count (`codecpar->ch_layout.nb_channels`), one
Measurement per audio stream at `Scope{audio, N}`, kept as an `int64`.
Derived from the header pass alone -- no decode is required, so it reports
a real value under `--no-content`.

This check reports the COUNT only. It never reports or infers a channel
*layout* (which speaker positions those channels map to) -- that is
`audio.layout`'s own, separately registered concern. A pair of files with
the same channel count but different layouts (e.g. `5.1` vs `5.1(side)`)
compares `pass` here and is caught by `audio.layout` instead, so the two
checks together distinguish "the number of channels changed" from "the
number of channels stayed the same, but what they represent changed".

A file with no audio stream at all still reports this check as
`skipped:insufficient_data`, rather than emitting nothing -- `skipped !=
pass` is load-bearing.

## Why it matters

A channel-count change (stereo collapsing to mono, or a surround mix
losing channels) is one of the most audible regressions a media pipeline
can produce, and is often the result of a misconfigured downmix step.

## Accept / Tune / Silence

### Accept

If the channel-count change was an intentional downmix or remix, re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check.

### Silence

Set `audio.channels` to `ignore` in `[severity]` for a pipeline where
channel count is deliberately variable and never a meaningful signal on its
own.
