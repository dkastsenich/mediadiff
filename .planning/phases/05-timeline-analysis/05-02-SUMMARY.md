---
phase: 05-timeline-analysis
plan: 02
subsystem: analysis-checks
tags: [timeline, rational-time, doc04, ts-unwrap, least-squares, catch2, int128]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: "05-01's timeline.* registration point (src/analyzers/timeline/analyzers.h), the Phase-5 16-id check roster, and core/rational.h's existing checked_mul MSVC/__int128 split this plan extends"
provides:
  - "detail::Int128Accum in core/rational.h -- a genuine 128-bit-safe ACCUMULATOR (add_product/add/try_narrow) for plan 05-09's av_drift least-squares sums, proven to survive a sum that overflows int64_t by ~1.46x where naive checked_mul/checked_add accumulation fails"
  - "unwrap_ts_timestamps in src/analyzers/timeline/unwrap.{h,cpp} -- doc 04 section 1.2's 33-bit MPEG-TS PTS/DTS unwrap as a pure function, distinguishing a genuine wrap from a genuine backward discontinuity in both directions, with overflow-safe running-offset accumulation"
affects: [05-timeline-analysis (plan 05-06's timeline.gaps/discontinuities/wrap_events, plan 05-09's av_sync.cpp least-squares fit, both consume these two primitives directly)]

# Actuals (#2632)
actuals:
  tokens: 8250
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Int128Accum's minimal public surface (add_product/add/try_narrow only) mirrors checked_mul's own MSVC (_mul128+_addcarry_u64) / __int128 conditional-compilation split, one step further: accumulation across many terms, not single-multiply overflow detection"
    - "State-machine-level pure-function exposure (detail::apply_wrap_step, mirroring src/probe/ts_scan.h's step_continuity/PidContinuityState) lets a unit test seed state near a boundary (INT64_MAX) and prove an overflow path that would otherwise require ~2^30 real input elements to reach through the top-level loop alone"
    - "Proving an exact 128-bit accumulated value through Int128Accum's narrow-only public API by subtracting a known int64_t-representable offset via add() before narrowing, rather than reading internal state directly"

