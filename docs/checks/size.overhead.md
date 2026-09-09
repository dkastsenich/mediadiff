# size.overhead

## What it measures

`(file_bytes - Sum of per-stream byte_total) / file_bytes`, at global
scope, kept as an exact `RationalValue` (never pre-divided into a rounded
percentage). `file_bytes` is `size.file`'s own on-disk byte size;
`byte_total` per stream comes directly from `PacketScan`'s shared array --
the difference is everything that is neither video/audio/subtitle/data
payload: container structure, index tables, padding, and (for MPEG-TS)
null-packet stuffing.

A payload sum that exceeds the file's own size (a crafted or otherwise
structurally inconsistent input) skips `skipped:insufficient_data` rather
than reporting a nonsensical negative overhead -- the inputs disagree, and
this check refuses to fabricate an answer from disagreeing inputs.

## Why it matters

Muxer efficiency drift is invisible to `size.file` alone: two files with
identical payload content can differ substantially in overhead purely due
to container mechanics (e.g. MPEG-TS's fixed 188-byte packetization versus
MP4's variable-size boxes, or a change in `-muxrate` padding). This check
isolates that mechanism-level signal from the payload-level signal
`size.stream_bitrate`/`size.peak_bitrate` already cover.

## Accept / Tune / Silence

### Accept

If the overhead change was intentional (a deliberate muxer/container
setting change), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The default `±5%` relative tolerance is deliberately loose -- overhead is
an `info`-severity signal (doc 06's own table), not a gating one. Adjust
via `[check.tolerance]` in `mediadiff.toml` (or `--tol
size.overhead=<value>`) for a pipeline that wants a tighter bound.

### Silence

Set `size.overhead` to `ignore` in `[severity]` for a pipeline where
muxer overhead is expected to vary widely (e.g. one that regularly
switches container formats) and is not itself a signal worth tracking.
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
