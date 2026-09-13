# video.interlace

## What it measures

A video stream's interlace field order, reported from what the FRAMES
actually are, not from what the container merely declares. The container's
own declaration (`codecpar->field_order`, e.g. an MP4 `fiel` atom or a
Matroska `FieldOrder` element) is cross-checked against the field order the
parser observed on every individual access unit.

The observed value wins whenever a per-frame cross-check was possible: if
every access unit that reported a known field order agreed on one value,
that value is reported. Evidence records both the declared and observed
values, plus a `disagreement` flag -- but the cross-check compares WHICH
FIELD IS CODED FIRST, not the raw declared/observed spelling: a container
that declares a top-coded field order agrees with frames observed as
top-coded even when the two name the display half differently (the
container's `fiel`/`FieldOrder` element and the per-frame parser output
draw from different, non-overlapping spellings of the same coded-first
concept). `disagreement` is recorded only when the two sides genuinely
name different first-coded fields, or when one side names an interlaced
order and the other names progressive or no field order at all.
`disagreement` is evidence only: it never changes this check's pass/fail
status, and never affects the compared `value` itself, so a reader should
not expect it to gate anything. If the access
units disagree with each other (more than one distinct known field order
observed across the stream), the value is `mixed`, with evidence carrying
one exact `num`/`den` rational proportion per distinct observed field
order, summing to one -- never a floating-point percentage. If no access
unit reported a usable field order at all (a codec with no registered
parser, or a registered parser that never populates this field, e.g.
MPEG-4 Part 2's own parser), the declared value is reported, with evidence
recording that no per-frame cross-check was possible -- the declaration is
real information and discarding it would report less than the tool knows,
but it is never presented as if it had been verified against the frames.

`skipped:partial_scan` covers a truncated scan, checked ahead of
everything else. This check does NOT skip on a missing parser the way
`video.gop.*`/`video.frame_types` do -- the declared field order is always
reported.

## Why it matters

A field-order flip (top-field-first becoming bottom-field-first, or vice
versa) produces visible judder on an interlaced display or in a
deinterlacer -- fields are combined in the wrong temporal order, and
motion appears to stutter or tear. The failure mode that makes this worth
a dedicated check is that a typical preview player deinterlaces on the
fly and hides the defect entirely, so a field-order regression can ship
unnoticed until it reaches a display or a downstream tool that actually
respects field order. A container declaration that disagrees with its own
frames is exactly the kind of silent inconsistency a media pipeline can
introduce (a remux that recomputes the container-level flag incorrectly,
or an encoder that declares one thing and encodes another) without any
other check catching it.

## Accept / Tune / Silence

### Accept

If the field-order change was intentional (a deliberate re-interlace, a
switch to progressive, or a corrected declaration), re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

There is no tolerance to tune -- `exact` semantics means any change in the
reported field order (including a change to or from `mixed`) is a
finding. Severity is `fail`: an unnoticed field-order regression is a real,
silent defect for anything that plays or processes the fields directly;
demote it to `warn` in `[severity]` for a pipeline that only ever reaches
progressive-display consumers.

### Silence

Set `video.interlace` to `ignore` in `[severity]` for a pipeline with no
interlaced-display or field-order-sensitive downstream consumer at all.
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
