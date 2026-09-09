# container.track_order

## What it measures

The ordered `(media_type, codec_name)` signature of every stream, in
stream index order, as one canonical string. `codec_name` is libav's own
stable codec name (e.g. `h264`, `aac`), never the numeric codec ID, so the
recorded signature does not shift when a future FFmpeg release renumbers
its internal codec-ID enum.

Reordering two streams with identical membership (e.g. swapping which
audio track comes first) fails THIS check while `container.track_count`
and `container.track_types` (an order-insensitive count and an
order-preserving-but-membership-only-in-practice multiset for identical
type sequences) still pass -- doc 02's own framing: "a move, not
add+remove".

## Why it matters

Stream order matters to real players and pipelines: many tools pick "the
first audio stream" or "the first subtitle stream" by index, so a pure
reorder with no track added or removed can still change observed playback
behavior even though nothing was technically lost.

## Accept / Tune / Silence

### Accept

If the reorder was intentional (e.g. deliberately promoting a different
audio track to index 0), re-run `mediadiff snapshot` on the new candidate
to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the ordered signature either matches
exactly or it doesn't.

### Silence

Set `container.track_order` to `ignore` in `[severity]` for a pipeline
whose muxer is known to reorder streams non-deterministically without any
functional effect. Leave it enabled everywhere else -- a silenced check's
difference is still computed and shown under `-v`.
