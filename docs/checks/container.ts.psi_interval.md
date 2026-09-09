# container.ts.psi_interval

## What it measures

The MAXIMUM of two repetition intervals, expressed in milliseconds: the
Program Association Table (PAT)'s own section repetition interval (shared
across every program, since one PAT covers the whole transport stream) and
this program's own Program Map Table (PMT) section repetition interval.
One measurement is emitted per program, at `Scope{Kind::program, index}`
where `index` is the PSI `program_number` (CONT-08, same rule as
`container.ts.pcr_interval`). Both individual maxima (`pat_max_ms`,
`pmt_max_ms`) ride in evidence so a reader can tell which table was slow. A
program whose PAT AND PMT both had fewer than two observed occurrences
skips as `insufficient_data`.

Like `container.ts.pcr_interval`, this value is derived from `ts_scan`'s
mux-rate ESTIMATE, not measured against wall-clock time, and every
measurement carries `Measurement::estimated = true`.

## Why it matters

A downstream receiver (a set-top box, a channel-change scenario, a
mid-stream tune-in) relies on PAT/PMT repeating often enough to acquire the
stream's structure quickly -- a slow repetition interval means a longer
black screen or a failed tune-in. `warn` severity and a looser 500ms
default bound (versus PCR's 100ms) reflect that PSI table timing is looser
spec guidance than PCR accuracy, but still a real quality-of-service
signal. As with `pcr_interval`, the `estimated` marker and its 3x tolerance
widening (D-03) prevent estimation noise in the underlying mux-rate
estimate from fabricating a false regression.

## Accept / Tune / Silence

### Accept

If PSI table repetition cadence intentionally changed (a different muxer
configuration, a different `-pat_period`/`-sdt_period`-equivalent setting),
re-run `mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

The baseline tolerance (`500ms`, warn severity) is compared against the
directly-measured value; because this value is `estimated`, the effective
comparison tolerance is automatically widened 3x (to 1500ms) at comparison
time. Tightening `--tol container.ts.psi_interval=Xms` tightens the
pre-widening value.

### Silence

Set `container.ts.psi_interval` to `ignore` in `[severity]` only for a
pipeline where PSI repetition timing does not matter (e.g. a file-based
workflow with no live tune-in scenario). Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
