---
phase: 04-video-analysis
plan: 10
subsystem: video-analysis
tags: [ffmpeg, mpeg2video, h264, field_order, interlace, catch2]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "plan 04-01's ParserScan (AccessUnitRecord::field_order/repeat_pict), plan 04-02's mpeg2video interlace fixture corpus (video_ilace_tff/bff/mixed/tff_copy)"
provides:
  - "video.interlace: codecpar->field_order (declared) cross-checked against the per-access-unit field_order ParserScanResult records (observed), the observed value winning when a cross-check is possible"
  - "StreamInfo::field_order_raw (demux_session.h/.cpp) -- the raw AVFieldOrder ordinal, resolved the same per-field way as every other codecpar value"
  - "detail::classify_interlace/detail::field_order_name (analyzers.h/interlace.cpp) -- the tally-and-cross-check seam and the hand-written AVFieldOrder name table (no av_field_order_name exists in libav)"
affects: [video-analysis, doc03-coverage]

actuals:
  tokens: 12184
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "A check whose declared and observed values are drawn from two DIFFERENT subsets of the same enum (AVFieldOrder's TT/BB for per-frame parsers vs TB/BT for container muxers, confirmed against the real FFmpeg 8.1 source tree at vcpkg/buildtrees) can legitimately show a permanent, harmless evidence-only 'disagreement' for every genuinely interlaced file -- the compared VALUE still comes from the observed side, so status is unaffected; only evidence carries the always-true flag."
    - "detail:: seam signature takes the declared value alongside the scan data and returns the already-cross-checked classification (value + disagreement + cross-check-possible) in ONE struct, rather than splitting the tally and the cross-check across two functions -- keeps the disagreement computation itself untestable-in-isolation-from-classification, matching the plan's own action text."

key-files:
  created:
    - src/analyzers/video/interlace.cpp
    - tests/unit/test_video_interlace.cpp
    - docs/checks/video.interlace.md
  modified:
    - src/analyzers/video/analyzers.h
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "video.interlace deliberately does NOT skip skipped:no_parser the way video.gop.*/video.frame_types do -- a codec with no registered parser, or a registered parser that never sets field_order (mpeg4video_parser.c), still reports the declared value with evidence recording that no cross-check was possible (VIDEO-06-E1); only partial_scan skips."
  - "detail::classify_interlace takes the declared field_order_raw as a parameter and returns the classified value, the cross-check-possible flag, and the disagreement flag together, rather than computing disagreement separately in the emit function -- lets tests 3-6 drive the full cross-check behavior (not just the tally) through one seam."
  - "field_order_name's six spellings (unknown/progressive/top_field_first/bottom_field_first/top_coded_bottom_displayed/bottom_coded_top_displayed) are this project's own hand-written table (no av_field_order_name exists in libav), mirroring video.level's render_level_value precedent."

requirements-completed: [VIDEO-06]

coverage:
  - id: D1
    description: "video.interlace registered end to end (exact/none/string/fail per 04-CHECK-ROSTER.md): declared field order cross-checked against per-access-unit field order, observed value wins when available, mixed content named with exact rational proportions, no-cross-check case honestly reported rather than presented as verified"
    requirement: VIDEO-06
    verification:
      - kind: unit
        ref: "tests/unit/test_video_interlace.cpp -- 18 test cases covering all 8 behaviors from Task 1"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.interlace declared pair (tff/bff trigger, tff/tff_copy clean)"
        status: pass
    human_judgment: false

duration: ~55min
completed: 2026-09-13
status: complete
---

# Phase 04 Plan 10: video.interlace -- Declared Field Order Cross-Checked Against Per-Frame Flags Summary

**Interlace reported from what the frames actually are, not from what the container merely declares: `video.interlace` cross-checks `codecpar->field_order` against `ParserScanResult`'s own per-access-unit field order, with the real `video_ilace_mixed.mp4` fixture materializing a genuine `mixed` classification (not just a hand-built one).**

