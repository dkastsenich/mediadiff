# audio.bit_depth

## What it measures

The audio stream's declared raw bit depth (`codecpar->bits_per_raw_sample`),
one Measurement per audio stream at `Scope{audio, N}`, kept as an `int64`.
Derived from the header pass alone -- no decode is required, so it reports
a real value under `--no-content`.

When a codec declares no `bits_per_raw_sample` at all (most lossy codecs,
which have no single meaningful "bit depth" the way lossless PCM/FLAC do),
this check reports `Absent{}` with an explicit `skipped:insufficient_data`
reason -- **never** a fabricated `0`, and **never** silently coerced to the
sample format's container width (e.g. reporting 16 for a codec whose
sample container happens to be 16-bit s16, when the codec itself declared
nothing). A fabricated number here is exactly the class of false positive
this project exists to prevent: it would either manufacture a spurious
difference against a codec that legitimately declares a real bit depth, or
mask a genuine bit-depth change behind a value that was never actually
declared.

A file with no audio stream at all still reports this check as
`skipped:insufficient_data`, rather than emitting nothing -- `skipped !=
pass` is load-bearing.

## Why it matters

A bit-depth change on a lossless or near-lossless source (16-bit vs 24-bit
PCM/FLAC) is a real quality and file-size decision, usually made
deliberately -- an unnoticed change here means either a lossy truncation
crept in, or storage/bandwidth assumptions downstream are now wrong.

## Accept / Tune / Silence

### Accept

If the bit-depth change was an intentional quality/size trade-off, re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check.

### Silence

Set `audio.bit_depth` to `ignore` in `[severity]` for a pipeline where bit
depth is deliberately variable and never a meaningful signal on its own.
This check is naturally silent (via `skipped`) on any codec that declares
no bit depth at all -- no severity override is needed for that case.
