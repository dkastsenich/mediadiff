---
phase: 03-probe-layer-container-size
plan: 01
subsystem: core-model
tags: [cpp20, nlohmann-json, catch2, skip-reason, tolerance, rational-arithmetic]

requires:
  - phase: 02-core-engine
    provides: >
      SkipReason enum, Measurement/Finding structs, compare_fingerprints seam,
      snapshot read/write, checked_mul/checked_sub/checked_add, the JSON/JUnit
      report renderers, and the checks.def/gen_registry.py check-registration
      machinery this plan extends.
provides:
  - "SkipReason extended from 11 to 14 enumerators (partial_scan, insufficient_data, no_timing_data), all four consuming sites (model.h, json.cpp, junit.cpp, docs/schema/report-1.0.json) moved together"
  - "Measurement.estimated (bool) — D-03's estimate marker, round-trips through core/snapshot.cpp with strict boolean-typed rejection on read"
  - "Finding.evidence (nlohmann::ordered_json) — populated at exactly one seam in compare/engine.cpp, closes Broken Window #1's evidence half"
  - "detail::checked_div in core/rational.h — the fifth overflow-checked primitive, guards zero divisor and INT64_MIN / -1"
  - "compare/tol.cpp's D-03 tolerance widening: kEstimatedToleranceFactor (3x) applied via checked_mul when either side of a tol comparison carries estimated=true"
  - "03-CHECK-ROSTER.md — the approved 27-id Phase-3 check-id roster, single source of truth for plans 03-02/03-04/03-05/03-06/03-08/03-09"
affects: [03-02-tracer, 03-04-topology-meta, 03-05-mp4, 03-06-mkv, 03-08-ts, 03-09-size]

actuals:
  tokens: 12000
  tasks: 4
  commits: 4

tech-stack:
  added: []
  patterns:
    - "Exhaustive switch, no default: arm, trailing fallback return — extending SkipReason requires touching model.h + both renderer switches + the schema enum in one commit, verified by tests reading the schema at test time rather than hardcoding its vocabulary a second time."
    - "Single-seam field propagation — Finding.evidence is populated only in compare/engine.cpp after a comparator returns, so no comparator has to change and none can forget."

