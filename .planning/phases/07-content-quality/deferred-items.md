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

## 07-08: the `tol` and `dist` comparators ignore `--tol` and `[tolerance]` overrides

Found during 07-08 Task 2, while testing the documented tune knob of `content.video.perceptual`. Pre-existing (Phase 2),
confirmed on a binary built from `47edaa2` (before this plan): `mediadiff compare size_crf20.mp4 size_crf23.mp4
--profile sw-encoder --tol size.file=90%` still reports `fail` ("delta -140424/1% beyond fail threshold") and only the
rendered tolerance changes to 90/1.

Cause: `src/core/policy.cpp` stores the layered result (profile, config `[tolerance]`, CLI `--tol`) in
`Policy::per_check[i].tolerance`, which the report renderer and `list-checks --effective` print, but
`src/compare/tol.cpp:88` and `src/compare/dist.cpp:52` call `parse_tolerance(check.tolerance_for(policy.profile), ...)`
and never read the resolved value. So an override is validated (a wrong unit is a usage error) and displayed, never
applied to a verdict.

Why it was not fixed in 07-08: it affects every `tol`/`dist` check, not just the one this plan adds, and changes what
existing users' configs do. The fix is small (resolve the tolerance through the policy entry when present, fall back
to the profile text for a hand-built policy) but wants its own plan, tests per comparator, and a look at the Phase 2
goldens. `docs/checks/content.video.perceptual.md` states the limitation instead of promising the knob.
