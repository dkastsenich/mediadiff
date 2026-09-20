---
phase: 06-audio-analysis
plan: 06
subsystem: audio-analysis
tags: [ffmpeg, priming, av-offset, resolve_priming, precedence-chain, mp4-elst, mkv-codec-delay]

requires:
  - phase: 05-timeline-analysis
    provides: "resolve_priming()'s two-tier resolver (skip_samples/initial_padding) and the {state, source, samples} evidence shape timeline.av_offset already established"
provides:
  - "resolve_priming() extended in place to a four-tier resolver: skip_samples -> initial_padding -> container-mechanism tier (mp4_edit_list/mp4_itunsmpb/mkv_codec_delay) -> unknown, with padding_samples and conflicting_readings"
  - "StreamPacketScan::last_packet_discard_padding -- trailing AV_PKT_DATA_SKIP_SAMPLES half, captured in the existing sweep"
  - "audio.priming: the check that makes unknown a comparable value, closing Phase 5's priming precedence chain"
  - "audio.priming's declared trigger/clean fixture pair and the swept re-baseline of every MP4-to-TS declared finding set it moved"
affects: [06-07-av-drift-span-fix]

actuals:
  tokens: 21600
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Container-mechanism tier consulted for RESOLUTION only on presence (not magnitude), so a declared zero stays distinguishable from absent -- the same discipline as the packet-level tiers"
    - "Rule 2 unit test added directly against an analyzer's own run() (bypassing the CLI) when the real binary has no practical way to force a partial-scan skip path against a corpus-sized fixture -- mirrors test_audio_stream_params.cpp's own established Test 8 pattern"

key-files:
  created:
    - src/analyzers/audio/priming.cpp
    - docs/checks/audio.priming.md
    - tests/unit/test_priming_resolver.cpp
    - tests/unit/test_audio_priming.cpp
    - tests/integration/test_audio_priming.cpp
  modified:
    - src/analyzers/timeline/analyzers.h
    - src/analyzers/timeline/av_sync.cpp
    - src/analyzers/audio/analyzers.h
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_timeline_av_sync.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/integration/test_timeline_structure.cpp
    - tests/golden/list_checks_effective.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_packet_scan.cpp
    - tests/integration/CMakeLists.txt

key-decisions:
  - "Container-mechanism tier is presence-gated, not magnitude-gated -- consulted whenever tiers 1-2 report nothing, regardless of whether the container reading is itself zero, so declared-zero and absent stay distinguishable end to end."
  - "audio_prime_multiedit.mp4's fold does NOT hold (06-RESEARCH.md A3, flagged assumption A2): tiers 1-2 both report nothing, and the container tier correctly resolves through mp4_edit_list to a real, declared 0 rather than falling to unknown. audio_prime_fragmented.mp4's fold DOES hold: tier 1 (skip_samples=1024) resolves directly, container tier never consulted."
  - "DEVIATION: substituted audio_prime_roundtrip.mkv for audio_prime_roundtrip2.mp4 as the must-pass round-trip pair everywhere the plan's literal text names the latter (Task 2 acceptance criterion, Task 2 Behavior Test 4, Task 3's DOC-03 clean pair). Measured directly against this project's own linked FFmpeg 8.1, audio_prime_roundtrip2.mp4 (MP4->MKV->MP4) reports \"1014\" vs the base's \"1024\" -- a genuine ~10-sample CodecDelay-ns rounding artifact, not a bug -- exactly the risk 06-02-SUMMARY.md's own Next Phase Readiness note flagged in advance. audio.priming is registered exact over a string (D-14), so no tolerance can absorb this, and regenerating the fixture would rewrite an existing CORPUS_DIGEST.txt line (forbidden). The roundtrip2.mp4 result is asserted and documented as real evidence, not silently avoided."
  - "Discovered and documented: the system /usr/local/bin/ffprobe (a recent GPL FFmpeg snapshot, N-126086) is NOT representative of this project's linked vcpkg FFmpeg 8.1 for exact discard_padding/side-data behavior. An initial test assertion built from system-ffprobe evidence failed against the real binary; re-derived by compiling and linking a standalone probe directly against build/x64-linux/vcpkg_installed/x64-linux/lib/libavformat.a et al. This is the exact class of trap 06-RESEARCH.md already warned about (re-verify every decoder/bit-exactness claim against the linked 8.1, never the generator/system ffmpeg)."

