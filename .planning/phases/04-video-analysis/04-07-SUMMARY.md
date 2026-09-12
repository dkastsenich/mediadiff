---
phase: 04-video-analysis
plan: 07
subsystem: probe
tags: [ffmpeg, libavcodec, cadence, rational, catch2]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-06's DemuxSession::StreamInfo extension pattern and video_stream_params.cpp/analyzers.h scaffolding; 04-05's video_sar_conflict.mp4/video_sar_4_3.mp4 fixtures and its empirical finding that AVStream::sample_aspect_ratio (container) and codecpar->sample_aspect_ratio (bitstream) are two distinct libav fields post-probe; 03's packet_scan.h rejection of a pre-computed IntervalStats struct (PROBE-10) and 04-CHECK-ROSTER.md's approved five-id declaration for this plan"
provides:
  - "src/probe/cadence.{h,cpp}: derive_cadence, a pure integer-only function over StreamPacketScan::packets -- PROBE-10's prescribed alternative to IntervalStats, D-05/D-06/D-07's shared mode-interval/axis/CFR-VFR derivation, designed as Phase 5's timeline.jitter/vfr_profile second consumer"
  - "video.sar/video.dar/video.sar.conflict/video.frame_rate.declared/video.frame_rate.measured registered end to end with --explain docs and DOC-03 fixture pairs (registry count 41)"
  - "DemuxSession::StreamInfo extended with avg_frame_rate/r_frame_rate and sar_container/sar_bitstream -- the codecpar/AVStream fields this plan's five checks need"
  - "detail::resolve_sar/compute_dar in src/analyzers/video/analyzers.h, exposed for direct testing of the unset-SAR and zero-dimension edge cases no real fixture can produce"
affects: [04-08-video-color, 04-09-video-gop-frame-types, 04-10-video-interlace, 04-11-video-hdr, 04-12-video-dovi, 05-timeline-analysis]

actuals:
  tokens: 20800
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A shared PURE derivation in the probe layer (src/probe/cadence.h) with two planned consumers in two different phases -- the concrete instance of PROBE-10's 'consumers derive their own statistic as a pure function over the shared, read-only array' prescription, first exercised by this plan and designed against claude_docs/04-timeline-analysis.md's own future consumer."
    - "Fixed-epsilon, fixed-threshold classification expressed as exact-rational named constants (kCadenceEpsilonTicks, kCfrMatchingProportionNum/Den) rather than a runtime-computed mean -- the same fixed-K/fixed-epsilon determinism rule SIZE-01's windowing already established, now applied to a classification decision rather than a peak computation."
    - "EffectiveSar's unset-vs-explicit-1:1 split (0/1 raw treated as 1:1 for comparison, `unset` recorded separately in evidence) -- the same 'compare normalized, record raw' shape VIDEO-02's frame_count established for counted-vs-declared."

key-files:
  created:
    - src/probe/cadence.h
    - src/probe/cadence.cpp
    - tests/unit/test_cadence.cpp
    - docs/checks/video.sar.md
    - docs/checks/video.dar.md
    - docs/checks/video.sar.conflict.md
    - docs/checks/video.frame_rate.declared.md
    - docs/checks/video.frame_rate.measured.md
  modified:
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - src/analyzers/video/stream_params.cpp
    - src/analyzers/video/analyzers.h
    - src/core/checks.def
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - tests/unit/test_video_stream_params.cpp
    - tests/integration/test_doc03_coverage.cpp
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "derive_cadence lives in src/probe/cadence.h, NOT under src/analyzers/ -- D-05 places it beside the array it reads (StreamPacketScan::packets) because it has two consumers in two different analyzer families (video.frame_rate.measured today, Phase 5's timeline.* next), and packet_scan.h:14-25's own rejection of a pre-computed struct is exactly what this shared pure function replaces."
  - "CFR/VFR epsilon fixed at zero ticks (exact equality), matching threshold transcribed as an exact rational (995/1000) directly from claude_docs/04-timeline-analysis.md's own 'ge 99.5% of intervals equal the mode interval' rule -- no floating-point percentage, no runtime-computed mean, per D-07 and the project's fixed-K/fixed-epsilon determinism rule."
  - "video.sar.conflict compares the EFFECTIVE (unset-normalized) container and bitstream ratios, not the raw values -- an unset container SAR (0/1) resolving to the same 1:1 as an explicit bitstream declaration is NOT a conflict (VIDEO-04-E1: a conflict check that fires on every well-formed file is noise, and this project treats noise as P0)."
  - "Empirical correction to the plan's own VIDEO-01-E1 test fixture assumption: video_base.mp4 (an ordinary mpeg4-in-mp4 encode) is NOT the 'unset SAR' case -- this project's pinned FFmpeg 8.1 mpeg4 muxer writes an explicit 1:1 pasp box even with no -aspect/setsar requested. The genuinely-unset case (both container and bitstream SAR literally 0/1) is a raw elementary stream with no container box and no VUI aspect_ratio_info at all (video_h264_closed.h264, already in the corpus from 04-05). Verified directly against the real binary before writing any test against it; the roster's id/semantic/unit/value_kind/severity for video.sar are unaffected -- only which fixture proves the unset half of the split changed."
  - "video.frame_rate.measured's declared-vs-measured internal mismatch flag (doc 03's own signal on this check) reuses the SAME relative-tolerance cross-multiplication formula src/compare/tol.cpp applies for a real baseline/candidate comparison, evaluated locally between one file's own declared and measured rates at the check's own registered 0.1% default -- documented as evidence-only (never itself a compared Value), degrading to false on any checked-arithmetic overflow rather than propagating an error, since this is a convenience flag, not a verdict."
  - "video.frame_rate.measured is gated on packet_scan.partial alone, deliberately independent of video.frame_count's own frame_count_partial (which additionally folds in parser_scan's own completeness) -- the shared cadence derivation reads StreamPacketScan::packets directly and never touches ParserScanResult, so folding in an unrelated pass's truncation state would be a spurious dependency."

