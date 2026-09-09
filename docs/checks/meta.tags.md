# meta.tags

## What it measures

Every container-level and per-stream metadata tag as a set of `key=value`
entries, with a small built-in list of volatile keys --
`creation_time`, `encoder`, `handler_name`, `encoding_tool` -- excluded
from the compared value (matched case-insensitively over ASCII only). One
measurement is emitted for the container's own dictionary (scoped
globally) and one per stream (scoped to that stream), so a per-stream tag
change is attributed to the stream it happened on rather than blamed on
the file as a whole.

`major_brand` is deliberately NOT on the volatile list -- MP4 brand
changes are scoped to their own dedicated `container.mp4.brands` check,
not folded into this generic tag comparison.

Ignoring a key for comparison is never the same as hiding it: every
ignored key whose value actually differs between baseline and candidate is
still recorded in this finding's evidence, visible under `-v`.

## Why it matters

Most encoders and muxers stamp a handful of tags (timestamps, tool
identity strings) that vary on every single run even when nothing about
the actual media changed -- comparing those verbatim would make this check
cry wolf on every clean re-run, which is exactly the false-positive
failure mode this project treats as P0. Excluding them from the compared
value, while still surfacing them in evidence, keeps the check trustworthy
without pretending those differences don't exist.

## Accept / Tune / Silence

### Accept

If the tag change was intentional (e.g. deliberately updating a `title`
tag), re-run `mediadiff snapshot` on the new candidate to establish it as
the new baseline.

### Tune

None -- this check has no numeric tolerance; the non-volatile tag set
either matches exactly or it doesn't.

### Silence

Set `meta.tags` to `ignore` in `[severity]` for a pipeline that
intentionally rewrites metadata on every run in ways this project's
built-in volatile list doesn't already cover. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`, and the built-in volatile list (`creation_time`, `encoder`,
`handler_name`, `encoding_tool`) already keeps the common noisy cases
quiet by default.
