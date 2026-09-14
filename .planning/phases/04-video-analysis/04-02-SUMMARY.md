---
phase: 04-video-analysis
plan: 02
subsystem: probe
tags: [ffmpeg, fixtures, mjpeg, mpeg2video, mpeg4, huffyuv, matroska, colorimetry, interlace]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-01's approved 31-id check roster (04-CHECK-ROSTER.md) and the video.gop.length fixture-pair precedent this plan's fixtures follow"
provides:
  - "REQUIRED_ENCODERS extended with mjpeg and huffyuv, asserted on every pinned generator build before scripts/gen_corpus.sh runs on any leg (closes 04-RESEARCH.md assumption A3)"
  - "27 new fixture files (plus 3 non-media .m2v sidecars) covering VIDEO-01/02/05/06/07/08/12's real-encoder-buildable stream-parameter, GOP, colorimetry, yuvj-signature and interlace pairs, each read back and verified against the project's own linked FFmpeg 8.1 rather than trusted on CLI-flag acceptance"
  - "The regenerated tests/golden/CORPUS_DIGEST.txt covering the whole enlarged corpus, still asserting with exactly 3 excluded lines"
affects: [04-06-video-stream-params, 04-07-video-sar-framerate, 04-08-video-color, 04-09-video-gop-frame-types, 04-10-video-interlace, 04-12-video-dovi]

actuals:
  tokens: 5977
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Read-back-over-recollection for fixture construction: every non-trivial recipe (profile/level, colorimetry, chroma location, field order) was verified with a throwaway program linked directly against the project's OWN vcpkg-built FFmpeg 8.1 (not the generator's 9.0.1, not an ambient system ffmpeg), because a CLI flag being accepted, or a newer/different ffmpeg version's read-back, is not evidence the property reaches the file the shipped analyzer will actually read."
    - "Container choice is part of what a fixture 'is': the same codecpar field (chroma_location) round-trips through Matroska's Colour master element but has no ISOBMFF box at all in movenc.c for any LGPL-safe codec this project can use -- the container, not just the codec, is a per-check research question."

key-files:
  created:
    - .planning/phases/04-video-analysis/04-02-SUMMARY.md
  modified:
    - scripts/install_pinned_ffmpeg.sh
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "REQUIRED_ENCODERS gained mjpeg (VIDEO-03's only LGPL encoder accepting yuvj420p) and huffyuv (VIDEO-12's no-parser codec), not ffv1 as the plan text named -- see Deviations."
  - "video_chroma_left/video_chroma_center mux to Matroska (.mkv), not MP4 as the plan text named -- movenc.c has no code path writing chroma sample location into any mp4/mov box for any LGPL-safe codec; matroskaenc.c/matroskadec.c round-trip it correctly via the Colour master element."
  - "video_ilace_tff/bff/mixed use mpeg2video, not mpeg4 as the plan text named -- mpeg4video_parser.c never sets AVCodecParserContext::field_order; mpegvideo_parser.c (MPEG-1/2 only) does, from the picture coding extension's top_field_first bit."
  - "video_ilace_mixed.mp4 is built by binary-concatenating two independently-encoded raw MPEG-2 elementary-stream segments (one interlaced, one progressive) and remuxing with -c copy, so the container-level declared field order (from the first sequence header) and later per-AU parser flags genuinely disagree within one file -- the exact VIDEO-06 cross-check fixture the plan's own flagged assumption A3 called for."

patterns-established:
  - "Throwaway libavformat/libavcodec C probes (not system ffprobe) as the read-back tool for fixture verification when the property under test is version-sensitive (chroma_location, field_order) -- compiled against build/x64-linux/vcpkg_installed/x64-linux with -Wl,--start-group ... --end-group to resolve the static-archive link order, discarded after use."

requirements-completed: [VIDEO-01, VIDEO-02, VIDEO-03, VIDEO-05, VIDEO-06, VIDEO-07, VIDEO-08, VIDEO-12]

