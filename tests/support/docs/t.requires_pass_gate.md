# t.requires_pass_gate

Test-only check (02-03-PLAN.md Task 1) -- never shipped. Declares
`requires_pass = true`, exercising the flags half of `CheckDef` rather than
a new semantic (`exact` again, over a `string` value).

## What it measures

A synthetic string value gating a later, dependent check -- the shape a
"only evaluate this if its prerequisite passed" dependency will use in a
later phase.

## Why it matters

Proves the generator's `requires_pass` flag round-trips through a real
generated `CheckDef` (`requires_pass == true`), not only through
production's own fixture registry trees (02-02-PLAN.md Task 3).

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- `exact` has no tolerance.
- **Silence**: not applicable -- test-only fixture.
