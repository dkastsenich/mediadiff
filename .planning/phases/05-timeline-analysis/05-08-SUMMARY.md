---
phase: 05-timeline-analysis
plan: 08
subsystem: timeline-analysis
tags: [jitter, vfr, cadence, fixed-point, isqrt, grid-relative-histogram, doc03, doc04, corpus-digest]

requires:
  - phase: 05-timeline-analysis
    provides: "05-01's derive_cadence (PROBE-10, Cadence::status/axis/klass/mode_interval_ticks/matching_intervals/total_intervals/span_ticks/interval_count/ideal_interval_num/ideal_interval_den/conforming_timestamps/considered_timestamps, D-05 99.5% CFR/VFR threshold), 05-05/05-06's push_skip/scope_kind_for_stream/compute_stream_scopes per-file-copy pattern, video_vfr.mp4's proven LGPL-clean select-filter VFR-thinning recipe"
provides:
  - "timeline.jitter: rational sigma (fixed-point, kJitterSigmaFixedShift=16 scale) and max absolute deviation of the interval distribution on CFR streams, skipped:vfr on VFR streams (ROADMAP SC3)"
  - "timeline.vfr_profile: a six-bucket (on_grid/one_tick/one_percent/two_x/three_x/longer) interval histogram keyed on deviation from the stream's OWN ideal_interval_num/den (D-06 grid-relative design, container-comparable), runs on every stream regardless of CFR/VFR classification"
  - "isqrt/isqrt_i64 in src/core/rational.h: portable bit-doubling/restoring integer square root, floor-exact and byte-identical across MSVC/GCC/Clang/AppleClang; kJitterSigmaFixedShift=16 fixed-point scale constant"
  - "timeline_jitter.mp4 / timeline_vfr.mp4 fixtures, DOC-03 declared_pairs() rows and DOC-04 declared-set coverage for both new ids, running total seventy -> seventy-two"
affects: [timeline-analysis, doc03-coverage, doc04-no-others, core-rational]

actuals:
  tokens: 68000
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "Portable integer square root (isqrt/isqrt_i64, core/rational.h): bit-doubling/restoring algorithm operating purely on unsigned integer types, floor-exact for all non-negative inputs, chosen specifically to avoid std::sqrt-on-double's cross-compiler ULP variance in a determinism-class comparison path (PROJECT.md's 'byte-identical --json' constraint)."
    - "Fixed-point sigma scale (kJitterSigmaFixedShift=16): deviations are scaled by 1<<16 BEFORE squaring so scale^2=2^32 fits int64_t without checked_mul, then accumulated via Int128Accum::add_product/try_narrow, divided by count via checked_div, and rooted via isqrt_i64 -- no floating point anywhere in the comparison path."
    - "Shared single derive_cadence() call per stream, consumed by both timeline.jitter (skips on VFR) and timeline.vfr_profile (always runs) -- the two checks are one analyzer registration (timeline_jitter_vfr_analyzer), proving ROADMAP SC3's two-outcome split from one probe read rather than two independent walks."
    - "D-06 grid-relative histogram bucketing: classify_vfr_bin() keys every bucket boundary off Cadence::ideal_interval_num/ideal_interval_den (the stream's own exact-rational span/count ideal), never off raw ticks or the CFR mode interval -- this is what makes the histogram comparable across containers with different timebases, verified to hold for exactly-representable frame rates and documented as NOT holding for non-exactly-representable ones (NTSC) since that is a real precision limit, not a bucket-design defect."
    - "setts bitstream filter PTS-perturbation recipe (timeline_jitter.mp4): the bsf's arithmetic literals operate in the VIDEO CODEC's own time_base, not the muxed container's tbn -- a `+1` literal is one WHOLE FRAME, and perturbing an interior frame beyond the file's own original PTS range corrupts derive_cadence's global span/count ideal for the WHOLE file, not just the perturbed interval. Fixed by shifting exactly one frame's PTS by +1 frame-tick, staying inside the original [0, max] range so ideal_interval never shifts."

