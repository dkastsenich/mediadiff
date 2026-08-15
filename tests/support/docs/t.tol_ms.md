# t.tol_ms

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Exercises the
`tol` semantic over a rational (time) value with a `5ms` baseline tolerance.

## What it measures

A synthetic rational (time) value supplied by a test fixture or
`StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `tol` cells (`pass`/`info`/`warn`/
`fail`/`error`) once plan 02-04 implements the `tol` semantic; recorded in
this plan's allow list as expected-uncovered until then (D-14/D-15).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- test-only fixture, not a shipped tolerance.
- **Silence**: not applicable -- test-only fixture.
