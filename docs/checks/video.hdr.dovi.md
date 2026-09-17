# video.hdr.dovi

## What it measures

Whether a video stream carries a Dolby Vision configuration record (an
ISOBMFF `dvcC`/`dvvC` box, or its Matroska/TS equivalents) -- the profile,
level, and the presence flags for RPU, enhancement-layer and base-layer
data.

A Dolby Vision configuration record is a stream-level box, read from
`codecpar->coded_side_data` (`AV_PKT_DATA_DOVI_CONF`) -- populated by the
demuxer at OPEN time, the same seam `video.hdr.mdcv`/`video.hdr.cll` read.
No decode pass is needed or performed here.

A Dolby Vision configuration record's presence or absence is a
distribution-affecting fact independent of whether the RPU (reshaping
metadata) itself changed: a player, receiver or set-top box decides
WHETHER to engage its Dolby Vision decode path based on whether this
record exists at all, before it ever looks at a single RPU byte. Losing
the record entirely -- even if the underlying picture content is
byte-identical -- silently downgrades playback to the base layer only.

This check reuses the SAME could/could-not-carry-frame-level-metadata
decision `video.hdr.mdcv`/`video.hdr.cll` already use: an absence on a
codec that could never carry Dolby Vision data any other way (`mpeg4`,
`mpeg2video`) is a genuine, permanent `Absent`; an absence on a codec that
COULD (`hevc`, `av1`) is reported as `skipped:requires_decode` -- a real
possibility this phase cannot check without a decode pass, never presented
as though it had been checked and found absent.

**v1 scope:** this check (and its sibling `video.hdr.dovi.config`) compares
the configuration record only. Per-frame Dolby Vision RPU diffing is
explicitly out of scope -- no combination of this project's LGPL-only
toolchain can produce genuine RPU-bearing Dolby Vision test fixtures (no
grading tool exists anywhere in this project's stack), and no RPU parsing
exists in this codebase.

Evidence, when present, carries the configuration record's version,
profile, level, the three presence flags, the signal-compatibility id, the
metadata-compression value, and the `source` tag. When absent, evidence
carries the codec name and whether it could carry frame-level metadata.

## Why it matters

A Dolby Vision-capable receiver decides its whole decode path based on
whether a `dvcC`/`dvvC` record exists. A remux, container conversion, or
transcode that drops the box silently downgrades every downstream Dolby
Vision device to SDR/HDR10 base-layer playback -- a real, user-visible
quality regression that a byte-diff of the picture content alone would
never surface, since the base layer itself may be unaffected.

## Accept / Tune / Silence

### Accept

If the Dolby Vision presence change was intentional (a deliberate strip of
DOVI metadata, or a new encode that adds it), re-run `mediadiff snapshot`
on the new candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune for presence itself -- `presence` semantics
mean the check passes iff both sides are absent or both are present. The
sibling check `video.hdr.dovi.config` carries the value-level comparison.

### Silence

Set `video.hdr.dovi` to `ignore` in `[severity]` for a pipeline with no
Dolby Vision-aware downstream consumer at all. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
