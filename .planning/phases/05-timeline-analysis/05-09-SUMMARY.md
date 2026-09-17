---
phase: 05-timeline-analysis
plan: 09
subsystem: timeline-analysis
tags: [av-sync, priming, skip-samples, initial-padding, aac, rational, doc03, doc04, corpus-digest]

requires:
  - phase: 05-timeline-analysis
    provides: "05-01's DemuxSession seam and packet_scan.h/.cpp's single av_read_frame sweep (PROBE-03), 05-05/05-06/05-08's push_skip/scope_kind_for_stream/compute_stream_scopes per-file-copy pattern, timeline_start_base.mp4/timeline_start_shift.ts's own established fixture recipes"
provides:
  - "PacketScan::first_packet_skip_samples / initial_padding: the first-packet AV_PKT_DATA_SKIP_SAMPLES side-data value and the stream's codecpar initial_padding, captured inside the existing sweep, each absent-vs-zero distinguishable"
  - "mediadiff::resolve_priming (src/analyzers/timeline/av_sync.cpp, declared in analyzers.h): shared probe-level priming resolver, packet-level skip_samples checked first, initial_padding fallback, open to extension for Phase 6's audio.priming (AUDIO-04)"
  - "timeline.av_offset: signed ms offset between first audible sample and first visible frame, dual raw/adjusted storage with structured priming evidence, comparison basis chosen per-pair (adjusted only when BOTH sides know priming, raw-to-raw otherwise), never demoted or tolerance-widened when priming is unknown (D-11)"
  - "src/compare/tol.cpp's generic evidence-driven magnitude override (Rule 2 addition): reads comparison_basis/adjusted_offset_ms from both Measurement::evidence objects, gated on evidence shape never on check.id"
  - "timeline_avoffset_video_shift.mp4 / timeline_avoffset_unknown.ts fixtures, DOC-03 declared_pairs() row and DOC-04 declared-set coverage for timeline.av_offset, running total seventy-two -> seventy-three"
affects: [timeline-analysis, audio-analysis, doc03-coverage, doc04-no-others, compare-tolerance]

actuals:
  tokens: 21000
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "Absent-vs-zero packet-level side data: StreamPacketScan::first_packet_skip_samples is std::optional<std::int64_t> (matches EbmlTrack::codec_delay_ns / PidStats::first_cc_error_offset's own precedent) so 'no AV_PKT_DATA_SKIP_SAMPLES side data was present' is distinguishable from 'present with value 0' -- load-bearing for MPEG-TS's own priming-unknown state."
    - "In-sweep side-data capture: AV_PKT_DATA_SKIP_SAMPLES is read from the live AVPacket inside the existing packet_scan.cpp av_read_frame loop, on a stream's first packet only, before pkt.unref() -- no second sweep, no decode call, PacketScanResult::read_frame_call_count proven unchanged by a hand-recorded literal."
    - "Priming-composes-with-edit-list, never re-applied: the first audible sample is first_audio_pts + skip_samples (on MP4, -1024 + 1024 == 0) -- libav has already applied the container's edit list to the packet PTS, so bmff_scan::EditListEntry is read only to build a mechanism evidence string, never used arithmetically."
    - "Per-pair comparison-basis selection (D-10): raw/adjusted are both stored per side; the compared magnitude uses adjusted only when BOTH sides' priming is known, otherwise raw-to-raw on both sides -- implemented as a generic evidence-shape-gated override in compare/tol.cpp (mirrors the pre-existing `estimated`-flag mechanism), never gated on check.id, so a future check with the same evidence shape gets the same override for free."
    - "Severity never softened for uncertainty (D-11): unknown priming changes which basis a comparison uses, never the registered severity or tolerance -- asserted directly (not just 'non-pass') in a dedicated ROADMAP SC4 test case."

key-files:
  created:
    - src/analyzers/timeline/av_sync.cpp
    - docs/checks/timeline.av_offset.md
    - tests/unit/test_av_sync.cpp
    - tests/integration/test_timeline_av_sync.cpp
    - tests/fixtures/timeline_avoffset_video_shift.mp4
    - tests/fixtures/timeline_avoffset_unknown.ts
  modified:
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/analyzers/timeline/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - src/compare/tol.cpp
    - CMakeLists.txt
    - .planning/REQUIREMENTS.md
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt
    - tests/unit/test_packet_scan.cpp
    - tests/unit/test_tolerance.cpp
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp

