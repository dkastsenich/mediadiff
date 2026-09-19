# timeline.pts_unique

## What it measures

The count of duplicate presentation PTS values for one stream, kept as an
`int64` under a zero-magnitude absolute tolerance (`0`, not `exact` -- see
Tune below). Found by sorting a LOCAL view of the stream's own PTS values
(never the packet array's own read order) and counting adjacent equal
values -- two packets that share an identical presentation timestamp
report as one duplicate.

Packets carrying `AV_NOPTS_VALUE` on the PTS axis are **excluded** from the
axis under test before counting starts, rather than coerced into the
comparison as a value -- a stream whose every PTS is the absent sentinel
reports `skipped:no_timing_data`, never a fabricated `0`. The excluded
count rides in evidence (`excluded_sentinel_count`).

On an MPEG-TS input, the PTS sequence under test is the **unwrapped** one
(doc 04 section 1.2) -- the `unwrapped` evidence flag records whether this
basis applied. Evidence also carries `first_duplicate_value` and
`duplicate_indices` (the two packet indices sharing that value) for the
first duplicate found.

This check runs on every stream carrying timestamps **except** subtitle
streams (05-CHECK-ROSTER.md's own scope decision).

When the packet scan was truncated (`partial_scan`), this check skips
rather than reporting a count derived from an incomplete sweep.

## Why it matters

Two frames sharing one presentation timestamp usually means a dropped or
duplicated sample, a broken timestamp-rewrite step, or a splice error --
each a distinct failure a player will render as a stutter or a frozen
frame, all visible through this one number.

## Accept / Tune / Silence

### Accept

If the duplicate was an intentional edit (e.g. a deliberately repeated
frame at a splice point), re-run `mediadiff snapshot` on the new candidate
to establish it as the new baseline.

### Tune

The default `0` tolerance behaves as exact equality while keeping
`--tol timeline.pts_unique=2` (or a `[check.tolerance]` entry in
`mediadiff.toml`) meaningful for a pipeline with known, accepted noise --
`exact` would not allow this override at all.

### Silence

Set `timeline.pts_unique` to `ignore` in `[severity]` for a pipeline where
duplicate presentation timestamps are deliberately not a meaningful signal.
A silenced check's difference is still computed and shown under `-v`.
