---
phase: 07-content-quality
plan: 14
subsystem: testing
tags: [video-proof, cross-architecture, d-09, d-10, ci-artifact, ledger, class-1-gate, ffmpeg, determinism]
status: complete

requires:
  - phase: 07-content-quality
    plan: 13
    provides: "the finished, watchdog-guarded video decode path; the empty class-1 table and determinism_class_for_video_decoder from 07-01/07-02"
provides:
  - "scripts/gen_video_proof.sh: the single D-10 producer, eleven proof streams, single-thread encoder parameters, bitexact output flags, LC_ALL=C sorted SHA-256 MANIFEST.sha256, refuses to write under tests/"
  - "tests/integration/test_video_hash_decoder.cpp (prefix 'video_hash_decoder - '): ledger parse, table-driven class-1 gate, identity-first cross-leg proof, required-but-missing failure"
  - "tests/support/video_proof_golden.{h,cpp}: strict ledger parser and formatter (ProofRow, ProofLedger, read_proof_ledger, format_proof_row)"
  - "tests/golden/VIDEO_PROOF_CHAINS.txt in '# MODE: report-only' with zero rows, plus its README section and promotion procedure"
  - "class1_video_decoder_names(): a read-only span over the same (empty) class-1 table determinism_class_for_video_decoder consults"
  - "CI: the video-proof-streams producer job, build needs it, per-leg download and manifest check, MEDIADIFF_VIDEO_PROOF_DIR / MEDIADIFF_REQUIRE_VIDEO_PROOF on the Test step, a Passed ran-guard on both Test-step branches, and a step printing each leg's pasteable rows"
affects: [07-15]

actuals:
  tokens: 14800
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "One producer, many consumers: bytes a proof compares travel as an artifact, never regenerated per leg, because encoders differ per architecture while the decoders under test must see identical input"
    - "A gate that cannot silently stop running: a REQUIRE environment flag turns 'proof directory missing' into a failure, and an independent post-run guard requires the test to appear as Passed in the ctest log"
    - "The gate decision is a pure function (decide_proof_gate) so the required-but-missing branch is unit-testable without touching the environment"
    - "Identity before comparison: a stream's XXH3-128 is asserted against its ledger row, both digests printed, before it is decoded"

key-files:
  created:
    - scripts/gen_video_proof.sh
    - tests/integration/test_video_hash_decoder.cpp
    - tests/support/video_proof_golden.h
    - tests/support/video_proof_golden.cpp
    - tests/golden/VIDEO_PROOF_CHAINS.txt
  modified:
    - src/probe/video_decode.h
    - src/probe/video_decode.cpp
    - tests/integration/CMakeLists.txt
    - tests/golden/README.md
    - .github/workflows/ci.yml
    - docs/checks/content.video.frame_hash.md
    - .planning/WINDOWS.md

key-decisions:
  - "Output flags, not input flags: `-flags +bitexact -fflags +bitexact` follow the lavfi input (they are output options). Before the input they would configure the source, not the encoder or muxer, and Matroska's random SegmentUID would make every run differ."
  - "The class-1 table moved to namespace scope in video_decode.cpp (constexpr std::array<std::string_view, 0>) so one object backs both class1_video_decoder_names() and determinism_class_for_video_decoder(); they can never disagree."
  - "The manifest check falls back to `shasum -a 256 -c` when `sha256sum` is absent (macOS runners); both read the same `<hash>  <name>` format, and the literal `sha256sum -c MANIFEST.sha256` is still the primary command."
  - "The ran-guard lives inside the Test step on BOTH branches (designated: existing tee log; non-designated: the ctest -E run now also tees to a log) so it sees the log ctest just wrote; the proof test is not a designated-leg-only golden, so EXCLUDED_TEST_REGEX and EXPECTED_EXCLUDED_COUNT=5 are untouched."
  - "Rows are printed by a separate step that re-runs only the proof test with -V, because `ctest --output-on-failure` shows a passing test's output nowhere and 07-15 must transcribe the designated leg's rows from the log."
  - "Test 1 does not pin 'report-only, zero rows' for the committed file (it asserts the file parses and each row round-trips); that fact is checked by the plan's grep acceptance criteria, so 07-15 flips the ledger without editing this test."
  - "In gate mode a class-1 row is compared on decoder, frames and chain (the plan names frames and chain; a different decoder name would make the chain comparison meaningless, so it is included)."

patterns-established:
  - "The pasteable-row convention for a cross-leg proof: one `stream=... xxh3=... frames=... chain=... decoder=... flags=...` line per stream on stdout, in every mode on every leg"

requirements-completed: []

