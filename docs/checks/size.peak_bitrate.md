# size.peak_bitrate

## What it measures

Per stream, at that stream's own scope: the MAXIMUM byte sum observed over
any 1-second sliding window (100ms step) of the stream's own DTS-in-ticks
axis, doubled to bits and kept as an exact `RationalValue` -- doc 06's
"buffer/VBV compatibility tell".

Windowing is defined precisely enough to be byte-identical across
platforms: every window boundary is computed fresh from its own window
index `k` (`first_dts + k * step_ticks`, never by accumulating `+=
step_ticks`, which drifts when the step does not divide the timebase
evenly), all arithmetic is exact checked integer arithmetic (no
floating-point conversion anywhere in the path), and the packet array is
sorted by DTS before windowing (`PacketScan`'s own read order is NOT
guaranteed DTS-sorted).

**Scope decision:** this check is PER-STREAM, not a combined muxed-container
bitrate (03-CHECK-ROSTER.md's own recorded decision) -- matches
`size.stream_bitrate`'s explicit per-stream framing, and a combined figure
would require interleaving packets from streams with different timebases
into one window, a materially harder merge doc 06 describes no algorithm
for.

Skips `skipped:no_timing_data` when every packet on the stream lacks a
DTS, and `skipped:insufficient_data` when the stream's own DTS span is
shorter than one full window (there is no complete window to take a
maximum over) -- never a zero, never a fabricated peak.

## Why it matters

Peak bitrate over a short window is what actually determines whether a
downstream decoder's buffer (VBV/HRD) can keep up -- a stream whose
AVERAGE bitrate looks fine can still contain a short burst that overflows
a real-world decoder's buffer. This is exactly the signal
`size.stream_bitrate` cannot see on its own.

## Accept / Tune / Silence

### Accept

If the peak-bitrate change was intentional (a deliberate VBV/rate-control
setting change), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The default `±5%,15%` tolerance (warn at 5%, fail at 15%) matches ordinary
encoder-to-encoder burst variation. Adjust via `[check.tolerance]` in
`mediadiff.toml` (or `--tol size.peak_bitrate=<value>`) for a pipeline with
tighter or looser buffer-compatibility requirements.

### Silence

Set `size.peak_bitrate` to `ignore` in `[severity]` for a pipeline that
does not target a constrained-buffer decoder (e.g. file-based-only
delivery with no real-time playback constraint). Leave it enabled
everywhere else -- a silenced check's difference is still computed and
shown under `-v`.
