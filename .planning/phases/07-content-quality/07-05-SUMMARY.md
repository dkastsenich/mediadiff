---
phase: 07-content-quality
plan: 05
subsystem: content
tags: [frozen-runs, black-runs, ssim, swscale, thumbnail, span-check, range-normalization, content.video.frozen_runs, content.video.black_runs, d-08]

requires:
  - phase: 07-content-quality
    plan: 01
    provides: "the fused video decode sweep (VideoDecodeState), the hash sink order, 07-CHECK-ROSTER.md's approved ids and finding 4"
  - phase: 07-content-quality
    plan: 04
    provides: "frame_count = decoded frames, video_frame_hashable, and D-08's rule that detectors see every decoded frame whatever --sample N is"
  - phase: 06-audio-analysis
    provides: "the span-check precedent (audio/silence.cpp): scopes, empty list as a real value, detail::ticks_to_ms"
provides:
  - "src/util/ssim_int.h: int64-only 8x8 SSIM in Q24 (kSsimC1 26634, kSsimC2 239708, ssim_window_q24, ssim_plane_q24, q24_to_micro)"
  - "src/probe/video_thumbnail.{h,cpp}: the deterministic 128-wide 8-bit luma thumbnail (Thumbnail, ThumbnailScaler, thumbnail_height, normalize_to_8bit, scaler_record(height))"
  - "src/probe/video_detectors.{h,cpp}: FrozenDetector, BlackDetector, detail::FrozenHysteresis, FrameRun, black_point_for_range, thumbnail_is_black and the fixed constants"
  - "content.video.frozen_runs and content.video.black_runs: span/ms/span_list checks with --explain docs and DOC-03 pairs (registry now 94 checks)"
  - "StreamVideoDecode::frozen_runs, black_runs, tap_frame_count, detectors_available, thumbnail_height, scaler_record, black_point, first_tap_tick, min_tick_delta, tap_interval_*"
  - "eleven fixtures (video_frozen*, video_black*, video_dark*) with workstation-computed digest lines, all listed in the provisional ledger"
affects: [07-06, 07-08, 07-10, 07-11, 07-14, 07-15]

actuals:
  tokens: 37300
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A sink that thins nothing it does not own: the detectors are tapped in consume_frame after the unhashable check and before the stride's store decision, so both run lists are identical under --sample N"
    - "Run boundaries carry their own ticks (FrameRun first_tick/last_tick), so a span needs no per-frame tick array and the stride cannot leave one incomplete"
    - "A detection constant is a fixed named inline constexpr in the header with the research measurement cited (Phase 5 D-08), never a knob"
    - "Timing ladder for spans: pts relative to the stream's first frame with the declared rate, else the smallest observed pts step, else decode index times the rate, else no_timing_data"

key-files:
  created:
    - src/util/ssim_int.h
    - src/probe/video_thumbnail.h
    - src/probe/video_thumbnail.cpp
    - src/probe/video_detectors.h
    - src/probe/video_detectors.cpp
    - src/analyzers/content/video_runs.cpp
    - docs/checks/content.video.frozen_runs.md
    - docs/checks/content.video.black_runs.md
    - tests/unit/test_ssim_int.cpp
    - tests/unit/test_video_detectors.cpp
    - tests/integration/test_video_detectors.cpp
  modified:
    - src/probe/video_decode.h
    - src/probe/video_decode.cpp
    - src/probe/orchestrator.cpp
    - src/analyzers/content/analyzers.h
    - src/core/checks.def
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt
    - tests/integration/coverage_pairs.h
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_audio_corpus_sweep.cpp
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - claude_docs/06-content-and-size-analysis.md
    - .planning/WINDOWS.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "The thumbnail height is capped (kMaxThumbnailHeight 32768 rows, thumbnail_height returns 0 above it) because max_pixels bounds width x height, not the aspect ratio: a 1 x 67M source would otherwise ask for 128 x 8.6e9 bytes; a refused thumbnail makes both checks insufficient_data, never a fabricated list"
  - "One frame interval for a span's end is the stream's declared rate, else the smallest positive pts step between detector frames (MPEG-TS declares no rate at open), else decode index times the rate; with none of these and at least one run the skip is no_timing_data, and with no runs the lists are real and empty"
  - "normalize_to_8bit clamps to 255: the plan's (v + (1 << (b-9))) >> (b-8) gives 256 for a full-scale 10-bit sample, while the plan's own test wants 255"
  - "scaler_record takes the thumbnail height (the record contains dst=128x<th>), so it is scaler_record(height) rather than the plan's scaler_record()"
  - "The black family is Matroska: the MP4 muxer drops -color_range for an MPEG-4 Part 2 stream (read-back shows plain yuv420p) and Matroska keeps it, so a full-range label that survives the container is what makes the range proof non-vacuous"
  - "Dark grey is built with lutyuv=y=17 then setrange, because color=c=0x111111 yields luma 31 (a colour conversion), not the plan's about 17"
  - "Black frames are identical frames, so a black segment is also a frozen run; both checks report it and DOC-03 judges each per its own id"

