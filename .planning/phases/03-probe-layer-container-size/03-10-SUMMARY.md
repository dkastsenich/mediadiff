---
phase: 03-probe-layer-container-size
plan: 10
subsystem: testing
tags: [fuzzing, mutation-testing, tsduck, golden-testing, mpeg-ts, asan, ubsan, ci]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: bmff_scan/ebml_scan/ts_scan (03-05/03-06/03-07), the CLI inspect pipeline (03-04), and container.mp4.*/mkv.*/ts.* analyzers whose skip status this plan asserts on
provides:
  - "tests/support/mutate.{h,cpp}: the project's one shared, fixed-seed (mt19937, raw-output-modulo-range) mutation toolkit -- truncate/flip/random-bytes/PRNG-offsets, used by any future fuzz-adjacent test"
  - "PROBE-09 closed: bmff_scan/ebml_scan/ts_scan and the CLI's DemuxSession open path all degrade to exit 65 or an all-skipped report across a deterministic, cross-platform-reproducible mutation set, with permanent canaries"
  - "TRUST-09 closed: ts_scan's own claims are cross-checked against a committed, TSDuck-derived independent reference on every CI run, TSDuck never linked/installed/built"
  - "scripts/capture_tsduck_golden.sh + scripts/extract_tsduck_normalized.py: the developer-workstation-only mechanism to refresh the three ts_scan_*.txt goldens as a deliberate, reviewed act"
  - "scripts/lint_tsduck_goldens.sh wired into the lint (ENG-16 boundary) required CI job"
affects: [phase-06-decode-quality, phase-07-timeline-media-decode, any-future-phase-adding-a-raw-byte-scanner]

# Actuals (#2632)
actuals:
  tokens: 20500
  tasks: 3
  commits: 3

tech-stack:
  added: [tsduck-3.44-4676 (developer-workstation-only, never linked/built/installed on CI)]
  patterns:
    - "Shared mutation toolkit (tests/support/mutate.h) rather than per-test-file duplicated truncate/flip helpers"
    - "Independent-reference golden (TSDuck) captured once, committed, compared read-only in CI -- never the tool itself on a runner"
    - "Hand-written, structurally-identical serializers on both sides of a cross-language golden (Python extractor vs C++ test) instead of relying on two independent JSON pretty-printers to agree byte-for-byte"

key-files:
  created:
    - tests/support/mutate.h
    - tests/support/mutate.cpp
    - tests/unit/test_probe_fuzz_smoke.cpp
    - tests/integration/test_degradation.cpp
    - tests/unit/test_ts_scan_golden.cpp
    - scripts/capture_tsduck_golden.sh
    - scripts/extract_tsduck_normalized.py
    - scripts/lint_tsduck_goldens.sh
    - tests/golden/ts_scan_ts_single.txt
    - tests/golden/ts_scan_ts_multiprogram.txt
    - tests/golden/ts_scan_ts_204.txt
    - tests/golden/TSDUCK_MANIFEST.json
    - .planning/phases/03-probe-layer-container-size/deferred-items.md
  modified:
    - tests/integration/test_container_mp4.cpp
    - tests/integration/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/golden/README.md
    - .github/workflows/ci.yml
    - .planning/WINDOWS.md

