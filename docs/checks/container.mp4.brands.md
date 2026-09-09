# container.mp4.brands

## What it measures

The `ftyp` box's declared major brand plus every compatible brand, as one
set. `evidence` separately names the major brand and minor version, so a
change to the major brand (which selects the file's primary interpretation
-- `isom`, `mp42`, `qt  `, ...) is distinguishable from ordinary
compatible-brand-set churn that many muxers add or reorder harmlessly
across versions.

## Why it matters

The brand list is how a player decides which feature set and parsing rules
to apply before it has read anything else. A major-brand change can alter
how downstream tools interpret the same bytes; a compatible-brand-only
change is usually a muxer-version artifact. Because this is a `set`
comparison, the ORDER compatible brands appear in never itself triggers a
finding -- only an added, removed, or genuinely changed brand does.

## Accept / Tune / Silence

### Accept

If the brand change reflects an intentional muxer or profile change (e.g.
upgrading to a muxer that adds a new compatible brand), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

None -- this check has no tolerance; each brand either matches exactly or
it doesn't.

### Silence

Set `container.mp4.brands` to `ignore` in `[severity]` for a pipeline that
churns muxer versions frequently and has already verified the brand
changes are cosmetic. Leave it enabled everywhere else -- a silenced
check's difference is still computed and shown under `-v`.
