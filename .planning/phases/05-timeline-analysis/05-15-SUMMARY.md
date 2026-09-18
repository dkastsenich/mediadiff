---
phase: 05-timeline-analysis
plan: 15
subsystem: timeline-analysis
tags: [ts-scan, mpegts, pes-header, iso13818-1, dts-monotonic, gap-closure, container-truth]

requires:
  - phase: 05-timeline-analysis
    provides: "05-07's detail::record_discontinuity_offset bounded-seam shape (PidStats, kMaxDiscontinuityOffsetsPerPid) this plan's PES timestamp seam mirrors; 05-VERIFICATION.md's Gap 4 (dts_monotonic reports a DTS violation the file does not contain, an MPEG-TS read-back inference) and its Orchestrator Correction/Addendum, which is this plan's evidence"
provides:
  - "PesTimestampRecord / PidStats::pes_timestamps / kMaxPesTimestampRecordsTotal in src/probe/ts_scan.h -- container-truth PES header PTS/DTS presence and values, read from ISO/IEC 13818-1 section 2.4.3.7 bytes, never inferred by libavformat's read-back heuristics"
  - "detail::parse_pes_timestamps: a bounds-checked, pure PES header parser (no_pes/no_timestamps/pts_only/pts_dts/excluded_stream_id/malformed/truncated_in_packet), never fabricates a timestamp"
  - "detail::record_pes_timestamp: the same bounded-seam shape as 05-07's record_discontinuity_offset, now budgeted globally (kMaxPesTimestampRecordsTotal=5,000,000) across every PID"
  - "detail::apply_container_dts / detail::ContainerDtsJoin: a pure, unit-tested pos+pts join from PacketRecord to PesTimestampRecord, ready for 05-20 to wire into the orchestrator's DTS-axis consumers"
affects: [timeline-analysis, ts-scan, probe-layer, dts-monotonic]

actuals:
  tokens: 8400
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "PES header parse fused into the existing process_packet walk at the point af.payload_start_index/hf.payload_start are already known -- never a second adaptation-field parse (grep -c 'parse_adaptation(' stayed at 2 throughout: the definition and its one call site)."
    - "A global (not per-PID) bounded-record budget threaded by reference through run_ts_scan/process_packet, next to cc_states/pmt_owner_by_pid -- the same pattern 05-07's per-PID discontinuity offset seam established, generalized to a cross-PID shared budget for T-05-68."
    - "apply_container_dts mirrors src/analyzers/timeline/discontinuities.cpp's own is_flagged join: std::lower_bound over an ascending per-PID offset list, confirmed by a second field (there: byte range; here: raw pts equality) before trusting the match."

key-files:
  created: []
  modified:
    - src/probe/ts_scan.h
    - src/probe/ts_scan.cpp
    - tests/unit/test_ts_scan.cpp

key-decisions:
  - "PID range for the PES parse is 0x0010..0x1FFE, excluding any PID currently recorded as a PMT PID in pmt_owner_by_pid -- PAT (0x0000) is excluded by the range floor and the null PID (0x1FFF) by the range ceiling, matching the plan's own action text exactly; no new exclusion logic was needed beyond the existing pmt_owner_by_pid table."
  - "The PES header parser's priority order (no_pes, excluded_stream_id, truncated_in_packet, malformed, no_timestamps, pts_only/pts_dts) follows the prose order in the plan's own action text literally: truncation (both the fixed-9-byte and the 9+PES_header_data_length checks) is decided before any malformed-flags check, so a short PES_header_data_length that IS satisfied by the available bytes reports malformed (the declared length itself is wrong), while one that is NOT satisfied reports truncated_in_packet (the packet ran out) -- verified against the plan's own two boundary tests (header_data_length=4 -> malformed; only 14 of 19 declared bytes -> truncated_in_packet)."
  - "Task 3's stride join-rate measurement used a throwaway g++-compiled scratch tool (not committed, not part of the build) linked directly against src/probe/ts_scan.cpp and the vcpkg tl-expected header, to print PidStats::pes_timestamps for PID 256 against ffprobe's own packet pos/pts for the same PID -- confirmed a constant +4 byte offset shift on tests/fixtures/ts_192.ts (the M2TS 4-byte timestamp prefix ts_scan's own sync-byte offset includes but libavformat's PacketRecord::pos excludes) and a constant +16 byte shift on tests/fixtures/ts_204.ts, both exact across all 50 measured video-PID records. Per the plan's own A1 instruction, this shift is recorded, not adapted, in this plan."

