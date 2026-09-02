---
phase: 03-probe-layer-container-size
plan: 08
subsystem: probe-layer
tags: [mpeg-ts, ts-scan, pcr, psi, continuity-counter, multi-program, tolerance-widening, cpp20, catch2]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      src/probe/ts_scan.{h,cpp} (03-07-PLAN.md) -- TsScanResult, PidStats,
      PcrSample, TsProgram, MuxRateEstimate, exactly the shape 03-07-SUMMARY.md
      designed for this plan to consume directly with no re-scanning; the
      two-AnalyzerSpec-per-family split pattern proven by
      src/analyzers/container/mp4.cpp (03-05) and mkv.cpp (03-06); D-03's
      Measurement::estimated flag and kEstimatedToleranceFactor widening in
      src/compare/tol.cpp (03-01); the approved 27-id check roster
      (03-CHECK-ROSTER.md) -- this plan registers the six container.ts.* ids
      exactly as spelled there.
provides:
  - "src/analyzers/container/ts.cpp: the six container.ts.* checks --
    cc_errors/cc_discontinuities (kept independent, per-PID evidence),
    pcr_interval/psi_interval (D-03 estimated widening, program-scoped),
    pmt_version_churn (directly counted, never estimated, program-scoped),
    null_ratio (exact RationalValue, global scope) -- split across
    container_ts_analyzer() (ContainerFamily::ts) and
    container_ts_not_applicable_analyzer() (ContainerFamily::other)"
  - "ProgramRateContext (ts.cpp, file-local): a per-program, PID-filtered
    mux-rate estimate that sidesteps ts_scan's own flat list-adjacency
    limitation on interleaved multi-program PCR PIDs -- falls back to
    TsScanResult::mux_rate only when a program's own samples cannot
    produce one"
  - "src/compare/engine.cpp: an explicit unpaired-program topology-fail
    path (CONT-08) -- a program-scoped measurement present on only one
    side now emits Status::fail naming the mismatched program number,
    ahead of the pre-existing unpaired-continue branch that silently
    dropped it"
  - "Rule 1 fix: src/probe/ts_scan.cpp's mux-rate estimate no longer
    carries an erroneous *8 bytes-to-bits factor -- MuxRateEstimate::
    bytes_per_second_num/den now actually mean bytes/sec, matching the
    struct's own name and doc comment"
  - "20 new fixture recipes in scripts/gen_corpus.sh: PCR close/far pairs
    (-pcr_period), a single-PCR truncation, a null-ratio muxrate pair, a
    real flagged-discontinuity file (-mpegts_flags initial_discontinuity),
    and byte-patched reordered/renumbered multi-program PAT/PMT siblings
    with recomputed CRC32"
affects: [03-09-size-checks, 03-10-tsduck-golden-comparison, 03-11-verbose-render]

actuals:
  tokens: 27500
  tasks: 3
  commits: 1

tech-stack:
  added: []
  patterns:
    - "Third proof of the two-AnalyzerSpec-per-family pattern (container_ts_analyzer + container_ts_not_applicable_analyzer), now consistent across all three container families (mp4/mkv/ts)."
    - "ProgramRateContext: when a per-scope quantity needs a rate/ratio derived from a shared, potentially-sparse global signal, deriving a LOCAL estimate from the already-scope-filtered sample list first (falling back to the global one only when the local list can't produce one) is more robust than trusting a single file-wide estimate computed by flat adjacency over an interleaved stream."
    - "An explicit, unconditional-on-severity-policy Status::fail path in compare_fingerprints (mirroring the existing cross_container demotion and value_kind_mismatch paths) is the right shape for 'a structural fact about the two inputs themselves', not a per-check severity decision -- CONT-08's unpaired-program topology mismatch is the second instance of this shape after CONT-02's cross-container demotion."

