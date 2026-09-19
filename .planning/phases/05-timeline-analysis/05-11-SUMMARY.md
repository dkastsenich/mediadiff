---
phase: 05-timeline-analysis
plan: 11
subsystem: timeline
tags: [timecode, tmcd, smpte, drop-frame, presence-semantic, doc-04, doc-03]

requires:
  - phase: 05-timeline-analysis
    provides: "05-01's DOC-04 no-others harness (timeline_findings.h), the fail-first fixture-pair discipline every prior timeline plan established"
provides:
  - "timeline.timecode / timeline.timecode.value: SMPTE timecode presence and exact byte-for-byte start value from a QuickTime tmcd track, reachable from Pass::demux_header alone (no scan, no decode)"
  - "DemuxSession::StreamInfo::timecode_metadata -- AVStream::metadata[\"timecode\"] resolved at the probe boundary, never exposed as a raw libav read to src/analyzers/"
  - "detail::derive_drop_frame -- punctuation-based drop-frame detection (semicolon vs colon before the frame field), empirically resolved against the pinned generator"
  - "The full, approved 16-id Phase 5 timeline check roster now registered, documented and DOC-03-covered"
affects: [timeline-analysis, phase-05-verification]

actuals:
  tokens: 14092
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "unreachable_sources evidence array: a structurally-impossible extraction path (no linked producer, or decode-only) is documented honestly in evidence AND in docs/checks/<id>.md's own prose, never built out as dead code and never silently omitted"

key-files:
  created:
    - src/analyzers/timeline/timecode.cpp
    - docs/checks/timeline.timecode.md
    - docs/checks/timeline.timecode.value.md
    - tests/unit/test_timecode.cpp
    - tests/integration/test_timeline_timecode.cpp
  modified:
    - src/analyzers/timeline/analyzers.h
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "The drop-frame flag is recoverable from string punctuation, not the timecode rate (A1 resolved): the pinned FFmpeg 9.0.1 generator renders a drop-frame-rate -timecode input as HH:MM:SS;FF (semicolon) and a non-drop-frame one as HH:MM:SS:FF (colon), confirmed via direct ffprobe read-back before any extraction code was written."
  - "unreachable_sources is evidence-only prose, not a second SkipReason-bearing Measurement: since 05-CHECK-ROSTER.md registers no separately-triggerable S12M id (DOC-03 could never supply it a trigger fixture), the honest-disclosure requirement for S12M/GOP timecode is satisfied by naming both sources with reason requires_decode inside every emitted finding's own evidence object -- present even when the file has no tmcd track at all, since the unreachability is a static build fact, not a per-file measurement."
  - "Scope::Kind::global, one measurement per file: TIME-11 is a file-level SMPTE origin, not a per-stream property (mirrors timeline.start's own D-03 global measurement), so the tmcd track's own stream index rides in evidence (timecode_stream_index) rather than becoming the Measurement's own Scope."

patterns-established:
  - "Structurally-unreachable check source disclosed via a static evidence array cited in docs/checks/<id>.md's own What it measures section -- the pattern a future decode-pass phase (S12M/GOP timecode) or any other build-time-unreachable source should follow instead of a dead code path."

requirements-completed: [TIME-11]
# DOC-04 is shared across sibling plans in this phase and was still blocked
# (requirements.ready-ids: blocked) at this plan's own close-out -- this
# plan's own DOC-04 obligation (declared_pairs, no-others sets) is fully
# met (see D2 in coverage below); the shared id itself will be marked
# complete once every sibling plan referencing it has also closed out.

coverage:
  - id: D1
    description: "timeline.timecode / timeline.timecode.value registered per TIME-11: SMPTE presence and exact start value extracted from a tmcd track via Pass::demux_header alone, explicit Absent{} (never an empty string) when no tmcd track exists"
    requirement: TIME-11
    verification:
      - kind: unit
        ref: "tests/unit/test_timecode.cpp (5 TEST_CASEs, detail::derive_drop_frame)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_timecode.cpp (4 TEST_CASEs: value trigger, presence trigger, clean pair, ROADMAP SC5)"
        status: pass
      - kind: integration
        ref: "ctest -R integration.doc03_coverage"
        status: pass
    human_judgment: false
  - id: D2
    description: "DOC-04 whole-report no-others declared-set assertions proven for the value-trigger, presence-trigger, and byte-identical clean fixture pairs, each causal collateral finding (meta.tags echo, container.track_count/types/order, size.overhead) named with a written reason (D-02)"
    requirement: DOC-04
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_timecode.cpp (expect_declared_set assertions)"
        status: pass
    human_judgment: false
  - id: D3
    description: "S12M packet side data and MPEG-2 GOP timecode are reported honestly as structurally unreachable (unreachable_sources evidence + docs/checks/timeline.timecode.md prose), with no dead extraction code path and no separately-triggerable id"
    verification:
      - kind: other
        ref: "grep -v \"^ *//\" src/analyzers/timeline/timecode.cpp | grep -c 'AVStream|libavformat|libavutil|libavcodec' == 0; grep -c S12M in source (evidence-string-only) and doc"
        status: pass
      - kind: manual_procedural
        ref: "mediadiff explain timeline.timecode"
        status: pass
    human_judgment: false

