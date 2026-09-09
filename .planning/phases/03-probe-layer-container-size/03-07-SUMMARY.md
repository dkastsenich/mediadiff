---
phase: 03-probe-layer-container-size
plan: 07
subsystem: probe-layer
tags: [mpeg-ts, ts-scan, pcr, pat-pmt, continuity-counter, cpp20, catch2, bounds-checking]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      src/probe/{pass.h, demux_session.{h,cpp}, orchestrator.{h,cpp}} and the
      Pass/AnalyzerSpec/ProbeResults pass-declaration seam (03-02-PLAN.md);
      the two-AnalyzerSpec-per-family pattern and BoundedReader/checked-
      arithmetic conventions proven by src/probe/bmff_scan.{h,cpp}
      (03-05-PLAN.md) and src/probe/ebml_scan.{h,cpp}
      plus src/core/container_family.{h,cpp} (03-06-PLAN.md).
provides:
  - "src/probe/ts_scan.{h,cpp}: a bounded, libav-free MPEG-TS packet-stream
    walker -- stride autodetection (188/192/204, multi-sync confirmed),
    forward-only resync after corruption, a structurally-bounded 8192-entry
    per-PID table, exact bit-level PCR extraction, PAT/PMT parsing with
    version-change tracking and section repetition offsets, and an exact-
    rational mux-rate estimate (run_ts_scan, PidStats, TsScanResult,
    TsProgram, PcrSample, MuxRateEstimate)"
  - "detail::step_continuity: the ISO 13818-1 section 2.4.3.3 continuity-
    counter carve-outs as a single pure, separately-testable function
    (PidContinuityState, ContinuityIncrement, ContinuityStepResult)"
  - "Pass::ts_scan wired into the orchestrator's pass union; ProbeResults::ts
    -- no analyzer declares this pass yet (container.ts.* is plan 03-08),
    so this arm is exercised directly via run_ts_scan() in this plan's own
    tests, not through a full probe run"
  - "6 new fixture recipes in scripts/gen_corpus.sh (ts_single.ts/_copy.ts,
    ts_204.ts, ts_192.ts, ts_multiprogram.ts, ts_ccgap.ts), the 192-/204-
    byte variants produced by deterministic post-mux Python padding since
    ffmpeg's own mpegts muxer only ever writes native 188-byte packets"
affects: [03-08-container-ts-checks, 03-10-tsduck-golden-comparison]

actuals:
  tokens: 23000
  tasks: 3
  commits: 1

tech-stack:
  added: []
  patterns:
    - "BoundedReader duplicated (not shared) a third time, in ts_scan.cpp -- mirrors 03-05/03-06's own recorded non-decision: the three binary grammars (ISOBMFF box tree, EBML element tree, MPEG-TS fixed-size packet stream) diverge enough that a shared abstraction would add indirection without removing real duplication."
    - "The per-PID table (8192 entries, the full 13-bit PID domain) is heap-allocated behind a unique_ptr<std::array<PidStats,8192>> rather than held by value on TsScanResult -- keeps TsScanResult itself small (safe to move/return by value through mediadiff::expected) while the array's own several-hundred-kilobyte footprint never risks a small-default-stack thread. Two other bounded, fixed-size-8192 auxiliary arrays inside run_ts_scan (cc_states, pmt_owner_by_pid) use std::vector for the same stack-frame-size reasoning, not because they are unbounded."
    - "The continuity-counter carve-outs are a PURE function (detail::step_continuity) taking the previous per-PID state and one packet's fields, returning the new state plus which counter (if any) to increment -- proven directly against a nine-case hand-verified table (test_ts_continuity.cpp) with no packet bytes constructed at all, and independently confirmed to break at least one named test when each of the three carve-outs was manually reverted during development (see Deviations/Issues below)."
    - "The mux-rate estimate is stored as an UNREDUCED exact rational (bytes_per_second_num/den), never resolved via division at all -- not merely 'avoid double', the division itself never happens in this scanner, deferring it to whatever downstream consumer (plan 03-08) needs the actual rate."

