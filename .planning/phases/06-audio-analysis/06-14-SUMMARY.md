---
phase: 06-audio-analysis
plan: 14
subsystem: audio
tags: [truncation, hash-incomparable, trust-02, wr-02, decode-determinism, gap-closure]

requires:
  - phase: 06-audio-analysis
    provides: AUDIO-10's shared audio-decode sweep (hash + loudness + silence sinks fused into one av_read_frame pass), plans 06-01/06-05/06-08/06-09/06-10; 06-12's perf ratchet infra
provides:
  - "src/probe/audio_decode.h/.cpp: kDecodeStopConsecutiveErrorLimit stop-reason vocabulary; StreamAudioDecode::decode_truncated/decode_truncation_reason/level_measurement_stopped/level_measurement_stop_reason; AudioDecodeState's private latch_decode_truncation()"
  - "src/core/model.h: kSamplingStateFull/kSamplingStateTruncated, the sampling_state vocabulary shared by producer (sample_hash.cpp) and comparator (compare/hash.cpp)"
  - "content.audio.sample_hash reports sampling_state \"truncated\" and a decode_truncation_reason evidence key for a limit-truncated decode, instead of the hardcoded \"full\""
  - "compare/hash.cpp's new truncated-sampling rule (checked before the ordinary kPreconditionKeys mismatch): either side truncated -> skipped:hash_incomparable, including truncated-vs-truncated"
  - "audio.loudness.integrated/.true_peak and audio.silence.edges/.dropouts skip partial_scan with evidence {reason: <stop token>} when level_measurement_stopped is true, inserted between the undecodable and loudness/silence_measured branches"
  - "docs/checks/content.audio.sample_hash.md's \"Truncated decodes\" section and \"Decode stop reasons\" table (the single stop-token list); the four level-check docs extend their skip sentences to point at it"
affects: [06-15-decoded-rate-and-cr01-cr02, 06-16-cr03-non-finite-guards, 06-20-designated-leg-confirmation]

actuals:
  tokens: 11358
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Stop-reason vocabulary as a single inline constexpr std::string_view list in the probe header, mirroring 'check IDs are forever' -- a token, once published, is never renamed"
    - "A truncated/stopped state degrades to skipped rather than propagating a value -- the sampling_state precondition key already existed (02-04), this plan just stopped hardcoding it to \"full\""
    - "compare_hash's own evidence-value-only rule (never gated on check.id) checked BEFORE the generic kPreconditionKeys mismatch, so it subsumes both the mismatched (truncated-vs-full) and the agreeing (truncated-vs-truncated) case in one place"
    - "push_skip(evidence) overload bracketed inside the existing -Wmaybe-uninitialized pragma scope, per the per-file-copy convention loudness.cpp/silence.cpp already established"

key-files:
  created: []
  modified:
    - src/core/model.h
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - src/analyzers/content/sample_hash.cpp
    - src/compare/hash.cpp
    - src/analyzers/audio/loudness.cpp
    - src/analyzers/audio/silence.cpp
    - src/analyzers/audio/analyzers.h
    - tests/unit/test_audio_decode.cpp
    - docs/checks/content.audio.sample_hash.md
    - docs/checks/audio.loudness.integrated.md
    - docs/checks/audio.loudness.true_peak.md
    - docs/checks/audio.silence.edges.md
    - docs/checks/audio.silence.dropouts.md

key-decisions:
  - "GAP_BASE recorded as 8e37b7284abad06573f4958b5e1cb37695e3956e (HEAD before any Task 1 edit) -- the commit 06-15/06-16/06-18/06-19's own corpus differentials build from"
  - "The truncated-sampling rule in compare_hash is placed BEFORE the existing kPreconditionKeys mismatch check, which means it now also handles the truncated-vs-full case that previously went through the generic mismatch path -- the message text changed (now names 'truncated' explicitly) but the observable status/skip_reason for that case is unchanged (still skipped:hash_incomparable)"
  - "Both new StreamAudioDecode booleans (decode_truncated, level_measurement_stopped) are latched together from the single feed_packet call site today (a decode truncation always also stops level measurement, per plan text) -- kept as two independent fields rather than one, since a future narrower stop condition may need to set them separately without a field rename"

patterns-established:
  - "Decode-stop-reason vocabulary: a single named-constant list in src/probe/audio_decode.h, mirrored by one Markdown table in docs/checks/content.audio.sample_hash.md -- 06-15/06-16 append tokens to both, never repurpose an existing one"

requirements-completed: [AUDIO-08, TRUST-02]

