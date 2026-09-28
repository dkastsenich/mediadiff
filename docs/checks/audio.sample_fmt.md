# audio.sample_fmt

## What it measures

The audio stream's decoder-native sample format (`codecpar->format`), one
Measurement per audio stream at `Scope{audio, N}`, resolved through
`av_get_sample_fmt_name` and reported in its PACKED-equivalent spelling --
`fltp` (planar float) and `flt` (packed float) record identically, so a
planar/packed distinction alone is never reported as a format change. This
is the same canonicalization `content.audio.sample_hash` applies to a
decoder's actual output format, applied here to the codec's own declared
format at the header pass alone -- no decode is required, so this check
reports a real value under `--no-content`. Evidence carries the raw
`AVSampleFormat` ordinal.

A file with no audio stream at all still reports this check as
`skipped:insufficient_data`, rather than emitting nothing -- `skipped !=
pass` is load-bearing.

## Why it matters

A sample-format change (e.g. 16-bit integer to 32-bit float) changes the
numeric precision and dynamic range available to every downstream
processing stage, and often accompanies an unintended re-encode.

## Accept / Tune / Silence

### Accept

If the sample-format change was an intentional re-encode, re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check over a canonical
string.

### Silence

Set `audio.sample_fmt` to `ignore` in `[severity]` for a pipeline where the
sample format is deliberately variable and never a meaningful signal on its
own.
