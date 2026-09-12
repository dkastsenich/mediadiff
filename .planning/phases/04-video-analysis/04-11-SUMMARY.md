---
phase: 04-video-analysis
plan: 11
subsystem: video-analysis
tags: [ffmpeg, hdr, mdcv, cll, coded_side_data, rational-arithmetic, quantisation]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "plan 04-04's read-back-verified HDR fixture corpus (video_hdr_a/_copy/_lum_b/_prim_b/_cll_b/_none.mp4); 04-CONTEXT.md's D-08 (precedence-seam-now, wire-one-source) and D-09 (MDCV/CLL are container-level boxes, codec-independent)"
provides:
  - "video.hdr.mdcv/.luminance/.primaries and video.hdr.cll/.max/.avg -- six checks reading codecpar->coded_side_data ONLY, with the D-08 precedence seam's second arm (first-frame side data) declared and reachable for Phase 7"
  - "StreamInfo::mdcv_*/cll_* fields (demux_session.h/.cpp) -- the raw AVMasteringDisplayMetadata/AVContentLightMetadata payload, resolved once at the libav-header boundary, never crossing into src/analyzers/"
  - "detail::quantize_chromaticity/detail::could_carry_frame_level_hdr (analyzers.h/hdr.cpp) -- the 0.0002-grid integer quantiser (fixed away-from-zero midpoint rounding) and the codec-capability decision, both directly unit-testable seams"
affects: [04-12-video-dovi-coherence]

actuals:
  tokens: 19780
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A precedence seam with only ONE arm wired (D-08): resolve_hdr_source's three-way classification (stream / requires_decode / not_applicable) is computed ONCE per StreamInfo field-pair and shared verbatim by two otherwise-unrelated check families (mastering-display, content-light) via a single parameterized function, rather than each family re-deriving its own could/could-not-carry logic."
    - "A tolerance too wide for the registry's one-scalar-per-check comparator (doc 03's 0.0002 absolute tolerance over eight chromaticities) expressed as fixed-epsilon INTEGER quantisation to a named grid, compared `exact` over the resulting canonical string -- never a literal `tol` check, never a float. Midpoint rounding is a documented, symmetric (away-from-zero) rule so a boundary value is deterministic on every platform."
    - "Value-bearing sibling checks split from a `presence` check (per src/compare/presence.cpp's own documented rule) must NEVER emit Absent{} on 'nothing to measure' -- src/compare/tol.cpp turns Absent into Status::error. They emit a named skip instead; this plan reuses SkipReason::requires_decode for both the could-carry-but-empty and could-not-carry-at-all sub-cases (a known imprecision, see Decisions Made), while the presence check itself DOES distinguish them via VIDEO-09-E1."

key-files:
  created:
    - src/analyzers/video/hdr.cpp
    - tests/unit/test_video_hdr.cpp
    - docs/checks/video.hdr.mdcv.md
    - docs/checks/video.hdr.mdcv.luminance.md
    - docs/checks/video.hdr.mdcv.primaries.md
    - docs/checks/video.hdr.cll.md
    - docs/checks/video.hdr.cll.max.md
    - docs/checks/video.hdr.cll.avg.md
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
  - "StreamInfo extended for coded_side_data (demux_session.h/.cpp), NOT in the plan's original files_modified list -- required by the dispatch's own repo-state-since-plan-was-written note (item 3): keep libav types out of src/analyzers/. AVMasteringDisplayMetadata/AVContentLightMetadata are read via av_packet_side_data_get ONLY inside demux_session.cpp; hdr.cpp never sees a libav pointer."
  - "The four value-bearing checks (mdcv.luminance/.primaries, cll.max/.avg) all skip SkipReason::requires_decode for BOTH sub-cases of 'nothing to measure' (could-carry-but-empty AND could-not-carry-at-all), rather than adding a new SkipReason enum value that model.h/checks.def modification would require but which is outside this plan's own files_modified scope. Evidence still carries `could_carry_frame_level` (bool) and `codec` so a reader is never misled about whether decoding could ever help -- the PRESENCE checks (video.hdr.mdcv/video.hdr.cll) are what fully implement VIDEO-09-E1's could/could-not distinction, per the roster's own presence/value split rationale (a presence check owns presence; a value check owns the value)."
  - "video.hdr.mdcv's presence value is the literal string \"present\" (never a richer descriptive string), matching container.mkv.duration_element's own established precedent -- compare/presence.cpp never inspects the value, only Absent-vs-not."
  - "Chromaticity quantisation rounds an exact grid midpoint AWAY FROM ZERO (not to-even, not toward-zero) -- a fixed, symmetric, documented rule (docs/checks/video.hdr.mdcv.primaries.md's own Tune section) chosen because it needs no floating-point representation reasoning and is trivially reproducible in pure integer arithmetic."

