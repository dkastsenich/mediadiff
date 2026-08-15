# t.tol_ms

Test-only check (02-03-PLAN.md Task 1; tolerance updated 02-04-PLAN.md
Task 3) -- never shipped. Exercises the `tol` semantic over a rational
(time) value with a two-threshold `3ms,5ms` tolerance: a delta inside
`3ms` passes, between `3ms` and `5ms` warns, and beyond `5ms` fails --
independent of `severity` below (doc 01 section 3).

## What it measures

A synthetic rational (time) value supplied by a test fixture or
`StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `tol` `pass`/`warn`/`fail` cells
(D-14/D-15); `t.tol_info` backs the `info` cell, which this check's
two-threshold form cannot produce on its own.

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- test-only fixture, not a shipped tolerance.

### Silence

Not applicable -- test-only fixture.
