# t.dist_bins

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Exercises the
`dist` semantic over a `histogram` value with a `percent` unit.

## What it measures

A synthetic bin-proportion histogram supplied by a test fixture or
`StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `dist` cells (`pass`/`warn`/`fail`/
`error`) once plan 02-04 implements the `dist` semantic; recorded in this
plan's allow list as expected-uncovered until then (D-14/D-15).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- test-only fixture, not a shipped tolerance.
- **Silence**: not applicable -- test-only fixture.
