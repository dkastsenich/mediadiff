# meta.extra_candidate

## What it measures

In `dir` mode, whether every candidate file that was paired by relative
path also has a matching baseline file. This check fires once per
candidate file that has no baseline counterpart — the file exists on the
candidate side of the comparison and not on the baseline side.

## Why it matters

A file appearing that wasn't in the baseline corpus is usually intentional
(a new variant added to the pipeline) rather than a regression, which is
why this check's baseline severity is `warn` rather than `fail` — unlike
`meta.missing_candidate`, an extra file rarely represents lost output. It
is still worth surfacing: an unexpectedly large batch of new files can
indicate a packaging step ran twice, or a stale directory got merged into
the corpus by accident (doc 01 section 10).

## Accept / Tune / Silence

- **Accept**: confirm the addition was intentional, then update the
  baseline corpus so the next run treats the new file as expected.
- **Tune**: none — this check has no tolerance; a file is either paired or
  it isn't.
- **Silence**: set `meta.extra_candidate` to `ignore` in `[severity]` for a
  corpus that is known to grow between runs by design (e.g. an
  append-only archive) — leave it enabled everywhere else, since an
  unexpected extra file is still worth a human noticing at least once.
