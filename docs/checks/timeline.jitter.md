# timeline.jitter

## What it measures

The standard deviation ("sigma") of a constant-frame-rate stream's own
interval distribution around its mode interval, as an exact fixed-point
value in milliseconds -- never a rounded real, and never computed via a
floating-point square root anywhere in the compared-value path. Sigma is
computed by a portable integer bit-doubling square root over the sum of
squared deviations (scaled by a fixed power-of-two factor before the root
is taken, so the result carries sub-tick precision without ever narrowing
to an approximate value first); the same operation produces a
byte-identical result on every supported toolchain (MSVC, GCC, Clang,
AppleClang).

The maximum absolute deviation from the ideal interval, in milliseconds,
rides in this check's own evidence (`max_abs_deviation_ms`) -- never a
second check id. 05-CHECK-ROSTER.md's own Discretion resolution is
explicit: sigma is the ONE compared value a jittery stream produces;
doubling it into a separate "max deviation" check would double-report the
same underlying jitter as two findings.

This check consumes `derive_cadence` (the one shared cadence primitive
every `timeline.*` cadence check reads) exactly once per stream, together
with `timeline.vfr_profile`. When that shared classification reports the
stream as **variable frame rate**, this check reports
`skipped:vfr` -- a sigma computed around a nominal interval that does not
exist for a genuinely variable-rate stream is not a measurement, it is
noise; `timeline.vfr_profile` is the check that describes a VFR stream's
own interval distribution instead.

This check runs on every stream carrying timestamps **except** subtitle
streams (mirrors `timeline.dts_monotonic`/`timeline.gaps`'s own scope
decision).

### Quantization rule (UD-2)

Sigma is measured from the stream's own EXACT ideal interval
(`ideal_interval_num`/`ideal_interval_den`, the same unreduced
span/count rational `timeline.vfr_profile` bins against) rather than the
timebase-bound mode interval. Per interval, with `Q = interval *
ideal_den - ideal_num`: a deviation STRICTLY BELOW one tick of the
stream's own timebase (`|Q| < ideal_den`) is representational rounding,
not real jitter -- it contributes EXACTLY ZERO to sigma, and is tallied
in the `sub_tick_intervals` evidence key. Every other deviation enters at
its FULL magnitude (never re-zeroed or truncated once at or past one
tick), formed as an exact fixed-point value (scale
`2^kJitterSigmaFixedShift`, `core/rational.h`) via an EXACT integer
division for the whole-tick part and a round-half-to-even integer
division for the sub-tick remainder -- integer/rational arithmetic only,
never a floating-point divide. For a stream whose ideal interval equals
its mode interval (the common case for any frame rate that divides
evenly into its container's timebase), this reduces to the
pre-quantization mode-referenced computation exactly, and the reported
sigma is unchanged.

### Contract impact

This is a **published contract change**, stated here because the release
has not shipped: sigma's REFERENCE moved, not just its value on a few
edge-case files.

- Sigma is now measured from the exact ideal interval, not the mode
  interval, with sub-tick deviations zeroed.
- For a CFR stream whose ideal interval equals its mode interval, the
  reported value is unchanged.
- Four new evidence keys: `deviation_reference` (always `"ideal"`),
  `ideal_interval_num`, `ideal_interval_den`, and `sub_tick_intervals`
  (the count of intervals zeroed as representational rounding).
- `max_abs_deviation_ms` keeps its key, now computed from the zeroed,
  fixed-point deviations rather than a bare tick count.
- Snapshots taken before this rule shipped compare against new ones with
  sigma's meaning changed. Reference snapshots carrying `timeline.jitter`
  measurements must be re-taken.

Before this rule, a lossless MP4-to-Matroska stream copy of NTSC content
(WINDOWS #28) reported `timeline.jitter` as `warn` on the video stream:
Matroska's mandated 1 ms timebase could not represent the 1001/30000s
period exactly, so every interval deviated from the (mode-referenced)
nominal by up to a tick, inflating sigma past both thresholds for content
with no real timing irregularity. Under this rule, that sub-tick residual
contributes zero, and the pair compares clean -- the fix is the deviation
reference and zeroing rule itself, never a widened tolerance
(`FALSE POSITIVES ARE P0`).

## Why it matters

A constant-frame-rate stream is supposed to emit its frames on an exactly
regular grid. A nonzero sigma on a CFR stream is the signature of a
misbehaving encoder, a lossy remux path re-timestamping frames, or a
capture pipeline with clock instability -- none of which necessarily
changes the DECLARED frame rate (`video.frame_rate.declared`) or even the
measured average rate (`video.frame_rate.measured`), since both of those
can average out to the expected value even while individual frames land
off-grid. Sigma is the signal that survives averaging.

## Accept / Tune / Silence

### Accept

If the jitter increase was an intentional pipeline change (a switch to a
capture or transcode path with known, acceptable timing tolerance),
re-run `mediadiff snapshot` on the new candidate to establish it as the
new baseline.

### Tune

The default `0.5ms,2ms` two-threshold tolerance (`--tol
timeline.jitter=<warn>,<fail>`) bounds how much sigma may grow between
baseline and candidate before warning, then failing. Widen it for a
pipeline whose transcode path is known to introduce more timing noise
than the default assumes; tighten it for a bitexact pipeline where any
jitter growth at all is suspicious.

### Silence

Set `timeline.jitter` to `ignore` in `[severity]` for a pipeline with no
dependency on sub-frame timing regularity (e.g. one that already gates on
`video.frame_rate.measured` and treats finer-grained jitter as noise).
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
