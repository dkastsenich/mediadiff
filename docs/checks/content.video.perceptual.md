# content.video.perceptual

## What it measures

How much the picture changed between the two files, as the **worst frame**: the minimum, over every
paired frame, of an integer 8x8-window SSIM computed on each frame's deterministic 128-wide, 8-bit
luma thumbnail. The value is a score in the `score` unit (a bare number, `1` meaning identical),
stored as millionths: `987654/1000000` is 0.987654. Identical pictures score exactly
`1000000/1000000`, so identical media report a delta of exactly 0.

**Live comparison only (D-01).** A perceptual score needs both pictures, so this check exists only in
a `compare` of two media files. The baseline records its **self-score** (exactly `1000000/1000000`,
exact by construction of the integer formula) and the candidate records its score *against the
baseline*, with the baseline's XXH3-128 input identity in evidence as `reference_identity`. Every
one-sided probe -- `snapshot`, `inspect`, and the snapshot side of a compare -- reports
`skipped:requires_media`, so no stored number can ever be mistaken for a comparable score. Under
`--no-content` the check reports `skipped:requires_decode`. Accepted cost: with a committed-snapshot
baseline the `hw-encoder` gate on this check becomes `requires_media`, so keep the baseline media
around if you rely on it.

**Which frames pair (D-02).** Frames are paired by **presentation time**, each side measured from its
own first frame, within strictly less than half of the finer frame interval -- the same rule the
frame-hash divergence locator uses (`src/core/frame_pairing.h`). A frame dropped from one file leaves
exactly one frame unpaired instead of shifting every later pair. When either side has no timestamps
on its first frame, or either side's frame interval is unknown (MPEG-TS declares no frame rate at
open), the whole comparison pairs by decode index and says so (`pairing: index` with a
`pairing_fallback` reason). Unpaired frames on either side are counted in evidence
(`unpaired_baseline`, `unpaired_candidate`; `unpaired_no_pts` counts frames in a time-paired stream
that lacked a timestamp).

**What gates (D-03).** The compared value is the **minimum** pair score, so one wrecked frame cannot
hide in a mean. Evidence also carries `mean` (the floor of the mean score), `first_below_threshold`
(the first pair strictly below **0.985**: both decode indices, the baseline PTS and the score, or
`null` when no pair is below), and `worst` (the ten lowest pairs, score ascending, ties by baseline
index ascending). A pair scoring exactly 0.985 is not below the threshold; 0.984999 is.

**Cross-resolution.** Two sides score against each other when their thumbnails have the same shape:
352x288 against 704x576 both thumbnail to 128x104 and are compared; 352x288 against 320x240
(128x104 against 128x96) cannot be paired and report `skipped:geometry_mismatch` on both sides. Each
side's evidence records its `scaler_path` (the exact swscale algorithm, flags, destination size and
library version) and `decode_path_signature` (the library versions, build triplet, CPU flags and the
decoder settings), the record a later precondition compares.

**Sampling.** `--sample N` scores every Nth paired frame (`pairs_scored` is about `1/N` of the pairs)
and records `sampling_state: sampled:N`. The frozen and black detectors still see every frame; only
this score is thinned.

**Colour range is not normalized.** A range flip (limited to full) is a real difference that
`video.color.range` already reports; the score reflects the pixels as decoded.

**When it does not measure.** `skipped:requires_media` (one-sided probe), `skipped:requires_decode`
(`--no-content`, or the decoder could not be opened), `skipped:partial_scan` (either side's scan or
decode stopped early: a prefix's worst frame says nothing about the rest),
`skipped:geometry_mismatch` (above), and `skipped:insufficient_data` (no pair was scored, or a
thumbnail could not be made). A file with no video stream emits nothing.

## Why it matters

A re-encode can keep every container field, the duration and the bitrate and still ruin a few
seconds of picture. The minimum over all frames finds the one bad GOP that a clip-wide average hides,
and the `worst` list says where to look.

## Accept / Tune / Silence

### Accept

If the drop is expected (a deliberate quality change), re-encode your baseline media from the new
pipeline so both sides come from the same settings. The `sw-encoder` profile reports this check at
`info`; the `hw-encoder` and `transform` profiles make a drop below the tolerance fail.

### Tune

The tolerance is a score difference written as a **bare number with no unit suffix**: the default is
`0.015`, and `0.015dB` or `0.015none` is a usage error that names the bare form. The 0.985 threshold
behind `first_below_threshold` and the ten-entry worst list are fixed reporting constants, not knobs;
they change what is *listed*, never the verdict. Severity is tuned per profile (see Accept) or with
`[severity]`.

Known limitation, recorded on the project's ledger rather than hidden: the `tol` comparators evaluate
the registry and profile tolerance and do not yet apply a `--tol` or `[tolerance]` override to the
verdict (for this check and every other `tol` check); the override is validated and shown in the
resolved policy only. Until that is fixed, tune through severity.

### Silence

Set `content.video.perceptual` to `ignore` in `[severity]` for a pipeline whose pictures are
legitimately rewritten each run (a watermark that changes, a generative source). A silenced check's
difference is still computed and shown under `-v`.
