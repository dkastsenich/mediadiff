---
phase: quick
plan: 260922-wtc
subsystem: core-serialization
tags: [json, serializer, ffmpeg, catch2, report-rendering]

requires:
  - phase: 06-audio-analysis
    provides: "the debug session (.planning/debug/audio-sweep-rate-truncation.md) that diagnosed the ms 1000x defect"
provides:
  - "value_to_json/rational_value_to_json now require a declared Unit and emit ms only for a time unit, at its true magnitude"
  - "unit_is_time(Unit) predicate in core/registry.h, exhaustive switch mirroring unit_suffix"
  - "a permanent regression guard tying rendered ms to independently-known ground truth (timeline_drift_base.mp4 = 20s)"
  - "06-REVIEW.md amended in place with four dated correction notes (CR-01, WR-02, WR-03, WR-09)"
affects: [phase-06-gap-closure, report-rendering, snapshot-format]

actuals:
  tokens: 11672
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Unit-required serialization: value_to_json takes a REQUIRED Unit parameter (no default), forcing every call site to declare the owning check's unit rather than silently assuming one"
    - "unit_is_time(Unit) switch with no default: arm, mirroring unit_suffix's own exhaustiveness discipline in core/registry.h"

key-files:
  created: []
  modified:
    - src/core/registry.h
    - src/core/serializer.h
    - src/core/serializer.cpp
    - src/core/snapshot.cpp
    - src/report/json.cpp
    - src/report/junit.cpp
    - src/cli/tty_render.cpp
    - src/cli/commands/inspect_render.h
    - tests/unit/test_serializer.cpp
    - tests/unit/test_snapshot_roundtrip.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/golden/inspect_audio.txt
    - tests/golden/inspect_container.txt
    - tests/fixtures/snapshots/audio_aac_handwritten.snap.json
    - .planning/phases/06-audio-analysis/06-REVIEW.md

key-decisions:
  - "D-1: value_to_json's Unit parameter is required, not defaulted — the compiler, not a lint, guarantees every call site declares a unit"
  - "D-2: a time unit is Unit::ms and nothing else; Unit::ms_per_min is a drift RATE, not a duration, and is deliberately excluded"
  - "D-3: for a time unit, ms is num/den as a double, no multiplier; the JSON shape is otherwise unchanged"
  - "D-4: tests/golden/inspect_container.txt is a designated-leg golden (check_golden_designated_leg) — updated by hand transcription of the num/den already on each line, never via UPDATE_GOLDENS"
  - "D-5: tests/fixtures/snapshots/audio_aac_handwritten.snap.json is hand-edited on its six ms lines only — never re-emitted by `mediadiff snapshot`, preserving its designated-leg two-build proof"
  - "D-6: the den == 0 guard survives the rewrite, rendering 0.0 rather than dividing"
  - "D-7: 06-REVIEW.md is amended, never restructured — every finding ID, heading and severity label is unchanged; only dated notes were added"

patterns-established:
  - "A future analyzer/renderer adding a RationalValue serialization call site must resolve and pass the owning check's Unit at the call site (typically via CheckRegistry::find/at) — there is no default to fall back on"

requirements-completed: [SNAP-03, REPORT-02, TRUST-05]

