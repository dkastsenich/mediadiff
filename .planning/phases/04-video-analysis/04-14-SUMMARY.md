---
phase: 04-video-analysis
plan: 14
subsystem: video-analysis
tags: [sar, av1, stream-params, code-review-gap-closure, docs]

requires:
  - phase: 04-video-analysis
    provides: video.sar/video.sar.conflict/video.level checks and their resolve_sar/render_level_value implementation (04-07-PLAN.md, 04-06-PLAN.md)
provides:
  - "detail::resolve_sar refuses a non-positive denominator, folding it into the same 1:1 unset=true shape a zero numerator already resolves to"
  - "detail::render_level_value bounds its AV1 spelling to the spec-defined seq_level_idx range (0-23), falling through to the raw decimal for reserved 24-31"
  - "docs/checks/video.sar.md's unset-rule paragraph matches resolve_sar's actual rule"
affects: [04-15, 04-VERIFICATION]

actuals:
  tokens: 6500
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns: []

key-files:
  created: []
  modified:
    - src/analyzers/video/stream_params.cpp
    - tests/unit/test_video_stream_params.cpp
    - docs/checks/video.sar.md

key-decisions:
  - "Folded the non-positive-denominator case into resolve_sar's existing unset=true 1:1 shape (not a third state), per WR-01's own recommendation, to keep EffectiveSar a two-field-plus-flag value type whose invariant holds unconditionally."
  - "Tightened the AV1 branch's upper bound from 32 (full 5-bit field width) to 24 (the spec-defined seq_level_idx range), matching HEVC's own tighter level%3==0 in-file precedent of refusing to spell an undefined value."
  - "video.sar.conflict.md was left untouched: 04-REVIEW.md IN-02 and this plan's files_modified scope both name only docs/checks/video.sar.md."

patterns-established: []

requirements-completed: [VIDEO-01, VIDEO-04]

coverage:
  - id: D1
    description: "detail::resolve_sar never returns an EffectiveSar with a non-positive denominator (WR-01)"
    requirement: VIDEO-04
    verification:
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#detail::resolve_sar folds a zero denominator into unset 1:1, regardless of numerator"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#detail::resolve_sar folds a negative denominator into unset 1:1, regardless of numerator"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#detail::resolve_sar's returned denominator is strictly positive across zero, negative, and large-magnitude inputs"
        status: pass
    human_judgment: false
  - id: D2
    description: "render_level_value renders an AV1 spelling only for seq_level_idx 0-23; 24-31 fall through to the raw decimal (IN-01)"
    requirement: VIDEO-01
    verification:
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#render_level_value bounds the AV1 spelling to spec-defined seq_level_idx 0-23, falling through to the raw decimal for reserved 24-31"
        status: pass
    human_judgment: false
  - id: D3
    description: "docs/checks/video.sar.md describes the unset rule the code implements (zero numerator regardless of denominator, plus non-positive denominator) (IN-02)"
    verification:
      - kind: other
        ref: "grep -qi 'zero numerator' docs/checks/video.sar.md && grep -qiE 'denominator is (zero|0) or negative|...' docs/checks/video.sar.md"
        status: pass
      - kind: integration
        ref: "ctest -R integration.doc03_coverage"
        status: pass
    human_judgment: false

duration: 25min
completed: 2026-09-13
status: complete
---

# Phase 4 Plan 14: Video Analysis Gap Closure (SAR Denominator Guard, AV1 Level Bound, SAR Doc Fix) Summary

**`detail::resolve_sar` now refuses a non-positive denominator, `render_level_value` bounds its AV1 spelling to the 24 seq_level_idx values the spec actually defines, and `docs/checks/video.sar.md` describes the unset rule the code has always implemented.**

## Performance

- **Duration:** ~25 min
- **Completed:** 2026-09-13T11:22:52Z
- **Tasks:** 3
- **Files modified:** 3

## Accomplishments

