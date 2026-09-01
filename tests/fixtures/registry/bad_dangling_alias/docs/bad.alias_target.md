# bad.alias_target

## What it measures

Nothing real — this is the registry generator's dangling-alias fixture
(02-02-PLAN.md Task 3), proving the generator rejects an alias that
collides with a registered check id.

## Why it matters

An alias colliding with a real check id would make `resolve_alias`
ambiguous: is the caller asking for the check, or for whatever it used to
be called before some other rename? Rejecting the collision at generation
time is what keeps the answer always unambiguous.

## Accept / Tune / Silence

- **Accept**: not applicable — this check is never registered outside the
  test fixture tree.
- **Tune**: not applicable.
- **Silence**: not applicable.
