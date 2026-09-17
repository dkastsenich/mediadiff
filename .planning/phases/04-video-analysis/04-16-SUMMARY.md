---
phase: 04-video-analysis
plan: 16
subsystem: testing
tags: [ffmpeg, mjpeg, colorimetry, fixtures, catch2, corpus-digest]

requires:
  - phase: 04-video-analysis
    provides: "04-02's yuvj signature trio and colour fold fixtures; 04-08's video.pix_fmt/video.color.range fold and its integration/DOC-03 tests; 04-13's CORPUS_DIGEST_PROVISIONAL.txt ledger and lint_corpus_digest_provenance.sh no-rewrite guard"
provides:
  - "video_yuv420p_pc_tagged.mp4: a fixture whose plain yuv420p pixel-format spelling survives alongside an explicit full-range colour tag, genuinely distinct in bytes from video_yuvj420p.mp4"
  - "A load-bearing integration.video_yuvj Test 2 that goes RED when detail::fold_pix_fmt_range is disabled"
  - "A DOC-03 video.pix_fmt clean pair that goes RED under the same mutation"
affects: [04-19]

actuals:
  tokens: 3200
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Stream-copy remux (-c copy) to change a container-level colour tag without re-invoking an encoder that would otherwise renormalise the pixel-format spelling"
    - "Pinning both -movie_timescale and -video_track_timescale on a remux to avoid incidental container.mp4.timescale/edit_list findings unrelated to the property under test"

key-files:
  created:
    - tests/fixtures/video_yuv420p_pc_tagged.mp4 (generated, gitignored; sha256 f700341b108d43bdb335c8a2ed1e8ca0918f3abe6c1c3d4e54d12358e06cc587)
  modified:
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/fixtures/GENERATOR_MANIFEST.json
    - tests/integration/test_video_yuvj.cpp
    - tests/integration/test_doc03_coverage.cpp

key-decisions:
  - "Produced the new fixture as a stream-copy remux of the already-generated video_yuv420p_tv.mp4 rather than a fresh encode, because every encoder in this build that accepts a yuvj* pixel format (mjpeg, ljpeg, amv) normalises a full-range yuv420p request back to a yuvj* name before muxing -- a fresh encode can never produce the second spelling."
  - "Pinned -movie_timescale 1000 and -video_track_timescale 12800 on the remux after confirming empirically that an unpinned remux rewrites the mvhd timescale from 1000 to 12800, which independently fires container.mp4.timescale/edit_list findings that would have destroyed the zero-count assertion."
  - "Left VIDEO-03's REQUIREMENTS.md wording uncorrected: that correction is explicitly 04-19-PLAN.md Task 3's scope, not this plan's, per 04-VERIFICATION.md's Human Decision 3 splitting the four confirmed defects across separate gap-closure plans."

requirements-completed: []

coverage:
  - id: D1
    description: "A fixture pair exists whose two colorimetric spellings (yuvj420p vs plain yuv420p+full-range) survive as genuinely distinct declarations and whose bytes differ"
    requirement: "VIDEO-03"
    verification:
      - kind: integration
        ref: "scripts/check_corpus.sh and scripts/lint_corpus_digest_provenance.sh (all 4 clauses pass); sha256 distinctness confirmed against video_yuvj420p.mp4"
        status: pass
    human_judgment: false
  - id: D2
    description: "integration.video_yuvj Test 2 is load-bearing: it fails when detail::fold_pix_fmt_range is disabled and passes when restored"
    requirement: "VIDEO-03"
    verification:
      - kind: integration
        ref: "tests/integration/test_video_yuvj.cpp - yuvj420p vs yuv420p_pc_tagged (the SAME intent, two spellings) produces ZERO non-pass findings across the whole report"
        status: pass
    human_judgment: false
  - id: D3
    description: "The DOC-03 video.pix_fmt row's clean pair carries real evidence: it fails under the same mutation and passes when restored"
    requirement: "BUILD-08"
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-09-13
status: complete
---

# Phase 4 Plan 16: Non-vacuous VIDEO-03 signature fixture pair Summary

**Replaced a byte-identical "two spellings" fixture pair with a genuine one (a stream-copy remux carrying a full-range colour tag), and proved both the rewritten integration test and the DOC-03 clean pair actually go red without the range fold.**

## Performance

- **Duration:** ~35 min
- **Started:** 2026-09-13T11:44:00Z (approx)
- **Completed:** 2026-09-13T12:19:38Z
- **Tasks:** 3
- **Files modified:** 6 (plus 1 new gitignored fixture)

## Accomplishments

