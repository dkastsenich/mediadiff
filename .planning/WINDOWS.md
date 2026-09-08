---
schema_version: 1
open_count: 11
waived_count: 1
fixed_count: 12
total_count: 24
last_updated: 2026-09-08T15:45:52.844Z
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
| 13 | 03 | deviation | tests/unit/test_ebml_scan.cpp | 89 | arm64-osx/x64-osx CI legs fail to build: 'constexpr std::uint64_t kClusterId' triggers -Werror,-Wunused-const-variable under AppleClang (this file-local constant is genuinely unused in the test body). GCC on the Linux legs does not flag this the same way. Revealed by 03-16's real CI run (33980515543) reaching further into the macOS build than any prior run; belongs to plan 03-06's ebml_scan test file, out of 03-16's declared files_modified. | fixed |  | 2026-09-05T17:26:17.518Z | 2026-09-06T08:33:47.354Z |
| 14 | 03 | deviation | .github/workflows/ci.yml |  | x64-osx (non-blocking, cross-built x86_64 from the arm64-osx host) fails at link: 'ld: symbol(s) not found for architecture arm64' against libmediadiff_core.a's FFmpeg symbols -- a triplet/architecture mismatch in the cross-build, matching research/STACK.md's own documented 'known failure class' for cross-compiling x64-osx from an Apple Silicon runner. Revealed by 03-16's real CI run (33980515543); non-blocking leg, out of 03-16's scope. | open |  | 2026-09-05T17:26:26.638Z |  |
| 15 | 03 | deviation | scripts/check_corpus.sh |  | Blocking arm64-osx CI leg failed at 'Verify the fixture corpus is complete' with exit 127 because check_corpus.sh used a bash-4-only array-reading builtin (mapfile) that macOS's system bash 3.2 does not provide, aborting before Configure/Build/Test ever ran. Fixed in commit 91d9d2f (03-14, which switched to a while-read loop) -- but that fix had never been exercised by any real CI run at the time it was recorded; 03-18 added the permanent bash-4-builtin lint guard, and 03-20 observed the runtime proof on real CI run 33990099158 (head 1b684de): arm64-osx's 'Generate media fixture corpus (BUILD-08 / D-08)' and 'Verify the fixture corpus is complete' steps both concluded success, printing 'check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty'. | fixed |  | 2026-09-05T20:35:10.410Z | 2026-09-05T20:35:40.208Z |
| 16 | 03 | deviation | src/cli/main.cpp | 297 | Blocking x64-windows-static-md CI leg fails at the Build step (all other blocking-leg build defects it previously failed on are now fixed): src/cli/main.cpp(297): error C3861: 'report_cli_error': identifier not found. report_cli_error is declared in namespace mediadiff (src/cli/diagnostics.h:43); main.cpp closes that namespace at line 209, and wmain (lines 222-312) calls it unqualified. Line 292 immediately above correctly writes mediadiff::wide_to_utf8(...); line 297 simply omits the qualification. The whole block is inside #ifdef _WIN32, so GCC/Clang on the other legs never compile it -- it only became reachable once 03-17 fixed the earlier C2059/NOMINMAX error that used to abort the Windows build first. Fix is a one-token change to mediadiff::report_cli_error(...). Discovered by 03-20 on real CI run 33990099158 (head 1b684de); out of 03-20's declared files_modified (WINDOWS.md, 03-VERIFICATION.md only) -- recorded, not fixed. | fixed |  | 2026-09-05T20:35:20.791Z | 2026-09-06T08:33:47.499Z |
| 17 | 03 | deviation | .github/workflows/ci.yml |  | 03-19 chose the 'designated' D-GAP-01 corpus-identity policy (not 'uniform') after measuring that the pinned ffmpeg builds do NOT produce byte-identical fixtures across CI legs (arm64-osx diverges from x64-linux/x64-windows-static-md on 76 of 80 fixtures, real run 33983460934). As a result 5 byte-exact fixture-derived golden tests -- unit.inspect_container - golden:, unit.ts_scan_golden (ts_204/ts_multiprogram/ts_single), and integration.size_checks - the size.* findings are pinned -- run ONLY on the designated leg (x64-linux); they are excluded by name on every other leg (arm64-osx, x64-osx, x64-windows-static-md, arm64-linux) via a CTest -E regex, with EXPECTED_EXCLUDED_COUNT=5 asserted against unfiltered-vs-filtered ctest -N totals so the exclusion cannot silently widen. Every non-designated leg's log announces the exclusion by name and reason (never silent). This is an accepted, deliberate narrowing of test COVERAGE (not of assertion strength -- the byte-exact assertions themselves stay byte-exact on the designated leg) that the ledger should keep visible for future rounds. Left open: a future ffmpeg-pin bump under this policy must regenerate tests/golden/CORPUS_DIGEST.txt from the designated leg's real CI output in the same commit as the pin change (03-19-SUMMARY.md's own Next Phase Readiness note). | open |  | 2026-09-05T20:35:33.156Z |  |
| 18 | 03 | deviation | tests/unit/CMakeLists.txt |  | Blocking arm64-osx CI leg fails to link tests/unit/mediadiff_unit_tests: undefined symbol mediadiff::render_provenance_chain(std::span<const PolicyProvenance>, int), referenced from test_inspect_container_section.cpp.o. src/cli/provenance_render.cpp (which defines it) is only compiled into the mediadiff executable target, never into the unit-test target, even though src/cli/commands/inspect_render.h's inline render_inspect_text() calls it under verbose=true. Every call site in test_inspect_container_section.cpp happens to pass a literal verbose=false, which lets GCC's inliner constant-fold the branch away and never reference the symbol on x64-linux -- AppleClang's arm64-osx leg does not perform the same fold and fails at link. Revealed by 03-21 on real CI run 34020446940 (head 501c0dd), once the two blocking-leg one-line defects it fixed stopped masking this one; fixed same-round by adding src/cli/provenance_render.cpp to tests/unit/CMakeLists.txt's source list, matching the established color_policy.cpp/tty_render.cpp/dir_pairing.cpp/worker_pool.cpp pattern. | fixed |  | 2026-09-06T08:18:22.996Z | 2026-09-06T08:33:05.116Z |
| 19 | 03 | deviation | tests/integration/test_container_ts.cpp | 140 | Blocking x64-windows-static-md CI leg fails to build: tests/integration/test_container_ts.cpp(140): error C2513: 'mediadiff::test::ProcessResult': no variable declared before '=', followed by cascading syntax errors on the next two lines. Root cause: a local variable is named 'far', which the Windows SDK's <windows.h> (transitively included by tests/process_spawn.h's _WIN32 CreateProcess path) defines as an EMPTY legacy 16-bit-compatibility macro (alongside 'near'/'pascal'), silently erasing the identifier and corrupting the declaration -- MSVC-only, since no other leg's toolchain defines any such macro. Revealed by 03-21 on real CI run 34020446940 (head 501c0dd), once the two blocking-leg one-line defects it fixed let the build reach this file for the first time; fixed same-round by renaming the variable to pcr_far. | fixed |  | 2026-09-06T08:18:23.142Z | 2026-09-06T08:33:05.258Z |
| 20 | 03 | deviation | scripts/gen_corpus.sh |  | Blocking arm64-osx CI leg's Test step reaches completion for the first time (build (arm64-osx) run 34021508083, head dfc9e8d) but reports 1 test failure: integration.doc03_coverage's declared CLEAN pair for size.stream_bitrate (size_near_a.mp4 vs size_near_b.mp4, -b:v 700k vs 730k) does not compare all-pass under --profile sw-encoder. Root cause: this fixture pair was calibrated on a single dev machine to a ~2.7% measured video-stream bitrate delta, only ~0.3 percentage points below the check's 3% warn threshold (tolerance = 3%,10% in src/core/checks.def) -- confirmed by an independent local re-measurement this round (video stream 279942 vs 287983 bytes, delta 2.87%). The pinned ffmpeg build's mpeg4 encoder produces measurably different output by CPU architecture even under -flags +bitexact -fflags +bitexact on the SAME binary (WINDOWS.md #12's already-documented SIMD-dispatch class), and on arm64-osx that variance is enough to tip the delta from pass into warn. Not fixed this round: correcting it requires regenerating size_near_a.mp4/size_near_b.mp4 with a wider safety margin (e.g. 700k/715k, locally re-measured at ~1.1%) AND regenerating tests/golden/CORPUS_DIGEST.txt's committed digest for the designated leg (x64-linux) from that leg's real CI output (D-GAP-01's own established procedure, 03-16/03-19 precedent) -- a byte-changing fix that needs its own dedicated CI round trip to capture the correct digest, out of this plan's remaining round-trip budget. This defect does not block 03-21's own must_haves.truths (which require blocking legs to reach Build success and their Test step, not a 100%-passing Test step) but must be fixed before SC5 can be considered fully closed. | fixed |  | 2026-09-06T08:32:50.473Z | 2026-09-06T09:19:16.163Z |
| 21 | 03 | deviation | scripts/install_pinned_ffmpeg.sh | 334 | Blocking x64-windows-static-md CI leg's job-level conclusion is failure even though Build and Test both conclude success: the 'PowerShell corpus generator version-gate and manifest-order cross-check' step (ci.yml, first reachable now that 03-21 fixed the two earlier Build defects) fails with 'gen_corpus requires a system ffmpeg >= 6.1 on PATH ... ffmpeg was not found', because install_pinned_ffmpeg.sh appends dirname($REAL_CANDIDATE_PATH) -- an MSYS/Git-Bash POSIX-style path such as /d/a/mediadiff/.../bin -- to GITHUB_PATH; this resolves fine for later bash-invoked steps (gen_corpus.sh/check_corpus.sh/corpus_digest.sh, which is why Build/Test both succeed) but is not a valid Windows path for the pwsh child process this step spawns, so Get-Command/& ffmpeg cannot find it. Confirmed present verbatim in both real CI run 34021508083 (head dfc9e8d) and run 34022461121 (head 6b57c2a); never previously observed because Build/Test never both succeeded on this leg before 03-21, so this later step was never reached. | fixed |  | 2026-09-06T08:51:51.713Z | 2026-09-06T09:19:24.340Z |
| 22 | 03 | deviation | scripts/gen_corpus.sh | 483 | The designated leg's (x64-linux) own fixture generation is not reproducible run-to-run on the SAME commit with the SAME pinned ffmpeg binary: real CI run 34021508083 (head dfc9e8d) passed 'Assert the corpus digest matches the committed pin (D-GAP-01)' cleanly, but real CI run 34022461121 (head 6b57c2a, a docs-only diff from dfc9e8d -- confirmed via git diff --name-only, no source changed) failed that same step on the SAME leg with a byte-level mismatch confined to mkv_opus_a.webm and mkv_opus_b.webm (CORPUS_DIGEST_SUMMARY differed: d351f426... vs 7dff4882...; every other one of the 80 fixtures matched byte-for-byte both times). Both fixtures are produced by gen_corpus.sh's libopus/-application-lowdelay recipes (lines 483-491) under -flags +bitexact -fflags +bitexact. Root cause not yet isolated -- most likely candidate is the same CPU-feature-dispatch (SIMD) class already documented at WINDOWS.md #12, but manifesting WITHIN one architecture-labeled leg across separate GitHub-hosted-runner invocations (the ubuntu-latest label is not a fixed physical host) rather than only across differing architectures. This threatens the evidentiary basis of the entire 'designated leg' byte-exact policy (WINDOWS.md #17): a policy that assumes x64-linux is internally reproducible is not established by this observation. Not fixed this round -- diagnosing and stabilizing libopus's encode determinism (or narrowing the byte-exact assertion to exclude Opus-encoded fixtures) is out of this plan's declared scope (WINDOWS.md, REQUIREMENTS.md only) and needs its own dedicated investigation. | waived | The underlying libopus cross-host nondeterminism is NOT fixed and is not fixable here -- -flags +bitexact is an ffmpeg-level flag that does not reach inside a third-party encoder. Supporting evidence this is cross-host rather than intra-host: five repeats of each recipe on one fixed host produced one distinct SHA-256 per recipe, and ffmpeg -h encoder=libopus reports no threading capability. What changed: scripts/assert_corpus_digest.sh excludes the mkv_opus_a.webm line, the mkv_opus_b.webm line and the derived CORPUS_DIGEST_SUMMARY line from byte-exact comparison and asserts exactly 3 excluded lines on both sides, so the remaining 78 stay strictly gated. What did not change: both fixtures are still generated, still in the corpus, still used by their tests on every leg, and still hashed into the run log. | 2026-09-06T08:52:04.751Z | 2026-09-08T15:45:45.970Z |
| 23 | 03 | deviation | scripts/install_pinned_ffmpeg.sh | 206 | CR-01 (03-REVIEW.md gap-closure round 3): the tar.xz path-traversal guard added in round 3 rejected symlink/hardlink members with an absolute linkname but not a relative linkname that resolves outside dest_dir -- because the pre-extraction scan runs over getmembers() before any member exists on disk, os.path.realpath on the raw (member_dir, linkname) join at scan time only lexically normalizes and cannot detect an escape that only materializes once the symlink is actually created by extractall(). Reproduced end-to-end (a relinkdir->../../../../../../tmp symlink member + a nested file member routed through it wrote outside dest_dir and the guard reported no error) and fixed same round by resolving each link member's target against its own containing directory inside dest_dir and rejecting if that resolved target escapes dest_dir, in addition to the existing absolute-path check; the header comment's overstated 'refused outright' claim was also corrected to describe only what the code guarantees (member-path validation, not a general defense against every TOCTOU race tar extraction can exhibit). Reachability caveat unchanged from round 3: every scripts/ffmpeg_pin.json entry uses archive:zip today, so this arm is still dead code, and reaching it also requires controlling a pin entry's URL+SHA-256 pair. | fixed |  | 2026-09-06T11:37:37.532Z | 2026-09-06T11:37:43.657Z |
| 24 | 03 | deviation | tests/golden/CORPUS_DIGEST.txt |  | mkv_opus_a.webm and mkv_opus_b.webm now have no byte-level drift detection on any leg -- a silent byte change to either (a gen_corpus.sh recipe edit, an ffmpeg pin bump that alters Opus output) would not fail CI. The tests which still consume them (test_container_mkv.cpp, test_ebml_scan.cpp, test_doc03_coverage.cpp) assert structure and findings rather than bytes, not bytes. Named alternative: hash the parsed EBML structure (element IDs, sizes, ordering, CodecDelay and SeekPreRoll values) instead of file bytes -- this would restore drift detection without depending on encoder byte-stability. | open |  | 2026-09-08T15:45:52.844Z |  |

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
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-05T17:26:17.518Z",
    "resolved_at": "2026-09-06T08:33:47.354Z"
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
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-05T20:35:20.791Z",
    "resolved_at": "2026-09-06T08:33:47.499Z"
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
  },
  {
    "id": 18,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/unit/CMakeLists.txt",
    "line": null,
    "description": "Blocking arm64-osx CI leg fails to link tests/unit/mediadiff_unit_tests: undefined symbol mediadiff::render_provenance_chain(std::span<const PolicyProvenance>, int), referenced from test_inspect_container_section.cpp.o. src/cli/provenance_render.cpp (which defines it) is only compiled into the mediadiff executable target, never into the unit-test target, even though src/cli/commands/inspect_render.h's inline render_inspect_text() calls it under verbose=true. Every call site in test_inspect_container_section.cpp happens to pass a literal verbose=false, which lets GCC's inliner constant-fold the branch away and never reference the symbol on x64-linux -- AppleClang's arm64-osx leg does not perform the same fold and fails at link. Revealed by 03-21 on real CI run 34020446940 (head 501c0dd), once the two blocking-leg one-line defects it fixed stopped masking this one; fixed same-round by adding src/cli/provenance_render.cpp to tests/unit/CMakeLists.txt's source list, matching the established color_policy.cpp/tty_render.cpp/dir_pairing.cpp/worker_pool.cpp pattern.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-06T08:18:22.996Z",
    "resolved_at": "2026-09-06T08:33:05.116Z"
  },
  {
    "id": 19,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/integration/test_container_ts.cpp",
    "line": 140,
    "description": "Blocking x64-windows-static-md CI leg fails to build: tests/integration/test_container_ts.cpp(140): error C2513: 'mediadiff::test::ProcessResult': no variable declared before '=', followed by cascading syntax errors on the next two lines. Root cause: a local variable is named 'far', which the Windows SDK's <windows.h> (transitively included by tests/process_spawn.h's _WIN32 CreateProcess path) defines as an EMPTY legacy 16-bit-compatibility macro (alongside 'near'/'pascal'), silently erasing the identifier and corrupting the declaration -- MSVC-only, since no other leg's toolchain defines any such macro. Revealed by 03-21 on real CI run 34020446940 (head 501c0dd), once the two blocking-leg one-line defects it fixed let the build reach this file for the first time; fixed same-round by renaming the variable to pcr_far.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-06T08:18:23.142Z",
    "resolved_at": "2026-09-06T08:33:05.258Z"
  },
  {
    "id": 20,
    "kind": "deviation",
    "phase": "03",
    "file": "scripts/gen_corpus.sh",
    "line": null,
    "description": "Blocking arm64-osx CI leg's Test step reaches completion for the first time (build (arm64-osx) run 34021508083, head dfc9e8d) but reports 1 test failure: integration.doc03_coverage's declared CLEAN pair for size.stream_bitrate (size_near_a.mp4 vs size_near_b.mp4, -b:v 700k vs 730k) does not compare all-pass under --profile sw-encoder. Root cause: this fixture pair was calibrated on a single dev machine to a ~2.7% measured video-stream bitrate delta, only ~0.3 percentage points below the check's 3% warn threshold (tolerance = 3%,10% in src/core/checks.def) -- confirmed by an independent local re-measurement this round (video stream 279942 vs 287983 bytes, delta 2.87%). The pinned ffmpeg build's mpeg4 encoder produces measurably different output by CPU architecture even under -flags +bitexact -fflags +bitexact on the SAME binary (WINDOWS.md #12's already-documented SIMD-dispatch class), and on arm64-osx that variance is enough to tip the delta from pass into warn. Not fixed this round: correcting it requires regenerating size_near_a.mp4/size_near_b.mp4 with a wider safety margin (e.g. 700k/715k, locally re-measured at ~1.1%) AND regenerating tests/golden/CORPUS_DIGEST.txt's committed digest for the designated leg (x64-linux) from that leg's real CI output (D-GAP-01's own established procedure, 03-16/03-19 precedent) -- a byte-changing fix that needs its own dedicated CI round trip to capture the correct digest, out of this plan's remaining round-trip budget. This defect does not block 03-21's own must_haves.truths (which require blocking legs to reach Build success and their Test step, not a 100%-passing Test step) but must be fixed before SC5 can be considered fully closed.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-06T08:32:50.473Z",
    "resolved_at": "2026-09-06T09:19:16.163Z"
  },
  {
    "id": 21,
    "kind": "deviation",
    "phase": "03",
    "file": "scripts/install_pinned_ffmpeg.sh",
    "line": 334,
    "description": "Blocking x64-windows-static-md CI leg's job-level conclusion is failure even though Build and Test both conclude success: the 'PowerShell corpus generator version-gate and manifest-order cross-check' step (ci.yml, first reachable now that 03-21 fixed the two earlier Build defects) fails with 'gen_corpus requires a system ffmpeg >= 6.1 on PATH ... ffmpeg was not found', because install_pinned_ffmpeg.sh appends dirname($REAL_CANDIDATE_PATH) -- an MSYS/Git-Bash POSIX-style path such as /d/a/mediadiff/.../bin -- to GITHUB_PATH; this resolves fine for later bash-invoked steps (gen_corpus.sh/check_corpus.sh/corpus_digest.sh, which is why Build/Test both succeed) but is not a valid Windows path for the pwsh child process this step spawns, so Get-Command/& ffmpeg cannot find it. Confirmed present verbatim in both real CI run 34021508083 (head dfc9e8d) and run 34022461121 (head 6b57c2a); never previously observed because Build/Test never both succeeded on this leg before 03-21, so this later step was never reached.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-06T08:51:51.713Z",
    "resolved_at": "2026-09-06T09:19:24.340Z"
  },
  {
    "id": 22,
    "kind": "deviation",
    "phase": "03",
    "file": "scripts/gen_corpus.sh",
    "line": 483,
    "description": "The designated leg's (x64-linux) own fixture generation is not reproducible run-to-run on the SAME commit with the SAME pinned ffmpeg binary: real CI run 34021508083 (head dfc9e8d) passed 'Assert the corpus digest matches the committed pin (D-GAP-01)' cleanly, but real CI run 34022461121 (head 6b57c2a, a docs-only diff from dfc9e8d -- confirmed via git diff --name-only, no source changed) failed that same step on the SAME leg with a byte-level mismatch confined to mkv_opus_a.webm and mkv_opus_b.webm (CORPUS_DIGEST_SUMMARY differed: d351f426... vs 7dff4882...; every other one of the 80 fixtures matched byte-for-byte both times). Both fixtures are produced by gen_corpus.sh's libopus/-application-lowdelay recipes (lines 483-491) under -flags +bitexact -fflags +bitexact. Root cause not yet isolated -- most likely candidate is the same CPU-feature-dispatch (SIMD) class already documented at WINDOWS.md #12, but manifesting WITHIN one architecture-labeled leg across separate GitHub-hosted-runner invocations (the ubuntu-latest label is not a fixed physical host) rather than only across differing architectures. This threatens the evidentiary basis of the entire 'designated leg' byte-exact policy (WINDOWS.md #17): a policy that assumes x64-linux is internally reproducible is not established by this observation. Not fixed this round -- diagnosing and stabilizing libopus's encode determinism (or narrowing the byte-exact assertion to exclude Opus-encoded fixtures) is out of this plan's declared scope (WINDOWS.md, REQUIREMENTS.md only) and needs its own dedicated investigation.",
    "status": "waived",
    "reason": "The underlying libopus cross-host nondeterminism is NOT fixed and is not fixable here -- -flags +bitexact is an ffmpeg-level flag that does not reach inside a third-party encoder. Supporting evidence this is cross-host rather than intra-host: five repeats of each recipe on one fixed host produced one distinct SHA-256 per recipe, and ffmpeg -h encoder=libopus reports no threading capability. What changed: scripts/assert_corpus_digest.sh excludes the mkv_opus_a.webm line, the mkv_opus_b.webm line and the derived CORPUS_DIGEST_SUMMARY line from byte-exact comparison and asserts exactly 3 excluded lines on both sides, so the remaining 78 stay strictly gated. What did not change: both fixtures are still generated, still in the corpus, still used by their tests on every leg, and still hashed into the run log.",
    "recorded_at": "2026-09-06T08:52:04.751Z",
    "resolved_at": "2026-09-08T15:45:45.970Z"
  },
  {
    "id": 23,
    "kind": "deviation",
    "phase": "03",
    "file": "scripts/install_pinned_ffmpeg.sh",
    "line": 206,
    "description": "CR-01 (03-REVIEW.md gap-closure round 3): the tar.xz path-traversal guard added in round 3 rejected symlink/hardlink members with an absolute linkname but not a relative linkname that resolves outside dest_dir -- because the pre-extraction scan runs over getmembers() before any member exists on disk, os.path.realpath on the raw (member_dir, linkname) join at scan time only lexically normalizes and cannot detect an escape that only materializes once the symlink is actually created by extractall(). Reproduced end-to-end (a relinkdir->../../../../../../tmp symlink member + a nested file member routed through it wrote outside dest_dir and the guard reported no error) and fixed same round by resolving each link member's target against its own containing directory inside dest_dir and rejecting if that resolved target escapes dest_dir, in addition to the existing absolute-path check; the header comment's overstated 'refused outright' claim was also corrected to describe only what the code guarantees (member-path validation, not a general defense against every TOCTOU race tar extraction can exhibit). Reachability caveat unchanged from round 3: every scripts/ffmpeg_pin.json entry uses archive:zip today, so this arm is still dead code, and reaching it also requires controlling a pin entry's URL+SHA-256 pair.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-06T11:37:37.532Z",
    "resolved_at": "2026-09-06T11:37:43.657Z"
  },
  {
    "id": 24,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/golden/CORPUS_DIGEST.txt",
    "line": null,
    "description": "mkv_opus_a.webm and mkv_opus_b.webm now have no byte-level drift detection on any leg -- a silent byte change to either (a gen_corpus.sh recipe edit, an ffmpeg pin bump that alters Opus output) would not fail CI. The tests which still consume them (test_container_mkv.cpp, test_ebml_scan.cpp, test_doc03_coverage.cpp) assert structure and findings rather than bytes, not bytes. Named alternative: hash the parsed EBML structure (element IDs, sizes, ordering, CodecDelay and SeekPreRoll values) instead of file bytes -- this would restore drift detection without depending on encoder byte-stability.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-08T15:45:52.844Z",
    "resolved_at": null
  }
]
````
