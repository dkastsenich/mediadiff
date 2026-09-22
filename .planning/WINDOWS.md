---
schema_version: 1
open_count: 17
waived_count: 1
fixed_count: 19
total_count: 37
last_updated: 2026-09-22T11:40:43.784Z
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
| 25 | 04 | deviation | tests/support/golden.h |  | Resolved by debug session corpus-fixture-byte-drift (.planning/debug/resolved/): the 76-of-81 'fixture byte drift' reported on 2026-09-10 was not drift. This workstation's fixture bytes are byte-identical to what it produced on 2026-09-05 (13ea9db's golden size.file=351486, reproduced exactly today). What moved was the goldens: bc09705 overwrote 13ea9db's workstation-baselined goldens with bytes captured on the x64-linux CI runner 23 minutes later, because the pinned ffmpeg's runtime CPU-feature (SIMD) dispatch makes fixture bytes host-dependent (WINDOWS.md #12, still open and still true -- proved again here: -cpuflags 0 alone moves tracer_a.mp4 from 141218 to 141194 bytes on one unchanged binary). The DEFECT was that this designated-leg policy lived only as a ctest -E regex in .github/workflows/ci.yml, so it could not reach a developer or agent running ctest directly: they saw 5 red tests whose own failure text recommended UPDATE_GOLDENS=1 -- the one remedy that must never be applied to this class, and exactly the mistake 13ea9db made. Cost: Phase 4 paused at 1/12 plans plus a full debug session, for a non-defect. Fixed: check_golden_designated_leg() (tests/support/golden.h) now carries the policy in the harness -- asserts byte-for-byte when MEDIADIFF_DESIGNATED_LEG is set (ci.yml sets it on x64-linux, with a post-run guard that fails the leg if any of the 5 degraded to a skip), SKIPs with the full explanation everywhere else, and refuses UPDATE_GOLDENS on every leg. Goldens, fixtures and gen_corpus.sh were not touched -- nothing was re-baselined. | fixed |  | 2026-09-10T20:10:09.760Z | 2026-09-10T20:10:20.227Z |
| 26 | 05 | deviation | src/analyzers/timeline/start_duration.cpp,src/analyzers/video/stream_params.cpp,src/analyzers/size/size.cpp |  | 05-06-PLAN.md's correct_ts_overflow=0 fix (needed so unwrap_ts_timestamps ever sees a genuine 33-bit wrap) exposes that timeline.start/timeline.duration/timeline.duration.coherence (start_duration.cpp), video.frame_rate.measured (stream_params.cpp) and size.stream_bitrate (size.cpp) all read raw un-unwrapped PTS/DTS axis values directly on any genuinely-wrapping TS file, producing corrupted evidence (one instance: int64_t overflow in size.stream_bitrate's tolerance comparator, status=error). A follow-up plan must extend the shared doc-04-section-1.2 unwrap to these consumers. | fixed |  | 2026-09-16T21:49:42.037Z | 2026-09-18T17:28:52.360Z |
| 27 | 05 | deviation | src/analyzers/timeline/jitter_vfr.cpp |  | Inherits WINDOWS.md #26's open TS-unwrap gap: timeline_jitter_vfr_analyzer() calls derive_cadence(stream_packets, stream_scan.tb) directly on the raw, un-unwrapped packet array on every container including MPEG-TS, the same pattern #26 already documents for start_duration.cpp/stream_params.cpp/size.cpp -- on a genuinely-wrapping TS file this produces corrupted timeline.jitter/timeline.vfr_profile evidence (unwrapped PTS deltas straddling the wrap boundary). Not fixed by 05-08-PLAN.md (out of declared scope, matching #26's own precedent); no fixture in this plan's own corpus exercises a genuinely-wrapping TS file through this analyzer, so it is undetected by the current test suite. A follow-up plan extending #26's fix (the shared doc-04-section-1.2 unwrap) to jitter_vfr.cpp closes this too. | fixed |  | 2026-09-16T23:27:33.089Z | 2026-09-18T17:28:56.345Z |
| 28 | 05 | deviation | docs/checks/timeline.vfr_profile.md |  | 05-08-PLAN.md Task 2's own acceptance criterion ('mediadiff compare tests/fixtures/timeline_ntsc_base.mp4 tests/fixtures/timeline_ntsc_remux.mkv --profile remux --json shows timeline.vfr_profile at pass -- identical bins across two timebases') does not hold empirically: NTSC's 1001/30000s period (~33.3667ms) has no exact millisecond representation, so MP4's native 1/30000 timebase lands every interval on_grid (ideal_interval_num/den=119119/119, an exact integer) while Matroska's mandated 1ms timebase can only reach one_tick (ideal=3971/119, non-integer) -- a real difference in what each container can represent at its own tick resolution, not a defect in the check's D-06 grid-relative design (verified via mediadiff compare --json evidence before writing any test assertion, per this task's own PROVE-before-asserting discipline). The check itself is correct per D-06's literal design (deviation from the stream's own ideal_interval_num/den, the six-bucket vocabulary exactly as 05-CHECK-ROSTER.md approves); the plan's own illustrative claim was an untested assumption for this specific non-exactly-representable frame rate. Documented in docs/checks/timeline.vfr_profile.md's own Accept/Tune sections and asserted as the real, honest outcome in tests/integration/test_timeline_jitter.cpp's own NTSC test rather than asserting the false pass. | fixed |  | 2026-09-16T23:27:42.021Z | 2026-09-18T18:31:10.190Z |
| 29 | 05 | deviation | src/compare/tol.cpp | 359 | The tol comparator's human-readable delta (Finding.message, shown in the default tty output, --json, markdown and junit) does not show the number the verdict was decided on. (1) For RELATIVE (percent) tolerances it renders the ABSOLUTE delta in the value's own unit, unreduced, with a '%' suffix: size.stream_bitrate video on timeline_ts_nowrap.ts vs timeline_ts_jump.ts printed 'delta +1915435756800000/158352084000%' (= 12096 bps absolute) while the verdict compared +3.18%; size.file prints 'delta +54332/1%' for a 54332-byte change. (2) The sign comes from compare_ticks on the two sides' num only (tb-scaled), ignoring each RationalValue's den, so it is wrong whenever the dens differ: the audio stream_bitrate of the same pair fell 18.8% but printed '+'; it is also empty whenever that num-only comparison overflows (e.g. the wrap pair's size.stream_bitrate). (3) Fractions are never reduced. The verdicts themselves are correct -- only the text misleads (it misled the first diagnosis of debug session test-898-ci-nonreproducible). Not fixed there because message text is serialized into every report format and may be pinned by golden files; a fix should render the relative percentage for is_relative tolerances, derive the sign from the exact delta_num (core/exact_int.h already computes it), reduce the fraction, and first check tests/golden/* and the designated-leg goldens for pinned message text. | open |  | 2026-09-18T12:14:27.240Z |  |
| 30 | 05 | deviation | src/analyzers/size/size.cpp |  | Follow-up to WINDOWS.md #26 (still open): #26 cites 'int64_t overflow in size.stream_bitrate's tolerance comparator, status=error' as its observable instance. Debug session test-898-ci-nonreproducible made src/compare/tol.cpp exact (core/exact_int.h), so that comparator can no longer overflow: on timeline_ts_nowrap.ts vs timeline_ts_wrap.ts the same corrupted, un-unwrapped size.stream_bitrate (and timeline.av_drift) inputs now produce status=fail with meaningless magnitudes (e.g. video 'delta 1164020667413667840000/3061451405548800%') instead of error. The defect is unchanged and still #26's (analyzers reading raw wrapped PTS/DTS); only its symptom moved from error to a false fail, so anyone searching reports for #26's error signature will no longer find it. tests/integration/test_timeline_structure.cpp's Test 4 still asserts only timeline.wrap_events on that pair, by design. Close together with #26. | fixed |  | 2026-09-18T12:14:35.643Z | 2026-09-18T17:28:56.479Z |
| 31 | 05 | deviation | src/analyzers/timeline/av_sync.cpp |  | timeline.av_offset/timeline.av_drift/timeline.av_drift.pattern read raw, un-unwrapped PTS on MPEG-TS, the same root cause as WINDOWS.md #26, found by 05-VERIFICATION.md Gap 2 and not previously filed. libavformat's declared durations were also corrupted under correct_ts_overflow=0 on a wrapping file, fixed by 05-17's overflow-corrected re-probe. Both are fixed by 05-16/05-17/05-18, and the whole-report wrap assertion (tests/integration/test_timeline_structure.cpp) guards them. | fixed |  | 2026-09-18T17:29:06.380Z | 2026-09-18T17:29:10.070Z |
| 32 | 05 | deviation | src/analyzers/timeline/av_sync.cpp |  | On the lossless MP4-to-TS remux pairs (timeline_start_base.mp4 vs timeline_start_shift.ts, and vs timeline_avoffset_unknown.ts), timeline.av_drift reports fail and timeline.av_drift.pattern reports fail (pattern=irregular, residual_max_ms=42). Cause: the checkpoint span uses libavformat's ESTIMATED TS audio stream duration -- MPEG-TS does not declare a per-stream duration the way MP4 does. Observed TS packet extents include AAC priming/padding samples with no edit list to exclude them, so neither span source removes the residual by itself: span:observed only swaps this reading for a different, still-wrong 39ms false linear-drift on the TS side (05-STEP-DESIGN.md Orchestrator note, 2026-09-18 -- the earlier span:observed MP4-side regression this research first reported was a harness-variant artifact, not evidence against span:observed as the plan actually scoped it). The human kept span:declared, today's shipped checkpoint span source, at the 05-21 blocking-human checkpoint. Closing this residual needs a priming/padding-aware span, filed as a follow-up. -- 06-07-PLAN.md (D-16) update, 2026-09-20: implemented a generic, evidence-shape-gated shared-basis span (src/analyzers/timeline/av_sync.cpp's detail::span_ticks_for_basis, src/compare/tol.cpp's generalised Rule 2 override) that reconstructs the trimmed span directly from the packet-derived extent whenever a stream's own priming AND padding are both known/convertible -- verified FIXED for real MP4-vs-MP4 priming pairs (timeline_start_base.mp4 vs timeline_avoffset_video_shift.mp4 now shares span_basis=adjusted on both sides and stays clean). For the two target MP4-to-TS pairs specifically, audio.priming's own evidence confirms the TS side's priming is genuinely unknown (source=unknown, samples=0, padding=null -- no skip_samples side data, no initial_padding, no edit list survives the remux), not merely unread by this analyzer, so the shared-basis rule correctly falls back to raw-to-raw on both sides per D-11 rather than fabricating a basis. Post-fix measured evidence: baseline (MP4) now reports span_basis=adjusted, end_delta_ms=0, residual_max_ms=0 (its own true zero drift); candidate (TS) reports span_basis=raw, end_delta_ms=39 (previously residual_max_ms=42 under pattern=irregular -- now pattern=linear-drift, a SHAPE change, not a resolution). timeline.av_drift and timeline.av_drift.pattern both still fail on both target pairs (verified fixtures: timeline_start_base.mp4 vs timeline_start_shift.ts, and vs timeline_avoffset_unknown.ts). Left OPEN, not fixed, per this plan's own A2 fallback: closing this fully needs a decode-based priming-detection mechanism this plan does not build, filed as a follow-up. | open |  | 2026-09-18T19:55:05.254Z |  |
| 33 | 06 | deviation | src/probe/audio_decode.cpp |  | Task 1 Test 6 (--hash-decoder aac_fixed falls back on USAC content) not exercised end-to-end -- hand-built USAC ASC rejected by avcodec_open2 for all candidate decoders in this env; steering code reviewed by inspection only, see 06-05-SUMMARY.md Known Stubs | open |  | 2026-09-20T15:50:55.876Z |  |
| 34 | 06 | deviation | src/analyzers/timeline/start_duration.cpp |  | 06-11-PLAN.md Task 2's corpus-wide clean sweep (tests/integration/test_audio_corpus_sweep.cpp) surfaces one pre-existing, non-audio artifact: timeline_ts_nowrap.ts vs its own byte-identical copy timeline_ts_nowrap_copy.ts reports timeline.duration.coherence status=info 'both values are flagged' -- a Phase 5 (D-02) known fact, already documented and independently asserted by tests/integration/test_timeline_structure.cpp's own Test 5 ('the wrap fixture's own byte-identical clean pair declares only the pre-existing TS-audio-duration artifact'). Root cause: timeline.duration.coherence is a state semantic (src/core/checks.def, flagged_values=[container_vs_stream,...]) whose own registered comment states 'there is no way to make a state-semantic pair with BOTH sides flagged report pass' -- this specific TS fixture's own container-vs-stream duration disagreement (a genuine MPEG-TS audio-duration-bookkeeping quirk, not a diff) is present on BOTH sides of any comparison involving it, including against itself. Not fixed within 06-11-PLAN.md's own file scope (fixing it is Phase 5 (timeline) analyzer/semantic work, out of scope for the audio inspect-section plan). tests/integration/test_audio_corpus_sweep.cpp records this ONE pair as a named, cited exception (expected non-pass set = {timeline.duration.coherence}, matching test_timeline_structure.cpp's own declared set exactly) rather than filtering the whole-report counter -- every OTHER declared clean pair in the corpus (90+ ids) reports zero non-pass findings. | open |  | 2026-09-20T22:02:22.475Z |  |
| 35 | 06 | deviation | src/analyzers/timeline/start_duration.cpp |  | Second instance of WINDOWS.md #34's same root cause, found by the SAME 06-11-PLAN.md Task 2 corpus-wide clean sweep: topo_subs.mp4 vs its own byte-identical copy topo_subs_copy.mp4 (container.track_count/container.track_types's own declared clean pair, test_doc03_coverage.cpp/coverage_pairs.h) reports timeline.duration.coherence status=info 'both values are flagged' at scope subtitle[0] this time (a mov_text subtitle track's own container-vs-stream duration bookkeeping disagreement, not audio). Same state-semantic limitation as #34 (src/core/checks.def's own registered comment: 'there is no way to make a state-semantic pair with BOTH sides flagged report pass') -- an inherent per-file property, not something a fixture swap can dodge while keeping subtitle-track coverage, and Phase 5 (timeline) analyzer/semantic work is out of scope for 06-11-PLAN.md. tests/integration/test_audio_corpus_sweep.cpp records this as a second named, cited exception (expected non-pass set = {timeline.duration.coherence} at scope subtitle[0]) alongside #34's TS-audio instance. | open |  | 2026-09-20T22:06:57.072Z |  |
| 36 | 06 | unmet-truth | tests/integration/test_audio_priming.cpp |  | audio_prime_base.mp4 vs audio_prime_roundtrip2.mp4: audio.priming reports baseline "1024" vs candidate "1014", status fail, reproduced 3x. Cause: MP4->MKV->MP4 round trip; MKV stores priming as CodecDelay in nanoseconds, so a sample count cannot survive the round trip exactly. No tolerance can express it -- D-14 forces audio.priming to semantic=exact over value_kind=string. Documented in 06-06-SUMMARY.md Deviations #3, asserted as a real fail in Test 4. Open, not confirmed closeable (contradicts ROADMAP SC2's 'closing the priming: unknown gap' framing). | open |  | 2026-09-22T09:57:49.321Z |  |
| 37 | 06 | deviation | src/compare/tol.cpp |  | CI run 35708992998: audio.loudness.true_peak was NOT cross-platform stable on lossy (class-2) float-decoded AAC audio -- timeline_drift_base.mp4 vs timeline_drift_linear.mp4 (--profile sw-encoder) measured a 0.100dB delta on x64-linux (within the 0.3dB tolerance) but exceeded 0.3dB on BOTH x64-windows-static-md and arm64-osx for the SAME comparison, failing expect_declared_set on all 3 blocking legs. PREMISE DISPROVEN, first fix REVERTED. f7ce12d class-gated this on D-05's decode_path_class evidence, on the premise that libebur128's float true-peak computation was not bit-identical across platforms on non-class-1 decode paths. Debug session true-peak-cross-platform (.planning/debug/resolved/true-peak-cross-platform.md) disproved it: all six readings were class1, so the gate provably could not fire, and CI run 35713912901 confirmed the failure persisted. The real cause was the FIXTURE. Media fixtures are gitignored and regenerated on EVERY runner, so the legs never compared the same bytes, AND timeline_drift_linear.mp4 was the only recipe in scripts/gen_corpus.sh carrying a resampler (asetrate=48048,aresample=48000), whose libswresample per-architecture SIMD made each leg's PCM differ; the AAC encoder then amplified that few-LSB delta non-linearly into a multi-dB decoded true-peak swing (-15.964 x64-linux / -13.500 C-ref / -17.697 arm64-osx). audio.loudness.true_peak was CORRECT throughout. Fixed for real by 476f4c5, which reaches the same 0.1% clock error DSP-free (sample_rate=47952,asetrate=48000); the tol.cpp gate and SkipReason::cross_platform_decode_noise were reverted. The decode_path_class evidence key on both loudness checks is KEPT as diagnostic-only evidence (it is what eliminated decoder nondeterminism in that investigation) and changes no verdict. Tolerance never widened, declared set never touched. CYCLE 2 (CI run 35723466889, after 476f4c5): that fix was NECESSARY but NOT SUFFICIENT. x64-windows-static-md went FULLY GREEN -- proof on a genuinely foreign DSP path that the resampler really was a cause -- but arm64-osx still failed test 1137 at delta +1.299dB, and the SIGN FLIPPED relative to x64-linux (candidate -14.765 where linux reads -15.976, previously -17.697). timeline_drift_base.mp4 stayed exactly -16.064 on every leg, confirming the resampler-free path is bit-stable on real aarch64. A sign flip under a SMALLER magnitude is a chaotic max reshuffle, not a shrinking proportional error: the remaining amplifier was the NATIVE AAC ENCODER itself, which the cycle-1 session had already proved independently DSP-divergent (every AAC elementary stream in this pair differs across DSP paths, including base's, whose PCM input is bit-identical). Removing the resampler removed the larger INPUT to the amplifier, not the amplifier. Cycle 2 applied the escalation cycle 1 pre-registered in its own blind_spots: BOTH fixtures' audio moved from -c:a aac to -c:a pcm_s16le, so the stored audio IS the filter-graph output and the decoded samples ARE the stored bytes -- decode class 1 by src/probe/audio_decode.h's own definition ('bit-exact by construction, no algorithm exists to diverge across SIMD levels or architectures'). Verified on both the x86-SIMD and the -cpuflags 0 C-reference DSP path: decoded PCM byte-identical on both arms, audio.loudness.true_peak -18.056 dBTP on BOTH files (delta EXACTLY 0, not merely small), audio.loudness.integrated delta 0, timeline.av_drift still the IDENTICAL rational -54717060000/907751640 ms/min, pattern still linear-drift, and a per-(id, scope) status diff of the whole report against the AAC incumbent shows ZERO changes. alac and flac were evaluated and rejected (flac's encoder is itself arch-divergent; alac's arm-to-arm stream delta of -1.188% sits only 1.8pp from size.stream_bitrate's 3% warn line -- the test-898-ci-nonreproducible trap). Mechanism demonstrated directly: a 1-LSB perturbation of 0.1% of the input samples moves the AAC-decoded true peak 0.237dB (pass -> warn) and the PCM-decoded peak 0.001dB. Tolerance still never widened, declared set still never touched. RESIDUAL: arm64-osx cannot be run locally; needs a real CI run to close. timeline_drift_step.mp4 is still AAC and is the same latent class, out of this fix's scope and green on arm64 across three runs. | open |  | 2026-09-22T09:57:59.516Z |  |

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
  },
  {
    "id": 25,
    "kind": "deviation",
    "phase": "04",
    "file": "tests/support/golden.h",
    "line": null,
    "description": "Resolved by debug session corpus-fixture-byte-drift (.planning/debug/resolved/): the 76-of-81 'fixture byte drift' reported on 2026-09-10 was not drift. This workstation's fixture bytes are byte-identical to what it produced on 2026-09-05 (13ea9db's golden size.file=351486, reproduced exactly today). What moved was the goldens: bc09705 overwrote 13ea9db's workstation-baselined goldens with bytes captured on the x64-linux CI runner 23 minutes later, because the pinned ffmpeg's runtime CPU-feature (SIMD) dispatch makes fixture bytes host-dependent (WINDOWS.md #12, still open and still true -- proved again here: -cpuflags 0 alone moves tracer_a.mp4 from 141218 to 141194 bytes on one unchanged binary). The DEFECT was that this designated-leg policy lived only as a ctest -E regex in .github/workflows/ci.yml, so it could not reach a developer or agent running ctest directly: they saw 5 red tests whose own failure text recommended UPDATE_GOLDENS=1 -- the one remedy that must never be applied to this class, and exactly the mistake 13ea9db made. Cost: Phase 4 paused at 1/12 plans plus a full debug session, for a non-defect. Fixed: check_golden_designated_leg() (tests/support/golden.h) now carries the policy in the harness -- asserts byte-for-byte when MEDIADIFF_DESIGNATED_LEG is set (ci.yml sets it on x64-linux, with a post-run guard that fails the leg if any of the 5 degraded to a skip), SKIPs with the full explanation everywhere else, and refuses UPDATE_GOLDENS on every leg. Goldens, fixtures and gen_corpus.sh were not touched -- nothing was re-baselined.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-10T20:10:09.760Z",
    "resolved_at": "2026-09-10T20:10:20.227Z"
  },
  {
    "id": 26,
    "kind": "deviation",
    "phase": "05",
    "file": "src/analyzers/timeline/start_duration.cpp,src/analyzers/video/stream_params.cpp,src/analyzers/size/size.cpp",
    "line": null,
    "description": "05-06-PLAN.md's correct_ts_overflow=0 fix (needed so unwrap_ts_timestamps ever sees a genuine 33-bit wrap) exposes that timeline.start/timeline.duration/timeline.duration.coherence (start_duration.cpp), video.frame_rate.measured (stream_params.cpp) and size.stream_bitrate (size.cpp) all read raw un-unwrapped PTS/DTS axis values directly on any genuinely-wrapping TS file, producing corrupted evidence (one instance: int64_t overflow in size.stream_bitrate's tolerance comparator, status=error). A follow-up plan must extend the shared doc-04-section-1.2 unwrap to these consumers.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-16T21:49:42.037Z",
    "resolved_at": "2026-09-18T17:28:52.360Z"
  },
  {
    "id": 27,
    "kind": "deviation",
    "phase": "05",
    "file": "src/analyzers/timeline/jitter_vfr.cpp",
    "line": null,
    "description": "Inherits WINDOWS.md #26's open TS-unwrap gap: timeline_jitter_vfr_analyzer() calls derive_cadence(stream_packets, stream_scan.tb) directly on the raw, un-unwrapped packet array on every container including MPEG-TS, the same pattern #26 already documents for start_duration.cpp/stream_params.cpp/size.cpp -- on a genuinely-wrapping TS file this produces corrupted timeline.jitter/timeline.vfr_profile evidence (unwrapped PTS deltas straddling the wrap boundary). Not fixed by 05-08-PLAN.md (out of declared scope, matching #26's own precedent); no fixture in this plan's own corpus exercises a genuinely-wrapping TS file through this analyzer, so it is undetected by the current test suite. A follow-up plan extending #26's fix (the shared doc-04-section-1.2 unwrap) to jitter_vfr.cpp closes this too.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-16T23:27:33.089Z",
    "resolved_at": "2026-09-18T17:28:56.345Z"
  },
  {
    "id": 28,
    "kind": "deviation",
    "phase": "05",
    "file": "docs/checks/timeline.vfr_profile.md",
    "line": null,
    "description": "05-08-PLAN.md Task 2's own acceptance criterion ('mediadiff compare tests/fixtures/timeline_ntsc_base.mp4 tests/fixtures/timeline_ntsc_remux.mkv --profile remux --json shows timeline.vfr_profile at pass -- identical bins across two timebases') does not hold empirically: NTSC's 1001/30000s period (~33.3667ms) has no exact millisecond representation, so MP4's native 1/30000 timebase lands every interval on_grid (ideal_interval_num/den=119119/119, an exact integer) while Matroska's mandated 1ms timebase can only reach one_tick (ideal=3971/119, non-integer) -- a real difference in what each container can represent at its own tick resolution, not a defect in the check's D-06 grid-relative design (verified via mediadiff compare --json evidence before writing any test assertion, per this task's own PROVE-before-asserting discipline). The check itself is correct per D-06's literal design (deviation from the stream's own ideal_interval_num/den, the six-bucket vocabulary exactly as 05-CHECK-ROSTER.md approves); the plan's own illustrative claim was an untested assumption for this specific non-exactly-representable frame rate. Documented in docs/checks/timeline.vfr_profile.md's own Accept/Tune sections and asserted as the real, honest outcome in tests/integration/test_timeline_jitter.cpp's own NTSC test rather than asserting the false pass.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-16T23:27:42.021Z",
    "resolved_at": "2026-09-18T18:31:10.190Z"
  },
  {
    "id": 29,
    "kind": "deviation",
    "phase": "05",
    "file": "src/compare/tol.cpp",
    "line": 359,
    "description": "The tol comparator's human-readable delta (Finding.message, shown in the default tty output, --json, markdown and junit) does not show the number the verdict was decided on. (1) For RELATIVE (percent) tolerances it renders the ABSOLUTE delta in the value's own unit, unreduced, with a '%' suffix: size.stream_bitrate video on timeline_ts_nowrap.ts vs timeline_ts_jump.ts printed 'delta +1915435756800000/158352084000%' (= 12096 bps absolute) while the verdict compared +3.18%; size.file prints 'delta +54332/1%' for a 54332-byte change. (2) The sign comes from compare_ticks on the two sides' num only (tb-scaled), ignoring each RationalValue's den, so it is wrong whenever the dens differ: the audio stream_bitrate of the same pair fell 18.8% but printed '+'; it is also empty whenever that num-only comparison overflows (e.g. the wrap pair's size.stream_bitrate). (3) Fractions are never reduced. The verdicts themselves are correct -- only the text misleads (it misled the first diagnosis of debug session test-898-ci-nonreproducible). Not fixed there because message text is serialized into every report format and may be pinned by golden files; a fix should render the relative percentage for is_relative tolerances, derive the sign from the exact delta_num (core/exact_int.h already computes it), reduce the fraction, and first check tests/golden/* and the designated-leg goldens for pinned message text.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-18T12:14:27.240Z",
    "resolved_at": null
  },
  {
    "id": 30,
    "kind": "deviation",
    "phase": "05",
    "file": "src/analyzers/size/size.cpp",
    "line": null,
    "description": "Follow-up to WINDOWS.md #26 (still open): #26 cites 'int64_t overflow in size.stream_bitrate's tolerance comparator, status=error' as its observable instance. Debug session test-898-ci-nonreproducible made src/compare/tol.cpp exact (core/exact_int.h), so that comparator can no longer overflow: on timeline_ts_nowrap.ts vs timeline_ts_wrap.ts the same corrupted, un-unwrapped size.stream_bitrate (and timeline.av_drift) inputs now produce status=fail with meaningless magnitudes (e.g. video 'delta 1164020667413667840000/3061451405548800%') instead of error. The defect is unchanged and still #26's (analyzers reading raw wrapped PTS/DTS); only its symptom moved from error to a false fail, so anyone searching reports for #26's error signature will no longer find it. tests/integration/test_timeline_structure.cpp's Test 4 still asserts only timeline.wrap_events on that pair, by design. Close together with #26.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-18T12:14:35.643Z",
    "resolved_at": "2026-09-18T17:28:56.479Z"
  },
  {
    "id": 31,
    "kind": "deviation",
    "phase": "05",
    "file": "src/analyzers/timeline/av_sync.cpp",
    "line": null,
    "description": "timeline.av_offset/timeline.av_drift/timeline.av_drift.pattern read raw, un-unwrapped PTS on MPEG-TS, the same root cause as WINDOWS.md #26, found by 05-VERIFICATION.md Gap 2 and not previously filed. libavformat's declared durations were also corrupted under correct_ts_overflow=0 on a wrapping file, fixed by 05-17's overflow-corrected re-probe. Both are fixed by 05-16/05-17/05-18, and the whole-report wrap assertion (tests/integration/test_timeline_structure.cpp) guards them.",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-18T17:29:06.380Z",
    "resolved_at": "2026-09-18T17:29:10.070Z"
  },
  {
    "id": 32,
    "kind": "deviation",
    "phase": "05",
    "file": "src/analyzers/timeline/av_sync.cpp",
    "line": null,
    "description": "On the lossless MP4-to-TS remux pairs (timeline_start_base.mp4 vs timeline_start_shift.ts, and vs timeline_avoffset_unknown.ts), timeline.av_drift reports fail and timeline.av_drift.pattern reports fail (pattern=irregular, residual_max_ms=42). Cause: the checkpoint span uses libavformat's ESTIMATED TS audio stream duration -- MPEG-TS does not declare a per-stream duration the way MP4 does. Observed TS packet extents include AAC priming/padding samples with no edit list to exclude them, so neither span source removes the residual by itself: span:observed only swaps this reading for a different, still-wrong 39ms false linear-drift on the TS side (05-STEP-DESIGN.md Orchestrator note, 2026-09-18 -- the earlier span:observed MP4-side regression this research first reported was a harness-variant artifact, not evidence against span:observed as the plan actually scoped it). The human kept span:declared, today's shipped checkpoint span source, at the 05-21 blocking-human checkpoint. Closing this residual needs a priming/padding-aware span, filed as a follow-up. -- 06-07-PLAN.md (D-16) update, 2026-09-20: implemented a generic, evidence-shape-gated shared-basis span (src/analyzers/timeline/av_sync.cpp's detail::span_ticks_for_basis, src/compare/tol.cpp's generalised Rule 2 override) that reconstructs the trimmed span directly from the packet-derived extent whenever a stream's own priming AND padding are both known/convertible -- verified FIXED for real MP4-vs-MP4 priming pairs (timeline_start_base.mp4 vs timeline_avoffset_video_shift.mp4 now shares span_basis=adjusted on both sides and stays clean). For the two target MP4-to-TS pairs specifically, audio.priming's own evidence confirms the TS side's priming is genuinely unknown (source=unknown, samples=0, padding=null -- no skip_samples side data, no initial_padding, no edit list survives the remux), not merely unread by this analyzer, so the shared-basis rule correctly falls back to raw-to-raw on both sides per D-11 rather than fabricating a basis. Post-fix measured evidence: baseline (MP4) now reports span_basis=adjusted, end_delta_ms=0, residual_max_ms=0 (its own true zero drift); candidate (TS) reports span_basis=raw, end_delta_ms=39 (previously residual_max_ms=42 under pattern=irregular -- now pattern=linear-drift, a SHAPE change, not a resolution). timeline.av_drift and timeline.av_drift.pattern both still fail on both target pairs (verified fixtures: timeline_start_base.mp4 vs timeline_start_shift.ts, and vs timeline_avoffset_unknown.ts). Left OPEN, not fixed, per this plan's own A2 fallback: closing this fully needs a decode-based priming-detection mechanism this plan does not build, filed as a follow-up.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-18T19:55:05.254Z",
    "resolved_at": null
  },
  {
    "id": 33,
    "kind": "deviation",
    "phase": "06",
    "file": "src/probe/audio_decode.cpp",
    "line": null,
    "description": "Task 1 Test 6 (--hash-decoder aac_fixed falls back on USAC content) not exercised end-to-end -- hand-built USAC ASC rejected by avcodec_open2 for all candidate decoders in this env; steering code reviewed by inspection only, see 06-05-SUMMARY.md Known Stubs",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-20T15:50:55.876Z",
    "resolved_at": null
  },
  {
    "id": 34,
    "kind": "deviation",
    "phase": "06",
    "file": "src/analyzers/timeline/start_duration.cpp",
    "line": null,
    "description": "06-11-PLAN.md Task 2's corpus-wide clean sweep (tests/integration/test_audio_corpus_sweep.cpp) surfaces one pre-existing, non-audio artifact: timeline_ts_nowrap.ts vs its own byte-identical copy timeline_ts_nowrap_copy.ts reports timeline.duration.coherence status=info 'both values are flagged' -- a Phase 5 (D-02) known fact, already documented and independently asserted by tests/integration/test_timeline_structure.cpp's own Test 5 ('the wrap fixture's own byte-identical clean pair declares only the pre-existing TS-audio-duration artifact'). Root cause: timeline.duration.coherence is a state semantic (src/core/checks.def, flagged_values=[container_vs_stream,...]) whose own registered comment states 'there is no way to make a state-semantic pair with BOTH sides flagged report pass' -- this specific TS fixture's own container-vs-stream duration disagreement (a genuine MPEG-TS audio-duration-bookkeeping quirk, not a diff) is present on BOTH sides of any comparison involving it, including against itself. Not fixed within 06-11-PLAN.md's own file scope (fixing it is Phase 5 (timeline) analyzer/semantic work, out of scope for the audio inspect-section plan). tests/integration/test_audio_corpus_sweep.cpp records this ONE pair as a named, cited exception (expected non-pass set = {timeline.duration.coherence}, matching test_timeline_structure.cpp's own declared set exactly) rather than filtering the whole-report counter -- every OTHER declared clean pair in the corpus (90+ ids) reports zero non-pass findings.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-20T22:02:22.475Z",
    "resolved_at": null
  },
  {
    "id": 35,
    "kind": "deviation",
    "phase": "06",
    "file": "src/analyzers/timeline/start_duration.cpp",
    "line": null,
    "description": "Second instance of WINDOWS.md #34's same root cause, found by the SAME 06-11-PLAN.md Task 2 corpus-wide clean sweep: topo_subs.mp4 vs its own byte-identical copy topo_subs_copy.mp4 (container.track_count/container.track_types's own declared clean pair, test_doc03_coverage.cpp/coverage_pairs.h) reports timeline.duration.coherence status=info 'both values are flagged' at scope subtitle[0] this time (a mov_text subtitle track's own container-vs-stream duration bookkeeping disagreement, not audio). Same state-semantic limitation as #34 (src/core/checks.def's own registered comment: 'there is no way to make a state-semantic pair with BOTH sides flagged report pass') -- an inherent per-file property, not something a fixture swap can dodge while keeping subtitle-track coverage, and Phase 5 (timeline) analyzer/semantic work is out of scope for 06-11-PLAN.md. tests/integration/test_audio_corpus_sweep.cpp records this as a second named, cited exception (expected non-pass set = {timeline.duration.coherence} at scope subtitle[0]) alongside #34's TS-audio instance.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-20T22:06:57.072Z",
    "resolved_at": null
  },
  {
    "id": 36,
    "kind": "unmet-truth",
    "phase": "06",
    "file": "tests/integration/test_audio_priming.cpp",
    "line": null,
    "description": "audio_prime_base.mp4 vs audio_prime_roundtrip2.mp4: audio.priming reports baseline \"1024\" vs candidate \"1014\", status fail, reproduced 3x. Cause: MP4->MKV->MP4 round trip; MKV stores priming as CodecDelay in nanoseconds, so a sample count cannot survive the round trip exactly. No tolerance can express it -- D-14 forces audio.priming to semantic=exact over value_kind=string. Documented in 06-06-SUMMARY.md Deviations #3, asserted as a real fail in Test 4. Open, not confirmed closeable (contradicts ROADMAP SC2's 'closing the priming: unknown gap' framing).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-22T09:57:49.321Z",
    "resolved_at": null
  },
  {
    "id": 37,
    "kind": "deviation",
    "phase": "06",
    "file": "src/compare/tol.cpp",
    "line": null,
    "description": "CI run 35708992998: audio.loudness.true_peak was NOT cross-platform stable on lossy (class-2) float-decoded AAC audio -- timeline_drift_base.mp4 vs timeline_drift_linear.mp4 (--profile sw-encoder) measured a 0.100dB delta on x64-linux (within the 0.3dB tolerance) but exceeded 0.3dB on BOTH x64-windows-static-md and arm64-osx for the SAME comparison, failing expect_declared_set on all 3 blocking legs. PREMISE DISPROVEN, first fix REVERTED. f7ce12d class-gated this on D-05's decode_path_class evidence, on the premise that libebur128's float true-peak computation was not bit-identical across platforms on non-class-1 decode paths. Debug session true-peak-cross-platform (.planning/debug/resolved/true-peak-cross-platform.md) disproved it: all six readings were class1, so the gate provably could not fire, and CI run 35713912901 confirmed the failure persisted. The real cause was the FIXTURE. Media fixtures are gitignored and regenerated on EVERY runner, so the legs never compared the same bytes, AND timeline_drift_linear.mp4 was the only recipe in scripts/gen_corpus.sh carrying a resampler (asetrate=48048,aresample=48000), whose libswresample per-architecture SIMD made each leg's PCM differ; the AAC encoder then amplified that few-LSB delta non-linearly into a multi-dB decoded true-peak swing (-15.964 x64-linux / -13.500 C-ref / -17.697 arm64-osx). audio.loudness.true_peak was CORRECT throughout. Fixed for real by 476f4c5, which reaches the same 0.1% clock error DSP-free (sample_rate=47952,asetrate=48000); the tol.cpp gate and SkipReason::cross_platform_decode_noise were reverted. The decode_path_class evidence key on both loudness checks is KEPT as diagnostic-only evidence (it is what eliminated decoder nondeterminism in that investigation) and changes no verdict. Tolerance never widened, declared set never touched. CYCLE 2 (CI run 35723466889, after 476f4c5): that fix was NECESSARY but NOT SUFFICIENT. x64-windows-static-md went FULLY GREEN -- proof on a genuinely foreign DSP path that the resampler really was a cause -- but arm64-osx still failed test 1137 at delta +1.299dB, and the SIGN FLIPPED relative to x64-linux (candidate -14.765 where linux reads -15.976, previously -17.697). timeline_drift_base.mp4 stayed exactly -16.064 on every leg, confirming the resampler-free path is bit-stable on real aarch64. A sign flip under a SMALLER magnitude is a chaotic max reshuffle, not a shrinking proportional error: the remaining amplifier was the NATIVE AAC ENCODER itself, which the cycle-1 session had already proved independently DSP-divergent (every AAC elementary stream in this pair differs across DSP paths, including base's, whose PCM input is bit-identical). Removing the resampler removed the larger INPUT to the amplifier, not the amplifier. Cycle 2 applied the escalation cycle 1 pre-registered in its own blind_spots: BOTH fixtures' audio moved from -c:a aac to -c:a pcm_s16le, so the stored audio IS the filter-graph output and the decoded samples ARE the stored bytes -- decode class 1 by src/probe/audio_decode.h's own definition ('bit-exact by construction, no algorithm exists to diverge across SIMD levels or architectures'). Verified on both the x86-SIMD and the -cpuflags 0 C-reference DSP path: decoded PCM byte-identical on both arms, audio.loudness.true_peak -18.056 dBTP on BOTH files (delta EXACTLY 0, not merely small), audio.loudness.integrated delta 0, timeline.av_drift still the IDENTICAL rational -54717060000/907751640 ms/min, pattern still linear-drift, and a per-(id, scope) status diff of the whole report against the AAC incumbent shows ZERO changes. alac and flac were evaluated and rejected (flac's encoder is itself arch-divergent; alac's arm-to-arm stream delta of -1.188% sits only 1.8pp from size.stream_bitrate's 3% warn line -- the test-898-ci-nonreproducible trap). Mechanism demonstrated directly: a 1-LSB perturbation of 0.1% of the input samples moves the AAC-decoded true peak 0.237dB (pass -> warn) and the PCM-decoded peak 0.001dB. Tolerance still never widened, declared set still never touched. RESIDUAL: arm64-osx cannot be run locally; needs a real CI run to close. timeline_drift_step.mp4 is still AAC and is the same latent class, out of this fix's scope and green on arm64 across three runs.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-22T09:57:59.516Z",
    "resolved_at": null
  }
]
````
