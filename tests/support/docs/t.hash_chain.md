# t.hash_chain

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Exercises the
`hash` semantic over a `hash_chain` value -- the one semantic that must be
able to emit `skipped` (D-15: proving `hash` can say "we cannot tell"
matters as much as proving it can fail).

## What it measures

A synthetic hash chain (algorithm, digest, element count) supplied by a
test fixture or `StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `hash` cells (`pass`/`fail`/`skipped`/
`error`) once plan 02-04 implements the `hash` semantic; recorded in this
plan's allow list as expected-uncovered until then (D-14/D-15).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- `hash` carries no tolerance.
- **Silence**: not applicable -- test-only fixture.
