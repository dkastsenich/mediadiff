---
phase: 07-content-quality
plan: 02
subsystem: content
tags: [ffmpeg, libavcodec, frame-hash, edit-list, cover-art, decode-errors, determinism-class, meta.decode_errors, max-pixels]

requires:
  - phase: 07-content-quality
    plan: 01
    provides: "Pass::video_decode fused in run_packet_scan, StreamVideoDecode, HashChain::element_ticks, DecodeBudget, the approved 07-CHECK-ROSTER.md"
  - phase: 06-audio-analysis
    provides: "run_meta_decode_errors' skip priority and the single place Fingerprint::partial is set"
provides:
  - "Eight fixtures, one per decode edge, each proven against a literal measured with the pinned ffmpeg or ffprobe"
  - "tests/integration/test_video_decode_edges.cpp: 11 cases, TEST_CASE prefix `video_decode_edges - `"
  - "detail::effective_video_class: a class-1 stream with any decode error or corrupt frame records class 2 in both the decode_path record and the decode_path_class evidence"
  - "meta.decode_errors over every decoded video stream at Scope{video, rank} (value = decode_errors + corrupt_frames, both in evidence)"
  - "max_pixels_exceeded is observable (fallback_reason on the requires_decode skip) and the stream is never opened"
  - "geometry_change_count counts transitions, so one resolution change is 1"
affects: [07-03, 07-04, 07-05, 07-14, 07-15]

actuals:
  tokens: 16700
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "One fixture per edge, derived from the already-proven base, with the expected frame count measured by the pinned ffmpeg (-f framemd5) or ffprobe and written into the test as a named literal"
    - "Budget tests at byte precision drive the compare pipeline in-process (set_default_packet_scan_max_bytes, then compare_fingerprints, build_report_model, render_json) because the CLI flag is integer megabytes and compare.cpp re-sets the cap on every run"

key-files:
  created:
    - tests/integration/test_video_decode_edges.cpp
  modified:
    - src/probe/video_decode.h
    - src/probe/video_decode.cpp
    - src/analyzers/content/video_frame_hash.cpp
    - src/analyzers/container/meta.cpp
    - src/analyzers/container/analyzers.h
    - docs/checks/meta.decode_errors.md
    - docs/checks/content.video.frame_hash.md
    - claude_docs/01-core-concepts.md
    - scripts/gen_corpus.sh
    - tools/gen_video_fixtures.py
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/inspect_container.txt
    - tests/integration/CMakeLists.txt
    - tests/unit/test_video_decode.cpp

key-decisions:
  - "geometry_change_count is a count of TRANSITIONS from the previous frame's geometry (07-01 counted every frame that differed from the first frame, which gave 25 for one resolution change)"
  - "A video stream's meta.decode_errors value is decode_errors + corrupt_frames (A6), and a clean stream carries both components in evidence as zeros"
  - "fallback_reason for a max_pixels refusal rides on the measurement's evidence, the same place content.audio.sample_hash puts it, not in a decode_path record (no decode happened, so no record is written)"
  - "The frame-record budget test runs at library level with a byte budget of (unconstrained accounted total - one frame record), because the CLI cannot express it and a mid-stream exhaustion always ends in the packet scan's own partial"

patterns-established:
  - "An oracle count for a hash claim is a named constant whose comment cites the measuring command, never a value read back from mediadiff"
  - "When a new check adds rows to a designated-leg golden, hand-edit only rows whose text is deterministic on every leg, verify them against the local rendering, and log the edit in WINDOWS.md for the CI leg to confirm"

requirements-completed: []  # CONTENT-01 is shared with 07-14 and 07-15; requirements ready-ids reports it blocked, so it stays Pending

