---
phase: 04-video-analysis
plan: 15
subsystem: video-analysis
tags: [interlace, field-order, evidence, gap-closure, ffmpeg]

requires:
  - phase: 04-video-analysis
    provides: "video.interlace's classify_interlace/emit_video_interlace (04-10), whose disagreement evidence field this plan repairs"
provides:
  - "video.interlace's disagreement evidence field compares field-order CLASS (top-coded-first / bottom-coded-first / progressive / unknown), not raw AVFieldOrder ordinals -- false on a correctly-encoded interlaced file, true only on a genuine conflict"
affects: [video-analysis, hdr, gop]

actuals:
  tokens: 4503
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Field-order CLASS mapping: a total, file-local enum folding {TT,TB} and {BB,BT} into shared classes so two disjoint AVFieldOrder domains (container declaration vs per-AU parser observation) can be compared meaningfully"

key-files:
  created: []
  modified:
    - src/analyzers/video/interlace.cpp
    - src/analyzers/video/analyzers.h
    - tests/unit/test_video_interlace.cpp
    - docs/checks/video.interlace.md

key-decisions:
  - "Field-order class mapping is file-local (mediadiff::detail, not exported through analyzers.h) since no test needs to drive it independently of classify_interlace, per the plan's own scoping instruction."
  - "Mutation check (revert single-branch computation to raw ordinal equality) confirmed exactly 4 of 22 tests fail: the two real-fixture cases and the two synthetic TT-vs-TB/BB-vs-BT class cases -- proving the new coverage is load-bearing, not vacuous."

patterns-established:
  - "When two evidence-producing domains draw from disjoint subsets of the same closed enum, compare a derived equivalence class instead of raw ordinals, and record the empirical read-back table that justifies the class mapping directly in the source comment."

requirements-completed: [VIDEO-06]

coverage:
  - id: D1
    description: "classify_interlace's single-branch disagreement computation compares field-order CLASS, not raw AVFieldOrder ordinal, folding {TT,TB} and {BB,BT} while keeping UNKNOWN/PROGRESSIVE distinct"
    requirement: VIDEO-06
    verification:
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#classify_interlace: observed TOP_FIELD_FIRST(2) against declared TOP_CODED_BOTTOM_DISPLAYED(4)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#classify_interlace: observed BOTTOM_FIELD_FIRST(3) against declared BOTTOM_CODED_TOP_DISPLAYED(5)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#classify_interlace: observed TOP_FIELD_FIRST(2) against declared BOTTOM_FIELD_FIRST(3)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#classify_interlace: observed BOTTOM_FIELD_FIRST(3) against declared TOP_CODED_BOTTOM_DISPLAYED(4)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#classify_interlace: observed PROGRESSIVE(1) against declared UNKNOWN(0)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#classify_interlace: observed TOP_FIELD_FIRST(2) against declared UNKNOWN(0)"
        status: pass
      - kind: unit
        ref: "ctest -R unit.video_interlace (22 tests, includes pre-existing byte-unchanged agree/disagree cases)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Real-fixture proof that video_ilace_tff.mp4 (declared TB, observed TT) and video_ilace_bff.mp4 (declared BT, observed BB) report disagreement==false, with a mutation check proving the coverage is load-bearing"
    requirement: VIDEO-06
    verification:
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#video_ilace_tff.mp4: cross-checked, observed top_field_first, disagreement FALSE"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp#video_ilace_bff.mp4: cross-checked, observed bottom_field_first, disagreement FALSE"
        status: pass
      - kind: other
        ref: "manual mutation check: reverted disagreement computation to raw ordinal equality, confirmed exactly these 2 cases plus 2 synthetic class cases FAIL (4/22), then restored and reconfirmed 22/22 green"
        status: pass
    human_judgment: false
  - id: D3
    description: "docs/checks/video.interlace.md describes the coded-first class comparison and states disagreement is evidence-only"
    verification:
      - kind: other
        ref: "grep -qiE 'coded first|first-coded|coded-first' and 'evidence only|evidence-only' docs/checks/video.interlace.md"
        status: pass
      - kind: integration
        ref: "ctest -R integration.doc03_coverage"
        status: pass
    human_judgment: false

duration: 15min
completed: 2026-09-13
status: complete
---

# Phase 4 Plan 15: video.interlace disagreement evidence fix Summary

**`video.interlace`'s `disagreement` evidence field now compares field-order CLASS instead of raw `AVFieldOrder` ordinals, so it reads `false` on both real non-conflicting interlaced fixtures instead of unconditionally `true`.**

## Performance

- **Duration:** 15 min
- **Started:** 2026-09-13T11:23:59Z
- **Completed:** 2026-09-13T11:38:46Z
- **Tasks:** 3
- **Files modified:** 4

## Accomplishments

