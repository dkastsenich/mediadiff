---
phase: 04-video-analysis
plan: 01
subsystem: probe
tags: [ffmpeg, libavcodec, av_parser_parse2, mpeg4, h264, hevc, catch2]

# Dependency graph
requires:
  - phase: 03-container-analysis
    provides: PacketScan's single av_read_frame sweep (PROBE-02/PROBE-10), the size.* analyzer/AnalyzerSpec registration pattern, DOC-03's fixture-pair coverage gate
provides:
  - "Pass::parser_scan fused inside run_packet_scan's own loop (probe/parser_scan.{h,cpp}) -- one AVCodecParserContext+AVCodecContext per stream, never a second sweep"
  - "video.gop.length, the phase's first registered video.* check, with its --explain doc and DOC-03 fixture pair"
  - "The approved 31-id Phase-4 check-id roster (04-CHECK-ROSTER.md) every remaining plan in this phase reads as source of truth"
affects: [04-06-video-stream-params, 04-07-video-sar-framerate, 04-08-video-color, 04-09-video-gop-frame-types, 04-10-video-interlace, 04-11-video-hdr, 04-12-video-dovi]

actuals:
  tokens: 27282
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "Parser-pass fusion: a NEW raw scan extends an EXISTING sweep's loop body rather than opening its own av_read_frame/av_parser dispatch arm -- the orchestrator's own union-implication rule (parser_scan implies packet_scan) is what keeps an analyzer declaring only the new pass from silently getting no sweep at all."
    - "Fixed-width per-unit record for a variable-length bitstream concept (NAL-type sequence -> a 64-bit type-presence mask + a single leading-VCL-type byte) so a per-AU store stays budget-accountable against the SAME shared byte cap an adjacent per-packet store already uses."

key-files:
  created:
    - src/probe/parser_scan.h
    - src/probe/parser_scan.cpp
    - src/analyzers/video/analyzers.h
    - src/analyzers/video/gop.cpp
    - docs/checks/video.gop.length.md
    - tests/unit/test_parser_scan.cpp
    - .planning/phases/04-video-analysis/04-CHECK-ROSTER.md
    - .planning/phases/04-video-analysis/deferred-items.md
  modified:
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/probe/pass.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/integration/test_doc03_coverage.cpp
    - tests/unit/test_pass_union.cpp
    - tests/unit/CMakeLists.txt
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "Phase-4 31-id video.* check roster approved as-proposed (04-CHECK-ROSTER.md), including all 7 one-semantic-per-id splits and video.sar.conflict's own info-severity check id."
  - "PacketScanRequest/PacketScanOutputs (and the new run_packet_scan(DemuxSession&, const PacketScanRequest&) overload) live in probe/packet_scan.h rather than probe/parser_scan.h -- parser_scan.h stays a self-contained, packet_scan-independent header (no dependency on PacketRecord/PacketScanResult at all), avoiding a circular include while still letting the pre-Phase-4 PacketScanLimits-only overload become a thin inline wrapper in the SAME header."
  - "AVCodecParserContext::flags must carry PARSER_FLAG_COMPLETE_FRAMES (set right after av_parser_init, mirroring libavformat/demux.c's own AVSTREAM_PARSE_HEADERS path) -- without it, mpeg4video_parser.c's frame-boundary search buffers a fed access unit across calls instead of returning it immediately, silently halving every measured GOP length. Discovered empirically against the real g48/g96 fixture pair, not assumed from the design doc."
  - "video.gop.length's key_frame derivation replicates libavformat/demux.c's own three-clause fallback verbatim (parser key_frame==1, else pict_type==I, else the packet's own AV_PKT_FLAG_KEY when pict_type is NONE) rather than trusting AVCodecParserContext::key_frame alone -- mpeg4video_parser.c never sets that field itself, so the fallback is load-bearing for this plan's own fixture codec, not a defensive no-op."

patterns-established:
  - "Parser lifetime RAII (detail::StreamParserState, probe/parser_scan.{h,cpp}): one AVCodecParserContext*/AVCodecContext* pair per stream, lazily initialized on first packet, freed on every exit path including move -- the template every later NAL-consuming video.* analyzer in this phase reuses."
  - "NAL-type walk as a pure, span-based, bounded function (detail::walk_annex_b_nal_types) driven directly by tests over hand-built byte arrays -- no fixture file, no av_parser_parse2 call -- mirroring src/analyzers/size/size.cpp's compute_peak_window precedent for 'test-only extraction point' pure functions."

requirements-completed: [PROBE-03, VIDEO-05]

