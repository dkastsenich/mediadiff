# timeline.duration.coherence

## What it measures

Whether `timeline.duration`'s own three members (container-declared,
stream-declared, computed) agree with each other, within a fixed
40ms threshold (the same "one frame at 25fps" constant
`timeline.duration`'s own tolerance uses). Reads the SAME three values
`timeline.duration` itself computes -- never a second, independently
derived triple.

Only a pair whose BOTH members are present is testable: an absent member
(a container or stream with no declared duration at all) cannot be
compared, so any pair naming it is skipped rather than treated as a
disagreement. The three testable pairs are checked in this fixed,
documented order: `container_vs_stream`, `container_vs_computed`,
`stream_vs_computed`. The compared value names the FIRST pair (in that
order) whose members disagree by more than the threshold; every
disagreeing pair, and every tested pair, rides in evidence.

This is registered under the `state` semantic (not `exact`): a Finding
fires whenever EITHER side's value is one of the three flagged pairs --
including when BOTH sides carry the identical incoherent value. Under
`exact`'s ordinary baseline-equality rule, two files that share an
incoherent triple would compare `pass`, making the shared incoherence
invisible in `compare` -- exactly the invisibility this check exists to
avoid, mirroring `video.hdr.coherence`'s own identical precedent
(Phase 4 D-10). An all-agreeing case, or a case where no pair is testable
at all (every member but one is absent), both emit the unflagged value,
`coherent`. The state is always visible as this check's own value in
`inspect`, whether or not a comparison ever runs.

### Declared durations on a wrapping MPEG-TS file

This project reads MPEG-TS with libavformat's own overflow correction
turned off, so this project's own doc 04 section 1.2 unwrap sees a real
33-bit PTS/DTS wrap. On a genuinely-wrapping file, the container-declared
and stream-declared members this check tests come from a second,
overflow-corrected re-probe rather than the primary session's own
(wrap-corrupted) values -- evidence carries
`declared_duration_source: overflow_corrected_reprobe`. If that re-probe
fails, times out, or disagrees with the primary session's own stream
layout, both declared members are withheld
(`declared_duration_source: withheld_wrap_uncorrectable`) rather than
compared corrupt -- the withheld case needs no special handling in this
check's own logic beyond that: with both members already `nullopt`, no
pair naming either one is testable, so the pair falls through to the
unflagged `coherent` value exactly as any other "member absent" case
does. Every other file reports `declared_duration_source: demuxer`.

**Contract note:** `declared_duration_source` is a new evidence key on
this check (and on `timeline.duration` above), one of these three exact
string values on every file.

## Why it matters

A file whose container-declared duration, stream-declared duration and
actual computed content length disagree with each other is ambiguous to
every downstream consumer: a player showing the container's own claimed
duration may seek past where content actually ends, or stop showing
progress before the real end. mediadiff reports this incoherence even
when both files share it, because a shared defect that predates this
tool's involvement is still worth surfacing to a reviewer -- it simply
should never, by itself, block a merge.

## Accept / Tune / Silence

### Accept

This check comparing `pass` does NOT mean the triple agrees, and it does
NOT mean both files report the identical value -- it means NEITHER side's
value is one of the three flagged pair spellings. Check the rendered
value directly (via `inspect` or `-v`) to see each side's own testable
pairs and which, if any, disagreed. If the incoherence is intentional (or
pre-existing and accepted), no action is needed -- this check never gates
the exit code.

### Tune

There is no tolerance to tune -- this is a `state`-semantic classification
over a closed vocabulary, not a scalar comparison. The 40ms disagreement
threshold itself is a fixed named constant
(`kDurationIncoherenceThresholdMs`), not a per-profile knob -- D-08's own
rule that a detection parameter (as opposed to a tolerance) stays fixed in
v1.

### Silence

This check is `info` severity in EVERY profile, with no
`[check.profile_severity]`/`[check.profile_tolerance]` override of any
kind -- it never gates the exit code by design, not by omission. Set it to
`ignore` in `[severity]` only if a pipeline has no interest in internal
duration coherence at all; a silenced check's difference is still computed
and shown under `-v`.
