# audio.sample_rate

## What it measures

The audio stream's core sample rate (`codecpar->sample_rate`), one
Measurement per audio stream at `Scope{audio, N}`, kept as an `int64` in
Hertz. Derived from the header pass alone -- no decode is required, so it
reports a real value under `--no-content`.

Evidence always carries both a `core_rate_hz` key and an `effective_rate_hz`
key. `core_rate_hz` is `codecpar->sample_rate` exactly as it stands AFTER
`avformat_find_stream_info()` -- which is what keeps this check's compared
value pass-independent (06-RESEARCH.md Q4): that call happens once, inside
the header pass alone, before any `--content` distinction exists, so the
same value is read whichever later passes run. For a compressed AAC stream
that decode already writes the decoder's own output rate back into
`codecpar`, so `core_rate_hz` is ALREADY THE DOUBLED RATE for an implicitly
signaled stream (`audio_sbr_implicit.mp4`: 44100 -> 88200) exactly as it is
for an explicitly signaled one (`isom.c`'s own `ext_sample_rate` branch,
06-RESEARCH.md Q4) -- `core_rate_hz`'s name predates this finding and is a
published evidence key, so it stays despite no longer describing an
"undoubled core" rate (06-18-PLAN.md, WR-09 correction).
`effective_rate_hz` (06-04-PLAN.md / 06-18-PLAN.md, AUDIO-03, CR-05
secondary) is never a formulaic doubling of `core_rate_hz` -- for an
HE-AAC stream whose SBR signaling `audio.profile` (06-04) resolved as
`implicit`, it carries the DECODE-OBSERVED rate from whichever D-12 step
actually resolved that stream (the header pass's own primary mechanism, or
the bounded fallback probe when the header pass resolved nothing); for
`explicit` and every other case it equals `core_rate_hz` unchanged. The two
keys are therefore equal for the common case (the decode-observed rate
agrees with `codecpar`'s own already-resolved rate), and the
`effective_rate_hz` key exists so the implicit case's real,
decoder-observed rate is visible in evidence without changing this check's
value shape or introducing a second id.

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
