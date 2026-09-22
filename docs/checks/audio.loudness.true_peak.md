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

**Class-gated cross-platform comparison (06-13-PLAN.md deviation, human-decided).** Every
measurement's evidence also carries `decode_path_class` -- D-05's own decode-path precondition key
(`src/analyzers/content/sample_hash.cpp`'s `content.audio.sample_hash`), reused here verbatim. When
the resolved decoder is a proven bit-exact class-1 sibling (a fixed-point codec or PCM/FLAC), this
check stays at FULL sensitivity: any delta beyond the ordinary `0.3dB` tolerance is a real, gating
non-pass exactly as before. When the resolved decoder is class 2 or 3 (not proven bit-exact), the
shared decode sweep's downstream libebur128 floating-point true-peak computation is not proven
bit-identical across platforms EITHER -- confirmed by a real designated-leg run (CI run
35708992998): the same `timeline_drift_base.mp4` vs `timeline_drift_linear.mp4` pair
(`--profile sw-encoder`) measured a 0.100dB delta on `x64-linux` (comfortably under 0.3dB) but
exceeded 0.3dB on BOTH `x64-windows-static-md` and `arm64-osx` for the identical comparison. In that
case, a delta that exceeds the ordinary tolerance but still falls within a wider **cross-platform
decode-noise floor** (`src/compare/tol.cpp`'s `kCrossPlatformDecodeNoiseFactor`, currently 3x the
ordinary tolerance) reports `skipped:cross_platform_decode_noise` instead of a fabricated warn/fail
-- the SAME "degrade rather than lie" principle `content.audio.sample_hash`'s own
`skipped:hash_incomparable` already applies to a hash comparison, generalized here to a magnitude
comparator. A delta that exceeds even the widened floor is still a genuine, gating non-pass: this
override never fully silences the check, only the narrow band attributable to known cross-platform
decode noise. A genuine asymmetric ceiling crossing (above) always wins over this gate -- headroom
loss is a real risk regardless of decode-path noise.

`audio.loudness.integrated` carries the same `decode_path_class` evidence and is governed by the
same generic override, but its own default headroom (`0.5LU`/`1.0LU`, `remux` tightened to `0.1LU`)
is roughly two orders of magnitude larger than the measured cross-platform noise on this run (a
0.002 LU delta was observed on the SAME comparison, against a 1.0 LU fail threshold) -- so this gate
is not currently observed to change `audio.loudness.integrated`'s behavior. It is wired identically
anyway (never gated on `check.id`) so a future measurement that DOES expose it is caught the same
way, without a second, independently-tuned mechanism.

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
since it is a directional (not magnitude) rule. The cross-platform decode-noise floor's own
multiplier (`kCrossPlatformDecodeNoiseFactor`) is a fixed named constant in `src/compare/tol.cpp`,
not exposed via `--tol` -- it only ever widens what counts as "known decode noise" on a non-class-1
path, never the ordinary tolerance itself.

### Silence

Set `audio.loudness.true_peak` to `ignore` in `[severity]` for a pipeline that does not target a
lossy delivery codec downstream (headroom loss carries no real risk there). Leave it enabled
everywhere else -- a silenced check's difference, including the ceiling escalation, is still
computed and shown under `-v`.
