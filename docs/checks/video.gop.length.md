# video.gop.length

## What it measures

The median distance between consecutive keyframes (I-frames/IDR access units),
measured in access units, for one video stream. Derived from `ParserScanResult`'s
per-access-unit `key_frame` flags -- the parser pass fused into the SAME
`av_read_frame` sweep `PacketScan` already performs, never a second sweep.
Kept as an exact rational value (never pre-divided into a rounded decimal):
the reported number is a whole access-unit count, computed once as the
median of every consecutive keyframe-to-keyframe delta on the stream.

A stream with fewer than two observed keyframes has nothing to measure a
distance over and skips `insufficient_data` -- never a fabricated zero. A
codec with no registered libav parser (`skipped:no_parser`) or a truncated
scan (`skipped:partial_scan`) also refuse rather than report a number that
might be confidently wrong.

## Why it matters

GOP length is the segment-alignment contract every HLS/DASH packager, editor
cut point and hardware encoder assumes stays fixed: chunk boundaries,
seek points and splice points are all placed at keyframes, and a
downstream packager that expects a GOP every 48 frames breaks silently
the moment an encoder starts emitting one every 96. This is UC3's own
failure mode -- an NVENC driver upgrade under `--profile hw-encoder` can
change the encoder's default GOP cadence without touching any explicit
`-g` setting, and the first symptom a team sees is misaligned segments in
production, days after the driver rolled out, not a build failure.
`video.gop.length` is what turns that into a merge-time finding instead.

## Accept / Tune / Silence

### Accept

If the GOP-length change was intentional (a deliberate re-tuning of
segment duration, a new encoder preset, a switch to a different keyframe
interval for adaptive-bitrate ladder alignment), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

The default `±10%` tolerance is a **median-distance** tolerance: it
bounds how far the candidate's median keyframe-to-keyframe access-unit
count may drift from the baseline's before this check reports a
regression, not a per-keyframe jitter budget. A GOP-length change is
UC3's own segment-alignment killer -- an encoder or driver upgrade that
silently shifts the default GOP cadence (e.g. NVENC's own default `-g`
under a new driver) is exactly the class of regression this tolerance is
tuned to catch at the default `±10%`, while still tolerating the ordinary
one-or-two-access-unit jitter a re-encode at the same nominal GOP size
can introduce. Adjust via `[check.tolerance]` in `mediadiff.toml` (or
`--tol video.gop.length=<value>`) for a pipeline whose packager tolerates
a wider (or requires a tighter) segment-alignment margin than the
default.

### Silence

Set `video.gop.length` to `ignore` in `[severity]` for a pipeline with no
segment-alignment or hardware-decode-refresh dependency on GOP cadence
(e.g. a pure archival transcode with no adaptive-bitrate packaging
downstream). Leave it enabled everywhere else -- a silenced check's
difference is still computed and shown under `-v`.
