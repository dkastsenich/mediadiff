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
- `(sbr: implicit)` -- a bare, ambiguous AAC-LC ASC where an actual decode
  found a doubled sample rate or an `AV_PROFILE_AAC_HE`-class profile. The
  decode is normally the one `avformat_find_stream_info()` already
  performs in the header pass, at no additional cost.
- No suffix -- either a genuinely non-SBR AAC stream (the header pass
  resolved a non-HE profile at the undoubled declared rate, or the
  declared ASC cannot describe implicit SBR at all), or a non-AAC codec.
  The value deliberately names neither "implicit" nor "explicit" in this
  case, so the third bucket is visible rather than silently folded into
  one of the other two.
- `(sbr: unknown)` -- the ambiguous case could not be resolved at all: the
  header pass resolved no profile (libav decoded nothing) **and** there is
  no ASC to reason from, or the bounded fallback decode itself failed.
  Never silently asserted as "no SBR".

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
entirely in the header pass (`src/probe/demux_session.cpp`) -- never a
full sweep, never a dependence on whether `Pass::audio_decode` ran. This
is what makes the value identical whether or not `--content`/a decode pass
ran at all: a check's value must never depend on which passes ran (D-12's
general rule).

The order the header pass decides in:

1. mediadiff's own AudioSpecificConfig bit-reader, for the no-decode
   explicit case.
2. **D-12's primary mechanism** -- the `codecpar` that
   `avformat_find_stream_info()` has already produced. Measured against
   the linked FFmpeg 8.1 (never the system `ffmpeg`) across every AAC
   fixture in this corpus, that call resolves a real
   `codecpar->profile` for all of them, and for
   `audio_sbr_implicit.mp4` it also resolves the SBR-doubled
   `sample_rate` (ASC declares 44100, `codecpar` reports 88200, profile
   `AV_PROFILE_AAC_HE`). `06-RESEARCH.md` Q4's finding that
   `has_codec_parameters()` *can* be satisfied without decoding stands as
   a possibility, which is exactly why the gate is
   `profile != AV_PROFILE_UNKNOWN` rather than an assumption: a resolved
   profile is proof a decode happened, and an unresolved one falls through
   to step 3 instead of asserting anything.
3. A **bounded one-packet decode** fallback, reached only when step 2
   resolved nothing at all, and only for an ASC that could describe
   implicit SBR in the first place (AAC-LC object type, and a doubled rate
   that AAC can actually express).

### History: why this doc once described the fallback as the main path

Plan `06-04` implemented step 3 as the *primary* path and never
implemented step 2, so every AAC file whose ASC lacked explicit SBR -- i.e.
every ordinary AAC-LC file -- triggered a second
`avformat_open_input` + `avformat_find_stream_info` of the whole file.
That cost 17% of the no-decode packet-scan path's retired instructions and,
worse, produced `(sbr: unknown)` rather than an answer on ordinary
encoder-produced AAC: the one packet the fallback is allowed to send is
consumed entirely as encoder-delay priming, so the decoder returns
`EAGAIN` and no frame is ever seen. Plan `06-13` implemented step 2 as
D-12 had actually decided it, which both removed that cost and turned
those files' `(sbr: unknown)` into a real `LC`. See
`.planning/WINDOWS.md` for the ledger entry.

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
