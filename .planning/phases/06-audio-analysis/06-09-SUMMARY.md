---
phase: 06-audio-analysis
plan: 09
subsystem: audio-analysis
tags: [ffmpeg, audio-decode, silence-detection, ebur128, hysteresis, span-semantics]

requires:
  - phase: 06-audio-analysis
    provides: shared AudioDecodeState decode sweep (hash sink + LoudnessSink), Ebur128Feed dispatch table, plans 06-06/06-08
provides:
  - detail::AudioDecodeState silence/dropout detector (edge peak hysteresis + dropout sliding-window RMS), fused into the existing single-decode-sweep lazy-init block
  - merge_touching_sample_spans() exported pure function (probe layer)
  - audio.silence.edges and audio.silence.dropouts registered checks (span semantic, ms unit)
  - src/analyzers/audio/silence.cpp analyzer consuming ProbeResults::audio_decode
  - AUDIO-10's single-sweep guarantee now fully closed (hash + loudness + silence all share one av_read_frame pass)
affects: [07-report-and-policy, any future audio check that needs per-sample amplitude]

actuals:
  tokens: 23994
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Symmetric (open AND close) hysteresis debounce for peak-threshold edge detection, to avoid a sine tone's exact-zero sample spuriously opening a run"
    - "Trailing sliding-window sum-of-squares RMS with cross-multiplied integer threshold comparison (sum_sq < threshold_sq * window_samples) — no division, no float in the derivation"
    - "Mutual exclusion between edge and dropout detectors via simple positional rules (dropout runs touching sample 0 or EOF are discarded, that's edge's territory) instead of cross-referencing detector state"

key-files:
  created:
    - src/analyzers/audio/silence.cpp
    - docs/checks/audio.silence.edges.md
    - docs/checks/audio.silence.dropouts.md
    - tests/unit/test_silence_sink.cpp
    - tests/integration/test_audio_silence.cpp
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - src/analyzers/audio/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/AUDIO_EBUR128_REFERENCE.txt
    - tests/golden/list_checks_effective.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_timeline_av_sync.cpp
    - tests/integration/test_timeline_jitter.cpp
    - tests/integration/test_timeline_start_duration.cpp
    - tests/integration/test_timeline_structure.cpp

key-decisions:
  - "Silence edges detected via symmetric (open+close) hysteresis debounce on peak amplitude, to avoid a plain sine tone's exact-zero first sample spuriously opening a one-sample leading run"
  - "Dropout detection uses trailing sliding-window sum-of-squares RMS with cross-multiplied integer threshold comparison, discarding runs touching sample 0 or EOF (edge detector's territory)"
  - "audio.silence.edges and audio.silence.dropouts registered as span-semantic checks: introduced spans gate on severity, removed spans are always info"

patterns-established:
  - "Pattern: third+ sink fused into AudioDecodeState's existing lazy-init block, reusing Ebur128Feed's native-format dispatch table rather than re-deriving format detection — the template any future per-sample audio check should follow"
  - "Pattern: trailing sliding-window measurements systematically underestimate true span duration by ~window_ms; document this explicitly in both code comments and docs/checks/*.md rather than trying to eliminate the bias"

requirements-completed: [AUDIO-07, AUDIO-10]

coverage:
  - id: D1
    description: "audio.silence.edges detects introduced/removed leading and trailing silence as spans, via symmetric hysteresis peak detection in the shared decode sweep"
    requirement: "AUDIO-07"
    verification:
      - kind: unit
        ref: "tests/unit/test_silence_sink.cpp"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_silence.cpp"
        status: pass
    human_judgment: false
  - id: D2
    description: "audio.silence.dropouts detects interior audio dropouts as spans, via trailing sliding-window RMS with a minimum-span filter"
    requirement: "AUDIO-07"
    verification:
      - kind: unit
        ref: "tests/unit/test_silence_sink.cpp"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_silence.cpp"
        status: pass
    human_judgment: false
  - id: D3
    description: "Single-sweep guarantee (AUDIO-10) closed: hash, loudness, and silence/dropout detection all run inside one av_read_frame decode pass with no re-decode"
    requirement: "AUDIO-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_silence_sink.cpp (read_frame_call_count invariance)"
        status: pass
    human_judgment: false

duration: 60min
completed: 2026-09-20
status: complete
---

# Phase 06 Plan 09: Silence Edges and Interior Dropout Detection Summary

**`audio.silence.edges` and `audio.silence.dropouts` detect introduced/removed leading-trailing silence and interior dropouts as millisecond spans, computed inside the same shared decode sweep that already feeds the hash and loudness sinks, closing AUDIO-10's single-sweep guarantee.**

## Performance

