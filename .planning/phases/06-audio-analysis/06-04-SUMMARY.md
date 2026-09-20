---
phase: 06-audio-analysis
plan: 04
subsystem: audio
tags: [ffmpeg, aac, he-aac, sbr, mpeg4audio, header-pass, demux, catch2]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-01's AudioSpecificConfig object-type reader (aac_fixed USAC pre-check), 06-02's hand-written audio_sbr_explicit.mp4/audio_sbr_implicit.mp4 fixture pair, 06-03's per-audio-stream identity checks and StreamInfo shape"
provides:
  - "resolve_sbr_signaling() and SbrSignaling{none, explicit_asc, implicit_decoded, unknown} in src/probe/audio_config.h/.cpp, a libav-free decision function taking an injected SbrProbeFn"
  - "AudioSpecificConfig extended with has_explicit_sbr, extension_sampling_frequency_hz, full sampling_frequency_index/channel_configuration"
  - "DemuxSession's bounded, wall-clock-limited one-packet SBR probe decode via a second, throwaway AVFormatContext (probe_implicit_sbr_via_second_open)"
  - "StreamInfo::sbr_signaling and StreamInfo::effective_sample_rate_hz, resolved once in the header pass"
  - "audio.profile check: codec profile plus HE-AAC SBR signaling mode, pass-independent"
affects: [audio-analysis, header-pass, demux-session]

actuals:
  tokens: 20767
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Dependency-injected SbrProbeFn callback (std::function) keeps the pure decision logic in audio_config.cpp libav-free and unit-testable via a call-counting fake, while the REAL bounded decode lives in demux_session.cpp"
    - "Second, throwaway AVFormatContext isolation for a bounded probe decode (mirrors reprobe_ts_declared_durations()'s existing precedent) -- never disturbs the primary DemuxSession's own packet-read position (PROBE-08)"
    - "open_context()'s disarm_interrupt_after_open parameter lets a second, nested open+decode sequence stay genuinely wall-clock-bounded by the same interrupt mechanism, instead of always disarming immediately after avformat_find_stream_info()"
    - "Cache the probe's own directly-observed decoded rate (never a formulaic doubling) -- avformat_find_stream_info()'s internal probing can already resolve codecpar->sample_rate to the SBR-doubled value for a short stream, so a literal core_rate*2 risks fabricating a false rate"

key-files:
  created:
    - src/probe/audio_config.h (extended)
    - src/probe/audio_config.cpp (extended)
    - docs/checks/audio.profile.md
    - tests/unit/test_audio_config.cpp
    - tests/integration/test_audio_profile_sbr.cpp
  modified:
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/analyzers/audio/stream_params.cpp
    - src/core/checks.def
    - docs/checks/audio.sample_rate.md
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "SBR signaling resolved entirely in DemuxSession::open()'s header pass (compute_sbr_signaling()), never from the decode pass's own results, so audio.profile's value is identical under snapshot, compare --content and compare --no-content (D-12)."
  - "Explicit signaling (top-level AOT_SBR or a 0x2b7 backward-compatible sync extension) costs no decode at all; only a bare AOT_AAC_LC ASC -- genuinely ambiguous between implicit SBR and plain LC -- triggers the bounded one-packet probe decode."
  - "effective_sample_rate_hz always reflects the probe's own directly-observed decoded rate for implicit_decoded, never a formulaic core_rate*2 -- see Deviations below."
  - "mediadiff's own ASC bit reader parses object-type/sampling-frequency escapes and the SBR extension fields; no avpriv_* symbol is ever referenced."

patterns-established:
  - "A dependency-injected probe callback (SbrProbeFn) is the seam that keeps a libav-free decision module unit-testable against a real bounded decode implemented elsewhere."

requirements-completed: [AUDIO-01, AUDIO-03]

