---
phase: 04-video-analysis
plan: 08
subsystem: probe
tags: [ffmpeg, libavutil, colorimetry, yuvj, catch2]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-02's colorimetry/yuvj-signature fixture corpus (with the .mkv chroma substitution and the yuvj trio's actual read-back table) and 04-07's DemuxSession::StreamInfo/analyzers.h extension pattern"
provides:
  - "detail::fold_pix_fmt_range (src/analyzers/video/analyzers.h/color.cpp): the single seam folding the five deprecated yuvj* pixel-format names to their plain counterpart with color_range forced to full, read by both video.pix_fmt and video.color.range"
  - "video.pix_fmt/video.color.range/video.color.primaries/video.color.transfer/video.color.matrix/video.color.chroma_loc registered end to end with --explain docs and DOC-03 fixture pairs (registry count 47)"
  - "DemuxSession::StreamInfo extended with the six colorimetry NAME strings, resolved via av_get_pix_fmt_name/av_color_*_name/av_chroma_location_name"
  - "tests/integration/test_video_yuvj.cpp: the phase's own signature property (yuvj420p vs yuv420p-limited-range produces exactly ONE non-pass finding) as a COUNT over the whole report, not a lookup"
affects: [04-09-video-gop-frame-types, 04-10-video-interlace, 04-11-video-hdr, 04-12-video-dovi]

actuals:
  tokens: 18512
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A colour-value fold expressed as a pure string-to-string seam (detail::fold_pix_fmt_range) operating on already-resolved libav NAME strings, never on raw AVPixelFormat/AVColorRange ordinals -- keeps the fold itself free of any libav header while still being the single code path both consumers read, mirroring mp4.cpp's detail::compute_median_fragment_duration precedent for an exposed, directly unit-testable seam."
    - "unspecified-as-an-ordinary-value: no wildcard, no absent-treated-as-match anywhere in the four non-folded colorimetry checks -- the ordinary `exact` string comparator already produces the right answer, and the deliberate absence of a convenience special case is stated in both code comments and checks.def."
    - "Deliberate absence of a profile override as the mechanism for an unconditional guarantee (video.color.range carries no [check.profile_severity]/[check.profile_tolerance] in any profile, including transform) -- the absence itself is load-bearing and is documented as such at the registration site so a future editor does not add one 'for symmetry'."

key-files:
  created:
    - src/analyzers/video/color.cpp
    - docs/checks/video.pix_fmt.md
    - docs/checks/video.color.range.md
    - docs/checks/video.color.primaries.md
    - docs/checks/video.color.transfer.md
    - docs/checks/video.color.matrix.md
    - docs/checks/video.color.chroma_loc.md
    - tests/unit/test_video_color.cpp
    - tests/integration/test_video_yuvj.cpp
  modified:
    - src/analyzers/video/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_registry.cpp
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - scripts/gen_corpus.sh
    - tests/fixtures/GENERATOR_MANIFEST.json
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "The fold operates on resolved NAME STRINGS (e.g. \"yuvj420p\" -> \"yuv420p\"), not on raw AVPixelFormat/AVColorRange integer ordinals -- StreamInfo carries the six colorimetry fields as std::optional<std::string> (resolved once, in demux_session.cpp, via av_get_pix_fmt_name/av_color_*_name/av_chroma_location_name), so detail::fold_pix_fmt_range and the rest of color.cpp never need a libav header at all, matching this project's 'no libav header crosses src/analyzers/' boundary rule exactly."
  - "video.color.range's comment block in both checks.def and color.cpp states explicitly that the absence of any [check.profile_severity]/[check.profile_tolerance] override is DELIBERATE and load-bearing (VIDEO-07/doc 03 section 5), so a future editor does not add a transform demotion 'for symmetry' with video.pix_fmt's own transform=info override."
  - "[Rule 1 - Bug] The VIDEO-03 signature trio's fixtures (video_yuvj420p.mp4/video_yuv420p_pc.mp4/video_yuv420p_tv.mp4) were regenerated from a testsrc2 gradient source to a flat color=c=gray source. testsrc2's tv/pc mjpeg re-quantization produced a genuine ~8% file-size delta that tripped size.file/size.stream_bitrate/size.peak_bitrate under --profile sw-encoder, polluting the phase's own signature count. A flat source's JPEG DCT is dominated by the DC term regardless of the range-shifted level, making the encoded byte size effectively invariant (empirically: byte-identical between the yuvj and full-range-yuv420p members, ~2.4% residual between full and limited range -- under size.file's 3% warn threshold, though still enough to trip size.file's OWN tighter 0.5% override under remux/strict-bitexact specifically, an orthogonal interaction with a different check's tolerance, not a fold defect). tests/golden/CORPUS_DIGEST.txt regenerated for exactly these 3 fixtures (verified via diff: only their 3 lines plus the whole-listing summary line changed)."
  - "[Rule 1 - Bug] video.pix_fmt's DOC-03 trigger pair could not reuse the plan's own literal suggestion (video_base.mp4/video_yuvj420p.mp4): both fold to the identical \"yuv420p\" name (video_base.mp4 was never a yuvj* format), so that pair compares pass, not a trigger -- proven empirically against the real binary before being rejected. Substituted video_noparser.mkv (huffyuv, genuinely yuv422p) as the trigger candidate."
  - "[Rule 1 - Bug] tests/unit/test_registry.cpp's pre-existing 'find returns nullopt for an unregistered id' test used the literal string \"video.color.range\" as its unregistered-id example, written before this id existed. Now that it is a real registered check, that assertion fails by construction. Replaced with a synthetic \"video.unregistered_probe\" id, matching this codebase's own existing \"meta.unregistered_probe\" convention (tests/unit/test_report_model.cpp)."

