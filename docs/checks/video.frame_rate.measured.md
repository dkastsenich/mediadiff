# video.frame_rate.measured

## What it measures

The frame rate actually observed in the packet stream, derived from
`src/probe/cadence.h`'s shared, pure `derive_cadence` function -- **the same
probe-level derivation Phase 5's timeline analysis reads, not a second
sweep or an independently re-derived statistic of this check's own**. This
check is that derivation's first consumer: it reads the mode (most
frequent) interval between consecutive usable presentation timestamps,
expresses it as an exact `RationalValue` rate (never pre-divided, never a
float), and compares it under a tight relative tolerance.

The measurement is taken on the presentation (PTS) axis whenever the stream
carries one, falling back to the decode (DTS) axis only when PTS is
entirely absent from the stream; the axis actually used, the mode interval
in ticks, the number of intervals matching that mode, the total number of
intervals considered, and the resulting CFR/VFR class (decided by exact
integer comparison within a fixed epsilon, never a runtime-computed mean)
all ride in evidence under `-v`. A boolean also records whether this
measured rate agrees with `video.frame_rate.declared`'s own value within
tolerance -- doc 03's own "declared-vs-measured internal mismatch" signal,
visible even when comparing a file against itself.

A **VFR** classification does not skip this check: a variable-rate stream
still has a meaningful modal cadence, and the evidence's own class field is
what tells a reader the stream is not constant-rate. This check skips only
when the derivation itself cannot produce an interval at all
(`no_timing_data`/`insufficient_data`) or when the underlying packet scan
was truncated (`partial_scan`) -- a rate derived from an incomplete sweep is
a confidently wrong number, not a conservative estimate.

## Why it matters

An unexpected change in the actually-measured cadence -- as opposed to what
the container merely claims -- usually means dropped/duplicated frames, a
frame-rate conversion, or a genuinely broken encode, independent of whether
the container's own declared rate changed at all.

## Accept / Tune / Silence

### Accept

If the measured rate change was intentional (a genuine frame-rate
conversion or re-encode), re-run `mediadiff snapshot` on the new candidate
to establish it as the new baseline.

### Tune

The default `±0.1%` tolerance is deliberately tight: a measured cadence
that moves at all is usually a real rate change rather than measurement
noise, since the derivation itself is exact-integer and deterministic (no
floating-point jitter can produce a spurious delta here the way it could
for a rate computed via floating-point division). Widen it via
`--tol video.frame_rate.measured=1%` or a `[check.tolerance]` entry only for
a pipeline with a known source of legitimate small cadence variation.

### Silence

Set `video.frame_rate.measured` to `ignore` in `[severity]` for a pipeline
where measured cadence is expected to vary and is not itself a meaningful
signal. A silenced check's difference is still computed and shown under
`-v`.
