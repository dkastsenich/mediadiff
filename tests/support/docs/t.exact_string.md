# t.exact_string

Test-only check (02-03-PLAN.md Task 1) -- never shipped, never registered in
`src/core/checks.def`. Exercises the `exact` semantic over a `string` value.

## What it measures

A synthetic string value supplied by a test fixture or `StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `(exact, pass)` / `(exact, error)`
cells and demonstrates a real `[check.profile_severity]` override against a
generated `test_registry()`, mirroring `meta.tool_version`'s production
override (02-02-PLAN.md).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- `exact` has no tolerance.
- **Silence**: not applicable -- test-only fixture.
