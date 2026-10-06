---
phase: 07-content-quality
plan: 11
subsystem: content
tags: [vmaf, libvmaf, quality, opt-in-build, harmonic-mean, self-score, sampling-conflict, trust-04, doc-03, content-09]
status: complete

requires:
  - phase: 07-content-quality
    plan: 10
    provides: "QualityRequest, PairScorer native scoring and its quality latch, assemble_quality, the one-sided quality placeholders, CoveragePair::extra_args, the TRUST-04 path keys"
  - phase: 07-content-quality
    plan: 09
    provides: "kTolPreconditionKeys (scaler_path, decode_path_signature) in compare_tol"
provides:
  - "quality.vmaf: libvmaf's VMAF with model vmaf_v0.6.1 pinned and recorded (model name and vmaf_version() on both sides), harmonic mean gates (D-03), min and arithmetic mean in evidence, baseline self-score COMPUTED by a second libvmaf context"
  - "MEDIADIFF_WITH_VMAF=ON links the pinned vcpkg libvmaf through pkg-config (x64-linux-vmaf preset, own binary dir); the default binary has no libvmaf symbols"
  - "vmaf_built_in() in util/version: --vmaf is a usage error (exit 64) naming MEDIADIFF_WITH_VMAF on every build without the option; quality.vmaf stays registered everywhere (not_requested live, requires_media one-sided)"
  - "--sample N>=2 is skipped:sampling_conflict; unsupported layout geometry_mismatch; non-finite pooled score and pictures of 16 px or less insufficient_data"
  - "DOC-03 build-conditional contract (CoveragePair::requires_build), registry now 99 ids, all verified on both builds"
  - "designated-leg CI step for the VMAF build; CONTENT-09 amended in the open"
affects: [07-14, 07-15]

actuals:
  tokens: 30800
  tasks: 3
  commits: 3

tech-stack:
  added: ["libvmaf 3.2.0 (existing vcpkg 'vmaf' feature; linked only under MEDIADIFF_WITH_VMAF)"]
  patterns:
    - "A libav-free, libvmaf-free header (vmaf_scorer.h) with a pimpl; the implementation is compiled only under the option, and PairScorer holds a shared_ptr so a default build never needs the destructor"
    - "Build-conditional contract in a coverage gate: CoveragePair::requires_build; a build without the option asserts a stated default-build contract and counts the id"
    - "Tests skip (visibly) on the build whose contract they are not, instead of passing vacuously"
    - "An independent oracle: the test drives libvmaf itself over its own copy of the decoded frames"

key-files:
  created:
    - src/probe/vmaf_scorer.h
    - src/probe/vmaf_scorer.cpp
    - docs/checks/quality.vmaf.md
    - tests/integration/test_vmaf.cpp
  modified:
    - CMakeLists.txt
    - CMakePresets.json
    - src/util/version.h
    - src/util/version.cpp
    - src/probe/pair_scorer.h
    - src/probe/pair_scorer.cpp
    - src/probe/lockstep.cpp
    - src/analyzers/content/quality.cpp
    - src/core/checks.def
    - src/cli/options.h
    - src/cli/options.cpp
    - tests/golden/list_checks_effective.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/coverage_pairs.h
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_version_output.cpp
    - tests/integration/test_lockstep.cpp
    - tests/integration/test_video_single_sweep.cpp
    - tests/integration/test_quality_snapshot.cpp
    - .github/workflows/ci.yml
    - claude_docs/06-content-and-size-analysis.md
    - claude_docs/00-design-and-requirements.md
    - .planning/REQUIREMENTS.md
    - .planning/WINDOWS.md

key-decisions:
  - "quality.vmaf's tolerance is the bare score 0.5 (doc 06's vmaf_drop), value in thousandths (97.43 is 97430/1000), the harmonic mean gating"
  - "A library-level VMAF request on a build without libvmaf is an ErrorKind::usage from fingerprint_pair, never a silent not_requested"
  - "n_threads = 0 and n_subsample = 1 for both libvmaf contexts: single-threaded, scheduler-independent, no log output"
  - "Pictures a libvmaf call failed on are unreffed by us (libvmaf consumes them only on success; vmaf_picture_unref zeroes a picture, so releasing 'whatever is left' is never a double free) - read from the libvmaf source, not assumed"
  - "A frame 16 px or smaller in either dimension is never handed to libvmaf (kVmafMinDimension = 17, measured): libvmaf 3.2.0 aborts in its own allocator on such a picture"
  - "A libvmaf depth between its supported 8/10/12/16 is promoted by the same exact left shift the other quality checks use"