patterns-established:
  - "Run lists leave src/probe/ as frame-index runs with boundary ticks; only the analyzer turns them into core/value.h spans"
  - "Hysteresis state machine separate from SSIM and pixels (detail::FrozenHysteresis) so boundaries are unit-testable with bare score sequences"

requirements-completed: [CONTENT-06]

coverage:
  - id: D1
    description: "Integer SSIM is exact and portable: int64 only, identical windows score exactly 1<<24, the plane score is the floor mean of the window scores, and known-answer windows (constant offset, halved contrast, inverted, two random) match an independent double reference within 1e-6, with worst-case windows inside the documented bound"
    requirement: CONTENT-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#ssim_int - identical windows score exactly one"
        status: pass
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#ssim_int - known answers match a double-precision reference"
        status: pass
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#ssim_int - worst case stays inside the documented bounds"
        status: pass
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#ssim_int - plane score is the floor mean of the window scores"
        status: pass
    human_judgment: false
  - id: D2
    description: "The 128-wide thumbnail is SIMD-independent (auto CPU flags equal pure C) and the plain-SWS_AREA variant is NOT, the luma plane is scaled without range conversion, a 10-bit frame normalizes to the 8-bit value, and a non-wrappable format is converted and flagged"
    requirement: CONTENT-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#video_thumbnail - simd equals c"
        status: pass
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#video_thumbnail - the luma plane is scaled without range conversion"
        status: pass
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#video_thumbnail - a 10-bit frame is normalized to 8 bits"
        status: pass
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#video_thumbnail - a format that cannot be wrapped is converted and says so"
        status: pass
    human_judgment: false
  - id: D3
    description: "CONTENT-06 frozen: one 50-frame freeze reports the identical span [2040, 4040) ms for MPEG-4 with B-frames, MPEG-4 without, MJPEG, a transport-stream copy and a raw MPEG-2 elementary stream, and comparing two encodes of one freeze is clean"
    requirement: CONTENT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - frozen span"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - frozen gop stable"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - hysteresis boundary"
        status: pass
    human_judgment: false
  - id: D4
    description: "CONTENT-06 black, range- and depth-normalized: limited-range and full-range black report the same span [1000, 2000) ms and compare clean; full-range luma 17 is not black while the same pixels labelled limited are black for the whole clip; a synthetic 10-bit frame is black at the limited point"
    requirement: CONTENT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - black range normalized"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - dark grey"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - black depth"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - black point"
        status: pass
    human_judgment: false
  - id: D5
    description: "Boundaries and ordering: a qualifying run of exactly 3 frames is reported and 2 is not for both detectors, spans ascend by start, and a size change breaks a frozen run"
    requirement: CONTENT-06
    verification:
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - boundary of three frames"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - ordering"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - a size change breaks a frozen run and a short thumbnail is not a measurement"
        status: pass
    human_judgment: false
  - id: D6
    description: "A decoded stream reports a real (possibly empty) span list, never Absent; every skip branch (requires_decode, undecodable/truncated partial_scan with the truncation reason, zero frames, unscorable thumbnail, no_timing_data, time overflow) is exercised, and spans are exact ms from the stream's first frame on every timing path"
    requirement: CONTENT-06
    verification:
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - analyzer skip ladder"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_detectors.cpp#video_detectors_unit - span times"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - truncated"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - no content"
        status: pass
    human_judgment: false
  - id: D7
    description: "D-08: both span lists and their evidence are identical with and without --sample 3; the serialized spans and evidence survive a read_snapshot round trip"
    requirement: CONTENT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - sampling independent"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - the spans survive a snapshot round trip"
        status: pass
    human_judgment: false
  - id: D8
    description: "Both checks are registered with --explain docs and DOC-03 pairs (frozen: base vs freeze trigger, bf2 vs bf0 clean; black: base vs limited black trigger, tv vs pc clean), keeping the count-equality REQUIRE green at 94; an introduced run fails at exit 1 and a removed run is info; output is byte-identical across runs"
    requirement: CONTENT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp#doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - an introduced run gates and a removed run does not"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_detectors.cpp#video_detectors - two compare runs are byte-identical"
        status: pass
    human_judgment: false
  - id: D9
    description: "Whole suite (1371) plain and under MEDIADIFF_DESIGNATED_LEG=1, the ten ci.yml lints and the pin-gate script are green; no pre-existing digest line changed"
    verification:
      - kind: other
        ref: "ctest --preset x64-linux (1371/1371, plain and MEDIADIFF_DESIGNATED_LEG=1) + the ten ci.yml lint scripts + scripts/test_gen_corpus_pin_gate.sh + scripts/check_corpus.sh"
        status: pass
    human_judgment: false
  - id: D10
    description: "The fixed detector constants behave on natural video, not just the synthetic test pattern"
    verification: []
    human_judgment: true
    rationale: "Validated on synthetic content only (A10); the real-content review is an open todo in .planning/WINDOWS.md (#47)."

