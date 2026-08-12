---
phase: 1
slug: foundation-toolchain
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-12
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Seeded from `01-RESEARCH.md` § Validation Architecture.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3 (vcpkg) + CTest |
| **Config file** | none yet — `tests/unit/CMakeLists.txt` is Wave 0 scope |
| **Quick run command** | `ctest --preset <triplet> -R unit --output-on-failure` |
| **Full suite command** | `ctest --preset <triplet> --output-on-failure` |
| **Estimated runtime** | ~5 seconds (unit) / ~30 seconds (full, includes binary-spawning integration tests) |

**Note:** `<triplet>` resolves per platform — `x64-linux`, `arm64-osx`, `x64-windows-static-md` (the three blocking triplets per locked decision D-06).

---

## Sampling Rate

- **After every task commit:** Run `ctest --preset <triplet> -R unit --output-on-failure` (Catch2 unit tests only — fast)
- **After every plan wave:** Run `ctest --preset <triplet> --output-on-failure` (full suite including integration tests that spawn the real binary)
- **Before `/gsd-verify-work`:** Full 3-OS CI matrix green (3 blocking + 2 non-blocking per D-06)
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

Task IDs are assigned by the planner; this table seeds the requirement → verification mapping that each task must inherit. Populated with concrete task IDs during `/gsd-validate-phase`.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | BUILD-01 | — | N/A | CI job | `cmake --preset <triplet> && cmake --build --preset <triplet>` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-02 | T-01-03 | Pinned registry, no third-party sources | CI (two-run) | second `cmake --preset` run resolves an identical package set | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-03 | T-01-01 | Build cannot ship GPL-linked binary | unit | `ctest -R test_license` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-04 | — | N/A | integration | run binary with PATH stripped; Linux `ldd build/mediadiff \| grep -i avcodec` is empty | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-05 | T-01-04 | Warnings-as-errors is a hardening control | CI config | CI job status across 3 blocking triplets | n/a | ⬜ pending |
| TBD | TBD | TBD | BUILD-06 | T-01-04 | Cache feed not writable from fork PRs | CI (log assertion) | NuGet restore log lines on second run | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-07 | — | N/A | unit | `ctest -R test_expected` — instantiate `mediadiff::expected<int, Error>`, exercise `and_then` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-08 | — | N/A | integration | run script twice, diff `GENERATOR_MANIFEST.json` byte-identical except the timestamp field | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-09 | — | N/A | integration | default build `--version` output does **not** contain `vmaf` in the feature list | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | BUILD-10 | — | N/A | doc check (manual) | PROJECT.md Key Decisions table contains the D-01 FFmpeg-baseline entry | n/a | ⬜ pending |
| TBD | TBD | TBD | CLI-05 | — | N/A | integration | run binary `--version`, regex-match all four required fields (tool version, FFmpeg lib versions, license string, feature list) | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | CLI-09 | T-01-02 | No ANSI/UTF-8 code-page confusion opening the wrong file | integration (Windows job) | `util/fs.h` round-trip open/read/write on a test-created non-ASCII filename | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/CMakeLists.txt` — Catch2 3 wiring via `find_package(Catch2 3 REQUIRED)` + `catch_discover_tests`
- [ ] `tests/unit/test_license.cpp` — BUILD-03 exact-match LGPL assertion (see Manual-Only note below on why exact-match, not substring)
- [ ] `tests/unit/test_expected.cpp` — BUILD-07 alias smoke test
- [ ] `tests/integration/` harness — helper that shells out to the built `mediadiff` binary and captures stdout, serving CLI-05, BUILD-04, BUILD-08, BUILD-09 and CLI-09
- [ ] Framework install — `find_package(Catch2 3 REQUIRED)` resolves once `catch2` is in `vcpkg.json` dependencies

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| VT / ANSI color renders in a real Windows console | CLI-09 | Terminal-capture assertions are brittle and platform-dependent; the file-I/O half of the criterion **is** automated (see map above). Research recommends recording this as a `checkpoint:human-verify` rather than forcing a fragile automated capture. | Run `mediadiff --version` in Windows Terminal and in `cmd.exe`; confirm ANSI styling renders rather than printing raw escape sequences. Confirm `NO_COLOR=1` suppresses it. |
| FFmpeg baseline recorded as an explicit decision | BUILD-10 | A documentation assertion, not a code behavior. | Confirm `.planning/PROJECT.md` Key Decisions table contains the D-01 entry naming the pinned FFmpeg major version and its rationale. |

**Scope note on CLI-09 (from research):** success criterion #5 mixes an automatable file-I/O assertion with an inherently visual VT-rendering check. Phase 1 automates the file-I/O half only. `libavformat`'s own UTF-8 path handling is not exercised until real media decode exists (Phase 3+), so no FFmpeg-level automated path test is in scope here.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