key-files:
  created:
    - src/analyzers/container/ts.cpp
    - docs/checks/container.ts.cc_errors.md
    - docs/checks/container.ts.cc_discontinuities.md
    - docs/checks/container.ts.pcr_interval.md
    - docs/checks/container.ts.psi_interval.md
    - docs/checks/container.ts.pmt_version_churn.md
    - docs/checks/container.ts.null_ratio.md
    - tests/unit/test_ts_analyzer.cpp
    - tests/integration/test_container_ts.cpp
    - tests/integration/test_multiprogram.cpp
  modified:
    - src/analyzers/container/analyzers.h
    - src/probe/orchestrator.cpp
    - src/probe/ts_scan.cpp
    - src/compare/engine.cpp
    - src/core/checks.def
    - scripts/gen_corpus.sh
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_ts_scan.cpp
    - tests/integration/CMakeLists.txt
    - tests/golden/list_checks_effective.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "Rule 1 fix: ts_scan.cpp's compute_mux_rate_estimate carried offset_delta * 8 * 27000000 -- an erroneous bytes-to-bits factor that made MuxRateEstimate::bytes_per_second_num/den actually mean bits/sec despite the field's own name and ts_scan.h's explicit doc comment ('a later byte-offset-to-milliseconds conversion (plan 03-08) stays exact'). Caught by cross-checking a real fixture's independently-computed PCR spacing (a scratch Python re-implementation of the same formula) against this analyzer's own output: 11ms reported vs 91ms expected, exactly an 8x ratio. Fixed by removing the *8; the one locked assertion in test_ts_scan.cpp that literally encoded the buggy formula was corrected in the same commit. Neither file is in this plan's own files_modified list, but the bug directly and silently corrupted this plan's own central deliverable (every pcr_interval/psi_interval value would have been 8x too small, likely masking real spacing regressions under the fixed absolute-ms tolerances in checks.def) -- Rule 1 applies regardless of which prior plan introduced the defect."
  - "ProgramRateContext (ts.cpp): derives a mux-rate estimate from the PID-FILTERED PCR sample list for each program independently, rather than trusting TsScanResult::mux_rate (the scanner's single file-wide estimate) directly. Discovered necessary when ts_multiprogram.ts's own 50 PCR samples were found to alternate PID on EVERY single list entry (confirmed via a small debug harness linked directly against libmediadiff_core.a) -- ts_scan.cpp's compute_mux_rate_estimate pairs only list-ADJACENT same-PID samples, so a fully-interleaved multi-program stream never produces a mux_rate estimate at all, which would have left EVERY multi-program file's pcr_interval/psi_interval permanently insufficient_data. 03-07-SUMMARY.md itself flags this exact limitation as 'doc 02 section 6 policy that 03-08 owns, not a defect in this scanner's own structural extraction' -- resolved within this plan's own file (ts.cpp), no further change to ts_scan.cpp needed. Falls back to the scanner's own ts.mux_rate only when a program's own PID-filtered samples can't produce an estimate (e.g. a single-PCR-PID file, or a program with exactly one sample)."
  - "CONT-08's unpaired-program topology-fail path lives in src/compare/engine.cpp (not in this plan's own files_modified list either), added after verifying empirically -- not assuming -- that the engine's existing 'unpaired on one side' branch performs a bare `continue` with NO Finding at all (worse than a skip: a program that vanished from a report is a program a reviewer never learns about), directly contradicting doc 02 section 6's literal requirement ('unpaired programs -> topology fail'). The new path is generic over any Scope::Kind::program measurement (not container.ts.*-specific), matching doc 02's own container-agnostic phrasing, and fires unconditionally on Status::fail regardless of the check's own severity policy -- mirroring the existing cross_container demotion and value_kind_mismatch paths' own 'structural fact about the inputs, not a severity decision' shape."
  - "container.ts.pcr_interval/psi_interval's millisecond conversion follows this plan's own action text literally: ms = offset_delta * 1000 * rate_den / rate_num via ONE checked_mul-then-checked_div chain (a single truncating integer division, not a preserved exact fraction) -- sub-millisecond precision is deliberately not retained, matching the checks' own ms-granularity tolerances (100ms/500ms baseline)."
  - "The mean recorded in container.ts.pcr_interval's evidence is an exact sum/count pair ({'num': sum_ms, 'den': sample_count}), never a pre-divided (and therefore potentially non-integer, requiring a double) mean -- the caller/reader divides if they want a decimal, this analyzer never does."

