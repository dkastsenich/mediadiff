---
phase: 04-video-analysis
plan: 09
subsystem: video-analysis
tags: [ffmpeg, h264, hevc, nal-parsing, exp-golomb, gop, catch2, cmake]

requires:
  - phase: 04-video-analysis
    provides: "plan 04-01's ParserScan (AccessUnitRecord::first_vcl_nal_type/nal_type_mask), plan 04-05's hand-constructed H.264/HEVC fixture corpus and SPS BitWriter"
provides:
  - "video.gop.idr_interval and video.gop.closed: IDR cadence and open/closed GOP classification derived from NAL types, never the key_frame boolean"
  - "video.gop.refs: H.264 SPS max_num_ref_frames via a hand-written bounded Exp-Golomb bit reader"
  - "video.frame_types: I/P/B picture-type distribution histogram with a VIDEO-12 no-parser degradation path (keyframe-flag two-bin fallback)"
  - "kPacketFlagKeyframe hoisted to a shared location (probe/packet_scan.h)"
  - "Hand-verified NAL-type classification table (tests/unit/test_gop_classification.cpp) and four DOC-03 fixture pairs"
affects: [video-analysis, probe, doc03-coverage]

actuals:
  tokens: 26808
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Bounded Exp-Golomb bit reader over an H.264 SPS RBSP, symmetric with plan 04-05's writer, bounds-checked against the NAL's own declared length on every read"
    - "detail::-exposed classify_access_unit/classify_gop seam, driven directly from unit tests over hand-built AccessUnitRecord sequences with no fixture file"
    - "Deliberate one-off exception to the project's per-file-duplication convention: kPacketFlagKeyframe hoisted to probe/packet_scan.h because two files need the byte-identical value"

key-files:
  created:
    - src/analyzers/video/frame_types.cpp
    - docs/checks/video.gop.idr_interval.md
    - docs/checks/video.gop.closed.md
    - docs/checks/video.gop.refs.md
    - docs/checks/video.frame_types.md
    - tests/unit/test_gop_classification.cpp
  modified:
    - src/analyzers/video/gop.cpp
    - src/analyzers/video/analyzers.h
    - src/probe/parser_scan.h
    - src/probe/parser_scan.cpp
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/analyzers/container/mp4.cpp
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "Classification reads the leading VCL NAL type only, never AccessUnitRecord::key_frame -- HEVC's own parser sets key_frame=1 for ANY IRAP including CRA_NUT (04-RESEARCH.md Priority Finding 3), so a boolean-only classifier cannot distinguish an open GOP from a closed one"
  - "The H.264 SPS reader implements full correctness for the high-profile chroma/bit-depth block and all three pic_order_cnt_type branches (0/1/2), not merely the narrow shape plan 04-05's own writer/fixtures exercise -- a reader limited to the fixture's shape would silently misread the vast majority of real-world H.264 content (profile_idc=100, poc_type=0), a P0 false-positive risk"
  - "HEVC and mpeg4/mpeg2video are treated identically for video.gop.refs (skipped:no_parser, codec named in evidence) since no acceptance criterion or fixture exercises HEVC SPS reference-frame extraction -- a documented scope decision, not a defect"
  - "kPacketFlagKeyframe hoisted from mp4.cpp into probe/packet_scan.h (public, next to PacketRecord) as a deliberate, narrow exception to this project's per-file-duplication convention (04-PATTERNS.md), made only because frame_types.cpp's packet-flags fallback and mp4.cpp both need the byte-identical constant and drift risk was the stated concern"
  - "video_h264_closed.h264 had no existing byte-identical copy fixture; added the cp to scripts/gen_corpus.sh and regenerated tests/golden/CORPUS_DIGEST.txt rather than reusing an unrelated codec family's clean pair, which would prove a different property than 'this check passes when nothing changed'"
  - "PROBE-03 intentionally NOT marked complete despite passing requirements.ready-ids -- see 'Requirement Marking' section below"

patterns-established:
  - "A classification function is exposed via detail:: taking a std::span of records and returning a plain result struct, so the unit test drives it directly without any fixture file or av_parser_parse2 call -- mirrors detail::compute_peak_window's existing precedent"
  - "Every NAL-type constant used for classification carries an inline comment citing its exact research source line, never a bare literal"

requirements-completed: [VIDEO-05, VIDEO-12]

