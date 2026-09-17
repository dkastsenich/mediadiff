# video.profile

## What it measures

The video stream's codec profile, one Measurement per video stream at
`Scope{video, N}`. The compared value is `avcodec_profile_name(codec_id,
profile)`'s rendered string when it resolves, and the raw profile
integer's decimal spelling when it does not -- an unrecognised profile
number is never collapsed into a shared `unknown` word, so two files with
*different* unrecognised profiles still compare as different (VIDEO-01-E2).
Evidence always carries the raw integer, including the `AV_PROFILE_UNKNOWN`
(-99) sentinel libav uses when a codec exposes no profile concept at all.

## Why it matters

A profile change (e.g. Main to High, Simple to Advanced Simple) usually
means a different encoder configuration or toolchain entirely, and can
affect decoder compatibility on constrained playback targets even when
every other stream parameter is identical.

## Accept / Tune / Silence

### Accept

If the profile change was an intentional encoder retune, re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

`--profile transform` demotes this check to `info`: an intentional
transformation is expected to change encoder-level parameters like
profile. No numeric tolerance applies -- this is an `exact` check.

### Silence

Set `video.profile` to `ignore` in `[severity]` for a pipeline where
profile is deliberately variable (e.g. adaptive per-title encoding) and
never a meaningful signal on its own.
