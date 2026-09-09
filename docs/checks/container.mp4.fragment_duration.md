# container.mp4.fragment_duration

## What it measures

The MEDIAN fragment duration of a fragmented MP4, as an exact rational
value in the primary video stream's own timebase -- derived from the
distance between consecutive keyframe DTS values (each fragment's own
`moof` begins at a keyframe under `frag_keyframe` muxing), sorted and
median-selected without ever converting to floating point. For an even
number of observed fragment durations, the LOWER of the two central values
is reported (never their mean), so the reported value is always exactly
one observed duration.

This check does not apply to a progressive file (`moof_count == 0`); it
auto-skips there as `skipped:not_applicable_container`, matching
`container.mp4.fragmentation`'s own mode signal.

## Why it matters

CMAF/low-latency delivery and player buffering assumptions are built
around a roughly consistent fragment duration. A muxer configuration
change (a different `-frag_duration`, a different keyframe interval) that
drifts the actual fragment length can degrade startup latency or trigger
unexpected rebuffering in a low-latency player even though no single frame
changed.

## Accept / Tune / Silence

### Accept

If the fragment-duration drift was intentional (e.g. deliberately
retuning the encoder's GOP/keyframe interval for a new delivery profile),
re-run `mediadiff snapshot` on the new candidate to establish it as the
new baseline.

### Tune

Adjust the check's tolerance in `[tolerance]` (default ±20%, matching
CMAF/low-latency players' typical drift budget) if a pipeline's own
fragment-duration jitter is legitimately wider or narrower than that
default.

### Silence

Set `container.mp4.fragment_duration` to `ignore` in `[severity]` for a
pipeline stage where fragment timing is deliberately variable and already
verified safe for its downstream player. Leave it enabled everywhere
fragment timing matters for playback -- a silenced check's difference is
still computed and shown under `-v`.
