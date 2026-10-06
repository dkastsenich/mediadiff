---
phase: 07-content-quality
plan: 10
subsystem: content
tags: [psnr, ssim, quality, native-resolution, fixed-point, lockstep, opt-in, snapshots, trust-04, doc-03, content-08, content-10]
status: complete

requires:
  - phase: 07-content-quality
    plan: 09
    provides: "kTolPreconditionKeys (scaler_path, decode_path_signature) in compare_tol; dir --content lockstep"
  - phase: 07-content-quality
    plan: 08
    provides: "PairScorer, FrameTap/TappedFrame with the borrowed AVFrame, fingerprint_pair, the one-sided placeholder-replacement step"
  - phase: 07-content-quality
    plan: 05
    provides: "integer 8x8-window SSIM (ssim_window_q24, ssim_plane_q24, q24_to_micro)"
provides:
  - "quality.psnr and quality.ssim: opt-in (--psnr / --ssim), native-resolution, integer and deterministic, baseline-referenced (D-01), mean-gated with the minimum in evidence (D-03)"
  - "src/util/quality_math.h: exact plane SSE, integer fixed-point log10, capped milli-dB PSNR; ssim_int.h gains the up-to-16-bit variant"
  - "skipped:requires_media on every snapshot side for both checks; snapshots hold the ids only as that skip (CONTENT-10 as amended by D-01)"
  - "CoveragePair::extra_args so DOC-03 covers opt-in checks (registry now 98 ids, all covered)"
affects: [07-11, 07-14, 07-15]

actuals:
  tokens: 44700
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A fixed-point log with a Q62 mantissa and a portable 32-bit-limb 128-bit multiply: no libm, no __int128, bytes identical across machines"
    - "Native planes read through av_read_image_line2 into 16-bit scratch (one reader for every endianness, packing and depth); the 8-bit planar case uses the frame's own memory"
    - "A per-metric latch inside the scorer (geometry / format) that stops the quality checks only, leaving the perceptual score running"
    - "Opt-in checks in DOC-03: CoveragePair::extra_args appended to the gate's compare arguments"

key-files:
  created:
    - src/util/quality_math.h
    - src/probe/quality_request.h
    - src/analyzers/content/quality.cpp
    - docs/checks/quality.psnr.md
    - docs/checks/quality.ssim.md
    - tests/unit/test_quality_math.cpp
    - tests/integration/test_quality.cpp
    - tests/integration/test_quality_snapshot.cpp
  modified:
    - src/util/ssim_int.h
    - src/probe/pair_scorer.h
    - src/probe/pair_scorer.cpp
    - src/probe/lockstep.h
    - src/probe/lockstep.cpp
    - src/probe/orchestrator.cpp
    - src/analyzers/content/analyzers.h
    - src/analyzers/content/video_perceptual.cpp
    - src/core/checks.def
    - src/report/model.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/commands/compare.h
    - src/cli/commands/compare.cpp
    - src/cli/commands/dir.cpp
    - src/cli/main.cpp
    - CMakeLists.txt
    - tests/golden/list_checks_effective.txt
    - tests/unit/test_ssim_int.cpp
    - tests/unit/test_report_model.cpp
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/coverage_pairs.h
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_lockstep.cpp
    - tests/integration/test_video_single_sweep.cpp
    - claude_docs/06-content-and-size-analysis.md
    - claude_docs/00-design-and-requirements.md
    - .planning/REQUIREMENTS.md
    - .planning/ROADMAP.md

key-decisions:
  - "PSNR gates on the sample-count-weighted combined Y+U+V MSE per frame, capped at (6*bpc)+12 dB; the baseline self-score is the cap; the candidate value is the floor mean of the per-frame milli-dB (D-03), the minimum frame and per-plane means ride in evidence"
  - "The PSNR logarithm is an integer fixed-point log (Q62 mantissa, 32 squarings, a 128-bit product from 32-bit limbs) rather than libm (A21); accuracy is asserted against a floating-point reference that exists only in the test"
  - "A 8-bit side against a 10-bit side is promoted by an exact left shift and scores; a bit depth, plane set or format that changes mid-stream latches geometry_mismatch rather than blending caps"
  - "--psnr / --ssim without content decode (--no-content, or dir without --content) is a usage error naming the flag, mirroring --sample"
  - "Both quality checks carry scaler_path (the literal 'native (no scaler)') and decode_path_signature on both sides, so 07-09's table guards them"

