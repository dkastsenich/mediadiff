# 07-15 round-trip log (Task 1: push, read the real CI run, iterate)

This is NOT a SUMMARY. 07-15 is not complete: no designated-leg number has been transcribed yet.
Each iteration below is appended by the continuation that read that run. Nothing in
`tests/golden/` was transcribed from any run in this log; every digest line the iteration
touched is a workstation prediction and stays in `CORPUS_DIGEST_PROVISIONAL.txt`.

Branch `gsd/phase-07-content-quality`, pull request https://github.com/dkastsenich/mediadiff/pull/9.

## Iteration 1 - capture run 36928305270 (head e58df65, conclusion failure)

Pre-flight #1 (at e58df65, before the push) was green: 41/41 provisional digest lines matched
the `TZ=UTC taskset -c 0-3` prediction, 1529/1529, 11 lints, 5 designated-leg goldens, VMAF
42/42, proof 4/4.

### Per-leg results

| Job | Id | Result | Where it stopped |
|---|---|---|---|
| lint (ENG-16 boundary) | 110590997958 | success | - |
| video-proof-streams | 110590998251 | success | - |
| build (x64-linux), designated | 110591129023 | success | - |
| build (arm64-linux) | 110591128809 | failure | Test: 3 of 1524 failed |
| build (x64-windows-static-md), REQUIRED | 110591128875 | failure | Build |
| build (x64-osx) | 110591128896 | failure | Build |
| build (arm64-osx), REQUIRED | 110591128948 | failure | Build |

Failures, quoted from the logs:

- arm64-osx and x64-osx, Build: `src/probe/pair_scorer.h:319:8: error: private field 'vmaf_finished_' is not used [-Werror,-Wunused-private-field]` (default, non-VMAF build).
- x64-windows-static-md, Build: `tests\unit\test_ssim_int.cpp(496): warning C4334: '<<': result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)` then C2220.
- arm64-linux, Test:
  - `integration.video_decode_edges - corrupt stream`: test_video_decode_edges.cpp:243 `CHECK( video_chain(*fp).element_count == kCorruptFramesFramemd5 )` expanded `100 == 99`; :260 `CHECK( meta->evidence.contains("first_error_reason") )` failed.
  - `integration.video_decode_edges - corrupt vs base`: :301 expanded `100 == 99`.
  - `integration.video_locator - propagation`: test_video_locator.cpp:74 `CHECK( range.at("first") == first )` expanded `40 == 41`; :76 `CHECK( range.at("differing") == differing )` also failed.
  - The arm64 decoder log still showed `ac-tex damaged at 4 0 / Error at MB: 4 / concealing 300 DC, 300 AC, 300 MV errors in P frame`.

The Windows, x64-osx and arm64-osx legs never reached Test, so nothing about their test
results was observed in this run. The designated x64-linux leg passed Build and Test.

### Per-leg corpus digest listings (read from the same run, evidence only)

`CORPUS_DIGEST_SUMMARY=` of each leg's own `Report corpus digest` step:

- x64-linux `6a513798c078a89696b06c83c934594cc1b3642bec05e29fde1f8d435f04f547`
- x64-windows-static-md `6a513798c078a89696b06c83c934594cc1b3642bec05e29fde1f8d435f04f547` (same listing as x64-linux)
- x64-osx and arm64-osx both `cef2dd160b06c0f122d64582fbbb544b1ca0f2aeb05bf8a15c3e096f7983ffeb` (identical to each other)
- arm64-linux `a4608637033d700c7600730441621b12d98545d14c1369af7ba8866eb9adec5a`

