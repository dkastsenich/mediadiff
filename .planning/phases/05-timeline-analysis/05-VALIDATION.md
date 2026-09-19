---
phase: "5"
slug: "timeline-analysis"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
status: validated
nyquist_compliant: true
wave_0_complete: true
created: "2026-09-16"
validated: "2026-09-19"
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
| **Quick run command** | `ctest --preset x64-linux -R "unit\.(timeline\|derive_cadence\|compute_jitter_sigma\|av_drift\|av_sync\|rational_wide\|ts_scan)" --output-on-failure` (178 tests) |
| **Full suite command** | `ctest --preset x64-linux --output-on-failure` (1010 tests) |
| **Estimated runtime** | ~1 s quick subset, ~11 s full suite, measured 2026-09-19 (corpus generation excluded) |

---

## Sampling Rate

- **After every task commit:** `ctest --preset x64-linux -R "unit\.(timeline\|derive_cadence\|compute_jitter_sigma\|av_drift\|av_sync\|rational_wide\|ts_scan)" --output-on-failure`
- **After every plan wave:** `ctest --preset x64-linux --output-on-failure`
- **Before `/gsd-verify-work`:** full suite green, plus the D-13 wall-clock perf recording
- **Max feedback latency:** 30 s for the quick subset

The instruction-count ratchet (D-14/D-16) runs only on the designated `x64-linux` CI leg. It is never a local blocking gate.

---

## Per-Task Verification Map

Task IDs are assigned by the planner; this table is the requirement-to-command contract each task's `<automated>` block must satisfy. Each filter was confirmed with `ctest --preset x64-linux -N -R` (a filter that matches nothing exits 0); the count after each command is the number of tests it selects.

| Req | Behavior | Test Type | Automated Command | Test Files | Status |
|-----|----------|-----------|-------------------|------------|--------|
| TIME-01 | `AV_NOPTS_VALUE` stays `absent` through every timeline check, never coerced | unit | `ctest --preset x64-linux -R "unit\.(timeline_start_duration\|timeline_unwrap\|timeline_monotonic)" --output-on-failure` (48) | `tests/unit/test_timeline_start_duration.cpp`, `tests/unit/test_timeline_unwrap.cpp`, `tests/unit/test_timeline_monotonic.cpp` | ✅ green |
| TIME-02 | 33-bit TS unwrap: wrap distinguished from genuine backward discontinuity; raw and unwrapped values preserved in `timeline.wrap_events` evidence | unit + integration | `ctest --preset x64-linux -R "unit\.timeline_unwrap\|integration\.timeline_structure" --output-on-failure` (34) | `tests/unit/test_timeline_unwrap.cpp`, `tests/integration/test_timeline_structure.cpp` (Tests 4, 6, 12) | ✅ green |
| TIME-03 | `timeline.start` global origin + per-stream relative (D-03); duration triple with incoherence note | unit + integration | `ctest --preset x64-linux -R "unit\.timeline_start_duration\|integration\.timeline_start_duration" --output-on-failure` (33) | `tests/unit/test_timeline_start_duration.cpp`, `tests/integration/test_timeline_start_duration.cpp` | ✅ green |
| TIME-04 | `dts_monotonic`, `pts_unique`, `gaps`, `discontinuities` trigger and clean pairs; container-truth DTS on MPEG-TS | unit + integration | `ctest --preset x64-linux -R "unit\.(timeline_monotonic\|ts_scan)\|integration\.timeline_structure" --output-on-failure` (72) | `tests/unit/test_timeline_monotonic.cpp`, `tests/unit/test_ts_scan.cpp`, `tests/integration/test_timeline_structure.cpp` | ✅ green |
| TIME-05 | Grid-conformance CFR/VFR (D-05), jitter sigma in integer math, `skipped:vfr` on VFR streams | unit + integration | `ctest --preset x64-linux -R "unit\.(derive_cadence\|compute_jitter_sigma)\|integration\.timeline_jitter" --output-on-failure` (30) | `tests/unit/test_cadence.cpp`, `tests/unit/test_jitter_vfr.cpp`, `tests/integration/test_timeline_jitter.cpp` | ✅ green |
| TIME-06, TIME-09 | `av_offset` priming-adjusted, with the priming converted into each container's own timebase; `priming: unknown` carries the unadjusted value at full severity (D-09, D-10, D-11) | unit + integration | `ctest --preset x64-linux -R "unit\.(av_sync\|timeline\.av_offset)\|unit\.compare_tol D-10\|integration\.timeline_av_sync" --output-on-failure` (36) | `tests/unit/test_av_sync.cpp`, `tests/unit/test_tolerance.cpp`, `tests/integration/test_timeline_av_sync.cpp` (Tests 1, 4, 7) | ✅ green |
| TIME-07, TIME-08 | 32-checkpoint least-squares drift; rate, pattern (`constant-offset` / `linear-drift` / `irregular`, UD-1), trajectory stored in the fingerprint; overflow-safe at multi-hour scale | unit + integration | `ctest --preset x64-linux -R "unit\.(av_drift\|rational_wide)\|integration\.timeline_av_sync" --output-on-failure` (31) | `tests/unit/test_av_drift.cpp`, `tests/unit/test_rational_wide.cpp`, `tests/integration/test_timeline_av_sync.cpp` (Tests 5, 6) | ✅ green |
| TIME-10 | Priming-recoverable and priming-unknown fixture pairs (D-12) | integration | `ctest --preset x64-linux -R "integration\.timeline_av_sync" --output-on-failure` (7) | `tests/integration/test_timeline_av_sync.cpp` (Tests 1, 2, 4) | ✅ green |
| TIME-11 | `tmcd` presence, SMPTE value and drop-frame flag; S12M and GOP sources reported as `unreachable_sources` with reason `requires_decode` | integration | `ctest --preset x64-linux -R "integration\.timeline_timecode" --output-on-failure` (5) | `tests/integration/test_timeline_timecode.cpp` (Tests 1–5) | ✅ green |
| DOC-04 | No-others clause: whole-report non-pass count matches each fixture's declared set (D-01, D-02) | integration | `ctest --preset x64-linux -R "integration\.timeline_" --output-on-failure` (41) | every `tests/integration/test_timeline_*.cpp` (`expect_declared_set` / `count_non_pass` in `tests/integration/timeline_findings.h`) | ✅ green |
| PERF-01 | Metadata + timeline of the 10-minute reference file, wall-clock recorded not gated (D-13) | manual-only | `bash scripts/measure_timeline_perf.sh` (records; never asserts) | `scripts/measure_timeline_perf.sh` | ✅ recorded (see Manual-Only) |
| PERF-03, PERF-05 | Instruction-count ratios and the regression ratchet against the committed ledger (D-14, D-15, D-16) | CI script | `bash scripts/measure_timeline_perf.sh --instructions --check-baseline` on the designated `x64-linux` leg | `scripts/measure_timeline_perf.sh`, `tests/golden/PERF_BASELINE.txt`, `.github/workflows/ci.yml` | ✅ green on the designated leg (runs 35389474602, 35391084761) |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `tests/unit/test_rational_wide.cpp` — the 128-bit accumulator added to `core/rational.h`, with a synthetic multi-hour tick magnitude proving no overflow where naive 64-bit accumulation fails
- [x] `tests/unit/test_timeline_unwrap.cpp` (planned as `test_ts_unwrap.cpp`) — hand-constructed tick sequences straddling 2^32 and 2^33, covering wrap versus genuine backward discontinuity
- [x] `tests/unit/test_av_drift.cpp` — doc 04 §3 rate and pattern classification on synthetic timelines (constant-offset, linear-drift, irregular), hitting classification exactly; `step` was withdrawn by UD-1 (05-STEP-DESIGN.md), and a clean step input now pins `irregular`
- [x] `tests/integration/test_timeline_*.cpp` — one file per family (`start_duration`, `structure`, `jitter`, `av_sync`, `timecode`), with a trigger and a clean pair per registered id plus the DOC-04 whole-report count
- [x] `scripts/measure_timeline_perf.sh` plus the committed baseline ledger `tests/golden/PERF_BASELINE.txt` (D-15) and the `apt-get install valgrind` CI step on the designated leg
- [x] Framework install: none — Catch2 and CTest were already wired

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Wall-clock budget on the 10-minute reference file | PERF-01 | D-11/D-13: wall-clock on shared runners is a false-positive source, so it is recorded, never asserted | Run `bash scripts/measure_timeline_perf.sh` on a quiet machine; transcribe the number into the plan summary |
| New corpus digest lines | DOC-04 fixtures | Fixture hashes must come from a designated-leg CI run, never from a workstation (`scripts/lint_corpus_digest_provenance.sh`) | After CI runs, transcribe the designated-leg listing into `tests/golden/CORPUS_DIGEST.txt`; never regenerate existing lines locally |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s (quick subset ~1 s)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** validated 2026-09-19 (`/gsd-validate-phase 5`, dispatched as the nyquist verify:post hook)