coverage:
  - id: D1
    description: "audio.profile resolves HE-AAC SBR signaling (explicit/implicit/none/unknown) with a no-decode fast path for explicit and a bounded one-packet decode for the genuinely ambiguous bare AOT_AAC_LC case"
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp (13 TEST_CASEs, unit.audio_config)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_profile_sbr.cpp (8 TEST_CASEs, integration.audio_profile_sbr)"
        status: pass
    human_judgment: false
  - id: D2
    description: "audio.profile's value is pass-independent -- identical under snapshot, compare --content and compare --no-content"
    requirement: "AUDIO-03"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_profile_sbr.cpp - audio_sbr_implicit.mp4 reports the SAME audio.profile value under --content and --no-content"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_profile_sbr.cpp - a snapshot of audio_sbr_implicit.mp4 reports the SAME audio.profile value as a live compare"
        status: pass
    human_judgment: false
  - id: D3
    description: "audio.profile registered in the check registry with docs/checks/audio.profile.md, DOC-03 declared_pairs coverage, and the ENG-12 golden updated"
    requirement: "AUDIO-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp - every registered check has a declared triggering fixture pair and a declared clean one"
        status: pass
      - kind: integration
        ref: "tests/integration/test_list_checks.cpp - ENG-12: --effective is byte-identical across two runs, checked against a golden"
        status: pass
    human_judgment: false

duration: 39min
completed: 2026-09-20
status: complete
---

# Phase 6 Plan 4: HE-AAC SBR Signaling and `audio.profile` Summary

**`audio.profile` carrying HE-AAC SBR signaling mode (explicit/implicit/none/unknown) via a no-decode ASC fast path and a bounded one-packet decode fallback, resolved entirely in the header pass so the value is pass-independent.**

## Performance

- **Duration:** 39 min
- **Started:** 2026-09-20T14:07:52Z
- **Completed:** 2026-09-20T14:46:43Z
- **Tasks:** 2
- **Files modified:** 14 (5 created, 9 modified)

## Accomplishments
- `resolve_sbr_signaling()` and `SbrSignaling{none, explicit_asc, implicit_decoded, unknown}`, a libav-free decision function over an injected `SbrProbeFn`, unit-tested via a call-counting fake to prove the explicit path never decodes and the ambiguous path decodes exactly once.
- Extended mediadiff's own ASC bit reader to detect both the top-level `AOT_SBR` object type and the legacy `0x2b7` backward-compatible sync-extension tail, bounds-checked against `extradata_size` throughout.
- A bounded, wall-clock-limited one-packet SBR probe decode via a second, throwaway `AVFormatContext` (mirrors the existing `reprobe_ts_declared_durations()` isolation pattern), never disturbing the primary `DemuxSession`'s own packet-read position.
- `audio.profile` registered end to end: codec profile name plus SBR signaling suffix, proven pass-independent across `snapshot`/`compare --content`/`compare --no-content`, with the third "plain LC, no SBR at all" bucket visibly distinct from "implicit SBR".

## Task Commits

Each task was committed atomically:

1. **Task 1: `resolve_sbr_signaling()` — the no-decode ASC fast path and the bounded one-packet fallback** - `163fee4` (feat)
2. **Task 2: `audio.profile` carrying the signaling mode, with the hand-written explicit/implicit fixture pair** - `29dd738` (feat)

**Plan metadata:** (this commit)

_Note: both tasks were `tdd="true"`; tests were written and iterated alongside the implementation within each task's single commit rather than as separate RED/GREEN commits, matching 06-01/06-02/06-03's own established plan-level TDD granularity for this phase._

