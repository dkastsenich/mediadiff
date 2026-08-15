# t.span_runs

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Exercises the
`span` semantic over a `span_list` value with an `ms` unit.

## What it measures

A synthetic list of time spans (start/end pairs) supplied by a test
fixture or `StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `span` cells (`pass`/`info`/`fail`/
`error`) once plan 02-04 implements the `span` semantic; recorded in this
plan's allow list as expected-uncovered until then (D-14/D-15).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- test-only fixture, not a shipped tolerance.
- **Silence**: not applicable -- test-only fixture.
