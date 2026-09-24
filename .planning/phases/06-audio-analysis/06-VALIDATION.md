---
phase: "6"
slug: "audio-analysis"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: validated
nyquist_compliant: true
wave_0_complete: true
created: "2026-09-20"
validated: "2026-09-24"
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Seeded by plan-phase from `06-RESEARCH.md` § Validation Architecture.
> Audited by `/gsd-validate-phase 6` on 2026-09-24, after plans 06-01..06-20 (gap closure included) completed.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3 (already integrated: `tests/unit/`, `tests/integration/`, CTest via `catch_discover_tests`) |
| **Config file** | `CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/x64-linux -R "audio" --output-on-failure` |
| **Full suite command** | `cmake --build build/x64-linux 2>&1 \| tail -5 && ctest --test-dir build/x64-linux --output-on-failure` |
| **Estimated runtime** | ~28 seconds (full suite, 1251 tests, x64-linux; corpus generation excluded) |

**Corpus prerequisite.** Fixture-dependent tests require the generated corpus:
`bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_corpus_digest_provenance.sh`
(Phase 5's proven command form — `prior_verify_commands`.) The five designated-leg goldens additionally need
`TZ=UTC taskset -c 0-3 bash scripts/gen_corpus.sh` and `MEDIADIFF_DESIGNATED_LEG=1` to run locally.

---

## Sampling Rate

- **After every task commit:** Run the targeted subset — `ctest --test-dir build/x64-linux -R "audio" --output-on-failure`
- **After every plan wave:** Run the full suite (`mediadiff_unit_tests` + `mediadiff_integration_tests`), plus the CI lint scripts (`scripts/lint_*.sh`, `scripts/test_gen_corpus_pin_gate.sh`) — the lints catch MSVC-only and source-shape defects a Linux ctest cannot
- **Before `/gsd-verify-work`:** Full suite green, plus the audio instruction ratchet for PERF-04
- **Max feedback latency:** ~30 seconds

---

## Per-Task Verification Map

*Filled 2026-09-24 from the 20 PLAN.md files' requirement IDs and `<automated>` verify blocks. Every non-checkpoint task (46 of 52) carries an automated verify; the six without one are human checkpoints by design (06-01 T1 decision, 06-10 T2 and 06-11 T2 human-verify, 06-13 T1/T3 and 06-20 T2 human-action). Status is the result of running each command on 2026-09-24 at HEAD `ac0ec7e`, and, for CI-only evidence, designated-leg run 35987510562.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 06-03, 06-04, 06-11 | 06-03, 06-04, 06-11 | 3, 4, 11 | AUDIO-01 | — | N/A | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_stream_params\|unit\.audio_stream_params\|unit\.inspect_audio" --output-on-failure` (26 tests) | ✅ | ✅ green |
| 06-02, 06-03, 06-11 | 06-02, 06-03, 06-11 | 2, 3, 11 | AUDIO-02 | — | N/A | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_stream_params\|unit\.audio_stream_params" --output-on-failure` (18 tests) | ✅ | ✅ green |
| 06-02, 06-04, 06-11, 06-18 | 06-02, 06-04, 06-11, 06-18 | 2, 4, 11, 18 | AUDIO-03 | T-06 (CR-05 probe determinism) | `audio.profile` depends only on the file's bytes; a probe timeout is a hard Error, never `(sbr: unknown)` | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_profile_sbr\|unit\.audio_config" --output-on-failure` (39 tests) | ✅ | ✅ green |
| 06-02, 06-06, 06-07, 06-19 | 06-02, 06-06, 06-07, 06-19 | 2, 6, 7, 19 | AUDIO-04 | T-06 (WR-07) | `span_basis: "adjusted"` only when the trimmed reconstruction succeeded | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_priming\|unit\.audio_priming\|unit\.priming_resolver\|unit\.av_sync - span_ticks_for_basis" --output-on-failure` (30 tests) | ✅ | ✅ green |
| 06-02, 06-08, 06-16 | 06-02, 06-08, 06-16 | 2, 8, 16 | AUDIO-05 | T-06 (CR-03) | A non-finite or out-of-range float sample stops level measurement; never reaches `llround`/libebur128 | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_loudness\|unit\.loudness_sink" --output-on-failure` (21 tests) | ✅ | ✅ green |
| 06-02, 06-08, 06-17 | 06-02, 06-08, 06-17 | 2, 8, 17 | AUDIO-06 | T-06 (CR-04) | Sub-0.010 dB ceiling crossings keep their tolerance verdict; every material crossing still fails | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_loudness\|ceiling escalation" --output-on-failure` (20 tests) | ✅ | ✅ green |
| 06-02, 06-09, 06-16 | 06-02, 06-09, 06-16 | 2, 9, 16 | AUDIO-07 | — | N/A | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_silence\|unit\.silence_sink" --output-on-failure` (18 tests) | ✅ | ✅ green |
| 06-01, 06-05, 06-10, 06-14, 06-15 | 06-01, 06-05, 06-10, 06-14, 06-15 | 1, 5, 10, 14, 15 | AUDIO-08 | T-6-* (hash evidence), T-06 (CR-01/CR-02, WR-02) | Divergence report never fabricates a location; a truncated or reconfigured decode degrades to `hash_incomparable` or a recorded stop token | integration + unit | `ctest --test-dir build/x64-linux -R "integration\.audio_sample_hash\|unit\.audio_decode" --output-on-failure` (50 tests) | ✅ | ✅ green |
| 06-05, 06-13, 06-20 | 06-05, 06-13, 06-20 | 5, 13, 20 | AUDIO-09 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_sample_hash\|integration\.audio_hash_decoder\|integration\.audio_corpus_sweep\|integration\.doc03_coverage" --output-on-failure` (32 tests; Passed, not Skipped, on the designated leg in run 35987510562) | ✅ | ✅ green |
| 06-01, 06-08, 06-09, 06-10, 06-12, 06-15 | 06-01, 06-08, 06-09, 06-10, 06-12, 06-15 | 1, 8, 9, 10, 12, 15 | AUDIO-10 | — | N/A | unit + integration | `ctest --test-dir build/x64-linux -R "unit\.packet_scan\|unit\.pass_union\|integration\.audio_decode_errors" --output-on-failure` (37 tests) | ✅ | ✅ green |
| 06-01, 06-05, 06-13, 06-20 | 06-01, 06-05, 06-13, 06-20 | 1, 5, 13, 20 | TRUST-01 | T-6-* (path signature) | Recorded decode path is never omitted for a hashed stream | unit + integration | `ctest --test-dir build/x64-linux -R "unit\.decode_path_record\|integration\.audio_hash_decoder" --output-on-failure` (24 tests) | ✅ | ✅ green |
| 06-05, 06-14 | 06-05, 06-14 | 5, 14 | TRUST-02 | T-6-* (fabricated verdict) | A class-2 cross-path compare skips, never passes or fails; either side truncated skips as `hash_incomparable` | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_sample_hash\|integration\.audio_hash_decoder - class proof" --output-on-failure` (21 tests) | ✅ | ✅ green |
| 06-12, 06-13, 06-20 | 06-12, 06-13, 06-20 | 12, 13, 20 | PERF-04 | — | N/A | perf harness | `bash scripts/measure_audio_perf.sh --instructions --check-baseline` (designated leg; run 35987510562: audio_plain 89275471 vs 89344287, audio_full 47436385679 vs 47339403661, both within ±2%) | ✅ | ✅ green |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `tests/integration/test_audio_stream_params.cpp` — AUDIO-01, AUDIO-02, AUDIO-03
- [x] `tests/integration/test_audio_priming.cpp` — AUDIO-04, extending the existing declared-finding-set harness
- [x] `tests/integration/test_audio_loudness.cpp` — AUDIO-05, AUDIO-06, against the committed `ffmpeg -af ebur128` text reference (D-13)
- [x] `tests/integration/test_audio_silence.cpp` — AUDIO-07
- [x] `tests/integration/test_audio_sample_hash.cpp` — AUDIO-08, AUDIO-09, TRUST-02
- [x] `tools/gen_he_aac.py` with `--selftest` — the D-10/D-11 bitstream writer; blocks AUDIO-03's fixture pair and the two-build class-1 proof
- [x] `docs/checks/audio.*.md` and `docs/checks/content.audio.sample_hash.md` — DOC-01 is build-enforced (12 `audio.*` docs plus the sample-hash doc)
- [x] Audio fixture recipes in `scripts/gen_corpus.sh`, with digest lines confirmed by the designated leg's corpus-digest assert (run 35987510562: 203 lines compared)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Cross-architecture bit-exactness of the class-1 fixed-point decoders | AUDIO-09, TRUST-01 | No arm64 target is available locally; only a real CI leg can prove it (research Open Question 1). **Now automated on CI:** `integration.audio_hash_decoder` class proof Test 2 and Test 9 run on every blocking leg; both Passed on arm64-osx and x64-windows-static-md in run 35987510562. | Run the phase's class-1 proof on every CI leg and compare against the designated-leg snapshot; the fixture identity guard (D-11) must pass first |
| `inspect` audio section rendering | AUDIO-01 (SC1) | Rendered TTY output is reviewed by a human for completeness and column layout (content is covered automatically by `unit.inspect_audio`, 8 tests) | `./build/x64-linux/mediadiff inspect tests/fixtures/<audio fixture>` and read the audio section |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies (46 of 52 automated; the other 6 are human checkpoints by design)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 60s (full suite ~28 s)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** validated 2026-09-24 (`/gsd-validate-phase 6`)

---

## Validation Audit 2026-09-24

| Metric | Count |
|--------|-------|
| Gaps found | 0 |
| Resolved | 0 |
| Escalated | 0 |

All 13 phase requirements map to plan tasks with automated verification, and every mapped command ran green at HEAD `ac0ec7e`. The run covered 1251/1251 tests in the full suite and 11/11 CI lints. The CI-only evidence (PERF-04's ratchet, AUDIO-09's designated-leg gates and the cross-architecture class-1 proof) comes from run 35987510562 and is transcribed in `06-20-SUMMARY.md`. No test files were generated.
