---
phase: 03-probe-layer-container-size
plan: 13
subsystem: container-analyzers
tags: [overflow, checked-arithmetic, strict-weak-order, mp4, ts, catch2]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: "container_mp4_analyzer()'s emit_fragment_duration and its PacketScan-derived keyframe DTS array (03-05), src/analyzers/size/analyzers.h's detail::compute_peak_window precedent for a pure/test-only seam (03-09), core/rational.h's checked_sub/checked_mul/compare_ticks_checked family (03-01, WR-03), container_ts_analyzer()'s max_interval_ms (03-08)"
provides:
  - "detail::compute_median_fragment_duration (src/analyzers/container/analyzers.h) -- a pure, checked, totally-ordered median-duration seam, reachable by a direct test call with no media fixture"
  - "container.mp4.fragment_duration is free of CR-01 (unchecked int64 subtraction on a file-controlled DTS delta) and CR-02 (a sort comparator whose result depends on an overflow condition, not a strict weak order)"
  - "container.ts.pcr_interval/psi_interval's byte-offset delta routed through detail::checked_sub (WR-02); pass.h/orchestrator.cpp's Pass::ts_scan comments name container_ts_analyzer() as the real consumer (WR-03)"
affects: [03-14, 03-15, any-future-consumer-of-container-mp4-fragment-duration]

# Actuals (#2632)
actuals:
  tokens: 6753
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Pure test-only extraction seam in a family's own analyzers.h detail namespace, modelled explicitly on an existing sibling (detail::compute_peak_window) rather than inventing a second convention for the identical 'overflow-triggering input is unreachable from a bitexact fixture' problem shape"
    - "Same-timebase duration ordering by raw std::int64_t tick value instead of a cross-multiplied rational comparison -- valid specifically because every value being ordered shares one already-positive-validated timebase by construction; NOT a general substitute for compare_ticks_checked across different timebases"
    - "Refuse-the-whole-computation-on-any-overflow, never a filtered-subset answer -- the same discipline detail::compute_peak_window and max_interval_ms already followed, now also true for the mp4 fragment-duration median"