## Files Created/Modified
- `src/probe/audio_config.h` - `SbrSignaling`, `SbrProbeFn`, `SbrProbeDecodeResult`, `kMaxSbrProbePackets`, extended `AudioSpecificConfig`, `resolve_sbr_signaling()` declaration
- `src/probe/audio_config.cpp` - ASC bit-reader extensions (`peek`, `bits_left`, shared `read_object_type`/`read_sampling_frequency` helpers), the `0x2b7` sync-extension scan, `resolve_sbr_signaling()`
- `src/probe/demux_session.h` - `StreamInfo::sbr_signaling`/`::effective_sample_rate_hz`, `DemuxSession`'s cached `sbr_signaling_`/`implicit_probe_rate_hz_`, `compute_sbr_signaling()` declaration
- `src/probe/demux_session.cpp` - `probe_implicit_sbr_via_second_open()` (the real bounded decode), `open_context()`'s new `disarm_interrupt_after_open` parameter, `compute_sbr_signaling()`, `stream_info()`'s effective-rate computation
- `src/analyzers/audio/stream_params.cpp` - `emit_profile()`, `render_sbr_suffix()`, `sbr_signaling_name()`, updated `emit_sample_rate()` evidence
- `src/core/checks.def` - `audio.profile` registration
- `docs/checks/audio.profile.md` - new, including the doc 05 §2.1 amendment record (D-12)
- `docs/checks/audio.sample_rate.md` - evidence-shape description corrected (see Deviations)
- `tests/unit/test_audio_config.cpp` - 13 unit TEST_CASEs (synthesized ASC fixtures plus real-fixture DemuxSession tests)
- `tests/integration/test_audio_profile_sbr.cpp` - 8 integration TEST_CASEs against the real CLI
- `tests/integration/test_doc03_coverage.cpp` - `audio.profile` `declared_pairs()` entry, running total 84 → 85
- `tests/integration/CMakeLists.txt` / `tests/unit/CMakeLists.txt` - new test files wired in
- `tests/golden/list_checks_effective.txt` - regenerated for the new registered check (ENG-12)

## Decisions Made
- SBR signaling resolution lives entirely in the header pass (`DemuxSession::compute_sbr_signaling()`, called from `DemuxSession::open()`), never in the decode pass — the single decision that makes `audio.profile` pass-independent (D-12).
- The bounded probe decode captures its OWN directly-observed decoded rate for `effective_sample_rate_hz`, rather than deriving it formulaically from the core rate. See Deviations for why.
- `audio.sample_rate`'s compared value stays `codecpar`'s own rate (`core_rate_hz`) unchanged; the effective rate rides only in evidence, so the same file never reports two different `audio.sample_rate` values depending on signaling mode.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `effective_sample_rate_hz` no longer derived as a formulaic `core_rate * 2`**
- **Found during:** Task 1, verification against the real CLI and real fixtures
- **Issue:** The plan's literal Test 8 and the frontmatter's own must-have truth ("the compared value itself stays `codecpar`'s rate, which 06-RESEARCH.md Q4 proved is the UNDOUBLED base rate for an implicitly signaled stream") assume `codecpar->sample_rate` always stays undoubled for an implicitly-signaled stream, so `effective_sample_rate_hz = core_rate * 2` seemed like the correct implicit-case formula. Empirically, against `tests/fixtures/audio_sbr_implicit.mp4`, `avformat_find_stream_info()`'s own internal probing ALREADY resolves `codecpar->sample_rate` to the SBR-doubled value (88200, not the ASC-declared 44100) — reproducible identically with and without `--content`, entirely within the header pass. Doubling this already-doubled value would have fabricated a false, quadrupled 176400 Hz.
- **Fix:** Added `DemuxSession::implicit_probe_rate_hz_`, populated in `compute_sbr_signaling()` by capturing the bounded probe's own `decoded_sample_rate_hz` (a side effect observable after `resolve_sbr_signaling()` returns, since the probe runs at most once). `stream_info()` reads this cached, actually-decoded rate directly for `implicit_decoded` streams instead of computing `core*2`. This is correct regardless of whether `codecpar` already carries the doubled value or not.
- **Files modified:** `src/probe/demux_session.h`, `src/probe/demux_session.cpp`, `tests/unit/test_audio_config.cpp`, `docs/checks/audio.sample_rate.md`
- **Verification:** `ctest -R "unit\.audio_config"` (13/13 pass); real CLI (`mediadiff compare tests/fixtures/audio_hash_base.mp4 tests/fixtures/audio_sbr_implicit.mp4 --json`) confirms `core_rate_hz == effective_rate_hz == 88200` for the candidate, matching the probe's own decode.
- **Committed in:** `163fee4` (Task 1 commit)

