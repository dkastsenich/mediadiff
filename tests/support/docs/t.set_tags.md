# t.set_tags

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Exercises the
`set` semantic over a `string_set` value.

## What it measures

A synthetic set of tag strings supplied by a test fixture or
`StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `set` cells (`pass`/`fail`/`error`)
once plan 02-04 implements the `set` semantic; recorded in this plan's
allow list as expected-uncovered until then (D-14/D-15).

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- `set` carries no tolerance.

### Silence

Not applicable -- test-only fixture.
