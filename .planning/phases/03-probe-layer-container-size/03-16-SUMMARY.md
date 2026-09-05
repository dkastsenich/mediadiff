---
phase: 03-probe-layer-container-size
plan: 16
subsystem: infra
tags: [ci, github-actions, ffmpeg, fixture-generation, corpus, golden, determinism]

# Dependency graph
requires:
  - phase: 03-14
    provides: Unconditional ffmpeg-install + corpus-generation + corpus-verification step group before Configure on all five CI matrix legs
provides:
  - "A checksum-pinned ffmpeg supply chain (scripts/ffmpeg_pin.json + scripts/install_pinned_ffmpeg.sh) replacing the three rolling-channel (apt/brew/choco) ffmpeg installs on every CI leg"
  - "scripts/corpus_digest.sh: deterministic per-fixture SHA-256 listing emitting a CORPUS_DIGEST_SUMMARY= line per leg, evidence for plan 03-19's cross-platform byte-identity evaluation"
  - "Fixture-derived goldens (inspect_container.txt, size_checks_size_crf20.txt, ts_scan_ts_*.txt, TSDUCK_MANIFEST.json) re-baselined against bytes captured on the real x64-linux CI runner"
  - "Real CI evidence (PR #3, run 33981198277) that x64-linux concludes success with both TRUST-06 test cases Passed"
affects: [phase-03-verification, ci-workflow, plan-03-19-cross-platform-identity]

# Actuals (#2632)
actuals:
  tokens: 10132
  tasks: 3
  commits: 5

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Checksum-pinned binary download: URL + SHA-256 recorded in a tracked JSON manifest, verified before extraction, no fallback path on mismatch (T-3-78 mitigation pattern, reusable for any future pinned-binary need)"
    - "Golden bytes derived from ffmpeg-synthesized fixtures must be captured FROM the actual blocking CI runner via a temporary diagnostic CI step (pushed, used, fully reverted), never assumed portable from a developer workstation -- even when the exact same checksum-verified binary is used, runtime CPU-SIMD-dispatch differences between microarchitectures change the encoder's floating-point output"

key-files:
  created:
    - scripts/ffmpeg_pin.json
    - scripts/install_pinned_ffmpeg.sh
    - scripts/corpus_digest.sh
  modified:
    - .github/workflows/ci.yml
    - tests/golden/inspect_container.txt
    - tests/golden/size_checks_size_crf20.txt
    - tests/golden/ts_scan_ts_204.txt
    - tests/golden/ts_scan_ts_multiprogram.txt
    - tests/golden/ts_scan_ts_single.txt
    - tests/golden/TSDUCK_MANIFEST.json
    - tests/fixtures/GENERATOR_MANIFEST.json
    - .gitignore
    - .planning/WINDOWS.md

key-decisions:
  - "Pin set: martin-riedl.de static release builds (ffmpeg 9.0.1) for linux-x86_64, linux-aarch64, macos-arm64; BtbN/FFmpeg-Builds LGPL static build (tag n9.0.1-11-ge47273f4d9) for windows-x86_64 -- rejected conda-forge (candidate B) after confirming its packages depend on ~40 separate conda-forge shared libraries (libopus, libvpx, libass, etc.), making 'download one file, verify its checksum, run it' impossible; both chosen providers ship genuinely self-contained static binaries needing only OS-provided libc/libm/libstdc++."
  - "Goldens must be captured on the actual x64-linux CI runner, not regenerated locally, even with the identical checksum-verified pinned binary -- discovered mid-task when corpus_digest.sh showed all 80 fixture hashes differing between a local regen and the real CI run using the same binary (almost certainly CPU SIMD-dispatch / floating-point differences between microarchitectures affecting the encoders even under -flags +bitexact). Recovered via a temporary CI diagnostic step (pushed, used, fully reverted before the final commit)."
  - "The temporary diagnostic step's first attempt incorrectly refreshed the three ts_scan_ts_*.txt goldens through the renderer-golden mechanism (UPDATE_GOLDENS=1 -R golden matched ts_scan_golden too) -- caught before committing; fixed with -E ts_scan_golden and a second diagnostic upload of the raw .ts fixture bytes, captured correctly via scripts/capture_tsduck_golden.sh run locally against those exact bytes (the only path D-04/TRUST-09 permit)."