- `detail::resolve_sar`'s early return now tests both `raw_num == 0` and `raw_den <= 0` in a single condition, so `video.sar`/`video.sar.conflict` can never be handed a `RationalValue` with a non-positive denominator. A malformed `pasp`/VUI pair (e.g. `4/0`, `4/-3`) folds into the same `1:1`, `unset=true` shape a `0/den` pair already resolves to.
- `render_level_value`'s AV1 branch upper bound is tightened from `level < 32` to `level < 24` — the AV1 specification defines levels 2.0 through 7.3 for `seq_level_idx` 0-23 only; indices 24-31 are reserved and now fall through to the existing raw-decimal fallback instead of producing a fabricated spelling.
- `docs/checks/video.sar.md`'s unset-rule paragraph now states the actual rule: a zero numerator in either position, regardless of denominator, means "declared nothing"; a zero or negative denominator is also folded the same way as a structurally degenerate ratio. The raw values remaining visible through `video.sar.conflict`'s evidence is called out explicitly.
- All three fixes carry unit tests observed failing when the fix is reverted (see Task Commits below for the exact failure output).

## Task Commits

Each task was committed atomically:

1. **Task 1: Guard resolve_sar against a non-positive denominator (WR-01)** - `8682a28` (fix)
2. **Task 2: Bound the AV1 level spelling to the indices the spec defines (IN-01)** - `f04e199` (fix)
3. **Task 3: Correct video.sar.md's unset rule to the one the code implements (IN-02)** - `4f6b12e` (docs)

## Files Created/Modified

- `src/analyzers/video/stream_params.cpp` — `detail::resolve_sar`'s early return extended to cover `raw_den <= 0`; `render_level_value`'s AV1 branch upper bound tightened from 32 to 24.
- `tests/unit/test_video_stream_params.cpp` — 4 new `TEST_CASE`s: three driving `resolve_sar` directly (zero denominator, negative denominator, and a 9-pair table asserting `den > 0` across zero/negative/large-magnitude inputs), one driving `render_level_value` across AV1 indices 0, 8, 23, 24, 31, -1 plus HEVC/H.264 spot checks.
- `docs/checks/video.sar.md` — unset-rule paragraph rewritten to state the zero-numerator (any denominator) rule and the new non-positive-denominator rule, plus a note that raw values stay visible via `video.sar.conflict`'s evidence.

## Mutation-Proof Evidence (Human Decision 3: "every changed test must be shown able to fail")

### Task 1 — resolve_sar guard reverted to `if (raw_num == 0)`

```
/home/dzka/projects/mediadiff/tests/unit/test_video_stream_params.cpp:702: FAILED:
  REQUIRE( negative_den.num == 1 )
with expansion:
  4 == 1

/home/dzka/projects/mediadiff/tests/unit/test_video_stream_params.cpp:693: FAILED:
  REQUIRE( zero_den.num == 1 )
with expansion:
  4 == 1

/home/dzka/projects/mediadiff/tests/unit/test_video_stream_params.cpp:725: FAILED:
  REQUIRE( result.den > 0 )
with expansion:
  0 > 0
```
3 of 27 tests failed. Restored (`if (raw_num == 0 || raw_den <= 0)`): 27/27 pass.

### Task 2 — AV1 level bound widened back to `level < 32`

```
/home/dzka/projects/mediadiff/tests/unit/test_video_stream_params.cpp:298: FAILED:
  REQUIRE( render_level_value("av1", 24) == "24" )
with expansion:
  "8.0" == "24"
```
The old bound fabricates "8.0" for a reserved index. Restored (`level < 24`): 27/27 pass.

## Decisions Made

- Folded the non-positive-denominator case into `resolve_sar`'s existing `unset=true` `1:1` shape rather than introducing a third state, per WR-01's own suggested fix and to preserve `EffectiveSar`'s documented invariant unconditionally.
- Left `docs/checks/video.sar.conflict.md` untouched — its own "0/1"-referencing wording is out of this plan's declared scope (`files_modified` names only `docs/checks/video.sar.md`), matching 04-REVIEW.md IN-02's exact file target.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- All three confirmed defects from 04-VERIFICATION.md Human Decision 3 that fall under this plan (WR-01, IN-01, IN-02) are closed.
- `analyzers.h` untouched (`git diff --stat -- src/analyzers/video/analyzers.h` empty), per this plan's prohibition — 04-15 owns that header next.
- Full `ctest --preset x64-linux` run: 762 passed, 0 failed, 6 skipped (baseline was 758/6; the 4-test increase matches the new unit cases this plan added).
- `git diff -- tests/golden/CORPUS_DIGEST.txt` is empty — no fixture touched.
- Ready for 04-15.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*

## Self-Check: PASSED

All key files found on disk (SUMMARY.md, src/analyzers/video/stream_params.cpp, tests/unit/test_video_stream_params.cpp, docs/checks/video.sar.md). All three task commits (8682a28, f04e199, 4f6b12e) found in git log.