key-files:
  created:
    - src/analyzers/timeline/jitter_vfr.cpp
    - docs/checks/timeline.jitter.md
    - docs/checks/timeline.vfr_profile.md
    - tests/unit/test_jitter_vfr.cpp
    - tests/integration/test_timeline_jitter.cpp
    - tests/fixtures/timeline_jitter.mp4
    - tests/fixtures/timeline_vfr.mp4
  modified:
    - src/core/rational.h
    - src/analyzers/timeline/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/unit/test_rational_wide.cpp
    - tests/unit/CMakeLists.txt
    - tests/golden/list_checks_effective.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/integration/test_timeline_structure.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - .planning/WINDOWS.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "[Rule 1] classify_vfr_bin's one-percent threshold had a spurious extra *100 in the cross-multiplication (pct_lhs = abs_diff*100*kVfrOnePercentDen instead of abs_diff*kVfrOnePercentDen), making the boundary 100x too tight. Caught by hand-rederiving boundary values before writing tests; fixed before any test ran against it, confirmed correct once all 16 new unit tests matched pre-computed values exactly."
  - "[Rule 3] jitter_vfr.cpp used std::map/std::gcd without including <map>/<numeric> -- added both, a straightforward blocking compile fix."
  - "timeline_jitter.mp4 built at 8s/200 frames (not the plan's original shorter duration) after discovering the setts codec-timebase behavior: a +1 whole-frame PTS shift at frame N=100 stays inside the file's original PTS range (never becoming a new span extremum), keeping ideal_interval exactly 512 ticks and landing exactly at the 99.5% CFR/VFR boundary (199/200 conforming) while still producing a real, threshold-crossing sigma (~4.01ms) and a genuine incidental timeline.pts_unique duplicate, both declared in the DOC-04 test."
  - "[Documented, not code-fixed] The plan's Task 2 acceptance criterion claiming timeline_ntsc_base.mp4 vs timeline_ntsc_remux.mkv --profile remux shows timeline.vfr_profile at pass does not hold empirically: NTSC's 1001/30000s period has no exact millisecond representation, so MP4's native timebase lands on_grid (integer ideal) while Matroska's 1ms timebase can only reach one_tick (non-integer ideal) -- a real per-container precision difference, not a bucket-design defect. Per 'FALSE POSITIVES ARE P0' and 'never narrow the fixture/report to force a pass', resolved by asserting the true, honest outcome in the test and documenting the limitation in docs/checks/timeline.vfr_profile.md and two WINDOWS.md ledger entries (#26 gap inheritance, #28 the deviation itself) rather than altering bucket logic or fixture recipes."

patterns-established:
  - "Two-check, one-derive_cadence-call analyzer: when two related checks share the same expensive probe-layer computation and differ only in how they interpret its output (skip-vs-always-run, sigma-vs-histogram), register them as a single AnalyzerSpec that computes the shared primitive once and emits both checks from it, rather than two analyzers each re-deriving cadence independently."

requirements-completed: [TIME-05, DOC-04]