patterns-established:
  - "A checksum-pinned external binary is verified end-to-end before being trusted: download over HTTPS, compute SHA-256 with the first of sha256sum/shasum/openssl available, compare, abort loudly with both digests on any mismatch, resolve the extracted path and assert it stays inside the extraction directory (archive-traversal guard), then functionally probe the binary (-encoders/-muxers) for the exact codec surface the caller needs before ever using it."

requirements-completed: [TRUST-06, TRUST-09, DOC-03]

coverage:
  - id: D1
    description: "Every CI leg obtains its fixture-synthesis ffmpeg from a checksum-verified pinned URL (scripts/ffmpeg_pin.json + scripts/install_pinned_ffmpeg.sh); no rolling-channel install remains; a checksum mismatch aborts with no fallback"
    requirement: "TRUST-06"
    verification:
      - kind: other
        ref: "bash scripts/install_pinned_ffmpeg.sh (local run, exit 0, prints resolved path + version)"
        status: pass
      - kind: other
        ref: "corrupted-sha256 negative test: exits non-zero, prints both digests, prints no export line (observed and recorded in this SUMMARY)"
        status: pass
      - kind: other
        ref: "real CI run 33981198277: 'Install the pinned ffmpeg build (D-GAP-01)' succeeded on all 5 build legs"
        status: pass
    human_judgment: false
  - id: D2
    description: "Fixture-derived goldens re-baselined against the pinned build's bytes as actually produced on the x64-linux CI runner; TSDuck cross-check goldens refreshed only via the correct capture_tsduck_golden.sh path"
    requirement: "TRUST-09"
    verification:
      - kind: other
        ref: "real CI run 33981198277: build (x64-linux) Test step -- 100% tests passed, 0 tests failed out of 620"
        status: pass
      - kind: other
        ref: "bash scripts/lint_tsduck_goldens.sh (local, exit 0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "x64-linux concludes success on a real CI run with both integration.trust06_idempotence cases observed Passed in the run log, plus per-leg corpus digest evidence"
    requirement: "DOC-03"
    verification:
      - kind: other
        ref: "gh run view 33981198277 --json jobs: build (x64-linux) = success"
        status: pass
      - kind: other
        ref: "gh run view 33981198277 --log | grep -c integration.trust06_idempotence = 4 (2 Start + 2 Passed lines)"
        status: pass
      - kind: other
        ref: "gh run view 33981198277 --log | grep CORPUS_DIGEST_SUMMARY= | sort -u | wc -l = 5 (one per build leg)"
        status: pass
    human_judgment: false

# Metrics
duration: ~90min (research + implementation + 4 real CI round-trips)
completed: 2026-09-05
status: complete
---

# Phase 03 Plan 16: Pin CI's Fixture-Synthesis ffmpeg by URL + SHA-256 Summary

**Replaced the three rolling-channel ffmpeg installs with a checksum-pinned supply chain (martin-riedl.de for Linux/macOS, BtbN/FFmpeg-Builds for Windows), re-baselined the five fixture-derived goldens against bytes captured directly from the real x64-linux CI runner (not a local regeneration, which was discovered mid-task to differ from CI even with the identical checksum-verified binary), and proved on a real GitHub Actions run (33981198277) that x64-linux concludes success with both TRUST-06 idempotence cases Passed.**

## Performance

- **Duration:** ~90 min (includes ~4 real CI round-trips, each 2-5 min)
- **Tasks:** 3
- **Files modified:** 13 (3 created, 10 modified)

## Accomplishments

- `scripts/ffmpeg_pin.json` pins a checksum-verified, genuinely self-contained static ffmpeg 9.0.1 build per runner key (`linux-x86_64`, `linux-aarch64`, `macos-arm64` from martin-riedl.de; `windows-x86_64` from BtbN/FFmpeg-Builds) — rejected conda-forge after confirming its packages need ~40 separate shared-library dependencies, incompatible with a "download one file, verify it, run it" installer.
- `scripts/install_pinned_ffmpeg.sh`: downloads, verifies SHA-256 with no fallback path, guards against archive path traversal, functionally probes the binary's `-encoders`/`-muxers` for the exact codec surface `gen_corpus.sh` needs, and exports `MEDIADIFF_FFMPEG` + `GITHUB_PATH`. Fixed a real cross-platform bug discovered on the Windows CI leg: comparing a bash-resolved path (`pwd -P`, POSIX-style under Git Bash) against a python3-resolved path (`os.path.realpath`, native Windows-style after Git Bash's argv auto-conversion) always reported a false archive-escape rejection.
- `scripts/corpus_digest.sh`: deterministic per-fixture SHA-256 listing + a `CORPUS_DIGEST_SUMMARY=` line per leg, now present in every real CI run log as raw evidence for plan 03-19's cross-platform byte-identity question.
- `.github/workflows/ci.yml`: one unconditional `Install the pinned ffmpeg build (D-GAP-01)` step replaces the three OS-scoped rolling installs; one `Report corpus digest` step follows corpus verification.
- Five fixture-derived goldens (`inspect_container.txt`, `size_checks_size_crf20.txt`, three `ts_scan_ts_*.txt`, `TSDUCK_MANIFEST.json`) re-baselined against the pinned build's actual output on the x64-linux CI runner.
- Real CI evidence: `build (x64-linux)` concludes `success`, both `integration.trust06_idempotence` cases observed `Passed`, 620/620 tests pass, and all five build legs emit a `CORPUS_DIGEST_SUMMARY=` line.

