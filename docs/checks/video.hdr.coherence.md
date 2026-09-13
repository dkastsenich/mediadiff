# video.hdr.coherence

## What it measures

Whether a video stream's transfer characteristic and its HDR metadata
(mastering-display and content-light presence) agree with each other.
Reads the SAME `codecpar` transfer-characteristic field `video.color.transfer`
reports, and the SAME mastering-display/content-light presence flags
`video.hdr.mdcv`/`video.hdr.cll` read -- never a second, independently
derived interpretation of either.

The compared value is one of exactly four spellings:

- `coherent` -- PQ (`smpte2084`) WITH mastering-display metadata present;
  OR HLG (`arib-std-b67`) with or without any HDR metadata (HLG is
  scene-referred and legitimately ships without mastering-display metadata
  under ITU-R BT.2100); OR a specified SDR transfer with no HDR metadata
  at all.
- `hdr_meta_sdr_transfer` -- mastering-display or content-light metadata is
  present while the transfer characteristic is a specified SDR transfer.
- `pq_without_mdcv` -- the transfer characteristic is PQ (`smpte2084`) and
  no mastering-display metadata is present. Content-light metadata alone
  does NOT substitute for mastering-display metadata here. HLG never
  produces this value.
- `indeterminate` -- the transfer characteristic is unspecified or reserved,
  so no coherence claim can be made in either direction.

This is registered under the `state` semantic (not `exact`): a Finding
fires whenever EITHER side's value is `hdr_meta_sdr_transfer` or
`pq_without_mdcv` -- including when BOTH sides carry the identical
incoherent value. Under `exact`'s ordinary baseline-equality rule, two
files that share an incoherent state would compare `pass`, making the
shared incoherence invisible in `compare` -- exactly the invisibility this
check exists to avoid. The state is always visible as this check's own
value in `inspect`, whether or not a comparison ever runs.

Evidence carries the resolved transfer characteristic's name, whether
mastering-display and content-light metadata were each present, and a
one-sentence rendering of why the value came out as it did.

## Why it matters

An HDR file whose transfer characteristic and metadata disagree is
ambiguous to every downstream player: `pq_without_mdcv` means a PQ-encoded
file with no display-mapping guidance, which most players will either
misrender or refuse to treat as HDR at all; `hdr_meta_sdr_transfer` means
metadata a player might apply to content that was never actually graded
for it. mediadiff reports this incoherence it notices even when both
files share it, because a shared defect that predates this tool's
involvement is still worth surfacing to a reviewer -- it simply should
never, by itself, block a merge.

## Accept / Tune / Silence

### Accept

This check comparing `pass` does NOT mean the file is coherent, and it does
NOT mean both files report the identical value -- it means NEITHER side's
value is one of the two flagged spellings (`hdr_meta_sdr_transfer` /
`pq_without_mdcv`). A baseline of `coherent` compared against a candidate of
`indeterminate` also reports `pass` under this rule, even though the two
files are in different states. Check the rendered value directly (via
`inspect` or `-v`) to see each side's actual classification. If the
incoherence is intentional (or pre-existing and accepted), no action is
needed -- this check never gates the exit code.

### Tune

There is no tolerance to tune -- this is a `state`-semantic classification
over a closed, four-value vocabulary, not a scalar comparison. The
vocabulary itself is fixed (04-CHECK-ROSTER.md's own approved resolution)
and does not change per-profile.

### Silence

This check is `info` severity in EVERY profile, with no
`[check.profile_severity]`/`[check.profile_tolerance]` override of any
kind -- it never gates the exit code by design, not by omission. Set it to
`ignore` in `[severity]` only if a pipeline has no interest in HDR
coherence at all; a silenced check's difference is still computed and
shown under `-v`.