patterns-established:
  - "Quality placeholders and live results share the lockstep's find / erase / set_skip helpers, now generic over a check id; the skips every two-file check shares (requires_decode, partial_scan) live in one apply_common_skips"
  - "A single content_has_primary_video definition serves every two-file analyzer"

requirements-completed: [CONTENT-08, CONTENT-10]

coverage:
  - id: D1
    description: "Integer quality math: exact plane SSE, fixed-point log10, PSNR in milli-dB capped at (6*bpc)+12 dB (60000 at 8 bit, 72000 at 10), half-away rounding; the log agrees with a floating-point reference within 1 milli-dB"
    requirement: CONTENT-08
    verification:
      - kind: unit
        ref: "tests/unit/test_quality_math.cpp#quality_math - sse / cap / log accuracy / rounding"
        status: pass
    human_judgment: false
  - id: D2
    description: "High-depth SSIM window and plane: identical 10- and 16-bit windows score exactly 1<<24, known answers match a floating-point reference within 1e-6, depth 8 agrees with the 8-bit function"
    requirement: CONTENT-08
    verification:
      - kind: unit
        ref: "tests/unit/test_ssim_int.cpp#ssim_int - wide identical / known answers / agrees with the 8-bit function / plane score"
        status: pass
    human_judgment: false
  - id: D3
    description: "quality.psnr in a live compare: identical media score exactly 60000/1000 on both sides with identical_frames == pairs_scored; a degraded encode's mean, minimum (frame index and PTS) and per-plane means match an independent floating-point oracle"
    requirement: CONTENT-08
    verification:
      - kind: integration
        ref: "tests/integration/test_quality.cpp#quality - identical media psnr / degraded psnr / hand computed sse on a tiny gray plane"
        status: pass
    human_judgment: false
  - id: D4
    description: "quality.ssim in a live compare: identical media score exactly 1000000/1000000, a degraded encode's mean and minimum match an independent floating-point oracle"
    requirement: CONTENT-08
    verification:
      - kind: integration
        ref: "tests/integration/test_quality.cpp#quality - ssim"
        status: pass
    human_judgment: false
  - id: D5
    description: "Opt-in and geometry: without the flag both checks are skipped:not_requested on both sides; unequal native resolution is skipped:geometry_mismatch while perceptual still scores; 8-bit vs 10-bit promotes exactly; --sample 2 halves pairs and records sampled:2; output is byte-identical across runs"
    requirement: CONTENT-08
    verification:
      - kind: integration
        ref: "tests/integration/test_quality.cpp#quality - not requested / geometry / bit depth promotion / scorer latches its own stops and perceptual runs on / sampling / deterministic"
        status: pass
    human_judgment: false
  - id: D6
    description: "CONTENT-10 as amended by D-01: against a snapshot on either side both checks are skipped:requires_media on both sides, snapshots store the ids only as that skip (no value), an unchanged snapshot compare stays clean, an older snapshot without the ids yields no finding, and the flags exist only on compare and dir"
    requirement: CONTENT-10
    verification:
      - kind: integration
        ref: "tests/integration/test_quality_snapshot.cpp#quality_snapshot - snapshot baseline / snapshot candidate / snapshots store no score / snap06 still clean / older snapshot / the flags exist only where a score can be computed"
        status: pass
    human_judgment: false
  - id: D7
    description: "DOC-03 covers opt-in checks through CoveragePair::extra_args; both ids registered with a triggering and a clean pair, registry count equals verified count"
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp#doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
    human_judgment: false
  - id: D8
    description: "TRUST-04 for quality.*: both checks carry scaler_path and decode_path_signature on both sides, and a differing decode_path_signature or a one-sided key is skipped:path_incomparable for each"
    requirement: TRUST-04
    verification:
      - kind: integration
        ref: "tests/integration/test_quality.cpp#quality - TRUST-04 path preconditions"
        status: pass
    human_judgment: false
  - id: D9
    description: "Documentation and amendments: check docs for both ids, D-01/D-03 amendments in REQUIREMENTS CONTENT-10, ROADMAP criterion 4, doc 06 section 3 and doc 00 section 3.1"
    verification: []
    human_judgment: true
    rationale: "Prose accuracy of the user-facing check docs and the recorded amendments is a reading judgment; the greps for the amendment strings pass but no test asserts the prose"

duration: 24min
completed: 2026-10-01
---

# Phase 7 Plan 10: Native PSNR and SSIM, Opt-in, Snapshot-Honest Summary