## Task Commits

Each task was committed atomically (Task 2/3 required follow-up fix commits after real CI evidence surfaced problems not visible locally):

1. **Task 1: Pin the fixture-synthesis ffmpeg by URL + SHA-256 on every leg, end to end** — `8a13b8c` (feat)
2. **Task 2: Regenerate the fixture-derived goldens once, against the pinned build** — `13ea9db` (test) — later discovered to be byte-wrong for CI (see Deviations) and corrected:
   - `cb324e4` (fix) — Windows path-traversal false-positive + first diagnostic CI step
   - `d888e27` (fix) — excluded `ts_scan_golden` from the diagnostic renderer refresh
   - `bc09705` (test) — re-baselined goldens from bytes captured on the real x64-linux CI runner, ci.yml diagnostic steps fully reverted, WINDOWS.md updated
3. **Task 3: Prove x64-linux green on a real CI run, with TRUST-06 observed in the log** — verified against run `33981198277` (no additional commit; this SUMMARY is the record)

**Plan metadata:** committed separately after this SUMMARY.

## Files Created/Modified

- `scripts/ffmpeg_pin.json` — pin manifest: `version`, `provider`, `builds` (4 runner keys, each with `url`/`sha256`/`archive`/`ffmpeg_path`)
- `scripts/install_pinned_ffmpeg.sh` — checksum-verifying downloader/installer
- `scripts/corpus_digest.sh` — per-fixture SHA-256 digest + summary line
- `.github/workflows/ci.yml` — pinned install step + corpus digest step replacing three rolling installs; stale comments referencing removed steps updated
- `.gitignore` — `/.ffmpeg-pinned/` excluded (extracted pinned binary, never committed)
- `tests/golden/inspect_container.txt`, `tests/golden/size_checks_size_crf20.txt` — re-baselined from the x64-linux runner's own rendered output
- `tests/golden/ts_scan_ts_204.txt`, `tests/golden/ts_scan_ts_multiprogram.txt`, `tests/golden/ts_scan_ts_single.txt`, `tests/golden/TSDUCK_MANIFEST.json` — re-captured via `scripts/capture_tsduck_golden.sh` against the `.ts` fixture bytes x64-linux actually generated
- `tests/fixtures/GENERATOR_MANIFEST.json` — rewritten by `gen_corpus.sh` with the pinned build's version/configuration strings
- `.planning/WINDOWS.md` — entry #10 (ffmpeg-version-drifted goldens) marked fixed; three new findings logged (#12 cross-CPU determinism, #13 AppleClang unused-const-variable, #14 x64-osx cross-build linker mismatch)

## Decisions Made

- **Pin providers:** martin-riedl.de (Linux x86_64/aarch64, macOS arm64) + BtbN/FFmpeg-Builds (Windows x86_64), both genuinely self-contained static builds — conda-forge was rejected after confirming its packages require ~40 separate shared-library dependencies not bundled in the downloaded file.
- **Archive extraction target:** a repo-relative `.ffmpeg-pinned/<runner_key>/` directory (not a `mktemp -d` under the EXIT trap), because `MEDIADIFF_FFMPEG` must keep resolving for every later step in the same CI job.
- **Goldens must be captured on the real CI runner, not locally regenerated** — see Deviations below; this became the dominant finding of the plan.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Windows path-traversal false-positive in install_pinned_ffmpeg.sh**
- **Found during:** Task 3 (first real CI run, 33979976185)
- **Issue:** `install_pinned_ffmpeg.sh` compared a bash-resolved (`cd`+`pwd -P`, POSIX-style under Git Bash) install-directory path against a python3-resolved (`os.path.realpath`, native Windows-style — Git Bash auto-converts argv for non-MSYS binaries) candidate path. The two representations of the identical directory never matched textually, so every Windows install was rejected as "escaping the extraction directory."
- **Fix:** Resolve both sides with the identical bash-native `cd`+`pwd -P` mechanism; never mix it with python3's path resolution for this comparison.
- **Files modified:** `scripts/install_pinned_ffmpeg.sh`
- **Verification:** Re-run against CI (run 33980515543): `Install the pinned ffmpeg build (D-GAP-01)` succeeded on `x64-windows-static-md`.
- **Committed in:** `cb324e4`