coverage:
  - id: D1
    description: "Pass::parser_scan fused inside run_packet_scan's existing av_read_frame loop -- proven by read_frame_call_count equality with/without parsing enabled, never a second sweep"
    requirement: PROBE-03
    verification:
      - kind: unit
        ref: "tests/unit/test_parser_scan.cpp#parser_scan - enabling the parser produces the SAME read_frame_call_count as packet_scan alone"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - an analyzer declaring ONLY Pass::parser_scan still causes Pass::packet_scan to run"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.gop.length registered end to end: median keyframe-to-keyframe access-unit distance as an exact RationalValue, visible in `mediadiff compare --json` and `explain video.gop.length`, with its DOC-03 trigger/clean fixture pair"
    requirement: VIDEO-05
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp#doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
      - kind: unit
        ref: "tests/unit/test_parser_scan.cpp#parser_scan - the parser's key_frame count matches the packet flags' keyframe count"
        status: pass
    human_judgment: false
  - id: D3
    description: "H.264/HEVC NAL-type walk (fixed-width mask + leading-VCL-type), bounded against truncation (T-4-03) and a crafted-NAL-count DoS (T-4-04)"
    verification:
      - kind: unit
        ref: "tests/unit/test_parser_scan.cpp#parser_scan - the walk stops after kMaxNalsPerAccessUnit NALs and does not iterate the remainder"
        status: pass
      - kind: unit
        ref: "tests/unit/test_parser_scan.cpp#parser_scan - a truncated trailing NAL is ignored without reading past the buffer"
        status: pass
    human_judgment: false

duration: 28min
completed: 2026-09-10
status: complete
---

# Phase 4 Plan 01: ParserScan fusion + video.gop.length Summary

**`Pass::parser_scan` fused into `PacketScan`'s existing `av_read_frame` sweep, proving the single riskiest architectural claim of the phase, and shipping `video.gop.length` as the first registered `video.*` check.**

## Performance

- **Duration:** 28 min (this continuation session, Task 2 + Task 3; Task 1's roster approval was completed by a prior executor session and is included in this plan's totals)
- **Started:** 2026-09-10T20:58:52+02:00 (Task 1 approval commit)
- **Completed:** 2026-09-10T21:26:24+02:00
- **Tasks:** 3/3 completed
- **Files modified:** 20 (across all 3 tasks; 11 created, 9 modified)

## Accomplishments

