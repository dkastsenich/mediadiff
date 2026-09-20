# audio.sample_rate

## What it measures

The audio stream's core sample rate (`codecpar->sample_rate`), one
Measurement per audio stream at `Scope{audio, N}`, kept as an `int64` in
Hertz. Derived from the header pass alone -- no decode is required, so it
reports a real value under `--no-content`.

Evidence always carries both a `core_rate_hz` key and an `effective_rate_hz`
key. `core_rate_hz` is `codecpar->sample_rate` itself, which is what keeps
this check's compared value pass-independent (06-RESEARCH.md Q4).
`effective_rate_hz` is never a formulaic doubling of `core_rate_hz` -- for
an HE-AAC stream whose SBR signaling `audio.profile` (06-04) resolved as
`implicit`, it carries the bounded probe's own DIRECTLY-OBSERVED decoded
rate; for `explicit` and every other case it equals `core_rate_hz`
unchanged. This asymmetry exists because `codecpar->sample_rate` cannot be
assumed undoubled going in: 06-RESEARCH.md Q4 confirmed the MP4 demuxer
itself writes the ALREADY-DOUBLED rate for **explicit** signaling
(`isom.c`'s own `ext_sample_rate` branch), and empirically
`avformat_find_stream_info()`'s own internal probing can ALSO already
resolve `codecpar->sample_rate` to the doubled value for a short
**implicit**-signaled stream, entirely within the header pass (reproducible
identically with and without `--content`). A literal `core_rate_hz * 2`
for the implicit case would therefore risk fabricating a false, quadrupled
rate whenever `codecpar` already carries the doubled value -- so
`effective_rate_hz` always reflects what the probe itself actually
decoded, never a derived formula. The two keys may therefore be equal even
for an `implicit`-signaled stream, when the probe's own decode agrees with
`codecpar`'s already-resolved rate. The `effective_rate_hz` key exists so
the implicit case's real, probe-confirmed rate is visible in evidence
without changing this check's value shape or introducing a second id.

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
