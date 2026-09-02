# container.mkv.cues_placement

## What it measures

Whether the `Cues` element (Matroska's own seek index) appears BEFORE or
AFTER the first `Cluster` in a Matroska/WebM file's top-level `Segment`
layout, determined by comparing the two elements' own byte offsets from the
same bounded EBML walk. The value is one of `front` (the index was reserved
or moved ahead of the media data), `end` (the muxer's own default: the
index is written last, after every `Cluster`), or `absent` (no `Cues`
element was located at all -- an unfinalized or streamed mux, doc 02
section 4's own case).

## Why it matters

This check does not apply to a fragmented MP4 style question at all -- it
measures a DIFFERENT mechanism entirely, which is exactly why it is
deliberately NOT called `faststart`: MP4's `faststart` (see
`container.mp4.faststart`) is a PLAYBACK-START-LATENCY property, because a
player generally cannot begin decoding an MP4 until it has read `moov`. A
Matroska/WebM player can begin decoding as soon as it sees `Tracks` --
playback starts fine with no `Cues` present at all. `Cues` only affects
SEEKING (jumping to an arbitrary timestamp without linear-scanning every
`Cluster`), not startup. Borrowing MP4's `faststart` name for this
mechanism would tell a reader the wrong story about what changed and why it
matters -- the parent design doc's own naming rule (section 3.5, rule 3)
forbids exactly this kind of cross-container name reuse.

A remux tool, a CDN transcoding step, or a change in muxer defaults can
silently flip this layout without touching a single sample -- invisible to
every frame-level or bitrate comparison.

## Accept / Tune / Silence

### Accept

If the layout change was intentional (e.g. deliberately moving to an
index-reserved layout for faster remote seeking), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; the placement either matches or it
doesn't.

### Silence

Set `container.mkv.cues_placement` to `ignore` in `[severity]` for a
pipeline that never seeks into these files remotely (e.g. local-only batch
transcoding where every file is always read start-to-finish). Leave it
enabled anywhere a player might seek -- a silenced check's difference is
still computed and shown under `-v`.