coverage:
  - id: D1
    description: "D-06: an MPEG-4 -bf 2 MP4 trimmed with -ss 0.5 -c copy and its Matroska remux report the same content.video.frame_hash digest and element_count, and the MP4's first tick is negative; the trimmed MP4 hashes one more frame than a default decode (87 + 1 = 88, the MKV's framemd5 count)"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - edit list trim equals remux"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - discard kept"
        status: pass
    human_judgment: false
  - id: D2
    description: "Pitfall 1: the EOF drain loses nothing, element_count equals the 100 frames ffmpeg -f framemd5 reports for the B-frame fixture"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - drain"
        status: pass
    human_judgment: false
  - id: D3
    description: "Pitfall 12: a cover-art stream is never decoded or hashed; video_cover.mp4 hashes identically to its base video, emits nothing at the cover's scope and writes one decode_path record"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - cover art ignored"
        status: pass
    human_judgment: false
  - id: D4
    description: "D-05: a mid-stream resolution change keeps hashing (24 + 25 frames, the ffprobe oracle), counts exactly one geometry change, names the first segment's size and reports unusable timestamps"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - geometry change"
        status: pass
    human_judgment: false
  - id: D5
    description: "VIDEO-03: a resolution change between two files is skipped:hash_incomparable naming normalization, while video.resolution reports the change, exit 1"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - resolution change is incomparable"
        status: pass
    human_judgment: false
  - id: D6
    description: "D-09 extended: effective_video_class demotes a class-1 stream with errors or corrupt frames to class 2 and leaves class 2 and 3 alone, over all combinations"
    requirement: CONTENT-01
    verification:
      - kind: unit
        ref: "tests/unit/test_video_decode.cpp#video_decode - effective class"
        status: pass
    human_judgment: false
  - id: D7
    description: "meta.decode_errors over video: a real 0 on a clean stream, errors + corrupt frames on a damaged one with both components in evidence, and a compare of the damaged stream against its base fails on both meta.decode_errors and content.video.frame_hash at exit 1"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - corrupt stream"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - clean zero"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - corrupt vs base"
        status: pass
    human_judgment: false
  - id: D8
    description: "T-07-01: a stream declaring more than kMaxVideoPixels is never opened (attempted false, no frames, no decode_path record), its frame hash and meta.decode_errors skip as requires_decode, the hash's evidence names max_pixels_exceeded, and the fingerprint is not partial"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - max pixels"
        status: pass
    human_judgment: false
  - id: D9
    description: "T-07-02: a byte budget one frame record short of the unconstrained total truncates the hash deterministically (frame_record_budget_exhausted, 99 of 100 frames), the compare reports skipped:hash_incomparable, and two runs render byte-identical JSON"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_decode_edges.cpp#video_decode_edges - record budget"
        status: pass
    human_judgment: false
  - id: D10
    description: "Corpus-wide: the whole suite (1295 tests, also under MEDIADIFF_DESIGNATED_LEG=1) and the ten ci.yml lints are green; no pre-existing CORPUS_DIGEST.txt line changed"
    requirement: CONTENT-01
    verification:
      - kind: other
        ref: "ctest --preset x64-linux (1295/1295, plain and MEDIADIFF_DESIGNATED_LEG=1) + the ten ci.yml lint scripts + scripts/test_gen_corpus_pin_gate.sh + scripts/check_corpus.sh"
        status: pass
    human_judgment: false
  - id: D11
    description: "The hand-added inspect_container golden rows are what the designated x64-linux CI leg will render"
    requirement: CONTENT-01
    verification: []
    human_judgment: true
    rationale: "Verified only against this workstation's rendering; the designated leg's own run is the authority for a fixture-derived golden (logged in WINDOWS.md)."

duration: 26min
completed: 2026-09-30
status: complete
---

# Phase 7 Plan 02: Video Decode Edge Cases Summary

**Edit-list frames, the EOF drain, cover art and a mid-stream resolution change each proven on their own fixture against a pinned-ffmpeg oracle; decode errors force class 2 and reach `meta.decode_errors` over video; declared-size and record-budget bounds are observable and deterministic**

## Performance

- **Duration:** 26 min
- **Started:** 2026-09-30T20:57Z
- **Completed:** 2026-09-30T21:23Z
- **Tasks:** 3 (Task 3 needed no code change)
- **Files modified:** 17 (1 created, 16 modified, eight new fixtures generated by `gen_corpus.sh`)

## Accomplishments

