# video.sar.conflict

## What it measures

Whether a video stream's container-level sample aspect ratio
(`AVStream::sample_aspect_ratio`, e.g. mp4's own `pasp` box) and its
bitstream-level sample aspect ratio (`codecpar->sample_aspect_ratio`, the
VUI/VOL-header value) **agree**, after both are normalized under `video.sar`'s
own `0/1`-means-unset-treated-as-1:1 rule. One Measurement per video stream,
`info` severity, `exact` semantic over a string value: `"agree"` when the two
effective ratios match, or a rendering of both divergent ratios when they do
not.

Both raw values (container and bitstream, including whichever is `0/1`) ride
in evidence regardless of outcome, so a reader can see the exact numbers
even when the check reports agreement.

## Why it matters

A container and a bitstream disagreeing about pixel shape means two
different players can legitimately render the same file at two different
aspect ratios -- one honoring the container's `pasp` box, another decoding
the bitstream's own VUI/VOL header. This is worth reporting even when
nothing changed between the baseline and the candidate: the disagreement is
a property of the file itself, not of a diff between two files, which is
exactly why this is its own check rather than a delta on `video.sar`.

## Accept / Tune / Silence

### Accept

A reported conflict is diagnostic, not something to "fix" via `mediadiff`
itself -- if the conflict is intentional (or has always been present and is
acceptable), re-run `mediadiff snapshot` on the candidate to establish it as
the new baseline; the conflict will then report as unchanged (both sides
carrying the same conflict) rather than as a new finding.

### Tune

There is no tolerance to tune -- `exact` at `info` severity means the state
is reported but never gates the exit code under normal severity policy.

### Silence

`video.sar.conflict` is `info` severity by default and does not gate the
exit code; set it to `ignore` in `[severity]` only if this signal is not
useful diagnostic information for a given pipeline. A silenced check's
difference is still computed and shown under `-v`.
