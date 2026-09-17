# video.resolution

## What it measures

The video stream's own `width`x`height`, as a `WIDTHxHEIGHT` string, one
Measurement per video stream at `Scope{video, N}`. This is the first
shipped check to carry `transform_affected = true`: under `--profile
transform` with a declared resolution expectation (`expect.resolution` in
`mediadiff.toml`, e.g. `"2x"` or `"3840x2160"`), the candidate is compared
against the value that expectation derives from the baseline, rather than
against the baseline's own resolution directly.

Coded (pre-crop) dimensions would additionally distinguish a cropping
change from a genuine resolution change, but that data lives on
`AVCodecContext` only after `avcodec_open2` -- unavailable in this phase,
which has no decode pass (04-CONTEXT.md D-08/D-09). Evidence is therefore
scoped to what `codecpar` alone provides.

## Why it matters

Resolution is the most visible video property there is -- an unexpected
change (a downscale nobody signed off on, an upscale hiding a lossy
intermediate step) is immediately apparent to a viewer and usually the
first thing a bug report names.

## Accept / Tune / Silence

### Accept

If the resolution change was an intentional re-encode at a new target
size, re-run `mediadiff snapshot` on the new candidate to establish it as
the new baseline.

### Tune

Declare the expected transform in `mediadiff.toml`:

```toml
[transform]
resolution = "2x"   # or an absolute "3840x2160"
```

Under `--profile transform`, the candidate is compared against the
resolution this expectation derives from the baseline (a scale factor
multiplies each axis exactly, refusing a non-integer result rather than
rounding) instead of against the baseline's own value. Outside `transform`
(or with no declared expectation), this check compares by ordinary exact
equality against the baseline.

### Silence

Set `video.resolution` to `ignore` in `[severity]` for a pipeline where
resolution is deliberately variable (e.g. adaptive-bitrate ladders
compared cross-rung) and never a meaningful signal on its own.
