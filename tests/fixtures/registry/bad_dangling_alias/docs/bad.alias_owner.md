# bad.alias_owner

## What it measures

Nothing real — this is the registry generator's dangling-alias fixture
(02-02-PLAN.md Task 3). This check declares the colliding alias
("bad.alias_target") that the generator must reject.

## Why it matters

Same rationale as bad.alias_target.md: proving the generator's alias
validation fires on the declaring check, not only on the collided-with
one.

## Accept / Tune / Silence

- **Accept**: not applicable — this check is never registered outside the
  test fixture tree.
- **Tune**: not applicable.
- **Silence**: not applicable.