**2. [Rule 1 - Bug] Diagnostic step incorrectly refreshed TSDuck cross-check goldens through the wrong path**
- **Found during:** Task 2 correction (reviewing the first diagnostic artifact)
- **Issue:** The temporary CI diagnostic step ran `UPDATE_GOLDENS=1 ctest -R golden`, whose `golden` substring also matched the `ts_scan_golden` test names — refreshing `ts_scan_ts_*.txt` through the renderer-golden mechanism would have baked `ts_scan`'s own current output in as if it were TSDuck's independent reference, exactly what D-04/TRUST-09 forbid.
- **Fix:** Caught before any commit; added `-E ts_scan_golden` to the diagnostic filter and a second diagnostic upload of the raw `.ts` fixture bytes, then ran `scripts/capture_tsduck_golden.sh` locally against those exact bytes — the only correct refresh path.
- **Files modified:** `.github/workflows/ci.yml` (diagnostic-only, fully reverted before final commit)
- **Verification:** `git diff --name-only` after the correction shows no committed golden was produced by the wrong mechanism; `bash scripts/lint_tsduck_goldens.sh` passes.
- **Committed in:** `d888e27` (the exclusion fix), `bc09705` (the corrected goldens)

---

**Total deviations:** 2 auto-fixed (2 bugs, both discovered only via real CI evidence — neither was reproducible locally)
**Impact on plan:** Both fixes were necessary for correctness; no scope creep. The second deviation is a direct, load-bearing instance of this project's own D-04/TRUST-09 discipline catching itself before a bad commit.

## Issues Encountered

**The dominant issue this plan surfaced: the pinned ffmpeg binary alone does not guarantee byte-identical fixtures, even across two runs of the literal same checksum-verified binary.** `scripts/corpus_digest.sh` on the real x64-linux run and on this developer's local workstation (also x86_64 Linux) produced completely different summaries — all 80 fixture hashes differed — despite running the identical, SHA-256-verified `linux-x86_64` pinned binary. The most likely explanation: the workstation's CPU has AVX-512 (`lscpu` confirms `avx512f`, `avx512bw`, etc.); GitHub's runner almost certainly does not. ffmpeg's encoders (native `mpeg4`, `aac`, etc.) use runtime CPU-feature detection to pick a SIMD codepath for floating-point DSP routines, and `-flags +bitexact` does not force a fixed SIMD path — it only removes things like embedded timestamps and uses deterministic *algorithms* per codec, not deterministic *floating-point rounding across instruction sets*. This is a real limitation of ffmpeg's bitexact mode this plan's own `<flagged_assumptions>` A1 anticipated in spirit ("different builds... possibly different SIMD paths") but framed as a cross-*platform* concern; it turned out to also be a same-platform, same-binary, cross-*microarchitecture* concern.

**Resolution:** goldens derived from fixture bytes must be captured directly from the actual blocking-leg CI runner, never assumed portable from a developer workstation. This was done via a temporary CI diagnostic step (added, used to pull the correct bytes down via `actions/upload-artifact`, then fully reverted — `.github/workflows/ci.yml` is byte-identical, modulo the permanent D-GAP-01 changes, to its state before the diagnostic was ever added). Logged as WINDOWS.md #12 for future rounds (notably plan 03-19, which evaluates cross-platform digest evidence) to account for.

