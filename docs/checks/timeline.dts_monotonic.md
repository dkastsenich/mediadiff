# timeline.dts_monotonic

## What it measures

The count of `dts[i] <= dts[i-1]` violations for one stream, evaluated in
READ order over the shared packet scan, kept as an `int64` under a
zero-magnitude absolute tolerance (`0`, not `exact` -- see Tune below). A
tie (`dts[i] == dts[i-1]`) counts as a violation, not only a strict
decrease -- doc 04's own definition is `<=`, never only `<`.

Packets carrying `AV_NOPTS_VALUE` on the DTS axis are **excluded** from the
axis under test before counting starts, rather than coerced into the
comparison as a value -- a stream whose every DTS is the absent sentinel
reports `skipped:no_timing_data`, never a fabricated `0`. A mixed stream
(some real DTS, some absent) counts violations only across consecutive
REAL values; the excluded count rides in evidence
(`excluded_sentinel_count`) so a reader can see how many packets were
skipped over.

On an MPEG-TS input, the DTS sequence under test is the **unwrapped** one:
the raw 33-bit values are passed through this project's TS unwrap rule
(doc 04 section 1.2) before this check ever sees them, so a stream that
legitimately wraps mid-file reports zero monotonicity violations from the
wrap itself. The `unwrapped` evidence flag records whether this basis
applied.

This check runs on every stream carrying timestamps **except** subtitle
streams -- a subtitle track's own presentation pattern is a different
property this check does not evaluate (05-CHECK-ROSTER.md's own scope
decision).

When the packet scan was truncated (`partial_scan`), this check skips
rather than reporting a count derived from an incomplete sweep -- a count
from a truncated scan understates the real count, which is worse than no
answer at all.

### MPEG-TS decode timestamps (UD-3)

The rule above is unchanged: `dts[i] <= dts[i-1]` still counts, including a
genuine tie. What changes on MPEG-TS is *where the DTS sequence under test
comes from*.

libavformat's own read-back DTS on MPEG-TS is sometimes **inferred**, not
read from the container: `compute_pkt_fields`'s own heuristics can
fabricate a tie that does not exist in the file (05-VERIFICATION.md's
Gap 4 -- a `-c copy` remux that dropped the MPEG-4 VOL header produced a
one-frame-lagged DTS guess with no basis in the file's own PES headers).
**This check never judges that inferred value on MPEG-TS** -- not even
when only some of a stream's packets joined.

The orchestrator's container-DTS post-pass (`src/probe/orchestrator.cpp`)
substitutes the PES header's own decode-timestamp truth (ISO/IEC 13818-1
section 2.4.3.7) for every joined packet -- once, ahead of every analyzer,
including this one. When a PES header carries PTS only (`PTS_DTS_flags` =
`10`), its DTS equals its PTS (ISO/IEC 13818-1's own absent-DTS rule). The
join lookup accounts for the container's own packet stride (188, 192 or
204 bytes -- `ts_scan`'s own recorded PES offset and libavformat's
`PacketRecord::pos` differ by exactly `stride - 188`, 05-REVIEW.md WR-01's
own fix); a wrong stride can only ever fail to join, never produce a
false one, since the join also requires the packet's own PTS to match the
PES header's.

A stream can be only **partially** joined: a frame the demuxer split out
of a multi-frame PES (no `pos` of its own) always keeps the demuxer's
value, since it carries no PES header of its own to join against, and a
packet whose `pos` IS non-negative can still fail to find a matching PES
record. This check judges **only the packets that actually joined** --
never a mix of PES-header truth and libavformat's own inferred read-back,
which are not guaranteed to agree at the boundary between a joined run
and an unjoined run. The excluded count is reported in evidence (see
`excluded_from_judgment` below), never silently dropped.

When **zero** packets joined for a stream, container truth could not be
established for it at all: this check **skips with `insufficient_data`**
rather than judging libavformat's own inferred value under a
`container_pes` label. The same skip applies when `ts_scan`'s own global
PES-record budget was exhausted before this stream's list, or a partial
scan stopped before this stream's last PES start. `pts_unique`, `gaps`
and `wrap_events` are unaffected by any of this, since none of them reads
DTS.

### `dts_source` evidence

Every computed `timeline.dts_monotonic` measurement -- MPEG-TS and every
other container alike -- carries a `dts_source` evidence object:

```json
{"source": "demuxer", "container_joined": 0, "unjoined_with_pos": 0}
```

`source` is one of `demuxer` (the value PacketScan read verbatim -- every
non-TS input, and any MPEG-TS stream whose container truth could not be
established), `container_pes` (this stream's `dts` came from the PES
header substitution above, and at least one packet joined), or
`container_unavailable` (why this measurement is a skip, MPEG-TS only --
including a stream on which the join was attempted but zero packets
joined). `container_joined` / `unjoined_with_pos` are
`detail::apply_container_dts`'s own join counters (`src/probe/ts_scan.h`),
both `0` when `source` is not `container_pes`.

When `source` is `container_pes` and the join was only partial, the
object also carries `excluded_from_judgment`: the number of samples on
the DTS axis (after AV_NOPTS_VALUE exclusion) whose own packet did not
join -- a split-out frame, or a non-negative-`pos` packet already counted
in `unjoined_with_pos`. Absent when the object's own `source` is not
`container_pes`, or when every sample on the axis joined (`0` is still
reported explicitly in that case, not omitted).

**Contract note:** `dts_source` is a new evidence key, present on every
`timeline.dts_monotonic` measurement as of 05-20-PLAN.md;
`excluded_from_judgment` is a further new key, present only on
`container_pes` measurements, as of 05-REVIEW.md's WR-01 fix.

## Why it matters

A decode timestamp that goes backward or repeats usually means a
corrupted, mis-spliced, or mis-multiplexed stream -- a real player either
stalls, skips, or misbehaves at exactly that point. This check catches
that class of structural defect independent of whether the content itself
looks fine.

## Accept / Tune / Silence

### Accept

If the violation was an intentional edit-point splice or a deliberately
crafted test stream, re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

The default `0` tolerance behaves as exact equality while keeping
`--tol timeline.dts_monotonic=2` (or a `[check.tolerance]` entry in
`mediadiff.toml`) meaningful for a pipeline with known, accepted noise --
`exact` would not allow this override at all.

### Silence

Set `timeline.dts_monotonic` to `ignore` in `[severity]` for a pipeline
where DTS ordering is deliberately not a meaningful signal (e.g. a raw
capture path known to carry harmless reordering). A silenced check's
difference is still computed and shown under `-v`.
