# t.transform_resolution

Test-only check (02-05-PLAN.md Task 3) -- never shipped. `exact` semantic
over a `string` value shaped `"WIDTHxHEIGHT"`, `severity = "fail"`, carrying
`transform_affected = true` -- the only check in either registry that does,
since no shipped check needs it until Phase 4's real resolution identity
check exists.

## What it measures

A synthetic `"WIDTHxHEIGHT"` resolution string supplied by a test fixture.

## Why it matters

Proves the `transform` profile's expectation mechanism (doc 01 section 5,
ENG-10): under `transform` with a declared `expect.resolution`, this check
compares the candidate against the value the expectation derives from the
baseline (a scale factor or an absolute pair) instead of against the
baseline itself, while every other check -- including this one under any
other profile -- keeps comparing by ordinary baseline equality.

## Accept / Tune / Silence

- **Accept**: not applicable -- this check never observes real media.
- **Tune**: not applicable -- `exact` has no tolerance.
- **Silence**: not applicable -- test-only fixture.
