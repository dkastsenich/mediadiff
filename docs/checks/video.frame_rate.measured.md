# video.frame_rate.measured

## What it measures

The frame rate actually observed in the packet stream, derived from
`src/probe/cadence.h`'s shared, pure `derive_cadence` function -- **the same
probe-level derivation Phase 5's timeline analysis reads, not a second
sweep or an independently re-derived statistic of this check's own**. This
check is that derivation's first consumer.

**D-05 (Phase 5, amending Phase 4's D-07):** the rate is derived from the
stream's own SPAN -- the first-to-last usable presentation timestamp
divided by the number of intervals between them -- never from the most
frequent (mode) interval. This is deliberate, not incidental: on a coarse
timebase such as Matroska's 1 ms, a genuinely constant cadence is stored as
a rounding sequence (NTSC's 29.97 fps frame interval reads as 33/33/34 ms,
repeating), and the MOST FREQUENT value in that sequence (33 ms) is not the
TRUE average interval -- deriving a rate from it reads 30.303 fps against
the true 29.970 fps, a false cadence change on content that never changed.
The span basis sidesteps this entirely: the same content measures the same
true rate regardless of which timebase stored it. The rate is held as an
exact `RationalValue` (never pre-divided, never a float) and compared under
a tight relative tolerance.

The measurement is taken on the presentation (PTS) axis whenever the stream
carries one, falling back to the decode (DTS) axis only when PTS is
entirely absent from the stream; the axis actually used, the span in ticks,
the exact ideal interval (`span/interval_count`), the number of timestamps
conforming to that ideal grid, the total number considered, the mode
interval and its own matching/total counts (D-07's original fields, still
populated for a same-timebase consumer), and the resulting CFR/VFR class
(decided by D-05's grid-conformance rule, not D-07's mode-interval
proportion -- see Tune below) all ride in evidence under `-v`. A boolean
also records whether this measured rate agrees with
`video.frame_rate.declared`'s own value within tolerance -- doc 03's own
"declared-vs-measured internal mismatch" signal, visible even when
comparing a file against itself.

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

**D-05 of Phase 5, amending D-07 of Phase 4:** this check's measurement
basis changed from the mode interval to the span. If a value you have
snapshotted moved after upgrading past this amendment, the move is a
measurement-basis correction, not a gate change -- the tolerance, severity
and unit are all unchanged. The class this amendment fixes: coarse-timebase
files (most commonly a stream-copy remux to a container with a coarser
timebase than the source, such as MP4 to Matroska) whose mode interval read
a different rate than the file's true average cadence. Re-run
`mediadiff snapshot` on the affected candidate to adopt the corrected
value as the new baseline; this is not a tolerance widening, and none was
applied.

### Silence

Set `video.frame_rate.measured` to `ignore` in `[severity]` for a pipeline
where measured cadence is expected to vary and is not itself a meaningful
signal. A silenced check's difference is still computed and shown under
`-v`.
