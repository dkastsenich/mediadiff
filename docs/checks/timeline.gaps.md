# timeline.gaps

## What it measures

The spans of a stream's own presentation timeline where the interval between
two consecutive presentation timestamps is larger than it should be. For
each pair of consecutive timestamps (walked in PRESENTATION order -- sorted
by PTS, never read/`av_read_frame` order), a span is recorded when the
interval **strictly exceeds**
`max(2 x nominal, declared_duration + 1 tick)`, where:

- `nominal` is the stream's own mode interval, taken from the ONE shared
  `derive_cadence` primitive (`src/probe/cadence.h`) -- never a second,
  independently computed interval statistic. `2` is a fixed named constant
  (`kGapNominalMultiplier`), never configurable in v1.
- `declared_duration` is the PRECEDING packet's own duration -- its declared
  `PacketRecord::duration` when usable, or the reconstructed value doc 04
  section 1.3 prescribes (the delta to the next presentation PTS, or the
  shared cadence's mode interval for the last packet in presentation order)
  -- computed once by `detail::reconstruct_packet_durations`
  (`src/analyzers/timeline/analyzers.h`), never re-derived here. `1` tick is
  a fixed named constant (`kGapDeclaredDurationSlackTicks`).

Both constants are fixed for the identical reason `timeline.gaps`' own
`derive_cadence` epsilon is fixed (D-08): a detection parameter changes the
MEASURED value, unlike a tolerance, which only changes the verdict.

Each recorded span's `start`/`end` are the two bracketing presentation
timestamps, expressed as exact `RationalValue` in ms (never a pre-divided
float). The `span` semantic compares span lists across files: a span
present in the candidate that does not overlap any baseline span is
**introduced** and gates on this check's own severity; a baseline span
absent from the candidate is **removed** and is always reported at `info`,
never gating.

On an MPEG-TS input, both the interval walk and the duration reconstruction
run over the **unwrapped** presentation timeline (doc 04 section 1.2's
33-bit unwrap, `unwrap_ts_timestamps`) -- a stream that genuinely wraps
mid-file produces zero spans attributable to the wrap itself.

This check runs on every stream carrying timestamps **except** subtitle
streams (05-CHECK-ROSTER.md's own scope decision) -- a subtitle track's
intentionally sparse presentation timing would false-positive under a rule
tuned for continuously-sampled audio/video content.

When the packet scan was truncated (`partial_scan`), this check skips
rather than reporting a span list computed from an incomplete sweep -- a
gap list from a truncated scan is silently MISSING exactly the gaps that
would have appeared at the truncation point, the opposite of an honest
answer. When the shared cadence derivation reports anything other than
`ok` (`insufficient_data`), this check skips too -- never a fallback
nominal.

## Why it matters

A hole in a stream's own timeline -- a span where no content was presented
for far longer than the stream's own normal cadence -- usually means a
dropped segment, a corrupted splice, or a demuxer that silently lost
packets. A media pipeline that introduces such a hole between a baseline
and a candidate is exactly the class of defect a media-aware diff gate
exists to catch; a hole that was ALREADY present in the baseline (and
merely persists) is informational, not a new regression.

## Accept / Tune / Silence

### Accept

If the introduced gap was an intentional edit (a deliberate content cut or
splice point), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The `2x`-nominal multiplier and the one-tick declared-duration slack are
fixed, named DETECTION constants, not a tolerance -- `--tol
timeline.gaps=...` has no effect on which intervals get recorded as spans
in the first place. `--tol` (and severity overrides) still govern whether
an introduced span gates the exit code.

### Silence

Set `timeline.gaps` to `ignore` in `[severity]` for a pipeline where
timeline continuity is deliberately not a meaningful signal (e.g. a
capture path known to carry harmless, expected drop-outs). A silenced
check's difference is still computed and shown under `-v`.