- **D-06 and the drain hold on independent oracles.** `video_trim.mp4` hashes 88 frames, one more than the 87 a default decode yields, and equals its Matroska remux's digest. Removing the `AV_PKT_FLAG_DISCARD` clear fails both trim tests. The B-frame base hashes the 100 frames `-f framemd5` reports.
- **Cover art and geometry change behave.** The attached picture emits nothing and writes no `decode_path` record. A 352x288 to 320x240 elementary stream keeps hashing and reports one geometry change.
- **Damage is counted, classed and reported.** `effective_video_class` is the single demotion rule, applied to the record and the evidence. `meta.decode_errors` now measures each decoded video stream as `decode_errors + corrupt_frames`. The damaged fixture (packet 40) reads 1 against 0 for its base, and the compare exits 1 on both checks.
- **Hostile input hits named bounds.** An SPS declaring 8208x8192 is never opened (`max_pixels_exceeded`). A byte budget one frame record short truncates the hash with `frame_record_budget_exhausted`, byte-identically across runs.
- **Whole suite 1295/1295** (1283 baseline + 12 new), also under `MEDIADIFF_DESIGNATED_LEG=1`; the ten lints and the pin-gate script pass; `CORPUS_DIGEST.txt` has added lines only (80 of 80 historical lines intact).

## Task Commits

1. **Task 1: Edit-list frames, EOF drain, cover art and geometry change** - `9a36bcb` (test)
2. **Task 2: Decode errors force class 2 and reach meta.decode_errors; hostile-input bounds** - `5d74bfb` (feat)
3. **Task 3: Corpus-wide re-baselining** - `be0a9d8` (docs; the re-baselining itself was a no-op, see below)

**Plan metadata:** recorded by the closing `docs(07-02)` commit.

## Files Created/Modified

- `tests/integration/test_video_decode_edges.cpp` - 11 cases covering every edge in the plan
- `src/probe/video_decode.{h,cpp}` - `effective_video_class`; `geometry_change_count` now counts transitions
- `src/analyzers/content/video_frame_hash.cpp` - effective class on record and evidence; `fallback_reason` on the skip
- `src/analyzers/container/{meta.cpp,analyzers.h}` - `meta.decode_errors` over video scopes, `Pass::video_decode` in `required_passes`
- `docs/checks/{meta.decode_errors,content.video.frame_hash}.md`, `claude_docs/01-core-concepts.md` - A6's counting unit, the error-class rule, the bounds, the amendment
- `scripts/gen_corpus.sh`, `tools/gen_video_fixtures.py` - eight recipes (`make_h264_huge_dims` is the new hand-written output)
- `tests/golden/CORPUS_DIGEST{,_PROVISIONAL}.txt` - eight appended digest lines and ledger names, none rewritten
- `tests/golden/inspect_container.txt` - six new `meta.decode_errors video[N]` rows

## Decisions Made

See `key-decisions`. The two later plans rely on: a video `meta.decode_errors` value always carries both components in evidence, and `geometry_change_count` is a transition count.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `geometry_change_count` counted frames, not changes**
- **Found during:** Task 1, `video_decode_edges - geometry change`
- **Issue:** 07-01 counted every frame whose geometry differed from the FIRST frame's, so one resolution change read 25. The plan's must_have requires 1.
- **Fix:** count a transition from the PREVIOUS frame's geometry; `width`/`height`/`normalization` stay the first frame's.
- **Files modified:** `src/probe/video_decode.h`, `src/probe/video_decode.cpp`
- **Committed in:** `9a36bcb`

**2. [Rule 3 - Blocking] The cover-art recipe with `-frames:v:1 1` truncates the video**
- **Found during:** Task 1 fixture check
- **Issue:** the plan's shape puts the streams in one sync queue and ends the stream-copied video at the picture's timestamp (25 of 100 packets).
- **Fix:** bound the picture with the one-frame `color` source alone (`duration=1`, `rate=1`); no `-frames` flag. The comment records the measurement.
- **Files modified:** `scripts/gen_corpus.sh`
- **Committed in:** `9a36bcb`

**3. [Rule 3 - Blocking] The geometry-change halves live in a `mktemp` directory**
- **Found during:** Task 1 (`check_corpus.sh` extracts every literal `$OUT_DIR/<name>` token and would demand the removed temporaries)
- **Fix:** write the two MPEG-2 halves under `mktemp -d`, `cat` them into the output, remove the directory.
- **Committed in:** `9a36bcb`