**quality.psnr (sample-weighted Y+U+V, capped at (6*bits)+12 dB, integer fixed-point log) and quality.ssim (luma, wide-integer window) score the candidate against the baseline at native resolution inside the lockstep when `--psnr` / `--ssim` is given, gate on the mean with the minimum named in evidence, and report `skipped:requires_media` against any snapshot.**

## Performance

- **Duration:** about 24 min
- **Started:** 2026-10-01T18:27:00Z
- **Completed:** 2026-10-01T18:51:00Z
- **Tasks:** 3
- **Files modified:** 38 (diff against the end of 07-09)
- Native cost, measured on the 100-frame 352x288 pair: `compare` 0.04 s without the flags, 0.05 s with `--psnr`, 0.10 s with `--ssim` or both (T-07-32 accepted; not part of PERF-02).

## Accomplishments

- `src/util/quality_math.h`: exact `uint64` plane SSE, an integer `log10` (Q62 mantissa, a portable 128-bit product from 32-bit limbs, no libm, no `__int128`), and `psnr_milli_db` capped at `(6 * bpc) + 12` dB with half-away rounding. The log agrees with a floating-point reference (test-only) within 1 milli-dB across 8, 10 and 16 bits and ratios from 1 to 10^12.
- `ssim_int.h`: `ssim_window_q24_wide` / `ssim_plane_q24_wide` for up to 16-bit samples, constants `C1 = (L^2*4096 + 5000)/10000`, `C2 = (L^2*36864 + 5000)/10000`, `ExactInt` products and a fixed 25-step Q24 search; identical windows score exactly `1<<24` at 8, 10 and 16 bits.
- `PairScorer` reads the borrowed AVFrames natively (8-bit planar in place, every other depth/packing through `av_read_image_line2` with an exact left-shift promotion), checks geometry before touching a plane (T-07-30), and feeds `PsnrAccumulator` / `NativeSsimAccumulator`. The lockstep writes the baseline self-score and the candidate mean per D-01/D-03 with `reference_identity`, `pairing`, `min` (value, indices, PTS), `per_plane`, `identical_frames`, `bpc`, `sampling_state` and the two path keys.
- `--psnr` / `--ssim` on `compare` and `dir` only; `snapshot` and `inspect` reject them. One-sided probes and every snapshot compare read `skipped:requires_media` (`requires_decode` under `--no-content`); a live compare without a flag reads `skipped:not_requested`.
- DOC-03 now covers opt-in checks (`CoveragePair::extra_args`); both ids are registered, documented and grouped under `content`.
- CONTENT-10's D-01 amendment is recorded in REQUIREMENTS, ROADMAP criterion 4 and doc 06; doc 00 documents the flags.

## Task Commits

1. **Task 1: integer quality math, fixed-point PSNR with the cap, high-depth SSIM** - `dfa2532` (feat)
2. **Task 2: quality.psnr and quality.ssim - flags, native accumulators, one-sided skips, registration, DOC-03 extra_args** - `9a67f47` (feat)
3. **Task 3: CONTENT-10 against snapshots, the D-01 amendments recorded in the open** - `3e13713` (test)

**Plan metadata:** recorded in the docs commit that follows this file.

_Note: tdd="true" tasks were written test-and-implementation together, one commit each; see Deviations._

## Files Created/Modified

See the frontmatter `key-files`. The substance: `src/util/quality_math.h` and `src/util/ssim_int.h` (the arithmetic), `src/probe/pair_scorer.{h,cpp}` (native scoring and accumulators), `src/probe/lockstep.cpp` (assembly), `src/analyzers/content/quality.cpp` (one-sided placeholders), `src/core/checks.def` and the two `docs/checks/` pages (registration), `src/cli/options.*` and the two commands (flags), and three new test files.

## Decisions Made

- The PSNR value is the **floor mean** of per-frame milli-dB, and the baseline's self-score is the cap of the pair's bit depth (60000 at 8 bits, 72000 at 10), recorded as the exact rational `cap/1000`. The unit test `quality - bit depth promotion` proves the computed PSNR of identical frames equals that cap, and the identical-media integration test proves candidate == baseline.
- `scaler_path` on the quality checks is the literal `native (no scaler)` on both sides: nothing is rescaled, but the key must exist on both sides so the generic table covers it.
- A frame narrower or shorter than an 8x8 window drops only SSIM (`insufficient_data`, `frame_too_small`); PSNR is unaffected.
- When the perceptual consumer stops early (no partner, a stop hook, an unmakeable thumbnail) the quality results are a prefix and report `insufficient_data` with `reason: pairing_stopped`, never a prefix mean.
- A quality-specific latch (`geometry_mismatch`, `frame_unavailable`, `unsupported_format`) stops the quality checks only; perceptual keeps scoring.