- **Duration:** 60 min
- **Started:** 2026-09-20T18:53:17Z
- **Completed:** 2026-09-20T19:53:16Z
- **Tasks:** 2
- **Files modified:** 23

## Accomplishments
- Added a third sink (silence/dropout detector) fused into `AudioDecodeState`'s existing first-decoded-frame lazy-init block, reusing the `Ebur128Feed` native-format dispatch table — no second decode pass, `read_frame_call_count` invariant preserved.
- Edge detector: symmetric (open AND close) hysteresis debounce over per-frame peak amplitude at -60 dBFS / 5ms, correctly discriminating a continuous tone (0 spans) from real leading/trailing silence, validated first via a standalone Python simulation of the exact `sine=` waveform math before writing C++.
- Dropout detector: trailing 100ms sliding-window RMS at -70 dBFS with a 150ms minimum-span filter, entirely integer/fixed-point in the threshold comparison itself; mutual exclusion from the edge detector via simple positional discard rules (runs touching sample 0 or EOF belong to edge, not dropout).
- Registered `audio.silence.edges` and `audio.silence.dropouts` as `span`/`ms` checks, with `src/analyzers/audio/silence.cpp` converting `SampleSpan` (sample-index domain) to `RationalValue` ms via `detail::ticks_to_ms`, mirroring `timeline.gaps`/`timeline.discontinuities`.
- Full doc coverage (`docs/checks/audio.silence.{edges,dropouts}.md`) echoing all five named thresholds, verified via `mediadiff explain` grep.
- 8 new integration behavior tests + 9 new unit tests, plus DOC-03 coverage pairs (running total 88 -> 90).

## Task Commits

Each task was committed atomically:

1. **Task 1: Silence/dropout detector in the shared decode sweep** - `519a912` (feat)
2. **Task 2: The two span checks, docs, and DOC-03 pairs** - `87da7ef` (feat)

**Plan metadata:** (this commit) - `docs(06-09): complete silence edges and dropouts plan`

## Files Created/Modified
- `src/probe/audio_decode.h` - `SampleSpan`, 5 named constants, edge/dropout detector state, `merge_touching_sample_spans()` declaration
- `src/probe/audio_decode.cpp` - `normalize_amplitude_q15`, `dbfs_to_q15_linear`, `observe_silence_sample()`, lazy-init wiring, `finalize()` closing logic
- `src/analyzers/audio/silence.cpp` - the analyzer: `spans_to_ms()`, skip-reason ladder, `run_audio_silence()`
- `src/analyzers/audio/analyzers.h` - `audio_silence_analyzer()` declaration
- `src/probe/orchestrator.cpp` - registered the new analyzer after `audio_loudness_analyzer()`
- `src/core/checks.def` - two new `[[check]]` entries (span/ms/fail)
- `docs/checks/audio.silence.edges.md`, `docs/checks/audio.silence.dropouts.md` - full docs echoing named thresholds
- `tests/unit/test_silence_sink.cpp` - 9 unit tests against real fixtures + a hand-built minimal-span negative case
- `tests/integration/test_audio_silence.cpp` - 8 behavior tests against the real CLI
- `scripts/gen_corpus.sh` - fixed `audio_dropout_clean.flac`'s recipe (Rule 1, see Deviations)
- `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/AUDIO_EBUR128_REFERENCE.txt`, `tests/golden/list_checks_effective.txt` - regenerated to match the corrected fixture and new checks
- `tests/integration/test_doc03_coverage.cpp` - two new `declared_pairs()` rows
- `tests/integration/test_timeline_av_sync.cpp`, `test_timeline_jitter.cpp`, `test_timeline_start_duration.cpp`, `test_timeline_structure.cpp` - declared-set additions for a real, previously-invisible `audio.silence.edges` finding on `timeline_start_base.mp4`-derived pairs

## Decisions Made
- Symmetric hysteresis (open AND close) chosen over close-only debounce specifically to avoid spurious one-sample leading-silence detection on any continuous tone whose first sample lands on an exact zero crossing — validated numerically via Python simulation before implementation.
- Dropout RMS derivation kept in integer/fixed-point via cross-multiplied threshold comparison (`sum_sq < threshold_sq * window_samples`), avoiding division; only the native-format-to-Q15 normalization step (unavoidably, for float/double sample formats) uses `double`, and this is documented as distinct from "derivation."
- Mutual exclusion between edge and dropout detectors implemented via simple positional discard rules rather than cross-referencing detector state, keeping both detectors independent and easy to reason about.
- Trailing sliding-window RMS's systematic ~window_ms underestimation of true silent-span duration is treated as an intentional, documented design property (in both code comments and `docs/checks/audio.silence.dropouts.md`), not a bug to eliminate.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Missing minimum-span filter left the 150ms dropout threshold unenforced**
- **Found during:** Task 1 (unit test for the 100ms-below-minimum negative case)
- **Issue:** The dropout detector had no check against `kDropoutMinSpanMs`, so a synthetic 100ms hole was still reported as a dropout span.
- **Fix:** Added `dropout_min_span_samples_` (computed at lazy-init) and a `(run_end - dropout_run_start_) >= dropout_min_span_samples_` guard before pushing a dropout span.
- **Files modified:** `src/probe/audio_decode.cpp`, `src/probe/audio_decode.h`
- **Verification:** `tests/unit/test_silence_sink.cpp`'s 100ms-hole negative test passes.
- **Committed in:** `519a912` (Task 1 commit)