**4. [Rule 3 - Blocking] 16384x16384 cannot be declared**
- **Found during:** Task 2, `video_huge_dims.h264`
- **Issue:** libavcodec's own `av_image_check_size` rejects that size before the SPS is accepted, so the stream reports 0x0 and the pre-open bound is unreachable. 12288x12288 was accepted but made `inspect` allocate 420 MB.
- **Fix:** the SPS declares 513x512 macroblocks (8208x8192), one column past `kMaxVideoPixels`, which is also the smallest allocation (195 MB). Existing H.264 fixture bytes are unchanged (asserted by hash).
- **Files modified:** `tools/gen_video_fixtures.py`
- **Committed in:** `5d74bfb`

**5. [Rule 3 - Blocking] The record-budget test cannot go through the CLI**
- **Found during:** Task 2, `video_decode_edges - record budget`
- **Issue:** `--probe-memory-budget-mb` is integer megabytes and `compare.cpp` re-sets the cap on every run. A mid-stream frame-budget exhaustion is always followed by the packet scan's own partial (the next packet record needs more bytes than the failed frame record left), so the check reports `skipped:partial_scan`, not `truncated`. Measured with a tuned CLI fixture: only one frame count (5462 of a 16x16 stream) gave `truncated`, and it depends on `sizeof(PacketRecord)`.
- **Fix:** the test drives the same pipeline in-process with a byte budget of (an unconstrained sweep's `accounted_bytes` minus one `kVideoFrameRecordBytes`), so every packet fits and the last drain frame overflows. The value comes from a measurement, never `sizeof`, so it holds on every leg.
- **Files modified:** `tests/integration/test_video_decode_edges.cpp`
- **Committed in:** `5d74bfb`

**6. [Rule 3 - Blocking] The `inspect_container` designated-leg golden needed its new rows**
- **Found during:** Task 3 full run under `MEDIADIFF_DESIGNATED_LEG=1`
- **Issue:** the golden renders the meta section, which now has a `meta.decode_errors video[N]` row per video stream. The plan's file list omits it, and off the CI leg the test only skips, so the x64-linux leg would have gone red.
- **Fix:** hand-edited six rows (value 0, evidence `{"decode_errors":0,"corrupt_frames":0}`), verified against this workstation's rendering under `MEDIADIFF_DESIGNATED_LEG=1`. `UPDATE_GOLDENS` is refused for this file, as the README requires. The values are counts of decode errors on clean streams, which do not vary by architecture. Logged in `.planning/WINDOWS.md` for the designated leg to confirm.
- **Files modified:** `tests/golden/inspect_container.txt`
- **Committed in:** `5d74bfb`

### Plan statements that did not hold as written

- **A7 did not materialize; Task 3 re-baselined nothing.** The plan expected Phase 4's parse-only Annex-B fixtures to exit 66 in a decoding compare. Measured on all eight: every self-compare exits 0. The H.264 streams decode (libavcodec conceals the garbage slices, returns success and flags nothing, so `meta.decode_errors` is 0), and the two HEVC streams decode zero frames with no error (`insufficient_data`, never `undecodable`). The full suite was already green after Task 2, so no declared set or exit-code assertion changed and there is no list of re-baselined assertions. Task 3's commit is the error-class documentation amendment.
- **Task 3's acceptance `git diff main -- tests/ | grep -E '^\+.*--no-content'` prints 4, not 0.** All four lines are 07-01's own `test_video_hash.cpp` (it tests `--no-content` itself); against 07-01's HEAD (`0cbfa45`) the count is 0. No 07-02 test uses `--no-content`, and no declared id was removed.
- **`video_geom_change.m2v` decodes 49 of its 50 packets.** libavcodec drops the last frame of the first sequence at the size change (`ffprobe -show_frames` agrees: 24 + 25). The test's oracle is that count, not 50.
- **The must_have says `fallback_reason` is in the decode-path record; Test 5 and the action say measurement evidence and no record.** I followed Test 5 and the action (two of three, and the audio precedent).
- **`detail::checked_mul` is not used for the pre-open product.** Two `int` dimensions widened to `int64_t` cannot overflow, so the existing widening multiply from 07-01 stays.

---

**Total deviations:** 6 auto-fixed (1 Rule 1, 5 Rule 3) plus 5 plan statements that did not hold
**Impact on plan:** All needed for a green suite, a reproducible corpus or an honest oracle; no scope creep and no behaviour change beyond the geometry counter and the new video measurement the plan asked for.

## TDD Gate Compliance

Tasks 1 and 2 are `tdd="true"`, but there is **no RED commit**: the fixtures, the new test file and the symbols under test (`effective_video_class`) had to exist before anything compiled, and I committed tests and code together per task. That violates the plan's RED-then-GREEN sequence, and I am recording it rather than hiding it. Task 1's implementation from 07-01 already satisfied five of its six tests on first run, so those tests were written against existing behaviour.

Substitute evidence that the tests can fail: removing the `AV_PKT_FLAG_DISCARD` clear fails `edit list trim equals remux` and `discard kept` (run, then restored). The geometry test failed on its first run (25 != 1) before the counter fix. Every frame-count literal comes from `ffmpeg -f framemd5` or `ffprobe`, not from mediadiff.

## Issues Encountered

- **`decode_path`-adjacent finding for the user:** with one budget shared by packet, access-unit and frame records, `frame_record_budget_exhausted` with a complete packet scan is reachable only when the last frame records (the EOF drain) overflow. A mid-stream exhaustion is followed at once by the packet scan's own partial, so every decode-dependent check of the stream skips `partial_scan`. Both outcomes are deterministic and honest, and `docs/checks/content.video.frame_hash.md` now says so. Whether frames should get a sub-budget so the hash truncates gracefully is a design question, left open and logged in `WINDOWS.md`.
- `meta.decode_errors` findings render their unit as `bytes` ("delta +1/1bytes exceeds tolerance") for this count-unit check. It predates this plan (the audio scopes do the same) and is out of scope.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None. `docs/checks/content.video.frame_hash.md` still describes `--sample N` before the option exists; that is 07-01's deliberate stub and 07-04's to fill.

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: resource-exhaustion | src/probe/demux_session.cpp (pre-existing) | `avformat_find_stream_info` opens a decoder with no `max_pixels`, so a stream declaring a large size allocates before the decode pass's pre-open bound can refuse it. Measured: 195 MB RSS for 8208x8192 and 420 MB for 12288x12288 through `inspect`. libavcodec's own size check caps the declarable size, so the exposure is bounded (roughly a gigabyte), but it is outside T-07-06's scope. |

## Next Phase Readiness

- 07-03 can rely on `geometry_change_count` being a transition count, on `element_ticks` being empty for a raw elementary stream, and on a negative first tick for an edit-list trim.
- 07-15 fills the class-1 table knowing `effective_video_class` already demotes an error-bearing stream, tested over every combination.
- The five 07-01 and eight 07-02 digest lines are workstation-computed and listed in `CORPUS_DIGEST_PROVISIONAL.txt` for the phase's final CI plan to transcribe. Nothing blocks.

## Self-Check: PASSED

- `test_video_decode_edges.cpp`, all eight fixtures (`video_trim.mp4`, `video_trim.mkv`, `video_cover.mp4`, `video_geom_change.m2v`, `video_hash_small.mp4`, `video_corrupt_mpeg4_base.mkv`, `video_corrupt_mpeg4.mkv`, `video_huge_dims.h264`) exist; `check_corpus.sh` verifies 218 fixtures.
- Commits `9a36bcb`, `5d74bfb` and `be0a9d8` exist on `gsd/phase-07-content-quality`.
- Acceptance commands re-run against HEAD: `effective_video_class` appears twice in `video_frame_hash.cpp`, `Pass::video_decode` in `analyzers.h` and `meta.cpp`, `max_pixels_exceeded` once in `video_decode.h`, the corrupt pair compare exits 1 naming `meta.decode_errors` at a video scope, full ctest 1295/1295 (plain and designated-leg), the ten lints and the pin-gate script pass, `git diff main` shows 0 removed digest lines and 0 removed ids.

---
*Phase: 07-content-quality*
*Completed: 2026-09-30*