---

## Validation Audit 2026-09-19

| Metric | Count |
|--------|-------|
| Gaps found | 3 |
| Resolved | 3 |
| Escalated | 0 |

The three gaps were requirement clauses with no test, each now pinned by a new integration test:

- **TIME-02**: "preserving raw values in evidence". Test 12 in `test_timeline_structure.cpp` asserts that `timeline.wrap_events` evidence carries `first_wrap_raw` / `first_wrap_unwrapped` / `first_wrap_index` on both streams, exactly one 2^33 modulus apart.
- **TIME-06**: "priming-adjusted". Before this audit, nothing would fail if the adjustment stopped being applied: the recoverable pair still fails raw-to-raw, and the MP4-to-MKV copy reads -23 ms raw on both sides. Test 7 in `test_timeline_av_sync.cpp` asserts on every known-priming side:
  - the adjusted basis;
  - an adjusted offset that differs from the raw one;
  - the per-container priming conversion: 1024 ticks in MP4's 1/44100 timebase, 23 in Matroska's 1/1000.
- **TIME-11**: "S12M/GOP timecode". Test 5 in `test_timeline_timecode.cpp` asserts both unreachable sources, with reason `requires_decode`, in their fixed order, on the tmcd-bearing baseline and on the tmcd-absent candidate.

All three assert invariants of the fixture recipe, never byte-dependent values. Fixtures are regenerated on every CI leg.

Map corrections in the same audit:
- Five planning-time filters matched zero tests: `unit\.timeline_absent`, `unit\.ts_unwrap`, `integration\.timeline_av_offset`, `integration\.timeline_priming` and `integration\.timeline_no_others`. A zero-match filter exits 0, so they could never fail.
- Two filters matched only part of their target. `unit\.cadence` missed `unit.derive_cadence`, and `integration\.timeline_av_drift` does not exist.
- The quick-run filter `unit\.(timeline\|cadence\|rational)` missed the cadence, jitter, drift, av_sync and ts_scan groups.
- Every command above is now confirmed with `ctest -N`.

Result: `ctest --preset x64-linux --output-on-failure`, 100% passed, 0 failed out of 1010.
