# content.video.black_runs

## What it measures

Stretches of a video stream that are black: a run of at least 3 consecutive decoded frames whose
picture is black. The value is a list of time spans, in milliseconds, measured from the stream's own
first decoded frame and end-exclusive (a span ends one frame interval after its last black frame). A
stream that decodes but contains no black run reports an **empty span list** as a real measured value
-- never a skip and never `Absent{}`.

**How a frame is judged black.** Every decoded frame (whatever `--sample N` is) is reduced to the
deterministic 128-wide luma thumbnail `content.video.perceptual` uses, normalized to 8 bits. The frame
is black when its **mean luma is at most `black_point + 2`** and its **variance is below 4**, both
computed with exact integer sums. The black point depends on the colour range:

- **16** for limited ("tv") range, and for an unspecified range (the YUV convention);
- **0** for full ("pc") range, read *after* the `yuvj` fold, so a deprecated `yuvj420p` counts as full
  range exactly as `video.color.range` reports it.

That is what makes the check safe against a range flip. The same black segment encoded limited-range
(luma 16) and full-range (luma 0) reports the **same span**, because each is measured against its own
black point. The dark-grey example shows why a range-unaware rule cannot work: a full-range frame
whose luma is about 17 is dark grey, not black, while the very same pixels labelled limited-range are
black. A rule of "mean at most 18" flags the first; a rule of "mean at most 2" misses every
limited-range black. Bit depth needs no separate handling because the thumbnail is normalized to 8 bits
before judging: 10-bit luma 64 is 8-bit 16, black at the limited black point. A format that cannot be
read as a luma plane (RGB, packed YUV) is converted to gray by the scaler, already range-expanded, and
judged at black point 0.

**Introduced and removed runs.** The check uses the `span` semantic. A black run in the candidate
that overlaps no baseline run is **introduced** and gates at the check's severity; a baseline run the
candidate no longer has is **removed** and is reported at `info`, never gating. The 3-frame minimum
keeps a one-frame black flash near a fade, which can cross the threshold in one encode and not the
other, from gating.

**When it does not measure.** `skipped:requires_decode` under `--no-content`, or when the decoder
could not be opened; `skipped:partial_scan` when the packet scan or the decode stopped early (the
evidence carries `decode_truncation_reason`), because a prefix's spans compared against a full stream
would fabricate introduced or removed runs; `skipped:insufficient_data` when no frame decoded;
`skipped:no_timing_data` when a run exists but the stream gives no way to place it in time.

## Why it matters

A black segment in the middle of a programme is a failure class ordinary CI never sees: a splice that
lost its source, a transcoder that dropped a scene, an ad insertion that never filled. The container
is valid and the duration unchanged. Because the rule is range- and depth-normalized, an honest
re-encode that only changes the range flag or the bit depth does not produce a false alarm, and a
false alarm is what gets a gate muted.

## Accept / Tune / Silence

### Accept

If the black segment is intentional (a fade to black, a slate between sections), re-run `mediadiff
snapshot` on the new candidate to make it the new baseline.

### Tune

The margin (`kBlackMeanMargin` = 2), the variance limit (`kBlackVarianceLimit` = 4) and the 3-frame
minimum (`kBlackMinFrames`) are fixed, named **detection** constants, not configurable knobs in v1. A
detection parameter changes the *measured* span list, unlike a tolerance, which only changes the
verdict (Phase 5 D-08). They were validated on synthetic content only; a real-content review is on the
project's ledger. `--tol` and severity overrides still decide whether an introduced span gates the
exit code.

### Silence

Set `content.video.black_runs` to `ignore` in `[severity]` for a pipeline whose content legitimately
contains black (a night scene, a slate-heavy programme). A silenced check's difference is still
computed and shown under `-v`.
