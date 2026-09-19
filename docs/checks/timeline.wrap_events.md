# timeline.wrap_events

## What it measures

Whether a stream's own MPEG-TS 33-bit PTS/DTS timeline wrapped -- a
`state`-semantic string per stream, on the raw, ContainerFamily::ts-scoped
timestamp sequence produced by this project's own asymmetric unwrap rule
(doc 04 section 1.2, `unwrap_ts_timestamps`). A stream whose raw PTS
sequence exhibits at least one wrap (a backward delta strictly more
negative than `-2^32`, the half-range guard) reports the flagged value
`ts_33bit_wrap`; a stream with zero wrap events reports the unflagged
value `no_wrap`.

Because the `state` semantic compares WHICH values are flagged, not
baseline-vs-candidate equality, a pair where **both** files wrap still
reports the flagged value as a non-`pass` finding -- under `exact`'s
baseline-equality rule the pair would compare `pass` (both sides identical)
and the shared wrap would be invisible. `pass` under this check means
NEITHER side was flagged, never "both sides agree" (mirrors
`video.hdr.coherence`'s own precedent, Phase 4 D-10).

Evidence carries `wrap_count` (the total number of wrap events detected on
this stream), and -- when at least one wrap occurred -- the FIRST wrapping
packet's own array index (`first_wrap_index`), its RAW pre-unwrap value
(`first_wrap_raw`), and its UNWRAPPED value (`first_wrap_unwrapped`), so a
reader can reconstruct the correction by hand (TIME-02's "raw values are
preserved in evidence" requirement, satisfied concretely here).

On a non-`ContainerFamily::ts` input this check reports
`skipped:not_applicable_container` -- the 33-bit wrap is an MPEG-TS-specific
property of the 90 kHz PES timestamp field; it has no meaning for MP4 or
Matroska. When the unwrap's own running-offset arithmetic overflows (an
input engineered to force unbounded offset growth, T-05-05), this check
reports `skipped:insufficient_data` rather than a wrapped or fabricated
value.

## Why it matters

A 33-bit PTS/DTS wrap is a normal, expected property of any sufficiently
long-running MPEG-TS stream (the field's own domain repeats roughly every
26.5 hours at 90 kHz) -- it is not, by itself, a defect. But a CANDIDATE
that wraps where the BASELINE did not (or vice versa) usually signals a
start-offset change further upstream in the pipeline: a splice, a
re-multiplex from a different absolute origin, or a genuinely different
recording session being compared as if it were the same one. Making the
wrap visible, even when it fires no other check, keeps that upstream
question answerable instead of silently absorbed into "the numbers still
line up."

## Accept / Tune / Silence

### Accept

If the wrap (or its absence) was an intentional consequence of a known
upstream change (e.g. a deliberate re-anchor of the stream's own PCR
origin), re-run `mediadiff snapshot` on the new candidate to establish it
as the new baseline.

### Tune

This check has no tolerance to tune -- it is a `state`-semantic presence
check, not a magnitude comparison. `--severity timeline.wrap_events=warn`
(or any other severity) changes how a flagged finding gates the exit code,
but never which packets get flagged.

### Silence

Set `timeline.wrap_events` to `ignore` in `[severity]` for a pipeline where
long-running MPEG-TS wraps are a routine, already-understood property of
the input (e.g. a 24/7 broadcast capture path). A silenced check's
difference is still computed and shown under `-v`.
