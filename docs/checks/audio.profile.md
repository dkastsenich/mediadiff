# audio.profile

## What it measures

The audio stream's codec profile plus its HE-AAC SBR signaling mode, one
Measurement per audio stream at `Scope{audio, N}`. The compared value is
`avcodec_profile_name(codec_id, profile)`'s rendered string when it
resolves, and the raw profile integer's decimal spelling when it does not
-- an unrecognised or undeclared profile is never collapsed into a shared
`unknown` word, so two files with *different* unrecognised profiles still
compare as different (mirroring `video.profile`'s VIDEO-01-E2 precedent).

For AAC specifically, that string carries a signaling-mode suffix so the
three buckets an HE-AAC stream can land in are distinguishable:

- `(sbr: explicit)` -- the AudioSpecificConfig itself declares SBR (a
  top-level `AOT_SBR` object type, or a `0x2b7` backward-compatible sync
  extension). Resolved with **no decode at all**.
- `(sbr: implicit)` -- a bare, ambiguous AAC-LC ASC where a bounded
  one-packet decode found a doubled sample rate or an `AV_PROFILE_AAC_HE`-
  class profile.
- No suffix -- either a genuinely non-SBR AAC stream (the same bare ASC,
  but the bounded decode found neither signal), or a non-AAC codec. The
  value deliberately names neither "implicit" nor "explicit" in this case,
  so the third bucket is visible rather than silently folded into one of
  the other two.
- `(sbr: unknown)` -- the ambiguous case could not be resolved at all (no
  ASC, or the bounded decode itself failed) -- never silently asserted as
  "no SBR".

Evidence always carries the raw `profile` integer (including the
`AV_PROFILE_UNKNOWN` sentinel) and a `sbr_signaling` key spelling the
resolved mode (`explicit_asc` / `implicit_decoded` / `none` / `unknown`).

## Why it matters

HE-AAC's SBR extension roughly doubles the effective sample rate carried
inside a nominally lower-rate AAC-LC core stream. Whether that extension is
signaled explicitly (in the container-level configuration) or only
discoverable by decoding directly affects downstream compatibility --
some decoders and hardware players only recognize one signaling
convention. A silent flip between the two, even when the audible result is
identical, is a real regression this check exists to catch.

### The doc 05 section 2.1 amendment (D-12)

Doc 05 section 2.1's literal recipe says to detect implicit SBR by
comparing `codecpar->sample_rate` (before decode) against the first
decoded frame's own rate. **Against the linked FFmpeg 8.1, that comparison
is not available in the header pass at all**: `06-RESEARCH.md` Q4 traced
both `libavformat/isom.c` and `libavcodec/aac/aacdec.c` directly and found
that (1) the MP4 demuxer writes the UNDOUBLED base rate into `codecpar` for
an implicitly-signaled stream, and (2) `avformat_find_stream_info()`'s own
internal probe can be satisfied (`has_codec_parameters()`) before ever
decoding a single frame, since `sample_rate`/`channels` are already
non-zero from the demuxer's own (wrong, undoubled) values.

The amendment, recorded here in the open rather than silently applied
(the same discipline Phase 5 D-14 used for `PERF-03`): resolution happens
entirely in the header pass (`src/probe/demux_session.cpp`), via
mediadiff's own AudioSpecificConfig bit-reader for the no-decode explicit
case, falling back to a **bounded one-packet decode** -- never a full
sweep, never a dependence on whether `Pass::audio_decode` ran -- only when
the ASC itself is genuinely ambiguous. This is what makes the value
identical whether or not `--content`/a decode pass ran at all: a check's
value must never depend on which passes ran (D-12's general rule).

## Accept / Tune / Silence

### Accept

If the SBR signaling change was an intentional re-encode (e.g. switching
encoder tooling or container muxer), re-run `mediadiff snapshot` on the new
candidate to establish it as the new baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check over a string
value. A pipeline that deliberately varies HE-AAC signaling conventions
should declare that intent via `--profile transform` rather than tuning a
tolerance that has no meaningful "close enough" magnitude for a discrete
signaling mode.

### Silence

Set `audio.profile` to `ignore` in `[severity]` for a pipeline where the
codec profile or SBR signaling convention is deliberately variable and
never a meaningful signal on its own.
