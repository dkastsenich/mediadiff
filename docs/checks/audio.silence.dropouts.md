# audio.silence.dropouts

## What it measures

Interior audio dropouts, detected inside the SAME shared audio decode sweep `content.audio.
sample_hash` and `audio.loudness.*`/`audio.silence.edges` already consume -- no second decode
(AUDIO-10). A sliding **100ms RMS window** over the per-sample-frame peak amplitude is compared
against a fixed **-70 dBFS** threshold (a mean-square-vs-threshold-squared comparison, computed
entirely in integer/fixed-point arithmetic -- cross-multiplied to avoid a division, never
floating point in the derivation itself). A run is reported only once its length reaches a fixed
**150ms minimum span**.

A candidate run is discarded here -- never reported as a dropout -- when it starts at the very
first sample, or is still below threshold when the stream ends: both are `audio.silence.edges`'
own territory, never double-reported. Only a run that is both properly closed before the
stream's last sample AND does not begin at the first sample is a genuine interior dropout.

Because the window is trailing (causal), a reported span's own duration is systematically
shorter than the true silent stretch by roughly one window width (100ms) -- the window must be
ENTIRELY inside the silent region before the sliding comparison first reads "below threshold",
and it starts reading "above threshold" again as soon as it includes even one loud sample past
the resumption point. This is a deliberate, documented property of the trailing-window design,
not a bug: a hole exactly at (or below) the 150ms minimum span may report no span at all, or a
shorter one, which is why 150ms is a floor on the TRUE underlying event, not on the reported
span length.

Each recorded span's `start`/`end` are exact `RationalValue`s in ms, derived from the sample
index and the stream's own sample rate through checked rational arithmetic (`src/core/
rational.h`) -- never a pre-divided float. Two spans that touch exactly (`end == start` of the
next) are merged into one, mirroring `timeline.discontinuities`' own merge convention. A stream
with no detected dropout reports an **empty span list** as a real measured value -- never
`Absent{}` and never a skip.

When the shared decode sweep never ran for this stream (`--no-content`, or a codec this build's
linked FFmpeg cannot open at all), this check skips `requires_decode`. When the sweep ran but
produced zero decoded samples, or a native sample format this detector cannot interpret, it
skips `insufficient_data` -- never a fabricated empty list. When the sweep stopped early (a decode
sweep limit hit before this stream's own end), it skips `partial_scan` with evidence `reason`
naming the stop token -- see the "Decode stop reasons" table in `content.audio.sample_hash.md` --
never a span list computed from only the part of the stream that was measured.

## Why it matters

An interior dropout -- audio that silently drops out mid-stream and resumes -- is the classic
buffer-handling regression: a decoder underrun, a muxer splice error, or a transcode pipeline
that briefly lost its input. Loudness measurements average a short dropout away almost entirely,
and no container-level parameter check observes decoded sample content at all, so this is
exactly the class of defect that is invisible everywhere except here.

## Accept / Tune / Silence

### Accept

If the introduced dropout was an intentional edit (a deliberate mute or splice point), re-run
`mediadiff snapshot` on the new candidate to establish it as the new baseline.

### Tune

The -70 dBFS threshold, the 100ms RMS window and the 150ms minimum span are fixed, named
DETECTION constants (`kDropoutThresholdDbfs`, `kDropoutRmsWindowMs`, `kDropoutMinSpanMs`), never
a configurable knob in v1 -- a detection parameter changes the MEASURED span list, unlike a
tolerance, which only changes the verdict (Phase 5 D-08's own rule, applied here). `--tol`/
severity overrides still govern whether an introduced span gates the exit code.

### Silence

Set `audio.silence.dropouts` to `ignore` in `[severity]` for a pipeline where brief interior
dropouts are deliberately not a meaningful signal (e.g. a lossy live-capture path with known,
tolerated micro-dropouts). A silenced check's difference is still computed and shown under `-v`.