coverage:
  - id: D1
    description: "isqrt/isqrt_i64: portable, floor-exact, cross-compiler-identical integer square root in src/core/rational.h, extending Int128Accum with try_isqrt; kJitterSigmaFixedShift=16 fixed-point scale constant"
    requirement: "TIME-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_rational_wide.cpp (perfect squares, non-perfect-square floor cases, zero, large 128-bit-accumulated values, try_isqrt on Int128Accum)"
        status: pass
    human_judgment: false
  - id: D2
    description: "timeline.jitter and timeline.vfr_profile registered in checks.def per 05-CHECK-ROSTER.md verbatim IDs (group=timeline, semantic=tol/dist, unit=ms/percent, value_kind=rational/histogram); timeline_jitter_vfr_analyzer() calls derive_cadence exactly once per stream, feeding both checks"
    requirement: "TIME-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_jitter_vfr.cpp (13 TEST_CASEs: classify_vfr_bin boundaries all six buckets + overflow, compute_jitter_sigma uniform/hand-computed/empty/overflow, compute_sorted_axis_intervals sort/axis-selection/AV_NOPTS_VALUE exclusion)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_jitter.cpp Test 4 (ROADMAP SC3): same file, video scope skipped:vfr on timeline.jitter, audio scope real non-skipped sigma, both scopes real populated timeline.vfr_profile histograms"
        status: pass
    human_judgment: false
  - id: D3
    description: "timeline.vfr_profile bins keyed on deviation from Cadence::ideal_interval_num/den (D-06), never raw ticks -- verified against real mediadiff compare --json evidence before asserting any test"
    requirement: "TIME-05"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_jitter.cpp Test 5 (NTSC remux honest-outcome assertion, documenting the real per-container precision limit rather than a false pass)"
        status: pass
    human_judgment: false
  - id: D4
    description: "timeline_jitter.mp4 (CFR with real jitter at the 99.5% boundary) and timeline_vfr.mp4 (genuinely VFR, LGPL-clean select-filter thinning, never mpdecimate) generated via .ffmpeg-pinned, two new CORPUS_DIGEST.txt lines added with zero pre-existing lines touched"
    requirement: "DOC-04"
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (all 4 clauses pass)"
        status: pass
    human_judgment: false
  - id: D5
    description: "DOC-03 declared_pairs() rows for both new ids and DOC-04 declared-set assertions (including 4 pre-existing-fixture true-positive additions caused by the two new checks now running on every stream) matching the real binary's output exactly, running total seventy -> seventy-two"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "ctest -R integration\\.doc03_coverage; ctest -R integration\\.timeline_jitter"
        status: pass
    human_judgment: false
  - id: D6
    description: "Full test suite green after all three tasks"
    verification:
      - kind: integration
        ref: "ctest --preset x64-linux (873/873 passing, 846 baseline + 11 Task-1 + 16 Task-2 unit tests already counted in Task-1's total revision -- net +27 across the plan)"
        status: pass
    human_judgment: false

duration: ~2h across two session segments
completed: 2026-09-17
status: complete
---

# Phase 5 Plan 8: timeline.jitter and timeline.vfr_profile Summary

**CFR interval jitter (fixed-point sigma/max-deviation, no floating point) and a container-comparable, grid-relative VFR interval histogram, both derived from a single shared `derive_cadence()` call per stream.**

## Performance

- **Duration:** ~2h across two session segments (context-compaction pause between Task 2 and Task 3)
- **Tasks:** 3
- **Files modified:** 24 (7 created, 17 modified)

## Accomplishments

- `isqrt`/`isqrt_i64` in `src/core/rational.h`: a portable, floor-exact, cross-compiler-byte-identical integer square root (bit-doubling/restoring algorithm), plus `Int128Accum::try_isqrt` and `kJitterSigmaFixedShift=16` — no `std::sqrt`-on-double anywhere in the comparison path.
- `timeline_jitter_vfr_analyzer()` (`src/analyzers/timeline/jitter_vfr.cpp`): a single `AnalyzerSpec` calling `derive_cadence` exactly once per stream, feeding both `timeline.jitter` (fixed-point sigma + max absolute deviation, `skipped:vfr` on VFR streams) and `timeline.vfr_profile` (six-bucket `on_grid`/`one_tick`/`one_percent`/`two_x`/`three_x`/`longer` histogram, always runs, D-06 grid-relative bucketing keyed on the stream's own `ideal_interval_num/den`).
- Both check IDs registered in `checks.def` per `05-CHECK-ROSTER.md` verbatim spelling, documented in `docs/checks/timeline.jitter.md`/`timeline.vfr_profile.md`, wired into the orchestrator and build.
- `timeline_jitter.mp4` (CFR sitting exactly at the D-05 99.5% conformance boundary, real ~4.01ms sigma) and `timeline_vfr.mp4` (genuinely VFR via the proven LGPL-clean `select`-filter thinning recipe, proving ROADMAP SC3's video-skips/audio-doesn't split on one file) — both generated with the pinned FFmpeg, never `mpdecimate`.
- DOC-03/DOC-04 coverage for both new IDs, running total seventy → seventy-two. Full suite: 873/873 passing.
- **Deviation loudly flagged, not hidden:** the plan's own NTSC-remux acceptance criterion (`timeline.vfr_profile` "pass" across MP4→MKV) does not hold empirically — a genuine per-container floating-point-precision limit (NTSC's 1001/30000s period has no exact ms representation), not an implementation defect. Documented honestly rather than hacked around; see Deviations below.

