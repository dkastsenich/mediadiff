# audio.silence.edges

## What it measures

Introduced leading or trailing silence, detected inside the SAME shared audio decode sweep
`content.audio.sample_hash` and `audio.loudness.*` already consume -- no second decode
(AUDIO-10). A per-sample-frame PEAK (the maximum absolute amplitude across every channel of
that sample instant) is compared against a fixed **-60 dBFS** threshold. The transition is
debounced by a fixed **5ms hysteresis** on BOTH the open and close side -- a plain, continuous
tone's own mathematically-exact-zero first sample (or an interior zero-crossing landing within
one output sample of the true crossing) never registers as a spurious one-sample "silence" run,
because a run only commits once the peak has stayed on one side of the threshold for a full 5ms.

Only two runs are ever reported by this check:

- The **leading** run, if it starts at the very first sample.
- The **trailing** run, if it is still below threshold when the stream ends (closed at that
  point, never dropped for lack of a following transition).

An interior peak-silent run -- one that neither touches the first sample nor reaches the last --
is discarded here entirely; it is `audio.silence.dropouts`' own territory (see that check's own
independent RMS-window criteria), never forwarded between the two.

Each recorded span's `start`/`end` are exact `RationalValue`s in ms, derived from the sample
index and the stream's own sample rate through checked rational arithmetic (`src/core/
rational.h`) -- never a pre-divided float. Two spans that touch exactly (`end == start` of the
next) are merged into one, mirroring `timeline.discontinuities`' own merge convention. A stream
with no detected silence reports an **empty span list** as a real measured value -- never
`Absent{}` and never a skip: an empty list and an unmeasured stream stay distinguishable.

When the shared decode sweep never ran for this stream (`--no-content`, or a codec this build's
linked FFmpeg cannot open at all), this check skips `requires_decode`. When the sweep ran but
produced zero decoded samples, or a native sample format this detector cannot interpret, it
skips `insufficient_data` -- never a fabricated empty list. When the sweep stopped early (a decode
sweep limit hit before this stream's own end), it skips `partial_scan` with evidence `reason`
naming the stop token -- see the "Decode stop reasons" table in `content.audio.sample_hash.md` --
never a span list computed from only the part of the stream that was measured.

## Why it matters

Introduced leading silence is a classic sync or gapless-playback bug -- a track that used to
start (or end) with content now starts (or ends) with dead air, which loudness averages away
entirely and no container-level parameter check can see. This is exactly the class of silent,
media-specific regression a diff gate exists to catch.

## Accept / Tune / Silence

### Accept

If the introduced leading/trailing silence was an intentional edit (a deliberate fade, trim, or
gapless-alignment change), re-run `mediadiff snapshot` on the new candidate to establish it as
the new baseline.

### Tune

The -60 dBFS threshold and the 5ms hysteresis are fixed, named DETECTION constants
(`kEdgeSilenceThresholdDbfs`, `kEdgeSilenceHysteresisMs`), never a configurable knob in v1 --
a detection parameter changes the MEASURED span list, unlike a tolerance, which only changes the
verdict (Phase 5 D-08's own rule, applied here). `--tol`/severity overrides still govern whether
an introduced span gates the exit code.

### Silence

Set `audio.silence.edges` to `ignore` in `[severity]` for a pipeline where leading/trailing
silence is deliberately not a meaningful signal (e.g. a capture path with an intentional,
variable pre-roll). A silenced check's difference is still computed and shown under `-v`.
