# quality.vmaf

## What it measures

How good the candidate's picture looks next to the baseline's, as Netflix's **VMAF** score (0 to 100, higher
is better) computed by libvmaf over the primary video stream (the first video stream that is not cover art).
It is opt-in: give `--vmaf` to `compare` or `dir --content`. The value is a bare `score`, stored as
thousandths: `97430/1000` is 97.43.

**Build option and platforms.** libvmaf is not part of the default single static binary. It is linked only
by a build configured with `-DMEDIADIFF_WITH_VMAF=ON` (the `x64-linux-vmaf` preset does this), on Linux and
macOS; the vcpkg `libvmaf` port does not support Windows, so configuring the option there fails at configure
time. On every other build `quality.vmaf` is still registered (a config or a snapshot that names it never
breaks) and a live compare reports it as `skipped:not_requested`, but `--vmaf` itself is a **usage error**
(exit 64) naming `MEDIADIFF_WITH_VMAF` -- it is never silently skipped. `mediadiff --version` lists `vmaf` in
`features:` exactly when the build has it.

**The model is pinned.** The score uses the built-in model `vmaf_v0.6.1`, loaded by name, and the model name
and the linked `vmaf_version()` are recorded in evidence (`model`, `libvmaf_version`) on both sides. The pin is
a correctness feature, not a convenience: two VMAF numbers from different models are not comparable, so a
change of model is a change you can see, never a silent shift in every score.

**The baseline's self-score is computed, not assumed.** VMAF of a picture against itself is not 100 (it was
measured at 97.43 on identical input). The baseline side is therefore the score of a second libvmaf context fed
the baseline against itself, and the candidate side is the score of the candidate against the baseline. For
identical media the two are the same number, so the delta is exactly 0.

**Live comparison only (D-01).** A quality score needs both pictures, so this check exists only in a `compare`
of two media files. Every one-sided probe -- `snapshot`, `inspect`, and the snapshot side of a compare --
reports `skipped:requires_media` on both sides, and a snapshot **never stores a score**. Under `--no-content`
it reports `skipped:requires_decode`.

**What gates (D-03).** The compared value is the **harmonic mean** of the per-frame VMAF over the scored pairs;
the harmonic mean is dominated by bad frames, which is what a regression gate wants. The **minimum** and the
**arithmetic mean** ride in evidence (`min`, `mean`), with `pairs_scored` and the baseline's
`reference_identity`. Each score is quantized once, half away from zero, to a thousandth; a score libvmaf
returns as not-a-number is `skipped:insufficient_data`, never written to the report.

**Sampling is refused.** VMAF's temporal feature looks at neighbouring frames, so scoring every Nth frame would
not be VMAF. With `--sample N` for N of 2 or more the check reports `skipped:sampling_conflict` on both sides
while the other checks still honour the stride; `--sample 1` scores normally.

**Geometry.** libvmaf needs the same width, height, plane layout and bit depth on both sides and accepts 4:2:0,
4:2:2, 4:4:4 and gray; a lower bit depth is promoted by an exact left shift. A different resolution or layout is
`skipped:geometry_mismatch` on both sides; mediadiff does not rescale the reference to make a pair fit. A picture
16 pixels or smaller in either dimension cannot be scored (libvmaf itself aborts on it, measured against
libvmaf 3.2.0), so it reports `skipped:insufficient_data` with `reason: frame_too_small` and is never handed to
libvmaf.

**Which frames pair and path preconditions.** The same time-based pairing as the other quality checks (D-02).
Both sides carry `scaler_path` (`native (no scaler)`) and `decode_path_signature`; a differing or one-sided
record is `skipped:path_incomparable` (TRUST-04, D-04).

## Why it matters

VMAF is the quality number streaming teams already track, and it is the one that correlates best with how a
viewer rates a clip. A change that leaves PSNR flat can still move VMAF, so it is the second gate that catches
an encoder or a scaler quietly getting worse.

## Accept / Tune / Silence

### Accept

If the drop is expected (a deliberate quality or bitrate change), re-encode your baseline media from the new
pipeline so both sides come from the same settings. Because the value is baseline-referenced, accepting a
change means replacing the baseline file, never a stored number.

### Tune

The tolerance is a score difference written as a **bare number with no unit suffix**: the default is `0.5` (half
a VMAF point), and `0.5dB` is a usage error that names the bare form. The harmonic-mean gating and the model are
fixed, not knobs. Severity is `fail`; tune it per profile or with `[severity]`.

Known limitation, recorded on the project's ledger rather than hidden: the `tol` comparators evaluate the
registry and profile tolerance and do not yet apply a `--tol` or `[tolerance]` override to the verdict (for this
check and every other `tol` check); the override is validated and shown in the resolved policy only. Until that
is fixed, tune through severity.

### Silence

Do not pass `--vmaf` (the default) and the check reports `skipped:not_requested`, or set `quality.vmaf` to
`ignore` in `[severity]` for a pipeline whose pictures legitimately change each run. A silenced check's
difference is still computed and shown under `-v`.
