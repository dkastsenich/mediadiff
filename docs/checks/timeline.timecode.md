# timeline.timecode

## What it measures

Whether the file carries a SMPTE timecode -- a `presence`-semantic check
over exactly one source: the QuickTime/MP4 `tmcd` timecode track. This
phase reads the starting timecode string the MOV/MP4 demuxer resolves
during header parsing itself (`AVStream::metadata["timecode"]`), with ZERO
decode calls. The value comes from the FIRST stream (by array order) whose
codec tag identifies it as a `tmcd` track; a file with no such track
reports this check as `Absent{}` -- an explicit, comparable absence, never
an empty string, so a timecode that disappears between two builds is
reported.

SMPTE timecode has two other named sources this project does NOT detect,
and it is important to say so plainly rather than let their absence look
like a bug:

- **S12M packet side data** (`AV_PKT_DATA_S12M_TIMECODE`) -- in this
  build's linked FFmpeg, the only file that ever attaches this side data
  is `libavdevice/decklink_dec.cpp`, a live capture-card driver. This
  project does not link `libavdevice` at all (the LGPL decode-only feature
  list has no `avdevice` entry), so no file this tool can ever open --
  MP4, Matroska, or MPEG-TS -- can produce this side data. There is no
  code path here to enable, now or later, without adding a dependency this
  project deliberately excludes.
- **MPEG-2 GOP timecode** (`AV_FRAME_DATA_GOP_TIMECODE`, embedded in the
  GOP header of an MPEG-2 elementary stream) -- this is populated only on
  a DECODED frame. No no-decode inspection point (format tag, per-stream
  metadata, or first-packet side data) ever surfaces it. This project's
  probe/packet-scan passes never decode a frame, so this source stays
  `requires_decode` in this phase and becomes reachable only once a decode
  pass exists (a future phase).

Every finding this check emits carries `unreachable_sources` in evidence,
naming both of the above with the reason `requires_decode`, so `mediadiff
explain timeline.timecode` gives a direct answer to "why wasn't my MPEG-2
GOP timecode (or broadcast S12M timecode) detected?" rather than leaving a
user to infer it from silence.

## Why it matters

A SMPTE timecode is how broadcast and post-production pipelines address a
specific frame unambiguously across a chain of tools. Losing the timecode
track during a remux or transcode -- or having it silently reset to a
different start value -- breaks that addressing scheme for every
downstream tool that relies on it (editorial conform, closed-caption
alignment, multi-camera sync). A diff tool that cannot tell "this file
never had a timecode track" apart from "the timecode track just vanished"
is not useful for catching that regression.

## Accept / Tune / Silence

### Accept

If the timecode track's presence (or absence) reflects an intentional
change (a deliberate strip during a mezzanine export, or a newly added
broadcast timecode), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

There is no tolerance to tune for presence itself -- `presence` semantics
mean the check passes iff both sides are absent or both are present. The
sibling check `timeline.timecode.value` carries the exact-value
comparison.

### Silence

Set `timeline.timecode` to `ignore` in `[severity]` for a pipeline with no
timecode-aware downstream consumer at all. Doc 04's own note applies in
the other direction too: a broadcast configuration that needs this to gate
the merge can raise it with `--severity timeline.timecode=fail`. Leave it
enabled everywhere else -- a silenced check's difference is still computed
and shown under `-v`.
