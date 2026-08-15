# t.volatile_tag

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Declares
`volatile = true`, exercising the flags half of `CheckDef` rather than a
new semantic (`exact` again, over a `string` value).

## What it measures

A synthetic string value flagged as volatile metadata -- the kind of field
a `set`-semantic check's ignore list would exclude in a later phase.

## Why it matters

Proves the generator's `volatile` flag round-trips through a real
generated `CheckDef` (`is_volatile == true`), not only through
production's own fixture registry trees (02-02-PLAN.md Task 3).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- `exact` has no tolerance.
- **Silence**: not applicable -- test-only fixture.
