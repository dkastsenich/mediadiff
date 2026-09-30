---
phase: "7"
slug: "content-quality"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: "2026-09-30"
---

# Phase 7 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Seeded from `07-RESEARCH.md` § Validation Architecture. Task IDs are assigned by the planner;
> `/gsd-validate-phase 7` refines this map against the executed plans.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3 via CTest (`catch_discover_tests`) |
| **Config file** | `CMakeLists.txt`, `CMakePresets.json` |
| **Quick run command** | `ctest --test-dir build/x64-linux -R '<regex>' --output-on-failure` |
| **Full suite command** | `ctest --preset x64-linux --output-on-failure` |
| **Estimated runtime** | ~23 seconds (1251 tests, measured at research time) |

---

## Sampling Rate

- **After every task commit:** Run the task's `ctest -R '<regex>'` quick command plus the lint scripts named in `.github/workflows/ci.yml` that the task's files touch (the build+ctest gate misses MSVC-only and source-shape lint violations).
- **After every plan wave:** Run `ctest --preset x64-linux --output-on-failure`.
- **Before `/gsd-verify-work`:** Full suite green, DOC-03 gate (`tests/integration/test_doc03_coverage.cpp`) green, designated-leg goldens checked with the local differential recipe (regenerate fixtures under `TZ=UTC taskset -c 0-3` and run the designated-leg goldens directly), and CI green on all five legs.
- **Max feedback latency:** ~30 seconds for the full local suite.

---

## Per-Task Verification Map

Requirement-level map; the planner binds each row to concrete task IDs (`07-NN-MM`).

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | CONTENT-01 | — | N/A | integration | `ctest --test-dir build/x64-linux -R video_hash --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-02 | — | N/A | integration | `ctest --test-dir build/x64-linux -R video_locator --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-03 | — | N/A | unit | `ctest --test-dir build/x64-linux -R sampling_mismatch --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-04 | — | N/A | unit + integration | `ctest --test-dir build/x64-linux -R perceptual --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-05 | — | N/A | integration | `ctest --test-dir build/x64-linux -R lockstep_pairing --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-06 | — | N/A | integration | `ctest --test-dir build/x64-linux -R video_detectors --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-07 | — | N/A | integration | `ctest --test-dir build/x64-linux -R single_sweep --output-on-failure` | ✅ extend | ⬜ pending |
| TBD | TBD | TBD | CONTENT-08 | — | N/A | unit | `ctest --test-dir build/x64-linux -R ssim_int --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-09 | — | N/A | integration | `ctest --test-dir build/x64-linux -R vmaf --output-on-failure` | ✅ extend `integration.vmaf_absent` | ⬜ pending |
| TBD | TBD | TBD | CONTENT-10 | — | N/A | integration | `ctest --test-dir build/x64-linux -R quality_snapshot --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CONTENT-11 | — | bounded memory: one decoded frame in flight per side | integration / stress | `ctest --test-dir build/x64-linux -R lockstep --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | VIDEO-11 | — | N/A | integration | `ctest --test-dir build/x64-linux -R closed_captions --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | VIDEO-09 (first-frame arm) | — | N/A | integration | `ctest --test-dir build/x64-linux -R hdr_first_frame --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | TRUST-04 | — | precondition mismatch is an explicit skip, never a fabricated fail | unit | `ctest --test-dir build/x64-linux -R tol_path_signature --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | TRUST-07 | — | N/A | integration | `ctest --test-dir build/x64-linux -R video_thread_invariance --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | PERF-02 | — | N/A | script + CI | `bash scripts/measure_video_perf.sh --check-baseline` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | D-12 / D-13 (watchdog, T-06-34) | T-06-34 | a stalled libav call trips the watchdog: report flushed, exit 66, never a changed value | integration (test hook) | `ctest --test-dir build/x64-linux -R watchdog --output-on-failure` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/test_ssim_int.cpp`, `tests/unit/test_sampling_mismatch.cpp`, `tests/unit/test_tol_path_signature.cpp`, `tests/unit/test_gen_video_fixtures.cpp` (Python writer tests, following `tests/unit/test_gen_he_aac.cpp`)
- [ ] `tests/integration/test_video_hash.cpp`, `test_video_locator.cpp`, `test_video_detectors.cpp`, `test_lockstep.cpp`, `test_video_thread_invariance.cpp`, `test_closed_captions.cpp`, `test_hdr_first_frame.cpp`, `test_watchdog.cpp`
- [ ] New fixtures appended to `tests/fixtures/GENERATOR_MANIFEST.json` and the corpus digest (append only; never regenerate existing lines)
- [ ] `tests/golden/VIDEO_PROOF_CHAINS.txt` and the video lines of `tests/golden/PERF_BASELINE.txt` (human-transcribed after the first CI measurement)
- [ ] `docs/checks/<id>.md` for every new check ID (DOC-01/DOC-03 gate) and the skip-reason additions in `docs/schema/report-1.0.json`
- [ ] `.github/workflows/ci.yml`: the `video-proof-streams` job, updated `EXCLUDED_TEST_REGEX` / `EXPECTED_EXCLUDED_COUNT`, and the proof-gate guard
- [ ] Framework install: none (Catch2 already present)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Cross-architecture class-1 proof for each video decoder (D-09/D-10) | TRUST-07, CONTENT-01 | Needs the four non-designated CI legs decoding the designated leg's proof streams; cannot run on one machine | Land the `video-proof-streams` job report-only, read every leg's chains from the first CI run, transcribe `tests/golden/VIDEO_PROOF_CHAINS.txt` in a reviewed commit, then promote the job to a failing gate |
| PERF-02 instruction-count baseline | PERF-02 | Baseline must come from the designated `x64-linux` leg's own measurement (Phase 5 D-15: CI measures, humans update) | Run the CI perf step, copy the printed replacement lines into `tests/golden/PERF_BASELINE.txt` in a reviewed commit |
| `quality.vmaf` on a VMAF-enabled build | CONTENT-09 | The default build keeps libvmaf absent (BUILD-09), and the vcpkg libvmaf port does not build on Windows | Configure with `MEDIADIFF_WITH_VMAF=ON` on Linux/macOS, run the vmaf integration tests, confirm `vmaf_v0.6.1` is recorded in the fingerprint |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
