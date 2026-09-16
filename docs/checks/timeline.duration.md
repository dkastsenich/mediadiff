# timeline.duration

## What it measures

A per-stream **triple** (doc 04 section 1.3): three independently-absent
sources of "how long is this stream", never substituted for one another.

- **container-declared** -- `AVFormatContext->duration`, in AV_TIME_BASE
  (microsecond) units.
- **stream-declared** -- `AVStream->duration`, in that stream's own native
  timebase.
- **computed** -- the last frame's presentation end (`pts + duration`, with
  a missing packet duration RECONSTRUCTED per doc 04 section 1.3: the delta
  to the next PTS in presentation order, or -- for the very last frame --
  the shared cadence derivation's own mode interval) minus the stream's own
  first presentation PTS.

**The compared value is the computed member only.** The other two, plus a
`duration_source` field (`declared` when every packet's own duration was
usable as-is, `reconstructed` when at least one needed substitution) and an
`absent_members` list, ride in evidence. A container without an
`AVFormatContext->duration` (a common case for an unfinalized or streamed
mux), or a stream without an `AVStream->duration`, reports that member
**absent** in evidence -- never a fabricated zero, never silently
substituted from one of the other two members. This is deliberate: a
silent substitution would make `timeline.duration.coherence` structurally
unable to fire on the exact files where the triple actually disagrees.

Every value is a millisecond count via checked integer multiplication and
division (`ms = ticks * 1000 * tb.num / tb.den`, truncated toward zero,
matching `timeline.start`'s identical conversion) -- never a
floating-point type anywhere in the compared value. Reconstruction and
frame-end arithmetic both go through checked add/subtract; an overflow
anywhere degrades the measurement to `insufficient_data` rather than
reporting a wrapped or fabricated duration.

## Why it matters

A duration that silently shrinks or grows -- independent of what the
container's own claimed value says -- usually means a dropped tail, a
truncated encode, or a genuine change to the media's own content length.
Because the triple keeps all three sources visible, a change that is
purely in the container's OWN claimed duration (with the actual media
content unchanged) is distinguishable in evidence from a change to the
media itself.

## Accept / Tune / Silence

### Accept

If the duration change was intentional (a deliberate trim, a genuinely
shorter re-encode), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The default tolerance is `20ms` warn / `40ms` fail. **Deviation from doc
04's literal "±1 frame":** 40ms is exactly one frame at 25fps, chosen as a
FIXED named constant rather than a frame-rate-derived threshold -- a
frame-derived threshold would gate the identical delta differently on two
files that differ only in frame rate, which this project's D-08 rules out
(a detection parameter that changes the measured value needs fingerprint
recording and mismatch-skip machinery deliberately deferred past v1).
Widen via `--tol timeline.duration=100ms` or a `[check.tolerance]` entry
for a pipeline with a known source of legitimate small duration drift.

### Silence

Set `timeline.duration` to `ignore` in `[severity]` for a pipeline where
duration is expected to vary and is not itself a meaningful signal. A
silenced check's difference is still computed and shown under `-v`.
