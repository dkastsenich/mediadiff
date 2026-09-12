# video.gop.idr_interval

## What it measures

The median distance between consecutive real IDR access units, measured in
access units, for one video stream. An access unit is a real IDR when its
leading VCL NAL type is H.264 type 5 (`H264_NAL_IDR_SLICE`) or HEVC type 19
or 20 (`IDR_W_RADL`/`IDR_N_LP`) -- never derived from the parser's own
`key_frame` boolean, which cannot tell an IDR apart from other kinds of
random-access point (see `video.gop.closed`'s own "What it measures" for
why). Kept as an exact rational value (never pre-divided into a rounded
decimal): a whole access-unit count, computed once as the median of every
consecutive IDR-to-IDR delta on the stream.

A stream with fewer than two observed IDRs has nothing to measure a
distance over and skips `insufficient_data` -- never a fabricated zero.
`skipped:no_parser` covers both a codec with no registered libav parser
and a codec with no NAL layer at all (e.g. `mpeg4`), with the codec named
in evidence in the second case. `skipped:partial_scan` covers a truncated
scan. An access-unit array longer than this check's own classification
bound also skips `insufficient_data`, rather than classifying from a
partial view that might disagree with the untruncated answer.

## Why it matters

IDR cadence is the hard reset point every splicer, ad-insertion system and
random-access seek assumes exists at a known interval: a downstream system
that expects a real IDR every 48 frames and instead gets one every 96 will
either seek to the wrong place or splice into the middle of a GOP it
cannot decode cleanly. Unlike `video.gop.length` (which counts every
keyframe-like access unit, including a heuristically-flagged non-IDR I
slice), this check counts only REAL IDRs -- the two numbers can legitimately
differ on a stream using open-GOP structure, and a change in one without
the other is itself a meaningful signal about which part of the encoder's
keyframe behavior actually shifted.

## Accept / Tune / Silence

### Accept

If the IDR-cadence change was intentional (a deliberate re-tuning of the
random-access interval, a new encoder preset, a switch to a longer or
shorter GOP structure for adaptive-bitrate ladder alignment), re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

The default `±10%` tolerance is a **median-distance** tolerance, matching
`video.gop.length`'s own convention: the median is compared, not the mean,
because a single long final IDR-to-IDR span (whatever content happens to
follow the last IDR before the stream ends) is normal and should not pull
the reported number away from the interval every OTHER pair of IDRs
actually shares. Adjust via `[check.tolerance]` in `mediadiff.toml` (or
`--tol video.gop.idr_interval=<value>`) for a pipeline whose splicer or
ad-insertion system tolerates a wider (or requires a tighter) random-access
margin than the default.

### Silence

Set `video.gop.idr_interval` to `ignore` in `[severity]` for a pipeline
with no splicing, ad-insertion or random-access-seek dependency on IDR
cadence specifically (as distinct from ordinary keyframe cadence, which
`video.gop.length` already covers). Leave it enabled everywhere else -- a
silenced check's difference is still computed and shown under `-v`.