key-decisions:
  - "Mutation offsets derive from raw std::mt19937 output modulo range, never std::uniform_int_distribution (T-3-52) -- the latter's mapping is unspecified across standard libraries and would make a red CI leg unreproducible cross-platform"
  - "Test 3 (PRNG-seeded byte flips) asserts 'never crashes, stays in bounds', not 'always degrades' -- empirically confirmed against the real binary that bmff_scan/ebml_scan/ts_scan correctly do NOT flag most payload-region single-byte flips as unparseable (they don't validate payload content), so asserting forced degradation there would assert something false about correctly-behaving scanners"
  - "TS truncation at 10/50/90% needs truncate_to_fraction_off_stride, not the plain fraction helper -- ts_single.ts is exactly 1270 packets (divisible by 10), so a naive byte-fraction truncation lands exactly on a 188-byte packet boundary at those three points and produces a validly-short prefix, not a corrupt one"
  - "bmff_scan/ebml_scan legitimately report complete==true on a genuinely empty (0-byte) input (zero boxes/elements is not malformed); only ts_scan reports complete==false there (no stride is detectable from nothing). The 0-byte truncation/empty-file/canary cases for MP4/MKV assert 'no fabricated content' instead of forced incompleteness"
  - "A structurally-significant byte flip at offset 0 can legitimately produce EITHER skipped:unparsed_mechanism (the raw scanner's own walk failed) OR skipped:not_applicable_container (libav's own prober reclassified the corrupted bytes as a different container family entirely) -- both are safe 'never a silent pass' outcomes; test_degradation.cpp's assert_degrades_cleanly accepts either for the offset-0 flip cases specifically, while still pinning unparsed_mechanism for pure truncation"
  - "Canonical TSDuck-cross-check JSON is a hand-written, byte-identical serializer on BOTH the Python extractor and the C++ golden test, rather than either side calling a generic JSON pretty-printer -- two independent pretty-printers are not guaranteed to agree on whitespace/comma placement, and check_golden is a raw byte diff"
  - "cc_errors (canonical JSON field) maps to TSDuck's own 'discontinuities' per-PID field (no exact TSDuck equivalent exists); pcr_present maps to TSDuck's 'pcr=<count>' collapsed to a bool; pat_present to any tid=0 table; version_number to the PMT table's own 'lastversion' -- all recorded in extract_tsduck_normalized.py's own module docstring for a future TSDuck-version diff to reason against"
  - "Declined a libFuzzer/AFL harness or a permanent sanitizer CI job as disproportionate to three bounded, read-only, non-payload-loading scanners -- ran one, throwaway ASan/UBSan build instead (see Issues Encountered)"

patterns-established:
  - "tests/support/mutate.h is the one mutation toolkit any future fuzz-adjacent test in this project should extend, not copy"
  - "A cross-tool independent-reference golden (TSDuck) follows the same D-12 UPDATE_GOLDENS mechanism as every other golden, but its refresh path is a DIFFERENT script (capture_tsduck_golden.sh) -- documented prominently in tests/golden/README.md so a blanket 'UPDATE_GOLDENS=1 ctest -R golden' does not silently destroy the independent reference"

requirements-completed: [PROBE-09, TRUST-09]

coverage:
  - id: D1
    description: "Every scanner (bmff_scan/ebml_scan/ts_scan) and the CLI's DemuxSession open path degrade to exit 65 or an all-skipped report across truncation, a structurally-significant byte flip, fixed-seed PRNG byte flips, pure random bytes, and an empty file -- never a crash, never a silent pass -- with a permanently-red canary per family"
    requirement: "PROBE-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_probe_fuzz_smoke.cpp (12 test cases)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_degradation.cpp (9 test cases) + tests/integration/test_container_mp4.cpp (its own MP4 half, unchanged in behavior)"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_scan's own claims (per-PID packet counts, cc_errors, PAT presence, per-program pmt_pid/version_number, PCR presence) cross-checked against a committed TSDuck-derived independent reference on every CI run, TSDuck never linked/installed/built"
    requirement: "TRUST-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_scan_golden.cpp (3 test cases: ts_single, ts_multiprogram, ts_204, all matching real TSDuck 3.44-4676 output)"
        status: pass
      - kind: other
        ref: "bash scripts/lint_tsduck_goldens.sh (self-testing lint, wired into the lint (ENG-16 boundary) required CI job)"
        status: pass
    human_judgment: false

duration: 80min
completed: 2026-09-03
status: complete
---

# Phase 3 Plan 10: Cross-Scanner Mutation Smoke and the TSDuck Golden Cross-Check Summary

**Deterministic fixed-seed mutation smoke closing PROBE-09 across bmff_scan/ebml_scan/ts_scan and the CLI's libav open path, plus a committed TSDuck-3.44-4676-derived independent golden closing TRUST-09 with TSDuck never linked or installed on CI.**

## Performance

- **Duration:** ~80 min (includes empirical validation of every mutation-outcome assumption against the real binary before writing an assertion — see Deviations below)
- **Completed:** 2026-09-03
- **Tasks:** 3 (Task 2 was a checkpoint pre-satisfied by the orchestrator: TSDuck 3.44-4676 confirmed installed, `tsanalyze --version` confirmed as the working version flag, `tsanalyze --normalized` output format confirmed against real captured dumps)
- **Files modified:** 20 (13 created, 7 modified)

## Accomplishments

