# t.tol_info

Test-only check (02-04-PLAN.md Task 3) -- never shipped. Exercises the
`tol` semantic's single-threshold, severity-escalation path with
`severity = "info"`: a delta beyond the `5ms` tolerance escalates to
`info` rather than `fail`/`warn`, the one `tol` status `t.tol_ms`'s
two-threshold form cannot produce on its own.

## What it measures

A synthetic rational (time) value supplied by a test fixture or
`StubMeasurement`.

## Why it matters

Backs the fail-first coverage gate's `tol` `info` cell (D-14/D-15) --
`t.tol_ms` backs `pass`/`warn`/`fail` via its own two-threshold tolerance,
and this check exists solely so `info` has a real, dispatchable fixture
too, rather than a coverage allow-list entry.

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- test-only fixture, not a shipped tolerance.
- **Silence**: not applicable -- test-only fixture.