patterns-established:
  - "Every opt-in quality check shares one assemble_quality prefix (find / erase / not_requested / common skips / quality latch / pairing_stopped); the VMAF branch only adds sampling_conflict, layout, frame size and the libvmaf error"

requirements-completed: [CONTENT-09]

coverage:
  - id: D1
    description: "Default build: --vmaf exits 64 naming MEDIADIFF_WITH_VMAF (compare and dir); quality.vmaf is registered, documented, skipped:not_requested in a live compare and skipped:requires_media in a snapshot; a library request is refused"
    requirement: CONTENT-09
    verification:
      - kind: integration
        ref: "tests/integration/test_vmaf.cpp#vmaf - default build usage error / registered and not requested / one sided / a library request on a build without libvmaf is refused, not skipped"
        status: pass
    human_judgment: false
  - id: D2
    description: "libvmaf stays out of the default binary: integration.vmaf_absent passes unchanged and nm finds no libvmaf symbol; a VMAF build lists vmaf in --version"
    requirement: CONTENT-09
    verification:
      - kind: integration
        ref: "tests/integration/test_version_output.cpp#vmaf_absent - BUILD-09 optional feature not advertised by default / version_output - vmaf listed"
        status: pass
    human_judgment: false
  - id: D3
    description: "Identical media: model vmaf_v0.6.1 and libvmaf_version recorded, baseline = candidate = the COMPUTED self-score (99974/1000 over the 100-frame clip), equal to libvmaf's own harmonic mean of the baseline against itself, driven directly by the test"
    requirement: CONTENT-09
    verification:
      - kind: integration
        ref: "tests/integration/test_vmaf.cpp#vmaf - identical media"
        status: pass
    human_judgment: false
  - id: D4
    description: "Degraded encode: harmonic mean 58174, min 53970, mean 58225 (thousandths) all equal libvmaf's own pooled scores over the same decoded pictures; status fail; the harmonic mean never exceeds the mean"
    requirement: CONTENT-09
    verification:
      - kind: integration
        ref: "tests/integration/test_vmaf.cpp#vmaf - degraded"
        status: pass
    human_judgment: false
  - id: D5
    description: "--sample 2 is skipped:sampling_conflict on both sides while perceptual records sampled:2; --sample 1 scores; unequal resolution is geometry_mismatch; output is byte-identical across runs; a snapshot side stays requires_media with the flag"
    requirement: CONTENT-09
    verification:
      - kind: integration
        ref: "tests/integration/test_vmaf.cpp#vmaf - sampling conflict / geometry / deterministic / snapshot sides are requires_media even with the flag"
        status: pass
    human_judgment: false
  - id: D6
    description: "TRUST-04 for quality.vmaf: both sides carry scaler_path and decode_path_signature (plus model and libvmaf_version), and a differing decode_path_signature or a one-sided key is skipped:path_incomparable"
    requirement: TRUST-04
    verification:
      - kind: integration
        ref: "tests/integration/test_vmaf.cpp#vmaf - TRUST-04 path preconditions"
        status: pass
    human_judgment: false
  - id: D7
    description: "Layouts and depths through the real scorer (4:2:0, 4:2:2, 4:4:4, gray, 8 vs 10 bit with a non-vacuous control), an unsupported layout, mismatched dimensions, and the 16-px floor under MALLOC_CHECK_=3; quantisation half away from zero and non-finite refusal"
    requirement: CONTENT-09
    verification:
      - kind: integration
        ref: "tests/integration/test_vmaf.cpp#vmaf - layouts and bit depths through the scorer / frames below libvmaf's minimum size are skipped, never fed to it / score quantisation"
        status: pass
    human_judgment: false
  - id: D8
    description: "DOC-03 covers quality.vmaf build-conditionally; running total ninety-nine on both builds"
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp#doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
    human_judgment: false
  - id: D9
    description: "Designated-leg CI step and the CONTENT-09 amendments in REQUIREMENTS, doc 06 and doc 00"
    verification: []
    human_judgment: true
    rationale: "The ci.yml step has never run on a GitHub runner (YAML validated and the step's pass/skip guard simulated locally against both builds); the amendment prose is a reading judgment"

duration: 39min
completed: 2026-10-01
---

# Phase 7 Plan 11: quality.vmaf behind MEDIADIFF_WITH_VMAF Summary

