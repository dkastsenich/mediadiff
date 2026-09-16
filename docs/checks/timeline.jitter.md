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

The maximum absolute deviation from the mode interval, in milliseconds,
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