coverage:
  - id: D1
    description: "video.gop.idr_interval and video.gop.closed classify GOP structure from NAL types (H.264 IDR type 5, HEVC IDR_W_RADL/IDR_N_LP types 19/20 vs CRA/BLA range 16-23), never from key_frame"
    requirement: VIDEO-05
    verification:
      - kind: unit
        ref: "tests/unit/test_gop_classification.cpp -- HEVC IDR_W_RADL row and HEVC CRA_NUT row, side by side"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.gop.idr_interval/video.gop.closed declared pairs"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.gop.refs reads H.264 SPS max_num_ref_frames via a hand-written bounded Exp-Golomb reader; skips no_parser for codecs with no SPS concept"
    requirement: VIDEO-05
    verification:
      - kind: unit
        ref: "tests/unit/test_gop_classification.cpp -- read_h264_max_num_ref_frames (7 cases, all three pic_order_cnt_type branches, high-profile block, truncation)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.gop.refs declared pair (refs1 vs refs4)"
        status: pass
    human_judgment: false
  - id: D3
    description: "video.frame_types reports I/P/B picture-type histogram as raw counts; degrades to a two-bin keyframe/non_keyframe split from packet flags for codecs with no registered parser (VIDEO-12), never a skip"
    requirement: VIDEO-12
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- video.frame_types declared pair; manual CLI verification of video_noparser.mkv's degraded two-bin evidence"
        status: pass
    human_judgment: false
  - id: D4
    description: "PROBE-03 (parser-scan overhead under 10%) -- structurally ready per requirements.ready-ids, but deliberately withheld"
    requirement: PROBE-03
    verification: []
    human_judgment: true
    rationale: "04-03's own measurement recorded 46% overhead, directly contradicting the requirement's literal under-10% clause. Marking this complete would misrepresent a known, measured contradiction as satisfied; a human must decide whether to relax the requirement text, re-measure after further optimization, or accept the overhead as-is before this can be marked."

duration: ~50min
completed: 2026-09-13
status: complete
---

# Phase 04 Plan 09: GOP Structure and Frame-Type Distribution Summary

**IDR cadence and open/closed GOP classification read directly from H.264/HEVC NAL types (never the `key_frame` boolean), a hand-written bounded Exp-Golomb SPS reader for reference-frame count, and an I/P/B picture-type histogram with an explicit no-parser degradation path.**

## Performance

- **Duration:** ~50 min (spanning task 1 through task 3 commits, plus mutation-testing verification)
- **Completed:** 2026-09-13
- **Tasks:** 3
- **Files modified:** 20 (6 created, 14 modified)

## Accomplishments