requirements-completed: []

coverage:
  - id: D1
    description: "video.hdr.mdcv/.luminance/.primaries registered end to end: stream-level extraction from codecpar->coded_side_data, presence/absence distinguishing could-carry (skipped:requires_decode) from could-not-carry (ordinary Absent), luminance under 5% tolerance, primaries as an eight-chromaticity canonical string quantised to the 0.0002 grid via integer arithmetic"
    requirement: VIDEO-09
    verification:
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp -- 13 test cases covering the quantiser, the could-carry decision, real-fixture presence/value/skip behavior, dimension isolation, and cross-family independence"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.hdr.mdcv/.luminance/.primaries declared pairs (trigger against video_hdr_none.mp4/video_hdr_lum_b.mp4/video_hdr_prim_b.mp4; clean against video_hdr_a_copy.mp4)"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.hdr.cll/.max/.avg registered end to end, reusing Task 1's extraction seam and codec-capability decision verbatim: MaxCLL/MaxFALL independently attributable, each under 5% tolerance"
    requirement: VIDEO-09
    verification:
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp -- video_hdr_cll_b.mp4 cross-family-independence test and the real-fixture presence/value tests"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.hdr.cll/.max/.avg declared pairs"
        status: pass
    human_judgment: false

duration: ~40min
completed: 2026-09-13
status: complete
---

# Phase 04 Plan 11: HDR10 Mastering-Display and Content-Light Metadata Summary

**video.hdr.mdcv/.luminance/.primaries and video.hdr.cll/.max/.avg read from `codecpar->coded_side_data` with no decode pass, chromaticities compared exact over an integer-quantised 0.0002-grid canonical string, and the D-08 precedence seam's second arm declared for Phase 7 without touching this plan's evidence shape.**

## Performance

- **Duration:** ~40 min
- **Completed:** 2026-09-13
- **Tasks:** 3/3
- **Files modified:** 17 (8 created, 9 modified)

## Accomplishments

- `video.hdr.mdcv` (`presence`), `video.hdr.mdcv.luminance` (`tol` 5%, rational), and `video.hdr.mdcv.primaries` (`exact`, quantised canonical string) registered per `04-CHECK-ROSTER.md`, reading `AVMasteringDisplayMetadata` from `codecpar->coded_side_data` (`AV_PKT_DATA_MASTERING_DISPLAY_METADATA`) via new `StreamInfo::mdcv_*` fields.
- `video.hdr.cll` (`presence`), `video.hdr.cll.max`/`video.hdr.cll.avg` (`tol` 5%, int64) registered, reading `AVContentLightMetadata` from the same `coded_side_data` array (`AV_PKT_DATA_CONTENT_LIGHT_LEVEL`), reusing Task 1's `resolve_hdr_source`/`could_carry_frame_level_hdr` seam verbatim -- no second codec-capability table.
- The D-08 precedence seam is fully declared: `resolve_hdr_source` returns a three-way classification (`stream` / `requires_decode` / `not_applicable`), with the `requires_decode` branch reachable and named for Phase 7's first-frame-side-data arm, and the `source` evidence field already emitting `"stream"` today.
- `detail::quantize_chromaticity` quantises a raw `AVRational` chromaticity to the nearest 1/5000th (the 0.0002 grid) via checked integer arithmetic, rounding an exact midpoint AWAY FROM ZERO -- no floating point anywhere in the path (`grep -c 'double\|float' src/analyzers/video/hdr.cpp` reports 0), no decode call (`grep -c 'avcodec_send_packet\|avcodec_receive_frame\|avcodec_open2'` reports 0).
- Dimension isolation empirically proven: comparing `video_hdr_a.mp4` against `video_hdr_lum_b.mp4`/`video_hdr_prim_b.mp4`/`video_hdr_cll_b.mp4` each fires ONLY its own check(s) (luminance-only, primaries-only, both content-light checks), with the unrelated family's checks reporting `pass` in every case -- cross-family independence.
- `tests/unit/test_video_hdr.cpp`: 13 test cases -- 6 hand-built-table tests driving `detail::quantize_chromaticity`/`detail::could_carry_frame_level_hdr` directly (exact grid multiples, the fixed midpoint rule, non-positive-denominator and overflow rejection, the closed hevc/av1/mpeg4/mpeg2video table), plus 7 real-fixture tests (all-six-present with read-back-verified values, ordinary-absence vs `requires_decode`-skip, three dimension-isolation comparisons, the byte-identical clean pair, and the presence-checks non-pass comparison).
- `tests/integration/test_doc03_coverage.cpp`: six declared pairs added, registry total now 58. `tests/golden/list_checks_effective.txt` refreshed (6 new lines across two `UPDATE_GOLDENS=1` runs, one per task).

## Task Commits

Each task was committed atomically:

1. **Task 1: The precedence seam and the mastering-display family** - `124cf5a` (feat)
2. **Task 2: The content-light family** - `9d556a2` (feat)
3. **Task 3: Unit coverage and six DOC-03 pairs** - `e5be55f` (test)

**Plan metadata:** commit to follow this SUMMARY.

## Files Created/Modified

- `src/analyzers/video/hdr.cpp` (new) -- `video_hdr_analyzer()`, the six `emit_*` functions, `detail::quantize_chromaticity`, `detail::could_carry_frame_level_hdr`, the shared `resolve_hdr_source`/`HdrSourceKind` seam
- `src/analyzers/video/analyzers.h` -- `video_hdr_analyzer()` declaration, `detail::quantize_chromaticity`/`detail::could_carry_frame_level_hdr` declarations
- `src/probe/demux_session.h`/`.cpp` -- `StreamInfo::mdcv_*`/`cll_*` fields, resolved from `codecpar->coded_side_data` at the libav-header boundary (T-4-48 short-payload guard)
- `src/probe/orchestrator.cpp` -- `video_hdr_analyzer()` registered in `all_analyzers()`
- `src/core/checks.def` -- six `video.hdr.*` ids registered per the roster
- `docs/checks/video.hdr.{mdcv,mdcv.luminance,mdcv.primaries,cll,cll.max,cll.avg}.md` (new) -- What it measures / Why it matters / Accept-Tune-Silence for each
- `CMakeLists.txt` -- new source entry
- `tests/unit/test_video_hdr.cpp` (new) -- 13 test cases
- `tests/unit/CMakeLists.txt` -- test file registered
- `tests/integration/test_doc03_coverage.cpp` -- six declared pairs added, running total comment updated to 58
- `tests/golden/list_checks_effective.txt` -- refreshed via `UPDATE_GOLDENS=1` (twice, once per registering task)

## Decisions Made

See `key-decisions` in frontmatter. Additionally:

- **Atomic-commit split after a combined implementation pass**, mirroring 04-04-SUMMARY.md's own precedent: `src/analyzers/video/hdr.cpp`, `src/probe/demux_session.h`/`.cpp`, and `src/core/checks.def`/`docs/checks/` were authored in full (both HDR families) in one editing pass, then temporarily rolled back to a Task-1-only slice (mdcv family only), rebuilt, verified against the real fixtures, and committed as Task 1's own commit -- before the content-light (`cll_*`) code, fields, checks.def entries, and docs were restored, rebuilt, re-verified, and committed as Task 2. No functional impact; both commits reflect exactly the plan's own task boundaries, and each intermediate state built and ran cleanly on its own.
- **`grep -c 'double\|float'` acceptance criterion caught an unintended substring match**: local variable names `scaled_doubled`/`den_doubled` (an intermediate rounding step in `quantize_chromaticity`) contain the literal substring "double", tripping the plan's own acceptance grep even though no floating-point type was ever used. Renamed to `scaled_twice`/`den_twice` -- a Rule 1 fix (the grep is a real acceptance criterion the code must satisfy, and the rename has zero behavioral effect).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Auto-add missing critical functionality] Extended `StreamInfo` for `coded_side_data`, outside the plan's own `files_modified` list**
- **Found during:** Task 1
- **Issue:** The plan's frontmatter `files_modified` does not list `src/probe/demux_session.h`/`.cpp`, but the dispatch's own `<repo_state_since_this_plan_was_written>` explicitly requires extending `StreamInfo` the same way prior plans did, to keep libav types out of `src/analyzers/`. Reading `AVMasteringDisplayMetadata`/`AVContentLightMetadata` directly inside `hdr.cpp` would have violated this project's own established per-file libav-header boundary (every other `video/*.cpp` file reads only plain `StreamInfo` fields).
- **Fix:** Added `mdcv_*`/`cll_*` fields to `StreamInfo` and the corresponding extraction code (with the T-4-48 short-payload guard) to `DemuxSession::stream_info()`, following the identical per-field pattern every prior `04-*-PLAN.md` used (color, field_order, etc.).
- **Files modified:** `src/probe/demux_session.h`, `src/probe/demux_session.cpp`
- **Verification:** `grep -c 'AVPacketSideData\|AVMasteringDisplayMetadata\|AVContentLightMetadata' src/analyzers/video/hdr.cpp` reports 0; the six checks render correctly end to end.
- **Committed in:** `124cf5a` (mdcv fields, Task 1), `9d556a2` (cll fields, Task 2)

---

**Total deviations:** 1 auto-fixed (1 missing critical functionality, split across two task commits as the fields it needed were introduced).
**Impact on plan:** Necessary to honor this project's own architecture boundary; no scope creep beyond what the dispatch instructions explicitly required.

## Issues Encountered

None beyond the acceptance-criterion substring match documented above (not a design defect -- a variable-naming collision with the grep pattern itself).

## Record: what is wired versus seam-only (VIDEO-09's precedence clause)

Per the dispatch's own instruction to be explicit and separate:

- **WIRED and exercised by a test:** `codecpar->coded_side_data` (stream-level extraction). Every real fixture in this plan's test suite exercises this arm; `video_hdr_a.mp4`'s `source` evidence field reads `"stream"`, proven by `tests/unit/test_video_hdr.cpp`'s "emits all six checks present" test.
- **SEAM ONLY, no live implementation:** first-frame side data (the frame-level precedence arm VIDEO-09's text names second). `resolve_hdr_source`'s `HdrSourceKind::requires_decode` branch is the named, reachable declaration of this arm -- it is reached whenever the codec `could_carry_frame_level_hdr` (hevc/av1) but the stream-level source was empty. No decode pass exists in this phase (D-08/D-09), so this branch never actually decodes a frame; it only recognizes the situation and reports `skipped:requires_decode` honestly. **This is not exercised by any real fixture** -- every fixture in this phase's corpus is a plain `mpeg4` encode (04-04-SUMMARY.md), which `could_carry_frame_level_hdr` correctly returns `false` for, so real fixtures only ever reach the `not_applicable` branch, never `requires_decode`. `tests/unit/test_video_hdr.cpp`'s Test 2 (`could_carry_frame_level_hdr` returns true for hevc/av1) is the only coverage this arm's DECISION LOGIC has; the branch's actual reachability with a real HEVC/AV1 fixture carrying no stream-level HDR data is unverified here, by design (no such fixture exists in this phase's corpus).
- **How "which source was used" is recorded, and what it records when the wired source is absent:** the `source` evidence field is a string literal `"stream"`, written ONLY on the branch where `codecpar->coded_side_data` actually produced a value. When the wired source is absent, the check never fabricates a `source` value -- it either reports ordinary `Absent{}` with `codec`/`could_carry_frame_level` in evidence (could-not-carry) or `skipped:requires_decode` with the same evidence fields (could-carry-but-empty). At no point does an unwired source report itself as though it succeeded; mutation testing (see below) confirms this recording is load-bearing, not decorative.

## Mutation Testing (test_evidence_guard)

Fixture pairs confirmed distinct/identical by SHA-256 before dispatch (values pre-verified in the dispatch's own `<test_evidence_guard>` block; re-confirmed here by the trigger/clean behavior actually observed):

| Comparison | Result |
|---|---|
| `video_hdr_a.mp4` vs `video_hdr_none.mp4` | `video.hdr.mdcv`/`video.hdr.cll` both `fail` (genuine trigger) |
| `video_hdr_a.mp4` vs `video_hdr_lum_b.mp4` | `video.hdr.mdcv.luminance` `fail`, `video.hdr.mdcv.primaries` `pass` (isolated) |
| `video_hdr_a.mp4` vs `video_hdr_prim_b.mp4` | `video.hdr.mdcv.primaries` `fail`, `video.hdr.mdcv.luminance` `pass` (isolated) |
| `video_hdr_a.mp4` vs `video_hdr_cll_b.mp4` | `video.hdr.cll.max`/`video.hdr.cll.avg` both `fail`, mdcv family `pass` (cross-family independence) |
| `video_hdr_a.mp4` vs `video_hdr_a_copy.mp4` | all six `pass` (clean pair) |

Two mutations applied to `src/analyzers/video/hdr.cpp`, rebuilt, confirmed the expected tests FAIL, then restored via `cp` from a pre-mutation backup + `git diff` verification (zero diff against HEAD), rebuilt, and the full suite re-run (741/741 passed) after each:

| # | Mutation | Result | Tests that correctly failed |
|---|----------|--------|------------------------------|
| 1 | `quantize_chromaticity`'s round-half-up term skipped (floor-divides instead of quantising to the nearest grid point) -- this plan's own mandated "a mutant that skips quantisation should fail a boundary test" | 2 of 13 `unit.video_hdr` tests failed | The hand-built exact-midpoint-rounding test (`quantize_chromaticity(1, 10000)` no longer returns 1), and the real `video_hdr_a.mp4` primaries value test (its white-point x, which lands exactly on a grid midpoint, renders `1563` instead of `1564`) |
| 2 | `resolve_hdr_source` ignores `stream_present` entirely (simulating a precedence seam that never records which source fired) -- this plan's own mandated "the precedence seam's own 'which source was used' recording" mutation | 6 of 13 `unit.video_hdr` tests failed | Every test asserting a `present`/`stream`-sourced value on a real fixture with genuine stream-level data (the all-six-present test, the clean-copy-passes test, and all three dimension-isolation comparisons, plus the ordinary-absence comparison test) |

Both mutations produced the exact failure classes this plan's own `test_evidence_guard` requires evidence for; no test passed vacuously under either mutation.

## Known Stubs

None. All six checks emit a real, non-vacuous value or an honestly-labeled skip/absence on every real fixture tested; no hardcoded empty values, no placeholder text.

The one deliberately-unwired path (first-frame side data, Phase 7's arm) is NOT a stub in the sense this section warns against: it never fabricates a plausible-looking value. It reports `skipped:requires_decode` -- an explicit, named, evidence-carrying non-answer -- which is exactly the "unwired source must report itself as unwired/absent, never as a silent empty success" contract the dispatch instructions require.

## Threat Flags

None beyond the STRIDE register already authored in `04-11-PLAN.md`'s own `<threat_model>` (T-4-48 through T-4-52, T-4-SC) -- every `mitigate`-disposition threat is implemented as specified: a short `coded_side_data` payload is never read past its end (`mdcv_short_payload`/`cll_short_payload` recorded, never dereferenced), every chromaticity/luminance rational is validated for a positive denominator before quantisation or comparison (`quantize_chromaticity`'s own T-4-49 guard; `mdcv_max_luminance_den > 0` gating `video.hdr.mdcv.luminance`'s `has_value`), quantisation and tolerance arithmetic route through `core/rational.h`'s checked helpers with no code path silently wrapping on overflow (T-4-50), the fixture pairs include a genuine present-versus-absent case with unit tests asserting against `04-04-SUMMARY.md`'s read-back-verified values rather than the analyzer's own output (T-4-51), and evidence always carries the codec plus whether it could carry frame-level metadata so an absence is never unaccountable (T-4-52).

## User Setup Required

None - no external service configuration required.

## Requirement Marking

Per this plan's `requirement_marking_guard`, `gsd_run query requirements.ready-ids ".planning/phases/04-video-analysis/04-11-PLAN.md" VIDEO-09` was run AFTER this SUMMARY.md was written -- see the tool-call output recorded immediately after this SUMMARY was committed. VIDEO-09 is ALSO declared by 04-12-PLAN.md (unfinished, covers `video.hdr.dovi`/`.config`/`video.hdr.coherence`), so the gate is expected to report it `blocked`, and **nothing was marked complete**.

## Next Phase Readiness

Six HDR checks (mastering-display and content-light) are registered, documented, DOC-03-covered, and mutation-tested. `./build/x64-linux/mediadiff list-checks | grep -c '^video\.hdr\.'` reports 6. Full suite: 741 tests, 0 failures, 6 designated skips. Plan 04-12 (DOVI configuration record + `video.hdr.coherence`) can reuse this plan's `resolve_hdr_source`/`could_carry_frame_level_hdr` seam if its own extraction shares the same could/could-not-carry shape -- worth checking before writing a third copy.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*

## Self-Check: PASSED

All 8 created files confirmed present on disk (`src/analyzers/video/hdr.cpp`, `tests/unit/test_video_hdr.cpp`,
and the six `docs/checks/video.hdr.*.md` files). All three task commit hashes (`124cf5a`, `9d556a2`, `e5be55f`)
confirmed present in `git log --oneline --all`.