**2. [Rule 1 - Bug] `audio_dropout_clean.flac` and `audio_dropout.flac` were byte-identical fixtures**
- **Found during:** Task 2, while reading `scripts/gen_corpus.sh` in preparation for writing DOC-03 pairs
- **Issue:** Both fixtures applied the same `-af "volume=enable='between(t,3,3.4)':volume=0"` mute filter, making the "clean" and "trigger" fixtures for `audio.silence.dropouts` literally identical — the check could never actually be triggered by the declared pair.
- **Fix:** Changed `audio_dropout_clean.flac`'s recipe to a plain unmuted 6s sine tone. Verified via `sha256sum` that the two fixtures now differ. Updated `tests/golden/CORPUS_DIGEST.txt`'s existing line for this fixture in place (confirmed absent from the lint's protected historical anchor commit `8caf1f1`, so this is a permitted content fix, not a forbidden rewrite) and recomputed `CORPUS_DIGEST_SUMMARY`. Regenerated `tests/golden/AUDIO_EBUR128_REFERENCE.txt`'s line for the corrected fixture (`integrated_lufs` changed from -22.0 to -21.8).
- **Files modified:** `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/AUDIO_EBUR128_REFERENCE.txt`
- **Verification:** `bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_corpus_digest_provenance.sh` all pass; re-ran `gen_corpus.sh` a second time confirming byte-identical/deterministic regeneration.
- **Committed in:** `87da7ef` (Task 2 commit)

**3. [Rule 1 - Bug] Six pre-existing tests failed after the new checks surfaced real, previously-invisible findings**
- **Found during:** Full suite run after Task 2
- **Issue:** `timeline_start_base.mp4` has a genuine ~14ms near-silent trailing audio stretch (~4026-4040ms) that `audio.silence.edges` now correctly detects. This surfaced as an undeclared finding in five timeline integration tests and one stale golden (`list_checks_effective.txt`).
- **Fix:** Regenerated the `list_checks_effective.txt` golden via `UPDATE_GOLDENS=1 ctest`; added `"audio.silence.edges"` to the declared sets of `test_timeline_av_sync.cpp`, `test_timeline_jitter.cpp`, `test_timeline_start_duration.cpp`, and both affected cases in `test_timeline_structure.cpp` (one as a removed/info span, two as an introduced/fail span where the candidate's near-silence shifted to a non-overlapping position), each with a causal-explanation comment citing the exact evidence verified via direct CLI inspection before editing.
- **Files modified:** `tests/golden/list_checks_effective.txt`, `tests/integration/test_timeline_av_sync.cpp`, `tests/integration/test_timeline_jitter.cpp`, `tests/integration/test_timeline_start_duration.cpp`, `tests/integration/test_timeline_structure.cpp`
- **Verification:** Full `ctest --test-dir build/x64-linux` run twice, 100% of 1176 tests passing both times.
- **Committed in:** `87da7ef` (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (2 bugs found during implementation/testing, 1 bug in pre-existing fixture generation discovered during prep for this plan's own DOC-03 pairs)
**Impact on plan:** All three fixes were necessary for correctness (a silently-unenforced threshold, an unreachable check condition, and undeclared-but-real findings that would otherwise fail DOC-04's "no others" discipline). No scope creep — all fixes are directly caused by this plan's own new checks.

## Issues Encountered
None beyond the deviations documented above — all root-caused via direct CLI inspection before any test edit, per DOC-04 discipline.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- AUDIO-10's single-decode-sweep guarantee is now fully closed: hash, loudness, and silence/dropout detection all run inside one `av_read_frame` pass per audio track.
- Phase 06 (Audio Analysis) has 3 plans remaining (10-13, `total_plans: 13`); no blockers identified for continuing.
- The `Ebur128Feed`-dispatch + lazy-init-on-first-frame pattern established across hash/loudness/silence sinks is now the clear template for any future per-sample audio check.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*
