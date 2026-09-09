# container.mkv.codec_delay

## What it measures

Per track, the `CodecDelay` element from that track's `TrackEntry` --
Matroska's own encoder-priming mechanism, converted from the wire's
nanosecond value into SAMPLES using that same track's own `SamplingFrequency`
(`samples = delay_ns * rate_hz / 1_000_000_000`, computed with checked
integer arithmetic, never a floating divide). A track with no `CodecDelay`
element at all emits no measurement for this check (absent is not the same
as an explicit zero); a track whose sampling frequency cannot be determined
skips as `insufficient_data` rather than dividing by a guessed rate.
`SeekPreRoll` (also nanoseconds) rides alongside in evidence, unconverted.

## Why it matters

`CodecDelay` is how Matroska records an encoder's algorithmic priming delay
(most visibly for Opus, whose encoder always introduces some amount of
lookahead before the first "real" sample) -- the number of samples a
decoder must discard from the start of the decoded stream to align it with
the original, un-delayed source. A change here (a different encoder
version, a different `application` mode, a different lookahead setting)
shifts every sample's true presentation time relative to any other track in
the file without changing a single encoded sample byte -- this is
foundational input to `audio.priming` (a later phase's check), and a silent
change here would misalign A/V sync in exactly the way this project exists
to catch.

## Accept / Tune / Silence

### Accept

If the encoder change was intentional (e.g. a deliberate upgrade to a newer
libopus with a different default lookahead), re-run `mediadiff snapshot` on
the new candidate to establish it as the new baseline.

### Tune

The baseline tolerance (`1samples`) is intentionally tight -- a codec's
priming delay is normally a fixed, deterministic property of the encoder
and its configuration, not something that should drift run to run.
Widening it further is rarely correct; prefer re-baselining after a
confirmed, intentional encoder change instead.

### Silence

Set `container.mkv.codec_delay` to `ignore` in `[severity]` only for a
pipeline that re-derives audio priming from the decoded stream itself
rather than trusting the container's own declared delay. Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
