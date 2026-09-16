# timeline.discontinuities.flagged

## What it measures

The spans of an MPEG-TS stream's own presentation timeline where a jump
between two consecutive presentation timestamps strictly exceeds the same
250 ms threshold `timeline.discontinuities` uses, but where the transport
packet on the far side of the jump carries `discontinuity_indicator=1` --
the muxer's own on-the-wire declaration that it broke the timeline
deliberately (a splice point, a re-multiplex boundary), not by accident.

**Attribution rule.** `PacketRecord::pos` is the byte offset libav reports
for a DEMUXED packet (typically a whole PES packet); the
`discontinuity_indicator` flag lives on a single 188-byte MPEG-TS
TRANSPORT packet somewhere inside that PES packet's own byte range. A jump
is attributed to flagged structure when at least one recorded
`discontinuity_indicator` byte offset (`src/probe/ts_scan.h`,
`PidStats::discontinuity_indicator_offsets`, joined by this stream's own
PID) falls within `[pos, next_pos)` -- the demuxed packet's own byte
extent, bounded above by the byte offset of the NEXT packet on this stream
in read order. This is "the flagged transport packet falls within this
demuxed packet's byte range," a real but PES-boundary-approximate
attribution -- not a claim of per-transport-packet precision the join does
not actually have.

Every jump that is NOT attributed this way is reported instead by the
separate `timeline.discontinuities` check, gating; every jump on a non-TS
container is *always* unattributable (see below).

Each recorded span's `start`/`end` follow the exact same `RationalValue`-ms,
`span`-semantic shape as `timeline.discontinuities` (introduced spans
compare against this check's own `info` severity; removed spans are always
`info`). The interval walk itself runs over the UNWRAPPED presentation
timeline on a genuinely wrapping stream, matching every sibling `timeline.*`
check in this phase.

On a **non-TS** input this check reports `skipped:not_applicable_container`
-- never an empty span list. "No flagged structure exists here" (an empty
span list) and "this container cannot express flagged structure at all"
(this skip) are different facts, and conflating them would make an MP4
compare look identically clean to an MPEG-TS file that genuinely has no
flagged splices, which is not actually the same claim.

## Why it matters

A broadcaster-flagged splice (ad insertion, program boundary, re-anchor
from a different upstream source) is EXPECTED structure -- the muxer told
you, on the wire, that it did this on purpose. The identical presentation
jump, unflagged, is usually a real packaging defect: a lost segment, a
corrupted remux, or a pipeline stage that dropped packets without telling
anyone. Splitting the two into separate check ids (rather than one check
with a severity that varies per-finding) means a pipeline can gate hard on
real breakage while still being able to see -- and snapshot-track -- every
intentional splice point, without either signal drowning out the other.

## Accept / Tune / Silence

### Accept

A flagged splice is `info` by design and never gates the merge on its own
-- there is normally nothing to "accept." If the set of flagged splice
points itself changed in a way worth tracking, re-run `mediadiff snapshot`
on the new candidate.

### Tune

This check shares `timeline.discontinuities`' own fixed 250 ms detection
threshold -- not a tolerance, `--tol` has no effect on which intervals are
recorded. `--severity timeline.discontinuities.flagged=warn` (or any other
severity) changes how a flagged jump gates the exit code, but never which
jumps get attributed to flagged structure.

### Silence

Set `timeline.discontinuities.flagged` to `ignore` in `[severity]` for a
pipeline where flagged TS splice points are routine and never worth
surfacing at all. A silenced check's difference is still computed and
shown under `-v`.