**Secondary finding (out of this plan's scope, recorded and left untouched):** the real CI run reached further into the macOS build than any prior run and surfaced two new, unrelated pre-existing defects — a genuine `-Werror,-Wunused-const-variable` under AppleClang in `tests/unit/test_ebml_scan.cpp:89` (WINDOWS.md #13) and an `arm64-osx`-cross-building-`x64-osx` linker architecture mismatch (WINDOWS.md #14, matching a failure class research/STACK.md already flagged as expected turbulence). Neither is in this plan's `files_modified`; per this plan's own Task 3 instruction ("do not attempt to fix them here"), both were recorded, not fixed.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- **ROADMAP SC5 (encoder-variance half) is closed.** `x64-linux` — the blocking leg SC5 names — concludes `success` on a real run with `TRUST-06` observed passing.
- **Per-leg corpus digest evidence is now in every CI run log**, ready for plan 03-19 to evaluate cross-platform byte-identity. Recorded here for that plan's direct use:
  - `x64-linux`: `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712`
  - `x64-windows-static-md`: `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` (identical to x64-linux)
  - `arm64-osx`: `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4`
  - `x64-osx`: `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` (identical to arm64-osx — expected, both share the same host per this plan's A3)
  - `arm64-linux`: `a1148c1bc3457152a25a032afd03e370c281398521639d60f3ec50a69d446aac`
  - Whether these values "should" match is explicitly not this plan's judgment to make (Task 3's own instruction) — recorded as raw evidence only.
- **Windows and macOS blocking legs remain red**, both for reasons explicitly out of this plan's scope and already tracked: `x64-windows-static-md` on WINDOWS.md #9 (NOMINMAX, owed to plan 03-17), `arm64-osx`/`x64-osx` on WINDOWS.md #13/#14 (new this round, unowned).
- **`arm64-linux` (non-blocking) still fails at the vcpkg NuGet feed step** — pre-existing, WINDOWS.md #11, unrelated to this plan.

## CI Evidence (Task 3, run 33981198277)

**Overall:** `gh run view 33981198277 --json status,conclusion` → `{"conclusion":"failure","status":"completed"}` (failure driven entirely by the three out-of-scope legs below; `x64-linux`, this task's own bar, is `success`).

**Per-leg conclusions (`gh run view 33981198277 --json jobs`):**

| Job | Conclusion | Failing step (if any) |
|---|---|---|
| `build (x64-linux)` | `success` | — |
| `build (arm64-osx)` | `failure` | `Build` (WINDOWS.md #13, AppleClang `-Werror` unused-const-variable, out of scope) |
| `build (x64-windows-static-md)` | `failure` | `Build` (WINDOWS.md #9, NOMINMAX, owed to plan 03-17) |
| `build (arm64-linux)` | `failure` | `Register vcpkg NuGet feed (read-write, trusted runs only)` (WINDOWS.md #11, pre-existing, non-blocking) |
| `build (x64-osx)` | `failure` | `Build` (WINDOWS.md #14, cross-arch linker mismatch, out of scope) |
| `lint (ENG-16 boundary)` | `success` | — |

**`integration.trust06_idempotence` — both cases observed executing and Passed in the `x64-linux` log:**
```
Start 614: integration.trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder
614/620 Test #614: integration.trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder ... Passed 0.01 sec
Start 615: integration.trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this test records whether the pair is byte-identical or merely equivalent
615/620 Test #615: integration.trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this test records whether the pair is byte-identical or merely equivalent ... Passed 0.01 sec
```
Full `x64-linux` Test summary: `100% tests passed, 0 tests failed out of 620` / `Total Test time (real) = 3.80 sec`.

**`Install the pinned ffmpeg build (D-GAP-01)` — same nominal 9.0.1 version on every leg that ran it:**
- `x64-linux`: `install_pinned_ffmpeg.sh: ffmpeg version 9.0.1-https://www.martin-riedl.de Copyright (c) 2000-2026 the FFmpeg developers`
- `arm64-osx`: `install_pinned_ffmpeg.sh: ffmpeg version 9.0.1-https://www.martin-riedl.de Copyright (c) 2000-2026 the FFmpeg developers`
- `x64-windows-static-md`: `install_pinned_ffmpeg.sh: ffmpeg version n9.0.1-11-ge47273f4d9-20260902 Copyright (c) 2000-2026 the FFmpeg developers`

**`CORPUS_DIGEST_SUMMARY=` — one per build leg (values recorded above under Next Phase Readiness; matching/non-matching interpretation deferred to plan 03-19).**

## Self-Check: PASSED

- `scripts/ffmpeg_pin.json` FOUND
- `scripts/install_pinned_ffmpeg.sh` FOUND
- `scripts/corpus_digest.sh` FOUND
- Commits `8a13b8c`, `13ea9db`, `cb324e4`, `d888e27`, `bc09705` all FOUND in `git log --oneline`
- Real CI run `33981198277`: `build (x64-linux)` = `success`, `integration.trust06_idempotence` both `Passed` — confirmed via `gh run view`/`gh run view --log` (see CI Evidence above)

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-05*