coverage:
  - id: D1
    description: "D-10 producer: scripts/gen_video_proof.sh encodes eleven proof streams from one pinned ffmpeg, single-thread, bitexact, with a sorted SHA-256 manifest; two consecutive runs and a run under taskset -c 0-1 give identical manifests"
    requirement: CONTENT-01
    verification:
      - kind: other
        ref: "bash scripts/gen_video_proof.sh build/video-proof (x3), cmp of MANIFEST.sha256, (cd build/video-proof && sha256sum -c MANIFEST.sha256), grep -c '^[0-9a-f]\\{64\\}  proof_' = 11, scripts/lint_bash4_builtins.sh"
        status: pass
    human_judgment: false
  - id: D2
    description: "Ledger parser is strict and round-trips; the committed ledger parses in report-only mode with zero rows"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_hash_decoder.cpp#video_hash_decoder - ledger parses"
        status: pass
    human_judgment: false
  - id: D3
    description: "A class-1 decoder with no ledger row fails (failure branch exercised with a hand-built table); the real table is empty so every decoder stays class 2"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "tests/integration/test_video_hash_decoder.cpp#video_hash_decoder - class-1 decoders have proof rows"
        status: pass
    human_judgment: false
  - id: D4
    description: "Cross-leg proof: identity first, zero decode errors, one printed row per stream in report-only mode; gate mode (exercised by temporarily editing the ledger) fails for a missing row and for a wrong identity, and prints rather than fails a class-2 difference"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "MEDIADIFF_VIDEO_PROOF_DIR=build/video-proof ctest -R integration.video_hash_decoder -V | grep -c 'stream=proof_' = 11; tests/integration/test_video_hash_decoder.cpp#video_hash_decoder - cross-leg proof"
        status: pass
    human_judgment: false
  - id: D5
    description: "The proof cannot silently stop running: with MEDIADIFF_REQUIRE_VIDEO_PROOF=1 and no directory the cross-leg test fails; without the flag it skips with a message naming both variables"
    requirement: CONTENT-01
    verification:
      - kind: integration
        ref: "MEDIADIFF_REQUIRE_VIDEO_PROOF=1 ctest -R 'integration.video_hash_decoder - cross-leg' exits 8; tests/integration/test_video_hash_decoder.cpp#video_hash_decoder - required but missing"
        status: pass
    human_judgment: false
  - id: D6
    description: "CI wiring: producer job, needs, artifact download, manifest check, environment, ran-guard, rows step; YAML parses and required check names are unchanged. The wiring itself is exercised for real only by 07-15's CI run"
    requirement: CONTENT-01
    verification:
      - kind: other
        ref: "python3 yaml.safe_load + needs assertion; bash -n over every run block; the guard function run against a real passing and a real skipping ctest log"
        status: pass
    human_judgment: true
    rationale: "Artifact handoff, the Windows Git Bash and macOS shasum manifest paths and the producer's byte stability on a GitHub runner cannot be exercised locally (flagged assumptions A29, A30); 07-15's first real CI run is the proof. Recorded as an unrun-verify entry in WINDOWS.md."

duration: 23min
completed: 2026-10-01
---

# Phase 7 Plan 14: Cross-architecture video proof mechanism Summary

**A single CI producer encodes eleven proof streams (x264/x265 at 8 and 10 bit, VP9, libaom AV1, MPEG-4, MPEG-2, MJPEG, HuffYUV, FFV1), every build leg decodes the same bytes, identity is asserted first against a human-transcribed ledger, and a table-driven gate makes a class-1 claim impossible without a ledger row; it lands report-only with every decoder still class 2.**

## Performance

- **Duration:** 23 min
- **Started:** 2026-10-01T20:27:32Z
- **Completed:** 2026-10-01T20:50:00Z
- **Tasks:** 3
- **Files modified:** 12 (5 created, 7 modified)

## Accomplishments

- `scripts/gen_video_proof.sh` resolves the pinned ffmpeg through `resolve_pinned_ffmpeg.sh` (never a bare PATH ffmpeg), checks every encoder by name, encodes the eleven streams single-threaded with bitexact output flags, and writes a sorted `MANIFEST.sha256`. It is bash-3.2 clean and refuses an output directory under `tests/`.
- **Producer reproducibility (measured):** three runs on this workstation (two consecutive, one under `taskset -c 0-1`) produced byte-identical `MANIFEST.sha256` files; `sha256sum -c` is OK for all eleven. This is one machine class; runner-CPU-generation variation is unmeasured (A30), and 07-15's transcription from a real CI run is what settles it.
- The proof test enumerates the manifest, asserts each stream's XXH3-128 against its ledger row (both digests printed on mismatch), requires zero decode errors, decodes through `fingerprint_input`, and prints one pasteable row per stream. In `gate` mode a stream without a row fails, a class-1 decoder must match decoder/frames/chain exactly, and a class-2 difference is printed, not failed.
- The class-1 table is now reachable (`class1_video_decoder_names()`), and a test fails for any class-1 decoder with no ledger row (failure branch proven with a hand-built table).
- `tests/golden/VIDEO_PROOF_CHAINS.txt` is in `# MODE: report-only` with zero rows and a provenance header; README documents the format, modes and the 07-15 promotion procedure.
- CI: `video-proof-streams` job (ubuntu-24.04, pinned x86_64 ffmpeg) uploads the artifact; `build` needs it, downloads it on all five legs, verifies the manifest (Git Bash on Windows, `shasum` fallback on macOS), exports the two environment variables, and fails any leg whose cross-leg proof is not `Passed` in the ctest log.