- Extracted plan 03-05's MP4-only mutation helpers into `tests/support/mutate.{h,cpp}` — one shared, fixed-seed (`mt19937`, raw-output-modulo-range, never `uniform_int_distribution`) mutation toolkit for truncation, byte flips, and pure random bytes.
- `tests/unit/test_probe_fuzz_smoke.cpp` drives `bmff_scan`/`ebml_scan`/`ts_scan` directly over truncated, structurally-corrupted, and fixed-seed PRNG-mutated buffers; `tests/integration/test_degradation.cpp` generalizes the same coverage to the real CLI for MKV and TS (MP4 already covered by `test_container_mp4.cpp`), proving the `DemuxSession` libav open path degrades cleanly too.
- Closed TRUST-09: `scripts/capture_tsduck_golden.sh` + `scripts/extract_tsduck_normalized.py` capture a small canonical JSON from a real `tsanalyze --normalized --deterministic` dump; `tests/unit/test_ts_scan_golden.cpp` builds the byte-identical canonical shape from `ts_scan`'s own result and diffs it via `check_golden` — all three fixtures (`ts_single`, `ts_multiprogram`, `ts_204`) verified to match real TSDuck 3.44-4676 output exactly.
- `scripts/lint_tsduck_goldens.sh` (self-testing, zero-file-guarded) wired into the existing `lint (ENG-16 boundary)` required CI job so a missing golden or manifest fails the build, never silently passes.
- Ran the plan's required one-off ASan/UBSan build of the full suite: the mutation smoke and every probe-layer test are clean; a genuine, unrelated Phase-2 test-only bug (`test_markdown_budget.cpp`) was found and logged, not fixed (out of this plan's scope).

## Task Commits

Each task was committed atomically:

