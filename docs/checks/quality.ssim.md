# quality.ssim

## What it measures

How structurally close the candidate's picture is to the baseline's, as a single-scale **SSIM** of the
**luma plane at native resolution**, over the primary video stream (the first video stream that is not
cover art). It is opt-in: give `--ssim` to `compare` or `dir --content`. The value is a score in the
`score` unit (a bare number, `1` meaning identical), stored as millionths: `987654/1000000` is 0.987654.

**The definition.** The same integer 8x8-window SSIM as `content.video.perceptual` (windows every 4
pixels, the floor mean of the window scores, exact `int64` arithmetic, Q24 internally), but run on the
full-resolution luma of each frame instead of a 128-wide thumbnail. For bit depths above 8 the window
constants scale with `L = 2^bits - 1` (`C1 = (0.01 L)^2`, `C2 = (0.03 L)^2`, in integer arithmetic) and the
products are formed in exact wide integers, so nothing overflows and the result is identical on every
toolchain. Identical native frames score exactly `1000000` at 8 and at 10 bits, so the baseline's
self-score equals the candidate's score on identical media and the delta is exactly 0. There is no floating
point anywhere in the path.

**Live comparison only (D-01).** A quality score needs both pictures, so this check exists only in a
`compare` of two media files. The baseline records its **self-score** (exactly `1000000/1000000`) and the
candidate records its score *against the baseline*, with the baseline's XXH3-128 input identity in evidence
as `reference_identity`. Every one-sided probe -- `snapshot`, `inspect`, and the snapshot side of a compare
-- reports `skipped:requires_media` on both sides, and a snapshot **never stores a score**. Without
`--ssim` a live compare reports `skipped:not_requested`; under `--no-content` it reports
`skipped:requires_decode`.

**What gates (D-03).** The compared value is the **mean** of the per-frame SSIM over the scored pairs. The
**minimum** frame (its score, both decode indices and the baseline PTS) and `identical_frames` (frames
whose luma windows all scored exactly 1) ride in evidence, so the worst frame is named even though it does
not gate. For the single worst frame as the gate, see `content.video.perceptual`.

**Native geometry.** Both sides must have the same display width and height and the same plane layout;
otherwise the check reports `skipped:geometry_mismatch` on both sides, while `content.video.perceptual`
still runs. A different bit depth is not a mismatch: the lower side is promoted by an exact left shift. A
frame narrower or shorter than one 8x8 window has no SSIM and reports `skipped:insufficient_data` with
`reason: frame_too_small`.

**Which frames pair, sampling and path preconditions.** Identical to `quality.psnr`: time-based pairing
(D-02), `--sample N` scores every Nth paired frame (`sampling_state: sampled:N`), and both sides carry
`scaler_path` (`native (no scaler)`) and `decode_path_signature`; a differing or one-sided record is
`skipped:path_incomparable` (TRUST-04, D-04).

## Why it matters

SSIM tracks what the eye notices -- blur, blocking, lost detail -- better than a raw error sum, so it is
the usual second opinion next to PSNR. Run at native resolution it sees the fine detail the 128-wide
thumbnail of `content.video.perceptual` cannot.

## Accept / Tune / Silence

### Accept

If the drop is expected (a deliberate quality change), re-encode your baseline media from the new pipeline
so both sides come from the same settings. Because the value is baseline-referenced, accepting a change
means replacing the baseline file, never a stored number.

### Tune

The tolerance is a score difference written as a **bare number with no unit suffix**: the default is
`0.005`, and `0.005dB` is a usage error that names the bare form. The gating on the mean is fixed, not a
knob. Severity is `fail`; tune it per profile or with `[severity]`.

Known limitation, recorded on the project's ledger rather than hidden: the `tol` comparators evaluate the
registry and profile tolerance and do not yet apply a `--tol` or `[tolerance]` override to the verdict (for
this check and every other `tol` check); the override is validated and shown in the resolved policy only.
Until that is fixed, tune through severity.

### Silence

Do not pass `--ssim` (the default) and the check reports `skipped:not_requested`, or set `quality.ssim` to
`ignore` in `[severity]` for a pipeline whose pictures legitimately change each run. A silenced check's
difference is still computed and shown under `-v`.
