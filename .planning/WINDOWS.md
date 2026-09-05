---
schema_version: 1
open_count: 12
waived_count: 0
fixed_count: 5
total_count: 17
last_updated: 2026-09-05T20:35:40.346Z
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
| 9 | 03 | deviation | src/probe/ebml_scan.cpp | 348 | x64-windows-static-md CI leg fails to build: 'std::max(1.0, std::abs(value))' hits C2059 syntax error because windows.h's max macro (NOMINMAX not defined anywhere in the project) clobbers std::max. Revealed by 03-14's real CI run (PR #3, run 33951407521); belongs to plan 03-06's ebml_scan, out of 03-14's declared files_modified. | fixed |  | 2026-09-05T07:11:26.378Z | 2026-09-05T20:35:40.346Z |
| 10 | 03 | deviation | tests/fixtures/GENERATOR_MANIFEST.json |  | x64-linux CI leg: 5 of 620 tests fail (unit.inspect_container, ts_scan_golden ts_204/ts_multiprogram/ts_single, integration.size_checks) because committed byte-level goldens were generated against a local ffmpeg master snapshot (N-126086-ge5ecfe8970-20260812) while CI's apt-installed ffmpeg was actually 6.1.1-3ubuntu5 (Ubuntu 24.04's packaged version, confirmed from this leg's own ffmpeg -version output in run 33951407521, not the 9.0.1 this entry originally and incorrectly claimed) -- a two-major-version gap, not the patch drift first recorded here; scripts/gen_corpus.sh's 6.1 floor admits that build, so the version gate passes while different muxer bytes come out. Revealed by 03-14's real CI run (PR #3, run 33951407521); resolved by 03-16 pinning fixture-synthesis ffmpeg by URL+SHA-256 and re-baselining the goldens against the pinned build's real x64-linux CI output; this entry's own ffmpeg-version claim corrected by 03-20 on real CI run 33990099158. | fixed |  | 2026-09-05T07:11:26.508Z | 2026-09-05T17:37:11.854Z |
| 11 | 03 | deviation | .github/workflows/ci.yml |  | arm64-linux (non-blocking leg) CI run: 'Register vcpkg NuGet feed (read-write, trusted runs only)' step exits 1, a credentials/infra problem unrelated to the fixture corpus. Revealed by 03-14's real CI run (PR #3, run 33951407521); non-blocking leg, out of 03-14's scope. | open |  | 2026-09-05T07:11:26.648Z |  |
| 12 | 03 | deviation | scripts/ffmpeg_pin.json |  | The SAME checksum-verified pinned ffmpeg binary produces different fixture bytes on GitHub's x64-linux runner than on a local x86_64 Linux workstation (all 80 corpus_digest.sh hashes differed) -- almost certainly runtime CPU-feature-dispatch (SIMD) differences (the workstation has AVX-512, GH's runner likely does not) affecting floating-point DSP paths inside ffmpeg's encoders even under -flags +bitexact. Goldens must be captured from the actual blocking-leg CI runner (via a temporary CI diagnostic step), not assumed portable from a developer workstation, even when the exact same pinned binary is used. | open |  | 2026-09-05T17:25:14.513Z |  |
| 13 | 03 | deviation | tests/unit/test_ebml_scan.cpp | 89 | arm64-osx/x64-osx CI legs fail to build: 'constexpr std::uint64_t kClusterId' triggers -Werror,-Wunused-const-variable under AppleClang (this file-local constant is genuinely unused in the test body). GCC on the Linux legs does not flag this the same way. Revealed by 03-16's real CI run (33980515543) reaching further into the macOS build than any prior run; belongs to plan 03-06's ebml_scan test file, out of 03-16's declared files_modified. | open |  | 2026-09-05T17:26:17.518Z |  |
| 14 | 03 | deviation | .github/workflows/ci.yml |  | x64-osx (non-blocking, cross-built x86_64 from the arm64-osx host) fails at link: 'ld: symbol(s) not found for architecture arm64' against libmediadiff_core.a's FFmpeg symbols -- a triplet/architecture mismatch in the cross-build, matching research/STACK.md's own documented 'known failure class' for cross-compiling x64-osx from an Apple Silicon runner. Revealed by 03-16's real CI run (33980515543); non-blocking leg, out of 03-16's scope. | open |  | 2026-09-05T17:26:26.638Z |  |
| 15 | 03 | deviation | scripts/check_corpus.sh |  | Blocking arm64-osx CI leg failed at 'Verify the fixture corpus is complete' with exit 127 because check_corpus.sh used a bash-4-only array-reading builtin (mapfile) that macOS's system bash 3.2 does not provide, aborting before Configure/Build/Test ever ran. Fixed in commit 91d9d2f (03-14, which switched to a while-read loop) -- but that fix had never been exercised by any real CI run at the time it was recorded; 03-18 added the permanent bash-4-builtin lint guard, and 03-20 observed the runtime proof on real CI run 33990099158 (head 1b684de): arm64-osx's 'Generate media fixture corpus (BUILD-08 / D-08)' and 'Verify the fixture corpus is complete' steps both concluded success, printing 'check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty'. | fixed |  | 2026-09-05T20:35:10.410Z | 2026-09-05T20:35:40.208Z |
| 16 | 03 | deviation | src/cli/main.cpp | 297 | Blocking x64-windows-static-md CI leg fails at the Build step (all other blocking-leg build defects it previously failed on are now fixed): src/cli/main.cpp(297): error C3861: 'report_cli_error': identifier not found. report_cli_error is declared in namespace mediadiff (src/cli/diagnostics.h:43); main.cpp closes that namespace at line 209, and wmain (lines 222-312) calls it unqualified. Line 292 immediately above correctly writes mediadiff::wide_to_utf8(...); line 297 simply omits the qualification. The whole block is inside #ifdef _WIN32, so GCC/Clang on the other legs never compile it -- it only became reachable once 03-17 fixed the earlier C2059/NOMINMAX error that used to abort the Windows build first. Fix is a one-token change to mediadiff::report_cli_error(...). Discovered by 03-20 on real CI run 33990099158 (head 1b684de); out of 03-20's declared files_modified (WINDOWS.md, 03-VERIFICATION.md only) -- recorded, not fixed. | open |  | 2026-09-05T20:35:20.791Z |  |
| 17 | 03 | deviation | .github/workflows/ci.yml |  | 03-19 chose the 'designated' D-GAP-01 corpus-identity policy (not 'uniform') after measuring that the pinned ffmpeg builds do NOT produce byte-identical fixtures across CI legs (arm64-osx diverges from x64-linux/x64-windows-static-md on 76 of 80 fixtures, real run 33983460934). As a result 5 byte-exact fixture-derived golden tests -- unit.inspect_container - golden:, unit.ts_scan_golden (ts_204/ts_multiprogram/ts_single), and integration.size_checks - the size.* findings are pinned -- run ONLY on the designated leg (x64-linux); they are excluded by name on every other leg (arm64-osx, x64-osx, x64-windows-static-md, arm64-linux) via a CTest -E regex, with EXPECTED_EXCLUDED_COUNT=5 asserted against unfiltered-vs-filtered ctest -N totals so the exclusion cannot silently widen. Every non-designated leg's log announces the exclusion by name and reason (never silent). This is an accepted, deliberate narrowing of test COVERAGE (not of assertion strength -- the byte-exact assertions themselves stay byte-exact on the designated leg) that the ledger should keep visible for future rounds. Left open: a future ffmpeg-pin bump under this policy must regenerate tests/golden/CORPUS_DIGEST.txt from the designated leg's real CI output in the same commit as the pin change (03-19-SUMMARY.md's own Next Phase Readiness note). | open |  | 2026-09-05T20:35:33.156Z |  |

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
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-05T07:11:26.378Z",
    "resolved_at": "2026-09-05T20:35:40.346Z"
  },
  {
    "id": 10,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/fixtures/GENERATOR_MANIFEST.json",
    "line": null,
    "description": "x64-linux CI leg: 5 of 620 tests fail (unit.inspect_container, ts_scan_golden ts_204/ts_multiprogram/ts_single, integration.size_checks) because committed byte-level goldens were generated against a local ffmpeg master snapshot (N-126086-ge5ecfe8970-20260812) while CI's apt-installed ffmpeg was actually 6.1.1-3ubuntu5 (Ubuntu 24.04's packaged version, confirmed from this leg's own ffmpeg -version output in run 33951407521, not the 9.0.1 this entry originally and incorrectly claimed) -- a two-major-version gap, not the patch drift first recorded here; scripts/gen_corpus.sh's 6.1 floor admits that build, so the version gate passes while different muxer bytes come out. Revealed by 03-14's real CI run (PR #3, run 33951407521); resolved by 03-16 pinning fixture-synthesis ffmpeg by URL+SHA-256 and re-baselining the goldens against the pinned build's real x64-linux CI output; this entry's own ffmpeg-version claim corrected by 03-20 on real CI run 33990099158.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-05T07:11:26.508Z",
    "resolved_at": "2026-09-05T17:37:11.854Z"
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
  },
  {
    "id": 15,
    "kind": "deviation",
    "phase": "03",
    "file": "scripts/check_corpus.sh",
    "line": null,
    "description": "Blocking arm64-osx CI leg failed at 'Verify the fixture corpus is complete' with exit 127 because check_corpus.sh used a bash-4-only array-reading builtin (mapfile) that macOS's system bash 3.2 does not provide, aborting before Configure/Build/Test ever ran. Fixed in commit 91d9d2f (03-14, which switched to a while-read loop) -- but that fix had never been exercised by any real CI run at the time it was recorded; 03-18 added the permanent bash-4-builtin lint guard, and 03-20 observed the runtime proof on real CI run 33990099158 (head 1b684de): arm64-osx's 'Generate media fixture corpus (BUILD-08 / D-08)' and 'Verify the fixture corpus is complete' steps both concluded success, printing 'check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty'.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-05T20:35:10.410Z",
    "resolved_at": "2026-09-05T20:35:40.208Z"
  },
  {
    "id": 16,
    "kind": "deviation",
    "phase": "03",
    "file": "src/cli/main.cpp",
    "line": 297,
    "description": "Blocking x64-windows-static-md CI leg fails at the Build step (all other blocking-leg build defects it previously failed on are now fixed): src/cli/main.cpp(297): error C3861: 'report_cli_error': identifier not found. report_cli_error is declared in namespace mediadiff (src/cli/diagnostics.h:43); main.cpp closes that namespace at line 209, and wmain (lines 222-312) calls it unqualified. Line 292 immediately above correctly writes mediadiff::wide_to_utf8(...); line 297 simply omits the qualification. The whole block is inside #ifdef _WIN32, so GCC/Clang on the other legs never compile it -- it only became reachable once 03-17 fixed the earlier C2059/NOMINMAX error that used to abort the Windows build first. Fix is a one-token change to mediadiff::report_cli_error(...). Discovered by 03-20 on real CI run 33990099158 (head 1b684de); out of 03-20's declared files_modified (WINDOWS.md, 03-VERIFICATION.md only) -- recorded, not fixed.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T20:35:20.791Z",
    "resolved_at": null
  },
  {
    "id": 17,
    "kind": "deviation",
    "phase": "03",
    "file": ".github/workflows/ci.yml",
    "line": null,
    "description": "03-19 chose the 'designated' D-GAP-01 corpus-identity policy (not 'uniform') after measuring that the pinned ffmpeg builds do NOT produce byte-identical fixtures across CI legs (arm64-osx diverges from x64-linux/x64-windows-static-md on 76 of 80 fixtures, real run 33983460934). As a result 5 byte-exact fixture-derived golden tests -- unit.inspect_container - golden:, unit.ts_scan_golden (ts_204/ts_multiprogram/ts_single), and integration.size_checks - the size.* findings are pinned -- run ONLY on the designated leg (x64-linux); they are excluded by name on every other leg (arm64-osx, x64-osx, x64-windows-static-md, arm64-linux) via a CTest -E regex, with EXPECTED_EXCLUDED_COUNT=5 asserted against unfiltered-vs-filtered ctest -N totals so the exclusion cannot silently widen. Every non-designated leg's log announces the exclusion by name and reason (never silent). This is an accepted, deliberate narrowing of test COVERAGE (not of assertion strength -- the byte-exact assertions themselves stay byte-exact on the designated leg) that the ledger should keep visible for future rounds. Left open: a future ffmpeg-pin bump under this policy must regenerate tests/golden/CORPUS_DIGEST.txt from the designated leg's real CI output in the same commit as the pin change (03-19-SUMMARY.md's own Next Phase Readiness note).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-05T20:35:33.156Z",
    "resolved_at": null
  }
]
````