- Added a total, file-local `FieldOrderClass` mapping in `src/analyzers/video/interlace.cpp` (`mediadiff::detail`) that folds `{kAvFieldTt, kAvFieldTb}` into `top_coded_first` and `{kAvFieldBb, kAvFieldBt}` into `bottom_coded_first`, keeping `unknown`/`progressive` distinct from each other and from either interlaced class.
- Rewrote `classify_interlace`'s `single`-branch `disagreement` computation to compare classes instead of raw ordinals — the only line in the branch that changed; `value`, `kind`, `cross_check_possible`, `counts`, and `total_observed` assignments are byte-unchanged.
- Replaced the top-of-file "harmless BY DESIGN" comment with an honest read-back table (declared TB(4)/observed TT(2) for `video_ilace_tff.mp4`, declared BT(5)/observed BB(3) for `video_ilace_bff.mp4`), re-measured against the real linked FFmpeg via `mediadiff inspect` before writing.
- Corrected the `disagreement` member comment in `analyzers.h` to describe the class rule, keeping the "evidence-only" sentence intact.
- Added 6 new synthetic unit cases (all 9 required behaviour rows now covered: 3 pre-existing + 6 new) and 2 real-fixture cases proving `disagreement == false` on both real interlaced fixtures.
- Ran a mutation check: reverted the fix to raw-ordinal equality, confirmed exactly 4 of 22 tests fail by name (the 2 real-fixture cases + the 2 synthetic TT-vs-TB/BB-vs-BT cases), then restored the fix and reconfirmed 22/22 green.
- Updated `docs/checks/video.interlace.md` to describe the coded-first class comparison and state `disagreement` is evidence-only, never gating.

## Task Commits

1. **Task 1: Record the two-domain read-back table and compare field-order CLASS, not ordinal** - `25ba9ae` (fix)
2. **Task 2: Prove both directions against the real fixtures, and prove the tests can fail** - `3554aa3` (test)
3. **Task 3: Bring docs/checks/video.interlace.md's disagreement wording in line** - `380e4ed` (docs)

## Files Created/Modified

- `src/analyzers/video/interlace.cpp` - `FieldOrderClass` enum + `field_order_class()` mapping; `classify_interlace`'s `disagreement` now compares classes; top-of-file comment replaced with the measured read-back table
- `src/analyzers/video/analyzers.h` - `InterlaceClassification::disagreement` member comment corrected to describe the class rule
- `tests/unit/test_video_interlace.cpp` - 6 new synthetic class-comparison cases, 2 new real-fixture cases, file header per-test inventory brought current
- `docs/checks/video.interlace.md` - disagreement paragraph rewritten around the coded-first class rule and evidence-only status

## Decisions Made

- The class mapping lives file-local in `mediadiff::detail` inside `interlace.cpp` rather than being exported through `analyzers.h`, per the plan's own scoping instruction (no test needs it independent of `classify_interlace`).
- Kept the class enum ordering unrelated to raw `AVFieldOrder` values (an independent 4-member enum), avoiding any temptation to encode a numeric relationship that doesn't exist between the two domains.

## Deviations from Plan

None - plan executed exactly as written. All acceptance criteria for all three tasks were verified directly (grep checks, `git diff` inspection, `ctest` runs, and the mutation check) before being marked complete.

## Issues Encountered

None.

## Mutation Check Evidence (Human Decision 3's own requirement)

**Reverted** (`result.disagreement = result.value != declared_field_order_raw;`, class helper left in place):

```
82% tests passed, 4 tests failed out of 22

The following tests FAILED:
	551 - unit.video_interlace - classify_interlace: observed BOTTOM_FIELD_FIRST(3) against declared BOTTOM_CODED_TOP_DISPLAYED(5) -- same class, no disagreement (the real video_ilace_bff.mp4 shape) (Failed)
	555 - unit.video_interlace - classify_interlace: observed TOP_FIELD_FIRST(2) against declared TOP_CODED_BOTTOM_DISPLAYED(4) -- same class, no disagreement (the real video_ilace_tff.mp4 shape) (Failed)
	561 - unit.video_interlace - video_ilace_bff.mp4: cross-checked, observed bottom_field_first, disagreement FALSE (declared BT vs observed BB is the same coded-first class) (Failed)
	564 - unit.video_interlace - video_ilace_tff.mp4: cross-checked, observed top_field_first, disagreement FALSE (declared TB vs observed TT is the same coded-first class) (Failed)
```

**Restored** (`result.disagreement = field_order_class(result.value) != field_order_class(declared_field_order_raw);`):

```
100% tests passed, 0 tests failed out of 22
```

`git status --porcelain src/analyzers/video/interlace.cpp` confirmed empty after restoring — the mutation was never committed.

## Verification Results

- `cmake --build --preset x64-linux` — warning-clean, no `-Wswitch` violation from the new `FieldOrderClass` switch (has a `default` covering the unrecognised-value branch, matching `field_order_name`'s own posture).
- `ctest --test-dir build/x64-linux -R "unit\.video_interlace"` — 22/22 passed.
- `ctest --test-dir build/x64-linux -R "integration\.doc03_coverage"` — 2/2 passed.
- `ctest --preset x64-linux --output-on-failure` — 770/770 ran tests passed (6 pre-existing golden/console tests skipped, same skip set as before this plan), no drop from the pre-plan baseline.
- `git diff -- tests/golden/CORPUS_DIGEST.txt` — empty, no fixture line touched.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`video.interlace`'s evidence layer now matches its own cross-check's intent; the compared value, `mixed` proportions, and `no_cross_check` path remain independently verified unchanged. This closes 04-VERIFICATION.md's fourth gap (SC3's evidence-field defect) and deferred-items.md's 04-10 entry. Remaining Phase 4 gap-closure plans (04-16 through 04-19) and the corpus-digest draft-PR step (Human Decision 4) are still outstanding before phase re-verification.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*
