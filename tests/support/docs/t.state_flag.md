# t.state_flag

Test-only check (04-12-PLAN.md, D-10) -- never shipped, never registered in
`src/core/checks.def`. Exercises the `state` semantic (`src/compare/state.cpp`),
the eighth comparison semantic added additively by this plan.

## What it measures

A synthetic string value supplied by a test fixture, compared against this
check's own `flagged_values = ["flagged"]`.

## Why it matters

Backs the fail-first coverage gate's `(state, pass)` / `(state, fail)` /
`(state, error)` cells (`tests/unit/test_fail_first_coverage.cpp`) -- in
particular, the `fail` cell's own fixture pair carries the IDENTICAL
flagged value on both sides, proving the `state` semantic's own defining
property: a value shared by both sides still fires when it is a flagged
value, unlike `exact`'s baseline-equality rule.

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- `state` has no tolerance.

### Silence

Not applicable -- test-only fixture.
