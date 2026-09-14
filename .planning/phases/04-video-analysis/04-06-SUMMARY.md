---
phase: 04-video-analysis
plan: 06
subsystem: probe
tags: [ffmpeg, libavcodec, codecpar, video-identity, catch2]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-01's Pass::parser_scan fusion and video/analyzers.h conventions; 04-02's video_base/video_codec_mpeg2/video_prof_a/video_prof_b/video_res_640/video_frames_50/video_base_copy fixtures; 04-05's video_h264_closed.h264 (used here as the VIDEO-02-E1 evidence-disagreement proof); 04-CHECK-ROSTER.md's approved five-id declaration"
provides:
  - "src/analyzers/video/stream_params.cpp: video.codec/profile/level/resolution/frame_count, the five per-video-stream identity checks, registered end to end with --explain docs and DOC-03 fixture pairs"
  - "DemuxSession::StreamInfo extended with profile/profile_name/level/width/height/codec_id_raw/codec_tag_raw/declared_frame_count -- the codecpar fields every later video.* analyzer in this phase will also need"
  - "The first shipped check (video.resolution) to carry transform_affected = true, exercising src/compare/exact.cpp's own transform_active branch for the first time in production"
  - "detail::render_profile_value/render_level_value, exposed in src/analyzers/video/analyzers.h for direct testing of VIDEO-01-E2 and the level table's covered/uncovered split"
affects: [04-07-video-sar-framerate, 04-08-video-color, 04-09-video-gop-frame-types, 04-10-video-interlace, 04-11-video-hdr, 04-12-video-dovi]

actuals:
  tokens: 13477
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "DemuxSession::StreamInfo as the ONE place codecpar fields cross the opaque-AVFormatContext boundary into src/analyzers/ -- profile_name is resolved via avcodec_profile_name INSIDE demux_session.cpp itself (never a raw AVCodecID reaching an analyzer), matching this file's own top-of-file 'no libav header crosses this file's public surface' rule exactly."
    - "A rendering rule exposed as a pure detail:: function (render_profile_value/render_level_value) specifically so a two-different-unresolved-integers-compare-as-different property (VIDEO-01-E2) can be proven without needing a real fixture whose profile happens to be unresolvable in two different ways."

key-files:
  created:
    - src/analyzers/video/stream_params.cpp
    - docs/checks/video.codec.md
    - docs/checks/video.profile.md
    - docs/checks/video.level.md
    - docs/checks/video.resolution.md
    - docs/checks/video.frame_count.md
    - tests/unit/test_video_stream_params.cpp
  modified:
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/analyzers/video/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "DemuxSession::StreamInfo extended with the codecpar fields these five checks need (profile, profile_name, level, width, height, codec_id_raw, codec_tag_raw, declared_frame_count) -- a Rule 3 blocking-issue fix. The plan's own files_modified list did not name probe/demux_session.{h,cpp}, but StreamInfo previously exposed only media_type/codec_name/is_timecode/is_caption; there was no other sanctioned path for src/analyzers/video/ to reach these fields without violating this project's own 'no libav header crosses DemuxSession's public surface' boundary."
  - "HEVC tier is NOT folded into video.level's rendered string, contradicting claude_docs/03-video-analysis.md section 2's literal wording ('HEVC 123->4.1 main-tier flagging'). Verified empirically against this project's own pinned FFmpeg 8.1 source (libavcodec/hevc/ps.c/ps.h: general_tier_flag lives only in the decoder-private HEVCSPS struct) and against ffprobe's own source (no tier field anywhere in its own output-writer code) that tier is not exposed by AVCodecParameters, AVCodecParserContext, or any other public libav surface reachable without a decode pass. This phase has no decode pass (04-CONTEXT.md D-08/D-09). Documented in full in docs/checks/video.level.md; deferred to whichever future phase adds a decode pass or a dedicated bitstream-level tier extraction. The approved roster's id/semantic/unit/value_kind/severity/tolerance for video.level are unaffected -- only the internal rendering table's completeness changed, exactly as flagged assumption A1 anticipated ('keep it small, cover only the codecs the v1 matrix actually contains... render an unrecognised level as its raw integer rather than guessing')."
  - "video.resolution's evidence omits coded_width/coded_height and the odd-dimension/chroma-subsampling note doc 03 section 2 also mentions -- both require AVCodecContext fields only populated post-avcodec_open2 (a decode pass), which this phase does not have. Evidence is scoped to what codecpar alone provides; no acceptance criterion or must_have truth required these fields specifically."
  - "video.frame_count's evidence source is recorded as parser_scan for virtually every fixture in this corpus, because video_gop_analyzer() (registered, ContainerFamily::other, always applicable) unconditionally pulls Pass::parser_scan into the union for every file -- this analyzer's own required_passes deliberately does NOT declare parser_scan (Task 1's own instruction), and still benefits from it being present whenever the real orchestrator runs."

