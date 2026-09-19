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

### Declared durations on a wrapping MPEG-TS file

This project reads MPEG-TS with libavformat's own overflow correction
turned off (`AVFormatContext::correct_ts_overflow = 0`), so this project's
own doc 04 section 1.2 unwrap sees a real 33-bit PTS/DTS wrap rather than
having it silently pre-corrected upstream. That choice has a side effect:
libavformat's own duration estimation -- exactly the **container-declared**
and **stream-declared** members above, never the **computed** member --
is corrupted on a genuinely-wrapping file with correction turned off.

When a file genuinely wraps, this project recovers the two declared
members from a SECOND, overflow-corrected open of the same bytes (never a
second read of the primary session's own packets, and never that second
open's own `start_time`, which sits on libavformat's own shifted epoch).
Evidence carries `declared_duration_source: overflow_corrected_reprobe`
in that case.

If that second open fails, times out, or disagrees with the primary
session's own stream layout, both declared members are **withheld**
(reported in `absent_members`, never compared as corrupt values) with
`declared_duration_source: withheld_wrap_uncorrectable` -- a withheld
member is always preferable to a compared-corrupt one.

Every other file -- every non-wrapping file, and every non-MPEG-TS file --
reports `declared_duration_source: demuxer`, its own primary session's
values, completely unmodified.

**Contract note:** `declared_duration_source` is a new evidence key on
this check (and on `timeline.duration.coherence` below). `--json` and
snapshot consumers see it on every file, with one of these three exact
string values.

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
