# meta.missing_candidate

## What it measures

In `dir` mode, whether every baseline file that was paired by relative path
also has a matching candidate file. This check fires once per baseline
file that has no candidate counterpart — the file exists on one side of
the comparison and not the other.

## Why it matters

A file silently disappearing between baseline and candidate corpora is
often a packaging or build-step regression (an encode step skipped, a
publish manifest out of date, a rename that broke the pairing) — the kind
of change that "everything else compares clean" would otherwise hide
completely, because a file that isn't there produces no findings of its
own unless something explicitly says so (doc 01 section 10).

## Accept / Tune / Silence

### Accept

Confirm the removal was intentional (a deprecated variant dropped from
the pipeline, for example), then update the baseline corpus so the next
run no longer reports it.

### Tune

None — this check has no tolerance; a file is either paired or it isn't.

### Silence

Set `meta.missing_candidate` to `ignore` in `[severity]` only for a corpus
that is known to shrink between runs by design (e.g. a rolling retention
window) — leave it enabled everywhere else, since a missing file is
exactly the kind of regression `dir` mode exists to catch.
