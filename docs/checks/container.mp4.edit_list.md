# container.mp4.edit_list

## What it measures

One measurement per track, at that track's own scope: the canonical
string of that track's `elst` (edit list) entries, verbatim
(`segment_duration`, `media_time`, `media_rate`), in file order. `evidence`
classifies each entry: a `media_time` of `-1` is an EMPTY EDIT (a
presentation delay -- the track's playback is shifted later with no
media trimmed), and any other value is a TRIM (media before that time is
skipped).

This check pins the MECHANISM only -- it does not compute the semantic
effect of the edit list. `timeline.start` and `audio.priming` (elsewhere
in this project's check family) assert what that effect actually is; this
check exists so a change to the underlying edit-list mechanism itself is
visible even before its downstream effect is analyzed.

## Why it matters

An edit list controls exactly where playback starts and how streams are
aligned relative to each other. A change here -- even one that does not
alter any encoded sample -- can shift A/V sync, add or remove a
presentation delay, or change which portion of a track is actually played.
This is invisible to any check that only looks at encoded frame content.

## Accept / Tune / Silence

### Accept

If the edit-list change was intentional (e.g. deliberately re-timing a
track's start), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

None -- this check has no tolerance; each entry either matches exactly or
it doesn't.

### Silence

Set `container.mp4.edit_list` to `ignore` in `[severity]` for a pipeline
that intentionally varies edit lists in a way already verified safe.
Under `--profile strict-bitexact` and `--profile remux`, this check is
escalated to `fail` regardless (doc 02's own "fail in remux/strict, warn
in encoders" policy) -- an edit-list difference in a remux is rarely
intentional. Leave it enabled everywhere else -- a silenced check's
difference is still computed and shown under `-v`.