patterns-established:
  - "A PES-header byte-buffer test-construction convention (build_pes_header/encode_ts_field helpers in test_ts_scan.cpp) mirroring build_packet's own hand-built-TS-packet convention already established in the same file -- available to any future plan that needs to construct a synthetic PES header."

requirements-completed: [TIME-04]

coverage:
  - id: D1
    description: "ts_scan parses the PES header (ISO/IEC 13818-1 section 2.4.3.7) at every unit-start packet on an elementary-stream PID, recording PesTimestampRecord entries with exact PTS/DTS presence and values -- proven against the real Gap 4 fixture (timeline_start_shift.ts: PID 256 100 PTS-only records, first five 128090/131690/135290/138890/142490 strictly increasing; PID 257 13 records)"
    requirement: "TIME-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp#ts_scan - PES timestamps on timeline_start_shift.ts"
        status: pass
    human_judgment: false
  - id: D2
    description: "detail::parse_pes_timestamps rejects every malformed or truncated PES header and never fabricates a timestamp -- 13 hand-built byte-buffer TEST_CASEs pin every PesParseStatus outcome including the 33-bit maximum PTS, a cleared marker bit, a forbidden PTS_DTS_flags value, and both truncation boundaries"
    requirement: "TIME-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp#ts_scan - PES parse: ... (13 TEST_CASEs)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The PES record list is bounded by a global cap (kMaxPesTimestampRecordsTotal); reaching it sets the truncation flag and drops nothing silently"
    requirement: "TIME-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp#ts_scan - PES record budget: a budget of 2 truncates after two records, budget reaches 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "detail::apply_container_dts joins PacketRecords to PES records on (pos==offset AND raw pts==record pts); a joined packet's dts becomes the record's DTS when present or its PTS when PTS-only; unjoined and negative-pos packets keep their value unchanged -- 7 TEST_CASEs cover both record shapes, a pts mismatch, a negative pos, the AV_NOPTS_VALUE sentinel, an empty record list, and two same-pos packets joining independently"
    requirement: "TIME-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp#ts_scan - PES join: ... (7 TEST_CASEs)"
        status: pass
    human_judgment: false
  - id: D5
    description: "No golden and no existing ts_scan test moved -- the three designated-leg ts_scan goldens and the inspect_container golden ran (not skipped) and matched byte-for-byte under MEDIADIFF_DESIGNATED_LEG=1, with git diff --stat -- tests/golden/ empty"
    verification:
      - kind: integration
        ref: "MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure (963/963 passing, only unit.console_vt skipped)"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 15: PES Header Timestamps and the Container-DTS Join Summary

**ts_scan now reads per-PES PTS/DTS presence and values directly from MPEG-TS PES headers (ISO/IEC 13818-1 section 2.4.3.7), and a new pure `detail::apply_container_dts` join is ready to substitute that container truth for libavformat's read-back DTS inference on `timeline.dts_monotonic`.**

## Performance

- **Duration:** ~50 min
- **Started:** 2026-09-18
- **Completed:** 2026-09-18
- **Tasks:** 3/3 completed
- **Files modified:** 3

## Accomplishments

