# size.file

## What it measures

The container's own byte size on disk, as a plain `int64`, at global scope.
Read directly from the already-open probe session's own I/O layer (an
`avio_size`-equivalent call) rather than derived from anything the packet
sweep produced -- this is the one `size.*` check that does NOT skip when
`PacketScan` truncated (D-02). A file's size on disk is a property of the
FILE, not of the scan, so it remains meaningful even when the packet
sweep hit its own byte or packet-count ceiling.

## Why it matters

File size is the crudest, cheapest and most universally understood size
signal there is -- an unexpected size change is often the first thing a
reviewer or a CI dashboard notices, well before any finer-grained check
explains WHY. `size.overhead`, `size.stream_bitrate` and `size.peak_bitrate`
(elsewhere in this family) explain the "why"; this check is the "what
changed" headline.

## Accept / Tune / Silence

### Accept

If the size change was intentional (a real encoder/muxer setting change,
a deliberate quality trade-off), re-run `mediadiff snapshot` on the new
candidate to establish it as the new baseline.

### Tune

The default `±3%,8%` tolerance (warn at 3%, fail at 8%) matches ordinary
encoder-to-encoder size variation. `--profile strict-bitexact` and
`--profile remux` both tighten this to `±0.5%`: a bit-exact re-encode or a
remux that changes the container's own byte size at all is itself a
defect signal in those profiles, not expected variation. Adjust via
`[check.tolerance]` in `mediadiff.toml` (or `--tol size.file=<value>`) for
a pipeline whose normal size variance differs from either default.

### Silence

Set `size.file` to `ignore` in `[severity]` for a pipeline where size
alone is never a meaningful signal (e.g. one already gated purely on
`size.stream_bitrate`/`size.peak_bitrate`). Leave it enabled everywhere
else -- a silenced check's difference is still computed and shown under
`-v`.
