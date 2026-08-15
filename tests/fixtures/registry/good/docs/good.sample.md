# good.sample

## What it measures

Nothing real — this is the registry generator's known-good fixture
(02-02-PLAN.md Task 3), proving the generator exits 0 against a fully
documented single-check tree.

## Why it matters

Every `bad_*` fixture tree needs a passing sibling to prove against: a
generator that rejected everything would make every bad_* assertion
trivially true for the wrong reason.

## Accept / Tune / Silence

- **Accept**: not applicable — this check is never registered outside the
  test fixture tree.
- **Tune**: not applicable.
- **Silence**: not applicable.
