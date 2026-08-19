---
phase: 2
slug: core-engine
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-15
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from `02-RESEARCH.md` § Validation Architecture.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3, driven via CTest (`include(CTest)` + `include(Catch)` + `catch_discover_tests`) — already installed and wired in Phase 1 |
| **Config file** | `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` (both exist) |
| **Quick run command** | `ctest --test-dir build/x64-linux -R unit` |
| **Full suite command** | `ctest --test-dir build/x64-linux --output-on-failure` |
| **Estimated runtime** | ~30 seconds (unit); ~90 seconds (full) — re-measure at Wave 0 |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/x64-linux -R unit`
- **After every plan wave:** Run `ctest --test-dir build/x64-linux --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green, **plus** the D-16 canary fixture explicitly confirmed still reporting its one expected failure. A green suite where the canary silently started passing is the exact Phase-1 repeat this phase's discipline exists to prevent.
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

> Task IDs are assigned by the planner. Rows below are the requirement→test contract each
> planned task must satisfy; the planner binds Task IDs when PLAN.md files are written.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | 0 | D-14 / D-15 / D-16 | — | Fail-first discipline: every semantic × status combination has a triggering fixture; canary fixture must stay red | integration/CI | coverage-assertion step + canary fixture check | ❌ W0 | ⬜ pending |
| TBD | TBD | 1 | ENG-01, DOC-01 | — | Build fails when a registered check ID has no documentation | unit | `ctest -R unit.registry_generator` | ❌ W0 | ⬜ pending |
| TBD | TBD | 1 | ENG-04 | — | All seven comparison semantics behave per spec; time tolerances compared in ticks, not floats | unit | `ctest -R unit.compare_semantics` | ❌ W0 | ⬜ pending |
| TBD | TBD | 1 | ENG-02 | T-2-02 | Glob matcher `*`/`**` segment-wise; regex explicitly forbidden (removes ReDoS as a class) | unit | `ctest -R unit.glob_matcher` | ❌ W0 | ⬜ pending |
| TBD | TBD | 1 | D-06 | — | `Value` variant exhaustiveness via `std::visit` across all nine alternatives | unit | `ctest -R unit.value_variant` | ❌ W0 | ⬜ pending |
| TBD | TBD | 1 | D-08, TRUST-05 | — | Central serializer: float formatting and field order are deterministic; `std::to_chars` smoke assertion | unit | `ctest -R unit.serializer` | ❌ W0 | ⬜ pending |
| TBD | TBD | 2 | ENG-06, ENG-11 | T-2-01 | Precedence merger is last-writer-wins under argv permutation; malformed `mediadiff.toml` yields a `usage` Error, never a crash | unit (property-style) | `ctest -R unit.policy_merge` | ❌ W0 | ⬜ pending |
| TBD | TBD | 2 | ENG-05, CLI-10 | T-2-02 | Wrong-unit tolerance rejected as exit 64 naming the expected unit; unrecognized grammar rejected cleanly | integration | `ctest -R integration.exit_codes` | ❌ W0 | ⬜ pending |
| TBD | TBD | 2 | SNAP-03, TRUST-05 | — | Snapshot round-trip byte-identity; `--json` byte-identical across identical runs | unit + integration | `ctest -R unit.snapshot_roundtrip` / `ctest -R integration.compare_twice` | ❌ W0 | ⬜ pending |
| TBD | TBD | 2 | SNAP-07 | T-2-03 | `snapshot` refuses to overwrite a tracked baseline in CI without `--force` | integration | `ctest -R integration.snapshot_safe_write` | ❌ W0 | ⬜ pending |
| TBD | TBD | 2 | SNAP-06 | — | `compare` refuses an incompatible `schema_version` major with exit 65 | integration | `ctest -R integration.schema_version` | ❌ W0 | ⬜ pending |
| TBD | TBD | 3 | REPORT-01 | — | Emitted JSON validates against the shipped schema | integration | `ctest -R integration.json_schema` | ❌ W0 | ⬜ pending |
| TBD | TBD | 3 | REPORT-02, REPORT-04, REPORT-06 | — | Golden TTY / Markdown / JUnit bytes; Markdown budgeted under 65,536 chars | golden | `ctest -R golden` (refresh: `UPDATE_GOLDENS=1 ctest -R golden`) | ❌ W0 | ⬜ pending |
| TBD | TBD | 3 | CLI-08 | — | Colour auto-disables on `NO_COLOR`, non-TTY stdout, `CI=true`; stays enabled for `GITHUB_ACTIONS` | unit | `ctest -R unit.color_policy` | ❌ W0 | ⬜ pending |
| TBD | TBD | 4 | DIR-01…DIR-05 | — | Deterministic relative-path pairing, unpaired reporting, `--threads`-bounded pool | integration | `ctest -R integration.dir_mode` | ❌ W0 | ⬜ pending |
| TBD | TBD | 4 | CLI-06 | — | Exit-code contract 0/1/2 and 64/65/66/70; partial JSON still emitted on 66 | integration | `ctest -R integration.exit_codes` | ❌ W0 | ⬜ pending |
| TBD | TBD | 4 | DOC-01, DOC-02 | — | `explain <check.id>` prints measures/matters/accept-tune-silence; `inspect` renders every implemented family | integration | `ctest -R integration.explain_inspect` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] **Fail-first meta-infrastructure first** — the D-14/D-15/D-16 coverage-assertion step (semantic × status matrix) and the permanent canary fixture. Phase 1's verification report documented six checks structurally incapable of observing their own subject; this is built as Wave-0 infrastructure, not bolted on later.
- [ ] `tests/unit/test_registry_generator.cpp` — ENG-01/DOC-01 build-failure-on-missing-doc behavior, run against a fixture `checks.def` rather than the real one (the real file grows through Phases 3–7 and must not gate Phase 2's own tests)
- [ ] `tests/unit/test_value_variant.cpp` — `std::visit` exhaustiveness across all nine `Value` alternatives (D-06)
- [ ] `tests/unit/test_serializer.cpp` — D-08 central serializer float-formatting and field-order guarantees, including a `std::to_chars` smoke assertion (Assumption A4)
- [ ] `tests/fixtures/snapshots/` — hand-authored `.snap.json` pairs, one triggering + one clean per semantic (D-10/D-14). None exist yet beyond the empty `tests/fixtures/` directory and its `GENERATOR_MANIFEST.json`.
- [ ] `tests/golden/` directory and `UPDATE_GOLDENS` env-var wiring — does not exist yet
- [ ] Framework install: **none needed** — Catch2 already installed and wired from Phase 1

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Colour renders as styling rather than literal escape sequences in a **real** Windows console | CLI-08 | Requires an actual Windows console host; CI capture redirects stdout, which disables colour by design and therefore cannot observe the rendering | On a Windows VS 2022 runner or dev box, run `mediadiff compare a b` in `conhost.exe`/Windows Terminal with stdout attached and confirm styled (not escape-literal) output |
| JUnit XML displays with zero integration work in Jenkins/GitLab | REPORT-06 | Third-party CI UI rendering cannot be asserted from within this repo's test suite | Upload a generated `junit.xml` to a scratch Jenkins job and a GitLab pipeline; confirm test names and failure messages render without custom parsing |
| Markdown report renders correctly as a GitHub PR comment | REPORT-04 | GitHub's comment renderer is external; only the 65,536-character budget is automatable | Paste a generated report into a scratch PR comment and confirm formatting, then confirm the automated length assertion still guards the budget |
| Cross-release `--json` byte-identity against a snapshot from a previous release | TRUST-08 | No prior release exists yet — bootstrap problem flagged in RESEARCH.md Open Question 1 | Establish this phase's output as the frozen baseline; the comparison becomes automatable from the next release onward |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