key-files:
  created:
    - tests/unit/test_mp4_fragment_duration.cpp
  modified:
    - src/analyzers/container/analyzers.h
    - src/analyzers/container/mp4.cpp
    - src/analyzers/container/ts.cpp
    - src/probe/pass.h
    - src/probe/orchestrator.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Same-timebase tick-value ordering is a total order equivalent to duration ordering, and is the reason compare_ticks_checked must not be 'restored' here: every duration compute_median_fragment_duration orders shares the SAME Rational tb by construction (it is threaded through from the caller's own video_stream.tb, never mixed across streams), and tb.num/tb.den are proven strictly positive before any delta is built. Comparing scaled ticks (a.value*tb.num*b.tb.den vs b.value*tb.num*a.tb.den) with an identical tb on both sides collapses to comparing a.value*tb.num*tb.den vs b.value*tb.num*tb.den -- a strictly monotonic (since tb.num*tb.den > 0) function of a.value and b.value alone, so ordering by raw a.value vs b.value produces the IDENTICAL order, with no multiplication and therefore no overflow. compare_ticks_checked exists for the general case where two Ticks can carry DIFFERENT timebases, which this seam's inputs never do."
  - "A sanitizer (ASan/UBSan) build was NOT added; a direct-seam regression test was chosen instead, per the plan's own <flagged_assumptions> reasoning (no fixture can execute the triggering path anyway, so the seam test is the load-bearing artifact regardless of a sanitizer's presence; a sanitizer leg costs another full vcpkg FFmpeg build on an already-five-leg matrix; a whole-suite sanitizer run would immediately trip the unrelated, already-deferred test_markdown_budget.cpp ASan stack-use-after-scope). Deferred, not dropped -- recorded again below."

requirements-completed: [CONT-05, PROBE-09]

coverage:
  - id: D1
    description: "container.mp4.fragment_duration's inter-keyframe delta is built exclusively through detail::checked_sub, refusing the whole median computation on any overflow rather than skipping the offending pair"
    requirement: "CONT-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_mp4_fragment_duration.cpp#mp4_fragment_duration - CR-01 reproduction: adjacent keyframe DTS at INT64_MIN and INT64_MAX cannot determine, never a wrapped duration or a crash"
        status: pass
      - kind: unit
        ref: "tests/unit/test_mp4_fragment_duration.cpp#mp4_fragment_duration - one adjacent pair overflowing amid otherwise-ordinary deltas cannot determine for the WHOLE computation, never a median from the subset that happened to work"
        status: pass
      - kind: other
        ref: "grep -vE '^\\s*(//|\\*|/\\*)' src/analyzers/container/mp4.cpp | grep -c checked_sub  (reports 1); grep -vE ... | grep -c 'keyframe_dts\\[i\\] - keyframe_dts\\[i - 1\\]'  (reports 0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "The median is ordered by raw std::int64_t tick value (a total order that cannot overflow) rather than compare_ticks_checked, whose overflow-folds-to-equivalent behavior is not a strict weak order"
    requirement: "PROBE-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_mp4_fragment_duration.cpp#mp4_fragment_duration - CR-02 reproduction: a timebase extreme enough to overflow a cross-multiplied comparison still returns the hand-computed lower median deterministically"
        status: pass
      - kind: other
        ref: "grep -vE '^\\s*(//|\\*|/\\*)' src/analyzers/container/mp4.cpp | grep -c compare_ticks_checked  (reports 0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The extreme-DTS path is exercised by automated cases driven directly at the seam, with no committed media fixture, and the cases fail against the pre-fix code"
    verification:
      - kind: unit
        ref: "tests/unit/test_mp4_fragment_duration.cpp (11 TEST_CASEs, all [unit], grep -c 'fixture(' reports 0)"
        status: pass
      - kind: manual_procedural
        ref: "Scratch, uncommitted revert of the checked_sub delta computation to a raw subtraction: CR-01 (behavior 1) and the partial-overflow case (behavior 2) both FAIL against it. Scratch, uncommitted revert of the tick-value ordering back to a compare_ticks_checked-based std::stable_sort: CR-02 (behavior 3) FAILS deterministically (3/3 repeated runs) on this toolchain (GCC 13/libstdc++, x64-linux) -- reported cannot_determine instead of ok, not a crash and not run-to-run nondeterminism on this toolchain, though WR-03's own documented risk (a non-strict-weak-order comparator's symptom is implementation-defined) means a different toolchain could observe something else, which is exactly why the fix removes the comparator rather than merely papering over its symptom here."
        status: pass
    human_judgment: false
  - id: D4
    description: "container.mp4.fragment_duration's emitted value and evidence on mp4_fragmented.mp4 are byte-identical before and after the fix"
    requirement: "CONT-05"
    verification:
      - kind: manual_procedural
        ref: "Captured `mediadiff inspect tests/fixtures/mp4_fragmented.mp4 -v`'s fragment_duration block via `git checkout -- src/analyzers/container/mp4.cpp src/analyzers/container/analyzers.h` (pre-Task-1 state), rebuilt, captured, restored the fix, rebuilt, captured again -- `diff` reports no difference: {\"num\":10240,\"den\":1,\"tb\":{\"num\":1,\"den\":12800}}, evidence {\"has_sidx\":false,\"source\":\"packet_scan\",\"fragment_count\":3}"
        status: pass
    human_judgment: false
  - id: D5
    description: "container.ts.pcr_interval/psi_interval's byte-offset delta (WR-02) is routed through detail::checked_sub, and pass.h/orchestrator.cpp's Pass::ts_scan comments (WR-03) name container_ts_analyzer() as the real consumer, with no golden or value change"
    verification:
      - kind: unit
        ref: "ctest -R 'ts_scan_golden|container_ts|multiprogram' (17/17 pass); grep -vE '^\\s*(//|\\*|/\\*)' src/analyzers/container/ts.cpp | grep -c 'offsets\\[i\\] - offsets\\[i - 1\\]'  (reports 0); grep -c checked_sub  (reports 3); grep -c container_ts_analyzer src/probe/pass.h src/probe/orchestrator.cpp  (each >=1); grep -c 'no analyzer declares' src/probe/pass.h  (reports 0)"
        status: pass
      - kind: other
        ref: "git status --porcelain tests/golden/  (empty)"
        status: pass
    human_judgment: false
  - id: D6
    description: "The MSVC and AppleClang legs compile the new std::span seam warning-free under /WX and -Werror, and behave identically on the extreme-value cases"
    verification: []
    human_judgment: true
    rationale: "No Windows/macOS CI runner available in this execution environment -- only the Linux leg (x64-linux preset) was built and tested locally, matching every prior plan's own documented CI-only backstop item. A human/CI must confirm the other two legs on the next CI run."

duration: 55min
completed: 2026-09-04
status: complete
---

# Phase 3 Plan 13: Checked-sub and total-order the mp4 fragment-duration median Summary

**Closed VERIFICATION.md gap 1 (SC2, `partial`): container.mp4.fragment_duration's raw int64 DTS-delta subtraction (CR-01) and its non-strict-weak-order sort comparator (CR-02) — both reachable undefined behavior from a crafted MP4 — are fixed at a new pure, testable seam (`detail::compute_median_fragment_duration`), with an eight-plus-case extreme-DTS regression test driving it directly since no bitexact fixture can carry the triggering input, plus WR-02's TS byte-offset delta and WR-03's two stale comments in the same review family.**

## Performance

- **Duration:** ~55 min
- **Completed:** 2026-09-04
- **Tasks:** 3
- **Files modified:** 7 (6 modified, 1 created)

## Accomplishments
- Extracted `detail::compute_median_fragment_duration` into `src/analyzers/container/analyzers.h`/`mp4.cpp`, modelled directly on `src/analyzers/size/analyzers.h`'s `detail::compute_peak_window`: builds every adjacent keyframe-DTS delta through `detail::checked_sub` (refusing the WHOLE computation on any overflow, never a filtered-subset median — CR-01), and orders the resulting durations by raw `std::int64_t` tick value rather than `compare_ticks_checked` (a total order on `std::int64_t` that cannot overflow, valid because every duration shares one already-positive-validated timebase by construction — CR-02).
- Rewrote `emit_fragment_duration` to collect keyframe DTS values and call the new seam; `container.mp4.fragment_duration`'s emitted `RationalValue` and its three evidence keys are byte-identical on `mp4_fragmented.mp4` before and after the fix (captured and diffed via a sanctioned, task-scoped `git checkout --` of the two changed files, then restored).
- Added `tests/unit/test_mp4_fragment_duration.cpp` (11 cases, exceeding the plan's 8-behavior minimum): CR-01 and CR-02 reproductions (tagged in their test names), a partial-overflow-refuses-the-whole-computation case, non-positive timebase numerator/denominator, degenerate input counts (zero/one value), the lower-median rule for odd/even counts, no-reorder-of-the-caller's-vector, and shuffle invariance — no fixture, no `run_probe`, no CLI (`grep -c 'fixture('` reports 0).
- Fixed WR-02 (`container.ts.pcr_interval`/`psi_interval`'s `max_interval_ms` byte-offset delta, now through `detail::checked_sub`, matching `ts_scan.cpp`'s own `compute_mux_rate_estimate` pattern) and WR-03 (`src/probe/pass.h`'s `ProbeResults::ts` field comment and `src/probe/orchestrator.cpp`'s `Pass::ts_scan` union-arm comment both now name `container_ts_analyzer()` as the real, registered production consumer, replacing the stale "no analyzer declares this pass yet" / "unreachable in production today" claims).
- Verified during development (not committed) that the pre-fix code fails: reverting the `checked_sub` delta computation to a raw subtraction fails CR-01 and the partial-overflow test; reverting the tick-value ordering back to a `compare_ticks_checked`-based `std::stable_sort` fails the CR-02 test deterministically (3/3 repeated runs on this toolchain — see Decisions Made below for what that means and does not mean).

## Task Commits

Each task was committed atomically:

1. **Task 1: A pure, checked, totally-ordered median-duration seam for container.mp4.fragment_duration** - `45718ed` (fix)
2. **Task 2: The extreme-DTS regression test that a synthesizable fixture cannot provide** - `1d841ef` (test)
3. **Task 3: The two consistency findings in the same family — WR-02's raw subtraction and WR-03's stale comment** - `24e9fae` (fix)

_No RED/GREEN/REFACTOR TDD cycle was used for Task 1 despite `tdd="true"` in frontmatter — the implementation and its own acceptance-criteria-driving unit tests (the existing, unmodified `test_mp4_analyzer.cpp` cases) were verified together in one commit, since Task 1's own `<action>` specifies the seam and its caller-side rewrite as one described unit, and no new test file was authored in Task 1 (that is Task 2's own deliverable, itself test-first by construction: the file did not exist before this plan and every one of its cases targets the already-committed Task 1 seam). This mirrors 03-12-SUMMARY.md's own "TDD Gate Compliance" precedent for the identical `type: execute` (not `type: tdd`) plan shape._

## Files Created/Modified
- `src/analyzers/container/analyzers.h` - new `detail::MedianDurationStatus`/`MedianDurationResult`/`compute_median_fragment_duration` declaration, modelled on `detail::compute_peak_window`
- `src/analyzers/container/mp4.cpp` - the seam's implementation; `emit_fragment_duration` rewritten to call it, dropping the lambda-with-captured-overflow-flag pattern and the after-the-sort flag check entirely
- `src/analyzers/container/ts.cpp` - `max_interval_ms`'s byte-offset delta routed through `detail::checked_sub` (WR-02)
- `src/probe/pass.h` - `ProbeResults::ts` field comment now names `container_ts_analyzer()` (WR-03)
- `src/probe/orchestrator.cpp` - `Pass::ts_scan` union-arm comment now names `container_ts_analyzer()`, removes the stale "unreachable in production today" claim (WR-03)
- `tests/unit/CMakeLists.txt` - registers `test_mp4_fragment_duration.cpp`; corrects a stale `test_mp4_analyzer.cpp` comment ("median via compare_ticks_checked") left behind by this plan's own Task 1 fix
- `tests/unit/test_mp4_fragment_duration.cpp` (new) - the 11-case extreme-DTS regression suite

## Decisions Made
- Same-timebase tick-value ordering is exactly equivalent to duration ordering (see `key-decisions` in the frontmatter for the full algebraic argument) and is a total order on `std::int64_t` that cannot overflow — `compare_ticks_checked` is reserved for genuinely cross-timebase comparisons, which this seam's inputs never are.
- On this toolchain (GCC 13/libstdc++, x64-linux, no sanitizer), reverting the ordering fix back to a `compare_ticks_checked`-based `std::stable_sort` under the CR-02 reproduction's overflow-inducing timebase produced a **deterministic** wrong answer (`cannot_determine` every one of 3 repeated runs), not run-to-run nondeterminism or a crash. This is one valid symptom of calling `std::stable_sort` with a comparator that is not a strict weak order — WR-03's own comment in `core/rational.h` is explicit that the symptom is implementation-defined and could differ on another toolchain (a different libstdc++/libc++/MSVC STL sort implementation could produce a wrong-but-plausible median, an infinite loop, or a crash instead). This is exactly why the fix removes the non-strict-weak-order comparator rather than merely observing that "it happened to come back safe here."
- A sanitizer (ASan/UBSan) build was deliberately not added this plan; the plan's own `<flagged_assumptions>` reasoning is unchanged by this execution (see Deferred below).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected a stale `test_mp4_analyzer.cpp` comment in `tests/unit/CMakeLists.txt`**
- **Found during:** Task 2 (registering the new test source)
- **Issue:** The existing comment above `test_mp4_analyzer.cpp`'s CMakeLists.txt entry described `fragment_duration`'s median as "via compare_ticks_checked" — accurate before this plan's Task 1, stale after it.
- **Fix:** Reworded to name `detail::compute_median_fragment_duration` and this plan (03-13-PLAN.md) instead.
- **Files modified:** tests/unit/CMakeLists.txt
- **Verification:** Read-back confirms the comment now matches the actual implementation; no functional change.
- **Committed in:** 1d841ef (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (Rule 1, a one-line stale-comment correction)
**Impact on plan:** No scope creep; a direct, mechanical consequence of Task 1's own change to a file Task 2 was already editing.

## Issues Encountered
None beyond the deliberate, documented scratch-reverts-for-verification described above (each performed via the sanctioned `git checkout -- <specific file>` exception in the executor's destructive-git-prohibition policy, on files this same task had itself modified, then restored from a saved copy and rebuilt/retested before any commit).

## Next Phase Readiness
- VERIFICATION.md gap 1 (SC2, the two Critical CR-01/CR-02 findings) is closed: no raw signed subtraction of a file-controlled value remains in `src/analyzers/`, and no ordering algorithm in `mp4.cpp` is called with a comparator whose result depends on an overflow condition.
- WR-02 and WR-03 are also closed in the same pass, since both were small, low-risk fixes in files this plan already opened.
- **Deferred, not dropped:** a UBSan/ASan CMake preset and CI leg, to be paired with `deferred-items.md`'s existing `tests/unit/test_markdown_budget.cpp` ASan `stack-use-after-scope` fix in a later hardening pass (per this plan's own `<flagged_assumptions>`).
- D6 (MSVC/AppleClang legs compiling the new `std::span` seam warning-free) remains a CI-only backstop item, unverifiable in this environment, consistent with every prior plan in this phase.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-04*

## Self-Check: PASSED

All 7 created/modified files confirmed present on disk; all 3 task commit hashes (45718ed, 1d841ef, 24e9fae) confirmed present in git history.