## TRUST-04

TRUST-04's text names `quality.*`; 07-09 marked it complete on a generic table with only the perceptual check emitting the keys. This plan makes the named checks true.

- **Keys carried.** `quality.psnr` and `quality.ssim` measurements carry `scaler_path` and `decode_path_signature` in the evidence of BOTH the baseline and the candidate side of a live compare.
- **Proof.** `tests/integration/test_quality.cpp`, test `quality - TRUST-04 path preconditions`: it takes the two real fingerprints of a live `--psnr --ssim` compare (`video_hash_base.mp4` vs `video_hash_base.ts`), asserts both keys are present and non-empty on both sides of both checks and that the two sides' `decode_path_signature` are equal, runs the real engine (`compare_fingerprints` with the built-in registry and `sw_encoder` policy) and sees both findings `pass`; then (a) rewrites the candidate's `decode_path_signature` on both measurements and (b) erases the candidate's `scaler_path` on both, and in each case both `quality.psnr` and `quality.ssim` become `skipped` with `skip_reason == path_incomparable`, although the two scores are identical and inside tolerance.
- **Honest limit.** Under D-01 both sides of a real run come from one build, so no real fixture pair can differ in these keys (A19, as in 07-09); the mismatch is injected into real measurements through the real comparator and registry definitions, not a synthetic CheckDef. The generic table itself is `tests/unit/test_tol_path_signature.cpp`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `QualityRequest` lives in its own header**
- **Found during:** Task 2
- **Issue:** the plan puts `QualityRequest` in `lockstep.h`, but `src/cli/options.h` must build one for `resolve_quality_request`, and including the lockstep driver (threads, probe passes) from the CLI option layer is a heavy dependency.
- **Fix:** `src/probe/quality_request.h` defines it; `lockstep.h` includes it, so `QualityRequest` is still reachable from there.
- **Files modified:** `src/probe/quality_request.h`, `src/probe/lockstep.h`, `src/cli/options.h`, `CMakeLists.txt`
- **Committed in:** `9a67f47`

**2. [Rule 2 - Missing critical] `--psnr` / `--ssim` need content decoding**
- **Found during:** Task 2
- **Issue:** `dir --psnr` (decode is off by default there) or `compare --no-content --psnr` would silently report `requires_decode`, a skip the user cannot connect to their flag.
- **Fix:** `resolve_quality_request` returns a usage error (exit 64) naming the flag, the same rule `--sample N` follows; documented in doc 00. `compare --no-content --psnr` is covered by `quality_snapshot - the flags exist only where a score can be computed`.
- **Committed in:** `9a67f47`, `3e13713`

