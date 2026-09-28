---
slug: audio-sweep-rate-truncation
status: diagnosed
goal: find_root_cause_only
created: 2026-09-22
updated: 2026-09-22
phase: 06-audio-analysis
branch: gsd/phase-06-audio-analysis
head: 1ba1352
---

# Debug: audio sweep sample rate and decode truncation

## Trigger

DATA_START
The audio decode sweep's sample-rate handling is internally inconsistent, and it is not
determinable by inspection which of three candidate mechanisms is the defect. Diagnose only —
produce a Root Cause Report, apply no fix.
DATA_END

## Symptoms

### Expected behavior

The audio decode sweep configures every sink (libebur128 loudness, true peak, silence detector,
XXH3-128 hash chain) from the rate the decoder actually emits, decodes the whole track unless a
bound is deliberately hit, and reports `sampling_state` honestly when a bound truncates it. The
analyzers' reported spans and the hash chain's block count describe the same decoded audio.

### Actual behavior

Three observations, all reproduced against `build/x64-linux/mediadiff` at HEAD.

**Observation 1 — structural, certain.** `src/probe/audio_decode.cpp:679` seeds `sample_rate_`
from `codecpar.sample_rate`. The first-frame lazy-init block at `:716` only overrides it with
`frame.sample_rate` under `if (sample_rate_ <= 0)`, so the decoder's real output rate is
discarded whenever codecpar declared anything. Everything downstream derives from that number:

- `block_samples_ = sample_rate_ / kAudioBlockDivisor` (`:718`)
- `sink->init(channels_, sample_rate_, name_fmt)` (`:738`) — configures libebur128's 400 ms
  gating blocks and its true-peak oversampling filter
- `edge_hysteresis_samples_`, `dropout_window_samples_`, `dropout_min_span_samples_` (`:757-760`)
- `src/analyzers/audio/silence.cpp:181-182` converts sample indices to ms with
  `Rational{1, decode.sample_rate}`

The comment immediately above the lazy-init block states that codecpar's fields "do not reliably
describe the decoder's actual output format ahead of decode for a compressed codec" — and the
rate is then trusted from codecpar anyway. That self-contradiction is a fact of the code.

**Observation 2 — measured, and it CONTRADICTS the leading hypothesis.** `06-REVIEW.md` CR-01
predicts `tests/fixtures/audio_sbr_implicit.mp4` is "a 44100 Hz core decoding at 88200" and that
the sink therefore receives 44100. It does not. Measured with
`mediadiff inspect <fixture> --content --json`:

| fixture | `audio.sample_rate` | `element_stride` | `element_count` |
|---|---|---|---|
| `audio_sbr_implicit.mp4` | 88200 | 8820 | 1 |
| `audio_sbr_explicit.mp4` | 44100 | 4410 | 1 |
| `audio_aac_handwritten.mp4` | 44100 | 4410 | 1 |
| `timeline_drift_base.mp4` | 48000 | 4800 | 200 |

`element_stride` is exactly rate/10 in every case, so the sink's rate equals the reported rate.
The implicit fixture's sink got 88200, which looks correct — plausibly because
`avformat_find_stream_info` already decoded frames and updated codecpar before the sink opened.
CR-01's stated reproduction therefore does not reproduce, even though its code reading is right.

Note also that the IMPLICIT fixture reports the DOUBLED rate while the EXPLICIT one reports the
core rate — the opposite of what header-only SBR signalling predicts. And
`src/analyzers/audio/stream_params.cpp:163-171` asserts as established fact (citing
`06-RESEARCH.md` Q4) that "the COMPARED value stays the core rate always" and that codecpar's
rate is the undoubled base rate for an implicitly signalled stream. The measurement contradicts
that comment. `06-REVIEW.md` WR-09 repeats the comment's claim.

