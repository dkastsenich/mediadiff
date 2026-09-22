# audio.loudness.true_peak

## What it measures

The maximum true peak level, in dBTP, over every channel of an audio stream, computed by
libebur128's `EBUR128_MODE_TRUE_PEAK` (an oversampled, inter-sample peak estimate per ITU-R
BS.1770) from the SAME shared decode sweep `audio.loudness.integrated` and
`content.audio.sample_hash` consume -- no second decode. Read out as the linear maximum over every
channel (`ebur128_true_peak`), converted to dBTP via `20*log10`, then quantised to a `RationalValue`
at the same fixed milli-unit denominator `audio.loudness.integrated` uses, for the same reason:
`src/compare/tol.cpp`'s `tol` comparator only extracts a magnitude from `rational`/`int64` values.
The raw `double` rides in this check's evidence (`true_peak_dbtp`) at fixed (three-decimal)
precision.

**The asymmetric -1.0 dBTP ceiling rule.** Every measurement's evidence carries a `ceiling_state`
key -- `"under"` or `"above"` the named -1.0 dBTP constant. When a comparison's BASELINE reads
`under` and its CANDIDATE reads `above`, the finding escalates to `fail` regardless of whether the
numeric delta itself fits the declared tolerance: a candidate that newly crosses -1.0 dBTP has
lost the headroom a downstream lossy encode typically needs to avoid clipping, which is a real risk
even when the raw dB movement is small. The reverse direction -- a candidate that DROPS from
`above` to `under` -- never escalates: gaining headroom is not a regression. Two files that are
BOTH already above the ceiling compare on ordinary tolerance alone; an unchanged, already-hot
master is not itself a new finding.

This rule is implemented as ONE generic, evidence-shape-gated escalation in
`src/compare/tol.cpp` (the same mechanism `timeline.av_offset`/`timeline.av_drift` already use for
their own priming-basis and span-basis overrides) -- never gated on this check's own id, and never
a second `state`-semantic check id. A `state`-semantic id would fire whenever EITHER side's value
is flagged, including on an UNCHANGED pair that is already above the ceiling on both sides -- a
false positive on stable content, which this project treats as a P0-class bug.

A stream that never decoded, or decoded to zero samples, reports `skipped:requires_decode` or
`skipped:insufficient_data` respectively -- never a fabricated true peak reading.

**`decode_path_class` evidence (diagnostic only).** Every measurement's evidence carries
`decode_path_class` -- D-05's own key (`src/analyzers/content/sample_hash.cpp`'s
`content.audio.sample_hash`), reused here verbatim -- recording which decoder determinism class
produced the reading (`class1`, `class2 <signature>`, or `class3`). It **changes no verdict**: this
check compares on its declared tolerance alone, at full sensitivity, on every decode path. The key
exists so that a surprising reading can be attributed: knowing whether a value came off a
proven-bit-exact decoder is the first thing worth knowing when the same file reads differently on
two machines.

A cross-platform decode-noise gate on this key was briefly added (`f7ce12d`) and then reverted. Its
premise -- that libebur128's floating-point true-peak computation is not bit-identical across
platforms on non-class-1 paths -- was disproven by debug session `true-peak-cross-platform`
(`.planning/debug/resolved/true-peak-cross-platform.md`). The observed cross-leg divergence came
from the FIXTURE: media fixtures are gitignored and regenerated on every CI runner, and the one
affected recipe carried a resampler whose per-architecture SIMD made each leg's bytes differ. The
check was reporting that difference correctly. If this check ever again disagrees across platforms,
establish that both sides are the same bytes BEFORE suspecting the measurement.

## Why it matters

Headroom loss is the risk a `warn`-severity ordinary tolerance excursion alone would understate: a
candidate whose true peak newly crosses -1.0 dBTP is at real risk of intersample clipping the next
time it passes through a lossy codec, even if the measured delta from the baseline is tiny. The
asymmetric rule matches this real-world risk profile exactly -- headroom lost is worth escalating
attention to; headroom gained is not.

## Accept / Tune / Silence

### Accept

If the peak-level change (and any ceiling crossing) was an intentional mastering decision, re-run
`mediadiff snapshot` on the new candidate to establish it as the new baseline.

### Tune

The default `0.3dB` tolerance (`--tol audio.loudness.true_peak=<tol>`) bounds an ordinary peak-level
excursion at `warn` severity. The asymmetric ceiling escalation itself has no tunable threshold
beyond the named -1.0 dBTP constant this doc names -- it is not expressed in the tolerance grammar,
since it is a directional (not magnitude) rule.

### Silence

Set `audio.loudness.true_peak` to `ignore` in `[severity]` for a pipeline that does not target a
lossy delivery codec downstream (headroom loss carries no real risk there). Leave it enabled
everywhere else -- a silenced check's difference, including the ceiling escalation, is still
computed and shown under `-v`.