## Performance

- **Duration:** ~55 min
- **Completed:** 2026-09-13
- **Tasks:** 2/2
- **Files modified:** 12 (3 created, 9 modified)

## Accomplishments

- `video.interlace` registered (`src/core/checks.def`, exact/none/string/fail, per `04-CHECK-ROSTER.md`) and implemented (`src/analyzers/video/interlace.cpp`): the declared field order (`StreamInfo::field_order_raw`, a new codecpar-derived field) is cross-checked against the per-access-unit field order every registered parser records. The observed value wins whenever a cross-check was possible; a uniform observed value that disagrees with the declared one is still the compared value, with the disagreement recorded in evidence only. Non-uniform observed content is reported as `mixed`, with one exact `num`/`den` rational proportion per distinct observed field order in evidence -- never a float anywhere in the file (`grep -c 'double\|float'` reports 0).
- Empirically confirmed against the real linked FFmpeg 8.1 source tree (`vcpkg/buildtrees/ffmpeg/src/n8.1-.../fftools/ffmpeg_enc.c`, `libavformat/mov.c`, `libavcodec/mpegvideo_parser.c`, `libavcodec/h264_parser.c`) that the per-access-unit field order every registered parser sets is drawn only from `{UNKNOWN, PROGRESSIVE, TT, BB}`, while the container-level declaration (an MP4 `fiel` atom written by `fftools/ffmpeg_enc.c`'s own encoder-side convention) uses `TB`/`BT` for the identical real-world "top/bottom field first" property. This is why every genuinely interlaced fixture in this project's corpus shows `"disagreement": true` in evidence even though nothing is wrong -- a benign, documented consequence of two code paths using different subsets of the same six-value `AVFieldOrder` enum, never affecting the compared value or the pass/fail status.
- Flagged assumption A1 resolved empirically: `video_ilace_mixed.mp4`'s real per-frame variation (progressive for part of the stream, top-field-first for the rest, per `04-02-SUMMARY.md`'s own read-back table) DOES classify as `mixed` against the real linked parser -- no fallback to a hand-built-only test was needed for the real-fixture proof, though the hand-built `detail::classify_interlace` tests remain the load-bearing, reliable coverage per the plan's own instruction.
- `tests/unit/test_video_interlace.cpp`: 18 test cases -- 8 hand-built-array tests driving `detail::classify_interlace` directly (mixed, disagreement, agreement, no-cross-check, the empty-span/`has_parser==false` shape, the unknown-excluded-from-tally-but-repeat_pict-still-counted case, and the six-value `field_order_name` table), plus 9 real-fixture tests (TFF/BFF distinct spellings, the `fail` comparison, a genuinely progressive-throughout fixture, the no-parser fixture, a forced-partial-scan skip, the mixed fixture, and byte-identical-evidence determinism).
- `tests/integration/test_doc03_coverage.cpp`: `video.interlace`'s declared pair (trigger `video_ilace_tff.mp4`/`video_ilace_bff.mp4`, clean `video_ilace_tff.mp4`/`video_ilace_tff_copy.mp4`), registry total now 52. `tests/golden/list_checks_effective.txt` refreshed with the single new line.

## Task Commits

Each task was committed atomically:

1. **Task 1: video.interlace -- declared field order cross-checked against per-frame flags** - `9874d86` (feat)
2. **Task 2: Unit coverage and the DOC-03 pair** - `4cc5027` (test) -- this commit also reshaped `detail::classify_interlace`'s own signature to take the declared field order directly (see Decisions Made), a refinement made while writing Task 2's own tests, not a separate deviation.

**Plan metadata:** commit to follow this SUMMARY.

## Files Created/Modified

