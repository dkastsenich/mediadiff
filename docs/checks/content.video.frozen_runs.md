# content.video.frozen_runs

## What it measures

Stretches of a video stream where the picture stops changing: a run of consecutive decoded frames
that are (near) identical. The value is a list of time spans, in milliseconds, measured from the
stream's own first decoded frame. A span is end-exclusive: it ends one frame interval after its last
frozen frame. A stream that decodes but has no frozen run reports an **empty span list** as a real
measured value -- never a skip and never `Absent{}` -- so "nothing frozen" and "not measured" stay
distinguishable.

**How a run is found.** Every decoded frame (whatever `--sample N` is, since sampling thins only what
is hashed and stored) is reduced to the same deterministic 128-wide, 8-bit luma thumbnail
`content.video.perceptual` uses, and each consecutive pair of thumbnails is scored with an integer
8x8-window SSIM. The rule is hysteresis:

- a run **starts** when a consecutive-frame SSIM is above **0.9995**;
- it **continues** while each following score is above **0.995**;
- it is reported only if it covers at least **3 frames**.

**Why hysteresis, and why the same rule for every decoder.** Exact frame-hash equality is the obvious
freeze test, and it does not work on lossy video. Measured on twelve encodes of one synthetic
50-frame freeze, exact equality fragments the run at every I-frame and at every P-frame refinement,
finds nothing at all for MPEG-2 or for H.264 without a refresh, and moves with GOP length. A span
that moves with GOP structure is precisely the kind of finding that teaches a team to mute the gate.
A single 0.9995 threshold was better but split the run in three of the twelve encodes, because an
I-frame refresh inside a frozen region dips the pair score to about 0.998. With the two thresholds the
span was identical across all twelve encodes (MPEG-4 with and without B-frames, MPEG-2, H.264, H.265,
VP9, AV1 and MJPEG). The rule does not branch on the decoder's determinism class, so the same file
reports the same spans on every machine.

**Timing.** Times come from the frames' own presentation timestamps, relative to the first decoded
frame, so an MPEG-TS remux whose muxer shifts every timestamp reports the same spans as its MP4
source. One frame interval is the stream's declared frame rate, or -- when a container declares none
(MPEG-TS does not, at open) -- the smallest timestamp step the stream itself shows. A stream whose
frames carry no timestamps (a raw elementary stream) is placed by decode index times the frame
interval; the evidence says which (`timing: pts` or `index`).

**Introduced and removed runs.** The check uses the `span` semantic. A frozen run in the candidate
that overlaps no baseline run is **introduced** and gates at the check's severity. A baseline run the
candidate no longer has is **removed** and is reported at `info`, never gating.

**When it does not measure.** `skipped:requires_decode` under `--no-content`, or when the decoder
could not be opened; `skipped:partial_scan` when the packet scan or the decode stopped early (a
decode-error bound or the frame-record budget -- the evidence carries `decode_truncation_reason`),
because a prefix's spans compared against a full stream would fabricate introduced or removed runs;
`skipped:insufficient_data` when no frame decoded or a thumbnail could not be made; and
`skipped:no_timing_data` when a run exists but the stream gives no way to place it in time.

**Honest limit.** A near-static smooth source (a slow gradient, a still title card with grain) is
indistinguishable from a freeze at 128 pixels wide. Both sides of a real compare flag the same
content identically, and only an *introduced* run gates, so this does not by itself produce a false
alarm. A starved encode can also move a boundary: one MPEG-2 encode of the test clip at 1 Mbit/s
(CIF) started its run one frame (40 ms) late because the first pair of the freeze was coded just below
the 0.9995 enter threshold, while every encode at a normal quality reported the exact frame. Spans are
compared by overlap, so a one-frame boundary shift never turns into an introduced run. The constants
were validated on synthetic content only; a real-content review is on the project's ledger.

## Why it matters

A frozen picture is one of the most common silent media failures and one no container check sees: the
file is valid, the duration is right, the bitrate even drops, and the viewer watches a stuck frame. An
encoder that stalls on a bad input, a capture pipeline that drops its source, or a packager that
repeats the last frame all look healthy to ordinary CI. Reporting the exact time range tells you where
to look.

## Accept / Tune / Silence

### Accept

If the freeze is intentional (a held title card, a deliberate still), re-run `mediadiff snapshot` on
the new candidate to make it the new baseline, so the same span is expected from then on.

### Tune

The thresholds (`kFrozenEnterMicro` = 999500, `kFrozenContinueMicro` = 995000) and the 3-frame
minimum (`kFrozenMinFrames`) are fixed, named **detection** constants, not configurable knobs in v1. A
detection parameter changes the *measured* span list, unlike a tolerance, which only changes the
verdict (Phase 5 D-08). `--tol` and severity overrides still decide whether an introduced span gates
the exit code.

### Silence

Set `content.video.frozen_runs` to `ignore` in `[severity]` for a pipeline whose content is
legitimately static for long stretches (a slide recording, a surveillance feed at night). A silenced
check's difference is still computed and shown under `-v`.