key-decisions:
  - "D-10's cross-file basis-selection rule required extending src/compare/tol.cpp, a file outside Task 2's declared files_modified -- implemented as a Rule 2 deviation (auto-add missing critical functionality) as a generic, evidence-shape-gated override rather than a check.id-gated special case, following compare/tol.cpp's own pre-existing `estimated`-flag precedent exactly."
  - "The ROADMAP SC4 case (\"unknown-priming finding is non-pass at normal severity\") could not be demonstrated on the literal base-vs-unknown.ts pairing the plan assumed: that pairing's raw offsets coincide (only the container changed, not the audio timing), so timeline.av_offset genuinely PASSES there. Per D-12's own 'prove before assert' mandate, the SC4 case instead compares unknown.ts against the genuinely-shifted video_shift.mp4, which does produce a real non-pass finding while one side's priming is unknown -- verified against the real binary before being written."
  - "primary_video_stream's 'not an attached picture' exclusion (named in the roster's Discretion paragraph) was not implemented: no StreamInfo field exposes attached-picture status within Task 2's declared file scope, and no fixture in the corpus exercises the case. Implemented as 'first video-scoped stream by array order' with a scoping note in the header comment rather than fabricating untested logic."

patterns-established:
  - "Shared cross-phase primitive with a documented future consumer: resolve_priming's header comment states, in the project's own words, that Phase 6's audio.priming (AUDIO-04) is its designed second consumer and that its Source enum is open to extension without renaming existing members -- the same shape Phase 4's shared cadence derivation established for timeline.jitter/timeline.vfr_profile."

requirements-completed: [TIME-06, TIME-09, TIME-10, DOC-04]

coverage:
  - id: D1
    description: "PacketScan captures first-packet skip_samples and initial_padding inside the existing single av_read_frame sweep, absent-vs-zero distinguishable, sweep count proven unchanged"
    requirement: "TIME-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#av_sync/packet_scan skip-samples tests (Tests 1-7 of Task 1's behavior block)"
        status: pass
    human_judgment: false
  - id: D2
    description: "resolve_priming resolves packet-level skip_samples first, initial_padding fallback, shared primitive declared for Phase 6"
    requirement: "TIME-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_av_sync.cpp#av_sync - resolve_priming(...) (Tests 1-4)"
        status: pass
    human_judgment: false
  - id: D3
    description: "timeline.av_offset registered, dual raw/adjusted storage, per-pair comparison basis selection, unsoftened severity when priming is unknown"
    requirement: "TIME-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#compare_tol D-10 override tests, timeline.av_offset boundary tests"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_av_sync.cpp#timeline_av_sync - ROADMAP SC4: ... (D-11 no-softening property)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Both TIME-10 arms exist as real, ffprobe-read-back-verified fixtures (recoverable and unknown priming), DOC-03/DOC-04 coverage registered"
    requirement: "TIME-10"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_av_sync.cpp#timeline_av_sync - the video-shift (recoverable-priming) trigger pair ... / the MPEG-TS remux (unknown-priming) pair ..."
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp#doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
    human_judgment: false

duration: ~19min (measured task-commit-to-task-commit; full session spanned a context-compacted conversation)
completed: 2026-09-17
status: complete
---

# Phase 5 Plan 09: Probe-Level Audio Priming and timeline.av_offset Summary