patterns-established:
  - "detail::render_profile_value/render_level_value as pure, codecpar-independent functions exposed via analyzers.h for direct unit testing of a compared-value rendering rule -- the same 'test-only/production-shared extraction point' shape as detail::compute_median_fragment_duration (mp4.cpp) and detail::compute_peak_window (size.cpp)."

requirements-completed: [VIDEO-02]

coverage:
  - id: D1
    description: "video.codec/profile/level/resolution/frame_count registered end to end: real values per video stream, --explain docs, and DOC-03 trigger/clean fixture pairs, each proven empirically against the real binary"
    requirement: VIDEO-02
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp#doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#video_stream_params - video_base.mp4 emits all five checks once each, with the right Value alternative"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.frame_count is always counted from the packet/parser scan, never AVStream::nb_frames, with the container's own claim and an agreement boolean visible in evidence; the disagreement is proven on a real corpus fixture (video_h264_closed.h264, a raw elementary stream that never sets nb_frames)"
    requirement: VIDEO-02
    verification:
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#video_stream_params - video_h264_closed.h264's container-declared frame count (0, a raw elementary stream never sets AVStream::nb_frames) differs from the counted value in evidence"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#video_stream_params - video.frame_count skips partial_scan when the packet scan truncated, but the other four checks still report"
        status: pass
    human_judgment: false
  - id: D3
    description: "video.resolution is the first shipped check to carry transform_affected = true, comparing the candidate against the transform profile's derived expectation rather than the baseline"
    verification:
      - kind: other
        ref: "mediadiff compare video_base.mp4 video_res_640.mp4 --profile transform --config <[transform.expect] resolution=2x> --json reports video.resolution at pass (message: candidate resolution matches derived expectation 640x480); the same pair with resolution=3x reports non-pass"
        status: pass
    human_judgment: false
  - id: D4
    description: "video.profile renders an unresolved profile as its raw integer, never a shared 'unknown' word, so two different unresolved profile integers compare as different (VIDEO-01-E2)"
    verification:
      - kind: unit
        ref: "tests/unit/test_video_stream_params.cpp#video_stream_params - render_profile_value renders the raw integer when unresolved, and two DIFFERENT unresolved integers render as different strings"
        status: pass
    human_judgment: false

duration: 22min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 06: Video Stream Parameter Identity Checks Summary

**`video.codec`/`video.profile`/`video.level`/`video.resolution`/`video.frame_count` registered end to end -- the five per-video-stream identity checks, with `video.resolution` becoming the first shipped check to carry `transform_affected`, `video.frame_count` proven counted (never trusted) against a real fixture, and `DemuxSession::StreamInfo` extended to expose the codecpar fields none of Phase 4's prior plans had reached yet.**

## Performance

- **Duration:** 22 min
- **Started:** 2026-09-12T22:01:35+02:00 (approx., base HEAD at dispatch)
- **Completed:** 2026-09-12T22:22:23+02:00
- **Tasks:** 3/3 completed
- **Files modified:** 16 (7 created, 9 modified)

## Accomplishments

