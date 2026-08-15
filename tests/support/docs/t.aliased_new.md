# t.aliased_new

Test-only check (02-03-PLAN.md Task 1) -- never shipped. `exact` semantic
over a `string` value, `severity = "fail"`, carrying a declared alias
(`t.aliased_old`). Backs `(exact, fail)` in the fail-first coverage gate
and the permanent D-16 canary fixture.

## What it measures

A synthetic string value under its post-rename id.

## Why it matters

Proves `CheckRegistry::resolve_alias` resolves a declared alias against a
real generated `test_registry()`, and gives the fail-first coverage gate
and the D-16 canary a `fail`-severity `exact` check to drive a genuine
`fail` finding against, without touching any production check id.

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- `exact` has no tolerance.
- **Silence**: not applicable -- test-only fixture; the canary pair using
  this id must never be silenced or "fixed" (D-16).
