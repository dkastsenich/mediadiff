# container.ts.null_ratio

## What it measures

The proportion of null (stuffing) packets -- PID `0x1FFF` -- to total
packets in the transport stream, kept as an EXACT `num`/`den` ratio (never
pre-divided into a rounded percentage; the `tol` comparator's relative
branch already cross-multiplies, so a rounded value would throw away
precision the exact ratio has for free). Global scope: this is a
whole-transport-stream property, not per-program. A zero-packet scan skips
as `insufficient_data` rather than constructing a zero-denominator value.

## Why it matters

Null packets are padding a constant-bitrate (CBR) mux inserts to hold a
declared bitrate steady -- the ratio is a direct tell for how efficiently
the mux is using its declared rate. A shift in null ratio without a
corresponding change in actual content usually means a rate-control
configuration change (a different target bitrate, a different multiplexer,
a different VBR-vs-CBR posture) rather than a change to the media itself,
but it is still worth surfacing at `info` severity as a signal a reviewer
may want to investigate.

## Accept / Tune / Silence

### Accept

If the mux rate/rate-control configuration intentionally changed, re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

The baseline tolerance (`5%`, relative, info severity) accepts routine
rate-control jitter between two otherwise-identical muxes. Widen it with
`--tol container.ts.null_ratio=X%` for a pipeline with more inherent
rate-control variance; tighten it for a pipeline whose mux rate is expected
to be highly stable run to run.

### Silence

`container.ts.null_ratio` is `info` severity by default and does not gate
the exit code. Set it to `ignore` in `[severity]` only if this signal is
not useful diagnostic information for a given pipeline. A silenced check's
difference is still computed and shown under `-v`.