## Task Commits

Each task was committed atomically:

1. **Task 1: portable isqrt / fixed jitter sigma scale** — `4b43bb7` (test, RED), `3759323` (feat, GREEN)
2. **Task 2: timeline_jitter_vfr_analyzer implementation + registration** — `8166771` (test, RED), `fd4ab44` (feat, GREEN)
3. **Task 3: fixtures + DOC-03/DOC-04 coverage** — `5c42ab2` (test)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/core/rational.h` — `isqrt`/`isqrt_i64`, `Int128Accum::try_isqrt`, `kJitterSigmaFixedShift`
- `src/analyzers/timeline/jitter_vfr.cpp` (new) — both analyzers, shared `derive_cadence` consumption, `detail::` pure functions (`compute_sorted_axis_intervals`, `classify_vfr_bin`, `compute_jitter_sigma`)
- `src/analyzers/timeline/analyzers.h` — declarations for the new analyzer factory + `detail::` functions
- `src/probe/orchestrator.cpp` — registration
- `src/core/checks.def` — `timeline.jitter` / `timeline.vfr_profile` check definitions
- `docs/checks/timeline.jitter.md` / `docs/checks/timeline.vfr_profile.md` (new) — user-facing docs, the latter carrying an honest NTSC-precision caveat
- `CMakeLists.txt` — new source file
- `tests/unit/test_jitter_vfr.cpp` (new), `tests/unit/CMakeLists.txt` — 13 TEST_CASEs for the pure functions
- `tests/unit/test_rational_wide.cpp` — isqrt/try_isqrt test extensions
- `scripts/gen_corpus.sh` — `timeline_jitter.mp4`/`timeline_vfr.mp4` recipes with extensive setts-timebase-discovery comments
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` — 2 new fixture hash lines, zero pre-existing lines touched
- `tests/golden/list_checks_effective.txt` — 2 new rows (regenerated via `UPDATE_GOLDENS=1`, the safe non-designated-leg golden)
- `tests/integration/test_timeline_jitter.cpp` (new), `tests/integration/CMakeLists.txt` — 5 TEST_CASEs including ROADMAP SC3 and the honest NTSC-remux assertion
- `tests/integration/test_timeline_structure.cpp` / `test_timeline_start_duration.cpp` — 4 true-positive `expect_declared_set` additions caused by the two new checks now running on every stream
- `tests/integration/test_doc03_coverage.cpp` — 2 new `declared_pairs()` rows, running-total comment "seventy" → "seventy-two"
- `.planning/WINDOWS.md` — #27 (WINDOWS.md #26 TS-unwrap gap inheritance), #28 (NTSC-remux deviation)
- `.planning/REQUIREMENTS.md` — `TIME-05` marked complete

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- `classify_vfr_bin`'s one-percent threshold arithmetic bug (Rule 1) caught and fixed via hand-rederivation before any test ran.
- `timeline_jitter.mp4`'s recipe redesigned around the discovered `setts`-bsf codec-time_base semantics, landing the perturbation exactly at the D-05 99.5% CFR/VFR boundary while staying inside the file's original PTS range.
- The NTSC-remux acceptance-criterion mismatch documented honestly (docs + tests + WINDOWS.md) rather than papered over — the check's D-06 design is correct; the plan's illustrative claim was an untested assumption for a non-exactly-representable frame rate.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed `classify_vfr_bin`'s one-percent boundary arithmetic**
- **Found during:** Task 2, before writing any unit test (caught by hand-rederiving expected boundary values first)
- **Issue:** `pct_lhs = abs_diff * 100 * kVfrOnePercentDen` had a spurious extra `*100`, making the one-percent bucket boundary 100x too tight (effectively a 0.01% threshold).
- **Fix:** `pct_lhs = abs_diff * kVfrOnePercentDen`, the correct direct cross-multiplication of `abs_diff/ideal_num <= kVfrOnePercentNum/kVfrOnePercentDen`.
- **Files modified:** `src/analyzers/timeline/jitter_vfr.cpp`
- **Verification:** All 16 new unit tests (13 in `test_jitter_vfr.cpp`) matched hand-pre-computed boundary values exactly.
- **Committed in:** `fd4ab44` (Task 2 GREEN commit)