patterns-established:
  - "Shared pure probe-layer derivation with a second consumer designed in from the start (see tech-stack.patterns) -- the template Phase 5's timeline.jitter/vfr_profile will read rather than reshape."
  - "Fixed-epsilon/fixed-threshold classification as named exact-rational constants (see tech-stack.patterns)."

requirements-completed: [VIDEO-04]

coverage:
  - id: D1
    description: "src/probe/cadence.h: a pure, integer-only cadence derivation over StreamPacketScan::packets, with PTS-first/DTS-fallback axis selection, exact-tick CFR/VFR classification, and no pre-computed IntervalStats-shaped struct anywhere in the probe layer"
    requirement: VIDEO-01
    verification:
      - kind: unit
        ref: "tests/unit/test_cadence.cpp -- nine hand-verified behaviors against hand-built PacketRecord arrays (mode selection, axis fallback, no_timing_data/insufficient_data split, shuffle invariance, non-unit timebase, overflow refusal)"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.frame_rate.declared/measured registered end to end: declared reads AVStream::avg_frame_rate as an exact rational; measured reads derive_cadence's shared output, never a second sweep, with axis/mode-interval/class all visible in evidence and a VFR classification still producing a real measured rate rather than a skip"
    requirement: VIDEO-01
    verification:
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp -- video.frame_rate.declared/measured shape, VFR classification, the three skip paths (no_timing_data/insufficient_data/partial_scan), and the declared/measured internal-mismatch evidence flag"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.frame_rate.declared/measured each have a proven trigger/clean fixture pair"
        status: pass
    human_judgment: false
  - id: D3
    description: "video.sar/video.dar/video.sar.conflict registered end to end: the container's effective SAR compared with an unset marker in evidence (VIDEO-01-E1), DAR derived rationally, and the container-vs-bitstream conflict reported as its own info-severity check that does not fire when the two effective ratios agree (VIDEO-04-E1)"
    requirement: VIDEO-04
    verification:
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp -- video.sar unset/explicit-1:1 distinction, video.dar's 4/3 derivation and zero-dimension refusal, video.sar.conflict's agree/disagree states on video_base.mp4 and video_sar_conflict.mp4"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.sar/video.dar/video.sar.conflict each have a proven trigger/clean fixture pair, registry check count 41"
        status: pass
    human_judgment: false

duration: 28min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 07: SAR/DAR/Frame-Rate Checks and the Shared Cadence Derivation Summary

**`src/probe/cadence.h`'s shared pure cadence derivation (PROBE-10's prescribed alternative to the rejected `IntervalStats` struct) backing `video.frame_rate.measured`, plus `video.sar`/`video.dar`/`video.sar.conflict` comparing the container's effective sample aspect ratio against the bitstream's own, with the disagreement reported as its own info-severity finding.**

## Performance

