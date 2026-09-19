# timeline.timecode.value

## What it measures

The exact SMPTE timecode string the `tmcd` track publishes -- split from
`timeline.timecode` (the presence check) per this project's own
one-semantic-per-id convention, mirroring `video.hdr.mdcv`/`video.hdr.mdcv.
luminance`'s split. The compared value is the RAW BYTE SEQUENCE libav
publishes, taken directly from `AVStream::metadata["timecode"]` after the
header pass alone -- never parsed into hour/minute/second/frame fields,
never normalised, trimmed, or case-folded, and never reconstructed from
components.

This matters concretely for the drop-frame flag. A drop-frame SMPTE
timecode is conventionally rendered with a semicolon before the frame
field (`HH:MM:SS;FF`) instead of a colon (`HH:MM:SS:FF`) -- confirmed
empirically against this project's own pinned fixture generator. Because
the compared value is the exact string, that punctuation is part of what
gets compared: a drop-frame and a non-drop-frame timecode at the SAME
nominal `HH:MM:SS:FF` position compare as DIFFERENT, exactly as they
should -- they name different real-world instants once the drop-frame
correction is applied. Evidence also surfaces this explicitly as a
`drop_frame` boolean, derived from the same punctuation, so a reader does
not have to parse the string by eye to see which form a file uses.

A file with no `tmcd` track reports this check as `Absent{}`, never an
empty string -- `compare`'s `exact` semantic treats an absent-versus-string
pair as a genuine difference (`src/compare/exact.cpp`'s structural
`Value::operator==`), so a timecode track that disappears between two
builds is caught here too, independently of the presence check above.

Like its sibling, every finding carries `unreachable_sources` in evidence
naming the two structurally-unreachable sources (S12M packet side data,
MPEG-2 GOP timecode) with the reason `requires_decode` -- see
`timeline.timecode`'s own `## What it measures` for the full explanation
of why neither can fire in this build.

## Why it matters

The timecode's PRESENCE tells you whether a track exists at all;
its VALUE tells you whether it still addresses the same frame. A remux,
trim, or re-encode that shifts the start timecode -- even by one frame --
breaks frame-accurate addressing for any downstream tool that assumes the
timecode is stable across a pipeline stage. This check catches that shift
even when the track itself survives intact.

## Accept / Tune / Silence

### Accept

If the timecode value change was intentional (a deliberate re-slate, a
corrected start offset, or a legitimate trim that moves the first frame),
re-run `mediadiff snapshot` on the new candidate to establish it as the
new baseline.

### Tune

This check has no tolerance -- it is an `exact` byte-for-byte string
match, deliberately: parsing the string into fields and comparing them
separately would silently discard the drop-frame punctuation this check
exists to preserve. The only lever is severity, below.

### Silence

Set `timeline.timecode.value` to `ignore` in `[severity]` for a pipeline
with no frame-accurate timecode dependency downstream. Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