key-files:
  created:
    - src/probe/ts_scan.h
    - src/probe/ts_scan.cpp
    - tests/unit/test_ts_scan.cpp
    - tests/unit/test_ts_continuity.cpp
  modified:
    - src/probe/pass.h
    - src/probe/orchestrator.cpp
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/unit/CMakeLists.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "src/core/container_family.cpp required NO change -- confirmed empirically (mediadiff inspect ts_single.ts --json reports container.format == \"mpegts\") that the existing container_family_token mapping (\"mpegts\" -> \"ts\") already covers the only libav format-name spelling this scanner's own fixtures produce. Left unmodified rather than editing it speculatively; the plan's own files_modified listing was a planning-time approximation, not a strict per-file mandate once verification showed no gap."
  - "The mux-rate estimate is derived from the FIRST valid consecutive same-PID PCR pair only (in file order), not averaged across every valid pair in the file -- doc 02's own clause describes the derivation singularly (\"offset delta / PCR delta\"), and Task 2's own behaviors (7: \"the mux-rate estimate from two consecutive PCRs\"; 8: \"fewer than two PCRs yields no estimate\") both describe a single-pair derivation. `skipped_pairs` still accumulates across the WHOLE scan (every non-positive-delta pair encountered, not just ones before the first valid pair), so a caller can see how much of the file's PCR signal was unusable."
  - "Same-PID PCR pairing uses flat list-adjacency (pcr_samples[i-1]/pcr_samples[i], skipped silently -- not counted in skipped_pairs -- when their PIDs differ) rather than grouping every PCR PID's samples into independent per-PID sequences first. Correct and sufficient for every fixture this plan's tests exercise (each has exactly one PCR PID); a file whose PCR samples from two DIFFERENT PCR PIDs interleave tightly enough to break list-adjacency for same-PID pairs would under-count valid pairs for that PID. Flagged here per this plan's own <output> instruction; multi-program PCR scoping is doc 02 section 6 policy that 03-08 owns, not a defect in this scanner's structural extraction."
  - "PSI section reassembly is explicitly NOT attempted -- a PAT/PMT section is discarded (discarded_sections incremented) unless it starts AND fully fits within the single TS packet payload it was first observed in (payload_unit_start_indicator=1 required). This is the plan's own literal prohibition (T-3-33) rather than a shortcut: doc 02's PROBE-06 scope is structural observation, and section reassembly across packet boundaries is out of scope for this scanner."

patterns-established:
  - "The pure-state-machine-function shape for a carve-out-heavy per-item classification rule (detail::step_continuity) -- separately testable via a hand-verified table with no bytes constructed at all -- is now proven twice this phase's neighboring domain (the CC carve-outs here; the elst/CodecDelay classification logic in 03-05/03-06 was struct-shaped, not function-shaped) and is the recommended shape for any future rule this project needs to prove against ISO-sanctioned exceptions rather than the naive 'compare and flag' reading of a spec."

requirements-completed: [PROBE-06, PROBE-07]

