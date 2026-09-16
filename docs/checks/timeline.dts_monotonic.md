# timeline.dts_monotonic

## What it measures

The count of `dts[i] <= dts[i-1]` violations for one stream, evaluated in
READ order over the shared packet scan, kept as an `int64` under a
zero-magnitude absolute tolerance (`0`, not `exact` -- see Tune below). A
tie (`dts[i] == dts[i-1]`) counts as a violation, not only a strict
decrease -- doc 04's own definition is `<=`, never only `<`.

Packets carrying `AV_NOPTS_VALUE` on the DTS axis are **excluded** from the
axis under test before counting starts, rather than coerced into the
comparison as a value -- a stream whose every DTS is the absent sentinel
reports `skipped:no_timing_data`, never a fabricated `0`. A mixed stream
(some real DTS, some absent) counts violations only across consecutive
REAL values; the excluded count rides in evidence
(`excluded_sentinel_count`) so a reader can see how many packets were
skipped over.

On an MPEG-TS input, the DTS sequence under test is the **unwrapped** one:
the raw 33-bit values are passed through this project's TS unwrap rule
(doc 04 section 1.2) before this check ever sees them, so a stream that
legitimately wraps mid-file reports zero monotonicity violations from the
wrap itself. The `unwrapped` evidence flag records whether this basis
applied.

This check runs on every stream carrying timestamps **except** subtitle
streams -- a subtitle track's own presentation pattern is a different
property this check does not evaluate (05-CHECK-ROSTER.md's own scope
decision).

When the packet scan was truncated (`partial_scan`), this check skips
rather than reporting a count derived from an incomplete sweep -- a count
from a truncated scan understates the real count, which is worse than no
answer at all.

## Why it matters

A decode timestamp that goes backward or repeats usually means a
corrupted, mis-spliced, or mis-multiplexed stream -- a real player either
stalls, skips, or misbehaves at exactly that point. This check catches
that class of structural defect independent of whether the content itself
looks fine.

## Accept / Tune / Silence

### Accept

If the violation was an intentional edit-point splice or a deliberately
crafted test stream, re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The default `0` tolerance behaves as exact equality while keeping
`--tol timeline.dts_monotonic=2` (or a `[check.tolerance]` entry in
`mediadiff.toml`) meaningful for a pipeline with known, accepted noise --
`exact` would not allow this override at all.

### Silence

Set `timeline.dts_monotonic` to `ignore` in `[severity]` for a pipeline
where DTS ordering is deliberately not a meaningful signal (e.g. a raw
capture path known to carry harmless reordering). A silenced check's
difference is still computed and shown under `-v`.