- `src/analyzers/video/stream_params.cpp`: the five roster-approved identity checks, all codec-scoped (`ContainerFamily::other`, `required_passes = {demux_header, packet_scan}`), registered in `src/probe/orchestrator.cpp` before `video_gop_analyzer()`.
- `src/probe/demux_session.h`/`.cpp`: `StreamInfo` extended with `profile`, `profile_name`, `level`, `width`, `height`, `codec_id_raw`, `codec_tag_raw`, `declared_frame_count` -- resolved once inside `demux_session.cpp` (including `avcodec_profile_name`'s own call), never crossing the file's own opaque-`AVFormatContext` boundary into `src/analyzers/`.
- `video.resolution` registered with `transform_affected = true` and empirically proven against `src/compare/exact.cpp`'s `transform_active` branch: `mediadiff compare video_base.mp4 video_res_640.mp4 --profile transform` with a declared `2x` expectation reports `pass` (`candidate resolution matches derived expectation 640x480`); a declared `3x` expectation reports non-pass.
- `video.frame_count` counted from the shared scan (preferring the parser scan's own access-unit count when present, recorded as `source` in evidence) with the container's own `nb_frames` claim and an `agrees_with_declared` boolean also in evidence -- proven to genuinely disagree on `video_h264_closed.h264` (a raw Annex-B elementary stream, `declared_frame_count: 0`, counted: 96).
- Five `docs/checks/<id>.md` files, each with the three required headings and `Accept`/`Tune`/`Silence` sub-structure; `video.level.md` documents in full the empirical finding that HEVC tier cannot be folded in without a decode pass.
- `tests/unit/test_video_stream_params.cpp` (8 test cases) and five new `tests/integration/test_doc03_coverage.cpp` declared pairs (`video.profile`/`video.level` deliberately sharing one triggering pair, per the codec property this plan's own action text names).

## Task Commits

Tasks 1 and 2 were committed together (see Issues Encountered for why); Task 3 separately:

1. **Tasks 1+2: video analyzer skeleton, video.codec/profile/level, video.resolution's transform expectation, video.frame_count counted from the scan** - `c1e4df5` (feat)
2. **Task 3: unit coverage and the five DOC-03 fixture pairs** - `061a1ca` (test)

_No plan-metadata commit yet -- this SUMMARY.md and the STATE.md/ROADMAP.md/REQUIREMENTS.md updates it triggers are committed as a separate `docs(04-06)` commit immediately after this file is written, per this executor's own required order._

## Files Created/Modified

- `src/analyzers/video/stream_params.cpp` - `video_stream_params_analyzer()`, `run_video_stream_params`, `emit_codec`/`emit_profile`/`emit_level`/`emit_resolution`/`emit_frame_count`, `detail::render_profile_value`/`render_level_value`
- `src/analyzers/video/analyzers.h` - `video_stream_params_analyzer()` declaration plus the two `detail::` render-function declarations, exposed for direct testing
- `src/probe/demux_session.h`/`.cpp` - `StreamInfo` extended with the codecpar fields video.codec/profile/level/resolution/frame_count need
- `src/probe/orchestrator.cpp` - `video_stream_params_analyzer()` registered in `all_analyzers()`, before `video_gop_analyzer()`
- `src/core/checks.def` - `video.codec`/`video.profile`/`video.level`/`video.resolution`/`video.frame_count` (all `video`/exact-or-tol per the roster), plus the `transform_affected` header comment updated to name `video.resolution` as the first shipped carrier
- `docs/checks/video.codec.md`, `video.profile.md`, `video.level.md`, `video.resolution.md`, `video.frame_count.md` - the five `--explain` docs
- `CMakeLists.txt` - `src/analyzers/video/stream_params.cpp` added to `libmediadiff`'s source list
- `tests/unit/test_video_stream_params.cpp` (new), `tests/unit/CMakeLists.txt` - Task 3's own test suite
- `tests/integration/test_doc03_coverage.cpp` - the five declared trigger/clean pairs, registry-count comment updated to thirty-six
- `tests/golden/list_checks_effective.txt` - refreshed (a five-line addition, one per new check) via `UPDATE_GOLDENS=1`

## Decisions Made

See `key-decisions` in the frontmatter above for the full list. In short: `DemuxSession::StreamInfo` was extended (a Rule 3 fix, not named in the plan's own `files_modified`) because there was no other sanctioned path to codecpar's profile/level/width/height/nb_frames fields from `src/analyzers/video/`; HEVC tier is documented as genuinely unreachable in this no-decode-pass phase rather than guessed at; `video.resolution`'s evidence is scoped to what codecpar alone provides (no coded dimensions); `video.frame_count`'s evidence source is `parser_scan` for nearly every real-orchestrator run because `video_gop_analyzer()` always pulls that pass into the union.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `DemuxSession::StreamInfo` did not expose the codecpar fields this plan's five checks need**
- **Found during:** Task 1, while designing `stream_params.cpp`'s access to `codecpar->profile`/`level`/`width`/`height` and `AVStream::nb_frames`.
- **Issue:** `StreamInfo` (as of 03-04-PLAN.md) carried only `media_type`, `codec_name`, `is_timecode`, `is_caption`. `demux_session.h`'s own top-of-file comment establishes a hard boundary ("no libav header crosses this file's public surface... `src/analyzers/` reaches libav-derived data only through these accessors, never a raw `AVFormatContext*`"), and `src/analyzers/` never includes a libav header directly (confirmed by every existing analyzer's own top-of-file comment and by `scripts/lint_check_id_strings.sh`'s sibling gates). There was no sanctioned path for `stream_params.cpp` to read `codecpar->profile`, `avcodec_profile_name`'s resolved string, `codecpar->level`, `codecpar->width`/`height`, or `AVStream::nb_frames` without either violating that boundary or duplicating libav access inside `src/analyzers/`.
- **Fix:** Extended `StreamInfo` with `profile`, `profile_name` (resolved via `avcodec_profile_name` inside `demux_session.cpp` itself), `level`, `width`, `height`, `codec_id_raw`, `codec_tag_raw`, and `declared_frame_count` (from `AVStream::nb_frames`) -- populated in `DemuxSession::stream_info()`, the existing single per-stream accessor, following the exact pattern its own `is_timecode`/`is_caption` fields already established.
- **Files modified:** `src/probe/demux_session.h`, `src/probe/demux_session.cpp`
- **Verification:** `./build/x64-linux/mediadiff inspect tests/fixtures/video_base.mp4 --json` shows `video.codec`=`mpeg4`, `video.profile`=`Simple Profile`, `video.level`=`1`, `video.resolution`=`320x240`, `video.frame_count`=`100`, matching 04-02-SUMMARY.md's own read-back table exactly. `tests/unit/test_video_stream_params.cpp` pins the shape.
- **Committed in:** `c1e4df5` (Task 1+2's combined commit)

**2. [Rule 1 - Empirical correction, not a bug in written code] HEVC tier is not accessible from `codecpar` without a decode pass, contradicting the design doc's literal wording**
- **Found during:** Task 1, while implementing `video.level`'s hand-written table per claude_docs/03-video-analysis.md section 2 ("HEVC `123`->`4.1` main-tier flagging").
- **Issue:** Read the pinned FFmpeg 8.1 source directly (`libavcodec/hevc/ps.c`/`ps.h`): `general_tier_flag` is parsed into `HEVCSPS`, a struct entirely private to `libavcodec/hevc/`, never copied onto `AVCodecParameters`, `AVCodecContext`, or `AVCodecParserContext`. Confirmed independently by grepping `fftools/ffprobe.c` for "tier" (zero matches) -- ffprobe itself, using the same public API surface this project uses, does not report HEVC tier either. There is no public libav API that yields tier without a decode pass, which this phase does not have (04-CONTEXT.md D-08/D-09).
- **Fix:** `render_level_value`'s HEVC branch renders the numeric level only (`general_level_idc / 30 . (general_level_idc % 30) / 3`), with no tier suffix. Documented in full in `docs/checks/video.level.md` (a dedicated section) and in the `checks.def` comment block, so a future reader does not mistake the omission for an oversight. The approved roster's attributes for `video.level` (id/semantic/unit/value_kind/severity/tolerance) are unchanged -- only the internal rendering table's completeness is affected, exactly as flagged assumption A1 anticipated.
- **Files modified:** `src/analyzers/video/stream_params.cpp`, `docs/checks/video.level.md`, `src/core/checks.def` (comment only)
- **Verification:** No fixture in this plan's own corpus exercises HEVC stream parameters in a container (the project's only HEVC fixtures, `video_hevc_idr.hevc`/`video_hevc_cra.hevc` from 04-05, are raw elementary streams built for GOP classification, not profile/level), so this omission does not affect any acceptance criterion or DOC-03 pair in this plan.
- **Committed in:** `c1e4df5` (Task 1+2's combined commit)

**3. [Rule 3 - Blocking] `tests/golden/list_checks_effective.txt` needed refreshing for the five new checks' own registration**
- **Found during:** full-suite verification (`ctest --test-dir build/x64-linux --output-on-failure`).
- **Issue:** `integration.list_checks`'s own `--effective`-is-byte-identical golden test failed because the five new registrations are a real, intended change to the registry's output -- the golden predates this plan by construction (identical shape to 04-01-SUMMARY.md's own Deviation 2).
- **Fix:** Refreshed via `UPDATE_GOLDENS=1 ctest -R "list_checks.*ENG-12"`; the diff is exactly five new lines, one per new check.
- **Files modified:** `tests/golden/list_checks_effective.txt`
- **Verification:** `git diff` on the golden shows a five-line addition only; `ctest --test-dir build/x64-linux --output-on-failure` reports 648 passed, 0 failed, 6 designated skips.
- **Committed in:** `c1e4df5`

---

**Total deviations:** 3 auto-fixed (2 Rule 3, 1 Rule 1 empirical correction). **Impact on plan:** The `DemuxSession::StreamInfo` extension was necessary to complete the task at all, following an established per-file pattern exactly (not an architectural change). The HEVC tier omission is a documented capability boundary, not a defect, and does not affect this plan's own acceptance criteria (no HEVC-in-container fixture exists in this corpus). The golden refresh is an intended, foreseen consequence of registering new checks. No scope creep.

## Issues Encountered

**Tasks 1 and 2 committed together.** Both write to the same file (`src/analyzers/video/stream_params.cpp`) and share the same `push_skip`/`scope_kind_for_stream`/`compute_stream_scopes` helpers and the same `run_video_stream_params` loop body -- splitting the diff into two commits after the fact would have meant temporarily hacking apart already-validated, working code purely to satisfy commit granularity, the identical situation 04-05-SUMMARY.md's own "Issues Encountered" already documented for this exact reason. Both tasks' own acceptance criteria are independently verified above regardless of commit boundary.

## Requirement Gate

`gsd_run query requirements.ready-ids` was run against this plan's two declared requirements (`VIDEO-01`, `VIDEO-02`) AFTER this SUMMARY.md was written, per the `<requirement_marking_guard>` this execution operated under:

```json
{"ready": ["VIDEO-02"], "blocked": ["VIDEO-01"]}
```

Matches the dispatch-computed expected outcome exactly. `VIDEO-02` was marked complete (this plan implements its checks in full: `video.frame_count` counted, never trusted, container claim visible in evidence). `VIDEO-01` stays blocked -- also declared by plans 04-07, 04-08 and 04-12, none of which have landed yet.

## Known Stubs

None.

## Threat Flags

None beyond the STRIDE register already authored in `04-06-PLAN.md`'s own `<threat_model>` (T-4-24 through T-4-27, T-4-SC) -- every mitigation there is implemented as specified: T-4-24 (an unresolved profile renders its raw integer, never a shared word) is `detail::render_profile_value`'s own rule, pinned by a direct unit test; T-4-25 (rendered strings pass through `sanitize_for_display`) is satisfied structurally -- every rendered string here (`codec_name`, `profile_name`, the level table's own fixed spellings) originates from libav's own stable API surface or this project's own hand-written table, never from a freeform metadata field, so the existing `scripts/lint_control_bytes.sh` gate (which scans exactly this class of risky field) passes clean; T-4-26 (`skipped:partial_scan` with the resolved byte cap in evidence) is implemented and pinned by a direct unit test; T-4-27 (a file declaring a very large number of video streams) is accepted per Phase 3's own `T-3-12` bound, unchanged by this plan.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`DemuxSession::StreamInfo`'s codecpar-field extension (`profile`/`profile_name`/`level`/`width`/`height`/`codec_id_raw`/`codec_tag_raw`/`declared_frame_count`) is available to every remaining video.* plan; 04-07 (`video.sar`/`video.dar`/`video.frame_rate.*`) will likely need a further extension for `sample_aspect_ratio` and `framerate`/`avg_frame_rate`, following the exact same per-field-addition pattern this plan established. `video.resolution`'s `transform_affected = true` registration is the template 04-CHECK-ROSTER.md's remaining `transform`-eligible checks (none currently declared beyond this one) would follow if a future plan needs it. No blockers.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED

All 7 created files confirmed present on disk; both commit hashes (`c1e4df5`, `061a1ca`) confirmed present in `git log --oneline --all`.
