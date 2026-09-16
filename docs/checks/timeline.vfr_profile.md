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

One honest limitation, discovered empirically rather than assumed: when
the true frame period is **not** exactly representable at a container's
own tick resolution (e.g. NTSC's 1001/30000s period, ~33.37 ms, has no
exact millisecond representation), that container's own intervals can
never land exactly `on_grid` -- the closest achievable label is
`one_tick`, one tick away from the (irrational-at-this-precision) ideal.
A source stored at a finer, exactly-representable native timebase (the
original MP4) still reports `on_grid`, so the two sides' histograms
genuinely differ by one bucket step even though both are equally
"as on-grid as their own container's precision allows." This is a true
difference in what each container can represent, not a defect in either
the check or the underlying media -- `--tol` (below) is how a pipeline
that remuxes this class of content routinely absorbs it.

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
identical interval profile run to run. A pipeline that routinely remuxes
non-exactly-representable frame rates (NTSC content into a millisecond
timebase, e.g.) into a coarser container hits the `on_grid`-vs-`one_tick`
shift described above on every such file -- widen the tolerance (or use
`--profile remux`'s own override point) rather than treat it as a
regression each time.

### Silence

Set `timeline.vfr_profile` to `ignore` in `[severity]` for a pipeline with
no dependency on interval-profile shape (e.g. one that already gates on
`timeline.jitter` for CFR content and has no VFR content to profile).
Leave it enabled everywhere else -- a silenced check's difference is still
computed and shown under `-v`.