requirements-completed: [AUDIO-04]

coverage:
  - id: D1
    description: "resolve_priming() extended with the container-mechanism tier, trailing padding and conflicting-readings evidence, all four tiers unit-tested"
    requirement: "AUDIO-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_priming_resolver.cpp (9 TEST_CASEs, tiers 1-4, boundary/precision edges)"
        status: pass
    human_judgment: false
  - id: D2
    description: "audio.priming registered: unknown as a comparable value, {state, source, samples, padding} evidence, mechanism-vs-effect layering across MP4->MKV"
    requirement: "AUDIO-04"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_priming.cpp (9 TEST_CASEs against the real CLI)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_priming.cpp (partial_scan skip-reason priority)"
        status: pass
    human_judgment: false
  - id: D3
    description: "audio.priming's DOC-03 trigger/clean pair declared, and every MP4-to-TS/MKV declared finding set the new check moved is re-baselined with a written cause"
    requirement: "AUDIO-04"
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp, test_timeline_av_sync.cpp, test_timeline_start_duration.cpp, test_timeline_structure.cpp -- full ctest suite 1123/1123 passed"
        status: pass
    human_judgment: false

duration: 47min
completed: 2026-09-20
status: complete
---

# Phase 06 Plan 06: audio.priming -- unknown as a comparable value Summary

**Extended `resolve_priming()` to a four-tier resolver with a container-mechanism edge-case tier, and registered `audio.priming` so a lost or gained priming signal is a real, comparable finding instead of a silent skip.**

## Performance

- **Duration:** 47 min (measured from 06-05's own completion timestamp to this plan's completion)
- **Completed:** 2026-09-20T16:38:03Z
- **Tasks:** 3
- **Files modified:** 21 (5 created, 16 modified)

## Accomplishments

- `PrimingResult`/`resolve_priming()` (`src/analyzers/timeline/analyzers.h`/`av_sync.cpp`) extended in place with `Source::mp4_edit_list`/`mp4_itunsmpb`/`mkv_codec_delay` (appended, never reordered), `padding_samples` and `conflicting_readings` -- the container-mechanism tier is consulted for resolution only when the packet-level and codecpar-level tiers both report nothing, and for evidence always.
- `StreamPacketScan::last_packet_discard_padding` captures the trailing half of `AV_PKT_DATA_SKIP_SAMPLES` from the LAST packet carrying it, in the same sweep that already reads the leading half -- no second read, no new pass.
- `audio.priming` registered (`src/analyzers/audio/priming.cpp`): value is a string (the decimal sample count, or the literal `"unknown"`), evidence is `{state, source, samples, padding}` plus the raw container reading and any `conflicting_readings`. Never skips for unknown priming (D-14); only skips `insufficient_data`/`partial_scan`.
- Measured the two 06-RESEARCH.md A3 edge fixtures directly (flagged assumption A2, resolved on evidence): `audio_prime_fragmented.mp4`'s libav fold HOLDS (resolves via tier 1 directly, `skip_samples=1024`); `audio_prime_multiedit.mp4`'s fold does NOT hold (tiers 1-2 report nothing; the container tier correctly recovers a genuine, declared `0` from the real `elst` entry rather than falling to `unknown`).
- `docs/checks/audio.priming.md` written, including the doc 05 section 2 tier-order/tolerance amendment and a new "Round-trip stability, and its limit" section documenting the single-hop-vs-double-hop distinction this plan's own execution measured.
- `audio.priming`'s DOC-03 declared pair added (running total 85 -> 86), and every existing MP4-to-TS declared finding set the new check moved (three call sites across two files, one found only via the required sweep) re-baselined with a written causal reason.
- Full verification suite green: `gen_corpus.sh`/`check_corpus.sh`/`lint_corpus_digest_provenance.sh`, `ctest` 1123/1123 passed, `lint_eng16.sh`, `lint_check_id_strings.sh`. Exactly one declaration and one definition of `resolve_priming` in the tree.

## Task Commits

1. **Task 1: extend `resolve_priming()` with the container-mechanism tier, trailing padding and conflict evidence** - `5233130` (feat)
2. **Task 2: the `audio.priming` check** - `728482c` (feat)
3. **Task 3: the DOC-03 pair and the declared-set re-baselining** - `a86d1f4` (test)