**Observation 3 — the strongest anchor, reproducible, independent of any rate theory.** Both SBR
fixtures produce `element_count=1`, i.e. roughly one 0.1-second hash block of decoded audio. Yet
`audio.silence.edges` on `audio_sbr_implicit.mp4` reports a single span of 0 → 12000 ms, and
`audio.loudness.integrated` reports −70 LUFS (libebur128's silence floor). A 12-second silence
span cannot be derived from ~0.1 s of decoded samples at any sample rate — 120× apart. Either
the silence analyzer is not measuring over the decoded samples at all, or the decode is being
truncated while the analyzers still believe they saw the whole track.

### Error messages

None. Every one of the 1208 tests passes; nothing in this cluster is exercised by the suite.

### Timeline

Introduced during phase 6 (audio analysis), branch `gsd/phase-06-audio-analysis`. Surfaced by
the phase's own code review (`06-REVIEW.md`, commit `ebe25cc`) and independently corroborated in
part by `06-VERIFICATION.md` (commit `8b7488e`). Never worked differently — the audio decode
sweep is new in this phase.

### Reproduction

```bash
./build/x64-linux/mediadiff inspect tests/fixtures/audio_sbr_implicit.mp4 --content --json
./build/x64-linux/mediadiff inspect tests/fixtures/timeline_drift_base.mp4 --content --json
```

Compare `audio.sample_rate`, `content.audio.sample_hash.element_stride`,
`content.audio.sample_hash.element_count` and `audio.silence.edges` across the two.

## Questions this session must answer

- **(a)** What actually explains observation 3 — a 12000 ms silence span over ~0.1 s of decoded
  audio? **Start here**; it does not depend on resolving the rate question first.
- **(b)** Can `codecpar.sample_rate` ever differ from `frame.sample_rate` at sink-open time given
  this codebase's call order, on real media rather than the hand-written fixtures? Is the `:716`
  guard a live defect or only a latent one?
- **(c)** Are the `stream_params.cpp:163` comment and `06-REVIEW.md` WR-09 correct that the
  compared value stays the core rate, given that the implicit fixture measurably reports 88200?

## Candidate mechanisms (to test, not to assume)

- The `:716` rate guard (CR-01). Code reading confirmed; claimed reproduction disproved.
- `06-REVIEW.md` WR-02: `sampling_state` hardcoded to `"full"` even when the DoS limit truncated
  the decode.
- `06-REVIEW.md` WR-03: the decode DoS bound counts send errors only and is off by one from its
  own constant.

If decode truncation is real, observation 3 follows directly and observation 2's
`element_count=1` is explained without any rate defect at all.

## Constraints

- **Never** verify a libav claim against the `ffmpeg`/`ffprobe` on `PATH` — that is a different,
  GPL-enabled system build. Use `build/x64-linux/vcpkg_installed/x64-linux/lib/*.a` or the built
  `mediadiff` binary.
- **Do not** regenerate fixtures with `scripts/gen_corpus.sh`. Fixture digests in
  `tests/golden/CORPUS_DIGEST.txt` were transcribed from a designated CI leg; regenerating
  silently rewrites them while the local assert still passes.
- **Do not** trust `06-REVIEW.md`'s narrative as established fact. Observation 2 shows one of its
  central claims does not reproduce.
- Diagnose only. Produce a Root Cause Report; apply no fix.

## Evidence files

- `.planning/phases/06-audio-analysis/06-REVIEW.md` (CR-01, WR-02, WR-03, WR-09)
- `.planning/phases/06-audio-analysis/06-VERIFICATION.md`
- `src/probe/audio_decode.cpp`
- `src/probe/packet_scan.cpp`
- `src/analyzers/audio/silence.cpp`
- `src/analyzers/audio/loudness.cpp`
- `src/analyzers/audio/stream_params.cpp`
- `src/compare/hash.cpp`

## Current Focus

status: DIAGNOSED. All three questions answered. Diagnose-only mode -- no fix applied.

confirmed_root_cause: `src/core/serializer.cpp:26-30` (`rational_value_to_json`) derives the
  rendered `ms` field as `(num/den)*1000` on a documented assumption that `num`/`den` hold
  SECONDS. No producer in the codebase emits seconds: `detail::ticks_to_ms`
  (`src/analyzers/timeline/start_duration.cpp:169-178`) emits MILLISECONDS, and `compare_tol`
  (`src/compare/tol.cpp:61-76`) documents the field as "already expressed in the check's declared
  unit". Every time-valued check therefore renders `ms` 1000x too large, and every non-time
  RationalValue check renders a meaningless `ms` at all. Display-only -- `value_from_json`
  (`src/core/serializer.cpp:346`) never reads `ms` back, so no verdict is affected.

observation_1: real but LATENT, severity overstated by CR-01 -- 0 divergences in 144 audio streams.
observation_2: FALSE PREMISE -- the two SBR fixtures do not share a core rate (22050 vs 44100).
observation_3: FULLY EXPLAINED by the root cause -- the span is 0 -> 12 ms, not 0 -> 12000 ms.

next_action: none -- return the Root Cause Report to the caller.

## Investigation Log

### Phase 0 — knowledge base

- timestamp: 2026-09-22T00:00Z
  checked: `.planning/debug/knowledge-base.md`, keyword overlap on audio/decode/sample_rate/truncation
  found: **`phase6-packet-scan-instr-20pct`** is a same-phase, same-subsystem match. Its root
    cause included: "the single allowed packet is consumed entirely as encoder-delay priming
    (`AV_PKT_DATA_SKIP_SAMPLES` start_skip=1024) so `avcodec_receive_frame` returns EAGAIN".
    Also `true-peak-cross-platform` (cycles 1+2) establishes that `audio.loudness.*` sinks and
    the decode path have been audited before and the CHECKS were correct both times.
  implication: A bounded decode loop that treats EAGAIN / a priming-only first packet as a
    terminal condition is a PROVEN failure mode in this exact file family. Hypothesis candidate
    for observation 3 (element_count=1): the decode loop exits early, not that the rate is wrong.
    Test FIRST, do not assume.

### Phase 1 — observation 3 dissolves: the rendered `ms` field is 1000x inflated

- timestamp: 2026-09-22T00:05Z
  checked: `src/core/serializer.cpp:21-32` (`rational_value_to_json`) against
    `src/analyzers/timeline/start_duration.cpp:169-178` (`detail::ticks_to_ms`).
  found: The serializer's stated contract is "`num`/`den` already carry the rational VALUE itself
    (**seconds**, for a time measurement) ... so the derived `ms` convenience field is simply
    `(num/den)*1000`". But `detail::ticks_to_ms` returns
    `RationalValue{ms, 1, Rational{1,1}}` where `ms = ticks*1000*tb.num/tb.den` is **already in
    milliseconds**. Every RationalValue produced by `ticks_to_ms` therefore renders an `ms` field
    exactly 1000x too large.
  implication: The `12000 ms` in observation 3 is NOT 12 seconds. `num` is 12; the real span is
    **0 -> 12 ms**. Observation 3's "120x apart" is an artifact of reading the rendered `ms`.

- timestamp: 2026-09-22T00:06Z
  checked: `mediadiff inspect tests/fixtures/timeline_drift_base.mp4 --content --json`, a fixture
    whose real duration is known to be 20 s.
  found: `timeline.duration` = `{"num": 20000, "den": 1, "tb": {1,1}, "ms": 20000000.0}`.
    The `num` (20000) is the correct millisecond count; the rendered `ms` says 20,000 seconds.
  implication: CONFIRMS the 1000x render inflation on an independent, known-ground-truth value,
    and proves it is NOT specific to audio or to phase 6 -- `timeline.duration` is a phase-5
    check. Control: `audio.loudness.integrated` = `{"num": -21757, "den": 1000, ..., "ms":
    -21757.0}` renders correctly, because loudness does NOT go through `ticks_to_ms`. The defect
    is scoped exactly to `ticks_to_ms`'s unit contract, not to the serializer.

- timestamp: 2026-09-22T00:10Z
  checked: `src/compare/tol.cpp:61-76` -- the COMPARATOR's own documented contract for a
    RationalValue; and `src/core/serializer.cpp:346` -- `value_from_json`'s read-back.
  found: `compare_tol` states "`num` is the measured quantity **already expressed in the check's
    declared unit**". `value_from_json` explicitly does NOT read `ms` back ("a derived
    convenience field excluded from comparison"). Two contracts for the same field disagree:
    serializer says SECONDS, comparator says DECLARED UNIT.
  implication: The comparator is right and the serializer is wrong -- proven by ground truth
    (`timeline.duration` num=20000 for a 20 s file). The 1000x error is therefore **display-only**
    and CANNOT produce a wrong verdict. It is a reporting defect, not a comparison defect.

- timestamp: 2026-09-22T00:12Z
  checked: the human-readable TEXT renderer (`mediadiff inspect <f> --content`, no `--json`) --
    per the standing "verify output-absence claims, check text outputs too" rule.
  found: The text renderer is affected IDENTICALLY, and self-contradicts inside a single record:
    `timeline.duration video[0]: {"num":20000,...,"ms":2e+07}` printed directly above its own
    `evidence: {"computed_ms":20000, ...}`. The same line reports 20,000,000 ms and 20,000 ms.
  implication: Both rendered surfaces are wrong, not just `--json`. A human reading any mediadiff
    report sees a 20-second file described as 20,000 seconds long.

- timestamp: 2026-09-22T00:14Z
  checked: `git log -L` on both sides of the broken contract.
  found: `rational_value_to_json`'s seconds assumption is commit **19d51ed** (2026-08-15, phase
    02-01), written when `tol.cpp`'s own comment records "This engine layer has no real analyzer
    feeding it yet (02-CONTEXT.md D-10 -- Phase 2 proves the engine without media)".
    `ticks_to_ms`'s ms-valued return is commit **fafcdc0** (2026-09-16, phase 05-01), the FIRST
    real time analyzer.
  implication: Pre-existing since phase 5, NOT introduced by phase 6. The debug file's Timeline
    section ("Introduced during phase 6") is wrong for this defect. A phase-2 placeholder
    invariant was never re-validated when phase 5 supplied the first real producer.