### Local report-only rows (this workstation; never transcribed into the ledger)

Only the designated CI leg's rows are transcribed (07-15). These are local-machine values from the locally generated streams:

```
stream=proof_av1.mkv xxh3=902800225f5f87d04d54d21526986b0e frames=50 chain=9d6c58723088f2e339820e5a24d07e49 decoder=libdav1d flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_ffv1.mkv xxh3=e73e39043e796634884eb4cbea06b70b frames=50 chain=20dfc7d06d0c5658d4324531b2a3edd4 decoder=ffv1 flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_h264_10bit.mkv xxh3=f5fbd5f6cbd55fa18a7ff3216e3965f5 frames=50 chain=eba1fb69a7ca69cf839119304a80d943 decoder=h264 flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_h264_8bit.mkv xxh3=db45c8e181679c2aee990eedd4ff0aa8 frames=50 chain=e2b5ccc4e30c6edd5fb589ff966127c5 decoder=h264 flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_hevc_10bit.mkv xxh3=34c8f3887f00a929f4b92c8276a9717e frames=50 chain=c4fce9cb806c2da622aff9e9d8206122 decoder=hevc flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_hevc_8bit.mkv xxh3=bb04745dc83336fd55821224cd786fa0 frames=50 chain=95ae9c6502abb53938b831bbfb3761ae decoder=hevc flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_huffyuv.mkv xxh3=2671948ace771d0907dca005e8a6ad86 frames=50 chain=a6a9af36cf2aa2c3a44b4a3059a6c022 decoder=huffyuv flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_mjpeg.mkv xxh3=da3a187a3abe441316728dfb800aad5e frames=50 chain=a91585b90e49e110a43eeaaf1e38b9c6 decoder=mjpeg flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_mpeg2.mkv xxh3=eda800163b90a62c772a4d6830b2bcb7 frames=50 chain=9322c61d94a44dacc7d4e6c082731019 decoder=mpeg2video flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_mpeg4.mp4 xxh3=c0ffd549ddcfe98da56045bc812ecfdc frames=50 chain=1229e19a1a806c4e122929a192c27dcb decoder=mpeg4 flags=bitexact+unaligned;idct=simple;threads=1
stream=proof_vp9.mkv xxh3=e143cfdc9ee7e22e691d4490c24e6228 frames=50 chain=2d6ad97b25dd0962b36859b473378267 decoder=vp9 flags=bitexact+unaligned;idct=simple;threads=1
```

## Task Commits

1. **Task 1: gen_video_proof.sh, single-thread proof encodes and a SHA-256 manifest** - `b30e029` (feat)
2. **Task 2: proof test, ledger parser, table-driven class-1 gate** - `c15dcfb` (feat)
3. **Task 3: CI producer job, artifact handoff, ran-guard, docs** - `b5d47ae` (feat)

**Plan metadata:** the docs(07-14) commit that carries this SUMMARY, STATE.md, ROADMAP.md and the WINDOWS.md entry.

## Files Created/Modified

- `scripts/gen_video_proof.sh` - the D-10 producer
- `tests/integration/test_video_hash_decoder.cpp` - four test cases, `video_hash_decoder - ` prefix
- `tests/support/video_proof_golden.{h,cpp}` - strict ledger parser/formatter
- `tests/golden/VIDEO_PROOF_CHAINS.txt` - the ledger (report-only, no rows)
- `tests/golden/README.md` - the ledger section and the promotion procedure
- `src/probe/video_decode.{h,cpp}` - `class1_video_decoder_names()` over the one class-1 table
- `tests/integration/CMakeLists.txt` - registers the test and the support source
- `.github/workflows/ci.yml` - producer job, `needs`, download, manifest check, env, ran-guard, rows step
- `docs/checks/content.video.frame_hash.md` - how a decoder becomes class 1
- `.planning/WINDOWS.md` - one `unrun-verify` entry for the CI wiring (added through the CLI)

