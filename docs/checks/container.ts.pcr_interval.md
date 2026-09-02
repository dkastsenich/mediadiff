# container.ts.pcr_interval

## What it measures

The MAXIMUM spacing between consecutive Program Clock Reference (PCR)
samples on one program's own `PCR_PID`, expressed in milliseconds. One
measurement is emitted per program, at `Scope{Kind::program, index}` where
`index` is the PSI `program_number` read from the Program Association
Table -- never an array position (CONT-08), so two files that declare the
same programs in a different order still pair correctly. The mean spacing
also rides in evidence (doc 02 section 5's "also records mean"). A program
with fewer than two PCRs skips as `insufficient_data` -- never a zero,
never a fabricated interval.

The millisecond value is derived from `ts_scan`'s mux-rate ESTIMATE (an
exact rational built from a byte-offset/PCR-tick delta), not measured
directly against a real clock. Every measurement this check emits carries
`Measurement::estimated = true`.

## Why it matters

PCR spacing beyond spec bounds is what makes a decoder's clock recovery
(PLL) drift or stall -- the classic symptom is audio/video sync creeping or
a hiccup at playback. Because this value is derived from an ESTIMATE rather
than measured against wall-clock time, two files whose estimates differ
only by measurement noise (not a real spacing regression) must not
fail -- that is exactly what `estimated` and its 3x tolerance widening
(D-03) exist to prevent. False positives are P0 in this project: a check
that cries wolf on estimation noise gets muted, and a muted gate is worth
nothing.

## Accept / Tune / Silence

### Accept

If the encoder/muxer's actual PCR insertion cadence intentionally changed,
re-run `mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

The baseline tolerance (`100ms`, fail severity, spec-derived default) is
what a directly-measured PCR interval is compared against. Because this
value is `estimated`, the comparator automatically widens the EFFECTIVE
tolerance by 3x (to 300ms) before comparing -- tightening `--tol
container.ts.pcr_interval=Xms` tightens the PRE-widening value, and the
widened bound still applies on top of whatever you set. Widening this
tolerance further should be rare; prefer investigating a real regression
before loosening the gate that catches it.

### Silence

Set `container.ts.pcr_interval` to `ignore` in `[severity]` only for a
pipeline that does not care about decoder clock-recovery stability (e.g. a
transport stream that is never played live/streamed, only stored). Leave
it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