### Answer to question (a)

**The silence span is 0 -> 12 ms, not 0 -> 12000 ms.** There is no 120x discrepancy and no
decode truncation. At 88200 Hz, 12 ms ~= 1058-1146 samples, against a single decoded HE-AAC
frame of 2048 samples -- the span is the leading ~half of the one decoded frame, which is
exactly what a leading-edge silence detector should report. Self-consistency checks:
`element_count=1` is the trailing PARTIAL block (2048 < 8820 stride), `sampling_state: "full"`
is HONEST (nothing truncated), `decode_error_count: 0`, and `audio.loudness.true_peak`
-21.829 dBTP proves real signal is present, so the run correctly CLOSES rather than running to
end-of-stream. `audio.loudness.integrated` -70 LUFS with `"state": "silent"` is libebur128's
floor for a 23 ms input -- shorter than one 400 ms gating block -- not a claim of silence.

### Phase 2 — question (b): is the `:716` rate guard live or latent?

- timestamp: 2026-09-22T00:25Z
  checked: Built a standalone C probe against the project's OWN pinned vcpkg FFmpeg
    (`build/x64-linux/vcpkg_installed/x64-linux/lib/*.a` -- never the GPL system ffmpeg),
    replicating mediadiff's exact call order: `avformat_open_input` ->
    `avformat_find_stream_info` -> read `codecpar` -> `avcodec_open2` (aac_fixed under "auto")
    -> `av_read_frame` -> send/receive. Swept ALL 200 readable fixtures / 144 audio streams.
  found: **`codecpar_vs_frame_MISMATCH = 0` out of 144.** Also: `pre_fsi_zero = 42` (streams whose
    `codecpar.sample_rate` is 0 BEFORE find_stream_info) and `pre_fsi_diff = 1` --
    `audio_sbr_implicit.mp4`, which goes **44100 -> 88200 ACROSS find_stream_info**.
  implication: The `:716` guard is **LATENT, not live**. Making it unconditional would change
    nothing on any stream in this corpus. But the single `pre_fsi_diff` case shows exactly WHY
    it works: `codecpar`'s header value IS wrong for implicit SBR, and `avformat_find_stream_info`'s
    own internal decode is the ONLY thing that corrects it before `ensure_initialized` reads it.