## Decisions Made

See `key-decisions` in the frontmatter. The two with consequences beyond this plan: (1) a producer failure skips the entire `build` matrix, so the required contexts `build (x64-linux)`, `build (arm64-osx)`, `build (x64-windows-static-md)` never report and the merge is BLOCKED, not bypassed - intended, recorded in the CI job comment and the check doc, and no `if: always()` was added; (2) `-flags/-fflags +bitexact` are output options.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical] macOS manifest-check fallback**
- **Found during:** Task 3
- **Issue:** `sha256sum` is GNU coreutils and not present on macOS runners; the plan's bare `sha256sum -c MANIFEST.sha256` would fail the `arm64-osx` (required) and `x64-osx` legs before any test.
- **Fix:** the step uses `sha256sum -c` when available, else `shasum -a 256 -c` (same manifest format). The literal acceptance string is still present.
- **Files modified:** `.github/workflows/ci.yml`
- **Committed in:** `b5d47ae`

**2. [Rule 2 - Missing critical] Rows step and non-designated-leg log**
- **Found during:** Task 3
- **Issue:** the plan's ran-guard needs a ctest log on every leg, but only the designated branch teed one; and a passing test's stdout never reaches a CI log under `--output-on-failure`, so 07-15 would have had no rows to transcribe.
- **Fix:** the non-designated branch now tees to `ctest-nondesignated.log` (status captured and re-raised as on the designated branch), and a small step after Test prints each leg's rows.
- **Files modified:** `.github/workflows/ci.yml`
- **Committed in:** `b5d47ae`

**3. [Rule 1 - Bug] Output-vs-input bitexact flags**
- **Found during:** Task 1
- **Issue:** the plan's flag order read as input-side; placed before `-i` the flags would not reach the encoder or the muxer.
- **Fix:** passed after the input as output options; verified by three byte-identical manifests.
- **Files modified:** `scripts/gen_video_proof.sh`
- **Committed in:** `b30e029`

---

**Total deviations:** 3 auto-fixed (2 missing critical, 1 bug)
**Impact on plan:** all three were necessary for the proof to be producible, runnable on the required macOS leg and transcribable. No scope creep.

## Issues Encountered

None blocking. The first `cmake --build` exceeded the tool timeout and finished in the background; no source problem.

## Authentication Gates

None.

## Known Stubs

None. `tests/golden/VIDEO_PROOF_CHAINS.txt` has zero rows by design (the plan requires report-only with no rows; 07-15 transcribes real CI output), and the class-1 table is empty by design.

## Threat Flags

None beyond the plan's register (T-07-45..48 all mitigated as planned; T-07-SC accepted: `actions/download-artifact@v4` is the same first-party publisher and major version as the existing upload action).

## Next Phase Readiness

**CONTENT-01 is left pending.** This plan builds the proof mechanism; the requirement's cross-machine hash claim (D-09) is not yet proven for any decoder. 07-15 must confirm:

1. The first real CI run: the producer job succeeds on a runner, `actions/download-artifact@v4` delivers the streams (A29), the manifest check passes on Linux, macOS (shasum fallback) and Windows (Git Bash), and every leg's cross-leg proof test is `Passed` (the ran-guard enforces this).
2. The producer's bytes are stable across runs on a runner (A30): the identity assertion names a stream if not; transcribe the designated leg's rows only.
3. Per decoder, `frames` and `chain` agree on all legs before it is added to the class-1 table; then flip the ledger to `# MODE: gate`.
4. Local rows above are not ledger values.

Known property to keep in mind: with `needs: video-proof-streams`, a producer failure blocks all required build contexts (they skip, never report). Do not add `if: always()` to `build`.

Verification state: default build and full suite 1529/1529 (1525 plus the 4 new tests; also 1529/1529 with `MEDIADIFF_DESIGNATED_LEG=1` and the proof directory set, where the cross-leg proof runs), VMAF build `build/x64-linux-vmaf` 1529/1529, all 11 CI lint scripts exit 0, `ci.yml` parses with `build.needs` containing `video-proof-streams`, `EXPECTED_EXCLUDED_COUNT=5` and the required job names unchanged. `CORPUS_DIGEST.txt` is untouched by this plan (its only difference from 1696d28 is the derived summary line plus lines added by earlier 07 plans; no pre-existing fixture line removed or changed).

## Self-Check: PASSED

All created files exist (`scripts/gen_video_proof.sh`, `tests/integration/test_video_hash_decoder.cpp`, `tests/support/video_proof_golden.{h,cpp}`, `tests/golden/VIDEO_PROOF_CHAINS.txt`); commits `b30e029`, `c15dcfb`, `b5d47ae` exist; no proof media file is tracked in git.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
