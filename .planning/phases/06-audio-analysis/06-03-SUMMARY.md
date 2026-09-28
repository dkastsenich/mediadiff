---
phase: 06-audio-analysis
plan: 03
subsystem: audio-analysis
tags: [ffmpeg, avchannellayout, codecpar, catch2, doc03-coverage, header-pass]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-02's stream-parameter/layout/priming fixture corpus (audio_51.flac, audio_51_side.flac, audio_stereo_s16.wav, audio_stereo_s24.wav, audio_mono_s16.wav, audio_dropout.flac, etc.)"
provides:
  - "audio.codec, audio.sample_rate, audio.sample_fmt, audio.bit_depth, audio.channels, audio.layout -- six header-pass per-audio-stream identity checks (AUDIO-01, AUDIO-02)"
  - "src/analyzers/audio/{analyzers.h,stream_params.cpp} -- the audio family's first analyzer file, registered in all_analyzers()"
  - "StreamInfo extension (sample_fmt_name, sample_fmt_raw, bits_per_raw_sample, channels, channel_layout) on the demux_session.h/.cpp probe boundary"
  - "DOC-03 coverage table extended to 84 registered checks, all with proven trigger/clean fixture pairs"
affects: ["06-04", "06-06", "06-08", "06-09"]

# Actuals (#2632)
actuals:
  tokens: 18994
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "Header-pass-only per-audio-stream identity checks mirroring src/analyzers/video/stream_params.cpp's shape (file-local push_skip/scope_kind_for_stream, per-stream loop, no-stream fallback skip block)."
    - "Whole-block #pragma GCC diagnostic push/ignored(-Wmaybe-uninitialized)/pop bracketing an entire multi-branch inlined function set, rather than per-function narrow brackets -- discovered empirically that GCC 13.3/-O3 loses flow-sensitivity across the merged/inlined emit_*/push_skip call graph."
    - "AVChannelLayout-only layout derivation via av_channel_layout_describe, canonical string exposed on StreamInfo so src/analyzers/ never touches a libav type -- legacy uint64_t channel_layout / AV_CH_LAYOUT_* masks never referenced."
    - "Unspecified-as-real-value semantics for audio.layout (never Absent{}, never skipped) mirroring video.color.range's established 'unspecified is metadata loss, not a wildcard' treatment."

key-files:
  created:
    - src/analyzers/audio/analyzers.h
    - src/analyzers/audio/stream_params.cpp
    - docs/checks/audio.codec.md
    - docs/checks/audio.sample_rate.md
    - docs/checks/audio.sample_fmt.md
    - docs/checks/audio.bit_depth.md
    - docs/checks/audio.channels.md
    - docs/checks/audio.layout.md
    - tests/unit/test_audio_stream_params.cpp
    - tests/integration/test_audio_stream_params.cpp
  modified:
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/golden/list_checks_effective.txt

key-decisions:
  - "checks.def registration was sequenced task-by-task (5 ids in Task 1, audio.layout added only in Task 2) because tools/gen_registry.py hard-fails the build if a registered check lacks its docs/checks/<id>.md -- registering audio.layout before its own Task 2 doc existed would have broken Task 1's isolated build."
  - "The -Wmaybe-uninitialized pragma bracket had to wrap the ENTIRE push_skip-through-run_audio_stream_params block, not just push_skip alone -- narrower brackets moved the false-positive to emit_channels and the call site instead of suppressing it, confirming the warning is a property of the whole merged/inlined function set on this compiler/optimization level."
  - "audio.sample_fmt canonicalizes to the PACKED-equivalent name via av_get_alt_sample_fmt(fmt, /*planar=*/0), mirroring D-02's convention already established in src/probe/audio_decode.cpp for decoded-frame formats, applied identically at the header-pass/codecpar level."
  - "T-06-09 (sanitize audio.codec/audio.layout strings before they reach TTY/JUnit renderers) required no new code: src/cli/tty_render.cpp already routes finding.baseline/candidate through sanitize_for_display generically for every check id, and src/report/junit.cpp already XML-escapes generically (the T-2-33 choke point) -- these six new string-valued checks flow through the exact same Finding rendering path as every pre-existing check, so the existing pipeline already mitigates this threat without a per-check change."
  - "T-06-10 (DoS via absurd channel count/sample rate) confirmed clean: channel_layout is written into a fixed 64-byte stack buffer (char layout_buf[64]) bounded by av_channel_layout_describe's own length argument, and channels/sample_rate are plain int64 reads with no allocation sized by the attacker-controlled value."

