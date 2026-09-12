# video.frame_types

## What it measures

The I/P/B (and S/SI/SP/BI) picture-type distribution across a video
stream's own access units, as a proportion histogram compared by the
`dist` semantic. This check emits raw bin COUNTS per picture type, never a
pre-divided percentage: the `dist` comparator normalises both sides into
exact rational proportions itself, over their own respective totals, and
compares the maximum per-bin difference. Bin names are libav's own
single-letter picture-type spelling (`av_get_picture_type_char`: `I`, `P`,
`B`, `S`, `i`, `p`, `b`).

VIDEO-12's own degradation path: a codec with no registered libav parser
still reports a real, reduced-fidelity histogram -- a two-bin
`keyframe`/`non_keyframe` split derived from the packet scan's own
keyframe flag, which is available even when the parser is not. This is
never a skip: doc 03's own design calls for the distribution to degrade to
keyframe-flag granularity, not disappear, since the packet scan already
has the information a skip would discard. Evidence on the degraded path
records that the granularity is reduced, which source produced it, and
why. `skipped:partial_scan` covers a truncated scan on either path (D-02:
a distribution derived from a truncated sweep is a confidently wrong
shape).

## Why it matters

A sudden disappearance of B-pictures, or a shift in the I-frame
proportion, is one of the most common fingerprints of a silent encoder
configuration regression: a preset change that drops B-frame support, a
rate-control retune, or a hardware encoder falling back to a simpler GOP
structure under load. None of these necessarily change the resolution,
codec, or even the keyframe cadence (`video.gop.length`) -- the picture-type
MIX itself is often the only visible signal, and it is easy to miss by eye
across a long comparison report without a check dedicated to it.

## Accept / Tune / Silence

### Accept

If the picture-type distribution change was intentional (a deliberate
preset change, a switch to a B-frame-free low-latency profile, or a
retune for a different rate/quality trade-off), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

The default `±5%` tolerance bounds the WORST single bin's proportion
drift between baseline and candidate. `[check.profile_severity]` escalates
this to `fail` under `strict_bitexact` and `remux`: neither profile expects
ANY encoder-level change at all, so a picture-type shift under either is
exactly the silent configuration regression this check exists to catch,
not ordinary encoder variance. `[check.profile_tolerance]` widens the
tolerance to `10%` under `hw_encoder`: hardware rate control legitimately
redistributes picture types more aggressively than a software encoder
would for the same nominal settings, so the same drift that is suspicious
under `sw-encoder` is expected there. Adjust further via
`[check.tolerance]` (or `--tol video.frame_types=<value>`) for a pipeline
with its own tighter or looser expectation.

### Silence

Set `video.frame_types` to `ignore` in `[severity]` for a pipeline with no
dependency on picture-type mix (e.g. one that already gates on
`video.gop.length`/`video.gop.closed` and treats the finer-grained I/P/B
split as noise). Leave it enabled everywhere else -- a silenced check's
difference is still computed and shown under `-v`.