- `src/analyzers/video/interlace.cpp` (new) -- `video_interlace_analyzer()`, `run_video_interlace`, `emit_video_interlace`, `detail::field_order_name`, `detail::classify_interlace`
- `src/analyzers/video/analyzers.h` -- `video_interlace_analyzer()` declaration, `detail::InterlaceClassification`, `detail::classify_interlace` declaration
- `src/probe/demux_session.h`/`.cpp` -- `StreamInfo::field_order_raw` (the raw `codecpar->field_order` ordinal), resolved the same per-field way as every other codecpar value
- `src/probe/orchestrator.cpp` -- `video_interlace_analyzer()` registered in `all_analyzers()`
- `src/core/checks.def` -- `video.interlace` registered (`video`/`exact`/`none`/`string`/`fail`)
- `docs/checks/video.interlace.md` (new) -- What it measures / Why it matters / Accept-Tune-Silence
- `CMakeLists.txt` -- new source entry
- `tests/unit/test_video_interlace.cpp` (new) -- 18 test cases
- `tests/unit/CMakeLists.txt` -- test file registered
- `tests/integration/test_doc03_coverage.cpp` -- declared pair added, running total comment updated to 52
- `tests/golden/list_checks_effective.txt` -- refreshed (one new line, the check's own registration) via `UPDATE_GOLDENS=1`

## Decisions Made

See `key-decisions` in frontmatter. Additionally:

- **`detail::classify_interlace`'s signature was reshaped mid-plan** (during Task 2, before Task 2's own commit) from an earlier design that computed the tally/classification and the declared-value disagreement in two separate places, to one seam taking `(access_units, declared_field_order_raw)` and returning the fully cross-checked result (`value`, `cross_check_possible`, `disagreement`, `counts`, `total_observed`, `repeat_pict_count`) directly. This matches Task 1's own action text more precisely ("a `detail::` seam ... taking a span of `AccessUnitRecord` plus the declared field order, returning the classified value, the per-field-order counts and a flag recording whether a cross-check was possible") and lets every one of Tests 3-6 drive the real cross-check logic through one function, not just the tally. Re-verified against all five real fixtures byte-for-byte identical to the pre-refactor output before committing.

## Deviations from Plan