patterns-established:
  - "String-level fold seam over resolved libav names (see tech-stack.patterns) -- the template a future value-normalization-before-comparison check (should one arise) can follow without needing a libav header in src/analyzers/."

requirements-completed: [VIDEO-03, VIDEO-07, VIDEO-08]

coverage:
  - id: D1
    description: "detail::fold_pix_fmt_range: the single seam folding all five deprecated yuvj* pixel-format names to their plain counterpart with color_range forced to full, leaving a non-yuvj format and an already-explicit range unchanged"
    requirement: VIDEO-03
    verification:
      - kind: unit
        ref: "tests/unit/test_video_color.cpp -- three TEST_CASEs covering all five fold-table entries, the unchanged-for-non-yuvj behavior, and the never-overwrites-an-explicit-range behavior"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.pix_fmt and video.color.range registered end to end, reading the same fold seam; video.color.range carries no profile override of any kind in any profile"
    requirement: VIDEO-07
    verification:
      - kind: unit
        ref: "tests/unit/test_video_color.cpp -- folded-value identity across the yuvj420p/yuv420p-pc pair, effective-range split between full and limited"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_yuvj.cpp -- video.color.range non-pass under all five profiles (sw-encoder/hw-encoder/remux/strict-bitexact/transform)"
        status: pass
    human_judgment: false
  - id: D3
    description: "video.color.primaries/transfer/matrix/chroma_loc registered, each a direct codecpar field with no fold; unspecified/unknown compares as its own value in both directions"
    requirement: VIDEO-08
    verification:
      - kind: unit
        ref: "tests/unit/test_video_color.cpp -- bt709-vs-bt601 and both directions of bt709-vs-unspecified via real compare_fingerprints() against the builtin registry; chroma_loc warn severity and the unspecified-vs-left VIDEO-07-E1 case"
        status: pass
    human_judgment: false
  - id: D4
    description: "The phase's own signature property -- yuvj420p vs yuv420p-limited-range produces EXACTLY ONE non-pass finding across the WHOLE report, and the full-range spelling pair produces ZERO -- asserted as a count, never a lookup"
    requirement: VIDEO-03
    verification:
      - kind: integration
        ref: "tests/integration/test_video_yuvj.cpp -- count_non_pass() over the entire findings array, plus video.pix_fmt present-and-pass (not absent)"
        status: pass
    human_judgment: false

duration: 30min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 08: The Range-Fold Seam and Six Colorimetry Checks Summary

**A single string-level fold seam (`detail::fold_pix_fmt_range`) makes `video.pix_fmt` and `video.color.range` provably agree on the yuvj-trap, backing all six `video.color.*`/`video.pix_fmt` checks and a signature integration test that counts findings instead of looking one up.**

## Performance

- **Duration:** 30 min
- **Started:** 2026-09-12T22:57:04+02:00 (base HEAD at dispatch)
- **Completed:** 2026-09-12T23:27:13+02:00
- **Tasks:** 3/3 completed
- **Files modified:** 23 (9 created, 14 modified)

## Accomplishments