- `video.gop.idr_interval` and `video.gop.closed` classify GOP structure by reading the leading VCL NAL type of every access unit — H.264 IDR (type 5) vs non-IDR intra (type 1 with picture-type I), HEVC IDR_W_RADL/IDR_N_LP (19/20) vs the CRA/BLA/reserved-IRAP family (16-23 excluding 19/20) — proven to distinguish streams that are indistinguishable by `key_frame` alone (HEVC's own parser sets `key_frame=1` for every IRAP, IDR or CRA).
- `video.gop.refs` reads H.264 SPS `max_num_ref_frames` directly from the bitstream via a hand-written, bounds-checked Exp-Golomb reader (`RbspBitReader`, `read_h264_max_num_ref_frames`) — no public libav API exposes this value. Handles all three `pic_order_cnt_type` branches and the high-profile chroma/bit-depth block; refuses (returns `nullopt`) on a scaling-list-present SPS or a truncated payload rather than fabricating a value.
- `video.frame_types` reports the I/P/B picture-type distribution as a raw-count histogram (the `dist` comparator normalizes at comparison time), with VIDEO-12's own degradation path: a codec with no registered parser still emits a real two-bin `keyframe`/`non_keyframe` histogram derived from the packet scan's own keyframe flag, evidenced as degraded rather than skipped.
- Extended `tests/unit/test_gop_classification.cpp` to a 23-case hand-verified table (H.264 IDR-only, H.264 IDR-then-non-IDR-I, HEVC IDR-only, HEVC CRA-led, HEVC BLA-led, no-NAL sentinel, a side-by-side IDR-vs-CRA test with identical `key_frame` flags, and bound-exceeded/at-boundary tests for `kMaxAccessUnitsForGopClassification`).
- Registered four DOC-03 fixture pairs (`video.gop.idr_interval`, `video.gop.closed`, `video.gop.refs`, `video.frame_types`), each proven empirically against the real linked binary. Added a byte-identical `video_h264_closed_copy.h264` to the corpus (it didn't exist yet) and regenerated `tests/golden/CORPUS_DIGEST.txt`.

## Task Commits

Each task was committed atomically:

1. **Task 1: IDR cadence and open-versus-closed classification from NAL types** - `4615438` (feat)
2. **Task 2: video.gop.refs, video.frame_types, and the no-parser degradation** - `d053f26` (feat)
3. **Task 3: The hand-verified classification table and four DOC-03 pairs** - `9ba54ba` (test)

**Plan metadata:** commit to follow this SUMMARY.

## Files Created/Modified

- `src/analyzers/video/gop.cpp` - Extended with `emit_gop_idr_interval`, `emit_gop_closed`, `emit_gop_refs`, `detail::classify_access_unit`, `detail::classify_gop`
- `src/analyzers/video/frame_types.cpp` (new) - `video.frame_types` analyzer, parser-driven histogram plus VIDEO-12 packet-flags fallback
- `src/analyzers/video/analyzers.h` - `RandomAccessKind`, NAL type constants, `GopClassificationResult`, `video_frame_types_analyzer()` declaration
- `src/probe/parser_scan.{h,cpp}` - `RbspBitReader`, `strip_emulation_prevention`, `read_h264_max_num_ref_frames`, `StreamParserScan::ref_frame_count`, `picture_type_name`
- `src/probe/packet_scan.{h,cpp}` - `kPacketFlagKeyframe` hoisted here (public); `pstream.ref_frame_count` wiring
- `src/analyzers/container/mp4.cpp` - `kPacketFlagKeyframe` redeclaration removed, now references `packet_scan.h`'s
- `src/probe/orchestrator.cpp` - `video_frame_types_analyzer()` registered in `all_analyzers()`
- `src/core/checks.def` - `video.gop.idr_interval`, `video.gop.closed`, `video.gop.refs`, `video.frame_types` registered
- `docs/checks/video.gop.idr_interval.md`, `docs/checks/video.gop.closed.md`, `docs/checks/video.gop.refs.md`, `docs/checks/video.frame_types.md` (new)
- `CMakeLists.txt` - `src/analyzers/video/frame_types.cpp` added to `libmediadiff`
- `tests/unit/test_gop_classification.cpp` (new) - 23-case hand-verified table
- `tests/unit/CMakeLists.txt` - test file registered
- `tests/integration/test_doc03_coverage.cpp` - four declared pairs added, running total comment updated to 51
- `scripts/gen_corpus.sh` - `cp` for `video_h264_closed_copy.h264` added
- `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/list_checks_effective.txt` - regenerated

## Decisions Made

See `key-decisions` in frontmatter. Additionally:

- **`inspect --json`'s evidence rendering limitation** (pre-existing, out of this plan's `files_modified` scope): `src/cli/commands/inspect_render.h`'s `render_inspect_json` does not render the `evidence` field at all — only the text `-v` mode does (via `evidence_to_text`). Several of this plan's own acceptance criteria are worded as `--json` showing evidence; those were verified instead via the text `-v` mode, which does show it correctly (confirmed: identical `key_frame` counts between `video_hevc_cra.hevc` and `video_hevc_idr.hevc`, codec-named skip evidence for `mpeg4`/`video_noparser.mkv`). Not fixed here since `inspect_render.h` is outside this plan's declared files and the underlying evidence data itself is correct and complete — only its `--json` rendering path is incomplete, a defect for a future plan to address if `--json` evidence rendering becomes a requirement.

## Mutation Testing (test_evidence_guard)

Every load-bearing test below was mutation-checked: the logic under test was disabled/altered, rebuilt, confirmed the test FAILS, then restored via file copy and rebuilt to confirm the full suite passes again.

| # | Mutation | File | Result before fix | Fix | Result after fix |
|---|----------|------|--------------------|-----|-------------------|
| 1 | `pic_order_cnt_type == 0` and `== 1` branches disabled (`if (false && ...)`) | `src/probe/parser_scan.cpp` | Test 146 (poc_type=0) did NOT fail — original test used `log2_max_pic_order_cnt_lsb_minus4=2` and `max_num_ref_frames=2` (identical values), so skipping the field coincidentally decoded the same value | Changed `log2_max_pic_order_cnt_lsb_minus4` to a distinct value (9) from `max_num_ref_frames` (2) | Both Test 146 and 147 correctly failed with wrong decoded values; restored, all pass |
| 2 | `is_high_profile_idc(profile_idc)` gate disabled (`if (false && is_high_profile_idc(...))`) | `src/probe/parser_scan.cpp` | Test 144 (high-profile SPS) did NOT fail — original test used degenerate values (`bit_depth_luma_minus8=0`, `bit_depth_chroma_minus8=0`, `qpprime=0`, `max_num_ref_frames=4`) that coincidentally re-converged onto the same decoded value | Changed to distinctive values (`bit_depth_luma_minus8=5`, `bit_depth_chroma_minus8=3`, `qpprime=1`, `max_num_ref_frames=11`) | Test correctly failed (decoded `3` instead of `11`, confirming bit misalignment); restored via `cp` from backup, all 19 tests pass |
| 3 | HEVC branch of `classify_access_unit` collapsed: `cra_or_bla` classification removed, every IRAP (16-23) returns `idr` — simulating a key_frame-boolean-based classifier (T-4-42) | `src/analyzers/video/gop.cpp` | N/A (first attempt) | Direct mutation, no test fix needed | 5 tests correctly failed, including the side-by-side IDR-vs-CRA test and the BLA/CRA `classify_access_unit` direct tests; restored via `cp` from backup, all 23 pass |

Two of the three mutation attempts (#1, #2) initially passed under mutation due to coincidental bit-pattern collisions in the original hand-built test field values — both were caught by inspection (not by the mutation itself silently succeeding unnoticed) and fixed by choosing bit-pattern-distinct field values before being recorded here as resolved. Mutation #3, targeting the plan's own single most safety-critical property (T-4-42, "an open GOP reported as closed"), was caught on the first attempt.

## Fixture Pair Verification (test_evidence_guard)

Every DOC-03 fixture pair was confirmed via `sha256sum` and read back through the real linked binary before being registered:

| Check | Trigger pair | SHA-256 distinct? | Clean pair | SHA-256 identical? | Read-back result |
|---|---|---|---|---|---|
| `video.gop.idr_interval` | closed vs idr48 | yes (`d71bace3...` vs `652ec88d...`) | closed vs closed_copy | yes | trigger: `fail` (24 vs 48, +24/1% exceeds 10% tolerance); clean: `pass` |
| `video.gop.closed` | closed vs open | yes (`d71bace3...` vs `6330a5d9...`) | closed vs closed_copy | yes | trigger: `fail` (closed vs open, idr_count 4 vs 1, non_idr_intra_count 0 vs 3); clean: `pass` |
| `video.gop.refs` | refs1 vs refs4 | yes (`d71bace3...` vs `cde9424b...`) | closed vs closed_copy | yes | trigger: `warn` (1 vs 4); clean: `pass` |
| `video.frame_types` | base vs bf3 | yes (`4e69daa0...` vs `bea82ca8...`) | base vs base_copy | yes | trigger: `warn` (I=3,P=97 vs I=3,P=23,B=74, worst bin B exceeds tolerance); clean: `pass` |

Note: `video_h264_refs1.h264` happens to share a SHA-256 with `video_h264_closed.h264` (`d71bace3...`) — both fixtures declare `max_num_ref_frames=1` in their SPS and were generated with identical encode parameters. This is a real, verified property of those two fixtures (confirmed via `video.gop.refs` reading `1` from both), not a defect; it does not affect the `refs1`-vs-`refs4` trigger pair, which are distinct files.

Profile overrides also confirmed via CLI: `video.frame_types` resolves to `warn`/10% under `hw-encoder`, `fail`/5% under both `strict-bitexact` and `remux`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Full H.264 SPS field-layout correctness beyond the fixture's own narrow shape**
- **Found during:** Task 2 (SPS reference-frame reader)
- **Issue:** Plan 04-05's own writer/fixtures only exercise `pic_order_cnt_type=2` with Baseline profile — a reader limited to only that shape would misdecode `max_num_ref_frames` for the vast majority of real-world H.264 content (High profile, `pic_order_cnt_type=0`), a false-positive risk this project's own "false positives are P0 bugs" ethos treats as critical.
- **Fix:** Implemented all three `pic_order_cnt_type` branches (0/1/2) and the high-profile chroma_format_idc/bit-depth/scaling-list-flag block, deliberately refusing (nullopt) only on the narrow, undocumented `seq_scaling_matrix_present_flag=1` case.
- **Files modified:** `src/probe/parser_scan.cpp`
- **Verification:** 7 unit tests covering every branch; mutation-tested (see table above).
- **Committed in:** `d053f26` (Task 2 commit)

**2. [Rule 2 - Missing Critical] kPacketFlagKeyframe hoisted to a shared header**
- **Found during:** Task 2 (frame_types.cpp's VIDEO-12 fallback)
- **Issue:** `frame_types.cpp`'s packet-flags fallback and `mp4.cpp` both need the byte-identical `kPacketFlagKeyframe` value; per-file duplication (this project's stated convention) risks silent drift between the two on this one shared bit-flag value.
- **Fix:** Hoisted the constant from `mp4.cpp` into `probe/packet_scan.h` (public, next to `PacketRecord`), with `mp4.cpp` now referencing it.
- **Files modified:** `src/probe/packet_scan.h`, `src/analyzers/container/mp4.cpp`
- **Verification:** Build clean; `grep -c 'kPacketFlagKeyframe' src/analyzers/` confirms reference, not redeclaration.
- **Committed in:** `d053f26` (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (both Rule 2 — missing critical correctness). No scope creep; both were necessary to meet this plan's own stated correctness/no-drift requirements.

## Issues Encountered

- **Mutation-test coincidental collisions** (see Mutation Testing table above): two of the SPS reader's hand-built test cases initially used field values that happened to produce identical bit patterns whether or not the mutated branch executed, silently defeating the mutation check on first attempt. Resolved by choosing bit-pattern-distinct values for every field in the affected tests.
- **Plan-authoring inconsistency (already resolved in Task 1, restated here for completeness):** Task 1's own `<verify>` requires `tests/unit/test_gop_classification.cpp` to exist and pass, but Task 1's `<files>` list doesn't mention creating it (only Task 3's action text says "Create..."). Resolved by having Task 1 create the file with its own subset of tests satisfying Task 1's `<verify>`, and Task 3 extend it further per its own action text.
- **`inspect --json`'s evidence rendering gap** — see Decisions Made above; a pre-existing, out-of-scope limitation, not introduced by this plan.

## Known Stubs

None. All four registered checks emit real, non-vacuous values or explicit, evidenced skips — no hardcoded empty values, no placeholder text, no unwired data sources.

## Threat Flags

None. All four STRIDE entries this plan's own `<threat_model>` assigns `mitigate` (T-4-38 through T-4-43) are implemented and covered by the mutation-tested unit tests and bounds-checked reader above; no new surface outside the threat model was introduced.

## User Setup Required

None - no external service configuration required.

## Requirement Marking

Per this plan's `requirement_marking_guard`, `gsd_run query requirements.ready-ids` was run against this plan's three declared requirement IDs (PROBE-03, VIDEO-05, VIDEO-12) AFTER this SUMMARY.md was written, and only IDs confirmed in the gate's `.ready[]` array were passed to `requirements.mark-complete`.

**PROBE-03 held, regardless of gate result:** even if `requirements.ready-ids` reports PROBE-03 structurally ready (its declared `key_links`/artifacts from 03-xx plans are all present), it is deliberately NOT marked complete. 04-03-SUMMARY.md's own measurement recorded 46% parser-scan overhead over plain packet-scan — directly contradicting PROBE-03's literal "under 10% overhead" text. Marking this complete would misrepresent a known, measured contradiction as satisfied. This requires a human decision (relax the requirement's stated threshold, pursue further optimization, or accept the overhead as an intentional trade-off) before PROBE-03 can honestly be marked complete.

**VIDEO-05** — every named sub-check verified working with non-vacuous tests: `gop.length` (04-01, prior plan), `idr_interval` (Task 1, this plan), `closed` classification via NAL types (Task 1), `refs` (Task 2), `frame_types` distribution (Task 2). Marked complete if confirmed in `.ready[]`.

**VIDEO-12** — both named sub-checks verified: the `skipped:no_parser` GOP path (Task 1/2, confirmed via `video_noparser.mkv`/`video_base.mp4` codec-named skips) AND the keyframe-flag `frame_types` fallback (Task 2, confirmed via `video_noparser.mkv`'s two-bin degraded histogram). Marked complete if confirmed in `.ready[]`.

## Next Phase Readiness

- All four `video.gop.*`/`video.frame_types` checks are registered, documented, DOC-03-covered, and mutation-tested. `./build/x64-linux/mediadiff list-checks | grep -c '^video\.gop\.'` reports 4.
- Full suite: 714 tests, 0 failures, 6 designated skips (matching the golden-provenance expectation of 691+ passed / 0 failed / 6 skipped, now higher due to this plan's own added tests).
- No blockers for subsequent Phase 4 plans.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*

## Self-Check: PASSED

All created files verified present on disk (`src/analyzers/video/frame_types.cpp`, four `docs/checks/video.gop.*`/`video.frame_types.md` files, `tests/unit/test_gop_classification.cpp`, this SUMMARY.md). All three task commit hashes (`4615438`, `d053f26`, `9ba54ba`) verified present in `git log --oneline --all`.