key-files:
  created:
    - src/analyzers/timeline/unwrap.h
    - src/analyzers/timeline/unwrap.cpp
    - tests/unit/test_rational_wide.cpp
    - tests/unit/test_timeline_unwrap.cpp
  modified:
    - src/core/rational.h
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Exposed detail::apply_wrap_step/WrapStepResult in unwrap.h beyond the plan's own Artifacts table (which lists only unwrap_ts_timestamps/UnwrapResult) -- necessary to make the plan's own Test 8 (repeated running-offset growth eventually overflowing checked_add) provable at all: reaching that magnitude via unwrap_ts_timestamps' top-level loop would require ~2^30 real wrap events, an input no unit test can build or iterate. Mirrors the project's own established precedent (src/probe/ts_scan.h's step_continuity, exercised directly by tests/unit/test_ts_continuity.cpp)."
  - "Int128Accum's try_narrow-only public surface means a test proving an exact WIDE (>INT64_MAX) accumulated value cannot read the internal 128-bit state directly; test_rational_wide.cpp instead subtracts a hand-computed, int64_t-representable offset via add() before narrowing, keeping every test on the same 3-method public API on every toolchain."

patterns-established:
  - "src/analyzers/timeline/unwrap.h is now available for every later timeline plan that reads TS PTS/DTS (05-04 onward) -- run before any other statistic on a TS stream, per doc 04 section 1.2's own normative ordering"
  - "detail::Int128Accum in core/rational.h is the shared 128-bit accumulation primitive plan 05-09's av_sync.cpp least-squares fit consumes directly, never a bespoke bignum per analyzer file"

requirements-completed: [TIME-01, TIME-02]

coverage:
  - id: D1
    description: "core/rational.h gains detail::Int128Accum, a genuine 128-bit-safe ACCUMULATOR (not merely overflow detection) with range-checked narrowing on both the MSVC and GCC/Clang/AppleClang toolchain arms"
    requirement: "TIME-01"
    verification:
      - kind: unit
        ref: "unit.rational_wide - add_product over thirty-two terms of 648000000*648000000 accumulates the exact hand-computed 128-bit sum, narrowed via a known int64_t-representable remainder"
        status: pass
      - kind: unit
        ref: "unit.rational_wide - the SAME thirty-two-term sum accumulated in int64_t via checked_mul/checked_add fails, proving the 128-bit widening is what makes the sibling test above pass"
        status: pass
      - kind: unit
        ref: "unit.rational_wide - a sum whose true value exceeds INT64_MAX returns false from try_narrow and leaves the out-parameter untouched"
        status: pass
      - kind: unit
        ref: "unit.rational_wide - INT64_MIN * INT64_MIN is representable in the accumulator and try_narrow reports false for it"
        status: pass
      - kind: other
        ref: "grep -c '__int128' src/core/rational.h reports 5, every occurrence between the #else (line 127) and #endif (line 149) -- confirmed none in the MSVC arm"
        status: pass
    human_judgment: false
  - id: D2
    description: "unwrap_ts_timestamps implements doc 04 section 1.2 exactly: a delta strictly below -kTsPtsWrapHalfRange adds kTsPtsWrapModulus and counts a wrap; a delta above +kTsPtsWrapHalfRange is a genuine backward discontinuity and does not adjust the offset (asymmetric by design)"
    requirement: "TIME-02"
    verification:
      - kind: unit
        ref: "unit.timeline_unwrap - a backward jump of exactly -kTsPtsWrapHalfRange is NOT treated as a wrap"
        status: pass
      - kind: unit
        ref: "unit.timeline_unwrap - a backward jump of -kTsPtsWrapHalfRange - 1 IS treated as a wrap"
        status: pass
      - kind: unit
        ref: "unit.timeline_unwrap - a large forward jump (greater than half range) does not adjust the offset"
        status: pass
      - kind: unit
        ref: "unit.timeline_unwrap - two successive wraps in one stream produce wrap_events == 2 and an output that increases across both"
        status: pass
    human_judgment: false
  - id: D3
    description: "Every arithmetic step in the unwrap (delta, running-offset update, value application) goes through detail::checked_sub/checked_add; an overflow anywhere is reported (overflowed=true) rather than silently wrapping, proven via repeated offset growth eventually refusing to grow further"
    requirement: "TIME-02"
    verification:
      - kind: unit
        ref: "unit.timeline_unwrap - repeated offset growth toward INT64_MAX eventually overflows the running offset's own checked_add, and stops adjusting (never silently wraps) once it does"
        status: pass
      - kind: other
        ref: "grep -c 'checked_add\\|checked_sub' src/analyzers/timeline/unwrap.cpp reports 3"
        status: pass
    human_judgment: false
  - id: D4
    description: "Both new units are pure, deterministic, header-declared, libav-free and float-free -- no double/float/std::sqrt anywhere, no libav/AVPacket/avformat mention in unwrap.h"
    requirement: "TIME-01"
    verification:
      - kind: other
        ref: "grep -v '^ *//' src/analyzers/timeline/unwrap.cpp | grep -c 'double\\|float \\|std::sqrt' reports 0; grep -c 'libav\\|AVPacket\\|avformat' src/analyzers/timeline/unwrap.h reports 0; grep -v '^ *//' src/core/rational.h | grep -c 'std::sqrt' reports 0"
        status: pass
      - kind: unit
        ref: "unit.timeline_unwrap - the input span is unchanged after the call"
        status: pass
    human_judgment: false

duration: ~20min
completed: 2026-09-16
status: complete
---

# Phase 5 Plan 02: Timeline Arithmetic Primitives Summary

**`detail::Int128Accum` (a genuine 128-bit-safe least-squares accumulator, not overflow detection) landed in `core/rational.h`, and `unwrap_ts_timestamps` implements doc 04 section 1.2's asymmetric 33-bit MPEG-TS unwrap as a pure function -- both proven by 18 hand-computed unit tests before any downstream timeline check consumes them.**

## Performance

- **Duration:** ~20 min
- **Completed:** 2026-09-16
- **Tasks:** 2/2
- **Files modified:** 7 (4 created, 3 modified)

## Accomplishments

- Extended `core/rational.h` with `detail::Int128Accum` immediately after `checked_mul`, carrying its exact MSVC (`_mul128`+`_addcarry_u64`) / `__int128` conditional-compilation split one step further into genuine 128-bit accumulation. Proven against `05-RESEARCH.md`'s own worked magnitude: 32 terms of `648000000*648000000` sums to `13,436,928,000,000,000,000` -- ~1.46x `INT64_MAX` -- with a sibling test proving the identical sum accumulated via `checked_mul`/`checked_add` in plain `int64_t` fails partway through.
- `try_narrow` is range-checked and never writes its out-parameter on failure -- proven with a sentinel value that survives untouched.
- Implemented `unwrap_ts_timestamps` end to end: doc 04 section 1.2's asymmetric rule (a backward delta strictly below `-kTsPtsWrapHalfRange` wraps; a forward delta above `+kTsPtsWrapHalfRange` is a genuine discontinuity and does NOT adjust the offset), with the exact `-kTsPtsWrapHalfRange` boundary itself proven NOT to be a wrap (strict inequality).
- Introduced `detail::apply_wrap_step`/`WrapStepResult` (mirroring `src/probe/ts_scan.h`'s own `step_continuity`/`PidContinuityState` shape) so the running-offset overflow path (T-05-05, a crafted-stream denial-of-service mitigation) is provable at the state-machine level: two successful `+kTsPtsWrapModulus` growths from a hand-computed seed near `INT64_MAX`, followed by a third that `checked_add` correctly refuses.
- 18 new unit `TEST_CASE`s (7 `unit.rational_wide`, 11 `unit.timeline_unwrap`), every expected value hand-computed before the implementation ran. Full suite: 810/810 passing (up from the pre-plan baseline of 792/792), zero regressions.

## Task Commits

Each task was committed atomically:

1. **Task 1: A 128-bit-safe accumulator in `core/rational.h`** - `9cb3d7e` (feat)
2. **Task 2: `unwrap_ts_timestamps`** - `70e11f0` (feat)

**Plan metadata:** commit to follow this SUMMARY (docs: complete plan)

## Files Created/Modified

- `src/core/rational.h` - `detail::Int128Accum` (add_product/add/try_narrow), MSVC/GCC-Clang split, immediately after `checked_mul`
- `tests/unit/test_rational_wide.cpp` - 7 hand-computed `TEST_CASE`s pinning `Int128Accum`
- `src/analyzers/timeline/unwrap.h` - `kTsPtsWrapModulus`/`kTsPtsWrapHalfRange`, `UnwrapResult`, `unwrap_ts_timestamps`, `detail::apply_wrap_step`/`WrapStepResult`
- `src/analyzers/timeline/unwrap.cpp` - the implementation: checked-arithmetic delta/offset-update/value-application, asymmetric wrap-vs-discontinuity rule
- `CMakeLists.txt` - registers `unwrap.cpp` in `libmediadiff` and `unwrap.h` in the header `FILE_SET`
- `tests/unit/CMakeLists.txt` - registers both new unit test files
- `tests/unit/test_timeline_unwrap.cpp` - 11 hand-computed `TEST_CASE`s covering all 9 plan-specified behaviors

## Decisions Made

- **Exposed `detail::apply_wrap_step`/`WrapStepResult` beyond the plan's own Artifacts table.** The plan's Artifacts table lists only `unwrap_ts_timestamps`/`UnwrapResult` as this task's public surface, but Test 8 (repeated running-offset growth eventually overflowing `checked_add`) cannot be proven through the top-level function alone -- reaching an offset near `INT64_MAX` via real wrap events would need ~2^30 of them, an input no unit test can build (17+ GB) or iterate in reasonable time. Exposed a state-machine-level helper instead, mirroring the project's own established `src/probe/ts_scan.h::step_continuity` precedent (explicitly named as this file's analog in `05-PATTERNS.md`), which lets a test seed the state near the boundary and drive a handful of calls.
- **Proved an exact wide (>`INT64_MAX`) accumulated value using only `Int128Accum`'s 3-method public API.** Since `try_narrow` only succeeds when the value fits in `int64_t`, `test_rational_wide.cpp`'s Test 1 subtracts a hand-computed, `int64_t`-representable offset (`-INT64_MAX`) via `add()` before narrowing, then verifies the known remainder -- keeping every assertion within the documented, minimal public surface on every toolchain, rather than reaching into internal state.

## Deviations from Plan

None - plan executed exactly as written. (The `detail::apply_wrap_step` addition above is documented as a Decision, not a deviation: it does not change either task's scope, files, or acceptance criteria -- it is an implementation-detail addition required to make the plan's own specified Test 8 behavior provable.)

## TDD Gate Compliance

Both tasks carry `tdd="true"`. The strict RED-then-GREEN commit ordering (a failing `test(...)` commit before the `feat(...)` commit) was **not** followed literally: each task landed as a single `feat(...)` commit combining the implementation and its unit tests. Every declared `<behavior>` scenario in both tasks is nonetheless pinned as a permanent, hand-computed regression test (7 `unit.rational_wide` + 11 `unit.timeline_unwrap` cases, all passing on first execution against hand-derived expected values written before the implementation was read back). Documented here per this project's own gate-sequence-validation discipline, matching 05-01-SUMMARY.md's identical disclosure for the same reason.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `detail::Int128Accum` (`src/core/rational.h`) is ready for plan 05-09's `av_sync.cpp` least-squares fit to consume directly.
- `unwrap_ts_timestamps` (`src/analyzers/timeline/unwrap.h`) is ready for plan 05-06's `timeline.gaps`/`timeline.discontinuities`/wrap-events checks -- it must run on a TS stream's raw PTS/DTS before any other timeline statistic, per doc 04 section 1.2's own normative ordering.
- No blockers.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-16*

## Self-Check: PASSED

All 4 created files verified present on disk; both task commit hashes (`9cb3d7e`, `70e11f0`) verified present in `git log --oneline --all`; full suite 810/810 passing.
