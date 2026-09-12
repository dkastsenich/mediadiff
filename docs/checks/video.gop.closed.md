# video.gop.closed

## What it measures

Whether a video stream's GOP structure is `"closed"` or `"open"`, classified
by reading the leading VCL NAL type of every access unit -- NEVER the
parser's own `key_frame` boolean, which cannot make this distinction. An
access unit is a real IDR when its leading VCL NAL type is H.264 type 5 or
HEVC type 19/20; it is a non-IDR random-access point when the type is HEVC
16 through 23 excluding 19/20 (the BLA/CRA family); it is a non-IDR intra
picture when the type is H.264 1 and the parsed picture type is I. A stream
whose random-access points are all real IDRs is `"closed"`; a stream with
any CRA, BLA or non-IDR intra random-access point is `"open"`.

This distinction is invisible to `key_frame` alone: HEVC's own parser sets
`key_frame = 1` for ANY IRAP NAL, including `CRA_NUT` -- a CRA-led (open)
stream and an IDR-led (closed) stream can report the IDENTICAL `key_frame`
pattern while their NAL types differ completely. The H.264 parser has its
own, unrelated trap in the other direction: a non-IDR I slice with a low
reference-frame count is heuristically flagged as a keyframe too. Reading
the NAL type directly is the only way to tell these apart.

A stream with no random-access point at all skips `insufficient_data`.
`skipped:no_parser` covers both a codec with no registered libav parser and
a codec with no NAL layer at all (e.g. `mpeg4`), with the codec named in
evidence in the second case. `skipped:partial_scan` covers a truncated
scan.

## Why it matters

An open GOP cannot be cut cleanly at every keyframe: a segment boundary
placed at a non-IDR random-access point (a CRA, a BLA, or a heuristically-
flagged non-IDR I slice) produces a segment that does not decode correctly
from its own first frame, because that frame's own reference structure can
still point outside the segment. A packager, editor or HLS/DASH pipeline
that assumed closed GOPs -- the far more common case -- silently produces
broken segment boundaries the moment an encoder switches to open-GOP
structure, and the failure often does not surface until playback, far from
the point where the encoder configuration actually changed.
`video.gop.closed` is what turns that into a merge-time finding instead of
a player-side bug report.

## Accept / Tune / Silence

### Accept

If the open/closed change was intentional (a deliberate switch to
open-GOP structure for coding-efficiency reasons, or away from it for
segment-boundary safety), re-run `mediadiff snapshot` on the new candidate
to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` semantics means any change
between `"open"` and `"closed"` is reported. Severity is `fail`, not
`warn`: an open-GOP regression is a real, silent segment-alignment defect
in the class of failures this project treats as high-severity by default;
demote it to `warn` in `[severity]` for a pipeline that never packages
segments at all (e.g. a pure archival transcode with no downstream
HLS/DASH consumer).

### Silence

Set `video.gop.closed` to `ignore` in `[severity]` for a pipeline with no
segment-alignment dependency on GOP openness whatsoever. Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