coverage:
  - id: D1
    description: "mjpeg and huffyuv asserted present on every pinned generator build via REQUIRED_ENCODERS, closing 04-RESEARCH.md assumption A3 before scripts/gen_corpus.sh's recipes can fail with a less informative message"
    requirement: VIDEO-03
    verification:
      - kind: other
        ref: "bash scripts/install_pinned_ffmpeg.sh (exit 0) && .ffmpeg-pinned/linux-x86_64/ffmpeg -hide_banner -encoders | grep -cE '[[:space:]](mjpeg|huffyuv)[[:space:]]' == 2; a deliberately misspelled entry confirmed to fail loudly (exit 1, names the entry), then reverted"
        status: pass
    human_judgment: false
  - id: D2
    description: "Stream-parameter, GOP and no-parser fixture pairs (video_base/_copy, video_codec_mpeg2, video_prof_a/b, video_res_640, video_frames_50, video_sar_4_3, video_fps_30, video_vfr, video_bf3, video_noparser/_copy) generated and read back to confirm each differs from video_base.mp4 in exactly its intended dimension"
    requirement: VIDEO-01
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_bash4_builtins.sh && bash scripts/lint_fixture_case_collisions.sh"
        status: pass
      - kind: other
        ref: "ffprobe read-back table (this SUMMARY's Task 2 section) confirming distinct codec_name/profile/level/resolution/frame_count/sar/fps/pict_type per pair"
        status: pass
    human_judgment: false
  - id: D3
    description: "VIDEO-03 signature trio, colorimetry set and interlace set generated and verified against the linked FFmpeg 8.1 via a throwaway probe, tests/golden/CORPUS_DIGEST.txt regenerated once, ctest 640/640 with only the designated golden-provenance skips"
    requirement: VIDEO-07
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_bash4_builtins.sh && bash scripts/assert_corpus_digest.sh (3 excluded lines) && ctest --test-dir build/x64-linux --output-on-failure (640/640, 6 designated skips)"
        status: pass
    human_judgment: false

duration: 45min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 02: Video Fixture Corpus (stream params, GOP, colorimetry, interlace) Summary

**27 new video fixtures for VIDEO-01/02/05/06/07/08/12, every non-trivial property read back against the project's own linked FFmpeg 8.1 rather than trusted on the CLI flag being accepted -- surfacing and resolving three cases where the plan's literal recipe (codec or container choice) didn't actually express the property it was meant to.**

## Performance

- **Duration:** 45 min
- **Started:** 2026-09-12T17:05:00Z (approx.)
- **Completed:** 2026-09-12T17:30:41Z
- **Tasks:** 3/3 completed
- **Files modified:** 4 (scripts/install_pinned_ffmpeg.sh, scripts/gen_corpus.sh, tests/golden/CORPUS_DIGEST.txt, tests/fixtures/GENERATOR_MANIFEST.json)

## Accomplishments

