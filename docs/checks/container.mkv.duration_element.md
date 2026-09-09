# container.mkv.duration_element

## What it measures

Whether a Matroska/WebM file's `Info` element carries a `Duration`
sub-element at all -- a `presence` check (doc 01 section 3): both sides
absent or both sides present is a clean pass; either side flipping from
absent to present (or vice versa) is the finding. The actual duration VALUE
is never read or compared here; only its presence.

## Why it matters

A missing `Duration` element means an unfinalized or streamed mux -- the
muxer wrote clusters as media arrived and never went back to patch in a
known total duration (the same condition that also typically leaves the
file's `Segment` size and its seek index unfinalized). This is a real,
observable difference in what a downstream player or consumer can expect
from the file: many players show no total-duration UI, cannot seek past
"now", and treat the file as effectively live, even when the underlying
media content is identical to a properly finalized sibling.

## Accept / Tune / Silence

### Accept

If the presence change was intentional (e.g. deliberately switching a
pipeline stage to produce streamed, unfinalized output for a live-ingest
use case), re-run `mediadiff snapshot` on the new candidate to establish it
as the new baseline.

### Tune

Not applicable -- `presence` carries no tolerance; the element either
exists on both sides, neither side, or the comparison flags the
asymmetry.

### Silence

Set `container.mkv.duration_element` to `ignore` in `[severity]` for a
pipeline that intentionally and routinely produces unfinalized/streamed
output and does not care whether a consumer can see a total duration.
Leave it enabled everywhere a finalized, seekable file is expected -- a
silenced check's difference is still computed and shown under `-v`.