duration: 35min
completed: 2026-10-01
status: complete
---

# Phase 7 Plan 05: Frozen and Black Runs Summary

**Integer-exact 8x8 SSIM and a SIMD-independent 128-wide luma thumbnail feed two new span checks on the one video sweep: frozen runs by SSIM hysteresis (the same span across MPEG-4 with and without B-frames, MJPEG, MPEG-TS and a raw MPEG-2 stream) and black runs that are range- and depth-normalized (limited and full-range black report the same span, full-range luma 17 is not black)**

## Performance

- **Duration:** 35 min
- **Started:** 2026-09-30T22:05Z
- **Completed:** 2026-09-30T22:40Z
- **Tasks:** 3
- **Files modified:** 29 (11 created, 18 modified; eleven new generated fixtures are untracked corpus output)

## Accomplishments

- **A portable integer SSIM.** `ssim_int.h` uses only `int64_t` (the header says why: MSVC has no 128-bit integer, and a last-place difference would make a run boundary platform-dependent). Identical windows score exactly `1 << 24`; the overflow bound is derived in the header and asserted on the worst-case windows; known answers match an independent per-pixel double reference within 1e-6.
- **A thumbnail no CPU can change.** The luma plane is wrapped as a gray frame of the source depth (so no range conversion is applied), scaled by swscale with `SWS_AREA | SWS_ACCURATE_RND | SWS_BITEXACT`, and normalized to 8 bits. The SIMD-equals-C test was run with the flags removed and fails, so it has teeth. Whole-file cost on the 10-minute 1080p reference is 31 s for the entire snapshot (19x realtime), well above PERF-02's 4x.
- **Frozen by hysteresis, for every decoder class.** One 50-frame freeze reports exactly `[2040, 4040)` ms for MPEG-4 `-bf 2`, MPEG-4 `-bf 0`, MJPEG, an MPEG-TS stream copy (1.4 s PTS shift, no declared frame rate) and a raw MPEG-2 elementary stream (no timestamps at all). Comparing any two of them is clean.
- **Black that cannot false-alarm on a range flip.** The same black segment encoded limited-range (black point 16) and full-range (black point 0) reports the same `[1000, 2000)` ms span and `compare` says `pass`; full-range luma 17 reports no black while the same pixels labelled limited are black for the whole two seconds.
- **D-08 holds.** The detectors are tapped in `consume_frame` after the unhashable check and before the stride's store decision, so both lists and their evidence are identical with and without `--sample 3`.
- **Honest skips.** A truncated decode reports `skipped:partial_scan` with the truncation reason (a prefix's spans compared with a full stream would fabricate introduced or removed runs); `--no-content` reports `requires_decode`; a decoded stream is always a real, possibly empty list.
- **Registered and gated.** 94 checks, `--explain` docs, DOC-03 pairs; an introduced run fails at exit 1 and a removed run is `info`.

## Task Commits

1. **Task 1: Integer SSIM and the deterministic 128-wide thumbnail** - `d5cb7b2` (feat)
2. **Task 2: Frozen and black detectors as sinks, two registered span checks, fixtures, DOC-03 pairs** - `1768785` (feat)
3. **Task 3: Doc 06 section 2.3 amendment in the open, constant-review todo, full suite and lints** - `59b8c8d` (docs)

**Plan metadata:** recorded by the closing `docs(07-05)` commit.

## Files Created/Modified

- `src/util/ssim_int.h` - Q24 integer SSIM, plane mean, micro conversion
- `src/probe/video_thumbnail.{h,cpp}` - `ThumbnailScaler`, `thumbnail_height`, `normalize_to_8bit`, `scaler_record`
- `src/probe/video_detectors.{h,cpp}` - hysteresis, both detectors, the fixed constants with their research citations
- `src/probe/video_decode.{h,cpp}` - the tap, the new `StreamVideoDecode` fields, move operations
- `src/analyzers/content/video_runs.cpp` - runs to exact ms spans, the skip ladder, the timing ladder
- `src/core/checks.def`, `docs/checks/content.video.{frozen,black}_runs.md` - registration and `--explain`
- `scripts/gen_corpus.sh` and the two digest files - eleven fixtures, eleven appended lines, eleven provisional names
- `tests/unit/test_ssim_int.cpp` (13 cases), `tests/unit/test_video_detectors.cpp` (9), `tests/integration/test_video_detectors.cpp` (11)
- `claude_docs/06-content-and-size-analysis.md` section 2.3 - the amendment

## Decisions Made

See `key-decisions`. The two later plans lean on: `scaler_record(height)` and `ThumbnailScaler`/`Thumbnail` are the public primitives for 07-08's perceptual score, and `VideoDecodeState::thumb_` already holds each frame's thumbnail at the point 07-08's lockstep tap fires.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The plan's depth normalization overflows 8 bits**
- **Found during:** Task 1 (`video_thumbnail - depth` test)
- **Issue:** `(v + (1 << (b - 9))) >> (b - 8)` gives 256 for a full-scale 10-bit sample (1023), while the plan's own Test 7 asserts 255.
- **Fix:** clamp to 255 in `normalize_to_8bit`; the other asserted values (64 to 16, 940 to 235) are unchanged.
- **Files modified:** `src/probe/video_thumbnail.cpp`, `tests/unit/test_ssim_int.cpp`
- **Committed in:** `d5cb7b2`

**2. [Rule 2 - Missing critical] The plan's bound on the thumbnail allocation does not hold**
- **Found during:** Task 1 (T-07-15)
- **Issue:** The threat model says the thumbnail is at most 128 x (128 x 8192 / 1) rows, but `max_pixels` bounds width x height, not the aspect ratio: a 1 x 67,108,864 source asks for about 8.6e9 rows.
- **Fix:** `kMaxThumbnailHeight` (32768 rows, 4 MiB); `thumbnail_height` returns 0 above it, `ThumbnailScaler::scale` returns false, the decode state latches `detectors_unavailable_reason = thumbnail_unavailable`, and both checks report `insufficient_data` with that reason. Tested in `video_thumbnail - height` and the analyzer ladder.
- **Files modified:** `src/probe/video_thumbnail.{h,cpp}`, `src/probe/video_decode.cpp`
- **Committed in:** `d5cb7b2`, `1768785`

**3. [Rule 2 - Missing critical] A stream with no declared frame rate needs an interval too**
- **Found during:** Task 2 (MPEG-TS leaves `avg_frame_rate` unset at open; 07-01 measured `frame_interval` `{0, 0}`)
- **Issue:** The plan says a span ends at the last run frame's time plus one frame interval, and reports `no_timing_data` only "when neither exists"; for MPEG-TS the interval would not exist, so a remux of a frozen stream would skip while its MP4 source reports a span.
- **Fix:** the interval falls back to the stream's own smallest positive pts step (`min_tick_delta`, tracked in O(1) by the tap), recorded as `interval_source: pts_delta`. `video_frozen.ts` proves it: same span as the MP4 source.
- **Files modified:** `src/probe/video_decode.{h,cpp}`, `src/analyzers/content/video_runs.cpp`
- **Committed in:** `1768785`

**4. [Rule 3 - Blocking] The black family cannot be MP4**
- **Found during:** Task 2 (the plan's own "confirm the pc fixtures carry color_range=pc" step)
- **Issue:** `ffmpeg -i` on the MP4 pc fixtures reads back plain `yuv420p`: the MP4 muxer drops `-color_range` for an MPEG-4 Part 2 stream. A full-range label that does not survive the container would make the range proof vacuous.
- **Fix:** the black and dark-grey fixtures are Matroska (`video_black_base.mkv`, `video_black_tv.mkv`, `video_black_pc.mkv`, `video_dark_pc.mkv`, `video_dark_tv.mkv`), where the read-back shows `yuv420p(tv, ...)` and `yuv420p(pc, ...)`. The plan allowed this for the pc fixtures; the whole family moved so each pair stays in one container. The frozen family stays MP4 as planned.
- **Files modified:** `scripts/gen_corpus.sh`, `tests/integration/coverage_pairs.h`, the tests
- **Committed in:** `1768785`

**5. [Rule 1 - Bug] The plan's dark-grey recipe does not produce luma 17**
- **Found during:** Task 2 (measuring the recipe with the pinned ffmpeg)
- **Issue:** `color=c=0x111111` followed by the encoder's yuv420p conversion gives luma 31, not the plan's "about 17".
- **Fix:** a black yuv420p source raised to exactly `lutyuv=y=17`, then `setrange=full` or `setrange=limited` (relabels the frames without touching a sample), encoded with the matching `-color_range`.
- **Files modified:** `scripts/gen_corpus.sh`
- **Committed in:** `1768785`

**6. [Rule 3 - Blocking] Two files outside the plan's list had to change**
- **Found during:** Task 2 full-suite run
- **Issue:** `tests/golden/list_checks_effective.txt` (registering any check adds rows to it) and `tests/integration/test_audio_corpus_sweep.cpp` (every declared clean pair must report nothing else across the whole report) failed. Neither is in `files_modified`.
- **Fix:** the golden was regenerated with `UPDATE_GOLDENS=1` and its diff is exactly the two new rows. Two `known_exceptions()` entries were added, each matched by exact (baseline, candidate) and citing its recipe difference: `video_frozen.mp4` vs `video_frozen_bf0.mp4` (edit list, profile, frame types, frame hash and the size ids) and `video_black_tv.mkv` vs `video_black_pc.mkv` (`video.color.range` is the pair's point, plus frame hash and size). Each id list was read off the real binary's report.
- **Files modified:** `tests/golden/list_checks_effective.txt`, `tests/integration/test_audio_corpus_sweep.cpp`
- **Committed in:** `1768785`

### Plan statements that did not hold as written

- **Task 3 re-baselined nothing.** `test_timeline_structure.cpp`, `test_timeline_start_duration.cpp` and `test_video_yuvj.cpp` stayed green with both checks registered, so no declared set in them changed. The only re-baselined assertions are the two corpus-sweep exceptions and the list-checks golden above (adding checks, not absorbing a changed value in an existing one). `git diff main -- tests/integration/*.cpp` removes 0 declared ids.
- **`scaler_record()` takes the height.** The record string contains `dst=128x<th>`, so the function is `scaler_record(int height)`.
- **Two fixtures beyond the plan's nine:** `video_frozen.ts` and `video_frozen.m2v`, to prove the two timing paths an MP4 never takes. The m2v is encoded at `-q:v 5`: a first attempt at 1 Mbit/s started the run one frame late (2080 ms) because the pair (51, 52) was coded just below 0.9995, recorded as an honest limit in the `--explain` doc and the recipe comment.
- **The plan's `video_black_*.mp4` acceptance command** ran against the `.mkv` names (`content.video.black_runs` = `pass`).
- **The plan's `ctest --test-dir ... -R "(unit\.ssim_int|...)"` form works as written** (the discovered names are prefixed `unit.`/`integration.`), unlike 07-04's case-name regexes.
- **DOC-03 count.** The running total the plan gives (ninety-four) is what `mediadiff list-checks` prints and what the count-equality REQUIRE verified.

---

**Total deviations:** 6 auto-fixed (2 Rule 1, 2 Rule 2, 2 Rule 3) plus 6 plan statements that did not hold
**Impact on plan:** Every fix is needed for a correct, green, reproducible result; no scope creep. No existing check's value, finding or golden changed.

## TDD Gate Compliance

Tasks 1 and 2 are `tdd="true"` but there is **no RED commit**: the headers, the detectors and the fixtures had to exist before the test files compiled or could run, so tests and code are committed together per task. As in 07-01 through 07-04, this records the violation rather than hiding it.

Substitute evidence that the tests can fail, each run and then restored:

- Removing `SWS_ACCURATE_RND | SWS_BITEXACT` from the thumbnail flags fails `video_thumbnail - simd equals c`.
- Making `black_point_for_range` always return 16 fails four tests (`black depth`, `black point`, `black range normalized`, `dark grey`).
- Every span literal is derived from the fixture recipes (frames 51..100 at 25 fps, the middle second of three, two seconds of luma 17), never read back from the detectors.

## Issues Encountered

- **A black segment is also a frozen run.** Black frames are identical, so `video_black_tv.mkv` reports a frozen span `[1000, 2000)` beside its black span. That is correct (both checks judge their own id) and DOC-03 and the corpus sweep each test per id, but a reader comparing the two lists on those fixtures should expect both.
- The `x64-linux` rebuild after a doc edit relinks because `docs/checks/*.md` feed `explain`; a full rebuild exceeded the tool's 120 s foreground limit, so builds ran with a longer timeout.
- `-Werror=dangling-reference` (GCC 13) rejects a `const std::string&` parameter returning a reference into its other argument when called with a `const char*`; the test helpers take `std::string_view` instead.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None. Every evidence key and field this plan adds is written by production code and read by a test.

## Threat Flags

None. T-07-15 (thumbnail height) is mitigated and tighter than the plan states (see Deviation 2); T-07-16 (integer overflow) is mitigated by the derivation in `ssim_int.h` and `video_detectors.h`, with the worst-case windows asserted through `ssim_detail::window_terms`; T-07-17 (scaler rebuild per geometry change) is accepted as planned. The new stored evidence (`constants`, `thumbnail`, `scaler`, `timing`, `interval_source`, `black_point`) is proven by a `read_snapshot` round trip and known-value assertions; the spans by the same round trip.

## Next Phase Readiness

- **07-08** can reuse `ThumbnailScaler`, `Thumbnail`, `scaler_record(height)` and `ssim_plane_q24` as they stand; `VideoDecodeState::thumb_` already holds the current frame's thumbnail when the lockstep tap will fire (after hash, thumbnail and both detectors). `StreamVideoDecode::scaler_record` and `thumbnail_height` are filled from the first thumbnail for D-04's precondition.
- **07-10** adds the high-depth SSIM variant beside `ssim_int.h`'s 8-bit one (the header says so).
- **07-14/07-15:** eleven new digest lines are workstation-computed (`TZ=UTC taskset -c 0-3`, reproduced identically by a second `gen_corpus.sh` run) and are named in `CORPUS_DIGEST_PROVISIONAL.txt` for the phase's final CI plan to transcribe. No designated-leg golden changed, so nothing new needs the designated leg's confirmation. The constant review is WINDOWS.md #47.
- CONTENT-06 is complete: every condition is proven by this plan's own tests (see `coverage` D3 and D4).
- No blockers.

## Self-Check: PASSED

- All eleven created files exist and all eleven fixtures are present (`check_corpus.sh`: 235 verified); commits `d5cb7b2`, `1768785` and `59b8c8d` exist on `gsd/phase-07-content-quality`.
- Acceptance commands re-run against HEAD: no `__int128`, `double` or `float` in `ssim_int.h`; `SWS_BITEXACT` and `SWS_ACCURATE_RND` in `video_thumbnail.cpp`; one `id =` line per new check in `checks.def`; `fold_pix_fmt_range` in `video_decode.cpp`; 9 lines naming the four detector constants in `video_detectors.h`; `compare video_black_tv.mkv video_black_pc.mkv` reports `content.video.black_runs` = `pass`; one `Amended 2026-09-30 (07-05` line in doc 06; `windows status` lists the new open `todo` naming `src/probe/video_detectors.h`; `git diff main` removes 0 digest lines and 0 declared ids.
- Full ctest 1371/1371 (plain and `MEDIADIFF_DESIGNATED_LEG=1`), the ten ci.yml lints and `test_gen_corpus_pin_gate.sh` green.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
