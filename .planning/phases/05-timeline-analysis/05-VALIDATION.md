---
phase: "5"
slug: "timeline-analysis"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: "2026-09-16"
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from `05-RESEARCH.md` § Validation Architecture.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3 (vcpkg-pinned; `libCatch2.a` present in `build/x64-linux/vcpkg_installed/x64-linux/lib/`) |
| **Config file** | CTest via CMake presets — no separate Catch2 config; `tests/unit/CMakeLists.txt` and `tests/integration/CMakeLists.txt` register targets |
| **Quick run command** | `ctest --preset x64-linux -R "unit\.(timeline\|cadence\|rational)" --output-on-failure` |
| **Full suite command** | `ctest --preset x64-linux --output-on-failure` |
| **Estimated runtime** | ~20 s quick subset, ~3–6 min full suite (corpus generation excluded) |

---

## Sampling Rate

- **After every task commit:** `ctest --preset x64-linux -R "unit\.(timeline\|cadence\|rational)" --output-on-failure`
- **After every plan wave:** `ctest --preset x64-linux --output-on-failure`
- **Before `/gsd-verify-work`:** full suite green, plus the D-13 wall-clock perf recording
- **Max feedback latency:** 30 s for the quick subset

The instruction-count ratchet (D-14/D-16) runs only on the designated `x64-linux` CI leg. It is never a local blocking gate.

---

## Per-Task Verification Map

Task IDs are assigned by the planner; this table is the requirement-to-command contract each task's `<automated>` block must satisfy.

| Req | Behavior | Test Type | Automated Command | File Exists |
|-----|----------|-----------|-------------------|-------------|
| TIME-01 | `AV_NOPTS_VALUE` stays `absent` through every timeline check, never coerced | unit | `ctest --preset x64-linux -R "unit\.timeline_absent" --output-on-failure` | ❌ W0 |
| TIME-02 | 33-bit TS unwrap: wrap distinguished from genuine backward discontinuity | unit | `ctest --preset x64-linux -R "unit\.ts_unwrap" --output-on-failure` | ❌ W0 |
| TIME-03 | `timeline.start` global origin + per-stream relative (D-03); duration triple with incoherence note | integration | `ctest --preset x64-linux -R "integration\.timeline_start_duration" --output-on-failure` | ❌ W0 |
| TIME-04 | `dts_monotonic`, `pts_unique`, `gaps`, `discontinuities` trigger and clean pairs | integration | `ctest --preset x64-linux -R "integration\.timeline_structure" --output-on-failure` | ❌ W0 |
| TIME-05 | Grid-conformance CFR/VFR (D-05) and jitter sigma in integer math | unit + integration | `ctest --preset x64-linux -R "unit\.cadence\|integration\.timeline_jitter" --output-on-failure` | ⚠ partial — `tests/unit/test_cadence.cpp` exists from Phase 4 and must gain D-05 coverage |
| TIME-06, TIME-09 | `av_offset` priming-adjusted; `priming: unknown` carries the unadjusted value at full severity (D-09, D-11) | integration | `ctest --preset x64-linux -R "integration\.timeline_av_offset" --output-on-failure` | ❌ W0 |
| TIME-07, TIME-08 | 32-checkpoint least-squares drift; rate, pattern, trajectory; overflow-safe at multi-hour scale | unit + integration | `ctest --preset x64-linux -R "unit\.av_drift\|integration\.timeline_av_drift" --output-on-failure` | ❌ W0 — highest-priority gap |
| TIME-10 | Priming-recoverable and priming-unknown fixture pairs (D-12) | integration | `ctest --preset x64-linux -R "integration\.timeline_priming" --output-on-failure` | ❌ W0 |
| TIME-11 | `tmcd` presence and SMPTE value; S12M/GOP sources report `skipped:requires_decode` | integration | `ctest --preset x64-linux -R "integration\.timeline_timecode" --output-on-failure` | ❌ W0 |
| DOC-04 | No-others clause: whole-report non-pass count matches each fixture's declared set (D-01, D-02) | integration | `ctest --preset x64-linux -R "integration\.timeline_no_others" --output-on-failure` | ⚠ pattern exists — `tests/integration/test_video_yuvj.cpp:84-93` (`count_non_pass`) |
| PERF-01 | Metadata + timeline of the 10-minute reference file, wall-clock recorded not gated (D-13) | manual-only | `bash scripts/measure_timeline_perf.sh` (records; never asserts) | ❌ W0 |
| PERF-03, PERF-05 | Instruction-count ratios and the regression ratchet against the committed ledger (D-14, D-15, D-16) | CI script | `bash scripts/measure_timeline_perf.sh --instructions --check-baseline` on the designated leg | ❌ W0 |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/test_rational_wide.cpp` (or an extension of the existing rational tests) — the 128-bit accumulator added to `core/rational.h`, with a synthetic multi-hour tick magnitude proving no overflow where naive 64-bit accumulation fails
- [ ] `tests/unit/test_ts_unwrap.cpp` — hand-constructed tick sequences straddling 2^32 and 2^33, covering wrap versus genuine backward discontinuity
- [ ] `tests/unit/test_av_drift.cpp` — doc 04 §3 rate and pattern classification on synthetic timelines (constant-offset, linear-drift, step, irregular), hitting classification exactly
- [ ] `tests/integration/test_timeline_*.cpp` — one file per family, mirroring the `test_video_*.cpp` split, with a trigger and a clean pair per registered id plus the DOC-04 whole-report count
- [ ] `scripts/measure_timeline_perf.sh` plus a committed baseline ledger (D-15) and the `apt-get install valgrind` CI step — valgrind is **not** preinstalled on `ubuntu-24.04` runners
- [ ] Framework install: none — Catch2 and CTest are already wired; only new test files and the perf script are new

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Wall-clock budget on the 10-minute reference file | PERF-01 | D-11/D-13: wall-clock on shared runners is a false-positive source, so it is recorded, never asserted | Run `bash scripts/measure_timeline_perf.sh` on a quiet machine; transcribe the number into the plan summary |
| New corpus digest lines | DOC-04 fixtures | Fixture hashes must come from a designated-leg CI run, never from a workstation (`scripts/lint_corpus_digest_provenance.sh`) | After CI runs, transcribe the designated-leg listing into `tests/golden/CORPUS_DIGEST.txt`; never regenerate existing lines locally |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
