# t.span_runs

Test-only check (02-03-PLAN.md Task 1; severity changed to `fail`
02-04-PLAN.md Task 3) -- never shipped. Exercises the `span` semantic over
a `span_list` value with an `ms` unit: an introduced span escalates via
`severity` (`fail`), and a removed-span-only case is always `info`
regardless of severity (doc 01 section 3) -- this one check backs all
three of `pass`/`info`/`fail`.

## What it measures

A synthetic list of time spans (start/end pairs) supplied by a test
fixture or `StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `span` cells (`pass`/`info`/`fail`/
`error`) (D-14/D-15).

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- test-only fixture, not a shipped tolerance.

### Silence

Not applicable -- test-only fixture.
