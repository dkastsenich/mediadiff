# video.hdr.mdcv.primaries

## What it measures

The mastering display's eight chromaticity values -- the three display
primaries' (red, green, blue) x/y coordinates plus the white point's x/y
coordinate -- from the same stream-level side data `video.hdr.mdcv`
reads. Split into its own id from `video.hdr.mdcv` for the identical
reason `video.hdr.mdcv.luminance` was: a `presence` check never compares
values.

Doc 03 section 4 specifies these chromaticities compare under a 0.0002
absolute tolerance. This project's check registry allows exactly one
scalar comparator per check, and eight chromaticities cannot be reduced
to one scalar without losing per-value attribution -- so this check
instead QUANTISES each chromaticity to the nearest 1/5000th (0.0002) via
integer arithmetic, renders all eight quantised values into one canonical
string, and compares that string `exact`. Two chromaticities within the
same 0.0002 bucket quantise identically and compare equal; two that
differ by more than that quantise to different integers and compare
unequal -- the same tolerance semantics as a literal ±0.0002 `tol` check
would give, expressed instead as fixed-epsilon integer quantisation so
the result is byte-identical across every platform and every run (no
floating-point division anywhere in the path).

**Midpoint rounding rule:** a chromaticity landing EXACTLY on a grid
boundary (an exact half of 0.0002) rounds AWAY FROM ZERO. This is a fixed,
documented rule specifically so that boundary case is deterministic on
every platform -- an implementation that rounded to nearest-even, or that
rounded inconsistently depending on floating-point representation, would
produce a snapshot that differs between two runs on the identical input,
which this project's determinism requirement (byte-identical `--json`
across every run) forbids.

Evidence carries the RAW, unquantised rationals (never just the quantised
canonical string), so a user inspecting `-v` output can see the real
mastering values behind the comparison.

When there is nothing to measure -- the same three sub-cases as
`video.hdr.mdcv.luminance`, substituting `has_primaries` for
`has_luminance` -- this check emits the same named skip, never `Absent`.

## Why it matters

The display primaries and white point define the actual color gamut the
content was graded within. A shift large enough to matter (more than the
±0.0002 tolerance) means color coordinates a display or color-managed
pipeline uses to map the content's intended gamut into its own have
genuinely moved -- colors that were meant to sit exactly at the edge of
the mastering gamut now render slightly differently. Unlike luminance,
there is no legitimate re-derivation process expected to shift these
values at all; the small tolerance exists purely to absorb the
quantisation this format itself already applies (the values arrive
pre-quantised from an integer SEI/box encoding), not to tolerate real
drift.

## Accept / Tune / Silence

### Accept

If the primaries change reflects an intentional re-grade to a different
gamut, re-run `mediadiff snapshot` on the new candidate.

### Tune

The 0.0002 grid is fixed -- this is `exact` semantics over an already-
quantised canonical value, not a `tol` check, so there is no
`[tolerance]` override to set. Adjust `[severity]` instead if a pipeline
needs a different response than `fail`.

### Silence

Set `video.hdr.mdcv.primaries` to `ignore` in `[severity]` for a pipeline
with no gamut-sensitive downstream consumer. Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
