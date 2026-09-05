---
schema_version: 1
open_count: 12
waived_count: 0
fixed_count: 2
total_count: 14
last_updated: 2026-09-05T17:26:26.638Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 02 | stub | src/report/json.cpp |  | Finding.delta and Finding.evidence render as JSON null unconditionally -- core/model.h's Finding carries neither field; both keys are schema-nullable, populated by a future plan without a schema change | open |  | 2026-08-15T18:34:50.837Z |  |
| 2 | 03 | deviation | src/report/model.cpp |  | accumulate() previously gated worst_gating on a check's declared severity regardless of finding status; fixed to only gate on Status::warn/fail/error (see 03-02-SUMMARY.md deviations). | fixed |  | 2026-09-02T18:26:25.460Z | 2026-09-02T18:26:52.963Z |
| 3 | 03 | unrun-verify | src/probe/bmff_scan.cpp |  | ASan/UBSAN full-suite run for bmff_scan not performed: no sanitizer CMake preset exists in this repo (03-03-SUMMARY.md's own precedent notes the same gap); acceptance criterion explicitly permits recording this instead of claiming it. | open |  | 2026-09-02T20:13:42.454Z |  |
| 4 | 03 | unrun-verify | src/analyzers/container/mkv.cpp |  | container.mkv.codec_delay's skipped:insufficient_data path (unknown SamplingFrequency) and an explicit CodecDelay=0 value are not exercised by any fixture-level test -- no reasonably-constructible bitexact fixture reliably produces either (an ffmpeg Opus track always carries SamplingFrequency; Opus priming is never exactly zero). | open |  | 2026-09-02T20:53:44.223Z |  |
| 5 | 03 | deviation | src/probe/ebml_scan.cpp |  | 03-06 tasks carried tdd="true" but tdd_mode is false for this phase; tests and implementation were developed together (test-first in practice, verified via real fixture failures) rather than following separate RED/GREEN commits. | open |  | 2026-09-02T20:53:44.324Z |  |
| 6 | 02 | deviation | tests/unit/test_markdown_budget.cpp |  | ASan stack-use-after-scope: make_finding() test helper binds Finding::id (string_view) to a temporary std::string built per loop iteration, violating the documented static-storage-duration contract. Test-only, zero production risk (all 4 production Finding.id writers use CheckDef::id). Found during 03-10's sanitizer_note one-off ASan/UBSan build; see .planning/phases/03-probe-layer-container-size/deferred-items.md | open |  | 2026-09-03T21:45:41.359Z |  |
| 7 | 03 | deviation | tests/integration/test_doc03_coverage.cpp |  | container.ts.psi_interval/pmt_version_churn have no fixture pair that perturbs same-topology PAT/PMT spacing or PMT version directly; their declared DOC-03 trigger pair (ts_single.ts vs ts_multiprogram.ts) fires both via the CONT-08 unpaired-program topology-mismatch path instead, satisfying the gate's own trigger definition but not the intended semantic trigger. | open |  | 2026-09-03T22:38:18.319Z |  |
| 8 | 03 | deviation | .github/workflows/ci.yml |  | scripts/gen_corpus.sh (the Linux/macOS fixture generator) is never invoked anywhere in .github/workflows/ci.yml -- only the Windows-specific gen_corpus.ps1 positive/negative-path check runs. Predates this plan (present since Phase 1); every corpus-dependent integration test would fail on a real CI run for the Linux/macOS/x64-windows(sh) legs until a fixture-generation step is added to the Test step or a preceding step. Discovered while verifying TRUST-06's CI wiring; out of this plan's scope to fix. | fixed |  | 2026-09-03T22:38:18.413Z | 2026-09-05T07:11:14.567Z |
| 9 | 03 | deviation | src/probe/ebml_scan.cpp | 348 | x64-windows-static-md CI leg fails to build: 'std::max(1.0, std::abs(value))' hits C2059 syntax error because windows.h's max macro (NOMINMAX not defined anywhere in the project) clobbers std::max. Revealed by 03-14's real CI run (PR #3, run 33951407521); belongs to plan 03-06's ebml_scan, out of 03-14's declared files_modified. | open |  | 2026-09-05T07:11:26.378Z |  |
| 10 | 03 | deviation | tests/fixtures/GENERATOR_MANIFEST.json |  | x64-linux CI leg: 5 of 620 tests fail (unit.inspect_container, ts_scan_golden ts_204/ts_multiprogram/ts_single, integration.size_checks) because committed byte-level goldens were generated against a local ffmpeg master snapshot (N-126086-ge5ecfe8970-20260812) while CI installs ffmpeg 9.0.1 via apt -- different muxer output invalidates byte-identical goldens across ffmpeg builds. Revealed by 03-14's real CI run (PR #3, run 33951407521); the design question (pin ffmpeg version in CI vs regenerate/version-tolerant goldens) is explicitly not 03-14's to decide. | open |  | 2026-09-05T07:11:26.508Z |  |
| 11 | 03 | deviation | .github/workflows/ci.yml |  | arm64-linux (non-blocking leg) CI run: 'Register vcpkg NuGet feed (read-write, trusted runs only)' step exits 1, a credentials/infra problem unrelated to the fixture corpus. Revealed by 03-14's real CI run (PR #3, run 33951407521); non-blocking leg, out of 03-14's scope. | open |  | 2026-09-05T07:11:26.648Z |  |
| 12 | 03 | deviation | scripts/ffmpeg_pin.json |  | The SAME checksum-verified pinned ffmpeg binary produces different fixture bytes on GitHub's x64-linux runner than on a local x86_64 Linux workstation (all 80 corpus_digest.sh hashes differed) -- almost certainly runtime CPU-feature-dispatch (SIMD) differences (the workstation has AVX-512, GH's runner likely does not) affecting floating-point DSP paths inside ffmpeg's encoders even under -flags +bitexact. Goldens must be captured from the actual blocking-leg CI runner (via a temporary CI diagnostic step), not assumed portable from a developer workstation, even when the exact same pinned binary is used. | open |  | 2026-09-05T17:25:14.513Z |  |
| 13 | 03 | deviation | tests/unit/test_ebml_scan.cpp | 89 | arm64-osx/x64-osx CI legs fail to build: 'constexpr std::uint64_t kClusterId' triggers -Werror,-Wunused-const-variable under AppleClang (this file-local constant is genuinely unused in the test body). GCC on the Linux legs does not flag this the same way. Revealed by 03-16's real CI run (33980515543) reaching further into the macOS build than any prior run; belongs to plan 03-06's ebml_scan test file, out of 03-16's declared files_modified. | open |  | 2026-09-05T17:26:17.518Z |  |
| 14 | 03 | deviation | .github/workflows/ci.yml |  | x64-osx (non-blocking, cross-built x86_64 from the arm64-osx host) fails at link: 'ld: symbol(s) not found for architecture arm64' against libmediadiff_core.a's FFmpeg symbols -- a triplet/architecture mismatch in the cross-build, matching research/STACK.md's own documented 'known failure class' for cross-compiling x64-osx from an Apple Silicon runner. Revealed by 03-16's real CI run (33980515543); non-blocking leg, out of 03-16's scope. | open |  | 2026-09-05T17:26:26.638Z |  |

````json
[
  {
    "id": 1,
    "kind": "stub",
    "phase": "02",
    "file": "src/report/json.cpp",
    "line": null,
    "description": "Finding.delta and Finding.evidence render as JSON null unconditionally -- core/model.h's Finding carries neither field; both keys are schema-nullable, populated by a future plan without a schema change",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-15T18:34:50.837Z",
    "resolved_at": null
  },
  {
    "id": 2,
    "kind": "deviation",
    "phase": "03",
    "file": "src/report/model.cpp",
    "line": null,
    "description": "accumulate() previously gated worst_gating on a check's declared severity regardless of finding status; fixed to only gate on Status::warn/fail/error (see 03-02-SUMMARY.md deviations).",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-02T18:26:25.460Z",
    "resolved_at": "2026-09-02T18:26:52.963Z"
  },
  {
    "id": 3,
    "kind": "unrun-verify",
    "phase": "03",
    "file": "src/probe/bmff_scan.cpp",
    "line": null,
    "description": "ASan/UBSAN full-suite run for bmff_scan not performed: no sanitizer CMake preset exists in this repo (03-03-SUMMARY.md's own precedent notes the same gap); acceptance criterion explicitly permits recording this instead of claiming it.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-02T20:13:42.454Z",
    "resolved_at": null
  },
  {
    "id": 4,
    "kind": "unrun-verify",
    "phase": "03",
    "file": "src/analyzers/container/mkv.cpp",
    "line": null,
    "description": "container.mkv.codec_delay's skipped:insufficient_data path (unknown SamplingFrequency) and an explicit CodecDelay=0 value are not exercised by any fixture-level test -- no reasonably-constructible bitexact fixture reliably produces either (an ffmpeg Opus track always carries SamplingFrequency; Opus priming is never exactly zero).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-02T20:53:44.223Z",
    "resolved_at": null
  },
  {
    "id": 5,
    "kind": "deviation",
    "phase": "03",
    "file": "src/probe/ebml_scan.cpp",
    "line": null,
    "description": "03-06 tasks carried tdd=\"true\" but tdd_mode is false for this phase; tests and implementation were developed together (test-first in practice, verified via real fixture failures) rather than following separate RED/GREEN commits.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-02T20:53:44.324Z",
    "resolved_at": null
  },
  {
    "id": 6,
    "kind": "deviation",
    "phase": "02",
    "file": "tests/unit/test_markdown_budget.cpp",
    "line": null,
    "description": "ASan stack-use-after-scope: make_finding() test helper binds Finding::id (string_view) to a temporary std::string built per loop iteration, violating the documented static-storage-duration contract. Test-only, zero production risk (all 4 production Finding.id writers use CheckDef::id). Found during 03-10's sanitizer_note one-off ASan/UBSan build; see .planning/phases/03-probe-layer-container-size/deferred-items.md",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-03T21:45:41.359Z",
    "resolved_at": null
  },
  {
    "id": 7,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/integration/test_doc03_coverage.cpp",
    "line": null,
    "description": "container.ts.psi_interval/pmt_version_churn have no fixture pair that perturbs same-topology PAT/PMT spacing or PMT version directly; their declared DOC-03 trigger pair (ts_single.ts vs ts_multiprogram.ts) fires both via the CONT-08 unpaired-program topology-mismatch path instead, satisfying the gate's own trigger definition but not the intended semantic trigger.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-03T22:38:18.319Z",
    "resolved_at": null
  },
  {
    "id": 8,
    "kind": "deviation",
    "phase": "03",
    "file": ".github/workflows/ci.yml",
    "line": null,
    "description": "scripts/gen_corpus.sh (the Linux/macOS fixture generator) is never invoked anywhere in .github/workflows/ci.yml -- only the Windows-specific gen_corpus.ps1 positive/negative-path check runs. Predates this plan (present since Phase 1); every corpus-dependent integration test would fail on a real CI run for the Linux/macOS/x64-windows(sh) legs until a fixture-generation step is added to the Test step or a preceding step. Discovered while verifying TRUST-06's CI wiring; out of this plan's scope to fix.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-03T22:38:18.413Z",
    "resolved_at": "2026-09-05T07:11:14.567Z"
  },
  {
    "id": 9,
    "kind": "deviation",
    "phase": "03",
    "file": "src/probe/ebml_scan.cpp",
    "line": 348,
    "description": "x64-windows-static-md CI leg fails to build: 'std::max(1.0, std::abs(value))' hits C2059 syntax error because windows.h's max macro (NOMINMAX not defined anywhere in the project) clobbers std::max. Revealed by 03-14's real CI run (PR #3, run 33951407521); belongs to plan 03-06's ebml_scan, out of 03-14's declared files_modified.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T07:11:26.378Z",
    "resolved_at": null
  },
  {
    "id": 10,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/fixtures/GENERATOR_MANIFEST.json",
    "line": null,
    "description": "x64-linux CI leg: 5 of 620 tests fail (unit.inspect_container, ts_scan_golden ts_204/ts_multiprogram/ts_single, integration.size_checks) because committed byte-level goldens were generated against a local ffmpeg master snapshot (N-126086-ge5ecfe8970-20260812) while CI installs ffmpeg 9.0.1 via apt -- different muxer output invalidates byte-identical goldens across ffmpeg builds. Revealed by 03-14's real CI run (PR #3, run 33951407521); the design question (pin ffmpeg version in CI vs regenerate/version-tolerant goldens) is explicitly not 03-14's to decide.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T07:11:26.508Z",
    "resolved_at": null
  },
  {
    "id": 11,
    "kind": "deviation",
    "phase": "03",
    "file": ".github/workflows/ci.yml",
    "line": null,
    "description": "arm64-linux (non-blocking leg) CI run: 'Register vcpkg NuGet feed (read-write, trusted runs only)' step exits 1, a credentials/infra problem unrelated to the fixture corpus. Revealed by 03-14's real CI run (PR #3, run 33951407521); non-blocking leg, out of 03-14's scope.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T07:11:26.648Z",
    "resolved_at": null
  },
  {
    "id": 12,
    "kind": "deviation",
    "phase": "03",
    "file": "scripts/ffmpeg_pin.json",
    "line": null,
    "description": "The SAME checksum-verified pinned ffmpeg binary produces different fixture bytes on GitHub's x64-linux runner than on a local x86_64 Linux workstation (all 80 corpus_digest.sh hashes differed) -- almost certainly runtime CPU-feature-dispatch (SIMD) differences (the workstation has AVX-512, GH's runner likely does not) affecting floating-point DSP paths inside ffmpeg's encoders even under -flags +bitexact. Goldens must be captured from the actual blocking-leg CI runner (via a temporary CI diagnostic step), not assumed portable from a developer workstation, even when the exact same pinned binary is used.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T17:25:14.513Z",
    "resolved_at": null
  },
  {
    "id": 13,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/unit/test_ebml_scan.cpp",
    "line": 89,
    "description": "arm64-osx/x64-osx CI legs fail to build: 'constexpr std::uint64_t kClusterId' triggers -Werror,-Wunused-const-variable under AppleClang (this file-local constant is genuinely unused in the test body). GCC on the Linux legs does not flag this the same way. Revealed by 03-16's real CI run (33980515543) reaching further into the macOS build than any prior run; belongs to plan 03-06's ebml_scan test file, out of 03-16's declared files_modified.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T17:26:17.518Z",
    "resolved_at": null
  },
  {
    "id": 14,
    "kind": "deviation",
    "phase": "03",
    "file": ".github/workflows/ci.yml",
    "line": null,
    "description": "x64-osx (non-blocking, cross-built x86_64 from the arm64-osx host) fails at link: 'ld: symbol(s) not found for architecture arm64' against libmediadiff_core.a's FFmpeg symbols -- a triplet/architecture mismatch in the cross-build, matching research/STACK.md's own documented 'known failure class' for cross-compiling x64-osx from an Apple Silicon runner. Revealed by 03-16's real CI run (33980515543); non-blocking leg, out of 03-16's scope.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T17:26:26.638Z",
    "resolved_at": null
  }
]
````
