# timeline.discontinuities

## What it measures

The spans of a stream's own presentation timeline where a jump between two
consecutive presentation timestamps (walked in PRESENTATION order -- sorted
by PTS, never read/`av_read_frame` order) **strictly exceeds** a fixed 250 ms
threshold and is **NOT** explained by container structure.

On an MPEG-TS input, a jump IS explained by container structure -- and is
reported instead by the separate `timeline.discontinuities.flagged` check,
`info` severity, never here -- when the transport packet carrying
`discontinuity_indicator=1` falls within the demuxed packet's own byte
range at the jump. Every other jump on a TS input, and every jump on any
other container, lands here, gating.

Each recorded span's `start`/`end` are the two bracketing presentation
timestamps, expressed as exact `RationalValue` in ms (never a pre-divided
float); the 250 ms threshold comparison itself is done by
cross-multiplication against the stream's own timebase, never a division.
The `span` semantic compares span lists across files: a span present in the
candidate that does not overlap any baseline span is **introduced** and
gates on this check's own severity; a baseline span absent from the
candidate is **removed** and is always reported at `info`, never gating.

On an MPEG-TS input, both the interval walk and the attribution join run
over the **unwrapped** presentation timeline (doc 04 section 1.2's 33-bit
unwrap, `unwrap_ts_timestamps`) -- a stream that genuinely wraps mid-file
produces zero spans attributable to the wrap itself.

This check runs on every stream carrying timestamps **except** subtitle
streams (05-CHECK-ROSTER.md's own scope decision, mirroring
`timeline.gaps`).

When the packet scan was truncated (`partial_scan`), or -- on an MPEG-TS
input -- when the TS scan itself did not complete, this check skips rather
than reporting a span list computed from unreliable data: an incomplete TS
walk means the `discontinuity_indicator` offset list used for attribution
is itself unreliable, not merely absent. When the shared interval/threshold
arithmetic overflows anywhere (`insufficient_data`), or -- TS-scoped -- when
the recorded `discontinuity_indicator` offset list for this stream's PID
was itself truncated (a crafted stream setting the flag on every packet),
this check also skips: a classification built on a partial flag list could
silently demote real breakage to `info`, which would be a false negative
worse than an honest "cannot determine."

## Why it matters

A presentation-time jump that isn't explained by any structural signal
usually means a dropped segment, a corrupted splice, or a demuxer that
silently lost packets -- exactly the class of defect a media-aware diff gate
exists to catch. A jump the source container explicitly flagged as
intentional (an MPEG-TS splice point) is a different fact entirely, and is
reported separately at `info` by `timeline.discontinuities.flagged` so it
never gates the merge on its own.

## Accept / Tune / Silence

### Accept

If the introduced jump was an intentional edit (a deliberate content cut,
splice, or re-anchor), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The 250 ms threshold is a fixed, named DETECTION constant, not a
tolerance -- `--tol timeline.discontinuities=...` has no effect on which
intervals get recorded as spans in the first place. `--tol` (and severity
overrides) still govern whether an introduced span gates the exit code. The
way to accept a known structural break going forward is to re-snapshot the
new baseline, or to silence the check for a pipeline where this signal is
not meaningful.

### Silence

Set `timeline.discontinuities` to `ignore` in `[severity]` for a pipeline
where timeline continuity is deliberately not a meaningful signal (e.g. a
capture path known to carry harmless, expected drop-outs). A silenced
check's difference is still computed and shown under `-v`.
