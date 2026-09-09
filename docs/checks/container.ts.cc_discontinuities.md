# container.ts.cc_discontinuities

## What it measures

The count of FLAGGED continuity-counter resets across every PID in the
transport stream -- packets whose adaptation field carries
`discontinuity_indicator = 1`, summed into one number at global scope.
Split from `container.ts.cc_errors` (03-CHECK-ROSTER.md): a flagged
discontinuity is the muxer declaring an intentional break in the counter
sequence (e.g. at a splice point, an ad-insertion boundary, or a
re-multiplex join), and ISO 13818-1 section 2.4.3.3 explicitly permits the
counter to jump there. This check tracks that signal separately and never
folds it into `container.ts.cc_errors`' gating count.

## Why it matters

A flagged discontinuity is informational BY DESIGN, not a defect --
recording it separately (rather than silently ignoring it, or worse,
counting it as an error) lets a reader see where the stream intentionally
resets without that visibility ever gating a merge. Conflating flagged and
unflagged resets would either hide real damage behind an expected splice
point, or fail a merge over a splice the muxer explicitly declared safe --
this check exists specifically to prevent both failure modes.

## Accept / Tune / Silence

### Accept

A change in this count is not itself a defect signal; if the number of
splice/discontinuity points changed for a known, intentional reason (a
different ad-insertion schedule, a different remux boundary), re-run
`mediadiff snapshot` to establish the new candidate as baseline.

### Tune

There is no tolerance to tune -- `exact` at `info` severity means the count
is reported but never gates the exit code under normal severity policy.

### Silence

`container.ts.cc_discontinuities` is `info` severity by default and does
not gate the exit code; set it to `ignore` in `[severity]` only if this
count is not useful diagnostic signal for a given pipeline. A silenced
check's difference is still computed and shown under `-v`.