requirements-completed: [AUDIO-01, AUDIO-02]

coverage:
  - id: D1
    description: "audio.codec, audio.sample_rate, audio.sample_fmt, audio.bit_depth, audio.channels each emit one measurement per audio stream at Scope::Kind::audio, derived from the header pass alone (AUDIO-01)"
    requirement: AUDIO-01
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_stream_params.cpp -- Tests 1-9 (all-five-at-scope-0, s16-vs-s24, s16-vs-mono, aac-vs-mp2, bit_depth-skip, sample_rate-evidence, no-audio-stream, partial_scan, determinism)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- declared_pairs() entries for audio.codec/sample_rate/sample_fmt/bit_depth/channels"
        status: pass
    human_judgment: false
  - id: D2
    description: "audio.layout reports 5.1 vs 5.1(side) as DIFFERENT values on an equal channel count via av_channel_layout_describe on AVChannelLayout only, and an unspecified layout compares as its own real value rather than a wildcard (AUDIO-02)"
    requirement: AUDIO-02
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_stream_params.cpp -- Task 2 tests (5.1-vs-5.1(side), unspecified-vs-real both directions, two-unspecified-agree, mono canonical spelling, structural skip cases)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_stream_params.cpp -- Tests 1-5 (real CLI proof of the headline war story, whole-report declared-set exactness, unspecified-as-real-value both directions, canonical mono spelling)"
        status: pass
    human_judgment: false
  - id: D3
    description: "All six new ids have proven DOC-03 trigger/clean fixture pairs and the whole existing integration suite stays green with no unjustified declared-set changes"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux --output-on-failure -- 100% of 1051 tests passed (6 intentionally skipped: designated-leg-only goldens)"
        status: pass
    human_judgment: false

duration: ~50min (session continued after a context compaction; task commit timestamps 13:49:50, 13:54:54, 14:04:30 UTC)
completed: 2026-09-20
status: complete
---

# Phase 06 Plan 03: Audio Stream-Parameter Identity Checks Summary

**Six header-pass audio identity checks (codec/sample_rate/sample_fmt/bit_depth/channels/layout) that make `5.1` vs `5.1(side)` a reported regression instead of a silent match on channel count, all derived from `codecpar` via the `StreamInfo` probe boundary with no libav header ever reached from `src/analyzers/`.**

## Performance

- **Duration:** ~50 min (session resumed after a context compaction mid-Task-3)
- **Tasks:** 3
- **Files modified:** 19 (10 created, 9 modified across the three task commits plus the plan metadata commit)

## Accomplishments

- `audio.codec`, `audio.sample_rate`, `audio.sample_fmt`, `audio.bit_depth`, `audio.channels` implemented as five header-pass-only per-audio-stream identity checks, each backed by a doc under `docs/checks/` and registered in `src/core/checks.def` with the roster's approved attributes.
- `audio.layout` implemented as the sixth measurement in the same per-stream loop, derived exclusively through `av_channel_layout_describe` on the modern `AVChannelLayout` struct -- proven via real CLI to report `5.1` vs `5.1(side)` as different values on an equal channel count, and proven to treat an unspecified layout as a real, comparable value (never `Absent{}`, never skipped) in both comparison directions.
- A file with no audio stream at all emits `skipped:insufficient_data` for all six ids rather than nothing; `partial_scan` takes priority over `insufficient_data` when the packet scan itself was truncated.
- `StreamInfo` (the single probe-layer boundary between libav types and `src/analyzers/`) extended with `sample_fmt_name`, `sample_fmt_raw`, `bits_per_raw_sample`, `channels`, `channel_layout` -- all resolved from `codecpar` inside `src/probe/demux_session.cpp`, never touched directly by the analyzer.
- DOC-03 coverage table (`tests/integration/test_doc03_coverage.cpp`) extended with six empirically-verified trigger/clean fixture pairs, advancing the running total from 78 to 84 registered checks with zero uncovered ids.
- Full corpus-wide sweep confirmed no other integration test's declared finding set gained an unexpected `audio.*` member; the only reviewable diff outside the new test file itself was the expected six-row addition to `tests/golden/list_checks_effective.txt`.

