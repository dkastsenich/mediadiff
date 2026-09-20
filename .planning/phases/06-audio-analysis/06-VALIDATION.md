---
phase: "6"
slug: "audio-analysis"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: "2026-09-20"
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Seeded by plan-phase from `06-RESEARCH.md` § Validation Architecture.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3 (already integrated: `tests/unit/`, `tests/integration/`, CTest via `catch_discover_tests`) |
| **Config file** | `CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/x64-linux -R "audio" --output-on-failure` |
| **Full suite command** | `cmake --build build/x64-linux 2>&1 \| tail -5 && ctest --test-dir build/x64-linux --output-on-failure` |
| **Estimated runtime** | ~60 seconds (full suite, x64-linux; corpus generation excluded) |

**Corpus prerequisite.** Fixture-dependent tests require the generated corpus:
`bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_corpus_digest_provenance.sh`
(Phase 5's proven command form — `prior_verify_commands`.)

---

## Sampling Rate

- **After every task commit:** Run the targeted subset — `ctest --test-dir build/x64-linux -R "audio" --output-on-failure`
- **After every plan wave:** Run the full suite (`mediadiff_unit_tests` + `mediadiff_integration_tests`)
- **Before `/gsd-verify-work`:** Full suite green, plus the extended perf harness for PERF-04
- **Max feedback latency:** ~60 seconds

---

## Per-Task Verification Map

*Seeded at plan time; plans do not exist yet. `/gsd-validate-phase` fills task IDs after PLAN.md files are written. Requirement-level mapping below comes from `06-RESEARCH.md` § Validation Architecture.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | AUDIO-01 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_stream_params" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-02 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_stream_params" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-03 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_profile_sbr" --output-on-failure` | ❌ W0 (blocked on the D-10 fixture writer) | ⬜ pending |
| TBD | TBD | TBD | AUDIO-04 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_priming" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-05 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_loudness" --output-on-failure` | ❌ W0 (needs the committed ebur128 text reference, D-13) | ⬜ pending |
| TBD | TBD | TBD | AUDIO-06 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_loudness" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-07 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_silence" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-08 | T-6-* (hash evidence) | Divergence report never fabricates a location | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_sample_hash" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-09 | — | N/A | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_sample_hash" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | AUDIO-10 | — | N/A | unit | `ctest --test-dir build/x64-linux -R "unit\.(packet_scan\|pass_union)" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | TRUST-01 | T-6-* (path signature) | Recorded decode path is never omitted for a hashed stream | unit | `ctest --test-dir build/x64-linux -R "unit\.decode_path" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | TRUST-02 | T-6-* (fabricated verdict) | A class-2 cross-path compare skips, never passes or fails | integration | `ctest --test-dir build/x64-linux -R "integration\.audio_sample_hash" --output-on-failure` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | PERF-04 | — | N/A | perf harness | `bash scripts/measure_timeline_perf.sh --audio` (harness extension) | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/integration/test_audio_stream_params.cpp` — AUDIO-01, AUDIO-02, AUDIO-03
- [ ] `tests/integration/test_audio_priming.cpp` — AUDIO-04, extending the existing declared-finding-set harness
- [ ] `tests/integration/test_audio_loudness.cpp` — AUDIO-05, AUDIO-06, against the committed `ffmpeg -af ebur128` text reference (D-13)
- [ ] `tests/integration/test_audio_silence.cpp` — AUDIO-07
- [ ] `tests/integration/test_audio_sample_hash.cpp` — AUDIO-08, AUDIO-09, TRUST-02
- [ ] `tools/gen_he_aac.py` with `--selftest` — the D-10/D-11 bitstream writer; blocks AUDIO-03's fixture pair and the two-build class-1 proof
- [ ] `docs/checks/audio.*.md` and `docs/checks/content.audio.sample_hash.md` — DOC-01 is build-enforced, and zero `audio.*` checks are registered today
- [ ] Audio fixture recipes in `scripts/gen_corpus.sh`, with new digest lines transcribed from a designated-leg CI run (never regenerated locally)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Cross-architecture bit-exactness of the class-1 fixed-point decoders | AUDIO-09, TRUST-01 | No arm64 target is available locally; only a real CI leg can prove it (research Open Question 1) | Run the phase's class-1 proof on every CI leg and compare against the designated-leg snapshot; the fixture identity guard (D-11) must pass first |
| `inspect` audio section rendering | AUDIO-01 (SC1) | Rendered TTY output is reviewed by a human for completeness and column layout | `./build/x64-linux/mediadiff inspect tests/fixtures/<audio fixture>` and read the audio section |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
