# t.dist_bins

Test-only check (02-03-PLAN.md Task 1; tolerance added 02-04-PLAN.md
Task 3) -- never shipped. Exercises the `dist` semantic over a `histogram`
value with a `percent` unit and a `5%` worst-bin-proportion tolerance.

## What it measures

A synthetic bin-proportion histogram supplied by a test fixture or
`StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `dist` `pass`/`warn` cells
(D-14/D-15); `t.dist_bins_fail` backs the `fail` cell, which this check's
`warn` severity cannot produce on its own.

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- test-only fixture, not a shipped tolerance.

### Silence

Not applicable -- test-only fixture.
