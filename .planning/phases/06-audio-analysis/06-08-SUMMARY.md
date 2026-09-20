---
phase: 06-audio-analysis
plan: 08
subsystem: audio-analysis
tags: [libebur128, ebu-r128, loudness, true-peak, tol-comparator, cpp20]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-01's shared audio decode sweep (detail::AudioDecodeState, StreamAudioDecode) that content.audio.sample_hash already consumes -- the loudness sink is fused into the SAME sweep, never a second decode"
  - phase: 06-audio-analysis
    provides: "06-07's generalised evidence-shape-gated Rule 2 override family in src/compare/tol.cpp (D-10/D-16) -- the ceiling escalation joins this same mechanism as a third override"
provides:
  - "detail::LoudnessSink in src/probe/audio_decode.cpp -- a libebur128 EBUR128_MODE_I|EBUR128_MODE_TRUE_PEAK sink fed from the shared decode sweep's own interleaved scratch buffer, with explicit per-channel role mapping from AVChannelLayout via ebur128_set_channel"
  - "Two exported pure dispatch tables (loudness_feed_dispatch_for_sample_fmt, loudness_channel_role_for_avchannel) letting the channel-mapping/format-dispatch tables be asserted directly without one fixture per native format"
  - "audio.loudness.integrated and audio.loudness.true_peak checks, registered with the roster's approved tolerances/profile overrides"
  - "A THIRD generic, evidence-shape-gated escalation in src/compare/tol.cpp (ceiling_state == under/above transition), never gated on check.id"
  - "A Rule 1 fix: the -70 LUFS gating-floor comparison is inclusive (<=), not strict (<) -- the fixture built to exercise it decodes to EXACTLY -70.0 LUFS"
affects: [audio-analysis, compare-engine]

# Actuals (#2632)
actuals:
  tokens: 27489
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: ["libebur128 1.2.6 (vcpkg, find_library discovery -- no CMake package config exists for it)"]
  patterns:
    - "pImpl for an incomplete anonymous-typedef C type with no struct tag (ebur128_state): wrapped in a private LoudnessSink struct fully defined only in audio_decode.cpp, held via std::unique_ptr<LoudnessSink> forward-declared in the header -- and the owning class's default constructor must be declared (not `= default` inline) and defined out-of-line in the SAME translation unit as the pImpl type's full definition, since an inline-defaulted special member's implicit body needs the pointee complete wherever it is first instantiated."
    - "Quantise-to-fixed-denominator-rational as the forced representation for a comparator that supports only rational/int64 magnitudes under a tol semantic (a real value_kind hits its internal-error arm) -- ties away from zero via std::llround, raw double preserved in evidence only."
    - "Third generic, evidence-shape-gated comparator override in the D-10/D-16 family: an evidence key pair (here ceiling_state == under/above) read directly from both sides' Measurement::evidence, escalating only on the specific transition, never gated on check.id and never a second state-semantic id (which would fire on an unchanged, already-flagged pair)."

key-files:
  created:
    - src/analyzers/audio/loudness.cpp
    - docs/checks/audio.loudness.integrated.md
    - docs/checks/audio.loudness.true_peak.md
    - tests/unit/test_loudness_sink.cpp
    - tests/integration/test_audio_loudness.cpp
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - src/analyzers/audio/analyzers.h
    - src/compare/tol.cpp
    - src/core/checks.def
    - src/probe/orchestrator.cpp
    - CMakeLists.txt
    - tests/unit/test_tolerance.cpp
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_timeline_jitter.cpp
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "The below-floor comparison in audio_decode.cpp is inclusive (integrated <= kLoudnessGatingFloorLufs), not the doc's literal strict less-than: tests/fixtures/audio_loud_floor.flac, the fixture built specifically to exercise doc 05's gating-floor rule, decodes to EXACTLY -70.0 LUFS to full double precision under libebur128, so a strict < would never fire on the one fixture this rule exists to prove -- a Rule 1 bug fix, not a scope change (flagged_assumption A1 in the plan already named this exact boundary as an open reading)."
  - "tests/integration/test_audio_loudness.cpp was created during Task 2, not deferred to Task 3 as its own files_modified list implied -- Task 2's acceptance criteria requires ALL nine Behavior Tests to exist as named Catch2 assertions by the end of Task 2, and most need the real CLI/fixtures to prove at all. Task 3 extended the same file with the +/-0.1 LU golden-reference assertion (mirrors 06-05's own precedent of adding a test file beyond a task's literal list)."
  - "Behavior Tests 5, 6, 7 and 8 (the ceiling-escalation invariants themselves -- fires on an under-to-above transition regardless of tolerance, never fires on the reverse, never fires on an unchanged pair, driven by evidence shape rather than check.id) are proven at the unit level in tests/unit/test_tolerance.cpp against hand-built Measurement pairs on a synthetic check id, mirroring the existing D-10/D-16 test style -- the real audio_peak_under.flac/audio_peak_over.flac fixture pair's own ~1.5dB delta already exceeds its 0.3dB tolerance on magnitude alone, so it cannot isolate 'the escalation fired regardless of tolerance' from 'the delta alone would have failed anyway' the way a synthetic pair with a small in-tolerance delta can. The real fixture pair is still exercised end-to-end at the CLI level (Tests 5/6 in test_audio_loudness.cpp) to prove the escalation wires correctly through the whole stack."
  - "libebur128's own gating-block weighting table (verified by reading the vendored ebur128.c) applies an identical 1.41x factor to LEFT_SURROUND/RIGHT_SURROUND and Mp090/Mm090 in this library version, so a numeric-difference test cannot distinguish correct back-vs-side channel mapping. Channel-role mapping is instead tested as a pure function (loudness_channel_role_for_avchannel) directly against the real libebur128/libav enum constants, proving the MAPPING identity rather than a resulting number."

