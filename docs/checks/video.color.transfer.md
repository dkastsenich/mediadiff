# video.color.transfer

## What it measures

The video stream's transfer characteristic (`av_color_transfer_name`,
e.g. `"bt709"`, `"smpte2084"`, `"arib-std-b67"`, or `"unknown"` when
unspecified) -- one Measurement per video stream, compared as an exact
string against libav's own raw `codecpar->color_trc` field, with no fold
and no special case.

`"unknown"` compares exactly like any other value in both directions
(VIDEO-08) -- a change to or from it is a real, reported difference,
never a wildcard match.

## Why it matters

The transfer characteristic defines how stored sample values map to
light intensity (gamma/EOTF). Two transfer characteristics matter enough
to name explicitly: **PQ** (`smpte2084`, SMPTE ST 2084) and **HLG**
(`arib-std-b67`, ARIB STD-B67 "Hybrid Log-Gamma") -- both are HDR
transfer functions. A transfer change into or out of either of these is
an **SDR-to-HDR transition** (or its reverse), not an ordinary transfer
tweak: every downstream player, decoder and display pipeline that
assumes SDR will render PQ/HLG content with wildly wrong brightness if
this changes silently, and the reverse direction throws away the file's
entire HDR range.

## Accept / Tune / Silence

### Accept

If the transfer characteristic change was intentional (e.g. a deliberate
SDR-to-HDR remaster), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `fail` severity means any
change in the declared transfer characteristic blocks the merge by
default.

### Silence

Set `video.color.transfer` to `ignore` in `[severity]` for a pipeline
where the transfer characteristic is deliberately variable and never a
meaningful signal on its own. A silenced check's difference is still
computed and shown under `-v`.