_No TDD RED/GREEN/REFACTOR split commits: this plan's `tdd="true"` tasks (1 and 2) were executed test-and-implementation-together per task, matching this project's own established per-plan commit granularity for `execute`-type plans; each commit's diff includes both the new/extended test file and its corresponding implementation._

## Files Created/Modified

- `src/analyzers/timeline/analyzers.h` - `PrimingResult` extended: 3 new `Source` arms, `padding_samples`, `conflicting_readings`, `resolve_priming()`'s extended signature
- `src/analyzers/timeline/av_sync.cpp` - `resolve_priming()`'s definition, extended tier resolution and conflict detection; `priming_source_to_string()` extended exhaustively
- `src/probe/packet_scan.h` / `.cpp` - `StreamPacketScan::last_packet_discard_padding`, captured from the existing `AV_PKT_DATA_SKIP_SAMPLES` sweep
- `src/analyzers/audio/analyzers.h` / `priming.cpp` - `audio_priming_analyzer()` declaration and full implementation
- `src/probe/orchestrator.cpp` - registers `audio_priming_analyzer()` in `all_analyzers()`
- `src/core/checks.def` - `audio.priming` check registration (`group=audio, semantic=exact, unit=samples, value_kind=string, severity=fail`)
- `CMakeLists.txt` - adds `src/analyzers/audio/priming.cpp` to `libmediadiff`
- `docs/checks/audio.priming.md` - full check doc: tier chain, evidence shape, round-trip-stability limit, doc 05 amendment, Accept/Tune/Silence
- `tests/unit/test_priming_resolver.cpp` - 9 `TEST_CASE`s covering all four tiers, the boundary edge (zero vs absent) and two `priming_samples_to_ticks` precision edges
- `tests/unit/test_audio_priming.cpp` - Rule 2 addition: `partial_scan` skip-reason priority, driven directly against `audio_priming_analyzer()`'s own `run()`
- `tests/integration/test_audio_priming.cpp` - 9 `TEST_CASE`s against the real CLI covering all of Task 2's behavior tests, including the documented roundtrip2.mp4 deviation
- `tests/integration/test_doc03_coverage.cpp` - `audio.priming`'s declared trigger/clean pair, running total 85 -> 86
- `tests/integration/test_timeline_av_sync.cpp`, `test_timeline_start_duration.cpp`, `test_timeline_structure.cpp` - re-baselined declared sets (see Decisions/Deviations)
- `tests/golden/list_checks_effective.txt` - new `audio.priming` row (ENG-12 golden)
- `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` - new test file registrations

## Decisions Made

