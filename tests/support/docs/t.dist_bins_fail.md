# t.dist_bins_fail

Test-only check (02-04-PLAN.md Task 3) -- never shipped. Exercises the
`dist` semantic's severity-escalation path with `severity = "fail"`: a
worst-bin proportion difference beyond the `5%` tolerance escalates to
`fail`, the one `dist` status `t.dist_bins` (severity `warn`) cannot
produce on its own.

## What it measures

A synthetic bin-proportion histogram supplied by a test fixture or
`StubMeasurement` -- structurally identical to `t.dist_bins`, differing
only in declared severity.

## Why it matters

Backs the fail-first coverage gate's `dist` `fail` cell (D-14/D-15) --
`t.dist_bins` backs `pass`/`warn`, and this check exists solely so `fail`
has a real, dispatchable fixture too, rather than a coverage allow-list
entry.

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- test-only fixture, not a shipped tolerance.
- **Silence**: not applicable -- test-only fixture.
