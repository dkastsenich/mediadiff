# container.ts.cc_errors

## What it measures

The count of UNFLAGGED continuity-counter (CC) discontinuities across every
PID in the transport stream, summed into one number at global scope. A CC
error means a PID's own 4-bit continuity counter jumped in a way ISO
13818-1 section 2.4.3.3's carve-outs do not excuse -- payload-absent
packets are exempt from CC tracking entirely, exactly one repeated packet
per position is permitted, and a `discontinuity_indicator`-flagged reset is
counted separately by `container.ts.cc_discontinuities`, never here.
Evidence carries the per-PID breakdown (only PIDs with a non-zero count,
never all 8192), the first offending PID and its byte offset, and, when a
mux-rate estimate exists, that offset's ESTIMATED time (explicitly marked
as an estimate, never presented as measured).

## Why it matters

An unflagged CC discontinuity means the multiplexer dropped or duplicated a
packet without saying so -- every downstream demuxer/decoder has to guess
what happened, and the guess is usually a visible glitch (a dropped frame,
a corrupted GOP, an audio click) landing exactly where the counter broke.
This is the mechanism-level signal underneath "the stream looks damaged";
`exact 0` at `fail` severity means any nonzero count gates the merge,
independent of what the other side reports -- a damaged stream is a defect
regardless of whether the comparison side happens to share the same defect.

## Accept / Tune / Silence

### Accept

If the muxer/transcoder pipeline change genuinely reduces or introduces CC
errors and that is expected (e.g. moving off a known-broken remuxer), re-run
`mediadiff snapshot` to establish the new candidate as baseline.

### Tune

There is no tolerance to tune -- `exact 0` means any nonzero count fails.
If a specific, understood source of CC errors is acceptable for a given
pipeline (e.g. an intentionally lossy live-capture path), disable the check
for that pipeline via `[severity]` rather than trying to widen a tolerance
that does not exist for this check.

### Silence

Set `container.ts.cc_errors` to `ignore` in `[severity]` only for a
pipeline where continuity-counter integrity is known not to matter (e.g. a
deliberately lossy transcode whose output is never meant to be
demuxed/played directly). Leave it enabled everywhere else -- a silenced
check's difference is still computed and shown under `-v`.
