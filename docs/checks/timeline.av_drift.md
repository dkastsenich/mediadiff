# timeline.av_drift

## What it measures

The rate at which audio and video presentation timelines drift apart over
the file's own duration, in milliseconds per minute -- an exact rational,
not a rounded decimal. One measurement is produced per audio stream,
scoped to that stream, from the SAME least-squares fit doc 04 section 3
defines: 32 checkpoints spread evenly across the video timeline, each
pairing the nearest video frame's presentation time with the nearest
aligned audio sample boundary, fitted with a least-squares line. The
slope of that line, converted to ms/min, is this check's own value;
`timeline.av_drift.pattern` (a separate id) reports the CLASSIFIED SHAPE
of the same fit.

**This check gates on TWO conditions together, not the rate alone.** A
non-pass verdict requires BOTH:

1. The rate difference (baseline vs. candidate) to clear the registered
   `0.2ms/min` tolerance, AND
2. The accumulated END DELTA -- evidence `end_delta_ms`, the total drift
   from the first checkpoint to the last -- to clear a fixed 2ms epsilon.

When the end delta does not clear 2ms, the finding is forced to `pass`
regardless of what the rate delta alone would suggest. The rate value
itself is always published and always visible under `-v`; only the
pass/fail verdict is gated by the dual condition.

Evidence also carries `residual_max_ms`, `step_time_ms` (present only
when `timeline.av_drift.pattern` is `step`), `comparison_basis` (the same
raw-versus-adjusted basis `timeline.av_offset` chose for this side, D-10)
and the full 32-entry `{k, t_v_ms, offset_ms}` trajectory the fit was
computed from -- `mediadiff snapshot` stores the trajectory, so a later
`compare` against that snapshot reproduces the identical rate, pattern
and trajectory with no loss of fidelity.

No audio stream, no video stream, or too few usable checkpoints to fit a
line: `skipped:insufficient_data` -- never a fabricated rate.

## Why it matters

A/V offset at file start (`timeline.av_offset`) catches a fixed sync
error, but a pipeline can also introduce a clock-rate mismatch between
the audio and video encode paths -- two independent clocks running at
subtly different rates, or a resampling/timestamp-rewriting bug that
accumulates a small per-frame error. Neither symptom shows up as a
constant offset: sync is fine at the start of the file and steadily worse
by the end. A 0.5ms/min drift is imperceptible in a 30-second clip and
extremely noticeable by minute 20 of a livestream recording or a
long-form video -- exactly the class of regression a fixed-offset check
alone cannot see.

## Accept / Tune / Silence

### Accept

If the drift is an intentional or expected characteristic of the source
(for example, a capture device with a known, accepted clock-rate
mismatch that downstream tooling already compensates for), re-run
`mediadiff snapshot` on the file exhibiting that rate to establish it as
the new baseline.

### Tune

The default `0.2ms/min` tolerance (`--tol timeline.av_drift=<fail>`)
bounds how far the fitted rate may move between baseline and candidate
before failing -- but **widening this tolerance alone will not silence a
real drift regression**, because of the dual condition above. The
tolerance only controls the RATE half of the gate; the 2ms end-delta
epsilon is fixed and not exposed as a `--tol` knob, by design (D-08: a
detection constant, not a knob, in v1). This is also why a noisy SHORT
clip does not gate on an apparently large rate: 0.2ms/min sustained over
the ten-minute reference file this tolerance was calibrated against is
exactly 2ms of accumulated drift -- below that duration, ordinary
timebase rounding can produce a rate that LOOKS like it clears the
per-minute tolerance while the actual accumulated drift across the whole
clip is still sub-epsilon and not a real, actionable regression. If a
pipeline's real clips are consistently much shorter than ten minutes and
genuinely need a tighter or looser gate, that is a signal to revisit the
fixed epsilon in a future plan, not to work around it by widening
`--tol` alone (which will not have the intended effect).

### Silence

Set `timeline.av_drift` to `ignore` in `[severity]` for a pipeline with
no long-duration A/V sync requirement (for example, a pipeline that only
ever produces short clips where accumulated drift is structurally
incapable of reaching the 2ms end-delta gate). Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