- Added `video_yuv420p_pc_tagged.mp4`: a stream-copy remux of `video_yuv420p_tv.mp4` carrying a full-range `nclx` colour box, whose plain `yuv420p` bitstream survives untouched (unlike a fresh mjpeg encode, which always renormalises a full-range `yuv420p` request to `yuvj420p`). Confirmed distinct in bytes from `video_yuvj420p.mp4` and deterministic across two generation runs.
- Rewrote `integration.video_yuvj` Test 2 to compare against the new fixture, added positive assertions that `video.pix_fmt`/`video.color.range` are present, `pass`, and carry equal baseline/candidate values, and mutation-verified it goes RED with the fold disabled.
- Replaced the DOC-03 `video.pix_fmt` row's clean candidate with the new fixture and mutation-verified that row's clean half also goes RED with the fold disabled.
- Spliced exactly one new line into `tests/golden/CORPUS_DIGEST.txt` (no pre-existing line rewritten, confirmed by `git diff` and `lint_corpus_digest_provenance.sh`'s clause 4) and added the fixture to `CORPUS_DIGEST_PROVISIONAL.txt` in sorted position.

## Task Commits

1. **Task 1: A fixture whose plain yuv420p spelling survives alongside a full-range flag** - `7f75fe0` (feat)
2. **Task 2: Rewrite the signature mirror test and prove it goes red without the fold** - `a846c77` (test)
3. **Task 3: Make the DOC-03 clean pair for video.pix_fmt carry evidence too** - `a786de8` (test)

## Files Created/Modified

- `scripts/gen_corpus.sh` - new `video_yuv420p_pc_tagged.mp4` recipe (stream-copy remux of `video_yuv420p_tv.mp4`, pinned timescales)
- `tests/golden/CORPUS_DIGEST.txt` - one new fixture hash line spliced in, summary re-derived
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - fixture name added in sorted position
- `tests/fixtures/GENERATOR_MANIFEST.json` - `generated_at` timestamp updated by the required corpus regeneration (only field expected to change between runs)
- `tests/integration/test_video_yuvj.cpp` - Test 2 rewritten against the new fixture with added positive assertions; header comment updated to record the byte-identity defect and its cause
- `tests/integration/test_doc03_coverage.cpp` - `video.pix_fmt` row's clean candidate replaced; comment updated

## Decisions Made

See `key-decisions` in the frontmatter above.

## Deviations from Plan

None - plan executed exactly as written. All fixture properties (read-back, sha256 distinctness, determinism, timescale pinning necessity) were verified empirically against the pinned FFmpeg 9.0.1 binary before being committed, matching the plan's own pre-verified values exactly.

## Verification Evidence

**Task 1** (`scripts/check_corpus.sh`, `scripts/lint_corpus_digest_provenance.sh`):
```
check_corpus.sh: clean. Verified 138 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.
lint_corpus_digest_provenance.sh: clause 1 OK ... clause 2 OK ... clause 3 OK (58 entries) ... clause 4 RUN -- all 80 pre-existing line(s) from 8caf1f1 are present verbatim
lint_corpus_digest_provenance.sh: all clauses passed.
```
sha256(`video_yuv420p_pc_tagged.mp4`) = `f700341b108d43bdb335c8a2ed1e8ca0918f3abe6c1c3d4e54d12358e06cc587`, confirmed different from sha256(`video_yuvj420p.mp4`) = `f9d92aff10ff030a4f1bf742d188f53bedcade5a8087a407bb71a2df941c4508`, and identical across two independent `gen_corpus.sh` runs. `git diff -- tests/golden/CORPUS_DIGEST.txt` showed exactly one added fixture line plus the summary replacement, zero removed lines.

**Task 2 mutation check** (`detail::fold_pix_fmt_range` disabled via `if (false && declared_pix_fmt == deprecated)`):
```
765 - ... video.pix_fmt is present and pass, not absent (Failed)
767 - ... yuvj420p vs yuv420p-limited-range ... EXACTLY ONE non-pass finding ... (Failed)
768 - ... yuvj420p vs yuv420p_pc_tagged ... ZERO non-pass findings ... (Failed)
  REQUIRE( non_pass_count == 0 ) with expansion: 1 == 0
25% tests passed, 3 tests failed out of 4
```
Restored, rebuilt, re-ran: `100% tests passed, 0 tests failed out of 4`.

**Task 3 mutation check** (same disabled fold):
```
DOC-03 gap -- declared CLEAN pair did not compare all-pass for: video.pix_fmt
50% tests passed, 1 tests failed out of 2
```
Restored, rebuilt, re-ran: `100% tests passed, 0 tests failed out of 2`.

**Full suite:** `ctest --preset x64-linux --output-on-failure` → `100% tests passed, 0 tests failed out of 770` (6 pre-existing environmental skips unrelated to this plan: `unit.console_vt`, `unit.inspect_container` golden, three `unit.ts_scan_golden` TSDuck goldens, `integration.size_checks` golden).

`bash scripts/assert_corpus_digest.sh` still fails on this workstation (04-13's restored designated-leg hashes, expected per this plan's own `<verification>` instruction — not acted upon).

`git status --porcelain src/analyzers/video/color.cpp` is empty after both mutation checks: the fold-disabling edit was never committed.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`video_yuv420p_pc_tagged.mp4` and its two load-bearing tests are ready for 04-19-PLAN.md, which corrects VIDEO-03's REQUIREMENTS.md wording to match the now-proven two-half behavior (zero findings for the two-spellings case, exactly one for a real range flip) and explicitly cites this plan's rewritten Test 2 as the evidence. VIDEO-03 and BUILD-08 remain in traceability status "Gaps Found"/tracked-pending respectively until their other declaring sibling plans (04-19, 04-20, 04-21, and 04-13 for BUILD-08) also complete — confirmed via `requirements.ready-ids`, which reported both as still blocked, so no requirement checkbox was flipped by this plan alone.

## Self-Check: PASSED

- `tests/golden/CORPUS_DIGEST.txt` contains `video_yuv420p_pc_tagged.mp4` — FOUND
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` contains `video_yuv420p_pc_tagged.mp4` — FOUND
- Commit `7f75fe0` — FOUND in `git log --oneline`
- Commit `a846c77` — FOUND in `git log --oneline`
- Commit `a786de8` — FOUND in `git log --oneline`
- `tests/integration/test_video_yuvj.cpp` references `video_yuv420p_pc_tagged.mp4` — FOUND
- `tests/integration/test_doc03_coverage.cpp` `video.pix_fmt` row names `video_yuv420p_pc_tagged.mp4` as clean candidate — FOUND
- Full ctest suite: 770/770 passed — CONFIRMED