## Task Commits

Each task was committed atomically:

1. **Task 1: `audio.codec`/`sample_rate`/`sample_fmt`/`bit_depth`/`channels` from the header pass** - `26e0073` (feat, tdd)
2. **Task 2: `audio.layout` -- canonical `AVChannelLayout` description, 5.1 vs 5.1(side)** - `4e418cf` (feat, tdd)
3. **Task 3: DOC-03 pairs and corpus-wide declared-set sweep** - `83251fe` (test)

**Plan metadata:** commit pending (this document + STATE.md/ROADMAP.md/REQUIREMENTS.md)

_Note: this project's established TDD precedent (per 06-01/06-02) is implement+test together per task, verified against real fixtures, rather than strict commit-ordered RED-then-GREEN -- both TDD tasks here follow that same precedent, matching the phase's own prior convention._

## Files Created/Modified

- `src/analyzers/audio/analyzers.h` - family header declaring `audio_stream_params_analyzer()`
- `src/analyzers/audio/stream_params.cpp` - all six emit_* functions, the per-stream loop, the no-audio-stream fallback skip block
- `src/probe/demux_session.h` / `.cpp` - `StreamInfo` extension and its `codecpar`-derived population
- `src/probe/orchestrator.cpp` - `audio_stream_params_analyzer()` registered in `all_analyzers()`
- `src/core/checks.def` - six new `[[check]]` entries (group=audio, semantic=exact, severity=fail)
- `docs/checks/audio.{codec,sample_rate,sample_fmt,bit_depth,channels,layout}.md` - six `--explain` docs, each with the required three headings and Accept/Tune/Silence triple
- `tests/unit/test_audio_stream_params.cpp` - 13 named Catch2 unit tests (analyzer-level value proofs)
- `tests/integration/test_audio_stream_params.cpp` - 5 named Catch2 integration tests (real-CLI proofs, including the DOC-04 whole-report no-others count)
- `tests/integration/test_doc03_coverage.cpp` - six new `declared_pairs()` rows, running total 78 -> 84
- `tests/golden/list_checks_effective.txt` - six new rows (regenerated via `UPDATE_GOLDENS=1`, no existing row touched)
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` - new source/header/test registrations

## Decisions Made

- Sequenced `checks.def` registration to match doc availability per task (5 ids + docs in Task 1, `audio.layout` + its doc only in Task 2), since `tools/gen_registry.py` hard-fails the build on any registered check missing its `docs/checks/<id>.md`.
- Discovered empirically that the `-Wmaybe-uninitialized` GCC 13.3/-O3 false positive required bracketing the ENTIRE `push_skip`-through-`run_audio_stream_params` block in one `#pragma` rather than a narrow bracket around `push_skip` alone -- the warning is a property of the whole merged/inlined multi-branch function set, not any single function.
- `audio.sample_fmt` canonicalizes to its packed-equivalent spelling via `av_get_alt_sample_fmt(fmt, /*planar=*/0)`, mirroring the D-02 convention `src/probe/audio_decode.cpp` already established for decoded-frame formats, applied identically here at the header-pass/`codecpar` level.
- Confirmed (rather than assumed) that T-06-09's "route audio.codec/audio.layout strings through the existing sanitizing helper" mitigation is already satisfied generically: `src/cli/tty_render.cpp` already calls `sanitize_for_display` on every `finding.baseline`/`finding.candidate` regardless of check id, and `src/report/junit.cpp` already XML-escapes generically per the T-2-33 precedent -- no per-check code change was needed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected Task 1's own Test 1 assumption about `pcm_s16le`'s `bits_per_raw_sample`**
- **Found during:** Task 1, writing `tests/unit/test_audio_stream_params.cpp`
- **Issue:** The initial test asserted all five checks report `skip_reason == none` on `audio_stereo_s16.wav`, but `pcm_s16le` genuinely declares no `bits_per_raw_sample` (confirmed via `ffprobe`), so `audio.bit_depth` correctly reports `insufficient_data`.
- **Fix:** Restructured the test to assert `skip_reason == none` for the other four checks and separately assert `audio.bit_depth`'s expected `insufficient_data` skip with an `Absent{}` value.
- **Files modified:** `tests/unit/test_audio_stream_params.cpp`
- **Verification:** Test passes; matches the plan's own Test 5/must-have that `audio.bit_depth` never fabricates a value for a codec declaring no raw bit depth.
- **Committed in:** `26e0073` (Task 1 commit)

