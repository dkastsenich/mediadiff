# Phase 7 deferred items

## 07-07: video.hdr.coherence still reads stream-level metadata only

Found during 07-07 Task 2. `video.hdr.coherence` (04-12, VIDEO-10, closed four-value vocabulary approved in
04-CHECK-ROSTER.md) classifies from `StreamInfo::mdcv_present` / `cll_present`, which are the stream-level arm only.
After 07-07, `video.hdr.mdcv` reports `present` with `source: "frame"` for `video_pcm_hdr.h264`, but
`video.hdr.coherence` for the same stream still says `pq_without_mdcv` ("transfer 'smpte2084' is PQ but no
mastering-display metadata is present"). 07-06's writer comment calls that fixture "a coherent HDR10 set, so
video.hdr.coherence sees no incoherence", which is not what the check reports today.

Why it was not fixed in 07-07: the plan does not touch coherence, and the fix is a design decision, not a one-line
change. Making coherence frame-aware when the decode pass ran would make its value depend on which passes ran
(`--no-content` would say `pq_without_mdcv`, a default run `coherent`), which Phase 6 D-12 forbids; honouring D-12
needs either a skip state (the check is deliberately total and never skips) or a new member of the approved
vocabulary. Severity is `info` in every profile, so it never gates an exit code.

Needs a decision before it is changed: either (a) accept coherence as stream-level only and say so in its doc, or
(b) add an `indeterminate`-style outcome for "frame-capable codec, no stream-level entry, no decode result".
