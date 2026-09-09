# size.stream_bitrate

## What it measures

Per stream, at that stream's own scope: `byte_total * 8 / dts_span_seconds`
-- the stream's own average bitrate over its recorded DTS span, kept as an
exact `RationalValue` (never pre-divided into a rounded number). The span
is the stream's own first-to-last VALID (non-`AV_NOPTS_VALUE`) DTS; a
stream whose packets ALL lack a DTS skips `skipped:no_timing_data`.

The duration used here is the stream's own DTS span, derived directly from
`PacketScan`'s shared per-stream array -- doc 06 names this quantity as
"doc 04's computed duration", which does not exist until Phase 4's
timeline math lands. A future change to that shared duration definition is
therefore an intentional migration for this check, not a silent drift; if
Phase 4 ever changes what "duration" means, this check's own span
derivation is the place to revisit.

Never `estimated` (D-03): this is a value directly measured from the
packet sweep, not derived from any secondary estimate the way
`container.ts.pcr_interval`'s mux-rate-derived milliseconds are.

## Why it matters

A stream's average bitrate is the primary lever most encoding pipelines
tune directly -- an unexpected change here usually means an encoder
setting, a rate-control mode, or a source content change propagated
further than intended.

## Accept / Tune / Silence

### Accept

If the bitrate change was intentional (a deliberate encoder rate-control
change), re-run `mediadiff snapshot` on the new candidate to establish it
as the new baseline.

### Tune

The default `±3%,10%` tolerance (warn at 3%, fail at 10%) matches ordinary
encoder-to-encoder rate variation. Adjust via `[check.tolerance]` in
`mediadiff.toml` (or `--tol size.stream_bitrate=<value>`) for a pipeline
whose target bitrate variance is tighter or looser than that default.

### Silence

Set `size.stream_bitrate` to `ignore` in `[severity]` for a pipeline where
average bitrate is expected to vary widely and is monitored separately
(e.g. via an external rate-control dashboard). Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