**2. [Rule 1 - Bug] Widened the `-Wmaybe-uninitialized` pragma bracket after the narrow version merely relocated the warning**
- **Found during:** Task 1, initial build
- **Issue:** A pragma bracket around `push_skip` alone (mirroring `video/stream_params.cpp`'s WR-03 precedent) suppressed the warning at that call site but the same warning reappeared at `emit_channels` and the `run_audio_stream_params` call site.
- **Fix:** Verified empirically that the warning is a property of the whole merged/inlined function set and widened the bracket to cover the entire block from `push_skip`'s definition through `run_audio_stream_params`'s closing brace.
- **Files modified:** `src/analyzers/audio/stream_params.cpp`
- **Verification:** Clean build with `-Wall -Wextra -Werror`; documented the discovery in the file's own comments per the plan's `read_first` instruction to verify empirically rather than assume.
- **Committed in:** `26e0073` (Task 1 commit)

**3. [Documentation-only correction, not a code defect] Task 2's own `<verify>` grep snippet undercounted the JSON serializer's line layout**
- **Found during:** Task 2, running the plan's second `<verify>` command
- **Issue:** `grep -A2 '"id": "audio.channels"' ... | grep -c '"status": "pass"'` printed 0 because this project's serializer emits one value per line with `"scope"` spanning two lines by itself, pushing `"status"` outside a 2-line window -- not a real defect in the implementation.
- **Fix:** Re-ran with `-A6` context, confirmed the true underlying status IS `pass`; documented this as a verification-only correction in the Task 2 commit message rather than an implementation defect, mirroring 06-02-SUMMARY.md's own Deviation #2 precedent for the same class of issue.
- **Files modified:** none (verification-only)
- **Committed in:** `4e418cf` (Task 2 commit, commit message)

**4. [Rule 2 - Missing critical / gate maintenance] Regenerated `tests/golden/list_checks_effective.txt` after registering six new checks**
- **Found during:** Task 3, full `ctest` sweep
- **Issue:** `integration.list_checks - ENG-12: --effective is byte-identical across two runs, checked against a golden` failed because the committed golden predates the six new checks registered in Tasks 1-2.
- **Fix:** Regenerated via `UPDATE_GOLDENS=1`, producing a minimal, reviewable six-line addition with no existing row touched; re-ran the full suite to confirm 100% pass.
- **Files modified:** `tests/golden/list_checks_effective.txt`
- **Verification:** `ctest --test-dir build/x64-linux --output-on-failure` reports 100% of 1051 tests passed.
- **Committed in:** `83251fe` (Task 3 commit)

---

**Total deviations:** 4 auto-fixed (2 Rule 1 bugs, 1 verification-only documentation correction, 1 Rule 2 gate-maintenance golden refresh)
**Impact on plan:** All four were necessary for correctness or to keep the test suite passing after the plan's own intended change (new checks always add a new `list-checks --effective` row). No scope creep; no architectural changes.

## Issues Encountered

None beyond the deviations documented above. The corpus-wide DOC-04 sweep required by Task 3 (checking whether any other integration test's declared finding set gained a new `audio.*` member now that six audio ids exist) found no such regression: the full `ctest` run was 100% green both before and after the DOC-03 table addition, so no fixture narrowing or additional declared-set entries were needed anywhere else in the suite.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- All six header-pass audio identity checks are registered, documented, tested at both the unit and integration level, and covered by DOC-03's fixture-pair gate -- 06-04 (HE-AAC implicit sample-rate doubling) can build directly on `audio.sample_rate`'s `{core_rate_hz, effective_rate_hz}` evidence shape without changing this check's own value shape.
- 06-06, 06-08 and 06-09 can append sibling analyzer files to `src/analyzers/audio/` following the exact `analyzers.h`/`stream_params.cpp` convention established here.
- No blockers. The `06-CHECK-ROSTER.md` entries for all six ids are fully consumed; no roster drift.

## Self-Check: PASSED

All claimed created/modified files verified present on disk (13 files checked). All claimed commit hashes (`26e0073`, `4e418cf`, `83251fe`) verified present via `git log --oneline --all`.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*
