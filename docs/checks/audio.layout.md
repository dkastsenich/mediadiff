# audio.layout

## What it measures

The audio stream's canonical channel layout description
(`av_channel_layout_describe`), one Measurement per audio stream at
`Scope{audio, N}`. Derived from the header pass alone -- no decode is
required, so it reports a real value under `--no-content`. The value is
derived exclusively from the modern per-stream channel-layout struct;
neither a legacy integer channel-mask field nor a channel-count-derived
guess is ever consulted, so `5.1` and `5.1(side)` -- two layouts sharing
the same six-channel count but placing the surround pair at different
speaker positions -- report as **different** values even though
`audio.channels` reports the same count for both.

This check is the sole owner of layout information; `audio.channels`
(above) reports the count only and never the layout.

**A change TO unspecified is metadata loss, not a wildcard match.** When a
stream declares no real channel layout, `av_channel_layout_describe` still
returns a real, deterministic string (`"N channels"`, where N is the
observed channel count) -- this check reports that string as a genuine,
comparable value, never `Absent{}` and never a skip. A candidate that
LOSES a previously-declared layout (going from, say, `5.1` to `"6
channels"`) therefore reports as a regression here, exactly as
`video.color.range` treats a change to `"unknown"` as real information
loss rather than a value this check declines to compare. There is no
wildcard, no "unspecified matches anything" rule anywhere in this check.

A file with no audio stream at all still reports this check as
`skipped:insufficient_data`, rather than emitting nothing -- `skipped !=
pass` is load-bearing.

## Why it matters

A layout change with an unchanged channel count is the headline case this
check exists to catch: a 5.1 mix re-tagged from the SMPTE/ITU rear-surround
convention to the DVD/`5.1(side)` side-surround convention (or vice versa)
plays back with content routed to physically different speakers on any
downstream renderer that trusts the declared layout, even though every
other property of the file -- including the channel count -- looks
unchanged.

## Accept / Tune / Silence

### Accept

If the layout change was an intentional remap (a genuine downmix/remix
target, or a deliberate side-vs-rear surround convention switch), re-run
`mediadiff snapshot` on the new candidate to establish it as the new
baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check over a canonical
string.

### Silence

Set `audio.layout` to `ignore` in `[severity]` for a pipeline where channel
layout is deliberately variable and never a meaningful signal on its own.
**This check is not one to silence lightly for a surround pipeline** -- a
side/rear surround mismatch is exactly the class of silent regression this
check exists to catch, and it produces zero false positives on an
unchanged file (the layout string is deterministic for a given
`AVChannelLayout`).
