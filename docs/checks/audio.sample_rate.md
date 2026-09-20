# audio.sample_rate

## What it measures

The audio stream's core sample rate (`codecpar->sample_rate`), one
Measurement per audio stream at `Scope{audio, N}`, kept as an `int64` in
Hertz. Derived from the header pass alone -- no decode is required, so it
reports a real value under `--no-content`.

Evidence always carries both a `core_rate_hz` key and an `effective_rate_hz`
key. The two are equal for every codec this check alone handles; a later
check (`audio.profile`, 06-04) detects implicit HE-AAC signaling, where the
declared core rate and the decoder's actual effective rate diverge (SBR
doubles the effective rate). This check's own compared value is always the
core rate -- the `effective_rate_hz` key exists so that later work has
somewhere to ride the doubled rate without changing this check's value
shape or introducing a second id.

A file with no audio stream at all still reports this check as
`skipped:insufficient_data`, rather than emitting nothing -- `skipped !=
pass` is load-bearing.

## Why it matters

A sample-rate change silently alters playback speed and pitch on any
downstream consumer that trusts the declared rate -- one of the most basic
properties a media pipeline must preserve unless a resample was explicitly
intended.

## Accept / Tune / Silence

### Accept

If the sample-rate change was an intentional resample, re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check. A pipeline that
legitimately resamples should declare that intent via `--profile
transform` rather than tuning a tolerance on a rate, which has no
meaningful "close enough" magnitude.

### Silence

Set `audio.sample_rate` to `ignore` in `[severity]` for a pipeline where
the sample rate is deliberately variable and never a meaningful signal on
its own.
