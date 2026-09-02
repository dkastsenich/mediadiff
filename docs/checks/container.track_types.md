# container.track_types

## What it measures

The ordered sequence of every stream's media type, in stream index order,
as one canonical comma-joined string (e.g. `video,audio,audio,subtitle`).
Unlike `container.track_count`, this preserves both order and duplicates --
two files with the identical set of types in a different order produce
DIFFERENT values here.

A dropped timecode (`tmcd`) track or a dropped caption data track is named
explicitly in this check's finding message and evidence, not merely
reflected as a change in the generic `data` count -- their loss is the
headline doc 02 requires, not an inference a reader has to make from a
type letter.

## Why it matters

A track-count change alone doesn't say which kind of loss happened, or
whether a timecode or caption track specifically vanished -- both of which
matter far more to a downstream consumer than an undifferentiated `data`
stream dropping out. Naming the timecode/caption loss explicitly is what
makes the finding actionable without a human having to cross-reference the
raw stream list by hand.

## Accept / Tune / Silence

### Accept

If the type sequence change was intentional (e.g. deliberately dropping a
caption or timecode track for a distribution profile), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the type sequence either matches
exactly or it doesn't.

### Silence

Set `container.track_types` to `ignore` in `[severity]` only for a
pipeline whose entire purpose is deliberately reordering or dropping
tracks. Leave it enabled everywhere else -- a silenced check's difference
is still computed and shown under `-v`, so a lost timecode or caption
track is never hidden, only kept from gating the exit code.