coverage:
  - id: D1
    description: "ts_scan autodetects 188/192/204-byte packet stride via multi-sync (5-confirmation) detection, resyncs forward-only after a corrupted region while recording bytes skipped, and rejects a single stray sync byte as insufficient confirmation."
    requirement: PROBE-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp -- stride 188/192/204 detected in three separate assertions; single-stray-sync-byte yields complete=false; ts_ccgap.ts resyncs with resync_bytes_skipped > 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "The per-PID table is a structurally bounded 8192-entry array (never an unordered_map), heap-allocated to avoid stack-size risk; a stream cycling through every possible PID completes without unbounded allocation, and the packet-accounting identity (sum of per-PID counts + null_packets == total_packets) holds exactly."
    requirement: PROBE-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp -- all-8192-PID stream completes with exact per-PID/null counts; ts_multiprogram.ts's packet-accounting identity holds exactly"
        status: pass
    human_judgment: false
  - id: D3
    description: "PCR is extracted as base*300+extension in 27 MHz ticks with the 6 reserved bits skipped, each sample carrying its byte offset; PCR_flag=0 yields no PCR; a too-short adaptation field with PCR_flag=1 yields no PCR and is malformed, never reading adjacent bytes."
    requirement: PROBE-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp -- PCR extraction asserted against a hand-computed literal; PCR_flag=0 and too-short-field cases both proven"
        status: pass
    human_judgment: false
  - id: D4
    description: "PAT yields the program-number-to-PMT-PID map (one entry for a single-program TS, two for a two-program TS); PMT yields version_number/PCR_PID/ES PIDs, with a version bump counting exactly one change (first sighting is baseline); PAT/PMT section repetition offsets are all recorded."
    requirement: PROBE-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp -- single- and two-program PAT parsing; hand-built PMT version-bump case asserts version_changes==1; real-file offset-repetition assertions"
        status: pass
    human_judgment: false
  - id: D5
    description: "The mux-rate estimate is an exact, never-divided rational from the first valid consecutive same-PID PCR pair; a non-positive PCR delta pair is skipped (not unwrapped); fewer than two PCRs yields no estimate at all, never a zero; the estimate self-identifies as estimated=true."
    requirement: PROBE-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp -- exact-formula recomputation against a real fixture; hand-built non-positive-delta and single-PCR cases"
        status: pass
    human_judgment: false
  - id: D6
    description: "A section_length larger than the packet's own available bytes is discarded (never parsed from a truncated buffer); an oversized adaptation_field_length is malformed without reading past the packet -- both increment their own counters and the scan continues rather than aborting."
    requirement: PROBE-06
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan.cpp -- oversized section_length and oversized adaptation_field_length both proven to discard/malform and continue"
        status: pass
    human_judgment: false
  - id: D7
    description: "All three ISO 13818-1 section 2.4.3.3 continuity-counter carve-outs (payload-absent exemption, one permitted duplicate, flagged-discontinuity reset) are implemented as a single pure, separately-testable function and proven by a nine-case hand-verified table; each carve-out independently confirmed to break at least one named test when manually reverted."
    requirement: PROBE-07
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_continuity.cpp -- nine named TEST_CASEs, one per behavior, expectations written as literals"
        status: pass
      - kind: other
        ref: "manual mutation testing during development: reverting carve-out (a) broke test 3 (payload/af-only/payload), carve-out (b) broke test 4 (duplicate), carve-out (c) broke tests 5 and 8 (discontinuity) -- see Issues Encountered"
        status: pass
    human_judgment: false
  - id: D8
    description: "Flagged (cc_discontinuities) and unflagged (cc_errors) counts are maintained independently per PID, and the first unflagged error's byte offset is recorded per PID."
    requirement: PROBE-07
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_continuity.cpp -- independent per-PID tally test; tests/unit/test_ts_scan.cpp's per-packet CC bookkeeping runs through the same PidStats fields the offset-recording behavior depends on"
        status: pass
    human_judgment: false
  - id: D9
    description: "The full suite (491 tests: 365 unit + 126 integration) passes with no regressions; all four lint scripts pass; the library and both test/CLI binaries build clean under -Wall -Wextra -Werror."
    verification:
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure: 491/491 pass (459 baseline + 32 new: 23 test_ts_scan.cpp + 9 test_ts_continuity.cpp)"
        status: pass
      - kind: other
        ref: "scripts/lint_check_id_strings.sh, lint_dead_code_after_fail.sh, lint_eng16.sh, lint_fixture_case_collisions.sh: all exit 0"
        status: pass
    human_judgment: false

duration: ~40min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 7: ts_scan — Bounded MPEG-TS Packet Walk + PROBE-07 Continuity Carve-outs Summary

**A bounded, libav-free MPEG-TS packet-stream scanner (`src/probe/ts_scan.{h,cpp}`) — stride autodetection, forward-only resync, a structurally-bounded 8192-entry per-PID table, exact bit-level PCR extraction, PAT/PMT parsing with version tracking, an exact-rational mux-rate estimate, and the ISO 13818-1 §2.4.3.3 continuity-counter carve-outs as a pure, hand-verified function — building the scanner only; the `container.ts.*` checks it feeds are plan 03-08.**

## Performance

- **Duration:** ~40 min
- **Tasks:** 3/3 completed
- **Files modified:** 6 modified, 4 created (single commit)

## Accomplishments