- `detail::fold_pix_fmt_range` (`src/analyzers/video/analyzers.h`/`color.cpp`): the single seam folding all five deprecated `yuvj*` pixel-format NAMES (transcribed verbatim from this project's linked FFmpeg 8.1, `libavutil/pixfmt.h:85-283`) to their plain counterpart with `color_range` forced to full (`"pc"`), operating on already-resolved libav name strings so it never needs a libav header.
- `DemuxSession::StreamInfo` extended with six colorimetry NAME strings (`pix_fmt`, `color_range`, `color_primaries`, `color_transfer`, `color_matrix`, `chroma_location`), each resolved once in `demux_session.cpp` via its own `av_*_name` counterpart.
- `video.pix_fmt` (`transform="info"`) and `video.color.range` (deliberately no profile override in any profile) both read the same fold seam -- proven a single code path, not two independently-written branches.
- `video.color.primaries`/`.transfer`/`.matrix`/`.chroma_loc` registered as direct codecpar-field extractions with no fold and no special case for `unspecified`/`unknown` -- a change to or from that value is a real, reported difference in both directions.
- Six `docs/checks/<id>.md` files (`video.color.transfer.md` names PQ/HLG explicitly; `video.color.chroma_loc.md` explains its own `warn` severity).
- `tests/integration/test_video_yuvj.cpp`: the phase's signature property (VIDEO-03) as a `count_non_pass()` over the WHOLE report -- exactly one finding for the range-flip pair, exactly zero for the two-spellings-of-one-intent pair.
- Six new `tests/integration/test_doc03_coverage.cpp` declared pairs (registry check count 47, all verified against the real binary).

## Task Commits

Each task was committed atomically:

1. **Task 1: The range-fold seam, video.pix_fmt, and video.color.range** - `e4190f4` (feat)
2. **Task 2: primaries, transfer, matrix and chroma_loc, with unspecified as its own value** - `f431afa` (feat)
3. **Task 3: The exactly-one-finding signature test and six DOC-03 pairs** - `85af086` (feat)

## Files Created/Modified

- `src/analyzers/video/color.cpp` - `video_color_analyzer()`, `run_video_color`, the six `emit_*` functions, `detail::fold_pix_fmt_range`
- `src/analyzers/video/analyzers.h` - `video_color_analyzer()` accessor, `detail::ColorFold`/`fold_pix_fmt_range` declarations
- `src/probe/demux_session.h`/`.cpp` - six colorimetry name/raw-int field pairs on `StreamInfo`, resolved via `av_get_pix_fmt_name`/`av_color_range_name`/`av_color_primaries_name`/`av_color_transfer_name`/`av_color_space_name`/`av_chroma_location_name`
- `src/probe/orchestrator.cpp` - `video_color_analyzer()` registered in `all_analyzers()`
- `src/core/checks.def` - `video.pix_fmt`, `video.color.range`, `video.color.primaries`, `video.color.transfer`, `video.color.matrix`, `video.color.chroma_loc`
- `docs/checks/video.pix_fmt.md`, `video.color.range.md`, `video.color.primaries.md`, `video.color.transfer.md`, `video.color.matrix.md`, `video.color.chroma_loc.md`
- `CMakeLists.txt` - `src/analyzers/video/color.cpp` added to `libmediadiff`
- `tests/unit/test_video_color.cpp` - 21 new `TEST_CASE`s across both tasks (fold-table entries, per-fixture value assertions, real `compare_fingerprints()` calls against the builtin registry)
- `tests/unit/test_registry.cpp` - stale `"video.color.range"` unregistered-id example replaced (Deviation)
- `tests/unit/CMakeLists.txt` - `test_video_color.cpp` registered
- `tests/integration/test_video_yuvj.cpp` - the signature counting test (new)
- `tests/integration/CMakeLists.txt` - `test_video_yuvj.cpp` registered
- `tests/integration/test_doc03_coverage.cpp` - six new declared pairs, registry-count comment updated to forty-seven
- `scripts/gen_corpus.sh` - the yuvj signature trio's source switched from `testsrc2` to `color=c=gray` (Deviation)
- `tests/fixtures/GENERATOR_MANIFEST.json` - `generated_at` timestamp only (regenerated by every `gen_corpus.sh` run)
- `tests/golden/CORPUS_DIGEST.txt` - regenerated for exactly the 3 changed fixtures
- `tests/golden/list_checks_effective.txt` - six new check rows appended

## Decisions Made

See `key-decisions` in the frontmatter above for the full list. In short: the fold operates on resolved NAME STRINGS, never raw libav enum ordinals, keeping `src/analyzers/` free of any libav header; `video.color.range`'s absent profile override is documented as deliberate and load-bearing at both its registration site and its emission site; three Rule 1 bug fixes were required to make the plan's own signature test and DOC-03 pairs actually prove what they claim (see Deviations).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The yuvj signature trio's `testsrc2` source polluted the phase's own signature count with unrelated `size.*` findings**
- **Found during:** Task 3, first empirical run of the signature comparison (`mediadiff compare tests/fixtures/video_yuvj420p.mp4 tests/fixtures/video_yuv420p_tv.mp4 --profile sw-encoder --json`).
- **Issue:** `video_yuv420p_tv.mp4` was ~8% smaller than `video_yuvj420p.mp4` (223.9K vs 242.9K) -- mjpeg's limited-range re-quantization of the `testsrc2` gradient genuinely changes the compressed byte count, tripping `size.file`/`size.stream_bitrate`/`size.peak_bitrate` under `sw-encoder`'s own tolerance. This produced 5 non-pass findings instead of 1, exactly the false-positive class VIDEO-03 exists to prevent.
- **Fix:** Switched the three fixtures' `lavfi` source from `testsrc2=size=320x240:rate=25:duration=2` to `color=c=gray:size=320x240:rate=25:duration=2` in `scripts/gen_corpus.sh`. A flat/constant source's JPEG DCT is dominated by the DC term regardless of the range-shifted sample level, making the encoded size effectively invariant to the range flip. Regenerated the full corpus (`bash scripts/gen_corpus.sh`, 3.2s) and `tests/golden/CORPUS_DIGEST.txt` (verified via `diff` that only the 3 changed fixtures' lines plus the whole-listing summary line differ).
- **Files modified:** `scripts/gen_corpus.sh`, `tests/fixtures/GENERATOR_MANIFEST.json`, `tests/golden/CORPUS_DIGEST.txt`
- **Verification:** `mediadiff compare tests/fixtures/video_yuvj420p.mp4 tests/fixtures/video_yuv420p_tv.mp4 --profile sw-encoder --json` now yields exactly 1 non-pass finding (`video.color.range`); the mirror pair yields 0. `bash scripts/check_corpus.sh && bash scripts/assert_corpus_digest.sh` both pass (133 lines compared, the same 3 pre-existing Opus/summary exclusions).
- **Residual, recorded rather than hidden:** under `--profile remux`/`--profile strict-bitexact` specifically, `size.file`'s own separate, tighter `0.5%` override still catches the trio's residual ~2.4% JPEG size delta (an orthogonal interaction between the flat source's still-nonzero DC-level shift and a DIFFERENT check's own profile-specific tolerance -- not a defect in the fold or in `video.color.range`'s own guarantee). `tests/integration/test_video_yuvj.cpp`'s cross-profile test therefore asserts `video.color.range`'s own non-pass status across all five profiles (Task 1's already-proven property), while the exactly-one-finding COUNT is asserted only under `sw-encoder`, matching this task's own literal acceptance criteria.
- **Committed in:** `85af086` (Task 3's commit)

**2. [Rule 1 - Bug] `video.pix_fmt`'s suggested DOC-03 trigger pair does not actually trigger**
- **Found during:** Task 3, proving every declared pair empirically before committing (per this task's own instruction).
- **Issue:** The plan's own suggested trigger pair (`video_base.mp4`/`video_yuvj420p.mp4`) both fold to the identical `"yuv420p"` name -- `video_base.mp4` was never a `yuvj*` format to begin with, so the pair compares `pass`, not a trigger.
- **Fix:** Substituted `video_noparser.mkv` (huffyuv, genuinely `yuv422p`) as the trigger candidate against `video_base.mp4` (`yuv420p`) -- verified to report `video.pix_fmt: fail`.
- **Files modified:** `tests/integration/test_doc03_coverage.cpp`
- **Verification:** `ctest --test-dir build/x64-linux -R doc03_coverage` passes.
- **Committed in:** `85af086` (Task 3's commit)

**3. [Rule 1 - Bug] A pre-existing unit test used the now-real id `"video.color.range"` as its "unregistered id" example**
- **Found during:** Task 3, full-suite regression run after registering the six checks.
- **Issue:** `tests/unit/test_registry.cpp`'s `"find returns nullopt for an unregistered id"` test asserted `video.color.range` was NOT registered -- true when that test was written (Phase 2/3 era), false now that this plan registers it.
- **Fix:** Replaced with a synthetic, deliberately-never-real id, `"video.unregistered_probe"`, matching this codebase's own existing `"meta.unregistered_probe"` convention (`tests/unit/test_report_model.cpp`).
- **Files modified:** `tests/unit/test_registry.cpp`
- **Verification:** `ctest --test-dir build/x64-linux -R "unit.find returns nullopt"` passes.
- **Committed in:** `85af086` (Task 3's commit)

**4. [Scope-honest interpretation, not a Rule 1-3 auto-fix] Finding-message "practical effect" composition not implemented**
- **Found during:** Task 2, while implementing the four colorimetry emit functions per the plan's own action text ("Compose the message from the two values so a 601-to-709 matrix change names the global colour shift...").
- **Issue:** `Finding.message` is computed generically ("values match"/"values differ") by `src/compare/exact.cpp` for every `exact`-semantic check, from BOTH sides' values at comparison time -- a per-file `Measurement` emission (this plan's own scope) never sees the other side's value, and `src/compare/exact.cpp` is not in this plan's declared `files_modified`. Giving one check family a custom composed message would require changing that shared seam for every check, which is an architectural change this plan's frontmatter does not authorize.
- **Resolution:** The practical-effect language lives in each check's own `### Why it matters` doc section instead (verified via `mediadiff explain video.color.transfer` naming PQ/HLG explicitly, per this task's own acceptance criteria) -- doc 03's own requirement for naming practical effects is satisfied through the channel this plan's actual files can reach.
- **Files affected:** none (no code change; a scope clarification only).

---

**Total deviations:** 3 auto-fixed (all Rule 1 -- bugs where a fixture recipe or a pre-existing test assumption did not actually express the property under test), plus 1 scope-honest interpretation recorded above. **Impact on plan:** All three auto-fixes were necessary for the plan's own signature test and coverage gate to prove what they claim; none change any check's registered id/semantic/unit/value_kind/severity/tolerance. No scope creep.

## Issues Encountered

None beyond the deviations recorded above.

## Requirement Gate

Per this dispatch's `<requirement_marking_guard>`, `gsd_run query requirements.ready-ids` was run against this plan's declared `requirements` (`VIDEO-01`, `VIDEO-03`, `VIDEO-07`, `VIDEO-08`) AFTER this SUMMARY.md was written:

```json
{"ready": ["VIDEO-03", "VIDEO-07", "VIDEO-08"], "blocked": ["VIDEO-01"], "total": 4}
```

Matches the dispatch-computed expected outcome exactly. Before marking, each of `VIDEO-03`/`VIDEO-07`/`VIDEO-08`'s full text was checked clause by clause against `.planning/REQUIREMENTS.md`:
- **VIDEO-03** (the yuvj-trap fold, exactly-one-finding signature): satisfied -- `detail::fold_pix_fmt_range` is the single seam both checks read; `tests/integration/test_video_yuvj.cpp` proves the count empirically.
- **VIDEO-07** (five colorimetry fields, `video.color.range` fails in every profile including `transform`): satisfied -- all five checks registered; the no-override property verified across all five profiles.
- **VIDEO-08** (a change to `unspecified` is a real difference, both directions): satisfied -- no wildcard anywhere in the four non-folded checks; both directions tested against `video_color_bt709.mp4`/`video_color_unspec.mp4`.

All three were marked complete. `VIDEO-01` stays blocked -- also declared by plan `04-12`, which has not landed yet.

## Known Stubs

None.

## Threat Flags

None beyond this plan's own `<threat_model>` (T-4-33 through T-4-37, T-4-SC), every mitigation implemented as specified: T-4-33 (a colour-range flip suppressed by a profile override) is closed by `video.color.range`'s own absent-override registration, proven non-pass across all five profiles; T-4-34/T-4-35 (double-report / suppressed-range-delta) are closed by the single fold seam and the counting integration test; T-4-36 (unsanitised bytes in a composed message) is moot given Deviation 4 above (no composed message exists to sanitise; every rendered value already passes through libav's own fixed name tables, never raw untrusted bytes); T-4-37 (`unspecified` treated as matching any value) is closed by the deliberate absence of a special case, tested in both directions.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

All six `video.pix_fmt`/`video.color.*` checks are registered, documented, and DOC-03-covered (registry count 47). `StreamInfo`'s colorimetry NAME-string extraction pattern is available for 04-11 (HDR) to follow if it needs any adjacent codecpar field not yet exposed. No blockers for 04-09 (`video.gop.*`/`frame_types`) or 04-10 (`video.interlace`), neither of which depends on this plan's own outputs. 04-11/04-12 (HDR/DOVI) are unaffected by this plan's fixture-recipe change (the yuvj trio is exclusive to this plan and 04-02, confirmed by a repo-wide grep before regenerating it).

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED

All 9 created files confirmed present on disk; all 3 commit hashes (`e4190f4`, `f431afa`, `85af086`) confirmed present in `git log --oneline --all`.