**`quality.vmaf` scores the candidate with libvmaf (model `vmaf_v0.6.1` pinned and recorded, harmonic mean gating, baseline self-score computed by a second context) in a build configured with `MEDIADIFF_WITH_VMAF=ON`, while the default binary contains no libvmaf, `--vmaf` is a named exit-64 usage error there, and the id stays registered everywhere.**

## Performance

- **Duration:** about 39 min wall clock (most of it the first full builds of two trees)
- **Started:** 2026-10-01T18:56:00Z
- **Completed:** 2026-10-01T19:35:00Z
- **Tasks:** 3
- **Files modified:** 28 (diff against the end of 07-10)
- Cost, measured on the 100-frame 352x288 pair: `compare` 0.04 s without the flag, 0.45 s with `--vmaf` (single-threaded libvmaf, two contexts). Not part of PERF-02; it grows with resolution.

## Accomplishments

- **Default-build contract (Task 1).** `vmaf_built_in()` in `src/util/version.{h,cpp}`; `--vmaf` on `compare` and `dir` is a usage error (exit 64) whose message names `MEDIADIFF_WITH_VMAF`, with no `#ifdef` in `options.cpp`. `quality.vmaf` is in `checks.def` (`tol`, `score`, `0.5`), emits its one-sided `requires_media` / `requires_decode`, and a live compare without the flag reports `not_requested`. A library caller asking for VMAF on a build without it gets a `usage` Error, never a silent skip. `docs/checks/quality.vmaf.md` documents the option, platforms, pin, computed self-score, harmonic mean, refused sampling and snapshots.
- **DOC-03, build-conditionally.** `CoveragePair::requires_build`; a build with libvmaf runs the declared `--vmaf` pairs, a build without it asserts the stated contract for the row (the check reports `skipped:not_requested` on both declared pairs, and `--vmaf` exits 64 naming the option) and counts it. Registry 99 = verified 99 on both builds.
- **The libvmaf scorer (Task 2).** `CMakeLists.txt` finds libvmaf through pkg-config (the vcpkg toolchain puts the triplet's `lib/pkgconfig` on the path, so the pinned port is what is found) and adds `src/probe/vmaf_scorer.cpp` only under the option; configuring it on Windows is a `FATAL_ERROR` naming the port's `!windows`. `VmafAccumulator` owns a self and a candidate `VmafContext` plus one `VmafModel` loaded by name, copies rows into libvmaf's own pictures at the plane's row length (never `linesize`), reads them, flushes and pools harmonic mean, min and mean, quantized once to thousandths half away from zero. `PairScorer` feeds it at stride 1 only. Evidence carries `model`, `libvmaf_version`, `harmonic_mean`, `min`, `mean`, `pairs_scored`, `pairing`, `reference_identity` and the two TRUST-04 path keys.
- **Numbers (independent oracle).** On `video_hash_base.mp4` vs `video_hash_base.ts` baseline = candidate = 99974/1000 (computed self-score, not 100; the first frame alone scores 97428, matching research Q8's 97.43 on one identical frame). On the degraded encode: harmonic 58174, min 53970, mean 58225. The test drives libvmaf directly over its own copy of the decoded frames and its own quantisation and asserts all of these equal, so nothing is read back from the code under test.
- **CI and amendments (Task 3).** A designated-leg `VMAF build and tests (CONTENT-09, designated leg only)` step in `ci.yml` (explicit skip line on other legs; fails if the VMAF scoring tests did not actually pass); CONTENT-09 amended in REQUIREMENTS, doc 06 section 3 and doc 00, and marked complete.

## Task Commits

1. **Task 1: the default-build contract** - `f5eb6aa` (feat)
2. **Task 2: the libvmaf scorer behind the option** - `d351d8e` (feat)
3. **Task 3: designated-leg CI step, CONTENT-09 amended** - `92acd7c` (chore)

**Plan metadata:** recorded in the docs commits that follow this file.

_Note: tdd="true" tasks were written test-and-implementation together, one commit each; see Deviations._

## TRUST-04 for quality.vmaf

TRUST-04's text names `quality.*`; 07-09's `kTolPreconditionKeys` table enforces `scaler_path` and `decode_path_signature` on any `tol` check whose measurements carry them, and 07-10 made `quality.psnr` / `quality.ssim` carry both. `quality.vmaf` is covered the same way.

- **Keys carried.** Both the baseline and the candidate measurement of a live `--vmaf` compare carry `scaler_path` (the literal `native (no scaler)`) and a non-empty `decode_path_signature`, equal on both sides, plus `model` and `libvmaf_version`.
- **Proof.** `tests/integration/test_vmaf.cpp`, test `vmaf - TRUST-04 path preconditions` (VMAF build only; it is the VMAF-build twin of 07-10's `quality - TRUST-04 path preconditions`): it takes the two real fingerprints of a live `--vmaf` compare, asserts the keys on both sides, runs the real engine (`compare_fingerprints`, built-in registry, `sw_encoder` policy) and sees `pass`; then (a) rewrites the candidate's `decode_path_signature` and (b) erases its `scaler_path`, and in each case `quality.vmaf` becomes `skipped` with `skip_reason == path_incomparable` although the two scores are identical and inside tolerance.
- **Honest limit.** As in 07-09 and 07-10, both sides of a real run come from one build, so no real fixture pair can differ in these keys; the mismatch is injected into real measurements through the real comparator and registry.

## Decisions Made

- The compared value is the harmonic mean, quantized to thousandths; the tolerance is the bare score `0.5` (doc 06's `vmaf_drop`).
- `n_threads = 0`, `n_subsample = 1`, log level none: single-threaded and scheduler-independent, every pair scored.
- `PairScorer` holds the accumulator in a `shared_ptr` so a build without libvmaf never has to define `~VmafAccumulator`.
- libvmaf's own ownership rule was read from its source (`vmaf_read_pictures` returns before it unrefs on every error path, and `vmaf_picture_unref` zeroes a picture), so every failed step unrefs whatever it still holds and a double free is impossible.
- A depth between libvmaf's 8/10/12/16 (9, 11, 14 ...) is promoted by an exact left shift, as in 07-10.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] libvmaf aborts the process on a picture 16 px or smaller**
- **Found during:** Task 2 (probing a tiny synthetic frame to see what a short clip would do)
- **Issue:** a 2x2, 6x6, 8x8 or 16x16 pair made libvmaf 3.2.0 die with `malloc(): invalid size (unsorted)`; the boundary is exactly 16 vs 17 in either dimension (checked on a grid of sizes under `MALLOC_CHECK_=3`). A tiny clip would have crashed the gate. It is not documented upstream.
- **Fix:** `kVmafMinDimension = 17`; the scorer latches `frame_too_small` and never hands libvmaf such a picture; the check reports `skipped:insufficient_data` (`reason: frame_too_small`, `minimum_dimension`). Documented in `quality.vmaf.md`; tested at 16x64, 64x16, 16x16, 8x8, 2x2 (skipped) and 17x17, 17x40, 40x17 (scored), the latter under `MALLOC_CHECK_=3`.
- **Files modified:** `src/probe/vmaf_scorer.h`, `src/probe/pair_scorer.{h,cpp}`, `src/probe/lockstep.cpp`, `docs/checks/quality.vmaf.md`, `tests/integration/test_vmaf.cpp`
- **Committed in:** `d351d8e`

**2. [Rule 1 - Bug] Two pair-probe byte yardsticks needed the third quality id**
- **Found during:** Task 1 (full suite)
- **Issue:** `lockstep - close releases producers` and `single_sweep - tap does not change one-sided values` strip the ids a pair probe replaces; the new `quality.vmaf` placeholder tripped them (the same shape as 07-10 deviation 3).
- **Fix:** both helpers strip four ids; `lockstep - one side without video` also asserts the vmaf placeholder is erased. The effective-list golden gained the `quality.vmaf  severity=fail  tolerance=5/10 score` row.
- **Committed in:** `f5eb6aa`

**3. [Rule 3 - Blocking] The default build did not link**
- **Found during:** Task 2 (first default build after the scorer)
- **Issue:** `assemble_vmaf_target` named `VmafAccumulator::libvmaf_version`, which a build without libvmaf never defines.
- **Fix:** that one line is `#if defined(MEDIADIFF_WITH_VMAF)`-guarded in `lockstep.cpp` (unreachable without libvmaf: no `VmafSummary` exists there).
- **Committed in:** `d351d8e`

**4. [Rule 2 - Missing critical] A library-level `--vmaf` on a build without libvmaf**
- **Found during:** Task 1
- **Issue:** the CLI refuses it, but `fingerprint_pair` with `QualityRequest::vmaf` set would have silently reported `not_requested`.
- **Fix:** it returns `ErrorKind::usage` naming the option; tested (`vmaf - a library request on a build without libvmaf is refused, not skipped`).
- **Committed in:** `f5eb6aa`

**5. [Process] TDD tasks committed as one commit each**
- Tests and implementation were written together per task, so a RED commit was not observed. Non-vacuity was checked another way: the VMAF numbers are asserted against libvmaf driven directly by the test (independent copy, pooling and quantisation), the layout/depth test has a "genuinely different 10-bit picture scores lower" control, the size floor is tested on both sides of the boundary, and the CI guard was exercised against both builds' real ctest logs (it passes on the VMAF build and trips on the default one).

---

**Total deviations:** 4 auto-fixed (2 bugs, 1 missing critical, 1 blocking) plus 1 process note
**Impact on plan:** none on scope or requirement outcome. One addition (the 16-px floor) beyond the plan's list of skips.

## Issues Encountered

- The first VMAF configure spent several minutes downloading the libvmaf tarball (about 30 KB/s); the port then built from source normally. No vcpkg change, no new port, overlay or registry, no system install.

## Known Stubs

None.

## Threat Flags

None new beyond the plan's register. T-07-34 (picture copy): pictures come from `vmaf_picture_alloc` over the checked shared geometry, rows are copied at the picture's own row length from the plane's own stride, a layout or dimension mismatch skips before any copy. T-07-35 (dependency in the default binary): libvmaf is linked only under the option; `nm -C build/x64-linux/mediadiff | grep -c vmaf_init` prints 0 and no `vmaf_*` data/text symbol exists in it; `integration.vmaf_absent` is green. T-07-36 (leaks / DoS): every context, model and picture is released on every path (`Impl` destructor, `release_pictures`), errors are `ErrorKind::internal` naming the libvmaf call, and the 16-px abort is prevented rather than caught. T-07-SC: no package-manager install was added; `libvmaf` is the existing `vmaf` feature of the pinned manifest.

## Caveats for the verifier

- **CI step never ran on a runner.** The `x64-linux-vmaf` step in `ci.yml` is validated as YAML and its pass/skip guard was exercised locally against both builds' real `ctest` output, but a GitHub run has not happened. Recorded in `.planning/WINDOWS.md` as an `unrun-verify` entry. macOS: the port supports it but it is not exercised (A22).
- **The VMAF build's tests SKIP on the default build and vice versa** (`vmaf - default build usage error`, `vmaf - a library request ...`, `vmaf_absent` skip on a VMAF build; the score tests skip on the default build): each leg proves the contract of the build it is.
- **Scores are not cross-machine identical by construction.** libvmaf's SIMD dispatch can differ across CPUs; that is why the VMAF build's tests run on the designated leg only. Local results are byte-identical across runs.
- **`--tol` / `[tolerance]` overrides** still do not reach `tol` verdicts (the project-wide limitation in `deferred-items.md`); `docs/checks/quality.vmaf.md` states it.
- `tests/golden/CORPUS_DIGEST.txt` is unchanged by this plan (no fixture was added or regenerated); its summary line differs from `1696d28` because 07-09 added two fixtures. `tests/fixtures/GENERATOR_MANIFEST.json` (dirty before the plan began) was not staged.
- The five CI-only goldens were run locally under `MEDIADIFF_DESIGNATED_LEG=1` on both builds and pass.

## User Setup Required

None - no external service configuration required. To use the feature: `cmake --preset x64-linux-vmaf && cmake --build build/x64-linux-vmaf`.

## Next Phase Readiness

- The whole quality family (`psnr`, `ssim`, `vmaf`) is in place on the same lockstep, with one assembly prefix and one TRUST-04 contract.
- Later plans that add a quality-like check can reuse `assemble_quality`'s prefix and `CoveragePair::requires_build`.

## Self-Check: PASSED

- Files: `src/probe/vmaf_scorer.h`, `src/probe/vmaf_scorer.cpp`, `docs/checks/quality.vmaf.md`, `tests/integration/test_vmaf.cpp` found.
- Commits: `f5eb6aa`, `d351d8e`, `92acd7c` found.
- Default build: 1499/1499 (CI-only skips and the VMAF-build tests skipped as designed); VMAF build: 1499/1499; all 11 CI lint scripts exit 0; `ci.yml` parses as YAML; `nm -C build/x64-linux/mediadiff | grep -c vmaf_init` prints 0; `./build/x64-linux-vmaf/mediadiff --version` lists `vmaf`; no existing `CORPUS_DIGEST.txt` line changed by this plan.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