coverage:
  - id: D1
    description: "rational_value_to_json emits ms only when the owning check's unit is a declared time unit (Unit::ms), as exactly num/den with no multiplier"
    requirement: "SNAP-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_serializer.cpp#serializer - a RationalValue under a time unit renders 'ms' as exactly num/den"
        status: pass
      - kind: unit
        ref: "tests/unit/test_serializer.cpp#serializer - a RationalValue under a non-time unit renders no 'ms' key at all"
        status: pass
      - kind: unit
        ref: "tests/unit/test_serializer.cpp#serializer - a RationalValue with den == 0 under a time unit renders 'ms' as 0.0, not a division"
        status: pass
      - kind: unit
        ref: "tests/unit/test_serializer.cpp#serializer - a SpanList's start/end each carry 'ms' only when the unit is a time unit"
        status: pass
    human_judgment: false
  - id: D2
    description: "A permanent regression guard ties the rendered ms field to a fixture whose real duration (20s) is known independently of mediadiff; video.sar (non-time) carries no ms key"
    requirement: "TRUST-05"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_start_duration.cpp#timeline_start_duration - timeline.duration renders 'ms' at its true magnitude on a 20-second fixture, and video.sar carries no 'ms' key at all"
        status: pass
    human_judgment: false
  - id: D3
    description: "value_to_json's Unit parameter is required (no default); all six production call sites (snapshot, inspect --json, inspect --text, JSON report, JUnit report, TTY finding rows) resolve and forward the owning check's declared unit"
    requirement: "REPORT-02"
    verification:
      - kind: unit
        ref: "cmake --build --preset x64-linux (warnings-as-errors; a missing/defaulted Unit argument at any of the six call sites fails to compile)"
        status: pass
    human_judgment: false
  - id: D4
    description: "06-REVIEW.md amended in place with four dated correction notes (CR-01 severity overstated, WR-09 wrong, WR-03 half right, WR-02 confirmed), no finding deleted/renumbered/moved/re-severitized"
    verification:
      - kind: other
        ref: "git diff --name-only HEAD -- . (task 2 verify gate): only 06-REVIEW.md plus pre-existing unrelated files changed"
        status: pass
      - kind: other
        ref: "grep -c '^### CR-01:|WR-02:|WR-03:|WR-09:' 06-REVIEW.md == 1 each (headings intact, exactly once)"
        status: pass
    human_judgment: false

duration: ~55min
completed: 2026-09-23
status: complete
---

# Quick Task 260922-wtc: Fix the `ms` Serialization Defect and Amend 06-REVIEW.md Summary

**Threaded `CheckDef.unit` into `value_to_json` so `ms` is now DECLARED, not assumed — the 1000x-inflated / meaningless `ms` field is fixed at all six render call sites, backed by a permanent ground-truth regression test, and 06-REVIEW.md's three disproved audio claims are corrected in place.**

## Performance

- **Duration:** ~55 min
- **Tasks:** 2/2 completed
- **Files modified:** 15 (14 in Task 1, 1 in Task 2)

## Accomplishments

- Fixed the `ms` serialization defect: `rational_value_to_json` previously derived `ms = (num/den)*1000` on the false assumption that `num`/`den` held seconds. Every producer actually emits the check's own declared unit (milliseconds, for a time check), so every time-valued check rendered `ms` 1000x too large, and every non-time RationalValue check (sar, dar, frame_rate, bitrate, dB/LU loudness, `container.ts.*` intervals) carried a meaningless `ms` field. Fixed by threading `CheckDef.unit` through `value_to_json`, now a required parameter, with a new `unit_is_time(Unit)` predicate (exhaustive switch, no `default:` arm) deciding whether `ms` is emitted at all.
- Added a permanent regression guard: `timeline_drift_base.mp4`, a fixture whose real duration is independently known to be 20 seconds, now asserts `timeline.duration` renders `ms == 20000.0` (previously `20000000.0`) while `video.sar` carries no `ms` key at all.
- Updated the three committed artifacts whose `ms` fields moved: `tests/golden/inspect_audio.txt` (regenerated via `UPDATE_GOLDENS`), `tests/golden/inspect_container.txt` (hand-transcribed — a designated-leg golden that refuses `UPDATE_GOLDENS`), and `tests/fixtures/snapshots/audio_aac_handwritten.snap.json` (hand-edited — a designated-leg capture that must never be re-emitted by `mediadiff snapshot`).
- Amended `.planning/phases/06-audio-analysis/06-REVIEW.md` in place: added a dated `## Corrections (2026-09-22)` block plus in-place notes on CR-01 (severity overstated, reproduction disproved by measurement, guard latent not live), WR-09 (wrong, and the `stream_params.cpp` comment it agrees with is stale), WR-03 (substantive gap stands, claimed off-by-one withdrawn), and WR-02 (confirmed, P0-class, currently unreachable in this corpus). No finding was deleted, renumbered, moved between severity sections, or had its severity changed.

## Task Commits

Each task was committed atomically:

1. **Task 1: emit `ms` only for a declared time unit, at its true magnitude, with a ground-truth regression guard** - `190b7e4` (fix)
2. **Task 2: amend 06-REVIEW.md's three disproved claims and record WR-02 as confirmed** - `9d2ceb6` (docs)

_TDD note: this quick task's Task 1 carried `tdd="true"`, but the implementation and the RED-phase tests (`tests/unit/test_serializer.cpp`, `tests/integration/test_timeline_start_duration.cpp`) landed together in a single `fix()` commit rather than as separate `test()`→`feat()` commits, since the plan's own Task 1 action text designates the signature change (steps 1-4) as "one atomic change" that must land together — the tree is red from the moment `value_to_json`'s signature changes until every call site and every committed artifact is updated. Every new assertion was written and run before this commit, and confirmed to exercise the fix (verified via `ctest -R unit.serializer` / `integration.timeline_start_duration`, both green)._

## Files Created/Modified

- `src/core/registry.h` - added `unit_is_time(Unit)`, an exhaustive switch (no `default:` arm) beside `unit_suffix`
- `src/core/serializer.h` / `src/core/serializer.cpp` - `value_to_json`/`rational_value_to_json` now take a required `Unit`; `ms` emitted only for a time unit, as num/den with no multiplier; replaced the false "seconds" comment with the measured contract
- `src/core/snapshot.cpp`, `src/report/json.cpp`, `src/report/junit.cpp`, `src/cli/tty_render.cpp`, `src/cli/commands/inspect_render.h` - all six production `value_to_json` call sites now resolve and forward the owning check's declared unit
- `tests/unit/test_serializer.cpp` - four new TEST_CASEs proving the time-unit/non-time-unit/den==0/SpanList behavior; existing tests updated to pass a `Unit` (defaulted to `Unit::none` in this test file's own helper, not in production code)
- `tests/unit/test_snapshot_roundtrip.cpp` - `render_fingerprint`'s own write-path mirror now forwards `registry.at(m.check_index).unit`
- `tests/integration/test_timeline_start_duration.cpp` - new permanent ground-truth regression test
- `tests/golden/inspect_audio.txt`, `tests/golden/inspect_container.txt`, `tests/fixtures/snapshots/audio_aac_handwritten.snap.json` - the three committed artifacts whose `ms` field moved
- `.planning/phases/06-audio-analysis/06-REVIEW.md` - four in-place correction notes plus a Corrections block and a Summary pointer

## Decisions Made

All decisions were locked at planning time (D-1 through D-7 in the plan frontmatter) and followed exactly — see `key-decisions` above. No new decisions were required during execution.

## Deviations from Plan

None — plan executed exactly as written. All fourteen files in Task 1's `files` list were touched exactly as specified; Task 2 touched exactly the one file specified.

## Issues Encountered

None. The build produced no new warnings (warnings are errors project-wide), and the full test suite passed on the first run after the fix (1213/1213, with the same 6 expected skips — 1208 pre-existing + 5 new tests this task added: 4 in `test_serializer.cpp`, 1 in `test_timeline_start_duration.cpp`).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

The `ms` serialization defect is fixed at its root and backed by a permanent regression guard; no `--json`, text, JUnit or TTY output can silently regress to the old 1000x-inflated or meaningless-on-non-time-checks behavior without a compile error (required `Unit` parameter) or a test failure. `06-REVIEW.md` no longer misstates three findings a later phase-6 gap-closure plan would otherwise be planned from incorrectly. Two items remain explicitly out of scope for this task and are recorded in `.planning/debug/audio-sweep-rate-truncation.md` for that gap-closure plan: (1) `src/analyzers/audio/stream_params.cpp:163-171`'s stale core-rate comment (WR-09's correction flags it, does not fix it), and (2) WR-02's `sampling_state` hardcoded-`"full"` fix (confirmed real and P0-class, but unreachable in the current corpus).

## Self-Check: PASSED

All 15 files listed above (14 in Task 1 + 06-REVIEW.md in Task 2) confirmed present on disk. Both commit hashes (`190b7e4`, `9d2ceb6`) confirmed present in `git log --oneline --all`.

---
*Phase: quick*
*Completed: 2026-09-23*