**2. [Rule 1 - Bug] `test_audio_profile_sbr.cpp` Test 5 and Test 7 corrected against empirical CLI output**
- **Found during:** Task 2, verification against the real CLI
- **Issue:** Test 5 as planned asserted `effective_rate_hz == core_rate_hz * 2` for the implicit fixture — invalidated by Deviation 1 above (the two are equal for this fixture, both already 88200). Test 7 as planned asserted the explicit-vs-implicit comparison's declared non-pass set was `{"audio.profile"}` alone; empirically `audio.sample_rate` ALSO fires as non-pass (44100 vs 88200, since `audio_sbr_explicit.mp4` and `audio_sbr_implicit.mp4` were built with different ASC-declared base rates whose implicit-side codecpar rate additionally gets resolved to the doubled value at header-pass time) — both findings traceable to the same encode-time SBR-signaling difference (D-02: one cause, two declared facts).
- **Fix:** Rewrote Test 5 to assert `core_rate_hz == effective_rate_hz == 88200` (the actual, probe-confirmed observed values) instead of a doubling formula. Rewrote Test 7's declared set to `{"audio.profile", "audio.sample_rate"}`, with a comment recording the causal link per D-02. Also reworded Test 6's comment (it previously assumed every AAC fixture's `codecpar->profile` stays at its unset default; empirically `avcodec_profile_name()` resolves real names like "LC"/"HE-AAC" for this corpus's MP4-demuxed AAC, so the test now asserts the general "real, non-empty string" contract rather than a specific raw-integer-fallback claim).
- **Files modified:** `tests/integration/test_audio_profile_sbr.cpp`, `docs/checks/audio.sample_rate.md`
- **Verification:** `ctest -R "integration\.(audio_profile_sbr|audio_stream_params|doc03_coverage)"` (15/15 pass).
- **Committed in:** `29dd738` (Task 2 commit)

**3. [Rule 3 - Blocking] Regenerated `tests/golden/list_checks_effective.txt`**
- **Found during:** Task 2, full-suite verification
- **Issue:** `integration.list_checks - ENG-12: --effective is byte-identical across two runs, checked against a golden` failed because the newly registered `audio.profile` check wasn't yet reflected in the committed golden — an expected, mechanical consequence of registering a new check, not a real defect.
- **Fix:** Regenerated via `UPDATE_GOLDENS=1 ctest --test-dir build/x64-linux -R "integration\.list_checks - ENG-12: --effective is byte-identical"`; diff is exactly one added line (`audio.profile  severity=fail  tolerance=<none>`).
- **Files modified:** `tests/golden/list_checks_effective.txt`
- **Verification:** Full `ctest --test-dir build/x64-linux` — 1072/1072 pass (6 pre-existing, unrelated skips).
- **Committed in:** `29dd738` (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs against empirical CLI behavior, 1 Rule 3 mechanical golden regeneration)
**Impact on plan:** All three were necessary corrections against real, observed behavior of the built binary rather than the plan's literal (and, for the sample-rate-doubling assumption, empirically incorrect) wording. No scope creep — the design intent (pass-independent `audio.profile`, no formulaic rate fabrication, exact declared-set coverage) is unchanged and, if anything, more rigorously satisfied than the plan's literal text.

## Issues Encountered
- A transient syntax error while extending `audio_config.cpp` (a stray `}  // namespace` closed the outer `namespace mediadiff` early, cascading ~15 "not declared in scope" errors) was caught immediately by the build and fixed before any test run; not a lasting issue.

## Known Stubs

None — `audio.profile`'s three-bucket rendering and evidence are fully wired to real, decoded data with no placeholder values.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- AUDIO-01/AUDIO-03 requirements complete; `audio.profile` joins the audio check family alongside `audio.codec`/`audio.sample_rate`/`audio.sample_fmt`/`audio.bit_depth`/`audio.channels`/`audio.layout` (06-03) and `content.audio.sample_hash` (06-01).
- The `SbrProbeFn`-injection pattern and the "second, throwaway `AVFormatContext`" isolation pattern are both reusable precedents for any future bounded-decode-in-the-header-pass need.
- No blockers for subsequent 06-audio-analysis plans.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

All created files and both task commit hashes verified present.
