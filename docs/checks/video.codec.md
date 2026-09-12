# video.codec

## What it measures

The video stream's own codec, as the libav-stable string name
(`avcodec_get_name`), one Measurement per video stream at `Scope{video, N}`.
Evidence carries the raw `codec_id` enum ordinal and the container-level
`codec_tag` (fourcc) so a codec whose name is shared across different tags
is still distinguishable under `-v`.

## Why it matters

The codec is the first thing a reviewer checks when a media pipeline's
output looks or behaves differently -- an unexpected codec change (a
transcode nobody signed off on, a fallback path silently engaging) usually
explains everything else that follows from it.

## Accept / Tune / Silence

### Accept

If the codec change was an intentional transcode, re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline, or
compare under `--profile transform` if the pipeline routinely transcodes
by design.

### Tune

`--profile transform` demotes this check to `info`: under an intentional
transformation the codec is *expected* to change, which is exactly what
that profile exists to express. No numeric tolerance applies -- this is an
`exact` check.

### Silence

Set `video.codec` to `ignore` in `[severity]` for a pipeline where the
codec is deliberately variable and never a meaningful signal on its own.
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
