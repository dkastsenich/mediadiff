# timeline.av_drift.pattern

## What it measures

The CLASSIFIED SHAPE of the same least-squares fit `timeline.av_drift`
reports the rate from -- one of exactly four values, doc 04 section 3.4:

- `constant-offset` -- every checkpoint's offset from the fitted line is
  within 2ms (the residual max is below epsilon), and the fitted rate is
  itself below the 0.2ms/min tolerance. No meaningful drift.
- `linear-drift` -- residual max is below epsilon (the points sit
  cleanly on a line), but the fitted rate is at or above the 0.2ms/min
  tolerance. A steady clock-rate mismatch.
- `step` -- one single, large jump in the raw offset trajectory (greater
  than three epsilons), with the checkpoints on EITHER side of that jump
  forming their own stable, flat group. A discrete event, not a gradual
  drift -- evidence `step_time_ms` names the checkpoint time the jump
  occurs at.
- `irregular` -- residual max is at or above epsilon, but the trajectory
  does not fit either the step shape above or a clean line. Evidence
  `residual_max_ms` reports the largest single deviation.

Produced by the same `fit_drift` call as `timeline.av_drift`, one
measurement per audio stream, and skips together with it under identical
conditions (`skipped:insufficient_data` when there is no audio, no
video, or too few usable checkpoints).

## Why it matters

The pattern check exists to catch a **step small enough to leave the
fitted rate under `timeline.av_drift`'s own tolerance**. A single,
discrete jump in sync partway through a file -- a mid-stream ad-insertion
splice, a container remux that re-timestamps only part of the file, a
decoder that drops and re-syncs -- can average out to a shallow overall
SLOPE across the whole file's duration even though the jump itself is
large and clearly audible at the moment it occurs. The rate check alone,
fitting one line across the entire span, would stay silent on exactly
this case; the pattern check exists specifically to name it `step`
instead.

## Accept / Tune / Silence

### Accept

If the classified pattern change reflects an intentional edit (a
deliberate mid-file splice, a known and accepted resync point), re-run
`mediadiff snapshot` on the file with that pattern to establish it as the
new baseline.

### Tune

This check has no tolerance -- it is an `exact` string match (D-04,
locked and one-way at the roster checkpoint: the four pattern spellings
above are fixed, not knobs). The only lever is severity, below.

### Silence

Set `timeline.av_drift.pattern` to `ignore` in `[severity]` for a
pipeline that only cares about the fitted rate and not the shape of the
underlying trajectory. Leave it enabled everywhere else -- a silenced
check's difference is still computed and shown under `-v`.