requirements-completed: [AUDIO-05, AUDIO-06, AUDIO-10]

coverage:
  - id: D1
    description: "libebur128 sink fused into the shared decode sweep, with explicit per-channel role mapping from AVChannelLayout and a feed function dispatched once per stream"
    requirement: "AUDIO-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_loudness_sink.cpp (9 test cases -- mode mask/readout success, channel-role mapping incl. AV_CHAN_NONE fallback, feed dispatch by format, planar/packed equivalence, gating-floor sentinel, quantiser exactness/determinism, read_frame_call_count invariance, zero-sample not-measured state)"
        status: pass
    human_judgment: false
  - id: D2
    description: "audio.loudness.integrated matches the committed ffmpeg -af ebur128 reference within +/-0.1 LU on every measured fixture"
    requirement: "AUDIO-05"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_loudness.cpp#audio_loudness - every AUDIO_EBUR128_REFERENCE.txt fixture matches mediadiff's own measurement within +/-0.1 LU"
        status: pass
    human_judgment: false
  - id: D3
    description: "audio.loudness.true_peak gates asymmetrically: an upward crossing of -1.0 dBTP escalates to fail regardless of tolerance, a downward crossing never escalates, and an unchanged already-hot pair is not a finding"
    requirement: "AUDIO-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp (5 ceiling-escalation test cases against a synthetic non-loudness check id)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_loudness.cpp (Tests 5, 6, 9 -- real audio_peak_under.flac/audio_peak_over.flac fixtures, incl. under --profile transform)"
        status: pass
    human_judgment: false
  - id: D4
    description: "The asymmetric ceiling rule is one generic, evidence-shape-gated comparator escalation in src/compare/tol.cpp, never gated on check.id"
    requirement: "AUDIO-06"
    verification:
      - kind: other
        ref: "grep -n 'check.id' src/compare/tol.cpp -- no match inside the ceiling-escalation branch"
        status: pass
    human_judgment: false
  - id: D5
    description: "The loudness sink shares the SAME decode sweep as the hash sink -- no second decode, read_frame_call_count unchanged"
    requirement: "AUDIO-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_loudness_sink.cpp#loudness_sink - adding the loudness sink to the shared sweep does not change read_frame_call_count"
        status: pass
    human_judgment: false

# Metrics
duration: 60min
completed: 2026-09-20
status: complete
---

# Phase 6 Plan 8: EBU R128 Loudness and True Peak Summary

**libebur128 EBU R128 loudness/true-peak fused into the shared audio decode sweep, with explicit AVChannelLayout-to-EBUR128 channel mapping and a generic asymmetric -1.0 dBTP ceiling escalation in the tol comparator**

## Performance

