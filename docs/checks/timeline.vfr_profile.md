# timeline.vfr_profile

## What it measures

A six-bucket histogram of every consecutive-interval deviation on a
stream's own timestamp axis, compared by the `dist` semantic (a proportion
comparison, never a pre-divided percentage baked into the reported bin
counts). The six buckets -- `on_grid`, `one_tick`, `one_percent`, `two_x`,
`three_x`, `longer` -- are fixed and non-tunable, evaluated tightest-first
so an interval sitting exactly on a boundary lands in the tighter bucket
and one tick past it lands in the next.

Critically, every bucket is keyed on the interval's deviation from the
**stream's own grid** -- the exact rational ideal interval `derive_cadence`
derives from that stream's own span and interval count -- never from raw
tick counts. This is what makes the same real-world content bin
identically whether it is stored as an MP4 (a coarse, codec-native
timebase) or remuxed to Matroska (a 1 ms timebase) **whenever the true
frame period is exactly representable in both timebases** (e.g. 25 fps,
40 ms exactly in both a codec-native timebase and a 1 ms one): a raw-tick
histogram would bin that identical jitter into different buckets purely
because the two containers happen to store the interval as different tick
counts, which is exactly the cross-container false-positive class this
grid-relative design exists to prevent.

### Quantization rule (UD-2)

An interval's deviation from the stream's own ideal grid interval is
measured as an exact cross-multiplied integer, `Q = interval * ideal_den -
ideal_num` (never a division, never a floating-point subtraction). When
the true frame period is **not** exactly representable at a container's
own tick resolution (e.g. NTSC's 1001/30000s period, ~33.37 ms, has no
exact millisecond representation), even a perfectly regular stream cannot
land its intervals exactly on that ideal -- the container's own tick
granularity forces a small residual. That residual is representational
rounding, not real jitter, so `on_grid` now means `|Q|` is **strictly
below** one tick of the stream's own timebase (`|Q| < ideal_den`), rather
than exact equality. `one_tick` means `|Q|` is **at least** one tick and
**below** two ticks (`ideal_den <= |Q| < 2*ideal_den`) -- the boundary is
strict below on the on_grid side, inclusive at exactly one tick on the
one_tick side: a deviation of exactly one tick is a real, measurable
deviation, never zeroed as rounding. Every bucket at or past
`one_percent` is unchanged by this rule.

For a stream whose ideal interval is an **exact integer number of
ticks** (`ideal_num` an exact multiple of `ideal_den` -- the common case
for any frame rate that divides evenly into its container's timebase),
`|Q|` is itself always a multiple of `ideal_den`, so `on_grid` reduces to
the pre-quantization `|Q| == 0` test and `one_tick` reduces to the
pre-quantization `|Q| == ideal_den` test exactly -- **every such stream
bins identically to before this rule.** Only a non-exactly-representable
ideal interval (NTSC-class content, or any frame rate whose period is not
an integer number of the container's own ticks) is affected.

### Contract impact

This is a **published contract change**, stated here because the release
has not shipped: bin MEANINGS moved, not just bin membership on a few
edge-case files.

- `on_grid` now means "deviation from the ideal is below one tick"
  (previously "exactly zero").
- `one_tick` now means "at least one tick and below two ticks"
  (previously "above zero, up to and including one tick").
- For a stream whose ideal interval is an integer number of ticks, every
  bin count is unchanged.
- A new evidence key, `sub_tick_intervals`, reports the count of
  intervals whose deviation was zeroed as representational rounding (the
  `on_grid` bin's own count, surfaced by name so a reader does not have
  to infer it from the histogram).
- Snapshots taken before this rule shipped compare against new ones with
  these bin meanings changed. Reference snapshots carrying
  `timeline.vfr_profile` measurements must be re-taken.

Before this rule, a lossless MP4-to-Matroska stream copy of NTSC content
(WINDOWS #28) reported `timeline.vfr_profile` as `warn` on both streams:
MP4's native `1/30000` timebase represented the 1001/30000s period
exactly (`on_grid`), while Matroska's mandated 1 ms timebase could only
reach `one_tick`, a real bucket-label difference for genuinely identical
content. Under this rule, both sides' small sub-tick residuals land
`on_grid`, and the pair compares clean -- the fix is the bin definition
itself, never a widened tolerance or a fabricated pass (`FALSE POSITIVES
ARE P0`; `--profile remux`'s own default tolerance is unchanged).

Unlike `timeline.jitter`, this check runs **regardless** of whether
`derive_cadence` classifies the stream as constant or variable frame
rate -- the histogram itself is the signal that tells a reader the stream
is not constant-rate; skipping it on a VFR stream would hide the one
check actually describing that stream's own timing shape. This mirrors
`video.frame_rate.measured`'s own "a VFR classification does not skip
this check" precedent.

This check consumes the same `derive_cadence` call `timeline.jitter`
does -- exactly once per stream, never a second cadence or CFR/VFR
classification -- and runs on every stream carrying timestamps **except**
subtitle streams (same scope as `timeline.jitter`).

## Why it matters

A stream's interval profile is the fingerprint of its own frame-delivery
behavior: a clean CFR stream reports (close to) 100% `on_grid`; a
genuinely variable-rate stream (content-adaptive frame dropping, a
capture-driven VFR source) reports a real spread across the other five
buckets. A shift in this profile between baseline and candidate -- frames
newly landing in `two_x`/`three_x`/`longer` that used to sit `on_grid` --
is the signature of dropped frames, a broken VFR-to-CFR conversion step,
or a capture pipeline losing frames under load, independent of whether
the average measured rate (`video.frame_rate.measured`) still looks
correct.

## Accept / Tune / Silence

### Accept

If the interval-profile shift was an intentional pipeline change (a
switch to a genuinely variable-rate source, or a deliberate
frame-dropping transform), re-run `mediadiff snapshot` on the new
candidate to establish it as the new baseline.

### Tune

The default `2%` tolerance (`--tol timeline.vfr_profile=<value>`) bounds
the worst single bucket's proportion drift between baseline and
candidate. Widen it for a pipeline with a known, expected amount of
interval-profile variance (e.g. a capture path with intermittent, benign
frame-timing noise); tighten it for a pipeline expected to reproduce an
identical interval profile run to run. Since the quantization rule above
landed, a pipeline that remuxes a non-exactly-representable frame rate
(NTSC content into a millisecond timebase, e.g.) into a coarser container
no longer needs a widened tolerance for that reason alone -- the sub-tick
residual now lands `on_grid` on both sides. Widening remains the right
tool for genuine, larger interval-profile variance a pipeline is known to
introduce.

### Silence

Set `timeline.vfr_profile` to `ignore` in `[severity]` for a pipeline with
no dependency on interval-profile shape (e.g. one that already gates on
`timeline.jitter` for CFR content and has no VFR content to profile).
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