duration: this-session
completed: 2026-09-17
status: complete
---

# Phase 05 Plan 11: Timeline Timecode (timeline.timecode / timeline.timecode.value) Summary

**SMPTE timecode presence and exact byte-for-byte start value from a QuickTime `tmcd` track, reachable with zero decode calls -- plus an honest, `mediadiff explain`-visible disclosure that S12M packet side data and MPEG-2 GOP timecode are structurally unreachable in this build.**

## Performance

- **Duration:** this-session
- **Tasks:** 3 completed
- **Files modified/created:** 19 (5 created, 14 modified)

## Accomplishments

- `detail::derive_drop_frame` and `timeline_timecode_analyzer()` (`src/analyzers/timeline/timecode.cpp`): a no-scan analyzer (`required_passes = {Pass::demux_header}`) that finds the first `tmcd`-tagged stream (`DemuxSession::stream_info`, extended with `StreamInfo::timecode_metadata`) and publishes `timeline.timecode` (`presence`) and `timeline.timecode.value` (`exact`, byte-for-byte, drop-frame punctuation included).
- Task 1's own empirical resolution of `05-RESEARCH.md`'s Open Question 1: the pinned FFmpeg 9.0.1 generator renders a drop-frame-rate `-timecode` input as `HH:MM:SS;FF` and a non-drop-frame one as `HH:MM:SS:FF` -- the drop-frame flag is a punctuation search, not a rate-derived fallback.
- Honest, evidence-visible disclosure of the two structurally-unreachable TIME-11 sources (S12M packet side data, MPEG-2 GOP timecode) via a static `unreachable_sources` evidence array and `docs/checks/timeline.timecode.md`'s own "What it measures" prose -- no dead extraction code, no separately-triggerable S12M id.
- Five new corpus fixtures (`timeline_tc_ndf.mp4`, `timeline_tc_ndf_copy.mp4`, `timeline_tc_ndf_shifted.mp4`, `timeline_tc_absent.mp4`, `timeline_tc_df.mp4`) and a full `tests/integration/test_timeline_timecode.cpp` proving both ids' DOC-04 declared sets, DOC-03 trigger/clean coverage, and ROADMAP SC5's presence/value/drop-frame clause directly.
- The full, approved 16-id Phase 5 timeline check roster is now registered, documented, and DOC-03-covered (running total seventy-five to seventy-seven).

## Task Commits

1. **Task 1: resolve the drop-frame question empirically, commit fixtures** - `10c08bc` (feat)
2. **Task 2 RED: failing test for detail::derive_drop_frame** - `e3499b7` (test)
3. **Task 2 GREEN: timeline.timecode/timeline.timecode.value implementation** - `756f112` (feat)
4. **Task 3: test file, DOC-03 rows, DOC-04 declared sets** - `2acbdce` (test)

_This plan carried no separate "plan metadata" commit -- Task 3's own commit above is the final commit of the plan; STATE.md/ROADMAP.md/REQUIREMENTS.md updates land in a following docs commit per the standard executor workflow._

## Files Created/Modified

