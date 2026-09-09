# container.chapters

## What it measures

Every chapter marker in the container, as a set of canonicalized entries
each carrying a start tick, an end tick, the shared timebase both were
recorded in, and the chapter's own title. Chapter times are always
rational ticks, never floating milliseconds -- floating milliseconds
appear only in rendered output, per this project's own time-representation
constraint.

This check is not applicable to every container family: MPEG transport
streams have no chapter concept at all, so `container.chapters` auto-skips
as `skipped:not_applicable_container` on a TS input rather than reporting
an empty set (which would be indistinguishable from "measured zero
chapters on a container that genuinely supports them").

## Why it matters

Chapters are often the only navigation structure a viewer or a player's
scrubber UI exposes; silently losing them during a remux degrades the
viewing experience without touching a single video or audio sample. Because
this is a `set` comparison, chapter reordering with identical membership
does not itself trigger a finding -- only an added, removed, or genuinely
changed chapter entry does.

## Accept / Tune / Silence

### Accept

If the chapter change was intentional (e.g. deliberately stripping
chapters for a distribution profile), re-run `mediadiff snapshot` on the
new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; each chapter entry either matches
exactly or it doesn't.

### Silence

Set `container.chapters` to `ignore` in `[severity]` for a pipeline that
never carries chapters at all. Leave it enabled everywhere else -- a
silenced check's difference is still computed and shown under `-v`.