**3. [Rule 1 - Bug] Two pair-probe byte yardsticks and the effective-list golden needed the new ids**
- **Found during:** Task 2 (full suite)
- **Issue:** `lockstep - close releases producers` and `single_sweep - tap does not change one-sided values` strip the ids a pair probe replaces before comparing one-sided bytes; they stripped only `content.video.perceptual`, so the quality placeholders (one-sided `requires_media` vs the pair probe's `not_requested`) tripped them. `tests/golden/list_checks_effective.txt` lacked the two new rows.
- **Fix:** both helpers strip the three pair ids; the golden gains `quality.psnr  severity=fail  tolerance=5/10 dB` and `quality.ssim  severity=fail  tolerance=5/1000 score`; `lockstep - one side without video` also asserts the quality placeholders are erased (Pitfall 10).
- **Files modified:** `tests/integration/test_lockstep.cpp`, `tests/integration/test_video_single_sweep.cpp`, `tests/golden/list_checks_effective.txt`
- **Committed in:** `9a67f47`

**4. [Rule 3 - Blocking] The wide SSIM window is not byte-for-byte the 8-bit function**
- **Found during:** Task 1
- **Issue:** the plan's Test 7 says the wide function "equals" `ssim_window_q24` at depth 8. The 8-bit function trades its last Q24 place for int64-only division; the wide one is the exact floor.
- **Fix:** the test asserts agreement to within one Q24 unit and exact equality for identical windows. `ssim_window_q24_wide` returns `std::optional` (empty only on an unreachable 256-bit overflow) so no overflow can be silent.
- **Committed in:** `dfa2532`

**5. [Rule 3 - Blocking] The log uses `uint64` plus a local 128-bit multiply, not `ExactInt`**
- **Found during:** Task 1
- **Issue:** the threat register names `detail::ExactInt` for the log; `ExactInt` has no shift or division, which iterative squaring needs.
- **Fix:** a Q62 mantissa with a portable 32-bit-limb multiply (`quality_detail::mul_u64`), `uint64` only, no `__int128`; every bound is derived in the header and the accuracy test spans the full range. The wide SSIM products and Q24 search do use `ExactInt`.
- **Committed in:** `dfa2532`

**6. [Process] TDD tasks committed as one commit each**
- Tests and implementation were written together per task, so the RED-fails-first gate was not observed. Non-vacuity was checked another way: the integration oracle is floating point and independent of the scorer, the bit-depth test has a +40 control that must drop below the cap, and the geometry/format latches each have a failing-input case.

---

**Total deviations:** 5 auto-fixed (1 bug, 1 missing critical, 3 blocking) plus 1 process note
**Impact on plan:** none on scope or requirement outcomes; the file and API shapes differ in the ways listed.

## Issues Encountered

None beyond the deviations. No fixture was added or regenerated: `git diff` of `tests/golden/CORPUS_DIGEST.txt` across this plan is empty, and `tests/fixtures/GENERATOR_MANIFEST.json` (dirty before the plan began) was not staged.

## Known Stubs

None. The score-bearing fields are read back, not just serialized: the oracle tests read `mean`, `min` (value, indices, PTS), `per_plane`, `identical_frames`, `pairs_scored` and `sampling_state` from `--json` and compare the numbers with an independent floating-point computation. `reference_identity`, `candidate_stream_index` and the `unpaired_*` counters are serialized the same way perceptual's are and are not asserted by these tests.

## Threat Flags

None new. T-07-30 (plane loops on mismatched geometry) is mitigated by the per-pair layout check before any plane is read and the `geometry_mismatch` / `unsupported_format` latches; T-07-31 by the derived `uint64` / `ExactInt` bounds and the worst-case tests; T-07-33 by `quality_snapshot - snapshots store no score` (the snapshot entry has `value: null` and empty evidence). T-07-32 (native SSIM cost) is accepted; measured above.

## Caveats for the verifier

- `quality.vmaf` is not here: 07-11 adds it to the same analyzer. CONTENT-10's text names `quality.*`; against a snapshot `quality.vmaf` will read `requires_media` once 07-11 registers it. The amendment text says "every `quality.*` check".
- Only `video_hash_base.mp4` against `video_hash_base.ts`/`video_perc_degraded.mp4`/`video_perc_upscaled.mp4` exercise the live path on real media, all 8-bit 4:2:0. The 10-bit, gray and format-rejection paths are proven on synthetic AVFrames through `PairScorer`.
- The five CI-only goldens (`unit.inspect_container`, three `ts_scan_golden`, `integration.size_checks`) are skipped locally. This plan adds only `content`-group rows (via the `quality` -> `content` mapping), and those goldens cover the container, meta and size sections, so no change is expected; they were not run.
- `tests/golden/CORPUS_DIGEST.txt` is unchanged by this plan. Its summary line differs from `1696d28` because 07-09 added two fixtures.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 07-11 can add `quality.vmaf` to `content_quality_analyzer()` and to `QualityRequest::vmaf` (already present, unused), reuse `apply_common_skips` and the generic find/erase helpers, and emit the same two path keys to be covered by 07-09's table.

## Self-Check: PASSED

- Files: `src/util/quality_math.h`, `src/probe/quality_request.h`, `src/analyzers/content/quality.cpp`, `docs/checks/quality.psnr.md`, `docs/checks/quality.ssim.md`, `tests/unit/test_quality_math.cpp`, `tests/integration/test_quality.cpp`, `tests/integration/test_quality_snapshot.cpp` found.
- Commits: `dfa2532`, `9a67f47`, `3e13713` found.
- Full suite 1484/1484 (6 CI-only skips as before), all 11 CI lint scripts exit 0, `grep -nE 'std::log|<cmath>|double|float|__int128' src/util/quality_math.h src/util/ssim_int.h` returns no lines, the plan's `--psnr --json | grep` verify prints 1.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