1. **Task 1: Deterministic mutation smoke across every scanner and the libav open path (PROBE-09)** — `812696a` (test)
2. **Task 2: Install TSDuck locally and confirm its version flag and normalized output** — checkpoint, pre-satisfied by the orchestrator; no commit (see `<checkpoint_already_answered>` in this plan's execution context — TSDuck 3.44-4676, `tsanalyze --version`, three real captured `--normalized` dumps were supplied directly)
3. **Task 3: TSDuck golden capture jig, extraction adapter, and the CI comparison (TRUST-09, D-04)** — `35b2f6e` (test)

**Sanitizer-note follow-up (not a plan task, but required by this plan's own context):** `22a876c` (docs) — records the ASan finding from the one-off sanitizer build in `.planning/WINDOWS.md` and the phase's new `deferred-items.md`.

**Plan metadata:** (this commit)

## Files Created/Modified

- `tests/support/mutate.h` / `tests/support/mutate.cpp` — the shared mutation toolkit: `truncate_to`, `truncate_to_fraction`, `truncate_to_fraction_off_stride` (new, TS-stride-safe), `flip_byte_at`, `random_bytes`, `prng_offsets`, `write_mutated`, the named `kMutationSeed` literal.
- `tests/unit/test_probe_fuzz_smoke.cpp` — unit-level fuzz smoke for all three scanners: truncation matrix (0/1/10/50/90/99%), the structurally-significant offset-0 flip, fixed-seed PRNG flips, pure random bytes, empty-file handling, and per-scanner canaries.
- `tests/integration/test_degradation.cpp` — CLI-level degradation smoke generalized to MKV and TS (mirrors `test_container_mp4.cpp`'s own MP4 shape, which was refactored to call the shared helpers instead of its own local copies).
- `tests/integration/test_container_mp4.cpp` — now calls `tests/support/mutate.h` instead of its own local truncate/random-bytes helpers.
- `scripts/extract_tsduck_normalized.py` — reduces a `tsanalyze --normalized` dump to the canonical JSON `ts_scan`'s claims are checked against.
- `scripts/capture_tsduck_golden.sh` — developer-workstation-only jig; writes the three goldens plus `TSDUCK_MANIFEST.json`.
- `scripts/lint_tsduck_goldens.sh` — self-testing CI lint (golden presence + manifest well-formedness).
- `tests/unit/test_ts_scan_golden.cpp` — builds the same canonical shape from `ts_scan`'s own result, compares via `check_golden`.
- `tests/golden/ts_scan_ts_single.txt`, `ts_scan_ts_multiprogram.txt`, `ts_scan_ts_204.txt`, `TSDUCK_MANIFEST.json` — the committed independent reference.
- `tests/golden/README.md` — new section warning that these three goldens must be refreshed via `capture_tsduck_golden.sh`, never via a blanket `UPDATE_GOLDENS=1 ctest -R golden`.
- `.github/workflows/ci.yml` — one new step in the `lint (ENG-16 boundary)` job (name unchanged).
- `.planning/WINDOWS.md`, `.planning/phases/03-probe-layer-container-size/deferred-items.md` — the sanitizer finding, logged not fixed.

## Decisions Made

See `key-decisions` in the frontmatter — the seven decisions there (mutation-offset reproducibility, the PRNG-flip assertion narrowing, the TS-stride truncation fix, the empty-file special case, the dual-skip-reason acceptance, the hand-written cross-language serializer, and the `cc_errors`/`pcr_present`/`pat_present`/`version_number` field-mapping choices) are the load-bearing record of what this plan actually verified against the real system, not assumed from the plan text alone.

## Deviations from Plan

### Auto-fixed Issues (test-design corrections — Rule 1/3 applied to this plan's own test code, not to production code)

**1. [Rule 1 — test-design bug] Test 3's "always degrades" assumption was empirically false**
- **Found during:** Task 1, before writing any PRNG-flip assertion
- **Issue:** The plan's Test 3 behavior text asserts a fixed-seed PRNG byte flip "produces one of the two acceptable outcomes" (exit 65 or all-skipped). Manually flipping a dozen offsets across the first 256 bytes of all three real fixtures and running the actual `mediadiff` binary showed this is false: `bmff_scan`/`ebml_scan`/`ts_scan` deliberately do not validate payload content, so many single-byte flips (including several inside packet/box HEADER regions, not just payload) produce a clean, real-valued `pass` report — correct behavior for a scanner that must not over-fit to noise.
- **Fix:** Test 3 asserts the invariant that actually holds and that PROBE-09 is actually protecting: no exception, scan opens, `stop_offset` stays in bounds. Test 2 (a single HAND-PICKED structurally-fatal offset per family, confirmed empirically to force a degrade) keeps the strict two-outcome assertion.
- **Files modified:** `tests/unit/test_probe_fuzz_smoke.cpp`, `tests/integration/test_degradation.cpp`
- **Verification:** All 21 new test cases pass, including under a one-off ASan/UBSan build.
- **Committed in:** `812696a`

**2. [Rule 1 — test-design bug] ts_single.ts's packet count coincidentally aligns truncation at 10/50/90%**
- **Found during:** Task 1, testing the CLI directly against truncated copies of `ts_single.ts`
- **Issue:** `ts_single.ts` is exactly 1270 packets (188-byte stride), and 1270 is evenly divisible by 10 — a plain byte-fraction truncation at 10%/50%/90% therefore lands exactly on a packet boundary, producing a validly-short (not corrupt) TS stream that `ts_scan` correctly reports as `complete == true` with real values. The truncation-degradation test at those three points would have silently tested nothing.
- **Fix:** Added `truncate_to_fraction_off_stride` to `tests/support/mutate.h`, which shortens the computed length by one byte whenever it would otherwise land exactly on a multiple of the packet stride.
- **Files modified:** `tests/support/mutate.h`, `tests/support/mutate.cpp`, `tests/unit/test_probe_fuzz_smoke.cpp`, `tests/integration/test_degradation.cpp`
- **Verification:** `unit.probe_fuzz_smoke - ts_scan degrades cleanly at every truncation point` and its integration counterpart both pass.
- **Committed in:** `812696a`

**3. [Rule 1 — test-design bug] bmff_scan/ebml_scan legitimately report complete==true on a 0-byte input**
- **Found during:** Task 1, first test run (`REQUIRE_FALSE(result->complete)` failed for both scanners on an empty file)
- **Issue:** Zero bytes means zero top-level boxes/elements — not itself malformed. `bmff_scan`/`ebml_scan` correctly report `complete == true` with an empty result. Only `ts_scan` (which needs bytes to detect a stride at all) reports `complete == false` on empty input.
- **Fix:** Added `assert_no_fabrication_from_empty_bmff`/`_ebml` helpers asserting the real invariant (no exception, `stop_offset == 0`, and if `complete` then every field stays empty — never fabricated content), used for the 0-byte truncation point, the empty-file case, and the canary, for MP4/MKV specifically. `ts_scan`'s canary keeps the strict `complete == false` assertion.
- **Files modified:** `tests/unit/test_probe_fuzz_smoke.cpp`
- **Verification:** All truncation/empty-file/canary test cases pass.
- **Committed in:** `812696a`

**4. [Rule 1 — test-design bug] A structural offset-0 flip can produce either of two different skip_reasons**
- **Found during:** Task 1, testing the CLI against an offset-0-flipped MKV/TS file
- **Issue:** Flipping the EBML header ID's leading byte (or the first TS packet's sync byte) can EITHER break the raw scanner's own walk (`skipped:unparsed_mechanism`) OR break libav's own container-family detection entirely, so the file is reclassified as a different format and the family-specific checks report `skipped:not_applicable_container` instead. Both are safe, "never a silent pass" outcomes; hard-pinning one specific `skip_reason` for this case would be asserting an implementation detail of libav's prober, not the actual PROBE-09 guarantee.
- **Fix:** `test_degradation.cpp`'s `assert_degrades_cleanly` takes an `allow_family_reclass` flag: `true` for the structural-offset-flip cases (accepts either reason), `false` for pure truncation (which cannot break family detection, since only content bytes are cut, never the identifying header) — pinned to `unparsed_mechanism` there.
- **Files modified:** `tests/integration/test_degradation.cpp`
- **Verification:** All flip-test cases pass on real fixtures.
- **Committed in:** `812696a`

---

**Total deviations:** 4 auto-fixed test-design corrections, all Rule 1 (fixing a false assumption baked into the plan's own literal test wording, discovered by running the real system before writing the assertion) applied to this plan's own new test code — zero changes to production code, zero scope creep.
**Impact on plan:** All four corrections make the mutation smoke suite actually test what PROBE-09 requires rather than asserting something empirically false about correctly-behaving scanners; every correction is documented inline in the test files themselves for the next reader.

## Issues Encountered

**Sanitizer verification (per this plan's own context, not a plan task):** built the full suite once with `-fsanitize=address,undefined` in a throwaway `build/x64-linux-asan` directory (never committed, removed after use) and ran the mutation smoke plus the full `ctest` suite under it.

- The mutation smoke suite (all 26 new/modified test cases across `test_probe_fuzz_smoke.cpp`, `test_degradation.cpp`, `test_container_mp4.cpp`) is clean — zero ASan or UBSan findings across `bmff_scan`/`ebml_scan`/`ts_scan` for every truncation, byte-flip, and pure-random-bytes case.
- The full suite (564 tests) surfaced ONE pre-existing, unrelated finding: a stack-use-after-scope in `tests/unit/test_markdown_budget.cpp` (Phase 2, `02-08-PLAN.md`) — its `make_finding()` test helper binds `Finding::id` (a `std::string_view`, documented as safe only because production code always sources it from a static-storage-duration `CheckDef::id`) to a dynamically-constructed temporary `std::string`. Test-only; confirmed zero production risk (`src/compare/engine.cpp` is the only production writer of `Finding::id`, and it always assigns from `CheckDef::id`). Out of this plan's file scope — logged to `.planning/phases/03-probe-layer-container-size/deferred-items.md` and `.planning/WINDOWS.md` (entry 6), not fixed.
- No permanent sanitizer CMake preset was added — the repo has none today (matches `03-03-SUMMARY.md`'s and the `.planning/WINDOWS.md` entry-3 precedent), and adding one is a real but separate build-system change; noted as a follow-up rather than expanding this plan's scope.
- As explicitly instructed, no libFuzzer/AFL harness or permanent sanitizer CI job was added — a new toolchain and CI-time investment disproportionate to three bounded, read-only, non-payload-loading scanners. The deterministic mutation set (this plan's own deliverable) is the proportionate gate.

## Known Stubs

None.

## User Setup Required

None beyond this plan's own Task 2 checkpoint, already satisfied before execution began (TSDuck 3.44-4676 installed on the orchestrator's machine, `tsanalyze --version` confirmed working, real `--normalized` output captured and supplied).

## Next Phase Readiness

- PROBE-09 and TRUST-09 are both closed with real, verified evidence — not deferred or partially satisfied.
- `tests/support/mutate.h` is now the one mutation toolkit any later phase's own raw-parser tests should extend.
- The TSDuck golden mechanism (`capture_tsduck_golden.sh` + `TSDUCK_MANIFEST.json` + `lint_tsduck_goldens.sh`) is a reusable pattern if a future phase wants a second independent-reference cross-check against another external tool.
- One unrelated, out-of-scope ASan finding is logged (`.planning/WINDOWS.md` entry 6) and should be picked up by whichever plan next touches `tests/unit/test_markdown_budget.cpp` or does a Phase 2 hardening pass — it blocks nothing in this phase.
- One plan remains in Phase 3 (`03-11-PLAN.md`) before phase close.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-03*

## Self-Check: PASSED

All files created and all commit hashes referenced above were verified present on disk / in git history.
