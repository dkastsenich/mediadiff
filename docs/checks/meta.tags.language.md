# meta.tags.language

## What it measures

Each stream's own `language` metadata tag, ASCII-case-folded to lowercase.
An absent `language` tag normalizes to the literal string `und` (ISO
639-2's own "undetermined" code) at measurement construction time, so a
stream carrying `language=und` and a stream carrying no language tag at
all compare as identical values.

This normalization deliberately does NOT resolve ISO 639-2 terminology
(`T`) versus bibliographic (`B`) code aliases -- `fra`/`fre` and
`deu`/`ger` are treated as genuinely different values, not folded together.

## Why it matters

Different muxers disagree on whether an unset language should be omitted
entirely or written out as the literal `und` code -- that divergence is a
muxer implementation detail, not a real change to the media, so comparing
the two forms as different would be a false positive. A real language
change (e.g. `eng` to `fra`), by contrast, is exactly the kind of
regression this check exists to catch -- someone's pipeline started
tagging streams with the wrong language, or stopped tagging them
correctly.

### Why the T/B alias split stays a real difference

`und` versus absent is a muxer artifact with no semantic content: neither
form ever changes what a player does with the stream. A T-versus-B
spelling change (`fra` vs `fre`), however, IS a real value change some
downstream tooling treats differently -- silently collapsing the two would
be exactly the kind of false negative this project treats as worse than a
false positive.

## Accept / Tune / Silence

### Accept

If the language change was intentional (e.g. deliberately relabeling a
stream), re-run `mediadiff snapshot` on the new candidate to establish it
as the new baseline.

### Tune

None -- this check has no numeric tolerance; the normalized language code
either matches exactly or it doesn't.

### Silence

Set `meta.tags.language` to `ignore` in `[severity]` for a pipeline that
intentionally relabels stream languages on every run. Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