Packet-level `AV_PKT_DATA_SKIP_SAMPLES` capture inside the existing demux sweep, a shared `resolve_priming` primitive that checks it before `initial_padding` (MP4's own signal lives only at the packet level), and `timeline.av_offset` comparing on whichever basis both sides of a pair actually share -- unknown priming stays visible in evidence without ever softening the check's severity.

## Performance

- **Duration:** ~19 min (task-commit span; the full session, including a context-compacted continuation, was longer)
- **Started:** 2026-09-17T12:50:02+02:00 (first task commit)
- **Completed:** 2026-09-17T13:08:48+02:00 (last task commit)
- **Tasks:** 3/3 completed
- **Files modified:** 21 (6 created, 15 modified)

## Accomplishments
- `PacketScan` now captures each stream's first-packet `AV_PKT_DATA_SKIP_SAMPLES` and `initial_padding` inside the one existing `av_read_frame` sweep, with `read_frame_call_count` proven unchanged against a hand-recorded literal (139, verified against `tracer_a.mp4`) -- no second sweep, no decode call.
- `resolve_priming` (packet-level `skip_samples` checked first, `initial_padding` fallback) is a shared, documented probe-level primitive Phase 6's `audio.priming` (`AUDIO-04`) is designed to extend, matching `05-RESEARCH.md`'s empirically-verified per-container table (MP4: `skip_samples=1024`/`initial_padding=0`; Matroska: both `1024`; MPEG-TS: neither).
- `timeline.av_offset` stores RAW and ADJUSTED offsets plus a structured `priming: {state, source, samples}` object per side, and the CROSS-file comparison selects its basis per-pair: adjusted only when both sides know their priming, raw-to-raw otherwise -- implemented as a generic, evidence-shape-gated override in `src/compare/tol.cpp` (a Rule 2 deviation, mirroring the pre-existing `estimated`-flag mechanism).
- Unknown priming never demotes severity or widens tolerance (D-11) -- asserted directly against the real binary via a dedicated ROADMAP SC4 integration test, not merely inferred from "non-pass".
- Both TIME-10 arms exist as real, `ffprobe`-read-back-verified fixtures: `timeline_avoffset_video_shift.mp4` (video-only `-itsoffset`, priming survives, `source: skip_samples` on both sides) and `timeline_avoffset_unknown.ts` (a `-c copy` MPEG-TS remux carrying no side data at all, `priming: unknown`).
- DOC-03's `declared_pairs()` registry gate and the DOC-04 whole-report declared-set harness both cover `timeline.av_offset`; the full suite passes at 895/895 (up from the 873/873 baseline: +22 new tests across Tasks 1-3, 0 regressions, the same 6 pre-existing unrelated skips).

## Fixture read-back proof (required before any assertion was written)

`ffprobe -select_streams a:0 -show_packets -read_intervals "%+#2"` over both new fixtures and the base file:

| Fixture | First audio packet PTS | `skip_samples` side data | Measured `mediadiff` av_offset (vs `timeline_start_base.mp4`) |
|---|---|---|---|
| `timeline_start_base.mp4` (reference) | -1024 | present, 1024 | raw=-23ms, adjusted=0ms, `priming.source=skip_samples` |
| `timeline_avoffset_video_shift.mp4` | -1024 (audio untouched; only video shifted) | present, 1024 | raw=-63ms, adjusted=-40ms, `priming.source=skip_samples` -- delta vs base -40ms, `fail` (beyond the registered 20ms threshold) |
| `timeline_avoffset_unknown.ts` | 126000 (90kHz TS clock; TS muxer's own mux delay) | **absent** -- only an `MPEGTS Stream ID` side-data entry | raw=-23ms, adjusted=-23ms (no adjustment applied), `priming.state=unknown` -- delta vs base 0ms, `pass` (raw offsets coincide; only the container changed) |

Since the base-vs-unknown pairing produces a genuine `pass` (D-12's "prove before assert" mandate surfaced this empirically, not merely a doc-04 assumption), the ROADMAP SC4 case instead compares `timeline_avoffset_unknown.ts` against `timeline_avoffset_video_shift.mp4`: one side's priming is unknown, D-10's raw-to-raw basis applies, and because the video-shift side's raw offset genuinely differs (-63ms vs -23ms), the check produces a real `fail`-severity, non-pass finding with `priming.state=unknown` visible in evidence -- exactly the property SC4 asks for, proven on real files.

## Task Commits

Each task was committed atomically:

1. **Task 1: First-packet skip-samples captured inside the existing sweep (D-09)** - `d700f7a` (feat, tdd)
2. **Task 2: resolve_priming and timeline.av_offset — dual storage, shared-basis comparison, unsoftened severity** - `7f125ff` (feat, tdd)
3. **Task 3: Both TIME-10 arms on real files, the DOC-03 row and the DOC-04 declared sets** - `fb82b68` (test)

**Plan metadata:** (recorded after this SUMMARY commit)

## Files Created/Modified
- `src/probe/packet_scan.h` / `.cpp` - first-packet `skip_samples` and `initial_padding` capture inside the existing sweep
- `src/analyzers/timeline/av_sync.cpp` - `resolve_priming`, `detail::primary_video_stream`, `timeline_av_sync_analyzer()`
- `src/analyzers/timeline/analyzers.h` - `PrimingResult`/`resolve_priming` declared for Phase 6
- `src/core/checks.def` - `timeline.av_offset` registration (fail, 5ms/20ms tolerance)
- `src/compare/tol.cpp` - D-10's generic evidence-shape-gated raw/adjusted magnitude override (Rule 2)
- `docs/checks/timeline.av_offset.md` - full Accept/Tune/Silence doc, names the priming state explicitly (D-11)
- `tests/fixtures/timeline_avoffset_video_shift.mp4` / `timeline_avoffset_unknown.ts` - both TIME-10 arms
- `tests/integration/test_timeline_av_sync.cpp` - DOC-04 declared sets, DOC-03 empirical proof, ROADMAP SC4 case
- `.planning/REQUIREMENTS.md` - `EXT-05` note: Phase 5's D-09 partly satisfies it (probe-level priming from demuxer-exposed sources; codec/container-mechanism tier stays v2)

## Decisions Made
- Extended `src/compare/tol.cpp` outside Task 2's declared file list (Rule 2 auto-add: D-10's cross-file basis rule is structurally required for correctness and has no other implementation seam) as a generic evidence-shape override, never gated on `check.id`.
- Restated the ROADMAP SC4 case's fixture pairing to the one that empirically demonstrates the property (`unknown.ts` vs `video_shift.mp4`), per D-12's "prove before assert, restate to the measured value" mandate, rather than asserting the plan's original base-vs-unknown assumption which turned out to produce a genuine pass.
- Left `primary_video_stream`'s "not an attached picture" exclusion unimplemented (no field exposes it within scope, no fixture exercises it) with a documented scoping note, rather than fabricating untested logic.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] Extended `src/compare/tol.cpp` for D-10's cross-file basis selection**
- **Found during:** Task 2
- **Issue:** D-10 requires the CROSS-file comparison to select adjusted-vs-raw magnitude per pair (adjusted only when both sides know priming), which cannot live inside a single-file analyzer -- it is inherently a comparator-level decision. Task 2's own declared `files_modified` list did not include any `src/compare/*.cpp` file.
- **Fix:** Added a generic, evidence-shape-gated override in `compare_tol` (reads `comparison_basis`/`adjusted_offset_ms` from both `Measurement::evidence` objects when both are present and well-typed, never gated on `check.id`), mirroring the pre-existing `estimated`-flag mechanism in the same function.
- **Files modified:** `src/compare/tol.cpp`
- **Verification:** `tests/unit/test_tolerance.cpp`'s 3 new D-10-override tests, plus the real-binary `compare --json` proof recorded above.
- **Committed in:** `7f125ff` (part of Task 2 commit)

**2. [Documentation-only] Two literal acceptance-criteria greps proved unsatisfiable/inaccurate as written**
- **Found during:** Task 1 and Task 2
- **Issue:** Task 1's `grep -c 'av_read_frame' src/probe/packet_scan.cpp` criterion asks for exactly `1`, but the pre-existing file (before this plan's changes) already had 4 occurrences (3 in comments, 1 real call) -- the criterion was unsatisfiable before this plan started. Task 2's `inspect ... -v --json` criterion asserts evidence is visible in `--json` mode, but `inspect --json` (even combined with `-v`) never renders the `evidence` field at all -- only plain `-v` text mode does (a known documented gotcha, MEMORY.md "Verify output-absence claims").
- **Fix:** Verified the INTENDED narrower Task 1 check (`grep -c 'av_read_frame(ctx' src/probe/packet_scan.cpp` = 1, the sole real call site) is satisfied. Verified Task 2's actual evidence-rendering claim against plain `-v` (no `--json`), which correctly shows the full evidence object.
- **Files modified:** none (verification-only; no code change required)
- **Verification:** manual command re-run, documented here
- **Committed in:** n/a (no code change)

---

**Total deviations:** 2 (1 Rule 2 auto-add, 1 documentation-only acceptance-criteria correction)
**Impact on plan:** The Rule 2 addition was necessary for D-10's correctness and follows an established in-file precedent exactly -- no scope creep. The acceptance-criteria corrections did not require any code change; both were verified against the real binary before being accepted as satisfied.

## Issues Encountered
- System `ffprobe`/`ffmpeg` at `/usr/local/bin` is a much newer, unrelated dev build (`N-126086-ge5ecfe8970`) than the project's actual pinned/linked FFmpeg 8.1 -- an initial read-back showed MP4 `initial_padding=1024`, apparently contradicting `05-RESEARCH.md`. Resolved by trusting only the real linked binary (`mediadiff_unit_tests`, `mediadiff` CLI) for priming behavior, and using system `ffprobe` only for the sanctioned read-back-verification role (per `project_specifics`), which then correctly showed `skip_samples` side data alongside `initial_padding`.
- The base-vs-`timeline_avoffset_unknown.ts` pairing was expected (per the plan's own flagged assumption A1/A2) to produce a non-pass `timeline.av_offset` finding directly; empirically it produces `pass` (raw offsets coincide, since neither file's audio timing was ever shifted, only the container changed). Resolved per D-12's own "prove before assert, restate to the measured value" instruction: the DOC-04 declared set for that pair correctly omits `timeline.av_offset` (documented inline in the test file), and the dedicated ROADMAP SC4 case uses the pairing that actually demonstrates the D-11 no-softening property.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
`resolve_priming` and its `Source` enum are declared in `src/analyzers/timeline/analyzers.h` specifically for Phase 6's `audio.priming` (`AUDIO-04`) to extend with the container-mechanism tier (MP4 `elst`/iTunSMPB, MKV `CodecDelay`) -- the enum is open to extension without renaming existing members, and the header comment states this explicitly. No blockers for Phase 6.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-17*

## Self-Check: PASSED
All created files verified present on disk; all 3 task commit hashes (`d700f7a`, `7f125ff`, `fb82b68`) verified present in git history.
