# container.ts.pmt_version_churn

## What it measures

The number of times a program's Program Map Table (PMT) `version_number`
field CHANGED across the file -- the first sighting establishes the
baseline and is never itself counted as a change. One measurement is
emitted per program, at `Scope{Kind::program, index}` where `index` is the
PSI `program_number` (CONT-08, same rule as `container.ts.pcr_interval`).
Split from doc 02's `psi_interval` row (03-CHECK-ROSTER.md): a single
`CheckDef` cannot carry both an `±ms` interval semantic and an `exact`
version-count semantic on the same id, so this is its own check. Unlike
`pcr_interval`/`psi_interval`, this value is directly counted, never
`estimated` -- widening a directly-counted value would silently loosen a
real gate.

## Why it matters

A PMT that changes version mid-stream signals a structural change to the
program (a stream added/removed, a PID reassigned, a codec descriptor
changed) -- something many downstream remuxers and set-top decoders handle
poorly, sometimes dropping the stream entirely or freezing until the next
tune-in. A version-churn count that differs between baseline and candidate
means the two files' PMTs are not just byte-different but structurally
DIFFERENT-CHANGING at different points, which is exactly the kind of
mid-stream instability this check exists to surface.

## Accept / Tune / Silence

### Accept

If the PMT version churn intentionally changed (a deliberate mid-stream
reconfiguration, a different splicing workflow), re-run `mediadiff
snapshot` on the new candidate to establish it as the new baseline.

### Tune

There is no tolerance to tune -- `exact` at `warn` severity means the count
must match exactly to pass.

### Silence

Set `container.ts.pmt_version_churn` to `ignore` in `[severity]` only for a
pipeline that intentionally and routinely reconfigures programs mid-stream
(e.g. a live ad-splicing workflow where PMT churn is expected and
harmless). Leave it enabled everywhere else -- a silenced check's
difference is still computed and shown under `-v`.
