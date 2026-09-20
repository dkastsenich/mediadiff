# meta.decode_errors

## What it measures

The count of recoverable decode errors libav itself rejected and recovered from, while decoding
one audio stream inside the shared audio decode sweep `content.audio.sample_hash` and
`audio.loudness.*`/`audio.silence.*` already consume (AUDIO-10: no second decode). A "recoverable"
error is a negative `avcodec_send_packet`/`avcodec_receive_frame` return that is not `EAGAIN` or
`EOF` — the decoder reported it could not use this one packet or produce this one frame, and the
sweep CONTINUES with the SAME decoder afterward, never a re-open and never a different decoder
(D-07: decoder selection is a once-before-the-sweep decision; switching mid-sweep in response to
an error would make the recorded decode path a lie). The count increases monotonically within one
sweep and never resets. The first error's own reason (which libav call failed, and why) is
recorded in evidence so `--explain`/`inspect` can show what kind of error it was.

A clean stream reports a real, comparable **0** — never `Absent{}` and never a skip — under a
zero-magnitude tolerance (`tol`/`count`/`"0"`, the same shape `timeline.dts_monotonic` already
uses and for the identical reason: this behaves like exact equality while leaving
`--tol meta.decode_errors=2` meaningful for a pipeline with known, accepted noise).

### The narrow `undecodable` case

A stream is `undecodable` — and ONLY a stream is `undecodable` — when it produces **zero decoded
frames across its whole sweep** while carrying at least one decode error (a stream that simply has
no packets, or genuinely no audio content, and never errors at all is a different, unrelated case:
`insufficient_data`, never `undecodable`). An undecodable stream marks the whole fingerprint
`partial:true`, which reaches exit 66 through `claude_docs/01-core-concepts.md` §11's own
error-taxonomy mapping — the narrow case that genuinely could not run. Every decode-dependent
audio check (`content.audio.sample_hash`, `audio.loudness.*`, `audio.silence.*`,
`meta.decode_errors` itself) reports `skipped:partial_scan` for that stream: a `partial_scan` skip
is never a fabricated value computed from a decode that did not happen.

### The Amendment (D-09)

`claude_docs/01-core-concepts.md` §11, read literally, marks `partial:true` and exits 66 for ANY
mid-file decode error. Under that literal rule, a baseline carrying one known-bad, stable frame
would exit 66 on every future comparison, forever — a permanently "could not run" gate, and this
project treats a muted gate as worth nothing (PROJECT.md's own core value: "a diff tool that cries
wolf gets muted, and a muted gate is worth nothing" applies just as much to a gate that never runs
at all). **This check is the amendment**: a recoverable decode error is a counted, GATING finding
at exit 1 — a candidate that gains corrupt frames relative to its baseline is caught as an ordinary
regression, while hash, loudness and silence still measure what actually decoded — and only a
wholly undecodable stream (zero decoded frames) still marks the fingerprint partial and exits 66.
This narrows, rather than contradicts, doc 01 §11's own mapping; the narrowing is recorded here,
in the open, and as a matching note against `claude_docs/01-core-concepts.md` §11 itself — the same
recorded-in-the-open form Phase 5 used when 05-12-PLAN.md amended `PERF-03` in
`.planning/REQUIREMENTS.md`. Approved at 06-10-PLAN.md's Task 1 checkpoint
(`approve-amendment`, 2026-09-20).

## Why it matters

Without this check, a recoverable decode error had no comparable representation at all: doc 01
§11's literal rule would either exit 66 forever on a file with one known-bad frame (muting the
gate on exactly the corpora most likely to carry a real regression), or the error would go
unrecorded and unrecorded errors cannot be compared, so a candidate that silently starts producing
corrupt frames on a previously-clean stream would never be caught. Counting and comparing the
error rate closes that gap: it turns "libav quietly recovered from something" into a real,
regression-detectable signal, while keeping the exit-66 path meaningful and rare — reserved for the
stream that genuinely produced nothing at all.

## Accept / Tune / Silence

### Accept

If a candidate's higher decode-error count reflects an intentional change to the source media
(e.g. deliberately re-encoding from a known-lossy or partially-corrupt source), re-run
`mediadiff snapshot` on the new candidate to establish it as the new baseline.

### Tune

`--tol meta.decode_errors=N` widens the zero-magnitude baseline tolerance to accept up to `N`
additional errors before gating — useful for a pipeline whose input corpus is known to carry a
small, stable number of recoverable errors that are not worth chasing. There is no severity
override needed beyond the standard `[severity]`/`--tol` mechanism: this is a plain `tol`/`count`
check, not a detection-constant check (Phase 5 D-08's distinction does not apply here — there is
no detection threshold to fix, only a compared count).

### Silence

Set `meta.decode_errors` to `ignore` in `[severity]` for a pipeline where recoverable decode
errors are deliberately not a meaningful signal (e.g. a known-lossy ingest path where some
per-frame corruption is expected and tolerated end to end). A silenced check's difference is still
computed and shown under `-v`. Silencing this check does NOT silence the `undecodable`/exit-66
path — that path is driven by `Fingerprint::partial`, not by this check's own severity.