- `REQUIRED_ENCODERS` extended with `mjpeg` and `huffyuv`, asserted on every pinned generator build (all five CI legs) before any recipe using them runs -- closes `04-RESEARCH.md` assumption A3.
- 16 stream-parameter/GOP/no-parser fixtures (`video_base`/`_copy`, `video_codec_mpeg2`, `video_prof_a`/`b`, `video_res_640`, `video_frames_50`, `video_sar_4_3`, `video_fps_30`, `video_vfr`, `video_bf3`, `video_noparser`/`_copy`) for VIDEO-01/02/05/12, each read back via `ffprobe` to confirm it differs from `video_base.mp4` in exactly its intended dimension.
- 14 colorimetry/yuvj-signature/interlace fixtures (the VIDEO-03 signature trio, `video_color_bt709`/`bt601`/`unspec`, `video_range_pc`, `video_color_bt709_copy`, `video_chroma_left`/`center`, `video_ilace_tff`/`bff`/`mixed`/`tff_copy`) for VIDEO-03/06/07/08, each read back via a throwaway `libavformat`/`libavcodec` probe linked against the project's own vcpkg-built FFmpeg 8.1.
- `tests/golden/CORPUS_DIGEST.txt` regenerated once for the whole enlarged corpus; `assert_corpus_digest.sh` passes with the required 3 excluded lines; `ctest` is 640/640 with only the 6 designated skips (WINDOWS.md #12's golden-provenance class plus `unit.console_vt`).

## Task Commits

Each task was committed atomically:

1. **Task 1: Assert `mjpeg` and `ffv1` on every pinned generator build** - `d9cc67d` (feat) -- landed as `mjpeg`/`ffv1`; corrected to `mjpeg`/`huffyuv` in Task 2's commit per the plan's own instruction to land a substitution together with the encoder-list change.
2. **Task 2: Stream-parameter, GOP and no-parser fixture recipes** - `a0d5d83` (feat)
3. **Task 3: Colorimetry, the yuvj signature trio, and interlace fixture recipes** - `266c256` (feat)

## Files Created/Modified

- `scripts/install_pinned_ffmpeg.sh` - `REQUIRED_ENCODERS` gains `mjpeg`, `huffyuv` (Task 1, corrected in Task 2)
- `scripts/gen_corpus.sh` - 27 new fixture recipes plus 3 non-media `.m2v` sidecars, appended additively before the trailing summary `echo`; the resolution logic and identity gate in `resolve_pinned_ffmpeg.sh` untouched
- `tests/golden/CORPUS_DIGEST.txt` - regenerated once (Task 3) for the whole enlarged corpus
- `tests/fixtures/GENERATOR_MANIFEST.json` - `generated_at` timestamp only (regenerated by every `gen_corpus.sh` run)

## Decisions Made

- `REQUIRED_ENCODERS` closes `mjpeg` + `huffyuv`, not `mjpeg` + `ffv1` -- see Deviations.
- `video_chroma_left`/`video_chroma_center` are `.mkv`, not `.mp4` -- see Deviations. **This changes the filenames plan 04-08 will need to reference** (04-08-PLAN.md currently reads `video_chroma_left.mp4`/`video_chroma_center.mp4`); flagged prominently in Next Phase Readiness below.
- `video_ilace_tff`/`bff`/`mixed` use `mpeg2video`, not `mpeg4` -- see Deviations. Filenames are unchanged (`.mp4`), so this is transparent to plan 04-10's fixture references.
- `video_ilace_mixed.mp4` is a binary concatenation of two independently-encoded raw MPEG-2 elementary-stream segments (interlaced + progressive), remuxed with `-c copy`, producing a genuine declared-vs-per-frame field-order disagreement within one file -- exactly what VIDEO-06's cross-check fixture needs, and what the plan's own flagged assumption A3 anticipated in shape (though the plan's own text assumed `mpeg4`).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `ffv1`/`prores` no longer exercise VIDEO-12's no-parser path against this phase's actually-linked FFmpeg**
- **Found during:** Task 2, while verifying the VIDEO-12 no-parser fixture per the plan's own instruction ("confirm empirically ... that `av_parser_init` for that codec id returns null").
- **Issue:** 04-02-PLAN.md named `ffv1` (falling back to `prores`) as the no-parser candidate, both verified against an OLDER FFmpeg source tree during Phase 4 research. A throwaway `av_parser_init` probe against `build/x64-linux/vcpkg_installed/x64-linux`'s actually-linked FFmpeg 8.1 showed BOTH now have a real `*_parser.c` (`ffv1_parser.c`, `prores_parser.c` both exist in the linked source tree) and `av_parser_init` returns non-null for both -- neither exercises the no-parser path any more.
- **Fix:** Searched further native LGPL-safe encoders for one with no `*_parser.c` file at all; `huffyuv` has none, and a throwaway probe confirmed `av_parser_init(AV_CODEC_ID_HUFFYUV)` returns null. Substituted `huffyuv` for `ffv1` in both `REQUIRED_ENCODERS` (`scripts/install_pinned_ffmpeg.sh`) and the fixture recipe (`scripts/gen_corpus.sh`), per the plan's own documented decision criterion (the empirical `av_parser_init` result, never a recollection).
- **Files modified:** `scripts/install_pinned_ffmpeg.sh`, `scripts/gen_corpus.sh`
- **Verification:** `./build/x64-linux/mediadiff inspect tests/fixtures/video_noparser.mkv --json` succeeds, reports one video stream (`huffyuv`).
- **Committed in:** `a0d5d83` (Task 2's own commit, per the plan's instruction to land the substitution together with the encoder-list change)

**2. [Rule 1 - Bug] `mjpeg` refuses to open for a non-full-range request without `-strict unofficial`**
- **Found during:** Task 3, building `video_yuv420p_tv.mp4`.
- **Issue:** `-c:v mjpeg -pix_fmt yuv420p -color_range tv` fails to open the encoder: `"Non full-range YUV is non-standard, set strict_std_compliance to at most unofficial to use it."` Not mentioned in the plan's literal recipe text.
- **Fix:** Added `-strict unofficial`. Read back and confirmed distinct from the other two trio members on both `pix_fmt` and `color_range` (`mjpeg,yuv420p,tv` vs `mjpeg,yuvj420p,pc`).
- **Files modified:** `scripts/gen_corpus.sh`
- **Committed in:** `266c256` (Task 3's own commit)

**3. [Rule 1 - Bug] `video_chroma_left`/`video_chroma_center` do not round-trip through MP4**
- **Found during:** Task 3, building the chroma-location pair.
- **Issue:** The plan's literal recipe (`setparams=...:chroma_location=left/center`, `mpeg4`, `.mp4`) reads back `chroma_location=left` on BOTH fixtures regardless of what was requested -- exactly the "silently degrades to a matching pair" trap `04-RESEARCH.md`'s Pitfall 1 warns about, just for chroma location instead of primaries/transfer. Traced to source: `libavformat/movenc.c` (grepped directly, `build/x64-linux/vcpkg_installed`'s vendored FFmpeg 8.1 source tree) has NO code path that writes chroma sample location into any mp4/mov box at all, for any codec. The `setparams` filter's own `chroma_location` suboption additionally does not propagate through `mpeg4`/`mov` even where a box exists (tested independently of the container question).
- **Fix:** Switched to Matroska (`.mkv`) and the top-level `-chroma_sample_location` CLI option (not the `setparams` suboption). `libavformat/matroskaenc.c` writes `chroma_location` into the Colour master element's `ChromaSitingHorz`/`ChromaSitingVert` fields; `matroskadec.c` reads it back. Verified via a throwaway probe: `video_chroma_left.mkv` reads back `chroma_location=left` (raw value 1), `video_chroma_center.mkv` reads back `chroma_location=center` (raw value 2), both alongside identical `bt709` colorimetry.
- **Files modified:** `scripts/gen_corpus.sh`
- **Downstream impact:** `04-08-PLAN.md` currently references `video_chroma_left.mp4`/`video_chroma_center.mp4` (the `.mp4` extension). These fixtures do not exist under that name; the working fixtures are `video_chroma_left.mkv`/`video_chroma_center.mkv`. Flagged in Next Phase Readiness below for plan 04-08's executor.
- **Committed in:** `266c256` (Task 3's own commit)

**4. [Rule 1 - Bug] `video_ilace_tff`/`bff` do not populate `field_order` when encoded with `mpeg4`**
- **Found during:** Task 3, building the interlace pair per the plan's literal recipe (`mpeg4`, `-flags +ilme+ildct`).
- **Issue:** Both `codecpar->field_order` and the per-AU `AVCodecParserContext::field_order` (via a throwaway `av_parser_parse2` probe mirroring 04-01's own `PARSER_FLAG_COMPLETE_FRAMES` fusion) read back `unknown`/`0` for BOTH the TFF and BFF variants -- a silently-matching pair, the same trap class as Deviation 3. Traced to source: `libavcodec/mpeg4video_parser.c` never sets `AVCodecParserContext::field_order` at all (confirmed by grepping every parser source file that references `field_order`; `mpeg4video_parser.c` is absent from that list). `libavcodec/mpegvideo_parser.c` (MPEG-1/2 only) DOES set it, derived from the picture coding extension's `top_field_first` bit.
- **Fix:** Switched the encoder to `mpeg2video` (still real, always-built-in, never GPL, per D-04). Verified via the same throwaway parser probe: TFF reads back per-AU `AV_FIELD_TT`, BFF reads back per-AU `AV_FIELD_BB`; container-level `codecpar->field_order` also distinct (`TB`/`BT`); neither progressive.
- **Files modified:** `scripts/gen_corpus.sh`
- **Downstream impact:** None -- filenames (`video_ilace_tff.mp4`/`video_ilace_bff.mp4`) are unchanged; only the internal codec differs, which is transparent to plan 04-10's fixture references.
- **Committed in:** `266c256` (Task 3's own commit)

---

**Total deviations:** 4 auto-fixed, all Rule 1 (bugs where the plan's literal recipe did not actually express the property under test, or a discovered blocking encoder-open error). **Impact on plan:** All four were necessary for correctness -- each is exactly the class of "silently matching pair" or "encoder refuses to open" failure this plan's own `must_haves` and `04-RESEARCH.md`'s Pitfall 1 explicitly warn against. Deviation 1 required updating Task 1's already-committed `REQUIRED_ENCODERS` value in Task 2's commit, per the plan's own documented contingency for this exact situation. Deviation 3 has a real downstream-filename impact on plan 04-08 (see Next Phase Readiness).

## Issues Encountered

**Verification tooling mismatch (resolved before it produced a wrong result).** The workstation's ambient `ffprobe` (`/usr/local/bin/ffprobe`) is a very recent FFmpeg master-branch nightly build (`N-126086`, 2026-08-12), unrelated to both the fixture *generator* (the pinned 9.0.1 martin-riedl build) and the project's actually-*linked* FFmpeg (8.1, `build/x64-linux/vcpkg_installed`). Early in Task 3, using this ambient `ffprobe` to verify the chroma-location pair produced a MISLEADING result (it appeared to round-trip correctly through `mpeg2video`-in-`.mkv`), which would not have reflected what the shipped `mediadiff` binary (linked against 8.1) actually reads. Recognized and corrected by writing two throwaway C probes (`probe_stream.c`, `probe_parser.c`) compiled directly against `build/x64-linux/vcpkg_installed/x64-linux`'s own `libavformat`/`libavcodec`, and re-verifying every non-trivial property (profile/level already matched across versions; chroma_location and field_order did not) against those. All read-back claims in this SUMMARY and in `scripts/gen_corpus.sh`'s own comments are sourced from the linked-FFmpeg probes, not the ambient system `ffprobe`.

## Read-Back Verification Tables

### Task 2: stream-parameter fixtures (via ambient `ffprobe`; codec/profile/level/geometry/frame-count/pict_type are stable across FFmpeg versions)

| File | codec_name | profile | level | width | height | sar | avg_frame_rate | packets |
|---|---|---|---|---|---|---|---|---|
| video_base.mp4 | mpeg4 | Simple Profile | 1 | 320 | 240 | 1:1 | 25/1 | 100 |
| video_codec_mpeg2.mp4 | mpeg2video | Main | 8 | 320 | 240 | 1:1 | 25/1 | 100 |
| video_prof_a.mp4 | mpeg2video | Main | 8 | 320 | 240 | 1:1 | 25/1 | 100 |
| video_prof_b.mp4 | mpeg2video | Simple | 10 | 320 | 240 | 1:1 | 25/1 | 100 |
| video_res_640.mp4 | mpeg4 | Simple Profile | 1 | 640 | 480 | 1:1 | 25/1 | 100 |
| video_frames_50.mp4 | mpeg4 | Simple Profile | 1 | 320 | 240 | 1:1 | 25/1 | 50 |
| video_sar_4_3.mp4 | mpeg4 | Simple Profile | 1 | 320 | 240 | 4:3 | 25/1 | 100 |
| video_fps_30.mp4 | mpeg4 | Simple Profile | 1 | 320 | 240 | 1:1 | 30/1 | 120 |
| video_bf3.mp4 | mpeg4 | Advanced Simple Profile | 1 | 320 | 240 | 1:1 | 25/1 | 100 |

`video_base.mp4` pict_type histogram: 3 I / 97 P (zero B). `video_bf3.mp4`: 3 I / 23 P / 74 B. `video_vfr.mp4` packet PTS deltas: {512, 1024} ticks (non-uniform); `video_base.mp4`'s are uniformly 512.

### Task 3: colorimetry/chroma/interlace fixtures (via a throwaway `libavformat`/`libavcodec` probe linked against the project's own FFmpeg 8.1)

| File | pix_fmt | color_range (raw) | primaries (raw) | transfer (raw) | matrix (raw) | chroma_location (raw) |
|---|---|---|---|---|---|---|
| video_yuvj420p.mp4 | yuvj420p | pc (2) | unknown (2) | unknown (2) | bt470bg (5) | center (2) |
| video_yuv420p_pc.mp4 | yuvj420p | pc (2) | unknown (2) | unknown (2) | bt470bg (5) | center (2) |
| video_yuv420p_tv.mp4 | yuv420p | tv (1) | unknown (2) | unknown (2) | bt470bg (5) | center (2) |
| video_color_bt709.mp4 | yuv420p | tv (1) | bt709 (1) | bt709 (1) | bt709 (1) | left (1) |
| video_color_bt601.mp4 | yuv420p | tv (1) | smpte170m (6) | smpte170m (6) | smpte170m (6) | left (1) |
| video_color_unspec.mp4 | yuv420p | tv (1) | unknown (2) | unknown (2) | unknown (2) | left (1) |
| video_range_pc.mp4 | yuv420p | pc (2) | bt709 (1) | bt709 (1) | bt709 (1) | left (1) |
| video_chroma_left.mkv | yuv420p | tv (1) | bt709 (1) | bt709 (1) | bt709 (1) | left (1) |
| video_chroma_center.mkv | yuv420p | tv (1) | bt709 (1) | bt709 (1) | bt709 (1) | center (2) |

`video_yuvj420p.mp4` vs `video_yuv420p_pc.mp4`: identical on every field above (the ZERO-findings half of the VIDEO-03 signature pair). `video_yuvj420p.mp4` vs `video_yuv420p_tv.mp4`: differ ONLY on `pix_fmt`/`color_range` (the EXACTLY-ONE-finding half, once the range fold lands in plan 04-08).

| File | codecpar.field_order | per-AU field_order (first AU, `av_parser_parse2`) | per-AU field_order (later AUs) |
|---|---|---|---|
| video_ilace_tff.mp4 | TB (4) | AV_FIELD_TT (2) | AV_FIELD_TT (2) throughout |
| video_ilace_bff.mp4 | BT (5) | AV_FIELD_BB (3) | AV_FIELD_BB (3) throughout |
| video_ilace_mixed.mp4 | TT (2, from the first sequence header) | AV_FIELD_TT (2) | switches to `progressive` (1) partway through -- the declared-vs-per-frame disagreement VIDEO-06 needs |

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`REQUIRED_ENCODERS` now closes research assumption A3 on every pinned build/leg. All 27 fixtures this plan's analyzer plans (04-06 through 04-12) depend on exist, are non-empty, pass `check_corpus.sh`'s mechanical extraction, and have each been read back and verified to differ in exactly their intended dimension against the project's own linked FFmpeg 8.1. `tests/golden/CORPUS_DIGEST.txt` covers the whole enlarged corpus.

**Action required before/during plan 04-08:** `04-08-PLAN.md` (already authored) references `video_chroma_left.mp4`/`video_chroma_center.mp4`. Those exact filenames do not exist -- the working, verified fixtures are `video_chroma_left.mkv`/`video_chroma_center.mkv` (Deviation 3 above; MP4 has no container box for chroma sample location in `libavformat/movenc.c` for any LGPL-safe codec). Plan 04-08's executor should update its own file references (a Rule 3 blocking-issue fix, per this project's own deviation rules) when it reaches those test cases; no code from this plan needs to change.

No other blockers. `video_ilace_tff`/`bff`/`mixed`'s internal codec change (`mpeg2video` instead of `mpeg4`, Deviation 4) is filename-transparent and requires no action from plan 04-10.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED
