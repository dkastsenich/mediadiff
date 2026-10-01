# quality.psnr

## What it measures

How closely the candidate's decoded picture matches the baseline's, as a **peak signal-to-noise ratio**
in decibels, computed at the pictures' **native resolution** over the primary video stream (the first
video stream that is not cover art). It is opt-in: give `--psnr` to `compare` or `dir --content`. The
value is stored in milli-dB (`55250/1000` is 55.25 dB).

**The definition.** For each paired frame the error is summed **exactly** per plane (an unsigned 64-bit
sum of squared sample differences) and the three planes are combined by **sample count**: one combined
mean-squared error over all luma plus chroma samples (`(SSE_Y + SSE_U + SSE_V) / (N_Y + N_U + N_V)`),
so a 4:2:0 frame weighs chroma by its real share of the samples. The frame's PSNR is
`10 * log10(peak^2 / MSE)` with `peak = 2^bits - 1`, computed by an **integer fixed-point logarithm** (no
floating point and no `libm`, so the `--json` bytes are identical across machines) and rounded half away
from zero to a milli-dB. The per-plane mean PSNR (`y`, `u`, `v`; a gray picture has only `y`) rides in
evidence as `per_plane`.

**The cap.** Every frame's PSNR is capped at `(6 * bits) + 12` dB: **60 dB at 8 bits, 72 dB at 10 bits**
(libvmaf's convention). Identical frames score exactly the cap, so no infinite value ever reaches the
JSON, the baseline's self-score is that cap, and the delta of identical media is exactly 0.

**Live comparison only (D-01).** A quality score needs both pictures, so this check exists only in a
`compare` of two media files. The baseline records its **self-score** (the cap) and the candidate
records its PSNR *against the baseline*, with the baseline's XXH3-128 input identity in evidence as
`reference_identity`. The check therefore reads as a delta: baseline cap minus candidate mean.
Every one-sided probe -- `snapshot`, `inspect`, and the snapshot side of a compare -- reports
`skipped:requires_media` on both sides, and a snapshot **never stores a score**. Without `--psnr` a live
compare reports `skipped:not_requested`; under `--no-content` it reports `skipped:requires_decode`.

**What gates (D-03).** The compared value is the **mean** of the per-frame PSNR over the scored pairs.
The **minimum** frame (its value, both decode indices and the baseline PTS) and `identical_frames` ride
in evidence, so the worst frame is named even though it does not gate.

**Native geometry.** Both sides must have the same display width and height and the same plane layout
(gray, or YUV with the same chroma subsampling); otherwise the check reports
`skipped:geometry_mismatch` on both sides naming each side's frame (`352x288 yuv420p`), while
`content.video.perceptual` -- which scores a fixed-size thumbnail -- still runs. A different bit depth
is not a mismatch: the lower is promoted by an exact left shift (an 8-bit side against a 10-bit side
scores at 10 bits, and identical content scores the 10-bit cap). A pixel format with no comparable
luma/chroma samples (RGB, paletted, floating point) reports `skipped:insufficient_data` with
`reason: unsupported_pixel_format`.

**Which frames pair, and sampling.** The same time-based pairing as `content.video.perceptual` (D-02),
with the same `pairing`, `unpaired_*` and `pairing_fallback` evidence. `--sample N` scores every Nth
paired frame and records `sampling_state: sampled:N`.

**Path preconditions (TRUST-04, D-04).** Both sides carry `scaler_path` (always `native (no scaler)`:
nothing is rescaled) and `decode_path_signature` (library versions, build triplet, CPU flags and the
decoder settings). A pair whose records differ, or where only one side carries one, is
`skipped:path_incomparable`, even when the two PSNRs sit within tolerance.

**When it does not measure.** `skipped:not_requested` (no `--psnr`), `skipped:requires_media`
(one-sided probe, any snapshot), `skipped:requires_decode`, `skipped:partial_scan` (a side's scan or
decode stopped early: a prefix says nothing about the rest), `skipped:geometry_mismatch`,
`skipped:path_incomparable` and `skipped:insufficient_data` (no pair was scored, or the pairing stopped
early). A file with no video stream emits nothing.

## Why it matters

A re-encode can keep every container field and the bitrate while the picture degrades. PSNR is the
classic full-reference number for "how much noise did the pipeline add": a candidate that drops from the
cap to 41 dB has lost real detail, and the minimum frame says where.

## Accept / Tune / Silence

### Accept

If the drop is expected (a deliberate quality change), re-encode your baseline media from the new
pipeline so both sides come from the same settings. Because the value is baseline-referenced, accepting a
change means replacing the baseline file, never a stored number.

### Tune

The tolerance is a **dB difference**: the default is `0.5dB`, i.e. the candidate's mean may sit up to
half a dB below the baseline cap before the check fails. Write it with the `dB` suffix. The 60 / 72 dB cap
and the gating on the mean are fixed, not knobs. Severity is `fail`; tune it per profile or with
`[severity]`.

Known limitation, recorded on the project's ledger rather than hidden: the `tol` comparators evaluate
the registry and profile tolerance and do not yet apply a `--tol` or `[tolerance]` override to the
verdict (for this check and every other `tol` check); the override is validated and shown in the
resolved policy only. Until that is fixed, tune through severity.

### Silence

Do not pass `--psnr` (the default) and the check reports `skipped:not_requested`, or set
`quality.psnr` to `ignore` in `[severity]` for a pipeline whose pictures legitimately change each run. A
silenced check's difference is still computed and shown under `-v`.
