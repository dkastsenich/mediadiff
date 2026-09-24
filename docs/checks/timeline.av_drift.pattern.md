# timeline.av_drift.pattern

## What it measures

The CLASSIFIED SHAPE of the same least-squares fit `timeline.av_drift`
reports the rate from -- one of exactly three values (doc 04 section 3.4
originally specified a fourth, `step`; withdrawn under narrow-vocabulary
-- see `### Limits of timestamp-only detection` below and
`05-CHECK-ROSTER.md`'s `## Amendments`):

- `constant-offset` -- every checkpoint's offset from the fitted line is
  within 2ms (the residual max is below epsilon), and the fitted rate is
  itself below the 0.2ms/min tolerance. No meaningful drift.
- `linear-drift` -- residual max is below epsilon (the points sit
  cleanly on a line), but the fitted rate is at or above the 0.2ms/min
  tolerance. A steady clock-rate mismatch.
- `irregular` -- residual max is at or above epsilon, and the trajectory
  does not fit a clean line. Evidence `residual_max_ms` reports the
  largest single deviation. A spliced trim -- a mid-stream ad-insertion,
  a partial-file remux, a decoder drop-and-resync -- also reports
  `irregular` with its own residual max; it is not distinguished from
  other irregular trajectories by a separate value.

Produced by the same `fit_drift` call as `timeline.av_drift`, one
measurement per audio stream, and skips together with it under identical
conditions (`skipped:insufficient_data` when there is no audio, no
video, or too few usable checkpoints).

**The classification depends on the SAME shared-basis checkpoint span
`timeline.av_drift` documents (D-16).** This check is `exact` (a plain
string compare, never routed through the tolerance comparator), so it
never itself swaps bases at compare time -- but the span the K=32
checkpoints are spread across is measured on the trimmed
(priming-/padding-excluded) basis only when it was actually reconstructed
from the packet-derived extent (this audio stream's own priming AND
padding tick counts both known, both subtractions in range, result
strictly positive -- WR-07, 06-19-PLAN.md), and on the packet-derived raw
extent otherwise (`span_basis` in `timeline.av_drift`'s own shared
evidence). Because the trajectory itself -- and therefore the
residual and rate this classification is derived from -- comes from
whichever basis this stream actually used, two files whose priming/padding
knowledge differs can classify differently even with identical real
content; that difference is visible in the shared evidence's own
`span_basis` field, not hidden.

### Limits of timestamp-only detection

- **A seamlessly re-timestamped trim is undetectable from timestamps alone.**
  If content is removed and the remaining packets' timestamps are
  rewritten so no discontinuity appears in the packet timeline (no gap,
  no jump), every checkpoint-mapping design evaluated for this check --
  including the shipped mapping -- smears the trim into the whole-file
  fit and reports `linear-drift`, indistinguishable from a genuine
  clock-rate mismatch. Distinguishing the two requires decoding audio
  content itself, out of scope until Phase 6's audio decode path exists
  (`05-STEP-DESIGN.md` `## Seamless re-timestamped trim`).
- **A timestamp gap is ambiguous between a dropout and a sync step.** A
  100 ms audio timestamp gap is produced identically by two different
  edits: trimming 100 ms of content while keeping the original
  timestamps (a dropout -- sync preserved, content missing) and shifting
  timestamps by 100 ms over otherwise-contiguous content (a sync step --
  content contiguous, timestamps jumped). The two edits can produce
  byte-identical files; there is no signal in the packet data alone that
  distinguishes them (`05-STEP-DESIGN.md` `## Ambiguity analysis`, A1).
- **The shipped mapping assumes the sync-step reading, silently, for
  both.** `timeline.av_drift`/`timeline.av_drift.pattern` report the same
  `irregular` verdict and residual for a genuine dropout as for a genuine
  sync step, because the checkpoint mapping projects real elapsed time
  proportionally across the file without distinguishing which edit
  produced the gap. Neither reading is reported specially, and neither
  is flagged as ambiguous in the finding itself -- only here, in this
  document.

## Why it matters

The pattern check exists to catch a **discrete desync event that a
single fitted rate would smear into an unremarkable slope**. A single,
discrete jump in sync partway through a file -- a mid-stream ad-insertion
splice, a container remux that re-timestamps only part of the file, a
decoder that drops and re-syncs -- can average out to a shallow overall
SLOPE across the whole file's duration even though the jump itself is
large and clearly audible at the moment it occurs. The rate check alone,
fitting one line across the entire span, would stay silent on exactly
this case; the pattern check surfaces it as `irregular` instead, naming
the largest single deviation in `residual_max_ms`.

## Accept / Tune / Silence

### Accept

If the classified pattern change reflects an intentional edit (a
deliberate mid-file splice, a known and accepted resync point), re-run
`mediadiff snapshot` on the file with that pattern to establish it as the
new baseline.

### Tune

This check has no tolerance -- it is an `exact` string match (D-04,
locked and one-way at the roster checkpoint; narrowed from four to three
spellings under UD-1 -- see `05-CHECK-ROSTER.md`'s `## Amendments`). The
three pattern spellings above are fixed, not knobs. The only lever is
severity, below.

### Silence

Set `timeline.av_drift.pattern` to `ignore` in `[severity]` for a
pipeline that only cares about the fitted rate and not the shape of the
underlying trajectory. Leave it enabled everywhere else -- a silenced
check's difference is still computed and shown under `-v`.
