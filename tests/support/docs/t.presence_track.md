# t.presence_track

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Exercises the
`presence` semantic, mirroring the shape (not the identity) of production's
`meta.missing_candidate`/`meta.extra_candidate`.

## What it measures

Whether a synthetic track-like measurement is present on both sides of a
compare, supplied by a test fixture or `StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `presence` cells (`pass`/`fail`/
`error`) once plan 02-04 implements the `presence` semantic; recorded in
this plan's allow list as expected-uncovered until then (D-14/D-15).

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- `presence` carries no tolerance.

### Silence

Not applicable -- test-only fixture.