None beyond the mid-plan seam-signature refinement above (not a deviation from the plan's own instruction -- an adjustment made to match Task 1's action text more literally, discovered while writing Task 2's own tests).

## Mutation Testing (test_evidence_guard)

Fixture pairs confirmed distinct/identical by SHA-256 before any test was written (matching the values already verified at dispatch):

| File | SHA-256 (first 16 hex) |
|---|---|
| `video_ilace_tff.mp4` | `bc3740d9b7911dad` |
| `video_ilace_tff_copy.mp4` | `bc3740d9b7911dad` (identical -- the clean pair, by design) |
| `video_ilace_bff.mp4` | `e538b59b18209e21` |
| `video_ilace_mixed.mp4` | `e5c76ac60bc24471` |

Two mutations applied to `src/analyzers/video/interlace.cpp`, rebuilt, confirmed the expected tests FAIL, then restored via `cp` from a pre-mutation backup + `diff` verification, and rebuilt/re-ran the full suite to confirm 728/728 passed again after each:

| # | Mutation | Result | Tests that correctly failed |
|---|----------|--------|------------------------------|
| 1 | `classify_interlace` unconditionally returns `{no_cross_check, false, declared_field_order_raw}` -- simulating a check that reads ONLY the container's declared value, per this plan's own mandated `test_evidence_guard` mutation | 7 of 14 `unit.video_interlace` tests failed | Both TFF/BFF-distinct-spelling assertions, the `fail` comparison assertion's own value read, the progressive-fixture cross-check assertion, the mixed-fixture real-data assertion, and 3 of the hand-built classify_interlace tests (mixed, disagreement, agree-with-declared) |
| 2 | The `mixed` branch collapses to whichever observed field order is most common (a `std::max_element` over `counts`), instead of reporting `mixed` -- this plan's own explicit prohibition | 2 tests failed | The hand-built mixed-classification test and the real `video_ilace_mixed.mp4` fixture test |

**Noted finding, not a defect:** `integration.doc03_coverage`'s own `video.interlace` pair (`video_ilace_tff.mp4`/`video_ilace_bff.mp4`) still reports a real difference under mutation #1 (the container-only mutant), because the container ALSO declares genuinely distinct raw values for TFF vs BFF (`TB`=4 vs `BT`=5, per the `fftools/ffmpeg_enc.c` finding above) -- so a container-only reader would still (coincidentally) distinguish this specific pair. This is exactly why the standing `test_evidence_guard` requires the *unit-level* mutation check specifically: `doc03_coverage` alone cannot prove per-frame consumption for this check, but `tests/unit/test_video_interlace.cpp`'s own fixture-based tests (which assert the exact per-frame SPELLING, `top_field_first`/`bottom_field_first`, not merely "a difference exists") do fail under the same mutation, and that is the evidence this plan's own `must_haves` require.

## Issues Encountered

None beyond the FFmpeg-source investigation documented above (not a blocker -- it explained an initially-surprising `"disagreement": true` reading on every real interlace fixture, which turned out to be a benign, evidence-only artifact of two code paths using different subsets of the same enum, not a bug in this plan's own cross-check logic).

## Known Stubs

None. `video.interlace` emits a real, non-vacuous value or an honestly-labeled no-cross-check declared value on every real fixture tested; no hardcoded empty values, no placeholder text, no unwired data source.

## Threat Flags

None beyond the STRIDE register already authored in `04-10-PLAN.md`'s own `<threat_model>` (T-4-44 through T-4-47, T-4-SC) -- every `mitigate`-disposition threat is implemented as specified: the zero-denominator division is structurally unreachable (the `no_cross_check` branch never divides), proportions are exact `num`/`den` integer pairs (grep-verified zero `double`/`float` occurrences plus the byte-identical-evidence determinism test), the tally is a single bounded pass over an already-budget-accounted access-unit array, and evidence always carries whether a cross-check was possible.

## User Setup Required

None - no external service configuration required.

## Requirement Marking

Per this plan's `requirement_marking_guard`, `gsd_run query requirements.ready-ids ".planning/phases/04-video-analysis/04-10-PLAN.md" VIDEO-06` was run AFTER this SUMMARY.md was written. Gate result: `{"ready":["VIDEO-06"],"blocked":[],"total":1}` -- structurally ready.

`VIDEO-06`'s full text in `REQUIREMENTS.md:116` ("`video.interlace` cross-checks declared field order against per-frame parser flags and reports `mixed` with proportions when content is mixed") was read and every clause confirmed met, each proven by a non-vacuous, mutation-checked test:
- **Cross-checks declared field order against per-frame parser flags**: proven by the TFF/BFF-distinct-spelling tests and the mutation #1 result above (a container-only reader fails 7 of 14 unit tests).
- **Reports `mixed` with proportions when content is mixed**: proven by the real `video_ilace_mixed.mp4` fixture test and the hand-built cross-multiplication proportion-sums-to-one test, and by mutation #2 (collapsing mixed to the most-common value fails both).

**VIDEO-06 marked complete** via `requirements.mark-complete VIDEO-06` (the sole ID in `.ready[]`).

## Next Phase Readiness

`video.interlace` is registered, documented, DOC-03-covered, and mutation-tested. `./build/x64-linux/mediadiff list-checks | grep -c '^video\.interlace'` reports 1. Full suite: 728 tests, 0 failures, 6 designated skips (matching the dispatched golden-provenance expectation). No blockers for plan 04-11 (HDR).

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*

## Self-Check: PASSED

All 4 created files confirmed present on disk (`src/analyzers/video/interlace.cpp`, `tests/unit/test_video_interlace.cpp`, `docs/checks/video.interlace.md`, this SUMMARY.md). Both task commit hashes (`9874d86`, `4cc5027`) confirmed present in `git log --oneline --all`.
