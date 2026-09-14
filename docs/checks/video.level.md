# video.level

## What it measures

The video stream's codec level, one Measurement per video stream at
`Scope{video, N}`. The compared value is a codec-specific human string
this project renders itself (libav has no single API that produces one):
H.264's `level_idc` divided by 10 (`31` renders `3.1`), HEVC's
`general_level_idc` divided by 30 (`123` renders `4.1`), AV1's
`seq_level_idx` via the spec's `2 + idx/4` major / `idx%4` minor formula
(`8` renders `4.0`). A level this table does not cover (including every
codec outside that list) renders as its raw integer's decimal spelling --
guessing at a level spelling would make a wrong string permanent, since
check values enter committed snapshots. Evidence always carries the raw
integer, including the `AV_LEVEL_UNKNOWN` (-99) sentinel.

**HEVC tier is not folded into the rendered string.** `general_tier_flag`
is parsed internally by libavcodec's HEVC SPS parser but is not exposed
through `AVCodecParameters`, `AVCodecParserContext`, or any other public
libav surface reachable without a full decode pass -- confirmed by reading
the pinned FFmpeg 8.1's own `libavcodec/hevc/ps.c`/`ps.h` (tier lives only
in `HEVCSPS`, a decoder-private struct) and by `avcodec_profile_name`/
ffprobe's own reporting, neither of which surfaces tier either. Since this
phase's `video.*` family is scoped to `codecpar`-only extraction (no
decode pass; 04-CONTEXT.md D-08/D-09), tier folding is deferred to
whichever future phase adds a decode pass or a dedicated bitstream-level
tier extraction.

## Why it matters

A level mismatch means the two streams claim different maximum
resolution/bitrate/buffer envelopes, which affects hardware decoder
compatibility even when the profile is identical.

## Accept / Tune / Silence

### Accept

If the level change was an intentional encoder retune, re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

`--profile transform` demotes this check to `info`: an intentional
transformation is expected to change encoder-level parameters like level.
No numeric tolerance applies -- this is an `exact` check.

### Silence

Set `video.level` to `ignore` in `[severity]` for a pipeline where level
is deliberately variable and never a meaningful signal on its own.