coverage:
  - id: D1
    description: "A decode stopped by the 65th consecutive error is labelled decode_truncated with reason consecutive_decode_error_limit; 64 consecutive errors do not truncate (D-09 boundary unchanged)"
    requirement: "TRUST-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - 65 consecutive send failures truncate the decode; 64 do not"
        status: pass
    human_judgment: false
  - id: D2
    description: "A truncated-vs-full content.audio.sample_hash pair reports skipped:hash_incomparable (never a fabricated content verdict) with a message naming sampling_state and truncated"
    requirement: "TRUST-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a truncated-vs-full sample_hash pair compares skipped:hash_incomparable, never a fabricated verdict"
        status: pass
    human_judgment: false
  - id: D3
    description: "Two independently-truncated sample_hash chains also compare skipped:hash_incomparable, even when their sampling_state agrees and their prefixes digest-match"
    requirement: "TRUST-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - two truncated sample_hash chains compare skipped:hash_incomparable"
        status: pass
    human_judgment: false
  - id: D4
    description: "audio.loudness.integrated/.true_peak and audio.silence.edges/.dropouts skip partial_scan with evidence reason on a stopped stream, and stay pass/real-valued on a non-vacuous full-sweep baseline; Fingerprint::partial stays false (D-09)"
    requirement: "AUDIO-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a stopped stream's loudness and silence checks skip partial_scan with the stop reason"
        status: pass
    human_judgment: false
  - id: D5
    description: "Truncation and the decode-stop-reason vocabulary are documented in docs/checks/ where mediadiff explain surfaces them, with no golden/digest change from the doc regeneration"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (full suite, 1217 passed) + mediadiff explain content.audio.sample_hash"
        status: pass
    human_judgment: false

duration: 20min
completed: 2026-09-23
status: complete
---

# Phase 06 Plan 14: Truncated-decode honesty (WR-02/TRUST-02 gap closure) Summary

**A decode the consecutive-error DoS limit cut short is now labelled `sampling_state: "truncated"` instead of the hardcoded `"full"`, so `content.audio.sample_hash` degrades to `skipped:hash_incomparable` (never a fabricated content verdict) against any other decode, truncated or full, and the four level checks skip `partial_scan` with the stop reason instead of reporting a value measured over only part of the stream.**

## Performance

- **Duration:** 20 min
- **Started:** 2026-09-23T19:24:00Z
- **Completed:** 2026-09-23T19:43:45Z
- **Tasks:** 3
- **Files modified:** 14 (9 source/test, 5 docs)

## Accomplishments

- `StreamAudioDecode` gains `decode_truncated`/`decode_truncation_reason` and `level_measurement_stopped`/`level_measurement_stop_reason`, latched once (first-reason-wins) from `feed_packet`'s existing consecutive-error-limit branch via a new private `latch_decode_truncation()` — the 64/65 boundary and the `consecutive_errors_ > kMaxAudioDecodeErrorsPerStream` comparison are both unchanged (WR-03's off-by-one claim stays withdrawn, per 06-REVIEW.md's own correction).
- `content.audio.sample_hash`'s `sampling_state` evidence now reads `decode.decode_truncated ? kSamplingStateTruncated : kSamplingStateFull` and appends `decode_truncation_reason` after every existing key when truncated — a non-truncated stream's evidence is byte-identical to before.
- `compare/hash.cpp` gained a truncated-sampling rule, checked before the generic `kPreconditionKeys` mismatch: either side (or both) reporting `sampling_state: "truncated"` degrades to `skipped:hash_incomparable`, closing the case the generic mismatch rule structurally cannot catch — two independently-truncated sides whose `sampling_state` values happen to agree.
- `audio.loudness.integrated`/`.true_peak` and `audio.silence.edges`/`.dropouts` gained a `level_measurement_stopped` branch (between `undecodable` and the ordinary `*_measured` check) that skips `partial_scan` with evidence `{"reason": decode.level_measurement_stop_reason}` — `Fingerprint::partial` is not set (D-09: only an undecodable stream marks the fingerprint partial).
- `docs/checks/content.audio.sample_hash.md` gained a "Truncated decodes" section and the single "Decode stop reasons" table (`consecutive_decode_error_limit` today); the four level docs extend their existing skip sentences to point at it.
- Full `ctest` suite: 1217 passed, 0 failed, 6 designated-leg-only skips (unchanged from before this plan). `git diff --exit-code -- tests/golden/` exits 0 throughout — no fixture regenerated, no digest touched.

## Task Commits

Each task was committed atomically:

1. **Task 1: A decode stopped by the error limit is labelled truncated, and a truncated-vs-full hash pair degrades to hash_incomparable, end to end** - `d3c5285` (feat)
2. **Task 2: Level checks skip a stopped stream with its reason, and two truncated chains never compare** - `1d23ea0` (feat)
3. **Task 3: Document truncation and the stop-reason table, and prove no existing output moved** - `5a53dfd` (docs)

## Files Created/Modified