**2. [Rule 3 - Blocking] Added missing `<map>`/`<numeric>` includes**
- **Found during:** Task 2 (compile failure)
- **Issue:** `jitter_vfr.cpp` used `std::map`/`std::gcd` without the corresponding includes.
- **Fix:** Added both headers.
- **Files modified:** `src/analyzers/timeline/jitter_vfr.cpp`
- **Committed in:** `fd4ab44` (Task 2 GREEN commit)

**3. [Rule 1 - True positives, not bugs] Four `expect_declared_set` additions on pre-existing fixtures**
- **Found during:** Task 3, after registering both new checks
- **Issue:** `test_timeline_structure.cpp` (4 pairs) and `test_timeline_start_duration.cpp` (1 pair) regressed against DOC-04's whole-report assertion because `timeline.vfr_profile` now genuinely fires on every stream, including these fixtures' own pre-existing perturbations that alter interval distributions.
- **Fix:** Inspected real `mediadiff compare --json` output for each pair, confirmed each new finding was causally explained by the fixture's own known perturbation, added minimal declared-set entries with causal-reasoning comments — never suppressed or filtered.
- **Files modified:** `tests/integration/test_timeline_structure.cpp`, `tests/integration/test_timeline_start_duration.cpp`
- **Committed in:** `5c42ab2` (Task 3 commit)

### Deviations Requiring Documentation (not auto-fixed, no code change)