- `src/probe/parser_scan.{h,cpp}`: `Pass::parser_scan`'s per-access-unit walk, fused inside `run_packet_scan`'s own loop (`probe/packet_scan.cpp`) via a new additive `PacketScanRequest`/`PacketScanOutputs` overload -- never a second `av_read_frame` sweep, proven by an exact `read_frame_call_count` equality test rather than a comment.
- `video.gop.length` registered end to end (`src/core/checks.def`, `src/analyzers/video/gop.cpp`, `docs/checks/video.gop.length.md`): median keyframe-to-keyframe access-unit distance as an exact `RationalValue`, a real `--profile sw-encoder` finding on the `video_gop_g48.mp4`/`video_gop_g96.mp4` fixture pair (`48` vs `96`, matching `04-RESEARCH.md`'s own empirical prediction exactly), and green on `DOC-03`'s count-equality gate.
- `.planning/phases/04-video-analysis/04-CHECK-ROSTER.md`: the full 31-id Phase-4 roster, approved as-proposed by a human reviewer at Task 1's `checkpoint:decision` gate -- the frozen source of truth every remaining plan in this phase reads before registering an id.
- `tests/unit/test_parser_scan.cpp` (10 test cases) + 3 new cases in `tests/unit/test_pass_union.cpp`: the single-sweep invariant, the one-mpeg4-packet-is-one-access-unit count, the parser/packet keyframe-flag agreement, the parser-disabled unchanged-shape case, a hand-verified H.264/HEVC NAL-type table (including the T-4-03 truncation and T-4-04 crafted-NAL-count bounds), and the shared T-4-01 byte-budget exhaustion setting `partial` on both the packet and parser results at an exactly predicted count.

## Task Commits

Each task was committed atomically:

1. **Task 1: Approve the Phase-4 `video.*` check-id roster** - `1a866dd` (draft) + `c6f2309` (approval) (docs) -- completed by the prior executor session before this continuation.
2. **Task 2: ParserScan fused into the existing sweep, carrying `video.gop.length` end to end** - `21c7a0f` (feat)
3. **Task 3: Pin the single-sweep invariant and the hand-verified parser table** - `aa9c71f` (test)

_No plan-metadata commit yet -- this SUMMARY.md and the STATE.md/ROADMAP.md updates it triggers are committed as a separate `docs(04-01)` commit immediately after this file is written, per this executor's own required order._

## Files Created/Modified

- `src/probe/parser_scan.h` / `.cpp` - `AccessUnitRecord`/`StreamParserScan`/`ParserScanResult`, `detail::walk_annex_b_nal_types` (bounded Annex-B NAL-type walk), `detail::StreamParserState` (per-stream `AVCodecParserContext`/`AVCodecContext` RAII, no decode)
- `src/probe/packet_scan.{h,cpp}` - `PacketScanRequest`/`PacketScanOutputs`, the new primary `run_packet_scan` overload with the parser fused into its loop; the pre-Phase-4 `PacketScanLimits`-only overload becomes a thin inline wrapper
- `src/probe/pass.h` - `ProbeResults::parser_scan` (`std::optional<ParserScanResult>`), placed beside `packet_scan`
- `src/probe/orchestrator.cpp` - the parser_scan-implies-packet_scan union rule; the `Pass::packet_scan` arm now builds a `PacketScanRequest` and unpacks both `results.packet_scan`/`results.parser_scan`; `video_gop_analyzer()` registered
- `src/analyzers/video/analyzers.h` / `gop.cpp` - `video_gop_analyzer()`, `run_video_gop`, `emit_gop_length` (median via the lower-of-two-central-values convention, mirroring `mp4.cpp`'s own `compute_median_fragment_duration`)
- `src/core/checks.def` - `video.gop.length` (`video`/`tol`/`percent`/`rational`/`fail`/`10%`)
- `docs/checks/video.gop.length.md` - What it measures / Why it matters / Accept-Tune-Silence, per `tools/gen_registry.py`'s doc gate
- `CMakeLists.txt` - new source/header set entries
- `scripts/gen_corpus.sh` - `video_gop_g48.mp4`/`video_gop_g48_copy.mp4`/`video_gop_g96.mp4` recipes (D-04: real `mpeg4` encoder, `-bf 0`, differing only in `-g`)
- `tests/integration/test_doc03_coverage.cpp` - `video.gop.length`'s declared trigger/clean fixture pair
- `tests/unit/test_parser_scan.cpp` (new), `tests/unit/test_pass_union.cpp` (extended), `tests/unit/CMakeLists.txt` - Task 3's own test suite
- `tests/golden/list_checks_effective.txt` - refreshed (one new line, the check's own registration) via `UPDATE_GOLDENS=1`
- `.planning/phases/04-video-analysis/04-CHECK-ROSTER.md` - the approved 31-id roster (Task 1)
- `.planning/phases/04-video-analysis/deferred-items.md` - the 5 pre-existing, out-of-scope golden failures discovered while regenerating the corpus (see Deviations below)

## Decisions Made

- `PacketScanRequest`/`PacketScanOutputs` live in `probe/packet_scan.h` (not `probe/parser_scan.h`) so `parser_scan.h` stays self-contained with zero dependency on `packet_scan.h`, avoiding a circular include while still letting the legacy overload become an inline wrapper in the same header.
- `video.gop.length`'s key_frame derivation replicates `libavformat/demux.c`'s own three-clause `key_frame`/`pict_type`/`AV_PKT_FLAG_KEY` fallback verbatim, since `mpeg4video_parser.c` (this plan's own fixture codec) never sets `AVCodecParserContext::key_frame` itself.
- `AVCodecParserContext::flags |= PARSER_FLAG_COMPLETE_FRAMES` set immediately after `av_parser_init` (see Deviations: Rule 1 bug fix below) -- required for the "one packet = one access unit, one call" contract every downstream `video.*` parser-consumer in this phase depends on.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `AVCodecParserContext` needed `PARSER_FLAG_COMPLETE_FRAMES` set explicitly, or `mpeg4video_parser.c`'s frame-boundary search silently halved every measured GOP length**
- **Found during:** Task 2, while verifying `video.gop.length` against the real `video_gop_g48.mp4`/`video_gop_g96.mp4` fixture pair (the plan's own acceptance criterion: `mediadiff compare ... --json` must show a non-pass finding).
- **Issue:** Without this flag, `mpeg4video_parser.c`'s own `mpeg4_find_frame_end` searches the fed buffer for the START of the NEXT frame to decide the CURRENT one is complete. Since `parse_packet` feeds exactly one already-complete access unit per call (no trailing start code in that buffer), the search finds nothing and `ff_combine_frame` buffers across calls instead of returning the fed AU immediately -- silently shifting/mis-locating every keyframe index in `AccessUnitRecord`'s own array. Measured symptom: `video.gop.length` reported `24`/`48` instead of the expected `48`/`96` (exactly half), even though `keyframe_count` (3/2) was already correct.
- **Fix:** Set `parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;` immediately after a successful `av_parser_init`, mirroring exactly what `libavformat/demux.c` itself does when opening a stream with `AVSTREAM_PARSE_HEADERS` (confirmed by reading that file's own `av_parser_init` call sites).
- **Files modified:** `src/probe/parser_scan.cpp` (`StreamParserState::ensure_initialized`)
- **Verification:** `mediadiff compare video_gop_g48.mp4 video_gop_g96.mp4 --profile sw-encoder --json` now reports `baseline.num=48`/`candidate.num=96`, `keyframe_count=3`/`2` -- exactly matching `04-RESEARCH.md`'s own empirically-predicted values from planning. `tests/unit/test_parser_scan.cpp`'s Test 2/3 pin this.
- **Committed in:** `21c7a0f` (part of Task 2's own commit)

**2. [Rule 3 - Blocking] `tests/golden/list_checks_effective.txt` needed refreshing for the new check's own registration**
- **Found during:** Task 2's full-suite verification (`ctest --test-dir build/x64-linux --output-on-failure`).
- **Issue:** `integration.list_checks`'s own `--effective`-is-byte-identical golden test failed because `video.gop.length`'s registration is a real, intended change to the registry's output -- the golden predates this plan by construction.
- **Fix:** Refreshed via `UPDATE_GOLDENS=1 ctest -R list_checks`; the diff is exactly one new line (`video.gop.length  severity=fail  tolerance=10/1 %`).
- **Files modified:** `tests/golden/list_checks_effective.txt`
- **Verification:** `ctest -R "list_checks.*golden"` passes; `git diff` on the golden shows a single-line addition.
- **Committed in:** `21c7a0f`

---

**Total deviations:** 2 auto-fixed (1 Rule 1, 1 Rule 3). **Impact on plan:** Both were necessary for correctness (the `PARSER_FLAG_COMPLETE_FRAMES` fix is load-bearing for every downstream `video.*` parser-consumer this phase adds) or an intended, foreseen consequence of the plan's own change (the golden refresh). No scope creep.

## Issues Encountered

Running the plan's own mandated `bash scripts/gen_corpus.sh` step (required by Task 2's `<verify>`) regenerates the ENTIRE fixture corpus, not just this plan's two new recipes. Doing so against the same pinned, SHA-256-verified `linux-x86_64` ffmpeg binary this environment already had installed produced byte-different output for 5 fixtures this plan never touches (`size_crf20.mp4`, `ts_204.ts`, `ts_multiprogram.ts`, `ts_single.ts`, and `inspect_container`'s own representative fixture), failing 5 pre-existing golden tests unrelated to `video.gop.length`/`ParserScan`. Verified NOT flaky within this session (a second back-to-back regeneration produced a byte-identical `size_crf20.mp4`) -- this is a genuine environment-dependent corpus-generation gap, not a code regression from this plan. None of the affected recipes were touched by this plan; `ts_scan_golden` is deliberately TSDuck-derived (an independent cross-check) and was NOT blindly refreshed. Logged in full, with recommended follow-up, to `.planning/phases/04-video-analysis/deferred-items.md` per the Scope Boundary rule. `integration.doc03_coverage` -- this plan's own critical gate -- is unaffected and passes cleanly, isolated via `ctest -R doc03_coverage`.

## Known Stubs

None.

## Threat Flags

None beyond the STRIDE register already authored in `04-01-PLAN.md`'s own `<threat_model>` (T-4-01 through T-4-06, T-4-SC) -- every mitigation there (shared byte-budget accounting, RAII parser lifetime, bounds-checked NAL walk, `kMaxNalsPerAccessUnit` bound, `no_parser` degradation, actionable `partial_scan` evidence) is implemented as specified, with a regression test pinning each of T-4-01/T-4-02/T-4-03/T-4-04/T-4-05.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`04-CHECK-ROSTER.md` is approved and frozen -- plans `04-06` through `04-12` can register their own ids from it without re-litigating spellings or attributes. `Pass::parser_scan`'s fusion point and `detail::StreamParserState`/`detail::walk_annex_b_nal_types` are proven end to end against a real fixture and are the direct template `04-09` (GOP/frame-types, the same parser data) and `04-10` (interlace, `field_order`/`repeat_pict`) both build on. No blockers.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-10*

## Self-Check: PASSED

All 9 created files confirmed present on disk; all 4 task/roster commit hashes (`1a866dd`, `c6f2309`, `21c7a0f`, `aa9c71f`) confirmed present in `git log --oneline --all`.