- `src/analyzers/timeline/timecode.cpp` - `timeline_timecode_analyzer()`, `detail::derive_drop_frame`, the unreachable-sources evidence builder
- `src/analyzers/timeline/analyzers.h` - the analyzer's own doc-dense declaration plus `detail::derive_drop_frame`'s prototype
- `src/probe/demux_session.{h,cpp}` - `StreamInfo::timecode_metadata` (`AVStream::metadata["timecode"]`, absent-vs-empty distinguished)
- `src/probe/orchestrator.cpp` - registered in `all_analyzers()`, after `timeline_av_sync_analyzer()`
- `src/core/checks.def` - `timeline.timecode` (presence, info) and `timeline.timecode.value` (exact, info) registrations
- `docs/checks/timeline.timecode.md` / `docs/checks/timeline.timecode.value.md` - check documentation, including the unreachable-sources prose
- `CMakeLists.txt` - added `src/analyzers/timeline/timecode.cpp` to `libmediadiff`'s sources
- `tests/unit/test_timecode.cpp` + `tests/unit/CMakeLists.txt` - `derive_drop_frame`'s own hand-computed cases
- `tests/integration/test_timeline_timecode.cpp` + `tests/integration/CMakeLists.txt` - the plan's own DOC-04/SC5 proof
- `tests/integration/test_doc03_coverage.cpp` - `declared_pairs()` entries for both new ids, running total to seventy-seven
- `scripts/gen_corpus.sh` - `timeline_tc_ndf.mp4`, `timeline_tc_ndf_copy.mp4`, `timeline_tc_ndf_shifted.mp4`, `timeline_tc_absent.mp4`, `timeline_tc_df.mp4` recipes
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` - five new fixture digest lines, no pre-existing line rewritten (`lint_corpus_digest_provenance.sh` clean)
- `tests/golden/list_checks_effective.txt` - regenerated (`UPDATE_GOLDENS=1`) for the two new registered ids

## Decisions Made

- **The drop-frame flag is recoverable from string punctuation (A1 resolved empirically before Task 2's code was written).** The pinned FFmpeg 9.0.1 generator renders `00:00:10;00` for a drop-frame-rate `-timecode` input and `00:00:10:00` for a non-drop-frame one -- confirmed via direct `ffprobe` read-back (recorded above and in Task 1's own commit). `detail::derive_drop_frame` is therefore a bare substring search for `;`, not a rate-derived fallback.
- **`unreachable_sources` is evidence-only prose, never a second SkipReason-bearing Measurement.** `05-CHECK-ROSTER.md` registers no separately-triggerable S12M id (DOC-03 could never supply it a trigger fixture, since this build links no `avdevice`). The honest-disclosure requirement is satisfied by naming both S12M packet side data and MPEG-2 GOP timecode, with reason `requires_decode`, inside every emitted finding's own evidence object -- present even on a file with no `tmcd` track at all, since the unreachability is a static build fact, not something that varies per file.
- **`Scope::Kind::global`, one measurement per file.** TIME-11 is a file-level SMPTE origin, not a per-stream property (mirrors `timeline.start`'s own D-03 global measurement) -- the `tmcd` track's own stream array index rides in evidence (`timecode_stream_index`) rather than becoming the Measurement's own `Scope`.
- **`timeline_tc_ndf_copy.mp4` added beyond Task 1's own named artifact list.** Task 3's own action text requires a byte-identical clean pair (`timeline_tc_ndf.mp4 vs a byte-identical copy: the declared set is empty`); a plain `cp`, matching every other clean-pair fixture in this project's corpus (`tracer_a_copy.mp4`, `topo_subs_copy.mp4`, etc.), was the natural way to satisfy it.

## Deviations from Plan

None beyond the one documented decision above (the `timeline_tc_ndf_copy.mp4` addition, itself required by Task 3's own action text and not a change of intent) - plan executed as written, including its own empirically-driven Task 1 resolution step.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Threat Flags

None -- this plan reads one additional, already-attacker-controllable metadata string (`AVStream::metadata["timecode"]`) through the SAME rendered-output choke point (`sanitize_for_display`) every other string-valued check already routes through (`scripts/lint_control_bytes.sh` confirmed clean); the compared value itself is deliberately never sanitised, matching every other exact-string check in this project.

## Self-Check: PASSED

- `src/analyzers/timeline/timecode.cpp` - FOUND
- `docs/checks/timeline.timecode.md` - FOUND
- `docs/checks/timeline.timecode.value.md` - FOUND
- `tests/unit/test_timecode.cpp` - FOUND
- `tests/integration/test_timeline_timecode.cpp` - FOUND
- `tests/fixtures/timeline_tc_ndf.mp4` / `timeline_tc_ndf_shifted.mp4` / `timeline_tc_absent.mp4` / `timeline_tc_ndf_copy.mp4` / `timeline_tc_df.mp4` - FOUND
- Commit `10c08bc` - FOUND (git log)
- Commit `e3499b7` - FOUND (git log)
- Commit `756f112` - FOUND (git log)
- Commit `2acbdce` - FOUND (git log)
- Full `ctest` suite: 917/917 passed, 0 failed (6 legitimately skipped, unrelated to this plan -- same baseline count as at plan start)
- `./build/x64-linux/mediadiff list-checks --effective | grep -c '^timeline\.'` reports `16`

## Next Phase Readiness

The full, approved 16-id Phase 5 timeline check roster (`05-CHECK-ROSTER.md`) is now registered, documented, and DOC-03-covered end to end. `timeline.timecode`/`timeline.timecode.value` close out TIME-11; the S12M and MPEG-2 GOP arms remain honestly `requires_decode` pending a future decode-pass phase, matching Phase 4's own `video.closed_captions` precedent. No blockers for Phase 5's remaining verification work.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-17*
