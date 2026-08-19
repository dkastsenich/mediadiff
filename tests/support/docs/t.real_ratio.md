# t.real_ratio

Test-only check (02-07-PLAN.md Task 1) -- never shipped. Exercises the
`tol` semantic over a `real` (bare `double`) value with a `5%` tolerance --
the one Value alternative (D-06's ninth) no other check, production or
test-only, had exercised through a real generated registry before this
plan. `read_snapshot`/`write_snapshot` both dispatch a measurement's
on-disk shape off its CheckDef's declared `value_kind`, so
`tests/unit/test_snapshot_roundtrip.cpp`'s "all nine alternatives
round-trip" fingerprint needed a check with `value_kind = "real"` to build
one against.

## What it measures

A synthetic `double` value supplied by a test fixture or `StubMeasurement`.

## Why it matters

Backs `tests/unit/test_snapshot_roundtrip.cpp`'s full-envelope round-trip
coverage of `Value`'s `double` alternative.

## Accept / Tune / Silence

### Accept

Not applicable -- this check never observes real media.

### Tune

Not applicable -- test-only fixture, not a shipped tolerance.

### Silence

Not applicable -- test-only fixture.