Against x64-linux, arm64-linux and x64-osx each differ on 197 of the 246 listed names (the
listing includes the generator's hidden intermediates); 34 of the 41 Phase-7 provisional names
differ on each, including `video_corrupt_mpeg4*.mkv`, `video_loc_mpeg4_c40.mkv` and the lossless
HuffYUV family (`video_loc_huffyuv*.mkv`). Three groups of byte-identical legs therefore exist
(x64-linux with Windows, the two macOS legs, arm64-linux), and a fixture's bytes are not a leg
constant.

### Hypothesis test (arm64 test failures)

Working hypothesis: the mpeg4 fixtures carry per-architecture encoder bytes (DSP path), and the
tests hard-code numbers derived from x86 bytes. Probes with the pinned ffmpeg 9.0.1 on x86_64
(scratch dir, `-threads 1`, bitexact flags, `-cpuflags 0` as the C reference path):

| Probe | Result |
|---|---|
| base recipe, SIMD vs `-cpuflags 0` | different bytes: `d1e700aa...` vs `1f5c1a2c...` |
| noise amount 50 on the SIMD base | 99 frames (rejected) - the committed expectation |
| noise amount 50 on the `-cpuflags 0` base | 100 frames (concealed) - the arm64-linux symptom |
| amount sweep 10..70 on the `-cpuflags 0` base | 99/100 alternates with no pattern (e.g. 29,30,31 reject; 32 conceals; 50 conceals) |
| the OLD recipe on the `-cpuflags 0` base, real tests | reproduces the CI failures exactly: `test_video_decode_edges.cpp:243 100 == 99`, `:260`, `:301`, `test_video_locator.cpp:74` |

Confirmed: the outcome of the damage is a chaotic function of the packet bytes, and different
bytes reproduce the arm64-linux failures on this machine. Refuted: "make the base DSP-free and
the bytes become leg-independent". Evidence: (a) the lossless HuffYUV fixtures differ across
legs, so the difference is upstream of any lossy encoder (the generator's own frames); (b) on
x86 a whole-corpus regeneration with `-cpuflags 0` changed only `tracer_a.mp4` and
`tracer_a_copy.mp4`, so the C/SIMD split is not what separates the legs; (c) the legs fall into
three byte-identical groups that no single flag reproduces. A `-cpuflags 0` recipe would have
been a claim of leg-independence that could not be checked here. Not attempted: arm64 cannot be
run here (no binfmt/qemu).

### Fix: make the outcome byte-independent, not the bytes

`noise` damage is the only chaotic part. A family of 51 MPEG-4 encodes of the base recipe
(q:v 2..8, five frame sizes, SIMD and `-cpuflags 0`) was damaged at packet 40:

| Damage | Required outcome | Held on |
|---|---|---|
| `amount 1` (every byte overwritten, no VOP start code) | 99 frames, frame 40 missing, 41-49 differ | 51 of 51 |
| literal `30` (the first attempt) | same | 6 of 16 |
| `amount size/5` (about five bytes, header spared) | 100 frames, frames 40-49 differ | 51 of 51 |
| literal `200` (07-03's value) | same | not stable across byte sets |
| `size/4` | same | 34 of 35 |
| `size/6`, `size/8` | same | 50 of 51 each |
| `size/2` | same | 10 of 16 (the rest altered nothing) |

Real-test confirmation: with the new recipe applied to six alternative encodes of the base
(`c 4 320x240`, `simd 3 320x240`, `simd 5 316x236`, `simd 6 304x224`, `c 7 336x252`,
`simd 2 288x208`), the four affected test groups (`video_decode_edges`, `video_locator`,
`video_thread_invariance`, `video_sampling`) passed 35/35 each time. The HuffYUV locator
family (`noise` amount 50) was checked the same way: 21 of 21 outcomes across seven frame
sizes (100 frames, exactly the named frames differ, no decoder message).

Residual risk, stated plainly: the damage is statistical, not structural, for the concealed
case. About 5 altered bytes hit the VOP header with a small probability (observed 0 of 51);
that probability is per distinct byte set and the unobserved byte sets are the macOS legs'
and arm64-linux's own. No assertion, tolerance or skip was changed; the oracles (99 frames,
frame 40 missing, 41-49 differing; 100 frames, 40-49 differing) are the original literals.

### Portability pass (B), before asking for another push

- clang++-18 `-fsyntax-only -Wall -Wextra -Werror` over every translation unit of `build/x64-linux/compile_commands.json` (318) and of `build/x64-linux-vmaf` (315), using libstdc++ 13 headers (`--gcc-install-dir`) and an overlay `util/expected.h` (clang < 19 lacks CTAD for alias templates, which the project's AppleClang has). Validated: against the old `pair_scorer.h` it reproduces the CI error exactly; after the fix both passes report 0 diagnostics. Not covered: libc++-only missing includes.
- libc++ class: an include-what-you-use scan of all 112 added/modified Phase-7 files found `std::find` without `<algorithm>` (tests/integration/test_video_single_sweep.cpp) and `std::move` without `<utility>` (tests/unit/test_frame_pairing.cpp); both fixed. Pre-existing gaps (`std::int64_t` in options.h and three timeline tests) were left, they compile on `main`'s macOS legs.
- MSVC /W4 class, approximated with clang over every TU, filtered to lines added since 1696d28: `-Wshadow-all -Wshorten-64-to-32 -Wimplicit-int-conversion -Wimplicit-float-conversion -Wfloat-conversion -Wunreachable-code -Wsign-compare -Wunused-parameter -Wunused-variable -Wunused-but-set-variable -Wunused-value -Wreorder -Wuninitialized -Wsometimes-uninitialized -Wswitch -Wimplicit-fallthrough -Wcomma -Wmissing-field-initializers -Wunused-lambda-capture -Wunused-function -Wformat`: 0 diagnostics on Phase-7 lines in 318 TUs (clang has no C4334; the shifts were grepped: every other Phase-7 `<<` feeding 64 bits already uses `std::int64_t{1}`/`std::uint64_t{1}`/`1LL`). No new `std::getenv` (all `getenv_utf8`), no identifier `far`/`near`/`pascal`; `NOMINMAX` is applied to the library, the executables, both test binaries and the sweep tools, so unparenthesized `std::max`/`std::min` are safe.

### Fixes, each with its commit

| Commit | Concern |
|---|---|
| 1aa10ed | `pair_scorer.h` `vmaf_finished_` marked `[[maybe_unused]]` (kept unconditional: the define is PRIVATE to libmediadiff, guarding the member would change the class layout between the library and an including TU); `test_ssim_int.cpp:496` `i % (1U << shift)` now `i % (std::size_t{1} << shift)` (C4334) |
| 83ba1bf | `<algorithm>` in test_video_single_sweep.cpp, `<utility>` in test_frame_pairing.cpp |
| 90266aa | `scripts/gen_corpus.sh`: byte-independent damage for `video_corrupt_mpeg4.mkv` (amount 1) and `video_loc_mpeg4_c40.mkv` (`size/5`); re-predicted digest lines for those two, still provisional |

Re-predicted lines (workstation, `TZ=UTC taskset -c 0-3 bash scripts/gen_corpus.sh`; the base
`video_corrupt_mpeg4_base.mkv` is unchanged at `d1e700aa...`):

- `video_corrupt_mpeg4.mkv` `28b0537d...` -> `a26e90a2b32b68460efc513a496a6e70d2e2446ed86561bf3a055d576e7f4774`
- `video_loc_mpeg4_c40.mkv` `a9b3afc9...` -> `0ee1671eeee4222b13aa915e712347745b17b899650610381b061726945f2386`

Deviations (outside 07-15's `files_modified`): all of the above except the two golden digest
files are Rule 1/3 fixes; `scripts/gen_corpus.sh`, `src/probe/pair_scorer.h`,
`tests/unit/test_ssim_int.cpp`, `tests/integration/test_video_single_sweep.cpp` and
`tests/unit/test_frame_pairing.cpp` are not in the plan's list.

### Sweep: other Phase-7 numbers derived from lossy bytes

Fixed: only the two noise fixtures. The HuffYUV `noise` family is robust (21 of 21). Perceptual,
PSNR, SSIM and VMAF tests compare against an in-test oracle read from the decoded frames, not a
literal. Not provably safe on legs never observed (listed, not changed): the frozen/black/dark
span tests on lossy mpeg4/mjpeg/mpeg2 fixtures (`test_video_detectors`), `video_hash_*` class
evidence, the 4:2:0 `video_perc_*` thresholds. Each passed on the real arm64-linux leg of run
36928305270, passed on x64-linux, and passed on a whole-corpus `-cpuflags 0` regeneration
(1529 of 1529 locally); the macOS byte set is the one nobody has run.

### Pre-flight #2 (HEAD 90266aa, before this log's commit)

- `TZ=UTC taskset -c 0-3 bash scripts/gen_corpus.sh` exit 0; `bash scripts/check_corpus.sh`: `clean. Verified 246 fixture(s) present and non-empty under tests/fixtures/`.
- Digest: all 41 provisional names matched their committed lines (41 of 41).
- `cmake --build build/x64-linux`: `ninja: no work to do` after the last edit; `ctest --test-dir build/x64-linux`: `100% tests passed, 0 tests failed out of 1529` (17 designated-leg/VMAF-only skips).
- `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux -R "(inspect_container - golden|ts_scan_golden|size_checks - the size)"`: `100% tests passed, 0 tests failed out of 5`.
- `bash scripts/assert_corpus_digest.sh`: exit 0, `compared 244 line(s); did not compare: the mkv_opus_a.webm line, the mkv_opus_b.webm line, the CORPUS_DIGEST_SUMMARY= line.`
- No-rewrite guard: `git diff -U0 main -- tests/golden/CORPUS_DIGEST.txt | grep -cE '^-[0-9a-f]{64}  '` = 0.
- 10 lints and `scripts/test_gen_corpus_pin_gate.sh` all exit 0 (`ran 40 assertion(s) across 11 cases; 0 failure(s).`); `lint_corpus_digest_provenance.sh`: `all clauses passed.`
- VMAF: `cmake --preset x64-linux-vmaf`, build clean; `ctest --test-dir build/x64-linux-vmaf`: `100% tests passed, 0 tests failed out of 1529`; the `vmaf|quality|doc03_coverage` filter: `100% tests passed, 0 tests failed out of 42`.
- Proof: `bash scripts/gen_video_proof.sh build/video-proof` then `MEDIADIFF_REQUIRE_VIDEO_PROOF=1 ... ctest -R "integration\.video_hash_decoder"`: `100% tests passed, 0 tests failed out of 4`.
- clang-18 syntax pass after the last change: x64-linux `TUs 318 with-diagnostics 0`, x64-linux-vmaf `TUs 315 with-diagnostics 0`; MSVC-class pass `TUs 318 phase7-line diagnostics 0`.

Not run here and therefore unobserved: any Windows, macOS or arm64 execution of the new code.

## Iteration 2 - capture run 37511324806 (head e3d3d5a, conclusion failure)

Push #2 (e58df65..e3d3d5a) was human-approved and went to pull request 9. Iteration 1's
fixes held on every leg that reached Test; the one remaining failure is a Windows build break.

### Per-leg results

| Job | Result | Where it stopped |
|---|---|---|
| lint (ENG-16 boundary) | success | - |
| video-proof-streams | success | - |
| build (x64-linux), designated | success | - |
| build (arm64-linux) | success | - (the three corruption-fixture tests of run 36928305270 now pass) |
| build (arm64-osx) | success | - |
| build (x64-osx) | success | - |
| build (x64-windows-static-md), REQUIRED | failure (job 112433062073) | Build |

The iteration-1 damage recipes (`amount 1`, `size/5`) therefore held on arm64-linux, arm64-osx
and x64-osx, and the designated leg passed the corpus-digest assertion against the re-predicted
lines. Nothing was transcribed from this run: the Windows leg never reached Test, so the plan's
"every blocking leg reached and finished its Test step" condition is unmet.

### The Windows error, verbatim

```
D:\a\mediadiff\mediadiff\tests\integration\test_quality.cpp(576): error C2513: 'const std::unique_ptr<AVFrame,`anonymous-namespace'::FrameDeleter>': no variable declared before '='
D:\a\mediadiff\mediadiff\tests\integration\test_quality.cpp(576): error C2628: '`anonymous-namespace'::FramePtr' followed by 'char' is illegal (did you forget a ';'?)
D:\a\mediadiff\mediadiff\tests\integration\test_quality.cpp(579): error C2660: '`anonymous-namespace'::tapped': function does not take 0 arguments
D:\a\mediadiff\mediadiff\tests\integration\test_vmaf.cpp(708): error C2513: ... no variable declared before '='
D:\a\mediadiff\mediadiff\tests\integration\test_vmaf.cpp(708): error C2628: '`anonymous-namespace'::FramePtr' followed by 'char' is illegal (did you forget a ';'?)
D:\a\mediadiff\mediadiff\tests\integration\test_vmaf.cpp(710): error C2660: '`anonymous-namespace'::score_two_pairs': function does not take 0 arguments
FAILED: [code=2] tests/integration/CMakeFiles/mediadiff_integration_tests.dir/test_quality.cpp.obj
FAILED: [code=2] tests/integration/CMakeFiles/mediadiff_integration_tests.dir/test_vmaf.cpp.obj
```

Root cause: the Windows SDK's `<rpcndr.h>` (reached through `<windows.h>`) does `#define small char`,
so `const FramePtr small = yuv420(...)` (test_quality.cpp:576) and `const FramePtr small =
make_frame(...)` (test_vmaf.cpp:708) became `const FramePtr char = ...`. This is the same defect
class as the `far` local recorded in `.planning/WINDOWS.md` (renamed to `pcr_far` in Phase 3).
Ninja stops at the first failures, so other integration translation units may not have been
compiled on Windows in this run either; the sweep below does not rely on that.

### Fix

| Commit | Concern |
|---|---|
| 984bfc2 | `small`/`large` locals renamed `small_frame`/`large_frame` in tests/integration/test_quality.cpp and tests/integration/test_vmaf.cpp (the two broken lines); the same sweep also renamed `small` in tests/unit/test_pair_scorer.cpp (`too_small_scorer`) and tests/unit/test_video_detectors.cpp (`small_thumb`/`large_thumb`) |

Deviation (outside 07-15's `files_modified`, Rule 1): all four files. The two unit-test renames
are defensive: neither test_pair_scorer.cpp nor test_video_detectors.cpp includes a header that
reaches `<windows.h>` today, but nothing in the source guarantees that. Other translation units
do reach it: test_quality.cpp and test_vmaf.cpp through cli_harness.h -> process_spawn.h, and six
unit translation units (test_console_vt.cpp, test_dir_pairing.cpp, test_fs_utf8.cpp,
test_golden.cpp, test_process_spawn.cpp, test_registry_generator.cpp). (Corrected by the
orchestrator before push #3; the first wording said no unit translation unit reaches it.)

### Sweep for the whole class

Phase-7-changed set: `git diff --name-only 1696d28 -- '*.cpp' '*.h'` = 113 files.

1. Mechanical pass. A scratch header (outside the repo) `#define`s the SDK's object-like macros
   the way the SDK does: `small`, `hyper`, `near`, `far`, `NEAR`, `FAR`, `pascal`, `cdecl`, `IN`,
   `OUT`, `OPTIONAL`, `CALLBACK`, `WINAPI`, `APIENTRY`, `CONST`, `VOID`, `DELETE`, `ERROR`,
   `OPAQUE`, `TRANSPARENT`, `ABSOLUTE`, `RELATIVE`, `IGNORE`, `INFINITE`, `interface`, `TRUE`,
   `FALSE`, `NO_ERROR`, `MAX_PATH`, and about 60 A/W macros (`GetObject`, `CreateFile`,
   `DeleteFile`, `CopyFile`, `MoveFile`, `CreateWindow`, `MessageBox`, `DrawText`, `LoadImage`,
   `SendMessage`, `GetMessage`, `GetCurrentTime`, `Yield`, `GetUserName`, `FormatMessage`, ...).
   clang++-18 `-fsyntax-only -Wall -Wextra -Werror -include <header>` over every translation unit
   of `build/x64-linux` (318) and `build/x64-linux-vmaf` (315), libstdc++ 13 and the iteration-1
   `util/expected.h` overlay. Diagnostics reported only from Phase-7-changed files.
   - Validation of the detector: with the header and the pre-fix sources (`git show e3d3d5a:...`)
     it reproduces the Windows error on test_quality.cpp:576 (`cannot combine with previous
     'type-name' declaration specifier`) and finds the same defect at test_pair_scorer.cpp:315
     and test_video_detectors.cpp:265. Without the header the same pass reports 0 diagnostics.
   - Result after the fix: 0 diagnostics in Phase-7-changed files, in both builds, and no
     third-party or system header aborted a translation unit. The only translation unit that
     fails with the header is the unchanged, pre-Phase-7 tests/unit/test_mp4_analyzer.cpp
     (`const Fingerprint far = ...` at :158). It compiles on `main`'s Windows leg because that
     translation unit does not reach `<windows.h>`; it was left alone.
2. Grep pass (catches silent cases such as a macro expanding to nothing). Comments and string
   literals stripped, every identifier equal to a macro name above or to a Windows
   typedef/constant (`BOOL`, `BYTE`, `DWORD`, `HANDLE`, `LPWSTR`, `GENERIC_WRITE`, ...) in the 113
   files: the only hits left are the intended Windows API uses in `src/cli/main.cpp`,
   `src/cli/commands/{compare,dir,snapshot}.cpp` (`HANDLE`, `TRUE`, `BOOL`, `DWORD`, `INFINITE`,
   `LPWSTR`, `GENERIC_WRITE`), all inside the existing `_WIN32` blocks and all already on `main`'s
   Windows leg. Findings before the renames: a local named `small` in four files (test_quality,
   test_vmaf, test_pair_scorer, test_video_detectors), nothing else.
3. Not covered: a macro defined by a header reached only on Windows that is not in the list.
   No Windows build was run here.

### Pre-flight #3 (HEAD 984bfc2, before this log's commit)

- `TZ=UTC taskset -c 0-3 bash scripts/gen_corpus.sh` exit 0; `bash scripts/check_corpus.sh`: `clean. Verified 246 fixture(s) present and non-empty under tests/fixtures/, derived from scripts/gen_corpus.sh.`
- Digest: all 41 provisional names matched their committed CORPUS_DIGEST.txt lines (41 of 41; the name set equals iteration 1's), and the whole 246-line listing and `CORPUS_DIGEST_SUMMARY=9f48dc2ace4b539f16329a7abee4f13ddb61423ce8635c3d0033e445db3209f6` are identical to pre-flight #2. This iteration touched no fixture or recipe.
- `cmake --build build/x64-linux`: 6 steps (the four changed test files and two links), no `error`/`FAILED:`; `ctest --test-dir build/x64-linux`: `100% tests passed, 0 tests failed out of 1529` (17 designated-leg/VMAF-only skips).
- `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux -R "(inspect_container - golden|ts_scan_golden|size_checks - the size)"`: `100% tests passed, 0 tests failed out of 5`.
- `bash scripts/assert_corpus_digest.sh` exit 0, `compared 244 line(s)`; no-rewrite guard `git diff -U0 main -- tests/golden/CORPUS_DIGEST.txt | grep -cE '^-[0-9a-f]{64}  '` = 0.
- All 11 lints PASS (the ci.yml order, including `test_gen_corpus_pin_gate`: `ran 40 assertion(s) across 11 cases; 0 failure(s).`); `lint_corpus_digest_provenance.sh`: `all clauses passed.`
- VMAF: `cmake --preset x64-linux-vmaf` and build clean; `ctest --test-dir build/x64-linux-vmaf`: `100% tests passed, 0 tests failed out of 1529`; the `vmaf|quality|doc03_coverage` filter: `100% tests passed, 0 tests failed out of 42`.
- Proof: `bash scripts/gen_video_proof.sh build/video-proof` exit 0, then `MEDIADIFF_REQUIRE_VIDEO_PROOF=1 ... ctest -R "integration\.video_hash_decoder"`: `100% tests passed, 0 tests failed out of 4`.
- clang-18 syntax pass after the last change (`-Wall -Wextra -Werror`, no macro header): x64-linux `TUs 318 with-diagnostics 0`, x64-linux-vmaf `TUs 315 with-diagnostics 0`; with the macro header `diagnostics-in-changed-files 0` on both.

Not run here and therefore unobserved: any Windows execution of the new code. The Windows leg has
not yet reached its Test step for the Phase-7 tests, and macOS/arm64 execution of the code
after e3d3d5a is the same code as run 37511324806 plus this rename.

## Iteration 3 - next capture

(appended by the continuation that reads the run produced by the push of the head above)