- `PesTimestampRecord` (offset/pts/dts/dts_present), `PidStats::pes_timestamps` and its four `pes_headers_*` counters, and `kMaxPesTimestampRecordsTotal=5,000,000` in `src/probe/ts_scan.h` -- the container-truth seam Gap 4 needs.
- `detail::parse_pes_timestamps`: a bounds-checked, pure PES header parser fused into the existing `process_packet` walk (never a second adaptation-field parse -- `grep -c "parse_adaptation("` stayed at 2 throughout this plan). Every reject path (`no_pes`, `excluded_stream_id`, `truncated_in_packet`, `malformed`) is proven never to fabricate a timestamp.
- `detail::record_pes_timestamp`: the same bounded-seam shape as 05-07's `record_discontinuity_offset`, budgeted globally across every PID (T-05-68).
- `detail::apply_container_dts` / `detail::ContainerDtsJoin`: a pure join from `PacketRecord` to `PesTimestampRecord` on `(pos==offset AND raw pts==record pts)`, mirroring `discontinuities.cpp`'s own `is_flagged` binary-search join pattern -- unwired, tested, and ready for 05-20.
- Verified against the real Gap 4 fixture (`timeline_start_shift.ts`): PID 256 (video) yields exactly 100 PTS-only records, first five PTS 128090/131690/135290/138890/142490 strictly increasing, matching 05-VERIFICATION.md's own reproduction exactly; PID 257 (audio) yields 13 records.
- Stride join-rate measurement (assumption A1): on non-188-byte strides, ts_scan's own recorded offset is a constant shift ahead of libavformat's own `PacketRecord::pos` -- +4 bytes on `ts_192.ts` (the 4-byte M2TS timestamp prefix ts_scan includes in its own sync-byte offset but libavformat's `pos` excludes), +16 bytes on `ts_204.ts`, both exactly constant across all 50 measured video-PID records. Recorded, not adapted, per the plan's own instruction.
- Full suite: 963/963 passing (941 baseline + 22 new: 1 real-fixture test, 13 parse tests, 1 budget test, 7 join tests), same 6 pre-existing skips. Re-ran under `MEDIADIFF_DESIGNATED_LEG=1`: still 963/963, all three `ts_scan_golden` tests and the `inspect_container` golden ran (not skipped) and matched byte-for-byte; `git diff --stat -- tests/golden/` is empty.

## Task Commits

Each task was committed atomically:

1. **Task 1: PES timestamps flow from TS bytes to PidStats on the real fixture** - `09617eb` (feat)
2. **Task 2: The PES parser rejects every malformed or truncated header** - `7dfe70b` (test)
3. **Task 3: apply_container_dts joins demuxer packets to PES truth** - `b4178d3` (feat)

**Plan metadata:** (this commit)

_Note: Task 2 is a test-only commit -- all 14 hand-built behavior cases passed against Task 1's implementation unmodified, so no parser defect was found or fixed._

## Files Created/Modified

- `src/probe/ts_scan.h` -- `PesTimestampRecord`, `PidStats` PES fields, `kMaxPesTimestampRecordsTotal`, `detail::PesParseStatus`/`PesParseResult`/`parse_pes_timestamps`/`record_pes_timestamp`, `detail::ContainerDtsJoin`/`apply_container_dts`, `#include "probe/packet_scan.h"`
- `src/probe/ts_scan.cpp` -- the PES header parse fused into `process_packet`, the global `pes_budget` threaded through `run_ts_scan`, `apply_container_dts`'s implementation
- `tests/unit/test_ts_scan.cpp` -- 1 real-fixture TEST_CASE (Task 1), 13 hand-built parse TEST_CASEs + 1 budget TEST_CASE (Task 2), 7 hand-built join TEST_CASEs (Task 3)

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- PES-parse PID range (`0x0010..0x1FFE`, excluding known PMT PIDs) needed no new exclusion logic beyond the existing `pmt_owner_by_pid` table.
- The parser's priority order (no_pes -> excluded_stream_id -> truncated_in_packet -> malformed -> no_timestamps -> pts_only/pts_dts) follows the plan's own prose order literally, verified against its own two boundary tests.
- Task 3's stride measurement used an uncommitted scratch tool (g++, linked directly against `ts_scan.cpp`) rather than any shipped code change -- the measurement itself, not the tool, is the deliverable.

## Deviations from Plan

None - plan executed exactly as written. Every acceptance criterion in all three tasks was met on the first implementation attempt; Task 2's hand-built tests exposed no parser defect requiring a fix.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `detail::apply_container_dts` is a tested, pure join ready for 05-20 to wire into `dts_monotonic` (and any other DTS-axis consumer, per the plan's own flagged item: `size.stream_bitrate`, `jitter_vfr.cpp`'s `CadenceAxis::dts`) and to correct the declared finding sets that currently pin the inferred tie.
- No blockers for 05-16 onward. This plan touched only `src/probe/ts_scan.{h,cpp}` and its own unit tests -- `src/probe/orchestrator.cpp` and every analyzer/integration-test declared set are untouched, exactly as scoped.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED
