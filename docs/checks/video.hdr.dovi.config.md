# video.hdr.dovi.config

## What it measures

The Dolby Vision configuration record's own fields -- profile, level, and
the RPU/enhancement-layer/base-layer presence flags -- from the same
stream-level side data `video.hdr.dovi` reads. Split into its own id from
`video.hdr.dovi` for the identical reason `video.hdr.mdcv.luminance` was
split from `video.hdr.mdcv`: a `presence` check never compares values.

Compared as a canonical string in this FIXED field order:

```
profile=<N> level=<N> rpu=<0|1> el=<0|1> bl=<0|1>
```

For example, a profile 8, level 6 record with RPU and base-layer data
present but no enhancement layer renders as `profile=8 level=6 rpu=1 el=0
bl=1`. This canonical form is readable directly rather than opaque -- a
user reading `-v` output or a `--json` finding can see exactly which field
moved without decoding anything further.

**v1 scope:** this check compares the configuration record's fields only.
Per-frame RPU (reshaping metadata) diffing is explicitly out of scope --
see `video.hdr.dovi`'s own "What it measures" for why no RPU-bearing
Dolby Vision fixture is achievable in this project's toolchain, and no RPU
parsing exists in this codebase.

When there is nothing to measure -- the record is absent, on a codec that
either could or could not carry it via a decode pass this phase does not
have -- this check emits the shared `skipped:requires_decode` skip, never
`Absent`, matching every other value-bearing HDR check in this project
(`video.hdr.mdcv.luminance`/`.primaries`, `video.hdr.cll.max`/`.avg`).

## Why it matters

A profile or level change alters which decoder capability tier a receiver
needs to correctly render the content -- a profile 8.1 stream (base layer
+ enhancement layer + RPU) played on a decoder that only understands
profile 5 (single-layer) will not decode correctly. A silent profile/level
drift during a remux or transcode is exactly the kind of distribution
regression this check exists to catch, independent of whether the
underlying picture content changed at all.

## Accept / Tune / Silence

### Accept

If the configuration change reflects an intentional re-encode to a
different Dolby Vision profile or layer structure, re-run
`mediadiff snapshot` on the new candidate.

### Tune

This is `exact` semantics over a canonical string -- there is no
`[tolerance]` override to set. Adjust `[severity]` instead if a pipeline
needs a different response than `fail`.

### Silence

Set `video.hdr.dovi.config` to `ignore` in `[severity]` for a pipeline
with no Dolby Vision-aware downstream consumer. Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