- **Duration:** ~60 min
- **Started:** 2026-09-20T17:51:36Z (STATE.md's own last_updated at handoff from 06-07)
- **Completed:** 2026-09-20T18:49:41Z
- **Tasks:** 3
- **Files modified:** 18 (5 created, 13 modified)

## Accomplishments

- `detail::LoudnessSink` (src/probe/audio_decode.cpp) computes EBU R128 integrated loudness and true peak inside the existing decode sweep, fed the same interleaved samples the hash sink already produces -- no second decode, no retained PCM.
- Every channel's BS.1770 role is mapped explicitly from the decoded `AVChannelLayout` via `ebur128_set_channel` before any sample is fed -- side-surround and back-surround positions resolve to distinct EBUR128 enum identities, never a shared default.
- `audio.loudness.integrated` and `audio.loudness.true_peak` are registered, documented, and match the committed `ffmpeg -af ebur128` reference within +/-0.1 LU on all 11 measured fixtures.
- The asymmetric -1.0 dBTP ceiling rule is one generic, evidence-shape-gated escalation in `src/compare/tol.cpp` (a third override in the D-10/D-16 family), firing only on a genuine under-to-above transition -- proven both at the unit level (synthetic Measurement pairs) and end-to-end through the real CLI.
- A real Rule 1 bug was found and fixed: the gating-floor comparison had to become inclusive (`<=`) because the fixture built to exercise it decodes to exactly -70.0 LUFS.

## Task Commits

Each task was committed atomically:

1. **Task 1: the libebur128 sink inside the shared decode sweep, with explicit per-channel role mapping** - `1db6862` (feat)
2. **Task 2: the two checks and the generic asymmetric ceiling escalation** - `aa3ac87` (feat)
3. **Task 3: the +/-0.1 LU reference assertion and the DOC-03 pairs** - `159d47a` (test)

**Plan metadata:** _pending — see final commit below_

_Note: no TDD RED/GREEN/REFACTOR sub-commits were used; each `tdd="true"` task's tests and implementation were verified together before its single commit, consistent with this repository's own established per-task commit granularity for prior 06-* plans._

## Files Created/Modified

- `src/probe/audio_decode.h` - Named constants (gating floor, ceiling, quantiser denominator), `StreamAudioDecode`'s loudness fields, the two exported pure dispatch-table functions, `LoudnessSink` forward declaration
- `src/probe/audio_decode.cpp` - `detail::LoudnessSink` (init/set_channel_map/feed/finalize), the feed-format and channel-role dispatch tables, the quantiser, wiring into `consume_frame()`/`finalize()`
- `src/analyzers/audio/analyzers.h` - `audio_loudness_analyzer()` declaration
- `src/analyzers/audio/loudness.cpp` - The two checks' analyzer, skip-reason ladder, evidence construction
- `src/compare/tol.cpp` - The third generic evidence-shape-gated escalation (`ceiling_state`)
- `src/core/checks.def` - `audio.loudness.integrated`/`.true_peak` registrations
- `src/probe/orchestrator.cpp` - `audio_loudness_analyzer()` added to `all_analyzers()`
- `CMakeLists.txt` - `find_library(EBUR128_LIBRARY ...)`, link, `loudness.cpp` added to sources
- `docs/checks/audio.loudness.integrated.md`, `docs/checks/audio.loudness.true_peak.md` - `--explain` docs
- `tests/unit/test_loudness_sink.cpp` - Behavior Tests 1-8 (Task 1)
- `tests/unit/test_tolerance.cpp` - Ceiling-escalation unit tests (Task 2, Tests 5-8)
- `tests/integration/test_audio_loudness.cpp` - Behavior Tests 1-4, 6, 9 (Task 2) plus the +/-0.1 LU golden-reference assertion (Task 3)
- `tests/integration/test_doc03_coverage.cpp` - Two new `declared_pairs()` rows, running total 86 -> 88
- `tests/integration/test_timeline_jitter.cpp` - Declared-set update for a pre-existing fixture pair now legitimately triggering `audio.loudness.true_peak`
- `tests/golden/list_checks_effective.txt` - Refreshed renderer golden for the two new registered checks

## Decisions Made

See `key-decisions` in frontmatter. Summarized:

1. Gating-floor comparison made inclusive (`<=`) -- a Rule 1 fix, not a scope change.
2. `tests/integration/test_audio_loudness.cpp` created during Task 2 rather than deferred to Task 3.
3. Ceiling-escalation invariants (Tests 5/6/7/8) proven at the unit level against a synthetic check id, not solely via the real fixture pair.
4. Channel-role mapping tested as a pure function against real enum constants, since libebur128's own weighting table cannot numerically distinguish back- from side-surround in this library version.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The below-floor gating comparison was strict (`<`) and never fired on the fixture built to prove it**

- **Found during:** Task 1, writing `tests/unit/test_loudness_sink.cpp`'s Test 5
- **Issue:** `tests/fixtures/audio_loud_floor.flac` (built by `scripts/gen_corpus.sh` with `volume=-80dB`, specifically to exercise doc 05 §4's `< -70 LUFS` gating rule) decodes to EXACTLY `-70.0` LUFS to full double precision under libebur128 (confirmed via a `WARN(fmt::format("{:.15f}", ...))` debug print during test development). A strict `integrated < kLoudnessGatingFloorLufs` comparison therefore never set `loudness_below_floor` on this fixture, silently defeating the one behavior test doc 05's own gating rule exists to prove.
- **Fix:** Changed the comparison to `integrated <= kLoudnessGatingFloorLufs` in `src/probe/audio_decode.cpp::finalize()`, with an extended comment recording the discovery and citing `flagged_assumption A1` in `06-08-PLAN.md`, which had already flagged this exact boundary as an open reading of doc 05's wording.
- **Files modified:** `src/probe/audio_decode.cpp`
- **Verification:** `tests/unit/test_loudness_sink.cpp`'s gating-floor test passes; every other fixture's `loudness_below_floor` outcome is unaffected (all comfortably above -70 LUFS).
- **Committed in:** `1db6862` (Task 1 commit)

**2. [Rule 3 - Blocking] `tests/golden/list_checks_effective.txt` needed regeneration**

- **Found during:** Task 2, running the full test suite after registering the two new checks
- **Issue:** The committed `--effective` rendering golden did not yet list `audio.loudness.integrated`/`.true_peak`, failing `integration.list_checks - ENG-12` byte-for-byte.
- **Fix:** Regenerated via `UPDATE_GOLDENS=1 ctest -R "list_checks - ENG-12: --effective is byte-identical"` -- an expected two-line addition, not a renderer regression.
- **Files modified:** `tests/golden/list_checks_effective.txt`
- **Verification:** `integration.list_checks` passes; the diff is exactly the two new rows.
- **Committed in:** `aa3ac87` (Task 2 commit)

**3. [Rule 3 - Blocking] A pre-existing declared-set test broke on a real, new finding**

- **Found during:** Task 2, running the full integration suite
- **Issue:** `tests/integration/test_timeline_jitter.cpp`'s jitter-trigger-pair test declares its complete expected finding set (D-02's no-others discipline); adding `audio.loudness.true_peak` surfaced a real, previously-invisible ~5.3dB true-peak difference between the two fixtures' independently-generated audio tracks (both comfortably under the -1.0 dBTP ceiling -- an ordinary tolerance-exceeding `warn`, not an escalation), which the pre-existing declared set did not name.
- **Fix:** Added `audio.loudness.true_peak` to that test's declared set with a causal-reason comment (verified: -17.7 vs -12.4 dBTP baseline/candidate).
- **Files modified:** `tests/integration/test_timeline_jitter.cpp`
- **Verification:** The full ctest suite (1158 tests) passes.
- **Committed in:** `aa3ac87` (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (1 bug, 2 blocking)
**Impact on plan:** All three were necessary for correctness (Rule 1) or a fully green build (Rule 3). No scope creep -- no architectural changes, no new checks beyond the two the roster approved.

## Issues Encountered

None beyond the deviations above -- both `tests/unit/test_loudness_sink.cpp`'s zero-sample-stream test (Test 8) and the CMakeLists.txt libebur128 discovery worked as designed on the first attempt.

## User Setup Required

None - no external service configuration required. libebur128 1.2.6 was already pinned in `vcpkg.json` (no new dependency added).

## Next Phase Readiness

- The shared audio decode sweep now runs three consumers (hash, loudness, and the yet-to-be-built silence detector per 06-09) off one sweep -- AUDIO-10's single-sweep guarantee holds and is directly tested.
- `src/compare/tol.cpp` now carries three generic, evidence-shape-gated overrides in the same family (D-10, D-16, and this plan's ceiling escalation) -- a well-established pattern for any future asymmetric or basis-swapping comparator need.
- No blockers for 06-09 (silence detection), which is expected to add a fourth consumer to the same sweep.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

All claimed created files found on disk; all three task commit hashes (`1db6862`, `aa3ac87`, `159d47a`) found in git log.
