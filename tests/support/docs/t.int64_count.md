# t.int64_count

Test-only check (02-04-PLAN.md Task 2) -- never shipped. Exercises the
`exact` semantic over an `int64` value; exists specifically so
compare/engine.cpp's D-09 value_kind guard has a check declaring
`value_kind = "int64"` to test against -- every other `t.*` check declares
a different kind, and D-09's acceptance criterion names this exact pairing
("a measurement holding a std::string for a check whose declared
value_kind is int64").

## What it measures

A synthetic int64 value supplied by a test fixture or `StubMeasurement`.

## Why it matters

Backs `tests/unit/test_compare_semantics.cpp`'s D-09 value_kind-mismatch
test (compare/engine.cpp's `value_kind_mismatch`).

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- test-only fixture, not a shipped tolerance.

### Silence

Not applicable -- test-only fixture.