key-files:
  created:
    - tests/unit/test_measurement_provenance.cpp
    - tests/unit/test_rational.cpp
    - .planning/phases/03-probe-layer-container-size/03-CHECK-ROSTER.md
  modified:
    - src/core/model.h
    - src/core/rational.h
    - src/core/snapshot.cpp
    - src/compare/engine.cpp
    - src/compare/tol.cpp
    - src/report/json.cpp
    - src/report/junit.cpp
    - docs/schema/report-1.0.json
    - tests/unit/test_tolerance.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Roster approved as-proposed (auto-selected: Task 4's checkpoint carried gate=\"blocking\", not \"blocking-human\", so auto-mode's standard checkpoint:decision rule selected the plan's own first-listed, recommended option)."
  - "junit.cpp/markdown.cpp were NOT extended to render Finding.evidence — neither format has an existing slot for detail text, and adding one would change an existing golden's structure (plan's own explicit instruction). The JSON report is evidence's required carrier for this plan."
  - "compare_tol's D-03 widening tests call compare_tol directly against a hand-built CheckDef (mirroring tests/unit/test_glob.cpp's synthetic-CheckDef convention) rather than through a registered check id, so the overflow test could use a tolerance magnitude no real check declares without adding one solely to reach it."

patterns-established:
  - "Hand-built CheckDef via C++20 designated initializers for a comparator unit test that needs a tolerance magnitude/shape no registered check declares."

requirements-completed: [SIZE-01, CONT-07, PROBE-09]

coverage:
  - id: D1
    description: "SkipReason extended to 14 enumerators; all four consuming sites (model.h, json.cpp, junit.cpp, schema) carry them; no default: arm introduced"
    requirement: "PROBE-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#skip_reason: every SkipReason enumerator round-trips through the JSON renderer to its own snake_case spelling"
        status: pass
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#skip_reason: skip_reason_text (junit) agrees with skip_reason_to_string (json) for all 14 enumerators"
        status: pass
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#skip_reason: the JSON renderer's output vocabulary is exactly the set declared in docs/schema/report-1.0.json's skip_reason enum"
        status: pass
      - kind: integration
        ref: "tests/integration/test_json_schema.cpp (full suite, unmodified, still green against the edited schema)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Measurement.estimated survives a snapshot write/read round trip; a non-boolean 'estimated' key is rejected, never coerced"
    requirement: "CONT-07"
    verification:
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#measurement provenance: Measurement.estimated survives a snapshot write/read round trip"
        status: pass
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#measurement provenance: a snapshot whose 'estimated' key is not a boolean is rejected, never coerced"
        status: pass
    human_judgment: false
  - id: D3
    description: "Finding.evidence populated at exactly one seam (compare/engine.cpp) from paired measurements' evidence, rendered as a JSON object; null when neither side carries evidence, so every Phase-2 golden is unchanged"
    requirement: "PROBE-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#measurement provenance: Finding.evidence is populated from both sides' evidence at the compare seam and rendered as a JSON object"
        status: pass
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#measurement provenance: a comparison where neither side carries evidence still emits evidence:null"
        status: pass
      - kind: other
        ref: "git status --porcelain tests/golden/ (empty)"
        status: pass
    human_judgment: false
  - id: D4
    description: "detail::checked_div guards zero divisor and INT64_MIN / -1 before any division executes"
    requirement: "SIZE-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_rational.cpp#checked_div: a zero divisor returns false for any dividend, never executing the division"
        status: pass
      - kind: unit
        ref: "tests/unit/test_rational.cpp#checked_div: INT64_MIN / -1 is the one true overflow case and is rejected, never executed"
        status: pass
    human_judgment: false
  - id: D5
    description: "compare_tol widens the resolved tolerance 3x for estimated measurements via checked integer arithmetic, bounded (not a bypass), overflow-safe"
    requirement: "SIZE-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#tolerance widening: an estimated measurement passes at a delta beyond the declared tolerance but within 3x it, and the message names the widening"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#tolerance widening: a delta beyond 3x the declared tolerance still fails even when both sides are estimated -- widening is bounded, not a bypass"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#tolerance widening: a tolerance magnitude large enough that the 3x multiply would overflow int64 returns the existing overflow finding, never a silently wrapped threshold"
        status: pass
    human_judgment: false
  - id: D6
    description: "Approved 27-id Phase-3 check roster recorded on disk"
    verification:
      - kind: other
        ref: "grep -c '^| container\\.\\|^| meta\\.\\|^| size\\.' 03-CHECK-ROSTER.md == 27"
        status: pass
    human_judgment: false

duration: 50min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 1: Core Model Primitives + Check-ID Roster Summary

**SkipReason grown to 14 values, Measurement.estimated/Finding.evidence wired through snapshot and compare, detail::checked_div added, D-03's 3x tolerance widening implemented, and the 27-id Phase-3 check roster approved on disk.**

## Performance

- **Duration:** ~50 min
- **Tasks:** 4/4 completed
- **Files modified:** 10 modified, 3 created

## Accomplishments

- `SkipReason` extended from 11 to 14 enumerators (`partial_scan`, `insufficient_data`,
  `no_timing_data`), with `src/report/json.cpp`, `src/report/junit.cpp`, and
  `docs/schema/report-1.0.json`'s closed enum all moved in the same commit — the exhaustive
  switches (no `default:` arm) meant a partial edit would leave the build red.
- `Measurement.estimated` (bool) added as data the comparison layer reads (D-03), round-tripping
  through `core/snapshot.cpp`'s write/read cycle; emitted only when `true` so every pre-existing
  golden stays byte-identical, and a present-but-non-boolean `estimated` key is rejected as
  `ErrorKind::input_unsupported` naming the check id, never coerced.
- `Finding.evidence` added and populated at exactly one seam — `compare/engine.cpp`'s
  `compare_fingerprints`, after a comparator returns — closing the evidence half of Broken
  Window #1 (`.planning/WINDOWS.md` entry 1). Null when neither side carries evidence, so every
  Phase-2 golden is unchanged; `Finding.delta` stays `null` deliberately (its own half of window
  #1 remains open, tracked separately per the plan's own instruction).
- `detail::checked_div` added to `core/rational.h` alongside its four siblings
  (`checked_mul`/`checked_sub`/`checked_add`/`checked_negate`), guarding the zero-divisor and
  `INT64_MIN / -1` UB cases with no widening trick needed.
- `compare/tol.cpp` widens the resolved fail/warn thresholds by `kEstimatedToleranceFactor` (3x,
  a single named constant) via `detail::checked_mul` when either side of a `tol` comparison
  carries `estimated == true`; an overflowing widen routes through the existing
  `overflow_finding` path rather than silently falling back to the unwidened threshold. The
  widening is bounded (a delta beyond 3x still fails) and stated in the finding's own message.
- `.planning/phases/03-probe-layer-container-size/03-CHECK-ROSTER.md` created: the 27-id Phase-3
  roster, approved `approve-as-proposed`, recording the three ids added beyond doc 02's literal
  row count and the two scope decisions (per-stream `size.peak_bitrate`; `container.ts.*`
  program-scoping by `program_number`, not array position).

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend SkipReason across all four consuming sites in one commit** - `9d2a7a4` (feat)
2. **Task 2: Add Measurement.estimated and Finding.evidence with a single propagation seam** - `df8e15d` (feat)
3. **Task 3: Add checked_div to rational.h and widen tolerance for estimated measurements (D-03)** - `7c093c0` (feat)
4. **Task 4: Approve the Phase-3 check-id roster (ids are forever)** - `d004e59` (docs)

## Files Created/Modified

- `src/core/model.h` - `SkipReason` +3 enumerators; `Measurement.estimated`; `Finding.evidence`
- `src/core/rational.h` - `detail::checked_div`
- `src/core/snapshot.cpp` - `estimated` field read (strict-boolean, rejects else)/write (omit-when-false)
- `src/compare/engine.cpp` - the single evidence-propagation seam in `compare_fingerprints`
- `src/compare/tol.cpp` - `kEstimatedToleranceFactor`, D-03 widening, widened-suffix messages
- `src/report/json.cpp` - 3 new `skip_reason_to_string` arms; `evidence` rendered from `Finding.evidence`
- `src/report/junit.cpp` - 3 new `skip_reason_text` arms
- `docs/schema/report-1.0.json` - `skip_reason` enum +3 values
- `tests/unit/test_measurement_provenance.cpp` - new: SkipReason round-trip/agreement/schema tests (Task 1), `estimated`/`evidence` behavior tests (Task 2)
- `tests/unit/test_rational.cpp` - new: `checked_div` coverage
- `tests/unit/test_tolerance.cpp` - extended: D-03 widening tests via a hand-built `CheckDef`
- `tests/unit/CMakeLists.txt` - registers both new test files; adds `MEDIADIFF_REPORT_SCHEMA` compile definition
- `.planning/phases/03-probe-layer-container-size/03-CHECK-ROSTER.md` - new: approved roster

## Decisions Made

- **Roster approval:** `approve-as-proposed`, auto-selected. Task 4's checkpoint carried
  `gate="blocking"` (not `blocking-human`), so per `<checkpoint_protocol>`'s auto-mode rule the
  first-listed (recommended) option was selected without pausing for human input, since
  `workflow.auto_advance` is `true` in `.planning/config.json`.
- **Evidence rendering scope:** JSON is the only renderer extended to show `Finding.evidence` in
  this plan. `junit.cpp` and `markdown.cpp` were deliberately left untouched — neither format has
  an existing slot for detail text, and adding one would change an existing golden's structure,
  per the plan's own explicit instruction ("if adding one would change an existing golden's
  structure, do not add it in this task").
- **D-03 widening test construction:** rather than adding a synthetic check to
  `tests/support/test_checks.def` solely to reach an overflow-triggering tolerance magnitude,
  `compare_tol` is called directly against a hand-built `CheckDef` (C++20 designated
  initializers), mirroring `tests/unit/test_glob.cpp`'s own established convention for a
  synthetic registry entry.

## Deviations from Plan

### Auto-fixed / self-corrected

**1. [Git-history hygiene, not a functional defect] `src/report/json.cpp`'s `evidence` rendering
line landed in Task 1's commit instead of Task 2's**
- **Found during:** staging Task 2's commit (`git diff --staged --stat` showed `json.cpp` had
  nothing left to stage).
- **Cause:** Task 1 and Task 2 were both implemented in the working tree before any commit was
  made (batch implementation ahead of verification), and Task 1's commit staged
  `src/report/json.cpp` as a whole file rather than hunk-by-hunk, capturing the `evidence`
  rendering edit (Task 2's own change) alongside the `skip_reason_to_string` arms (Task 1's
  change). `src/core/model.h`, by contrast, was correctly split hunk-by-hunk via `git add -p`
  across the two commits.
- **Impact:** `9d2a7a4` (Task 1's commit), checked out in isolation, would **not compile** —
  `json.cpp` references `finding.evidence`, a field `Finding` does not carry until `df8e15d`
  (Task 2's commit). The final tree (after `df8e15d`) is correct, fully tested, and green; this
  affects only `git bisect`-style isolation of the intermediate Task 1 commit, not the delivered
  code.
- **Not fixed via amend:** per this executor's standing instructions, commits are never amended
  except on explicit user request; a corrective revert-and-reapply commit was judged not worth
  the added history noise for a local, unshared branch. Documented here instead.

**Total deviations:** 1, git-history-hygiene only (no code/test/behavior deviation).
**Impact on plan:** None on delivered functionality — all `<must_haves>` and acceptance criteria
below are met by the final tree.

## Issues Encountered

- **Plan's Task 1 acceptance criterion is over-broad for `junit.cpp`.** The criterion
  `grep -v '^\s*//' src/report/junit.cpp | grep -c 'default:'` reports `1`, not `0` — but that
  one `default:` arm is in `xml_escape`'s pre-existing switch over `char` (unrelated to
  `SkipReason`, present before this plan and untouched by it). The invariant the criterion
  actually intends — `skip_reason_text`'s own switch carries no `default:` arm — holds true,
  confirmed by direct inspection of `src/report/junit.cpp`'s `skip_reason_text` function. Not
  fixed (would require an unrelated, out-of-scope change to `xml_escape`'s char-switch style).
  `src/report/json.cpp` alone reports `0` as expected.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plans 03-02 (tracer), 03-04 through 03-09 can now register checks against a frozen 27-id
  roster and use `Measurement.estimated`/`Finding.evidence` as data channels their own
  analyzers populate.
- `detail::checked_div` is available for 03-09's `size.*` window-boundary math.
- No blockers identified for 03-02.

## Self-Check: PASSED

- `tests/unit/test_measurement_provenance.cpp` — FOUND
- `tests/unit/test_rational.cpp` — FOUND
- `.planning/phases/03-probe-layer-container-size/03-CHECK-ROSTER.md` — FOUND
- `9d2a7a4` — FOUND in `git log --oneline --all`
- `df8e15d` — FOUND in `git log --oneline --all`
- `7c093c0` — FOUND in `git log --oneline --all`
- `d004e59` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