- Container-mechanism tier is presence-gated, not magnitude-gated (boundary-edge correctness, D-14/D-15) -- see `key-decisions` in frontmatter for full reasoning.
- `audio_prime_multiedit.mp4`/`audio_prime_fragmented.mp4` measured rather than assumed (flagged assumption A2 resolved on evidence) -- see frontmatter.
- Fixture substitution for the round-trip-stability proof (`audio_prime_roundtrip.mkv` instead of `audio_prime_roundtrip2.mp4`) -- full reasoning in Deviations below and frontmatter.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected `test_packet_scan.cpp` assertions built from system-ffprobe evidence that disagreed with the actual linked FFmpeg 8.1**
- **Found during:** Task 1
- **Issue:** An initial assertion (`tracer_a.mp4` → `last_packet_discard_padding == 888`) was derived from `/usr/local/bin/ffprobe` output and failed against the real binary (`0 == 888`). Root-caused by compiling and linking a standalone probe tool directly against `build/x64-linux/vcpkg_installed/x64-linux/lib/libavformat.a` (this project's own linked FFmpeg 8.1): the system `ffprobe` is version `N-126086-ge5ecfe8970-20260812`, a materially newer, GPL-enabled build whose `mov.c` trailing-`discard_padding` computation genuinely differs from the linked 8.1's. This is exactly the trap `06-RESEARCH.md`/`06-CONTEXT.md` warn about: re-verify every decoder/bit-exactness claim against the linked libraries, never the generator/system binary.
- **Fix:** Removed temporary debug instrumentation, re-derived the correct expected values with the linked-8.1 probe tool: `tracer_a.mp4` carries only one side-data packet (start=1024, end=0), corrected the assertion to expect `0`. Found `audio_prime_roundtrip.mkv` genuinely carries a nonzero trailing value (820) on its last side-data packet and added a new, correctly-verified `TEST_CASE` using it instead.
- **Files modified:** `tests/unit/test_packet_scan.cpp`
- **Verification:** `ctest -R "unit\.(priming_resolver|av_sync|av_drift|packet_scan)"` -- 59/59 passed.
- **Committed in:** `5233130` (Task 1 commit)

**2. [Rule 2 - Missing coverage] Added a unit-level test for `audio.priming`'s `partial_scan` skip-reason priority**
- **Found during:** Task 2
- **Issue:** Task 2's own Behavior Test 9 requires "a truncated scan emits `skipped:partial_scan` ahead of it." The real CLI has no practical way to force a genuine partial packet scan against any fixture small enough to belong in this corpus -- a 1MB `--probe-memory-budget-mb` is already generous enough not to truncate `audio_prime_base.mp4`.
- **Fix:** Added `tests/unit/test_audio_priming.cpp` (not in the plan's declared `files_modified`), calling `audio_priming_analyzer()`'s own `run()` directly against a hand-truncated `PacketScanResult`, mirroring `tests/unit/test_audio_stream_params.cpp`'s own established Test 8 pattern exactly.
- **Files modified:** `tests/unit/test_audio_priming.cpp` (new), `tests/unit/CMakeLists.txt`
- **Verification:** `ctest -R "unit\.audio_priming"` -- 2/2 passed.
- **Committed in:** `728482c` (Task 2 commit)

**3. [Rule 1/Rule 4-adjacent, documented rather than silently worked around] `audio_prime_roundtrip2.mp4` does not satisfy the plan's own literal "must-pass round trip" acceptance criterion**
- **Found during:** Task 2
- **Issue:** Task 2's acceptance criteria and Behavior Test 4 both require `mediadiff compare tests/fixtures/audio_prime_base.mp4 tests/fixtures/audio_prime_roundtrip2.mp4 --json` to report `audio.priming` as `pass`. Measured directly against the real binary (linked FFmpeg 8.1), it reports `"1024"` vs `"1014"` -- a genuine, real, non-flaky result (reproduced identically across repeated runs). Root cause: `audio_prime_roundtrip2.mp4` is an MP4→MKV→MP4 double hop; the MKV intermediate stores priming as `CodecDelay` in nanoseconds, and converting `1024/44100s` to nanoseconds and back to an MP4 edit-list `media_time` loses ~10 samples to rounding. `06-02-SUMMARY.md`'s own "Next Phase Readiness" note flagged exactly this risk in advance: "06-06 should treat this as data to measure a tolerance against, not assume away."
- **Why not a numeric-tolerance fix:** `audio.priming` is registered `semantic=exact` over a `value_kind=string` (D-14 forces this shape specifically so `unknown` can compare as its own value) -- `src/compare/tol.cpp`'s numeric tolerance machinery has no path onto a string value, and introducing one would be an architectural change directly contradicting D-14's design intent (Rule 4 territory this plan's own `must_haves.prohibitions` rules out: "Never soften a severity or widen a tolerance because priming was unknown" generalizes to "no expressible tolerance exists for this value shape at all").
- **Why not regenerating the fixture:** `audio_prime_roundtrip2.mp4` already has a real, committed hash line in `tests/golden/CORPUS_DIGEST.txt` (from 06-02); this project's hard constraint is that an EXISTING digest line is never rewritten/regenerated.
- **Resolution:** Documented the real, measured result as fact rather than silently avoiding it. `tests/integration/test_audio_priming.cpp`'s Test 4 asserts the ACTUAL "1024" vs "1014" fail result (with an extensive top-of-file comment explaining why), and `audio_prime_roundtrip.mkv` (the single MP4→MKV hop, already named by the plan's own Behavior Test 5) is used as the fixture wherever a genuine must-pass round-trip-stability proof is needed: Task 2's Test 5, and Task 3's DOC-03 clean pair. This mirrors `06-02-SUMMARY.md`'s own established precedent of substituting a fixture/vehicle when it structurally cannot deliver its literally-stated purpose, documented with a code-level measurement rather than a silent rename or fabricated pass.
- **Files modified:** `tests/integration/test_audio_priming.cpp`, `tests/integration/test_doc03_coverage.cpp`
- **Verification:** `mediadiff compare tests/fixtures/audio_prime_base.mp4 tests/fixtures/audio_prime_roundtrip2.mp4 --json` → `"baseline": "1024", "candidate": "1014", "status": "fail"` (reproduced 3x). `mediadiff compare tests/fixtures/audio_prime_base.mp4 tests/fixtures/audio_prime_roundtrip.mkv --json` → `"baseline": "1024", "candidate": "1024", "status": "pass"`, with `container_reading.source` = `mp4_edit_list` vs `mkv_codec_delay` respectively.
- **Committed in:** `728482c` (Task 2 commit), `a86d1f4` (Task 3 commit, DOC-03 pair)

**4. [Rule 2 - Required sweep found an unnamed affected set] `test_timeline_structure.cpp`'s dts_backward pair also gains `audio.priming`**
- **Found during:** Task 3
- **Issue:** Task 3's action text requires sweeping "every existing declared finding set that involves an MP4-to-TS or MP4-to-MKV pair," not only the two call sites its own `read_first` list names (`test_timeline_av_sync.cpp`, `test_timeline_start_duration.cpp`). Running the full suite after Task 2 surfaced a third failure: `test_timeline_structure.cpp`'s `timeline_start_base.mp4` vs `timeline_dts_backward.ts` pair (a two-segment MPEG-TS splice/re-encode) also newly reports a non-pass `audio.priming` (known "1024" vs the re-encode's `"unknown"`).
- **Fix:** Verified empirically against the real binary, then added `audio.priming` to that test's declared set with a written causal reason (the same splice/re-encode root cause every other member of that set already cites, D-02).
- **Files modified:** `tests/integration/test_timeline_structure.cpp`
- **Verification:** Full `ctest` suite, 1123/1123 passed.
- **Committed in:** `a86d1f4` (Task 3 commit)

**5. [Rule 1 - Bug] `ENG-12`'s `list_checks_effective` golden needed the new check row**
- **Found during:** Task 2 (surfaced during the full-suite verification pass)
- **Issue:** Registering `audio.priming` in `checks.def` changed `mediadiff list-checks --effective`'s output by one line; `tests/golden/list_checks_effective.txt` is a committed, read-only golden and failed the byte-identical comparison.
- **Fix:** Appended the new `audio.priming  severity=fail  tolerance=<none>` line in registration order (immediately after `audio.profile`, matching `checks.def`'s own declaration order).
- **Files modified:** `tests/golden/list_checks_effective.txt`
- **Verification:** `ctest -R "integration\.list_checks"` -- 7/7 passed.
- **Committed in:** `728482c` (Task 2 commit)

---

**Total deviations:** 5 (2 auto-fixed bugs, 1 Rule 2 test coverage addition, 1 Rule 2 sweep-found declared-set addition, 1 documented empirical-vs-literal-acceptance-criterion resolution)
**Impact on plan:** No scope creep beyond what correctness and the plan's own required sweep demanded. The roundtrip2.mp4 deviation is the only one that changes a literal plan acceptance criterion's outcome, and it is fully measured, reproducible, and documented rather than silently worked around -- consistent with 06-02-SUMMARY.md's own advance warning that this exact risk existed and would need 06-06's own judgment call.

## Issues Encountered

None beyond the deviations documented above -- all were caught and resolved via this plan's own `<verify>` blocks or the project's pre-existing lint/test suite before committing.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `resolve_priming()`'s extended `Source` enum and `conflicting_readings` mechanism are ready for 06-07's `av_drift` span fix to consume (ROADMAP SC2 scopes both to this phase).
- Every declared finding set `audio.priming` moved is re-baselined and green, so 06-07 -- which moves some of the SAME MP4-to-TS/MKV pairs again -- starts from a known baseline rather than rediscovering this phase's own churn.
- `audio_prime_roundtrip2.mp4`'s real "1014" vs "1024" result is now a documented, asserted fact (`tests/integration/test_audio_priming.cpp` Test 4) rather than a silent gap -- if a future FFmpeg release changes this rounding behavior, that assertion is the single place to update alongside this summary's own Deviations section.
- No blockers for the next wave of plans.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*