patterns-established:
  - "Two-AnalyzerSpec-per-family (real-data + not-applicable sibling) now proven identically across all three container families this phase registers (mp4, mkv, ts) -- the reference shape for any FUTURE container-family-specific check group."
  - "A local, scope-filtered rate/ratio estimate derived from an already-filtered sample list, falling back to a shared/global estimate only when the local one is unavailable, is the pattern to reach for whenever a per-scope check needs to derive from a signal a shared/global estimate was only ever designed to approximate once for the whole file."

requirements-completed: [CONT-07, CONT-08]

coverage:
  - id: D1
    description: "container.ts.cc_errors (exact 0, fail) sums unflagged continuity errors per PID with per-PID evidence and first-error offset/estimated-time; container.ts.cc_discontinuities (info) tracks flagged discontinuity_indicator resets entirely separately -- a flagged reset never fails a merge."
    requirement: CONT-07
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_analyzer.cpp -- cc_errors 0-on-clean/non-zero-on-gap, evidence shape, cc_discontinuities non-zero while cc_errors stays 0 on ts_discontinuity.ts"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_ts.cpp -- compare ts_single.ts vs ts_single_copy.ts (pass) and vs ts_ccgap.ts (fail, evidence carries per_pid_errors + first_cc_error_offset)"
        status: pass
    human_judgment: false
  - id: D2
    description: "container.ts.pcr_interval/psi_interval carry Measurement::estimated == true, so D-03's 3x tolerance widening applies at comparison; a close-spacing pair (delta over the unwidened 100ms bound but under the widened 300ms one) passes, a far pair (over even the widened bound) still fails; a single-PCR file skips as insufficient_data, never a fabricated interval."
    requirement: CONT-07
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_analyzer.cpp -- pcr_interval/psi_interval estimated==true, mean/pat_max_ms/pmt_max_ms in evidence, insufficient_data on ts_single_pcr.ts and on a file with no usable mux-rate estimate"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_ts.cpp -- ts_pcr_close_a/b.ts compares pass with 'estimated' in the message, ts_pcr_far_a/b.ts compares fail, ts_single_pcr.ts inspects skipped:insufficient_data"
        status: pass
    human_judgment: false
  - id: D3
    description: "container.ts.pmt_version_churn (exact count, warn, never estimated) and container.ts.null_ratio (exact RationalValue num/den, never pre-divided, info, 5% relative tolerance) both register and behave per doc 02 section 5."
    requirement: CONT-07
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_analyzer.cpp -- pmt_version_churn 0-and-matching on identical files, estimated==false; null_ratio num/den shape, sharp difference across two muxrate settings"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_ts.cpp -- null_ratio's --json value is a num/den object; pmt_version_churn 0/pass on identical files"
        status: pass
    human_judgment: false
  - id: D4
    description: "Every container.ts.* check auto-skips as skipped:not_applicable_container on MP4/MKV inputs (ts_scan never runs there) and as skipped:unparsed_mechanism with stop_offset in evidence when ts_scan itself did not complete."
    requirement: CONT-07
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_analyzer.cpp -- all six skipped:not_applicable_container on an MP4 input with Pass::ts_scan absent from the PassExecutionLog; all six skipped:unparsed_mechanism with stop_offset on a resync-runoff construction"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_ts.cpp -- all six skipped:not_applicable_container on MP4 and MKV inputs; all six skipped:unparsed_mechanism on a truncated real pair"
        status: pass
    human_judgment: false
  - id: D5
    description: "A multi-program TS emits one measurement per program for every program-scoped check, keyed at Scope{Kind::program, program_number} (the PSI value, verified against the fixture's own PAT via ffprobe -show_programs, never an array position); two files declaring the same programs in a different on-wire PAT order still pair correctly with no unpaired finding; a program present on only one side produces a Status::fail topology-mismatch finding naming the program number, never a silent drop."
    requirement: CONT-08
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_analyzer.cpp -- single-program file emits 1 measurement per program-scoped check, two-program file emits 2; cc_errors/null_ratio stay global on a multi-program file"
        status: pass
      - kind: integration
        ref: "tests/integration/test_multiprogram.cpp -- scope indices equal 1/1,2 (ffprobe-confirmed literals); reordered-PAT sibling pairs with no unpaired/fail finding; renumbered sibling (programs {1,2} vs {1,3}) produces Status::fail findings naming programs 2 and 3 while program 1 compares normally"
        status: pass
    human_judgment: false
  - id: D6
    description: "The full suite (521 tests: 491 baseline + 30 new) passes with no regressions; all four lint scripts pass; two consecutive inspect --json runs are byte-identical; the doc-registry gate (six required docs/checks/*.md) passes."
    verification:
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure: 521/521 pass"
        status: pass
      - kind: other
        ref: "scripts/lint_check_id_strings.sh, lint_dead_code_after_fail.sh, lint_eng16.sh, lint_fixture_case_collisions.sh: all exit 0"
        status: pass
      - kind: other
        ref: "manual: two mediadiff inspect ts_single.ts --json runs byte-identical; grep -n 'estimated = true' src/analyzers/container/ts.cpp | wc -l == 2; grep for bare compare_ticks(/static_cast<double>/(double) in ts.cpp == 0"
        status: pass
    human_judgment: false

