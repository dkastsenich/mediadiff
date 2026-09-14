# video.gop.refs

## What it measures

The H.264 SPS's own declared `max_num_ref_frames` field -- the maximum
number of reference frames the encoder told the decoder to keep in its
reference picture buffer -- read directly from the bitstream's own
sequence parameter set, as an exact integer count. No public libav API
exposes this value: `AVCodecParameters` carries no reference-frame count
field, and `AVCodecParserContext` never publishes the H.264 parser's own
internal `sps->ref_frame_count` either, so this check reads the SPS's own
Exp-Golomb-coded fields directly.

A codec this reader does not extract a reference count from skips
`no_parser`, naming the codec in evidence: `mpeg4`/`mpeg2video` have no SPS
concept at all, and HEVC's own SPS is a structurally different layout this
project does not read. An H.264 stream whose SPS was never resolved --
never seen, truncated, unreadable, or carrying a scaling-list block this
reader deliberately does not decode -- skips `unparsed_mechanism` rather
than reporting a fabricated zero.

## Why it matters

Reference-frame count governs how much motion-compensation history an
encoder is allowed to look back through, which directly trades off coding
efficiency against decoder buffer memory and, on hardware decoders, against
maximum supported reference count. A change here -- an encoder preset
switch, a rate-control retune, or a hardware-encoder driver update that
silently adjusts its own default -- can push a stream past what a
constrained playback device's decoder can handle, or change coding
efficiency in a way worth knowing about even when playback compatibility
is not at risk.

## Accept / Tune / Silence

### Accept

If the reference-count change was intentional (a deliberate encoder preset
change, or a retune for a specific hardware decoder's own reference-buffer
ceiling), re-run `mediadiff snapshot` on the new candidate to establish it
as the new baseline.

### Tune

There is no tolerance to tune -- `exact` semantics means any change in the
declared count is reported. Severity is `warn`, not `fail`
(04-CHECK-ROSTER.md): a real, worth-noticing reference-structure signal,
but not on its own a shipping blocker the way an open-GOP regression is.
Promote it to `fail` in `[severity]` for a pipeline targeting a hardware
decoder with a known, hard reference-count ceiling.

### Silence

Set `video.gop.refs` to `ignore` in `[severity]` for a pipeline with no
reference-buffer or hardware-decode-compatibility dependency on this
value. Leave it enabled everywhere else -- a silenced check's difference is
still computed and shown under `-v`.
