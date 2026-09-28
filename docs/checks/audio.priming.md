# audio.priming

## What it measures

The number of samples an audio decoder is told to discard at the start of
a stream (encoder priming/pre-roll, e.g. AAC's typical 1024-sample delay),
one measurement per audio stream. **The compared value is a string**: the
decimal sample count (`"1024"`), or the literal `"unknown"` when no source
declares a priming count at all -- never `Absent{}`, never a skip. This
shape is forced, not preferred: a comparator cannot extract a numeric
magnitude from `Absent`, and the whole point of this check (below) is that
`unknown` must compare as a real value in its own right.

Priming is resolved through the SAME four-tier precedence chain
`timeline.av_offset` already established (Phase 5 D-09), extended here
with a third tier:

1. **`skip_samples`** -- the first audio packet's own
   `AV_PKT_DATA_SKIP_SAMPLES` side data. Checked FIRST because MP4 reports
   `initial_padding` as a real 0 while this packet-level signal carries the
   true value -- checking `initial_padding` first would report every MP4
   as unprimed.
2. **`initial_padding`** -- the stream's declared `codecpar->initial_padding`,
   consulted only when tier 1 reported nothing (Matroska's `CodecDelay` is
   already folded in here by libavformat itself).
3. **The container-mechanism tier** (`mp4_edit_list`, `mp4_itunsmpb`,
   `mkv_codec_delay`) -- the raw MP4 `elst` media_time / MKV `CodecDelay`
   reading, consulted for RESOLUTION only when tiers 1 and 2 both report
   nothing. Verified against the linked FFmpeg 8.1 (this phase's own
   research): libavformat already folds MP4 `elst`, iTunSMPB and MKV
   `CodecDelay` into the two fields above in the common case, so this tier
   only ever wins on the edge cases where that fold does not happen (a
   genuine multi-entry MP4 edit list, or a fragmented MP4 whose
   `advanced_editlist` logic auto-disables). It is consulted for EVIDENCE
   always: whenever a container reading is available and it disagrees with
   the resolved value, BOTH readings are recorded in
   `conflicting_readings` so `inspect`/`--explain` can show the
   discrepancy -- the resolution itself stays deterministic; the
   highest-precedence source always wins.
4. **`unknown`** -- no source declares anything (MPEG-TS is the common
   real-world case: it carries no trim mechanism at all).

**Zero and absent are different states.** A stream that DECLARES a zero
priming count (a real, present container-mechanism reading of exactly 0)
records `"0"` with its own source, distinguishable from a stream with NO
priming signal at all, which records the literal `"unknown"` -- the two
never compare equal.

### Evidence

`{state, source, samples, padding}` -- Phase 5 D-10's own `priming`
evidence shape (`state`/`source`/`samples`), extended with `padding`:

- `state`: `"known"` or `"unknown"`.
- `source`: which tier resolved the value (`skip_samples`,
  `initial_padding`, `mp4_edit_list`, `mp4_itunsmpb`, `mkv_codec_delay`, or
  `unknown`).
- `samples`: the resolved sample count (`0` when `state` is `"unknown"`).
- `padding` (D-17): the TRAILING padding sample count -- the second half
  of the same `AV_PKT_DATA_SKIP_SAMPLES` side-data record, captured from
  the stream's own LAST packet that carries it. Always present in
  evidence, `null` when no packet in the stream ever carried the side
  data. Trailing padding has NO check id of its own: check IDs are forever,
  and promoting padding to its own id later stays additive.
- `container_reading` (when available): the container-mechanism tier's own
  raw reading, independent of which tier actually won resolution.
- `conflicting_readings` (when a disagreement was found): both the
  resolved reading and the disagreeing container reading, named by source
  and sample count.

A file with no audio stream reports `skipped:insufficient_data` -- this is
the ONLY reason this check ever skips. Unknown priming is never one of
them: the check is applicable, it measured, and the measurement is that
nothing is declared.

## Why it matters

Losing (or gaining) priming signaling between two otherwise-equivalent
files is a real, audible difference: the decoder either discards the wrong
number of samples at the start of playback, or discards none at all,
shifting every subsequent sample's effective start time. A lossless
container remux that happens to lose the ability to express priming (the
common MP4-to-MPEG-TS case) is exactly the kind of silent regression this
project exists to catch.

## Round-trip stability, and its limit

A SINGLE container hop that changes the priming MECHANISM without changing
its EFFECT reports `pass`: an MP4 remuxed to Matroska keeps the same
resolved sample count even though the MP4 side resolves through
`mp4_edit_list` and the Matroska side through `mkv_codec_delay` (both
visible in `container_reading.source`) -- this is AUDIO-04's central
claim, and it holds.

A DOUBLE hop (MP4 -> MKV -> MP4) is not guaranteed to hold to the sample:
converting `initial_padding` to Matroska's own nanosecond-granularity
`CodecDelay` and back to an MP4 edit-list `media_time` can round the value
by a handful of samples (measured directly against this project's own
linked FFmpeg 8.1: a `1024`-sample MP4 round-tripped through MKV came back
as `1014`, a real ~10-sample rounding artifact of the intermediate's own
`CodecDelay` field, not a bug in this check). Because `audio.priming` is
registered `exact` over a string value, there is no tolerance band that
could absorb that rounding -- a double hop through a nanosecond-granularity
intermediate is exactly the case this check is designed to catch, not
paper over. Treat a double-hop priming mismatch as a real property of that
specific transcode chain, not a false positive.

## The doc 05 section 2 amendment

Two divergences from doc 05 section 2's literal wording, both recorded
here in the open (the same discipline Phase 5 D-14 used for `PERF-03`):

1. **Tier order.** Doc 05 section 2 lists `initial_padding` before the
   packet-level signal. Phase 5 D-09's own empirical finding (re-confirmed
   by this phase's own research against the linked FFmpeg 8.1) is that
   MP4's `codecpar->initial_padding` is ALWAYS 0 for AAC, while the true
   value rides in the first packet's own side data -- the order above is
   amended accordingly, and stays amended here.
2. **Tolerance.** Doc 05 section 2 states a `+/-32 samples` warn tolerance.
   That tolerance is not expressible under `audio.priming`'s own value
   shape: the value is a STRING (the decimal count, or `"unknown"`), and
   `src/compare/tol.cpp`'s numeric tolerance machinery only ever operates
   on `rational`/`int64` values under the `tol` semantic. `audio.priming`
   is registered `exact` for exactly this reason -- there is no numeric
   "close enough" band over `unknown`.

**Known cost, accepted (D-14):** when a later mediadiff build learns
priming from a container mechanism it previously could not read, an old
snapshot's `unknown` becomes a real, comparable value on unchanged media --
a one-time, visible churn already covered by `SNAP-05`'s tool-version-skew
warning.

## Accept / Tune / Silence

### Accept

If the priming change was an intentional re-encode or remux (e.g.
deliberately moving to a container that expresses priming through a
different mechanism, or accepting that a target container cannot express
priming at all), re-run `mediadiff snapshot` on the new candidate to
establish it as the new baseline.

### Tune

No numeric tolerance applies -- this is an `exact` check over a string
value (see the amendment above). A pipeline that deliberately produces
priming-losing remuxes as part of its normal operation should declare that
intent via `--profile transform` or `--profile remux` rather than tuning a
tolerance that has no meaningful "close enough" magnitude for a discrete
string.

### Silence

Set `audio.priming` to `ignore` in `[severity]` for a pipeline with no
dependency on sample-accurate playback start (e.g. one that only cares
about coarse A/V sync, which `timeline.av_offset`/`timeline.av_drift`
cover separately). Leave it enabled everywhere else -- a lossless remux
that silently drops priming signaling is exactly the false-positive-free,
real-regression class this check exists to catch.
