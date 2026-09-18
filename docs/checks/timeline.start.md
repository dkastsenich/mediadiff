# timeline.start

## What it measures

The file's timeline origin, split into two kinds of measurement (D-03).
**One** `global`-scoped measurement holds the file's earliest presentation
time across every timestamped stream, taken as the minimum first
presentation PTS -- compared via exact integer cross-multiplication, never
rescaled to a shared timebase by division. **One** per-stream measurement
(scoped to whichever stream carries it -- video, audio, subtitle, or data)
holds that stream's own first presentation PTS **minus** the global origin,
never a per-stream absolute PTS.

This split is what makes a whole-file shift report as ONE finding instead
of one per stream: stream-copying an MP4 to MPEG-TS shifts every stream's
absolute PTS by the TS muxer's own default mux delay (about 1.4 seconds),
but every stream's PTS moves by the *same* amount -- so the per-stream
*relative* values (each stream's distance from the file's own origin) stay
unchanged, and only the `global` measurement moves. A single stream moving
relative to the others (a real sync problem) still reports on that one
stream.

Every value is a millisecond count derived from native PTS ticks and the
stream's own timebase via checked integer multiplication and division
(`ms = ticks * 1000 * tb.num / tb.den`, the same conversion
`container.ts.pcr_interval`/`psi_interval` already use) -- truncated
toward zero, never a floating-point type anywhere in the compared value.
An unreduced exact fraction was tried during this check's own development
and rejected: a real file's native timebase denominators make an
unreduced fraction's `num`/`den` grow large enough, after even one
cross-timebase comparison, to overflow the compare engine's own delta
cross-multiplication on perfectly ordinary files.

The measurement is taken on the presentation (PTS) axis only, never DTS
(decode order is not a presentation-time concept). A stream whose packets
all carry the `AV_NOPTS_VALUE` sentinel skips `no_timing_data` -- the
sentinel is never coerced to 0, which would fabricate a start time at the
origin. A truncated packet scan (`partial_scan`) skips every scope,
including `global`, since a partial sweep can no longer prove which
stream's PTS is genuinely earliest.

### MPEG-TS: 33-bit unwrap and the cross-stream epoch rule

On MPEG-TS, both the per-stream starts and the `global` origin are taken
over 33-bit-unwrapped timestamps (doc 04 section 1.2), never the raw,
possibly-wrapped PTS a genuinely long-running or offset-shifted transport
stream can carry. A stream whose own first raw PTS sits more than 2^32
ticks below another stream's first raw PTS is placed one 2^33 epoch later
before the file's `global` origin is computed, so a wrap that falls
between two streams' own first packets still yields the correct origin
instead of a spurious multi-hour offset. An absolute origin genuinely moved
by a muxer offset (for example `-output_ts_offset`) is a real `global`-scope
finding, exactly as the whole-file-shift case above describes -- while the
per-stream *relative* starts stay comparable, because the unwrap and epoch
rule apply identically to both files being compared.

## Why it matters

An unexpected change in where a file's content actually starts --
independent of what the container's duration or edit list merely claims --
usually means a muxer delay, a dropped lead-in, or a genuine A/V sync
regression upstream of this tool.

## Accept / Tune / Silence

### Accept

If the origin shift was intentional (a deliberate re-mux to a container
with a different muxer delay convention, or a deliberate trim), re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

The default tolerance is `5ms` warn / `20ms` fail -- wide enough to absorb
ordinary encoder/muxer rounding while still catching a real shift.
`strict_bitexact`/`remux` tighten this to `1ms`: a byte-exact re-encode or
a stream-copy remux is expected to move the origin by nothing at all, so
even a small drift there is worth surfacing. Widen via
`--tol timeline.start=10ms` or a `[check.tolerance]` entry for a pipeline
with a known source of legitimate small origin jitter.

### Silence

Set `timeline.start` to `ignore` in `[severity]` for a pipeline where the
file's start offset is expected to vary and is not itself a meaningful
signal. A silenced check's difference is still computed and shown under
`-v`.
