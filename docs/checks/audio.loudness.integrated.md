# audio.loudness.integrated

## What it measures

EBU R128 integrated loudness (BS.1770 K-weighted, gated) for every audio stream, computed by
libebur128 in `EBUR128_MODE_I` from the SAME shared decode sweep `content.audio.sample_hash`
consumes -- no second decode. Every channel's role is mapped explicitly from the decoded
`AVChannelLayout`'s own per-position codes into libebur128's channel enum before a single sample
is fed: libebur128 has no knowledge of `AVChannelLayout` on its own, and the surround channels
carry a +1.5 dB weighting that a wrong (or default) mapping would silently misapply on exactly the
5.1(side) content this project's own corpus already flags.

The compared value is a quantised `RationalValue` at a fixed denominator (1000, i.e. milli-LU),
rounded half away from zero -- never a raw `double`: `src/compare/tol.cpp`'s `tol` comparator only
extracts a magnitude from `rational`/`int64` values, and a `real` value_kind reaches its own
internal-error arm. libebur128's raw `double` still rides in this check's evidence
(`integrated_lufs`), rounded to three decimal places for a stable, byte-identical-across-runs
report.

**The gating floor.** Below **-70 LUFS**, the compared value is NOT the real (softer) below-floor
reading -- it is the SAME quantisation of the fixed **-70.0 LUFS** floor constant itself, doc 05
section 4's own `silent` spelling. This is deliberate: two different tracks that both measure well
under -70 LUFS (say, -85 and -110 LUFS) would otherwise compare as a spurious "difference" purely
because two different kinds of near-silence happen to differ numerically -- reporting the SAME
fixed sentinel for both makes an all-silent track compare `pass` against another all-silent track,
while a track that CROSSES the floor (silent on one side, audible on the other) still reports a
real, gating difference (a `silent`-vs-numeric-LU comparison never accidentally lands "within
tolerance").

A stream that never decoded, or decoded to zero samples, reports `skipped:requires_decode` or
`skipped:insufficient_data` respectively -- never a fabricated 0 LUFS.

**`decode_path_class` evidence (diagnostic only).** Like `audio.loudness.true_peak`, every
measurement's evidence carries D-05's `decode_path_class` key, recording which decoder determinism
class produced the reading. It changes no verdict -- see `audio.loudness.true_peak.md` for the full
note, including why the gate once built on this key was reverted.

## Why it matters

Loudness is the one measurement that catches a silent re-level -- an enhancement stage, a loudness
normalizer, or an accidental gain change quietly altering the mix -- that no container or stream
parameter check can see. A remux, a lossless transcode, or a container hop should never move
integrated loudness at all; the tight `remux` tolerance below reflects that. A lossy re-encode or
an intentional mastering pass can move it slightly through ordinary encoder rounding, which the
default and `transform` tolerances accommodate without going silent on a real re-level.

## Accept / Tune / Silence

### Accept

If the loudness change was an intentional re-master or normalization pass, re-run
`mediadiff snapshot` on the new candidate to establish it as the new baseline.

### Tune

The default `0.5LU,1.0LU` two-threshold tolerance (`--tol audio.loudness.integrated=<warn>,<fail>`)
bounds how much integrated loudness may move between baseline and candidate. `remux` tightens to
`0.1LU` (D-13's own float-decode-noise allowance across FLAC/PCM fixtures) since a remux should not
move loudness at all; `transform` widens to `1.0LU` since an intentional transcode may legitimately
re-level the mix somewhat.

### Silence

Set `audio.loudness.integrated` to `ignore` in `[severity]` for a pipeline where loudness
normalization is an expected, unreviewed step of every run. Leave it enabled everywhere else -- a
silenced check's difference is still computed and shown under `-v`.