duration: ~30min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 8: container.ts.* — MPEG-TS Check Family, D-03 Widening, CONT-08 Program-Scoped Pairing Summary

**All six `container.ts.*` checks (`src/analyzers/container/ts.cpp`) consuming `ts_scan`, D-03's `estimated`-tolerance widening on `pcr_interval`/`psi_interval`, and CONT-08's PSI-program-number-keyed multi-program pairing (`src/compare/engine.cpp`'s new unpaired-program topology-fail path) — plus a Rule-1 fix to `ts_scan`'s mux-rate estimate, which carried an 8x bits-vs-bytes units bug this plan's own acceptance criteria would otherwise have silently passed against wrong values.**

## Performance

- **Duration:** ~30 min
- **Tasks:** 3/3 completed
- **Files modified:** 12 modified, 10 created (single commit)

## Accomplishments

- `src/analyzers/container/ts.cpp` (CONT-07): `container.ts.cc_errors` (summed unflagged CC errors, per-PID evidence, first-error offset with an estimated-time note), `container.ts.cc_discontinuities` (flagged resets, kept fully independent), `container.ts.pcr_interval`/`container.ts.psi_interval` (D-03 `estimated`-widened, program-scoped, max-spacing via `compare_ticks_checked`), `container.ts.pmt_version_churn` (directly-counted, program-scoped, never estimated), `container.ts.null_ratio` (exact `RationalValue`, global scope) — split across `container_ts_analyzer()`/`container_ts_not_applicable_analyzer()`, the same two-`AnalyzerSpec` pattern proven by `mp4.cpp`/`mkv.cpp`.
- **Rule 1 fix:** `src/probe/ts_scan.cpp`'s `compute_mux_rate_estimate` carried an erroneous `* 8` bytes-to-bits factor, silently making its own `bytes_per_second_num/den` field mean bits/sec despite the field's name and documented contract — caught mid-implementation when a real fixture's independently-computed PCR spacing came back exactly 8x this scanner's own reported value. Fixed at the source (`ts_scan.cpp` + its one locked `test_ts_scan.cpp` assertion), not worked around in the consumer.
- **`ProgramRateContext`:** derives a per-program mux-rate estimate from that program's own PID-filtered PCR samples (sidesteps `ts_scan`'s flat list-adjacency limitation on interleaved multi-program PCR PIDs — confirmed empirically that `ts_multiprogram.ts`'s 50 PCR samples alternate PID on every single entry, which would otherwise leave `ts.mux_rate` permanently unset for any real multi-program file), falling back to the scanner's own file-wide estimate only when the local one is unavailable.
- **CONT-08:** `src/compare/engine.cpp` gains an explicit, generic (not `container.ts.*`-specific) unpaired-`Scope::Kind::program`-measurement topology-fail path — verified empirically that the engine's prior behavior was a bare `continue` (no Finding at all, worse than a skip), not the skip this plan's own action text speculated it might be.
- 20 new fixture recipes in `scripts/gen_corpus.sh`: `-pcr_period`-driven close/far PCR-spacing pairs, a packet-boundary single-PCR truncation, a `-muxrate`-driven null-ratio pair, a real `-mpegts_flags initial_discontinuity` flagged-discontinuity file, and two byte-patched multi-program siblings (PAT entry order reversed; program 2 renumbered to 3 in both PAT and PMT) with correctly recomputed MPEG-2 CRC32.
- 32 new tests (`test_ts_analyzer.cpp`: 16, `test_container_ts.cpp`: 11, `test_multiprogram.cpp`: 5) — full suite now 521/521.

## Task Commits

Tasks 1-3 were implemented and committed together (see Deviations for why):

1. **Tasks 1-3: container.ts.* check family + D-03 widening + CONT-08 program pairing (CONT-07, CONT-08)** - `bc25bb0` (feat)

## Files Created/Modified

- `src/analyzers/container/ts.cpp` (new) — the six checks, `ProgramRateContext`, `bytes_to_ms`, `max_interval_ms`, `container_ts_analyzer()`, `container_ts_not_applicable_analyzer()`
- `src/analyzers/container/analyzers.h` — the two new accessor declarations
- `src/probe/orchestrator.cpp` — both ts `AnalyzerSpec`s registered in `all_analyzers()`
- `src/probe/ts_scan.cpp` — Rule 1 fix: removed the erroneous `* 8` from `compute_mux_rate_estimate`
- `src/compare/engine.cpp` — CONT-08's unpaired-program topology-fail path
- `src/core/checks.def` — six new `[[check]]` entries, matching 03-CHECK-ROSTER.md exactly
- `docs/checks/container.ts.{cc_errors,cc_discontinuities,pcr_interval,psi_interval,pmt_version_churn,null_ratio}.md` (new)
- `scripts/gen_corpus.sh` — 20 new fixture recipes
- `CMakeLists.txt` — `src/analyzers/container/ts.cpp` added
- `tests/unit/{CMakeLists.txt,test_ts_analyzer.cpp}` (new test file), `tests/unit/test_ts_scan.cpp` (Rule 1 fix companion)
- `tests/integration/{CMakeLists.txt,test_container_ts.cpp,test_multiprogram.cpp}` (two new test files)
- `tests/golden/list_checks_effective.txt` — regenerated (six new rows, D-12 discipline)
- `tests/fixtures/GENERATOR_MANIFEST.json` — regenerated (`generated_at` only)

## Decisions Made

See `key-decisions` in the frontmatter for the full list. Highlights:

- **The `*8` units bug in `ts_scan.cpp`'s mux-rate estimate** was found and fixed at the source, not compensated for in this plan's own conversion code — the field's name and `ts_scan.h`'s own doc comment are unambiguous about the intended contract (bytes/sec), and this plan's own action text explicitly assumes that contract holds.
- **`ProgramRateContext`'s per-program rate derivation** resolves the `03-07-SUMMARY.md`-flagged "doc 02 section 6 policy that 03-08 owns" gap entirely within this plan's own file, with no further change to `ts_scan.cpp` needed.
- **CONT-08's topology-fail path is generic over `Scope::Kind::program`**, not hard-coded to `container.ts.*` check ids — matching doc 02 section 6's own container-agnostic phrasing ("program-scoped checks... unpaired programs -> topology fail") and ready for any future container family that also uses program scoping.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `ts_scan.cpp`'s mux-rate estimate computed bits/sec despite being named/documented as bytes/sec**
- **Found during:** Task 2, while validating `container.ts.pcr_interval`'s real output against an independently-computed expected value (a scratch Python re-implementation of the exact same byte-offset/PCR-tick formula).
- **Issue:** `compute_mux_rate_estimate`'s `num = offset_delta * 8 * 27000000` includes a bytes-to-bits `* 8` factor. `MuxRateEstimate::bytes_per_second_num/den`'s own name and `ts_scan.h`'s doc comment ("a later byte-offset-to-milliseconds conversion (plan 03-08) stays exact") both commit unambiguously to bytes/sec. The real fixture (`ts_single.ts`) reported a max PCR interval of 11ms via this analyzer where the correct, independently-verified value is 91ms — exactly the 8x this bug would produce.
- **Fix:** Removed the `* 8` from `ts_scan.cpp`; corrected the one test assertion in `test_ts_scan.cpp` that had locked in the buggy formula (`expected_num = offset_delta * 8 * 27000000` -> `offset_delta * 27000000`), with a comment explaining why.
- **Files modified:** `src/probe/ts_scan.cpp`, `tests/unit/test_ts_scan.cpp`.
- **Verification:** `ts_single.ts`'s `container.ts.pcr_interval` now reports 91ms, matching the independent cross-check exactly; the full `unit.ts_scan` suite (23 tests) still passes; the full project suite (521 tests) passes.
- **Committed in:** `bc25bb0`.

**2. [Rule 2 - Missing critical functionality] `ts_scan`'s file-wide mux-rate estimate is permanently unavailable for any multi-program file whose PCR PIDs interleave in packet order**
- **Found during:** Task 3, while verifying `container.ts.pcr_interval`/`psi_interval` on `ts_multiprogram.ts` — both programs showed `insufficient_data` despite each having 25 real PCR samples.
- **Issue:** `TsScanResult::mux_rate` (03-07's own scanner) is derived by flat list-adjacency over the whole `pcr_samples` vector; `ts_multiprogram.ts`'s two PCR PIDs alternate on every single sample (confirmed via a small debug harness linked against `libmediadiff_core.a`), so no two list-adjacent entries ever share a PID and `mux_rate` never gets set at all. Left unaddressed, this plan's own `container.ts.pcr_interval`/`psi_interval` checks would be permanently non-functional on essentially every realistic multi-program TS — 03-07-SUMMARY.md itself flags this exact limitation as "doc 02 section 6 policy that 03-08 owns."
- **Fix:** Added `ProgramRateContext` (`ts.cpp`, file-local) — derives a mux-rate estimate directly from each program's own PID-FILTERED PCR sample list (where adjacency is trivially same-PID), falling back to `ts.mux_rate` only when the local derivation is unavailable.
- **Files modified:** `src/analyzers/container/ts.cpp` only (no change to `ts_scan.cpp` needed).
- **Verification:** `ts_multiprogram.ts`'s `container.ts.pcr_interval` now reports real values (89ms/88ms for programs 1/2) instead of `insufficient_data`; `tests/integration/test_multiprogram.cpp`'s reordered-pairing test now exercises a REAL value comparison (`status != "fail"`, `status != "skipped"`) rather than a skip-vs-skip pairing that would have proven pairing correctness only weakly.
- **Committed in:** `bc25bb0`.

**3. [Rule 2 - Missing critical functionality] `compare_fingerprints`' unpaired-measurement path silently DROPS a program-scoped measurement present on only one side**
- **Found during:** Task 3, verifying (not assuming, per this plan's own action text's explicit instruction) what the engine's existing unpaired path actually does.
- **Issue:** `compare_fingerprints`' existing "baseline or candidate missing" branch is a bare `continue` — no Finding is emitted at all. This is worse than a skip (a program that vanished from a report is a program a reviewer never learns about) and directly contradicts doc 02 section 6's literal requirement: "unpaired programs -> topology fail."
- **Fix:** Added an explicit branch, checked ahead of the generic unpaired-continue, that fires only for `Scope::Kind::program` and emits `Status::fail` naming the mismatched program number and which side it's present on — unconditional on the check's own severity policy, mirroring the existing cross-container-demotion and value_kind-mismatch paths' shape.
- **Files modified:** `src/compare/engine.cpp`.
- **Verification:** `tests/integration/test_multiprogram.cpp`'s renumbered-sibling test confirms programs 2 and 3 each produce a `Status::fail` finding naming the program number, while program 1 (present on both sides) compares normally.
- **Committed in:** `bc25bb0`.

### Claude's Discretion (not a deviation, documented per this plan's own `<output>` instruction)

**Tasks 1-3 implemented and committed together.** `ts.cpp`'s helper functions (`push_skip`, `emit_incomplete_walk_skips`, `bytes_to_ms`, `max_interval_ms`, `ProgramRateContext`) are shared across all six checks with no natural split boundary — Task 1's three directly-measured checks and Task 2's three estimate-derived/program-scoped checks live in the same file, built and verified together from the start rather than incrementally per task. Mirrors 03-06/03-07's own precedent for this exact shape (both landed all their tasks in one or two commits for the identical reason).

**The plan's own acceptance criterion for CONT-08's scope-object shape does not literally match `inspect --json`'s actual (pre-existing, unmodified) rendering.** The plan's Task 3 acceptance criteria describe `inspect ... --json` showing "`scope` objects have `"kind": "program"`" — but `inspect --json`'s `Scope` rendering has always been `report/model.cpp`'s `scope_to_text` compact STRING form (`"program[1]"`, `"global"`), established well before this plan and unchanged by it; only `compare --json`'s `Finding.scope` is the structured `{kind, index}` object. `tests/integration/test_multiprogram.cpp`'s `inspect`-based assertions were written against the actual string form (verified directly, not assumed); the `compare`-based assertions (reordered/renumbered pairing) use the real structured-object form and match the acceptance criteria's literal JSON shape exactly.

## Issues Encountered

- Building a throwaway debug harness (`g++` linked directly against `build/x64-linux/libmediadiff_core.a`) was the fastest way to confirm the PCR-PID-interleaving hypothesis for Deviation #2 without writing and discarding a full unit test — recorded here as the actual verification method, not just the conclusion.
- No sanitizer run was performed on `ts.cpp`'s new arithmetic (matches 03-05-SUMMARY.md's own recorded precedent: no ASan/UBSan CMake preset exists in this repository). Every checked-arithmetic path IS exercised functionally (the insufficient_data/overflow fallback branches are covered by the single-PCR and no-mux-rate-estimate test cases), just not additionally under sanitizer instrumentation.

## User Setup Required

None — no external service configuration required (the `MEDIADIFF_FFMPEG`/system-`ffmpeg`-≥6.1 precondition was already satisfied in this environment; `scripts/gen_corpus.sh` ran successfully, including the `-mpegts_flags initial_discontinuity` and `-muxrate`/`-pcr_period` recipes).

## Next Phase Readiness

- All 27 approved Phase-3 check ids are now registered: 03-09 (`size.*`) is the last check family this phase needs.
- The `*8` units bug fix and the `ProgramRateContext` per-program rate derivation are both worth flagging to 03-10 (TSDuck golden comparison) — a golden captured against the PRE-fix scanner output would be 8x off and must be regenerated fresh against this plan's corrected behavior, not diffed against a stale golden.
- No blockers identified for 03-09.

## Self-Check: PASSED

- `src/analyzers/container/ts.cpp` — FOUND
- `docs/checks/container.ts.cc_errors.md` — FOUND
- `docs/checks/container.ts.cc_discontinuities.md` — FOUND
- `docs/checks/container.ts.pcr_interval.md` — FOUND
- `docs/checks/container.ts.psi_interval.md` — FOUND
- `docs/checks/container.ts.pmt_version_churn.md` — FOUND
- `docs/checks/container.ts.null_ratio.md` — FOUND
- `tests/unit/test_ts_analyzer.cpp` — FOUND
- `tests/integration/test_container_ts.cpp` — FOUND
- `tests/integration/test_multiprogram.cpp` — FOUND
- `bc25bb0` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