**4. Plan's Task 2 acceptance criterion #3 does not hold empirically — flagged loudly, not hidden (FALSE POSITIVES ARE P0)**
- **Found during:** Task 3, while writing the NTSC-remux test assertion
- **Claim:** "`mediadiff compare timeline_ntsc_base.mp4 timeline_ntsc_remux.mkv --profile remux --json` shows `timeline.vfr_profile` at `pass` — identical bins across two timebases."
- **Reality:** NTSC's `1001/30000`s period (~33.3667ms) has no exact millisecond representation. MP4's native `1/30000` timebase gives an exact-integer `ideal_interval_num/den` (119119/119), so every interval lands `on_grid`. Matroska's mandated 1ms timebase gives a non-integer ideal (3971/119 ≈ 33.3697), so every interval instead lands `one_tick`. Real output: `status=warn` on both video/audio scopes, not `pass`.
- **Root cause analysis:** This is a genuine, mathematically unavoidable difference in what each container's own tick resolution can represent — not a defect in the check's D-06 design (deviation from the stream's OWN `ideal_interval_num/den`, exactly as `05-CHECK-ROSTER.md` specifies) and not fixable by any alternative bucket-boundary choice (no single design can make both "identical bins for exactly-representable content" AND "identical bins for NTSC-at-1ms" simultaneously true).
- **Resolution:** Per explicit project instruction ("never narrow the fixture/report to force a pass" and "FALSE POSITIVES ARE P0"), documented honestly rather than hacked: `tests/integration/test_timeline_jitter.cpp` Test 5 asserts the real `warn` outcome and the real bucket distribution (`baseline_on_grid == baseline_total`, `candidate_one_tick == candidate_total`); `docs/checks/timeline.vfr_profile.md` gained an Accept-section caveat and a Tune-section note about widening tolerance for pipelines that routinely remux non-exactly-representable frame rates; `.planning/WINDOWS.md` #28 records the full deviation for cross-phase visibility.
- **Impact:** No code change. This is a corrected understanding of the check's true behavior, captured where a future reader (or the ship-gate ledger) will see it.

**5. `timeline.jitter`/`timeline.vfr_profile` inherit WINDOWS.md #26's open TS-unwrap gap**
- **Found during:** Task 2 implementation review
- **Issue:** `timeline_jitter_vfr_analyzer()` calls `derive_cadence()` on the raw, un-unwrapped packet array on every container including MPEG-TS — the same pre-existing pattern WINDOWS.md #26 already documents for `start_duration.cpp`/`stream_params.cpp`/`size.cpp`. On a genuinely-wrapping TS file this would produce corrupted evidence.
- **Resolution:** Explicitly out of this plan's declared scope, matching #26's own precedent (a follow-up plan extending the shared unwrap to all consumers closes this too). No fixture in this plan's corpus exercises a genuinely-wrapping TS file through this analyzer, so it is undetected by the current suite — documented as WINDOWS.md #27 rather than silently left implicit.

---

**Total deviations:** 3 auto-fixed (1 Rule 1 bug, 1 Rule 3 blocking, 1 Rule 1 true-positive batch) + 2 documented-not-code-fixed (1 acceptance-criterion correction, 1 known-gap inheritance)
**Impact on plan:** All auto-fixes were required for correctness; the documented deviations reflect an untested planning-time assumption (NTSC) and an already-accepted pre-existing gap (TS-unwrap), neither requiring or receiving a design change in this plan.

## Issues Encountered

- The `setts` bitstream filter's PTS-perturbation literals were initially misunderstood to operate in the container's muxed timebase; two scratchpad experiments (`/tmp/.../scratchpad/test_jitter*.mp4`) were needed to correctly characterize the codec-time_base behavior and the "perturbation must stay inside the original PTS range" constraint before the final fixture recipe was written. No impact on shipped code — the discovery only affected the fixture-generation script.
- `test_timeline_jitter.cpp` Test 4 initially threw `json::out_of_range` on `m.at("status")` for real (non-skipped) measurements, which carry no `status`/`skip_reason` keys at all. Fixed via `m.contains("status")` guard before this file's own commit.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `timeline.jitter`/`timeline.vfr_profile` fully implemented, tested, and documented; DOC-03/DOC-04 coverage green at seventy-two checks; `TIME-05` and `DOC-04` marked complete in `REQUIREMENTS.md`.
- `isqrt_i64`/`kJitterSigmaFixedShift` are general-purpose `core/rational.h` additions any future check needing a deterministic, cross-compiler-identical integer square root can reuse directly.
- Two open items tracked for future plans: WINDOWS.md #26/#27 (shared TS-unwrap fix, not yet extended to `jitter_vfr.cpp`), WINDOWS.md #28 (NTSC-remux precision caveat, informational only — no fix needed, just documentation already in place).
- No blockers for 05-09 onward.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-17*

## Self-Check: PASSED

All 24 claimed files verified present on disk; all 5 claimed commit hashes (`4b43bb7`, `3759323`, `8166771`, `fd4ab44`, `5c42ab2`) verified present in `git log --oneline --all`.