- `src/probe/ts_scan.{h,cpp}` (PROBE-06): a `BoundedReader` (duplicated, matching bmff_scan/ebml_scan's own established non-decision) drives stride autodetection over `{188, 192, 204}` with a named 5-sync-confirmation constant, forward-only resync after corruption with bytes-skipped recorded, and a per-PID table addressed directly by the structurally-bounded 13-bit PID domain (8192 entries, heap-allocated behind `unique_ptr` rather than risked on a thread's stack). PCR is extracted at exact bit boundaries (`base*300+extension`, reserved bits skipped) with byte offset and PID recorded per sample. PAT/PMT sections (bounded to one packet each — no reassembly, T-3-33) yield the program-number-to-PMT-PID map, per-program `version_number`/`PCR_PID`/ES PID list, a version-CHANGE count (first sighting is baseline, never a change), and every PAT/PMT section-repetition byte offset. The mux-rate estimate is an exact, unreduced rational (`bytes_per_second_num/den`) from the first valid consecutive same-PID PCR pair, an `estimated=true` field documenting the D-03 origin point for plan 03-08.
- `detail::step_continuity` (PROBE-07): the three ISO 13818-1 §2.4.3.3 carve-outs — payload-absent packets exempt from CC tracking entirely, exactly one permitted duplicate per position, and a flagged discontinuity resetting expectation while incrementing its own independent counter — implemented as a single pure function, proven by a nine-case hand-verified table (`tests/unit/test_ts_continuity.cpp`) built with no packet bytes at all.
- `src/probe/pass.h`/`orchestrator.cpp`: `Pass::ts_scan` wired into the pass union and `ProbeResults::ts` added, matching `bmff_scan`/`ebml_scan`'s own wiring shape; no analyzer declares this pass yet (that is plan 03-08's job), so it is exercised directly via `run_ts_scan()` in this plan's tests.
- 6 new fixture recipes in `scripts/gen_corpus.sh`: `ts_single.ts`/`ts_single_copy.ts` (real 188-byte-stride MPEG-TS via ffmpeg's own muxer), `ts_204.ts`/`ts_192.ts` (deterministic post-mux Python padding, since ffmpeg's muxer only ever writes native 188-byte packets — confirmed empirically, not assumed), `ts_multiprogram.ts` (two independently A/V-mapped programs via `-program`, confirmed via `ffprobe -show_programs` to yield two real `program_num` entries), `ts_ccgap.ts` (a real file with a zeroed mid-stream region, proving resync against genuine corruption rather than only hand-built buffers).
- 32 new tests (`test_ts_scan.cpp`: 23, `test_ts_continuity.cpp`: 9) — full suite now 491/491 (459 baseline + 32 new).

## Task Commits

Tasks 1-3 were implemented and committed together (see Deviations for why):

1. **Tasks 1-3: ts_scan bounded packet walk + PCR/PSI/mux-rate + continuity carve-outs (PROBE-06, PROBE-07)** - `be20fc7` (feat)

## Files Created/Modified

- `src/probe/ts_scan.h` (new) — `PidStats`, `PcrSample`, `TsProgram`, `MuxRateEstimate`, `TsScanResult`, `run_ts_scan`, `detail::PidContinuityState`/`ContinuityIncrement`/`ContinuityStepResult`/`step_continuity`
- `src/probe/ts_scan.cpp` (new) — `BoundedReader` (duplicated), stride detection/resync, packet/adaptation-field/PSI parsing, the continuity state machine, mux-rate estimation
- `src/probe/pass.h` — `#include "probe/ts_scan.h"`, `ProbeResults::ts`
- `src/probe/orchestrator.cpp` — `Pass::ts_scan` execution arm and error propagation, mirroring `bmff_scan`/`ebml_scan`'s own arms
- `CMakeLists.txt` — `src/probe/ts_scan.cpp` added to `libmediadiff`'s sources, `src/probe/ts_scan.h` added to the header-compilation-gate `FILE_SET`
- `scripts/gen_corpus.sh` — 6 new fixture recipes (see Accomplishments)
- `tests/unit/{CMakeLists.txt,test_ts_scan.cpp,test_ts_continuity.cpp}` (two new test files, registered)
- `tests/fixtures/GENERATOR_MANIFEST.json` — regenerated (`generated_at` only)

## Decisions Made

See `key-decisions` in the frontmatter for the full list. Highlights:

- **`src/core/container_family.cpp` required no change**, confirmed empirically rather than assumed: `mediadiff inspect ts_single.ts --json` reports `container.format == "mpegts"`, exactly the spelling the existing `container_family_token` mapping already handles.
- **Mux-rate estimate uses the FIRST valid consecutive same-PID PCR pair**, not an average across the file — matches doc 02's own singular "offset delta / PCR delta" phrasing and this plan's own Task 2 Test 7/8 wording. `skipped_pairs` still accumulates across the whole scan for visibility.
- **PCR pairing is flat list-adjacent**, not grouped per-PID first — correct for every fixture this plan's tests exercise (one PCR PID each); a file with tightly-interleaved multi-PID PCR samples could under-count valid pairs for a given PID. Flagged for 03-08/03-10 as a scoping note, not a defect against this plan's own acceptance criteria (which describe single-pair derivation, not multi-program scoping — that is doc 02 section 6 policy, explicitly out of this plan's scope).

## Deviations from Plan

### Auto-fixed Issues

None — plan executed as written; the only adjustments were to this plan's own hand-built test fixtures (below), not to production code.

### Claude's Discretion (not a deviation, documented per this plan's own `<output>` instruction)

**Tasks 1-3 implemented and committed together.** `PidStats`/`TsScanResult`'s shared shape and the continuity state machine are used by every task's own code path with no natural split boundary, and `tests/unit/CMakeLists.txt`'s registration of both new test files landed as a single edit (adjacent lines in one hunk) — splitting into three commits would have produced an intermediate non-building state (a CMakeLists.txt referencing `test_ts_continuity.cpp` before that file existed). Mirrors 03-05/03-06's own precedent for this exact shape.

**Hand-built test fixtures initially too short for stride detection.** Several Task 2 behaviors (mux-rate edge cases, a malformed-section case, a PMT version-bump case) were first written as 1-3 hand-built packets, which cannot satisfy the 5-sync-confirmation stride detection this plan's own Task 1 design requires — `run_ts_scan` correctly reported `complete=false` for all of them (the scanner behaving exactly as designed, not a bug). Fixed by adding a `pad_to_min_packets` test helper that pads every short hand-built buffer to 5 packets with plain filler packets on an unrelated PID, rather than weakening the 5-confirmation requirement itself.

## Issues Encountered

- **A test gap discovered via manual mutation testing (this plan's own acceptance criterion: "mutating any one of the three carve-outs... makes at least one named test fail").** The first version of Behavior 3's test (`payload cc=0, af-only cc=0, payload cc=1 -> zero errors`) asserted only `tally.errors == 0`. Manually reverting carve-out (a) (the payload-absent exemption) did NOT fail this test: with the carve-out removed, the af-only packet's repeated `cc=0` matched carve-out (b)'s "one permitted duplicate" rule instead of being rejected as an error — a duplicate is not an error, so `tally.errors` stayed 0 and the mutant went undetected. Fixed (Rule 1, found before committing, not a deviation from the plan's own written behavior) by asserting the per-step `ContinuityIncrement` directly (`step2.increment == ContinuityIncrement::none`, not merely "no error") and that state is left completely untouched by the af-only packet. Re-verified: each of the three carve-outs, independently reverted, now breaks at least one named test (carve-out (a) -> test 3 fails; (b) -> test 4 fails; (c) -> tests 5 and 8 fail), confirmed by temporarily reverting each during development and observing the expected failures, then restoring the clean implementation before committing (per this plan's own acceptance criterion's explicit instruction: "record the observation in the summary, do not commit the mutation").

## User Setup Required

None — no external service configuration required (the `MEDIADIFF_FFMPEG`/system-`ffmpeg`-≥6.1 precondition was already satisfied in this environment; `scripts/gen_corpus.sh` ran successfully, including the two-program `-program` mux path, confirmed via `ffprobe -show_programs` rather than assumed to work).

## Next Phase Readiness

- `TsScanResult`'s shape is designed for plan 03-08 to consume directly: `cc_errors`/`cc_discontinuities` per PID, `pcr_samples` with offsets, `programs` with version-change counts and section-repetition offsets, and `mux_rate` as an exact rational with `estimated=true` for 03-08 to carry onto `Measurement::estimated` (D-03) — no re-scanning required.
- The multi-program PCR-pairing scoping note (flat list-adjacency, see Decisions) is the one open item worth revisiting if 03-08's multi-program policy work surfaces a real fixture where it matters; not a blocker, since every fixture this plan produced has exactly one PCR PID.
- No blockers identified for 03-08.

## Self-Check: PASSED

- `src/probe/ts_scan.h` — FOUND
- `src/probe/ts_scan.cpp` — FOUND
- `tests/unit/test_ts_scan.cpp` — FOUND
- `tests/unit/test_ts_continuity.cpp` — FOUND
- `be20fc7` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