- `src/core/model.h` - `kSamplingStateFull`/`kSamplingStateTruncated` constants beside `Measurement`
- `src/probe/audio_decode.h` - `kDecodeStopConsecutiveErrorLimit`, four new `StreamAudioDecode` fields, `AudioDecodeState`'s two new private members and `latch_decode_truncation()`
- `src/probe/audio_decode.cpp` - the latch call in `feed_packet`, move ctor/assignment moves, `finalize()`'s surfacing of the two new field pairs and the gate that skips the loudness/silence finalize blocks when stopped
- `src/analyzers/content/sample_hash.cpp` - `sampling_state`/`decode_truncation_reason` evidence
- `src/compare/hash.cpp` - the truncated-sampling rule ahead of `first_precondition_mismatch`
- `src/analyzers/audio/loudness.cpp` / `silence.cpp` - the evidence-carrying `push_skip` overload and the `level_measurement_stopped` skip branch
- `src/analyzers/audio/analyzers.h` - skip-priority doc-comment updates for both analyzers
- `tests/unit/test_audio_decode.cpp` - `drive_error_limit_decode`/`read_stream_packets`/`ScanBundle`/`run_content_audio_sample_hash`/`find_sample_hash_finding`/`find_measurement` helpers; 4 new `TEST_CASE`s; 1 extended assertion on the Task 1 compare test
- `docs/checks/content.audio.sample_hash.md` - "Truncated decodes" + "Decode stop reasons" table
- `docs/checks/audio.loudness.integrated.md`, `audio.loudness.true_peak.md`, `audio.silence.edges.md`, `audio.silence.dropouts.md` - extended skip sentences

## Decisions Made

- **GAP_BASE = `8e37b7284abad06573f4958b5e1cb37695e3956e`** (repo HEAD immediately before Task 1's first edit) — recorded per Task 1's own action item 1, for 06-15/06-16/06-18/06-19's corpus differentials to build from.
- Placing the new truncated-sampling rule in `compare_hash` *before* the generic `kPreconditionKeys` mismatch check means it now also owns the truncated-vs-full case (previously handled by the generic rule). The observable `status`/`skip_reason` for that case is unchanged (`skipped`/`hash_incomparable`); only the message text changed, now explicitly naming `sampling_state` and `truncated` (verified by the extended Task 1 test).
- `decode_truncated` and `level_measurement_stopped` stay two independent `StreamAudioDecode` fields even though today's single call site always latches both together — matches the plan's own framing that a decode truncation always also stops level measurement, but leaves room for a future, narrower stop condition without a field rename.

## Deviations from Plan

None - plan executed exactly as written. The pre-change RED status for Task 1's end-to-end compare test was captured by temporarily reverting the one `sampling_state` line in `sample_hash.cpp` back to the hardcoded `"full"`, rebuilding, and running the test in isolation (see "Pre-change RED status" below), then restoring the fix — this is the mechanical form of the plan's own instruction to "run against the pre-change sample_hash.cpp (after steps 3-4 compile)".

## Pre-change RED status (Task 1's acceptance criterion)

With `src/core/model.h`, `src/probe/audio_decode.h`/`.cpp` already carrying this plan's truncation fields (so `decode.decode_truncated` was a real, observable `true` for the drive-to-truncation helper) but `src/analyzers/content/sample_hash.cpp` still hardcoding `{"sampling_state", "full"}`, the compare test failed as follows:

```
CHECK( finding.status == Status::skipped )
with expansion:
  3 == 4          // 3 = Status::fail, 4 = Status::skipped

CHECK( finding.skip_reason == SkipReason::hash_incomparable )
with expansion:
  0 == 5          // 0 = SkipReason::none, 5 = SkipReason::hash_incomparable
```

i.e. the pair compared a real content **fail** ("digests differ") — the fabricated-verdict class TRUST-02 exists to prevent — instead of `skipped:hash_incomparable`. After restoring the `sampling_state` fix, all 42 tests in the `audio_decode|decode_path_record|loudness_sink|silence_sink` filter passed, including this one.

## Issues Encountered

None.

## Staged-file audit (Task 3's acceptance criterion)

`tests/fixtures/GENERATOR_MANIFEST.json` (a pre-existing, timestamp-only working-tree change from an earlier corpus run, present before this plan started) was never staged by any of this plan's three commits. `.planning/milestone.lock` and `.planning/state.json` (both pre-existing untracked files at plan start) were never staged either. `.planning/config.json` was not modified. No fixture was generated or regenerated by this plan; `scripts/check_corpus.sh` was run once, at the start (precondition check), and reported the pre-existing 205 fixtures clean.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- WR-02 is closed: a limit-truncated decode never produces a hash verdict or a level value.
- The `kDecodeStopConsecutiveErrorLimit` stop-token vocabulary and its doc table exist, in exactly one place each, ready for 06-15 (CR-01/CR-02) and 06-16 (CR-03) to append their own tokens to both.
- 06-13 remains `status: halted` (its confirming CI run is still pending) — unaffected by this plan, per the amended `depends_on: ["06-12"]`. Only 06-20 certifies 06-13.
- Ready for 06-15.

## Self-Check: PASSED

- `d3c5285`, `1d23ea0`, `5a53dfd` all found in `git log --oneline --all`.
- All 14 modified files exist on disk with the expected content (verified via the `grep`/build/test commands run during execution, not re-checked redundantly here since Edit/Write would have errored otherwise).
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1217 passed, 0 failed (6 pre-existing designated-leg-only skips).
- `git diff --exit-code -- tests/golden/`: clean (exit 0).

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-23*
