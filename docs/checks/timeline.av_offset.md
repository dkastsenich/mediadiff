# timeline.av_offset

## What it measures

The signed offset between the first audible sample and the first visible
frame of the file's primary video stream, in milliseconds -- **positive
means audio is late**, negative means audio leads video. One measurement
is produced per audio stream, scoped to that stream; the primary video
stream is the first video-scoped stream, by container order.

This check computes offset on **two bases**, both recorded in evidence:

- `raw_offset_ms` -- the first audio packet's own presentation time minus
  the video stream's first presentation time, computed WITHOUT any audio
  priming adjustment.
- `adjusted_offset_ms` -- the same computation, but with the audio side's
  known decoder priming (the samples a decoder is told to discard at the
  start of the stream, e.g. AAC's typical 1024-sample encoder delay) added
  back in. When priming is unknown for a given side, `adjusted_offset_ms`
  equals `raw_offset_ms` exactly -- no adjustment is applied when none can
  be determined.

Priming is read from what the container's demuxer already exposes,
without decoding: the first audio packet's own `AV_PKT_DATA_SKIP_SAMPLES`
side data is checked first, falling back to the stream's declared
`initial_padding` only when no packet-level signal was captured. This
ordering matters -- some containers (MP4 is the common case) signal
priming ONLY at the packet level, never via the stream-level field.

**The priming sample count is converted into the audio stream's own
timebase through its sample rate before it is added to `raw_offset_ms`.**
Priming is measured in SAMPLES (e.g. AAC's typical 1024-sample encoder
delay), while every timestamp this check compares is in the stream's own
native TICKS -- a sample count is only directly usable as a tick count
when the stream's timebase happens to equal its sample rate (true for
MP4-muxed AAC, where the timebase IS the sample rate). For any other
timebase (Matroska's mandated 1ms timebase is the common case), the
sample count is converted: `ticks = samples * tb.den / (sample_rate *
tb.num)`, rounded to nearest with ties away from zero -- the same
rounding `av_rescale_q` applies by default. A priming-known stream
whose sample rate is unavailable, or whose conversion overflows, compares
on the **raw basis** instead of adjusting by an unconverted count --
`priming.rescale` in evidence names which of `ok` / `no_sample_rate` /
`overflow` occurred, and `priming.sample_rate` / `priming.priming_ticks`
record the rate used and the converted tick count (both `null` when
priming itself is unknown, since there is nothing to convert).

**The comparison basis is chosen per pair, not per file.** When comparing
two files, this check uses `adjusted_offset_ms` only when BOTH sides both
know their own priming AND successfully converted it to ticks
(`priming.rescale` `ok`); if either side's priming is `unknown`, or a
priming-known side's conversion could not be performed, the comparison
falls back to `raw_offset_ms` on BOTH sides. This is why comparing a
known-priming file against an unknown-priming file (for example, an MP4
against its own MPEG-TS remux, where the remux typically loses the
priming signal entirely) still produces a meaningful number instead of
comparing an adjusted value against an unadjusted one. `priming: {state,
source, samples}` and `comparison_basis` are always present in evidence so
a reader can see exactly which basis was used and why.

A file with no audio stream, or no video stream, reports
`skipped:insufficient_data` -- never silence.

### MPEG-TS wraparound

On MPEG-TS, both streams' first presented timestamps are taken over
33-bit-unwrapped, epoch-aligned timestamps (doc 04 section 1.2's unwrap
rule, `TimelinePacketView`) before this check ever reads them -- a raw
90kHz PTS wraps every ~26.5 hours, and comparing un-unwrapped values
across a wrap would fabricate a spurious multi-hour offset with no real
sync change present. A file whose 33-bit wrap falls between its streams'
first packets therefore reports its true offset, unaffected by where the
wrap happened to land. A view that cannot be unwrapped without an int64
overflow reports `skipped:insufficient_data` for that stream's
`timeline.av_offset`/`timeline.av_drift`/`timeline.av_drift.pattern` --
never a wrapped or fabricated value.

## Why it matters

A/V sync drift or offset is one of the most user-visible defects a media
pipeline can introduce -- even a consistent 40ms lead or lag is
perceptible, and anything beyond about 100ms reads as obviously broken.
An encoder or remux tool that mishandles container-level audio delay (an
edit list, a codec delay field, a priming sample count) silently shifts
every audio sample relative to video without touching either stream's own
internal timing -- exactly the class of regression `timeline.av_offset`
exists to catch.

## Accept / Tune / Silence

### Accept

If the offset change was an intentional correction (fixing a previously
mis-synced source, or applying a deliberate sync compensation for a known
capture-chain latency), re-run `mediadiff snapshot` on the corrected file
to establish it as the new baseline.

### Tune

The default `5ms,20ms` two-threshold tolerance (`--tol
timeline.av_offset=<warn>,<fail>`) bounds how far the offset may move
between baseline and candidate before warning, then failing. **This
tolerance is never widened or the severity never softened just because
`priming: unknown` appears in evidence** -- an unknown-priming comparison
still runs raw-to-raw at the check's full registered severity (D-11): a
genuine 200ms sync break on an MPEG-TS file, where priming is essentially
never signalled, must still block the merge. If a pipeline routinely
produces comparisons where one side's priming is structurally always
unknown (e.g. it always compares against MPEG-TS candidates), account for
the raw-to-raw basis when setting `--tol`, rather than assuming the
adjusted basis is in effect.

### Silence

Set `timeline.av_offset` to `ignore` in `[severity]` for a pipeline with
no dependency on A/V sync at file-start (e.g. a pipeline that only cares
about sync drift over time, which `timeline.av_drift` covers separately).
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