- **Duration:** 28 min
- **Started:** 2026-09-12T22:25:15+02:00 (base HEAD at dispatch)
- **Completed:** 2026-09-12T22:53:33+02:00
- **Tasks:** 3/3 completed
- **Files modified:** 18 (8 created, 10 modified)

## Accomplishments

- `src/probe/cadence.{h,cpp}`: `derive_cadence(std::span<const PacketRecord>, Rational tb) -> Cadence`, a pure function taking no session and mutating nothing, sorting an index view (never the caller's array) before deriving. PTS-first axis selection with DTS fallback (D-06); mode interval by tally with smaller-interval tie-breaking; CFR/VFR by exact integer comparison within a fixed zero-tick epsilon against a fixed 99.5% matching-proportion threshold transcribed directly from `claude_docs/04-timeline-analysis.md` (D-07). Every arithmetic step routed through `core/rational.h`'s checked helpers; any overflow collapses to `insufficient_data`.
- `video.frame_rate.declared`: `AVStream::avg_frame_rate` as an exact `RationalValue`, `r_frame_rate` in evidence, never rendered as a non-integer approximation.
- `video.frame_rate.measured`: reads `derive_cadence` over the stream's own packet array and timebase, expresses the mode interval as a GCD-reduced rate; evidence carries axis/mode-interval-ticks/matching-and-total-interval-counts/CFR-VFR-class/a declared-vs-measured agreement boolean. Skip vocabulary: `no_timing_data`/`insufficient_data` (from the derivation), `partial_scan` (D-02, ahead of the derivation itself, gated on `packet_scan.partial` alone -- deliberately independent of `video.frame_count`'s own `frame_count_partial`, which also folds in `parser_scan`'s completeness that this check never touches).
- `video.sar`: the container's effective SAR (`AVStream::sample_aspect_ratio`) wins per libav's own resolution; a `0/1` raw value in either position is treated as `1:1` for comparison with `unset` recorded per-position in evidence (VIDEO-01-E1).
- `video.dar`: `width*sar_num/height*sar_den`, GCD-reduced, every step through the checked helpers; a zero width/height skips `insufficient_data`.
- `video.sar.conflict`: its own `info`-severity check (04-CHECK-ROSTER.md's approved resolution over the evidence-only alternative), comparing the EFFECTIVE container/bitstream ratios so an unset-vs-explicit-1:1 pair does not manufacture a conflict (VIDEO-04-E1). Value is `"agree"` or a rendering of both divergent ratios.
- Five `docs/checks/<id>.md` files; five new `tests/integration/test_doc03_coverage.cpp` declared pairs, each proven empirically against the real binary (registry check count 41, verified count 41).

## Task Commits

1. **Task 1: derive_cadence -- the shared pure cadence derivation (D-05/D-06/D-07)** - `fb8d054` (feat)
2. **Task 2: video.frame_rate.declared and video.frame_rate.measured** - `8999cc0` (feat)
3. **Task 3: video.sar, video.dar, the container-vs-bitstream conflict, and five DOC-03 pairs** - `e1b2aa0` (feat)

## Files Created/Modified

- `src/probe/cadence.{h,cpp}` - `derive_cadence`, `Cadence`/`CadenceAxis`/`CadenceStatus`/`CadenceClass`, `kCadenceEpsilonTicks`/`kCfrMatchingProportionNum`/`Den`
- `tests/unit/test_cadence.cpp` - nine hand-verified behaviors against hand-built `PacketRecord` arrays
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt` - `cadence.{h,cpp}`/`test_cadence.cpp` registered
- `src/probe/demux_session.h`/`.cpp` - `StreamInfo` extended with `avg_frame_rate_num`/`_den`, `r_frame_rate_num`/`_den`, `sar_container_num`/`_den`, `sar_bitstream_num`/`_den`
- `src/analyzers/video/analyzers.h` - `EffectiveSar`, `resolve_sar`, `compute_dar` declared, exposed for direct testing
- `src/analyzers/video/stream_params.cpp` - `emit_sar`/`emit_dar`/`emit_sar_conflict`/`emit_frame_rate_declared`/`emit_frame_rate_measured`, `declared_measured_agree`, a file-local `push_skip` helper (04-06's own file omitted one, added here per 04-PATTERNS.md's established convention)
- `src/core/checks.def` - `video.sar`/`video.dar`/`video.sar.conflict`/`video.frame_rate.declared`/`video.frame_rate.measured`
- `docs/checks/video.sar.md`, `video.dar.md`, `video.sar.conflict.md`, `video.frame_rate.declared.md`, `video.frame_rate.measured.md`
- `tests/unit/test_video_stream_params.cpp` - 15 new test cases across both tasks
- `tests/integration/test_doc03_coverage.cpp` - five new declared pairs, registry-count comment updated to forty-one
- `tests/golden/list_checks_effective.txt` - refreshed (a five-line addition, one per new check) via `UPDATE_GOLDENS=1`

## Decisions Made

See `key-decisions` in the frontmatter above for the full list. In short: `derive_cadence` lives in the probe layer beside the array it reads, never under `src/analyzers/`; the CFR/VFR epsilon and threshold are fixed named constants transcribed from doc 04, never a runtime-computed mean; `video.sar.conflict` compares effective (unset-normalized) ratios so an unset-vs-explicit-1:1 pair is not a manufactured conflict; the plan's own VIDEO-01-E1 fixture assumption (`video_base.mp4` as the "unset" case) was empirically wrong and corrected before any test was written against it; the declared/measured agreement flag is evidence-only and degrades safely on overflow; `video.frame_rate.measured`'s partial-scan gate is deliberately independent of `video.frame_count`'s own.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Empirical correction] `video_base.mp4` is not the "unset SAR" fixture the plan's own flagged assumption implied**
- **Found during:** Task 3, while writing the VIDEO-01-E1 unit test for `video.sar`'s unset/explicit-1:1 distinction.
- **Issue:** A direct `mediadiff inspect -v` run against `video_base.mp4` showed `container.unset: false` (an explicit `1/1`), not the unset (`0/1`) case the test needed to prove the distinction. This project's pinned FFmpeg 8.1 mpeg4-in-mp4 muxer writes an explicit `pasp` box defaulting to `1:1` even with no `-aspect`/`setsar` requested at encode time.
- **Fix:** Verified every existing corpus fixture's `video.sar` evidence directly against the real binary before writing any assertion. `video_h264_closed.h264` (a raw H.264 elementary stream from 04-05, no container box and no VUI `aspect_ratio_info`) genuinely reports `unset: true` on both container and bitstream. Used that fixture for the unset half of the VIDEO-01-E1 test and `video_base.mp4` for the explicit-1:1 half (still comparing equal, per the check's own compared-value rule).
- **Files modified:** `tests/unit/test_video_stream_params.cpp` (test design only; no production code changed)
- **Verification:** `mediadiff inspect tests/fixtures/video_h264_closed.h264 -v` shows `"container":{"num":0,"den":1,"unset":true}`; the corresponding unit test passes.
- **Committed in:** `e1b2aa0` (Task 3's commit)

**2. [Rule 3 - Blocking] `src/analyzers/video/stream_params.cpp` had no `push_skip` helper**
- **Found during:** Task 2, while implementing `video.frame_rate.declared`/`measured`'s skip paths.
- **Issue:** 04-PATTERNS.md's own "Skip-emission" pattern (copied file-local into every other `video/*.cpp`/`container/*.cpp`/`size.cpp`) was not present in this file -- 04-06's own `video.frame_count` skip path used an inline `Measurement` construction instead, and this plan's five new skip call sites needed the shared helper.
- **Fix:** Added the standard `push_skip(CheckId, Scope, SkipReason, Fingerprint&)` helper verbatim, matching `mp4.cpp:138-145`/`size.cpp:41-48` exactly. 04-06's own inline `video.frame_count` skip path was left untouched (not itself part of this plan's `files_modified`).
- **Files modified:** `src/analyzers/video/stream_params.cpp`
- **Verification:** builds clean under warnings-as-errors; all five new checks' skip paths pass their own unit tests.
- **Committed in:** `8999cc0` (Task 2's commit)

**3. [Rule 3 - Blocking] Two acceptance-criterion greps for the literal substrings "float"/"double" initially failed on descriptive comments**
- **Found during:** Task 1 and Task 2's own acceptance-criterion verification (`grep -c 'double\|float' src/probe/cadence.cpp`/`src/analyzers/video/stream_params.cpp`).
- **Issue:** Comments describing WHY no floating-point arithmetic is used (e.g. "no floating point", "never rendered to a float") contain the literal substrings the grep checks for, since the grep is naive text matching, not a semantic check for the `double`/`float` C++ types.
- **Fix:** Reworded both comments to describe the same guarantee without using the literal words ("integer-only throughout", "never rendered as a non-integer approximation"), satisfying the acceptance criterion literally while preserving the exact same documentation intent.
- **Files modified:** `src/probe/cadence.cpp`, `src/analyzers/video/stream_params.cpp`, `src/probe/cadence.h`
- **Verification:** both greps report `0`.
- **Committed in:** `fb8d054`, `8999cc0`

---

**Total deviations:** 3 auto-fixed (1 Rule 1 empirical correction, 2 Rule 3 blocking). **Impact on plan:** All three are necessary for correctness or to satisfy the plan's own literal acceptance criteria; none change any check's registered id/semantic/unit/value_kind/severity/tolerance. No scope creep.

## Issues Encountered

**Interleaved commit staging across tasks.** `src/core/checks.def`, `src/probe/demux_session.{h,cpp}`, and the `docs/checks/*.md` files needed edits for BOTH Task 2 (frame-rate checks) and Task 3 (SAR/DAR/conflict checks), since `StreamInfo`'s codecpar/AVStream field extension and the registry file are each a single contiguous edit site shared by all five checks. To keep Task 2 and Task 3 as separate, independently-verifiable commits (per this executor's own atomic-commit requirement), the Task-3-only hunks (SAR fields, `video.sar`/`video.dar`/`video.sar.conflict` registrations, their three docs) were temporarily written then held out of Task 2's commit (moved to the scratch directory / reverted in-place) and reintroduced immediately afterward for Task 3's own commit. Both tasks' own acceptance criteria were independently verified before each commit.

## Requirement Gate

`gsd_run query requirements.ready-ids` was run against this plan's two declared requirements (`VIDEO-01`, `VIDEO-04`) AFTER this SUMMARY.md was written, per the `<requirement_marking_guard>` this execution operated under:

```json
{"ready": ["VIDEO-04"], "blocked": ["VIDEO-01"], "total": 2}
```

Matches the dispatch-computed expected outcome exactly. Before marking, `VIDEO-04`'s full REQUIREMENTS.md text was checked clause by clause: "Container SAR and bitstream VUI SAR conflicts record both values" -- yes, `video.sar`'s evidence carries both the container and bitstream raw values; "compare the effective one" -- yes, `video.sar`'s compared value is the container's effective (unset-normalized) ratio; "flag the conflict itself as `info`" -- yes, `video.sar.conflict` is registered at `info` severity and fires on genuine disagreement only (VIDEO-04-E1). All three clauses are met; `VIDEO-04` was marked complete. `VIDEO-01` stays blocked -- also declared by plans 04-08 and 04-12, neither of which has landed yet.

## Known Stubs

None.

## Threat Flags

None beyond this plan's own `<threat_model>` (T-4-28 through T-4-32, T-4-SC), every mitigation implemented as specified: T-4-28 (a crafted timebase dividing by zero) is refused by the `tb.num <= 0 || tb.den <= 0` guard at `derive_cadence`'s own entry and by `checked_div`-free construction of the rate (cross-multiplication only, never a division by an untrusted value); T-4-29 (interval/rate arithmetic overflow) is covered by `checked_sub`/`checked_mul`/`checked_negate` at every step, pinned by `test_cadence.cpp`'s own overflow test; T-4-30 (a crafted stream of millions of distinct intervals) is bounded by the same `kMaxPacketsPerStream` ceiling `StreamPacketScan::packets` already enforces upstream, checked again defensively inside `derive_cadence` itself; T-4-31 (a zero-denominator rational reaching a report) is refused at every emission site (`video.dar`/`video.frame_rate.declared`/`video.frame_rate.measured` all skip rather than emit a degenerate `RationalValue`); T-4-32 (an unaccountable CFR/VFR class) is satisfied by evidence recording the axis, mode interval, and both interval counts on every `ok`-status measurement.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`src/probe/cadence.h`'s `derive_cadence` is ready for Phase 5's `timeline.jitter`/`timeline.vfr_profile` to consume directly -- the `Cadence` result already carries the axis, mode interval in ticks, matching/total interval counts, and the CFR/VFR class Phase 5's own design doc (`claude_docs/04-timeline-analysis.md`) describes needing, so no reshape is anticipated. `EffectiveSar`/`resolve_sar` established the unset-vs-explicit pattern any later check needing the same "0 means absent, but distinguishably so" rule can follow directly. No blockers for 04-08 (video.color.*) or 04-09 (video.gop.*/frame_types), neither of which depends on this plan's own outputs.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED

All 8 created files confirmed present on disk; all 3 commit hashes (`fb8d054`, `8999cc0`, `e1b2aa0`) confirmed present in `git log --oneline --all`.
