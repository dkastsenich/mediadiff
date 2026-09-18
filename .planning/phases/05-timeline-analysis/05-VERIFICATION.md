---
phase: 05-timeline-analysis
verified: 2026-09-18T00:00:00Z
status: gaps_found
score: 2/5 success criteria verified
behavior_unverified: 0
overrides_applied: 0
re_verification: false
gaps:
  - truth: "SC1 — a spliced 100 ms trim is reported as `step` with the step time"
    status: failed
    reason: >
      timeline.av_drift.pattern's `step` classification is proven, algebraically and
      empirically, unreachable under the shipped checkpoint-construction architecture.
      Reproduced: `build/x64-linux/mediadiff compare --profile sw-encoder --json
      tests/fixtures/timeline_start_base.mp4 tests/fixtures/timeline_drift_step.mp4`
      reports timeline.av_drift.pattern candidate="irregular" (baseline="constant-offset"),
      never "step", despite the fixture containing a genuine, isolated 60ms mid-file
      offset. 05-10-SUMMARY.md's own "Known Limitation" section gives the algebraic proof:
      a single whole-file affine checkpoint map either smears a genuine splice into
      `linear-drift` (when it changes the whole-file duration ratio) or collapses it to a
      transient, single-/few-point `irregular` deviation (when it doesn't) — a lasting
      second plateau (what `step` detection requires) is structurally unreachable. The test
      suite's own case name for this fixture pair says "irregular (step-intended)",
      confirming the gap is documented, not hidden, but it still leaves SC1's literal
      wording unmet.
    artifacts:
      - path: "src/analyzers/timeline/av_sync.cpp"
        issue: "fit_drift's single whole-file affine checkpoint map cannot produce a two-plateau (step) trajectory shape; only constant-offset, linear-drift and irregular are reachable outcomes."
    missing:
      - "A piecewise/local rate-estimation refinement across sub-windows of the K=32 checkpoints (05-10-SUMMARY.md's own recommendation) so a genuine isolated splice classifies as `step` rather than `irregular`."
      - "A fixture (or the existing timeline_drift_step.mp4) that empirically confirms `step` is reachable once the architecture changes, replacing the current declared-set assertion that pins `irregular` as the accepted outcome."
      - "USER DECISION 2026-09-18 (orchestrator note): make `step` reachable via a research-first gap plan — piecewise checkpoint mapping across timestamp discontinuities so doc 04 §5's step recipe (two segments concatenated with a 100 ms audio trim at the join) yields two plateaus and a step time; document that a seamlessly re-timestamped trim is undetectable from timestamps alone (needs Phase 6 audio decode). If research finds no sound design, stop at a checkpoint and fall back to narrowing the published vocabulary (amend SC1, TIME-07, 05-CHECK-ROSTER.md, docs/checks/timeline.av_drift.pattern.md)."
  - truth: "SC2 — MPEG-TS 33-bit wraparound is unwrapped rather than mistaken for a backward discontinuity, across timeline.start, the timeline.duration triple, dts_monotonic, pts_unique, gaps and discontinuities"
    status: failed
    reason: >
      dts_monotonic/pts_unique/gaps/discontinuities correctly unwrap (they route through
      detail::unwrap_axis_view / unwrap_ts_timestamps and pass cleanly on a genuinely-wrapping
      TS file compared against itself — ctest #898 verified green). timeline.start and the
      timeline.duration triple do NOT: start_duration.cpp never calls the shared unwrap
      primitive. Reproduced on a content-identical pair that differs only by a
      -output_ts_offset placed near the 33-bit wrap boundary (scripts/gen_corpus.sh
      timeline_ts_wrap.ts/timeline_ts_nowrap.ts, otherwise identical testsrc2/sine/mpeg4/aac
      recipe): `build/x64-linux/mediadiff compare --profile remux --json
      tests/fixtures/timeline_ts_nowrap.ts tests/fixtures/timeline_ts_wrap.ts` reports
      timeline.start FAIL x3 (global/video/audio, deltas -1378/-23/+206 ms), timeline.duration
      FAIL x2 (video/audio, deltas ~95439717 ms ≈ one full 2^33/90kHz wrap period),
      video.frame_rate.measured WARN, timeline.vfr_profile WARN x2, timeline.av_offset FAIL,
      timeline.av_drift FAIL (nonsensical magnitude), size.stream_bitrate FAIL x2 — none of
      these findings reflect a real content difference (the sole real difference is the applied
      offset, which unwrapping should make transparent to relative-timeline checks). Only
      timeline.wrap_events (info) is a correct finding. This is tracked, still `open`, in
      WINDOWS.md #26 (start_duration.cpp/stream_params.cpp/size.cpp), #27 (jitter_vfr.cpp) and
      #30 (size.cpp/tol.cpp interaction) — this verification independently reproduces all three
      and additionally establishes that src/analyzers/timeline/av_sync.cpp (timeline.av_offset
      / timeline.av_drift) is affected by the identical root cause (no call to
      unwrap_axis_view/unwrap_ts_timestamps or any `.pts`/`.dts` rescale anywhere in the file)
      and is NOT named by any existing WINDOWS.md entry — this extends #26's known scope.
      tests/integration/test_timeline_structure.cpp's own "Test 4" is deliberately NOT a
      whole-report declared-set assertion on this exact pair specifically because of this
      unresolved corruption (its own comment says so), which is itself an admission that the
      pair cannot currently be asserted clean.
    artifacts:
      - path: "src/analyzers/timeline/start_duration.cpp"
        issue: "Reads raw, un-unwrapped PTS/DTS directly for timeline.start / timeline.duration / timeline.duration.coherence on every container including MPEG-TS (WINDOWS.md #26)."
      - path: "src/analyzers/timeline/av_sync.cpp"
        issue: "timeline.av_offset / timeline.av_drift also read raw, un-unwrapped PTS via detail::first_presented_pts with no unwrap step — not yet filed in WINDOWS.md."
      - path: "src/analyzers/video/stream_params.cpp"
        issue: "video.frame_rate.measured reads the raw axis (WINDOWS.md #26)."
      - path: "src/analyzers/timeline/jitter_vfr.cpp"
        issue: "derive_cadence() called on raw, un-unwrapped packets on every container (WINDOWS.md #27)."
      - path: "src/analyzers/size/size.cpp"
        issue: "size.stream_bitrate reads the raw axis; no longer overflows post debug-session test-898-ci-nonreproducible but now produces a false `fail` instead (WINDOWS.md #30)."
    missing:
      - "Extend the shared doc-04 §1.2 unwrap primitive (detail::unwrap_axis_view / unwrap_ts_timestamps, already correctly used by monotonic.cpp and discontinuities.cpp) to start_duration.cpp, stream_params.cpp, size.cpp, jitter_vfr.cpp, and av_sync.cpp."
      - "Orchestrator note: once the unwrap is extended, convert tests/integration/test_timeline_structure.cpp Test 4 (timeline_ts_nowrap.ts vs timeline_ts_wrap.ts) into a whole-report expect_declared_set assertion, removing the D-01/D-02 carve-out; then close WINDOWS.md #26, #27 and #30 (recording av_sync.cpp's inclusion)."
  - truth: "SC4 — timeline.av_offset reports a signed, priming-adjusted offset correctly"
    status: failed
    reason: >
      resolve_priming's returned sample count is added directly to the audio stream's
      first-presented-PTS TICKS in that stream's own native container timebase, with no
      conversion via the stream's sample rate anywhere in src/analyzers/timeline/av_sync.cpp
      (grep for "sample_rate" in that file returns zero matches). This is silently correct
      only when the container's demuxed audio timebase happens to equal the sample rate
      (true for MP4-muxed AAC, tb=1/44100) and is wrong for any other timebase. Reproduced on
      a lossless, content-identical `-c copy` stream-copy remux with no wraparound involved at
      all (tests/fixtures/timeline_ntsc_base.mp4 -> timeline_ntsc_remux.mkv, Matroska's
      mandated 1 ms timebase): `build/x64-linux/mediadiff compare --profile remux --json
      tests/fixtures/timeline_ntsc_base.mp4 tests/fixtures/timeline_ntsc_remux.mkv` reports
      timeline.av_offset FAIL with baseline.raw_offset_ms == candidate.raw_offset_ms == -23
      and identical priming evidence (state=known, source=skip_samples, samples=1024) on both
      sides, yet baseline.adjusted_offset_ms=0 vs candidate.adjusted_offset_ms=1001 — the 1024
      sample count is being misread as 1024 ms on the MKV side (1001 ≈ 1024 - 23 rounding),
      not converted through the stream's 44100 Hz sample rate (which would yield ~23 ms). This
      is a pure computation bug producing a false A/V-sync regression on a stream copy with no
      audio change whatsoever — not a real finding. It is untested: every other
      priming-known av_offset/av_drift fixture in tests/integration/test_timeline_av_sync.cpp
      is an MP4 (tb==sample_rate, masking the bug); the one non-MP4 fixture
      (timeline_avoffset_unknown.ts) has priming.samples=0 (unknown), which also masks it
      (0 * anything = 0 either way). No test declares a set for the
      timeline_ntsc_base.mp4/timeline_ntsc_remux.mkv pair at all — item 3's own review already
      notes this gap in test coverage. Separately, test_timeline_start_duration.cpp's Test 4
      (and its mirror in test_timeline_av_sync.cpp) DOES pin timeline.av_drift /
      timeline.av_drift.pattern as an expected fail on the MP4->MPEG-TS pair
      (90 kHz timebase, priming known via skip_samples survives the -c copy remux), attributing
      the entire effect to the MPEG-TS audio-duration bookkeeping disagreement alone — but
      given this same units bug plausibly also corrupts that pair's own av_drift magnitude
      (90 kHz != 44100 Hz), that test risks pinning a partially-false-positive magnitude as
      "expected" without ruling the units bug out; it should be re-verified once the fix lands.
    artifacts:
      - path: "src/analyzers/timeline/av_sync.cpp"
        issue: "adjusted_audio_ticks = audio_first_pts_ticks + priming.samples (line ~617) treats priming.samples, a sample count, as if it were already in the stream's own PTS timebase; no sample_rate-based rescale is present anywhere in the file."
    missing:
      - "Convert priming.samples into the audio stream's own PTS timebase (e.g. via av_rescale_q against {1, sample_rate} -> stream.tb) before adding it to audio_first_pts_ticks."
      - "A regression fixture with priming known AND a non-44.1kHz-matching container timebase (timeline_ntsc_remux.mkv already exists and is unused for this purpose) with a declared-set assertion pinning the CORRECT adjusted offset."
      - "Orchestrator note: the drift path carries the same conversion error — av_sync.cpp:783 applies `priming_shift = priming.samples` in native ticks; convert it through the same sample-rate rescale."
      - "Orchestrator note: after the fix, re-verify the av_drift / av_drift.pattern verdicts that tests/integration/test_timeline_start_duration.cpp Test 4 (and its timeline_avoffset_unknown.ts mirror) pins on the 90 kHz MP4 -> TS pair."
  - truth: "Goal / SC2 / TIME-04 — timeline.dts_monotonic reports a DTS violation the file does not contain (MPEG-TS read-back inference)"
    status: failed
    reason: >
      ORCHESTRATOR RE-VERIFICATION (2026-09-18), superseding this report's "Findings Evaluated,
      Not Treated As Gaps" judgment on Known Defect #1. Parsing tests/fixtures/timeline_start_shift.ts
      directly: every video PES header carries PTS_DTS_flags='10' (PTS only; first five PTS 128090,
      131690, 135290, 138890, 142490, strictly increasing). Under ISO/IEC 13818-1 an absent DTS means
      DTS == PTS, so the file's decode timeline is strictly monotonic and contains no tie. The
      dts[1] == dts[0] == 128090 that timeline.dts_monotonic counts is fabricated on read-back by
      libavformat's compute_pkt_fields, which re-guesses DTS with a one-frame delay because the
      stream's MPEG-4 VOL header is absent: the -c copy remux dropped the esds decoder configuration
      (the TS video ES carries GOV/VOP start codes but no VOS/VOL). The premise that the tie is a real
      byte-level property of the candidate is therefore false. The missing VOL itself IS real: the
      video.profile/level/resolution fails on this pair are true positives in substance (the TS is not
      decodable standalone), which this gap does not change.
    artifacts:
      - path: "src/analyzers/timeline/monotonic.cpp"
        issue: "prepare_axis(packets, Axis::dts, is_ts) consumes libavformat's packet DTS, which on MPEG-TS may be inferred on read-back rather than read from the PES header."
      - path: "src/probe/ts_scan.cpp"
        issue: "ts_scan parses TS packets (PAT/PMT/PCR/CC/discontinuity_indicator) but not PES headers, so the container-level PTS/DTS presence ISO 13818-1 defines is unavailable to timeline checks."
      - path: "tests/integration/test_timeline_start_duration.cpp"
        issue: "Test 4 (and its timeline_avoffset_unknown.ts mirror) declares timeline.dts_monotonic as expected on this pair, enshrining the inferred tie."
    missing:
      - "Container truth for MPEG-TS decode timestamps: record per-PES PTS/DTS presence in ts_scan (a bounded seam, like 05-07's discontinuity_indicator) and have dts_monotonic, and any other DTS-axis consumer such as jitter_vfr.cpp's CadenceAxis::dts, treat a PES without a DTS field as DTS == PTS instead of libavformat's inferred value."
      - "Drop timeline.dts_monotonic from the declared sets that pin the inferred tie, and add a regression assertion that this PTS-only TS reports dts_monotonic pass."
      - "Keep doc 04's `<=` rule unchanged: a genuine DTS tie present in the container must still count."
  - truth: "Goal / D-06 / TIME-05 — timeline.jitter and timeline.vfr_profile warn on a lossless NTSC MP4 -> MKV stream copy (WINDOWS.md #28)"
    status: failed
    reason: >
      ORCHESTRATOR ADDITION (2026-09-18), by user decision. This report's No-Change Pair Audit
      classified these as true positives (Matroska's mandated 1 ms timebase genuinely alternates
      33/34 ms intervals for NTSC's 1001/30000 s period). The user reaffirmed treating them as false
      positives: D-06 (05-CONTEXT.md) made the vfr_profile bins grid-relative precisely so the
      histogram means the same thing across timebases, and 05-08-PLAN.md Task 2's own acceptance
      criterion expected vfr_profile to pass on this exact pair. Measured: video bins on_grid=119 vs
      one_tick=119 (100% disjoint), audio 173 vs 173; jitter warn on video. The content is identical
      CFR 29.97 in both files, so the bins measure the container's tick resolution, the failure class
      05-03 fixed for video.frame_rate.measured.
    artifacts:
      - path: "src/analyzers/timeline/jitter_vfr.cpp"
        issue: "Bins intervals and computes jitter sigma against the exact ideal interval, so sub-tick quantization of a non-representable period moves every interval from on_grid to one_tick and inflates sigma."
      - path: "docs/checks/timeline.vfr_profile.md"
        issue: "Documents the bin meanings; must state the quantization rule once it changes (docs/checks/timeline.jitter.md likewise)."
    missing:
      - "Quantization-aware binning: a deviation from the ideal interval smaller than one tick of the stream's own timebase is representational rounding; it counts as on_grid and contributes nothing to jitter sigma (integer/rational math only)."
      - "A whole-report declared-set assertion for timeline_ntsc_base.mp4 vs timeline_ntsc_remux.mkv under --profile remux (today only id-targeted tests cover it), landing together with the SC4 priming fix."
      - "Close WINDOWS.md #28."
  - truth: "Memory safety — sorted_pts_with_span reads out of bounds on a single-packet stream (05-REVIEW.md CR-01, WR-01)"
    status: failed
    reason: >
      ORCHESTRATOR ADDITION (2026-09-18) from the phase code review (05-REVIEW.md, commit 381c243),
      confirmed by reading the code. In src/analyzers/timeline/av_sync.cpp, with exactly one valid-PTS
      entry and a non-positive declared duration, i = 0 gives neighbor = i - 1 == SIZE_MAX; the guard
      `i != neighbor` can never be false (neighbor is always i +/- 1), so entries[SIZE_MAX] is read,
      16 bytes before the vector's buffer after pointer wraparound. Reachable from timeline.av_offset,
      timeline.av_drift and timeline.av_drift.pattern on short or truncated media, which for a CI gate
      is untrusted input. No test covers a single-packet stream; WR-01: the helper sits in an anonymous
      namespace, unlike its detail:: siblings, so unit tests cannot reach it.
    artifacts:
      - path: "src/analyzers/timeline/av_sync.cpp"
        issue: "sorted_pts_with_span's neighbor computation underflows when entries.size() == 1."
      - path: "src/analyzers/timeline/analyzers.h"
        issue: "sorted_pts_with_span and its siblings are not exposed via detail:: for unit testing (WR-01)."
    missing:
      - "Handle the single-entry case explicitly (effective_duration = 0) and remove the dead guard."
      - "Expose the helper via detail:: in analyzers.h and add unit tests for 0, 1 and 2 entries with and without declared durations."
human_verification: []
---

# Phase 5: Timeline Analysis Verification Report

**Phase Goal:** The family mediadiff is judged on — every `timeline.*` check and the A/V drift algorithm — computed on integer/rational math with false positives designed out.
**Verified:** 2026-09-18
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria, verified literally)

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---|---|---|
| SC1 | 0.1% drift -> `linear-drift`; spliced 100ms trim -> `step` with step time; pure offset -> `constant-offset`; each fixture fires exactly the intended finding and nothing else; K=32 trajectory stored in fingerprint | ✗ FAILED | `linear-drift` and `constant-offset` verified directly against the real binary (timeline_drift_base.mp4 vs timeline_drift_linear.mp4 -> `linear-drift`; timeline_start_base.mp4 vs timeline_avoffset_video_shift.mp4 -> `constant-offset`, both matching their declared sets). `step` is provably unreachable: timeline_start_base.mp4 vs timeline_drift_step.mp4 classifies `irregular`, never `step` — confirmed by reproduction and by 05-10-SUMMARY.md's own algebraic proof. Trajectory fidelity (TIME-08) independently verified: `ctest --preset x64-linux -R "^integration\.timeline_av_sync - TIME-08"` passed (K=32 checkpoint round-trips through a snapshot unchanged). |
| SC2 | timeline.start, the duration triple, dts_monotonic, pts_unique, gaps, discontinuities all work on presentation timelines; AV_NOPTS_VALUE is a first-class absent; MPEG-TS 33-bit wraparound is unwrapped, not mistaken for a backward discontinuity | ✗ FAILED | dts_monotonic/pts_unique/gaps/discontinuities correctly unwrap (ctest #898 green, a genuine mid-file wrap compared against itself reports zero false gaps/violations). AV_NOPTS_VALUE exclusion confirmed in monotonic.cpp's `build_axis_view`. BUT timeline.start and the duration triple do NOT unwrap: reproduced with `compare --profile remux tests/fixtures/timeline_ts_nowrap.ts tests/fixtures/timeline_ts_wrap.ts` (content-identical pair, differing only by an applied timestamp offset near the wrap boundary) — timeline.start fails x3, timeline.duration fails x2 with a delta of ~95439717 ms (one full wrap period), plus collateral corruption on video.frame_rate.measured, timeline.av_offset/av_drift and size.stream_bitrate. See Gap 2. |
| SC3 | VFR stream classified VFR, jitter `skipped:vfr`; CFR stream reports jitter σ and max deviation; shared interval statistics with video.frame_rate.measured, not a second implementation | ✓ VERIFIED | `compare --profile sw-encoder tests/fixtures/timeline_start_base.mp4 tests/fixtures/timeline_vfr.mp4`: video (VFR) -> `timeline.jitter skipped:vfr`; audio (CFR) -> `timeline.jitter pass, delta 0ms`. `derive_cadence()` confirmed as the single shared primitive consumed by both `src/analyzers/video/stream_params.cpp:369` and `src/analyzers/timeline/jitter_vfr.cpp:477`. (The MP4-vs-MKV NTSC pair's jitter/vfr_profile warn findings are a real, previously-documented grid-representability artifact — WINDOWS.md #27/#28 — not a check defect; see "No-Change Pair Audit" below.) |
| SC4 | timeline.av_offset reports a signed, priming-adjusted offset; on non-zero-priming fixtures the finding visibly carries `priming: unknown` with an unadjusted value rather than a confidently wrong number | ✗ FAILED | The `priming: unknown` / raw-comparison-basis half is verified: `compare --profile remux tests/fixtures/timeline_avoffset_unknown.ts tests/fixtures/timeline_avoffset_video_shift.mp4` shows baseline evidence `priming.state=unknown`, `comparison_basis=raw`. BUT the priming-ADJUSTED half is broken for any container whose audio timebase differs from the sample rate: a units bug (sample count added directly to native-timebase ticks with no sample-rate conversion) produces a false `timeline.av_offset fail` on a lossless MKV stream-copy remux with byte-identical raw offsets on both sides. See Gap 3. |
| SC5 | timeline.timecode reports presence, SMPTE start value, drop-frame flag; metadata+timeline analysis of the 10-min 1080p reference completes ≤3s (recorded, not asserted); measured in CI with regression tracking via committed instruction-count baseline ratchet | ✓ VERIFIED | `compare --profile sw-encoder tests/fixtures/timeline_tc_df.mp4 tests/fixtures/timeline_tc_ndf.mp4`: `timeline.timecode.value` renders `00:00:10;00` (drop-frame, semicolon) vs `00:00:10:00` (non-drop, colon) correctly. `tests/golden/PERF_BASELINE.txt` carries the designated x64-linux leg's committed `plain_instructions=257709408` / `full_instructions=344956981` at commit a56dd9b, matching the orchestrator's CI run 35347845434 exactly. `.github/workflows/*.yml` wires `scripts/measure_timeline_perf.sh --instructions --check-baseline` on the designated leg only, other legs explicitly skip it. Workstation wall-clock recorded in the baseline file's own comment (38.3ms plain / 50.8ms full) is comfortably under the 3s PERF-01 budget, and is recorded/printed, never asserted (D-13), matching SC5's literal wording. |

**Score:** 2/5 success criteria fully verified. behavior_unverified: 0 (every truth above was directly exercised against the real binary, not inferred from presence/wiring alone).

### Findings Evaluated, Not Treated As Gaps

**Known Defect #1 — `timeline.dts_monotonic` fails on MP4 -> MPEG-TS (`timeline_start_base.mp4` vs `timeline_start_shift.ts`).** Reproduced: candidate reports `dts_monotonic video fail, delta +1/1bytes` (one `dts[1] <= dts[0]` tie). Judgment: **this is a true positive, not a false positive to design out.** (1) `src/analyzers/timeline/monotonic.cpp:174` implements `dts[i] <= dts[i-1]` verbatim per `claude_docs/04-timeline-analysis.md` line 32's own pre-phase-5 definition (`count of dts[i] ≤ dts[i-1]`) — the check is faithful to its documented spec, not a phase-5-introduced defect. (2) The tie is a real, structural property of the produced candidate bitstream (verified via `ffprobe -show_entries packet=pts,dts` per the test's own comment): the mpegts muxer genuinely writes a duplicate DTS at the stream's start on this B-frame-less remux, a real byte-level difference from the MP4 source, not a spurious measurement. (3) `tests/integration/test_timeline_start_duration.cpp` Test 4 declares it in the pair's complete expected finding set with an explicit, empirically-verified causal explanation (D-02) rather than silently absorbing it. Not filed as a gap.

> **Orchestrator correction (2026-09-18):** the premise in point (2) above does not hold. The candidate's video PES headers carry PTS only (`PTS_DTS_flags='10'`, PTS strictly increasing), so under ISO/IEC 13818-1 its DTS equals its PTS and the file contains no tie. The tie is libavformat's read-back inference, triggered by the MPEG-4 VOL header that the `-c copy` remux dropped. Re-filed as a gap: see the "timeline.dts_monotonic reports a DTS violation the file does not contain" entry in the frontmatter and the Orchestrator Addendum below.

### No-Change Pair Audit

Every fixture pair in the Phase 5 integration suite where the candidate is a content-preserving transformation of the baseline (remux, stream copy, timestamp shift, wrap, same content in another container):

| Pair | Profile | Recipe relationship | Non-pass findings | Classification |
|---|---|---|---|---|
| timeline_start_base.mp4 / timeline_start_base_copy.mp4 | remux | plain `cp` | none | clean — true pass |
| timeline_ts_nowrap.ts / timeline_ts_nowrap_copy.ts | remux | plain `cp` | timeline.duration.coherence (info, both sides flagged) | true positive — a real, pre-existing container-vs-stream duration bookkeeping mismatch that exists identically on both sides (state semantic), correctly declared and explained in test_timeline_structure.cpp's own Test 5 |
| timeline_tc_ndf.mp4 / timeline_tc_ndf_copy.mp4 | remux | plain `cp` | none | clean — true pass |
| timeline_start_base.mp4 / timeline_start_shift.ts | remux | `-c copy` MP4->TS remux | container.format, video.profile/level/resolution, size.*, meta.tags, timeline.start, timeline.duration.coherence, timeline.dts_monotonic, timeline.vfr_profile, timeline.av_drift(.pattern) | true positives — real container/codec-exposure differences (all declared, causally explained in test_timeline_start_duration.cpp Test 4); dts_monotonic evaluated separately above |
| timeline_start_base.mp4 / timeline_avoffset_unknown.ts | remux | same `-c copy` MP4->TS recipe | identical declared set to the pair above | true positives, same reasoning |
| timeline_ts_nowrap.ts / timeline_ts_wrap.ts | remux | content-identical, differs only by an applied timestamp offset near the 33-bit wrap boundary | timeline.start x3, timeline.duration x2, video.frame_rate.measured, timeline.vfr_profile x2, timeline.av_offset, timeline.av_drift, size.stream_bitrate x2 (all fail/warn); timeline.wrap_events x2 (info, correct) | **false positives** — see Gap 2 |
| timeline_ntsc_base.mp4 / timeline_ntsc_remux.mkv | remux | `-c copy` lossless stream-copy remux to Matroska | timeline.jitter (warn), timeline.vfr_profile x2 (warn), timeline.av_offset (fail) | jitter/vfr_profile are true positives — genuine grid-representability differences between MP4's 1/30000 timebase and Matroska's mandated 1ms timebase for NTSC's non-exactly-representable 1001/30000s period (WINDOWS.md #28, already documented, not a defect); **av_offset is a false positive** — see Gap 3 |

No test in the current suite pins a false positive from Gap 2 as an unconditionally-expected finding without an explanatory disclaimer (`test_timeline_structure.cpp`'s own Test 4 for the wrap pair is deliberately NOT a whole-report declared-set assertion, precisely because of this open corruption — this is a documented carve-out, not a masked defect). `test_timeline_start_duration.cpp` Test 4 (and its mirror for the unknown-priming pair) DOES pin `timeline.av_drift`/`timeline.av_drift.pattern` as expected on a 90kHz-timebase MPEG-TS pair with known priming — plausibly also affected by Gap 3's units bug — flagged above as a re-verification item once Gap 3 is fixed, not asserted as a confirmed additional false-positive-pinning instance (the pair's fail is independently explained by a genuine audio-duration bookkeeping mismatch, so the exact magnitude, not the verdict, is what's in question).

### Required Artifacts (spot-checked)

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/analyzers/timeline/monotonic.cpp` | dts_monotonic / pts_unique / wrap_events, unwrap-aware | ✓ VERIFIED | Calls `detail::unwrap_axis_view`; AV_NOPTS_VALUE excluded via `build_axis_view` |
| `src/analyzers/timeline/discontinuities.cpp` | gaps / discontinuities, unwrap-aware | ✓ VERIFIED | Calls `detail::unwrap_axis_view` |
| `src/analyzers/timeline/start_duration.cpp` | timeline.start / duration(.coherence) | ⚠️ HOLLOW on TS wrap | No unwrap call; corrupted on genuinely-wrapping TS (Gap 2) |
| `src/analyzers/timeline/jitter_vfr.cpp` | jitter / vfr_profile, shared `derive_cadence` | ⚠️ HOLLOW on TS wrap | Shares cadence primitive correctly (SC3 verified) but no unwrap call (WINDOWS #27, folded into Gap 2) |
| `src/analyzers/timeline/av_sync.cpp` | av_offset / av_drift(.pattern), priming-adjusted | ✗ Computation bug | Priming-sample-to-tick units bug (Gap 3); no unwrap call (Gap 2); `step` pattern unreachable (Gap 1) |
| `src/analyzers/timeline/timecode.cpp` | timeline.timecode(.value) | ✓ VERIFIED | Drop-frame/non-drop-frame rendered correctly |
| `src/analyzers/timeline/unwrap.cpp` | shared 33-bit unwrap primitive | ✓ VERIFIED | Correct where consumed (monotonic.cpp, discontinuities.cpp); not consumed by the other four files above |
| `tests/golden/PERF_BASELINE.txt` | committed instruction-count ratchet | ✓ VERIFIED | Matches CI run 35347845434 exactly |
| `scripts/measure_timeline_perf.sh` | ratchet check script | ✓ VERIFIED | Wired into designated leg only, per `.github/workflows/*.yml` |
| `.planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md` | 16-id approved roster | ✓ VERIFIED | Matches registered check IDs observed in `--json` output |

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|---|---|---|---|
| TIME-01 | 05-01, 05-02, 05-04, 05-05 | ✓ SATISFIED | int64/AVRational math, AV_NOPTS_VALUE handling confirmed |
| TIME-02 | 05-02, 05-06, 05-07 | ✗ BLOCKED | Wraparound not unwrapped for timeline.start/duration/av_offset/av_drift (Gap 2) |
| TIME-03 | 05-01, 05-04 | ✗ BLOCKED | timeline.start / duration corrupted on wrap (Gap 2); otherwise correct |
| TIME-04 | 05-05, 05-06, 05-07 | ✓ SATISFIED | dts_monotonic/pts_unique/gaps/discontinuities all correctly unwrap-aware |
| TIME-05 | 05-03, 05-08 | ✓ SATISFIED | Jitter/vfr_profile, CFR/VFR classification, shared `derive_cadence` confirmed |
| TIME-06 | 05-09 | ✗ BLOCKED | Priming-adjusted offset computation bug (Gap 3) |
| TIME-07 | 05-10 | ✗ BLOCKED | `step` pattern unreachable (Gap 1) |
| TIME-08 | 05-10 | ✓ SATISFIED | K=32 trajectory snapshot round-trip verified (ctest #877) |
| TIME-09 | 05-09 | ✗ BLOCKED | Same units bug corrupts the priming-known/adjusted path (Gap 3) |
| TIME-10 | 05-09 | ⚠️ Partially satisfied | Non-zero-priming path IS fixture-covered (TIME-10's literal ask), but the one MP4-native-timebase fixture used for it masks Gap 3's bug rather than proving correctness |
| TIME-11 | 05-11 | ✓ SATISFIED | Presence, SMPTE value, drop-frame flag all confirmed |
| DOC-04 | all plans | ⚠️ Partially satisfied | No-others discipline correctly implemented and enforced on every pair EXCEPT the wrap pair, which is deliberately excluded from whole-report assertion because of Gap 2 |
| PERF-01 | 05-12 | ✓ SATISFIED | ≤3s budget met (recorded, not asserted) |
| PERF-03 | 05-12 | ✓ SATISFIED | Amended text accurately reflects measured overhead; ratchet supersedes the absolute ratio per D-13/D-14 |
| PERF-05 | 05-12, 05-13 | ✓ SATISFIED | CI-measured, committed baseline, designated-leg scoped |

No orphaned requirements: every ID in the phase's requirement list is claimed by at least one plan's frontmatter.

### Anti-Patterns Found

None. No `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` markers in any Phase 5 source file (`src/analyzers/timeline/*.cpp`, `src/probe/demux_session.cpp`, `src/probe/packet_scan.cpp`).

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| linear-drift classification | `compare --profile sw-encoder timeline_drift_base.mp4 timeline_drift_linear.mp4` | `av_drift.pattern` = `linear-drift` | ✓ PASS |
| constant-offset classification | `compare --profile sw-encoder timeline_start_base.mp4 timeline_avoffset_video_shift.mp4` | `av_drift.pattern` = `constant-offset` | ✓ PASS |
| step classification (SC1) | `compare --profile sw-encoder timeline_start_base.mp4 timeline_drift_step.mp4` | `av_drift.pattern` = `irregular`, expected `step` | ✗ FAIL (Gap 1) |
| TS-wrap unwrap (SC2) | `compare --profile remux timeline_ts_nowrap.ts timeline_ts_wrap.ts` | timeline.start/duration/av_offset/av_drift corrupted | ✗ FAIL (Gap 2) |
| VFR/CFR jitter classification (SC3) | `compare --profile sw-encoder timeline_start_base.mp4 timeline_vfr.mp4` | video `skipped:vfr`, audio jitter=0 pass | ✓ PASS |
| priming-unknown av_offset (SC4a) | `compare --profile remux timeline_avoffset_unknown.ts timeline_avoffset_video_shift.mp4` | baseline priming.state=unknown, basis=raw | ✓ PASS |
| priming-adjusted av_offset correctness (SC4b) | `compare --profile remux timeline_ntsc_base.mp4 timeline_ntsc_remux.mkv` | identical raw offsets, adjusted offsets diverge by exactly ~1024ms (samples misread as ms) | ✗ FAIL (Gap 3) |
| drop-frame timecode (SC5) | `compare --profile sw-encoder timeline_tc_df.mp4 timeline_tc_ndf.mp4` | `00:00:10;00` vs `00:00:10:00` | ✓ PASS |
| TIME-08 trajectory snapshot fidelity | `ctest --preset x64-linux -R "^integration\.timeline_av_sync - TIME-08"` | 1/1 passed | ✓ PASS |
| Mid-file wrap self-comparison (doc 04 §5) | `ctest --preset x64-linux -R "^integration\.timeline_structure - a genuine mid-file"` | 1/1 passed | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` files exist in this repository. Step 7c: SKIPPED (no runnable probes declared or conventionally located).

### Gaps Summary

Three blockers keep the phase goal — "every `timeline.*` check and the A/V drift algorithm ... computed on integer/rational math with false positives designed out" — from being achieved:

1. **SC1 / TIME-07**: `timeline.av_drift.pattern`'s `step` classification is architecturally unreachable, not merely untested — 05-10-SUMMARY.md's own algebraic proof and this verification's reproduction agree.
2. **SC2 / TIME-02 / TIME-03**: MPEG-TS 33-bit wraparound corrupts `timeline.start`, the duration triple, `timeline.av_offset`, `timeline.av_drift`, plus collateral `video.frame_rate.measured` and `size.stream_bitrate` — a known, tracked-but-unresolved defect (WINDOWS.md #26/#27/#30) that this verification confirms also extends to `av_sync.cpp`, previously undocumented.
3. **SC4 / TIME-06 / TIME-09**: `timeline.av_offset`'s priming adjustment silently misconverts sample counts to milliseconds whenever a container's audio timebase differs from its sample rate, producing a false A/V-sync regression on a lossless MKV stream-copy remux — undetected because every priming-known fixture in the suite happens to use an MP4 timebase that masks the bug.

Known Defect #1 (dts_monotonic tie on MP4->TS) was evaluated and judged a true positive, not a gap — see "Findings Evaluated, Not Treated As Gaps" above. *(Superseded by the orchestrator correction: re-filed as a gap, see the Orchestrator Addendum.)*


### Orchestrator Addendum (2026-09-18)

Three gaps were added to the frontmatter after this report was written, each attributed in its own `reason`:

4. **dts_monotonic, MPEG-TS read-back inference (Goal / SC2 / TIME-04).** Re-verification of Known Defect #1 at the PES level shows the counted tie does not exist in the file. The fix direction is container truth from `ts_scan`; doc 04's `<=` rule stays.
5. **jitter / vfr_profile on NTSC MP4 -> MKV (Goal / D-06 / TIME-05, WINDOWS.md #28).** The user decided to fix these as false positives, against this report's classification, by making binning and sigma quantization-aware.
6. **Memory safety, CR-01 / WR-01 (05-REVIEW.md).** An out-of-bounds read in `sorted_pts_with_span` on single-packet streams.

User decisions recorded the same day: `step` is to be made reachable through a research-first plan with a fallback checkpoint (Gap 1), and #28 is to be fixed (Gap 5). Gaps 1-3 also carry orchestrator notes in their `missing` lists. The status stays `gaps_found`, and the score (2/5, literal success criteria) is unchanged, because gaps 4-6 are goal-level defects rather than success-criterion rows.
---

*Verified: 2026-09-18*
*Verifier: Claude (gsd-verifier)*
