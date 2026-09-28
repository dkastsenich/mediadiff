# audio.codec

## What it measures

The audio stream's own codec, as the libav-stable string name
(`avcodec_get_name`), one Measurement per audio stream at `Scope{audio, N}`
-- the same extraction `video.codec` performs for a video stream, applied
here to `codecpar` for an audio one. Derived from the header pass alone, so
it reports a real value under `--no-content` (no decode is required).
Evidence carries the raw `codec_id` enum ordinal and the container-level
`codec_tag` so a codec whose name is shared across different tags is still
distinguishable under `-v`.

A file with no audio stream at all still reports this check as
`skipped:insufficient_data`, rather than emitting nothing -- `skipped !=
pass` is load-bearing.

## Why it matters

The audio codec is the first thing a reviewer checks when a pipeline's
output sounds different -- an unexpected transcode nobody signed off on,
or a fallback path silently engaging, usually explains everything else
that follows from it.

## Accept / Tune / Silence

### Accept

If the codec change was an intentional transcode, re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline, or
compare under `--profile transform` if the pipeline routinely transcodes
by design.

### Tune

No numeric tolerance applies -- this is an `exact` check.

### Silence

Set `audio.codec` to `ignore` in `[severity]` for a pipeline where the
audio codec is deliberately variable and never a meaningful signal on its
own. Leave it enabled everywhere else -- a silenced check's difference is
still computed and shown under `-v`.