- timestamp: 2026-09-22T00:28Z
  checked: Falsification attempt -- re-ran the probe with `find_stream_info`'s probing
    deliberately starved (`probesize=32`, `max_analyze_duration=1`) to try to make it return
    success WITHOUT having reconciled the rate.
  found: `fsi_rc=0` and `codecpar=88200` in all three configurations. Could NOT force divergence.
    Mechanism: `has_codec_parameters()` also requires `codecpar->format` (the sample format),
    which no container carries for AAC/MP3, so find_stream_info always decodes at least one
    frame for a compressed audio stream and always writes the decoder's real rate back.
  implication: The guard's correctness rests on an UNDOCUMENTED, load-bearing libav invariant.
    This makes `:716` correct-by-accident rather than correct-by-design: the comment directly
    above it warns that codecpar "does not reliably describe the decoder's actual output format",
    and that warning is TRUE of the raw header -- it is only false because a libav call
    three layers away already fixed it. CR-01's code reading is right; its severity is not.
  ALTERNATIVE NOT RULED OUT: a stream whose first packets fail to decode inside find_stream_info
    but succeed later would still diverge. Not constructible without generating new media
    (forbidden by this session's constraints). Recorded as residual, not as a measured finding.

### Phase 2 — question (c): is the `stream_params.cpp:163` core-rate claim correct?

- timestamp: 2026-09-22T00:32Z
  checked: `tools/gen_he_aac.py:167-171` and `:565-567` -- the generator that WRITES both SBR
    fixtures.
  found: Verbatim: "index 4 = 44100 Hz (this writer's IMPLICIT and decode-domain rate), index 7 =
    22050 Hz (**the EXPLICIT fixture's CORE rate** ... relationship: 22050 core, SBR extension to
    44100)". The explicit fixture is built `sampling_index=22050, ext_sampling_index=44100`.
  implication: **The two SBR fixtures do not share a core rate.** implicit = 44100 core -> 88200
    doubled; explicit = 22050 core -> 44100 doubled. Both therefore report the **DOUBLED** rate.
    Observation 2's "the IMPLICIT fixture reports the DOUBLED rate while the EXPLICIT one reports
    the core rate -- the opposite of what header-only SBR signalling predicts" is FALSE: it
    assumed a shared 44100 core. Behaviour is uniform and correct across both. Corroborated by
    the decoder itself: both fixtures emit **2048** samples/frame (= 1024 core x2, SBR active)
    while plain `audio_aac_handwritten.mp4` emits 1024.

- timestamp: 2026-09-22T00:35Z
  checked: `src/analyzers/audio/stream_params.cpp:146-165` against
    `src/probe/demux_session.cpp:843-862` -- the two comments describing the SAME value.
  found: They contradict each other. `stream_params.cpp:163` asserts "The COMPARED value stays the
    core rate always: 06-RESEARCH.md Q4 proved `codecpar`'s own rate is the undoubled base rate
    for an implicitly signaled stream". `demux_session.cpp` (rewritten by the LATER 06-13 plan)
    asserts the opposite and cites the same fixture: "`avformat_find_stream_info()`'s own internal
    probing can ALREADY resolve the doubled rate into `codecpar->sample_rate`" and "for that path,
    [it] is the doubled rate BY CONSTRUCTION".
  implication: `demux_session.cpp` is right, `stream_params.cpp` is STALE -- it survived 06-13's
    rework of the mechanism it describes. Measurement settles it: implicit reports 88200.
    `06-REVIEW.md` WR-09 repeats the stale claim, so WR-09 inherits the error.

- timestamp: 2026-09-22T00:38Z
  checked: Snapshotted all 200 fixtures and compared the `core_rate_hz` / `effective_rate_hz`
    evidence pair on every `audio.sample_rate` measurement.
  found: **147 measurements: `core == effective` in 147, differs in 0.**
  implication: The `effective_rate_hz` evidence key is INERT. It can only differ when
    `implicit_probe_rate_hz_` is populated, which only the FALLBACK probe does, which 06-13 made
    unreachable for any stream whose profile the header pass resolved -- i.e. every stream that
    exists here. The core-vs-effective distinction the check was designed to expose can never
    fire. This is the same shape as the KB's `true-peak-cross-platform` lesson: "a gate that
    cannot fire on the failure it was written for is not harmless".

### Phase 3 — candidate mechanisms WR-02 and WR-03

- timestamp: 2026-09-22T00:45Z
  checked: `src/analyzers/content/sample_hash.cpp:100` and `:181`, against
    `src/compare/hash.cpp:159` (`kPreconditionKeys`) and `:181` (`first_precondition_mismatch`).
  found: WR-02 is REAL but LATENT. `sample_hash.cpp:100` DOES guard the packet-budget truncation
    path (`packet_scan.per_stream[i].partial` -> `skip: partial_scan`), so the `max_bytes` /
    `max_packets_per_stream` ceilings are handled honestly. The ONLY truncation that escapes is
    `AudioDecodeState::consecutive_error_limit_hit_`, which sets no packet-scan flag. In that
    state `sampling_state` is still the hardcoded literal `"full"`.
  implication: `sampling_state` is one of three `kPreconditionKeys` whose stated purpose is that
    a mismatch degrades to `skipped:hash_incomparable` "never a fabricated pass or fail". Because
    it is a constant, it can NEVER mismatch, so a truncated-vs-untruncated pair compares as a
    real content FAIL instead of degrading. That is the P0 false-positive class by PROJECT.md's
    own definition -- but it did NOT produce any observation in this session.

- timestamp: 2026-09-22T00:48Z
  checked: Reachability of `consecutive_error_limit_hit_` across the corpus
    (`kMaxAudioDecodeErrorsPerStream = 64`, `src/probe/audio_decode.cpp:43`).
  found: Worst case in the corpus is `audio_corrupt_frames.mp4` with `decode_error_count: 7` --
    an order of magnitude below the 64 bound, and non-consecutive. `audio_undecodable.mp4`
    already skips `partial_scan` via the `decode.undecodable` guard. No fixture trips the limit.
  implication: WR-02 is unreachable by any current test, which is exactly why 1208 tests pass.

- timestamp: 2026-09-22T00:50Z
  checked: `src/probe/audio_decode.cpp:957-996` -- the two error-counting branches of
    `feed_packet`.
  found: WR-03's substantive half is CORRECT. `++consecutive_errors_` appears ONLY in the
    `avcodec_send_packet` failure branch; the `avcodec_receive_frame` failure branch increments
    `decode_error_count_` and `break`s WITHOUT touching `consecutive_errors_`. A stream that
    fails exclusively in receive is therefore completely unbounded by this DoS mitigation.
    WR-03's "off by one" half is NOT a defect: `if (consecutive_errors_ > kMax...)` trips on the
    65th consecutive error, which matches the header's own wording "refuses to decode past
    kMaxAudioDecodeErrorsPerStream consecutive errors".
  implication: WR-03 = half right. Real gap in the DoS bound's coverage; no off-by-one.

### Phase 4 — blast radius of the confirmed defect

- timestamp: 2026-09-22T00:55Z
  checked: `mediadiff snapshot` over all 200 readable fixtures; sampled every check that emits a
    RationalValue. Cross-referenced the six files that call `detail::ticks_to_ms`.
  found: `rational_value_to_json` stamps an `ms` field on **every** RationalValue regardless of
    the check's declared unit. From `video_base.mp4`:
      `video.sar`        1:1     -> `"ms": 1000.0`        (a square pixel, as milliseconds)
      `video.dar`        4:3     -> `"ms": 1333.33`
      `video.frame_rate.declared` 25 -> `"ms": 25000.0`
      `video.gop.length` 48 frames -> `"ms": 48000.0`
      `size.stream_bitrate`       -> `"ms": 331294949.49`
      `timeline.duration` 4000 ms -> `"ms": 4000000.0`    (a 4-second file)
  implication: TWO distinct wrongs from one line. (1) The ~12 genuinely time-valued checks routed
    through `ticks_to_ms` (`timeline.start`, `.duration`, `.gaps`, `.jitter`, `.discontinuities`,
    `.discontinuities.flagged`, `.av_offset`, `.av_drift`, `audio.silence.edges`, `.dropouts`,
    plus `container.mp4.fragment_duration`) render 1000x too large. (2) Every NON-time
    RationalValue check (sar, dar, frame_rate, gop.length, bitrate, loudness in dB/LU, the
    `container.ts.*` intervals) carries an `ms` field that is meaningless in any units.
    There is no check in the codebase for which `(num/den)*1000` is the correct millisecond value.

## Root cause analysis

reasoning_checkpoint:
  hypothesis: "Observation 3's 12000 ms span is a rendering artifact, not a decode truncation:
    `rational_value_to_json` computes `ms = (num/den)*1000` on the documented assumption that
    num/den is in SECONDS, while every producer emits the check's DECLARED unit -- milliseconds
    for time checks. The real span is 0 -> 12 ms over a 46 ms file."
  confirming_evidence:
    - "`timeline.duration` on the known-20-second `timeline_drift_base.mp4` reads num=20000 with
       rendered ms=20000000 -- ground truth fixes num/den as milliseconds."
    - "The same record's own `evidence` prints `computed_ms: 20000` beside `\"ms\": 2e+07`."
    - "`compare_tol` (src/compare/tol.cpp:61-76) documents num as 'already expressed in the
       check's declared unit' -- the serializer's seconds assumption contradicts the comparator."
    - "The decoder itself: audio_sbr_implicit.mp4 is 2 packets / 4096 samples @ 88200 Hz =
       46.4 ms, matching its own timeline.duration of 46 ms. A 12 ms leading span fits inside it."
    - "sampling_state=full, decode_error_count=0, element_count=1 (trailing partial block,
       4096 < 8820 stride) -- all mutually consistent with NO truncation."
  falsification_test: "Find any check whose num/den is genuinely in seconds, making (num/den)*1000
    the correct ms. Ran over all 200 fixtures and 26 RationalValue-emitting checks: none exists.
    `video.sar` 1:1 rendering as 1000.0 ms is the reductio."
  fix_rationale: "N/A -- diagnose-only session. Direction: the unit belongs to the CheckDef
    (`check.unit`, which `parse_tolerance` already consumes), so the serializer must either take
    the unit or stop emitting `ms` for non-time checks. Renaming `ticks_to_ms` would treat the
    symptom; the broken invariant is the serializer's undeclared seconds assumption."
  blind_spots:
    - "Did not construct a stream with >=65 consecutive send errors, so WR-02's dishonest
       sampling_state='full' is proven by code reading and reachability analysis, not executed."
    - "Question (b)'s codecpar/frame divergence was disproven on 144 streams of THIS corpus and
       under starved find_stream_info probing, but not on a stream whose first frames fail to
       decode inside find_stream_info and succeed later. Not constructible under this session's
       no-new-fixtures constraint."
  candidate_causes:
    - "CODE: `src/core/serializer.cpp:26-30` assumes seconds; every producer emits declared units."
    - "PROCESS: that invariant was authored in phase 02-01 (19d51ed) when tol.cpp's own comment
       records 'no real analyzer feeding it yet ... Phase 2 proves the engine without media', and
       was never re-validated when phase 05-01 (fafcdc0) landed the first real producer."
    - "DATA: the two SBR fixtures have DIFFERENT core rates (22050 vs 44100), documented only
       inside `tools/gen_he_aac.py` -- not in 06-REVIEW.md, not in docs/checks/. A reader
       comparing them reasonably but wrongly assumes a shared 44100 core."
    - "ENVIRONMENT: RULED OUT. Single host, single binary, fully deterministic and reproducible.
       Explicitly checked because the three nearest KB entries were all environment/data
       AND-gates -- this one is not."
  and_gate: "YES for the presenting observations, NO for the underlying defect.
    The defect itself is a single line with a single cause. But neither observation could have
    been raised without TWO conditions co-occurring: observation 3 needed the 1000x inflation AND
    a fixture only 46 ms long (12000 looks like a 12-second span only if you assume a normal-length
    file); observation 2 needed the stale stream_params.cpp comment AND the undocumented
    differing core rates. Removing either condition in either pair leaves nothing to report."
