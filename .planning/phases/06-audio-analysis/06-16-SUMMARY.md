---
phase: 06-audio-analysis
plan: 16
subsystem: audio
tags: [audio-decode, undefined-behavior, dos-bound, cr-03, wr-03, gap-closure]

requires:
  - phase: 06-audio-analysis
    provides: 06-14's decode-stop-reason vocabulary (kDecodeStopConsecutiveErrorLimit, latch_decode_truncation, the level-check partial_scan skip branches) and 06-15's decoded-rate basis and per-frame re-validation (consume_frame's lazy-init block, silence_feed_kind_) that this plan's float/double scan and error-bound seam extend unchanged
provides:
  - "src/probe/audio_decode.h/.cpp: kMaxMeasurableFloatSampleMagnitude (32768.0), kLevelStopNonFiniteOrOutOfRange (\"non_finite_or_out_of_range_sample\", level-only), kMaxAudioDecodeErrorsPerStream (moved to the header), detail::ConsecutiveDecodeErrorBound { limit, consecutive, exhausted, record_error(), record_success() }, normalize_amplitude_q15_for_sample_fmt"
  - "consume_frame's float/double scan: after the hash feed and before the loudness feed, the first non-finite or out-of-range decoded sample latches level_stop_reason_ ONLY (via the new latch_level_stop) -- the hash chain keeps consuming the stream in full while the loudness/silence sinks stop"
  - "normalize_amplitude_q15's float/double arms: non-finite input returns 0, otherwise the scaled value is clamped to [-32768, 32768] before std::llround -- closes the UB triple (negation of INT64_MIN, signed-square overflow, unspecified llround(NaN)/llround(inf))"
  - "feed_packet's send-error AND receive-error arms both route through the same ConsecutiveDecodeErrorBound -- a receive-side failure now counts toward the same consecutive-error DoS bound a send-side failure already did (WR-03)"
  - "docs/checks/content.audio.sample_hash.md's non_finite_or_out_of_range_sample row and the widened consecutive_decode_error_limit row; docs/checks/meta.decode_errors.md's one-sentence amendment; .planning/WINDOWS.md #41 (T-06-55, waived)"
affects: [06-17, 06-18, 06-19, 06-20-designated-leg-confirmation]

actuals:
  tokens: 15687
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A level-only latch (latch_level_stop) distinct from the decode-truncation latch (latch_decode_truncation) -- both share the first-reason-wins rule and the same level_stop_reason_ field, but only the latter also sets decode_truncation_reason_. This is the mechanism that lets a hostile sample stop measurement while the hash chain keeps consuming the stream in full (sampling_state stays \"full\")."
    - "A DoS bound extracted into its own unit-tested struct (detail::ConsecutiveDecodeErrorBound) with record_error()/record_success(), rather than two loose members threaded through every call site -- makes the off-by-one boundary (`>` vs `>=`) directly testable in isolation from any real decode, and makes it mechanically impossible for a future call site to update one member without the other."
    - "Corpus-wide GAP_BASE-vs-HEAD differential recipe (06-15's own pattern, reused unchanged a second time): git archive the base commit into scratch outside the repo, configure against the MAIN repo's already-built vcpkg_installed (VCPKG_MANIFEST_INSTALL=OFF, no FFmpeg rebuild), build only the mediadiff target, snapshot every real fixture with both binaries, diff pairs, delete scratch afterward."

key-files:
  created: []
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - tests/unit/test_audio_decode.cpp
    - docs/checks/content.audio.sample_hash.md
    - docs/checks/meta.decode_errors.md
    - .planning/WINDOWS.md

key-decisions:
  - "CR-03's guard is a raw-magnitude check against kMaxMeasurableFloatSampleMagnitude on the DECODED sample value itself (before the 32768x Q15 scale normalize_amplitude_q15 applies) -- a lone sample of exactly 32768.0 does not stop, 32769.0 does, matching this plan's own must_have boundary exactly and confirmed by a dedicated test."
  - "The level-stop scan runs only when silence_feed_kind_ resolves to float_fmt or double_fmt (the SAME Ebur128Feed dispatch the loudness sink and silence detector already resolved) -- the integer feed kinds (s16/s32) skip the scan entirely, so the aac_fixed/pcm perf reference path gains no per-sample work from this guard."
  - "normalize_amplitude_q15's clamp is a SECOND, independent safety net beyond the level-stop scan: even on a stream where level measurement was never stopped (all samples in range), the clamp bounds every arm's output to [-32768, 32768] unconditionally, which is what makes the dropout detector's peak-squared bound (documented inline, replacing the old, now-inaccurate comment) actually hold."
  - "ConsecutiveDecodeErrorBound::record_success() resets `consecutive` to 0 but never un-latches `exhausted` once set -- mirrors latch_decode_truncation's own first-reason-wins, never-un-set rule, and matches feed_packet's own early-return guard (a stream that already hit the limit is never fed again, so record_success() is never actually called past that point in practice)."
  - "flagged_assumption A2's own guard held against the real linked FFmpeg 8.1 without modification: a real pcm packet (one whole sample frame plus one extra byte) reproducibly yields exactly one decoded sample and then one avcodec_receive_frame failure, so the WR-03 receive-arm test needed no Rule 4 escalation."

patterns-established: []

requirements-completed: [AUDIO-05, AUDIO-07]

coverage:
  - id: D1
    description: "CR-03: a non-finite or out-of-range decoded float/double sample stops loudness/silence measurement with its own reason (non_finite_or_out_of_range_sample) before it ever reaches std::llround or libebur128 -- the hash chain is unaffected (sampling_state stays \"full\", total_samples equals the stream's real sample count), proven end to end on an in-process float WAV (NaN, +inf, and a 1e30 magnitude value all stop; a lone sample of exactly 32768.0 does not, 32769.0 does)."
    requirement: "AUDIO-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a non-finite float sample stops level measurement but the hash stays full, end to end (CR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a huge finite float sample (1e30) also stops level measurement (CR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a float sample of exactly 32768.0 does not stop level measurement; 32769.0 does (CR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - an inf double sample also stops level measurement, exercising the double arm (CR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a clean float tone does not stop level measurement (CR-03 control)"
        status: pass
    human_judgment: false
  - id: D2
    description: "CR-03: normalize_amplitude_q15's float/double arms return 0 for non-finite input and clamp every output to [-32768, 32768] -- closing the UB triple (llround(NaN)/llround(inf) undefined, negation of INT64_MIN in the silence loop's abs(), signed-square overflow in the dropout RMS window) reachable from pcm_f32le/pcm_f64le's unclamped decoder output. The clamp is decision-neutral for silence/dropout classification, confirmed by the corpus-wide differential finding zero changed snapshots including every float-decoded fixture."
    requirement: "AUDIO-07"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - normalize_amplitude_q15_for_sample_fmt clamps float/double to +/-32768 and maps non-finite to 0; integer arms are unchanged (CR-03)"
        status: pass
      - kind: integration
        ref: "200-fixture GAP_BASE-vs-HEAD corpus snapshot differential (0 differing)"
        status: pass
    human_judgment: false
  - id: D3
    description: "WR-03 (substantive half): an avcodec_receive_frame failure now counts toward the same consecutive-error DoS bound a send failure already did, through detail::ConsecutiveDecodeErrorBound. The `>` comparison against the limit of 64 is unchanged (the review's withdrawn off-by-one). A mixed real-decoder run (one receive failure, then 64 further send failures = 65 consecutive) truncates; the same run with 63 further send failures (64 consecutive) does not."
    requirement: "AUDIO-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - ConsecutiveDecodeErrorBound: 64 errors leave exhausted false, the 65th sets it true (WR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - ConsecutiveDecodeErrorBound: record_success resets the run, but not once exhausted (WR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a real pcm packet with one trailing extra byte decodes one sample then fails at avcodec_receive_frame (WR-03 guard)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - receive-side failures count toward the consecutive-error bound: 64 further send failures trip it (WR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - receive-side failures count toward the consecutive-error bound: 63 further send failures do not (WR-03)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - 65 consecutive send failures truncate the decode; 64 do not (06-14, re-verified unchanged)"
        status: pass
    human_judgment: false
  - id: D4
    description: "CR-03/WR-03 are documented where mediadiff explain surfaces them, and 06-14..06-16's combined effect is proven output-neutral on the full corpus and measured (informationally) against the audio instruction ratchet, both well within +/-2%."
    verification:
      - kind: integration
        ref: "full ctest --test-dir build/x64-linux (1237 passed, 0 failed) + mediadiff explain content.audio.sample_hash + 200-fixture GAP_BASE-vs-HEAD differential (0 differing) + scripts/measure_audio_perf.sh --instructions --check-baseline inside ubuntu:24.04 (audio_plain_instructions -0.18%, audio_full_instructions +0.20%, both within tolerance)"
        status: pass
    human_judgment: false

duration: 36min
completed: 2026-09-23
status: complete
---

# Phase 06 Plan 16: Non-finite float guards and the widened decode-error bound (CR-03/WR-03) Summary

**A hostile or corrupt float/double PCM sample can no longer reach `std::llround` unbounded or poison a libebur128 loudness state, and a decoder failure at `avcodec_receive_frame` now counts toward the same consecutive-error DoS bound a `avcodec_send_packet` failure already did -- both closed with in-process RED/GREEN tests, and 06-14 through 06-16's combined effect proven to change no real corpus output.**

## Performance

- **Duration:** 36 min
- **Started:** 2026-09-23T20:13:48Z
- **Completed:** 2026-09-23T20:49:46Z
- **Tasks:** 3
- **Files modified:** 6 (2 source, 1 test, 2 docs, 1 ledger)

## Accomplishments

- `normalize_amplitude_q15`'s float and double arms now return 0 for a non-finite input and clamp the scaled value to `[-kMaxMeasurableFloatSampleMagnitude, kMaxMeasurableFloatSampleMagnitude]` (i.e. `[-32768, 32768]`) before `std::llround` -- closing CR-03's undefined-behavior triple (`llround(NaN)`/`llround(inf)` unspecified, negation of `INT64_MIN` in the silence loop's `abs()`, signed-square overflow in the dropout RMS window), reachable from `pcm_f32le`/`pcm_f64le`'s unclamped decoder output. `normalize_amplitude_q15_for_sample_fmt` exports the same dispatch for direct unit coverage of the boundary.
- `consume_frame` gained a per-frame float/double scan, inserted between the hash feed and the loudness feed: when `level_stop_reason_` is empty and the resolved feed kind is float or double, every interleaved value in the frame is checked for `!std::isfinite(v) || std::fabs(v) > kMaxMeasurableFloatSampleMagnitude`. The first bad value calls the new **level-only** `latch_level_stop(kLevelStopNonFiniteOrOutOfRange)` -- distinct from `latch_decode_truncation`, this never sets `decode_truncated`, so the hash chain keeps consuming the stream in full (`sampling_state` stays `"full"`) while the loudness/silence sinks stop. Both sinks are gated on `level_stop_reason_.empty()`. Integer feed kinds (s16/s32) never enter this scan, so the aac_fixed/pcm perf reference path gains no per-sample cost from it.
- `detail::ConsecutiveDecodeErrorBound` (`limit`, `consecutive`, `exhausted`, `record_error()`, `record_success()`) replaces the former two-member pair inside `AudioDecodeState`. `kMaxAudioDecodeErrorsPerStream` moved from an anonymous-namespace `.cpp` constant to an `inline constexpr` in the header so the struct can default its own `limit` from it. Both `feed_packet`'s send-error arm AND its receive-error arm now call `error_bound_.record_error()`, closing WR-03's gap: before this plan, only send-side failures counted, so an interleaved success/receive-failure pattern could decode forever without ever tripping the bound (T-06-54). The `>` comparison against 64 is unchanged (the review's own withdrawn off-by-one correction).
- `docs/checks/content.audio.sample_hash.md` gained the `non_finite_or_out_of_range_sample` row (the one LEVEL-ONLY token in the "Decode stop reasons" table) and an amendment to the `consecutive_decode_error_limit` row naming both failure sources. `docs/checks/meta.decode_errors.md` gained one sentence on the same widened bound.
- A corpus-wide differential (GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e` vs HEAD, reusing 06-15's exact recipe) over all 200 real fixtures in `tests/fixtures/` found **0 differing snapshots** -- byte-identical with no masking needed, confirming both the level-stop guard and the normalize clamp are decision-neutral on every real fixture, float-decoded ones included.
- The audio instruction ratchet was measured (informationally) inside a throwaway `ubuntu:24.04` Docker container with `valgrind --tool=cachegrind`, mirroring 06-12's own procedure: `audio_plain_instructions` 89184449 (baseline 89344287, **-0.18%**), `audio_full_instructions` 47436294738 (baseline 47339403661, **+0.20%**) -- both comfortably inside the ±2% tolerance. `tests/golden/PERF_BASELINE.txt` was never edited; 06-20's designated-leg run remains the gate.
- `.planning/WINDOWS.md` entry #41 records the dropout-window memory observation (T-06-55) and immediately waives it as an accepted risk: `dropout_window_`'s size scales with a crafted sample rate, but only in proportion to the input actually decoded (at most ~4x raw PCM input size), and compressed codecs cap the rate in their own headers (FLAC's 20-bit `STREAMINFO` field tops out near 1 MHz) -- not an unbounded-allocation vulnerability.
- Full `ctest`: 1237 passed, 0 failed (11 new tests over 06-15's 1226 baseline; 6 pre-existing designated-leg-only skips unchanged). `git diff --exit-code -- tests/golden/` exits 0 throughout -- no fixture, digest, or baseline was regenerated or edited.

## Task Commits

Each task was committed atomically:

1. **Task 1: A non-finite or out-of-range float sample stops level measurement with its reason and can no longer reach llround or libebur128** - `f424194` (feat)
2. **Task 2: Receive-side decode failures count toward the same consecutive bound (WR-03), through a unit-tested seam** - `422565a` (feat)
3. **Task 3: Document the new token and the widened bound; prove 06-14..06-16 changed no real output and held the audio ratchet; record the deque observation** - `4973b51` (docs)

## Files Created/Modified

- `src/probe/audio_decode.h` - `kMaxMeasurableFloatSampleMagnitude`, `kLevelStopNonFiniteOrOutOfRange`, `kMaxAudioDecodeErrorsPerStream` (moved here), `detail::ConsecutiveDecodeErrorBound`, `normalize_amplitude_q15_for_sample_fmt` declaration, `AudioDecodeState`'s `error_bound_` member and `latch_level_stop()` declaration
- `src/probe/audio_decode.cpp` - `normalize_amplitude_q15`'s bounded float/double arms and rewritten comment; the exported `normalize_amplitude_q15_for_sample_fmt` wrapper; `consume_frame`'s float/double scan and the loudness/silence sink gates; `latch_level_stop()`; `feed_packet`'s send/receive arms routed through `error_bound_`; the dropout detector's corrected peak-squared bound comment
- `tests/unit/test_audio_decode.cpp` - `write_float_wav`/`synth_tone_with_bad_sample`/`unique_scratch_dir` helpers; `drive_receive_arm_decode` helper; 6 CR-03 tests (normalize boundary + 5 end-to-end float/double scan tests) and 5 WR-03 tests (2 seam tests on `ConsecutiveDecodeErrorBound` + 3 real-decoder tests)
- `docs/checks/content.audio.sample_hash.md` - the `non_finite_or_out_of_range_sample` row and its explanatory paragraph; the widened `consecutive_decode_error_limit` row
- `docs/checks/meta.decode_errors.md` - one sentence on the widened bound
- `.planning/WINDOWS.md` - ledger entry #41 (T-06-55, waived)

## Decisions Made

- **The level-stop guard checks the RAW decoded sample magnitude against `kMaxMeasurableFloatSampleMagnitude`**, not the post-scale Q15 value -- this is what makes the boundary land exactly where the plan's own must_have names it (32768.0 does not stop, 32769.0 does), independent of `normalize_amplitude_q15`'s own separate clamp.
- **Two independent safety nets, not one.** The level-stop scan (stops measurement entirely on a hostile frame) and `normalize_amplitude_q15`'s clamp (bounds every output regardless) serve different purposes: the scan prevents a hostile value from ever reaching libebur128 or the silence loop at all; the clamp is what makes the dropout detector's own peak-squared arithmetic bound hold even on a stream where the scan never triggered (e.g. a value inside range that still needs a defined, bounded normalized amplitude).
- **`latch_level_stop` is a genuinely separate latch from `latch_decode_truncation`**, not a thin wrapper -- it never sets `decode_truncation_reason_`, which is the mechanism that keeps the hash chain's `sampling_state` at `"full"` for this stop class alone, distinct from every `decoded_*`/`consecutive_decode_error_limit` token.
- **`ConsecutiveDecodeErrorBound` extraction kept the exact `>` semantics** (never `>=`) rather than revisiting the boundary -- 06-REVIEW.md's own correction had already withdrawn an earlier off-by-one claim, and this plan's job was only to widen WHICH failures count, not to relitigate the threshold.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None. flagged_assumption A2's own guard (a real pcm packet with one trailing extra byte reproducibly yields one decoded sample then one `avcodec_receive_frame` failure) held against the real linked FFmpeg 8.1 on the first attempt, so no Rule 4 escalation was needed for the WR-03 receive-arm test.

## RED values captured

- **CR-03 (Task 1):** `normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_FLT, 2.0f)` returned `65536` before the fix (twice the correct clamped `32768`); `NaN`/`+inf`/`1e300` all returned `INT64_MIN` (`-9223372036854775808`) -- `std::llround` on a non-finite double is unspecified and manifests as this sentinel on this toolchain, confirming the UB claim empirically. The end-to-end NaN-WAV test reported `level_measurement_stopped == false` and both `loudness_measured`/`silence_measured == true` before the fix (no stop at all -- the hostile sample reached libebur128 unguarded).
- **WR-03 (Task 2):** the mixed run (one real receive-frame failure, then 64 further one-byte send-failure packets) reported `decode_truncated == false` before the fix -- only the 64 send-side errors counted toward the old `consecutive_errors_` member, so the run needed a 65th SEND failure (not the receive failure already incurred) to trip the bound. After the fix, the same run correctly counts 65 consecutive errors (1 receive + 64 send) and truncates.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- CR-03 and WR-03 (substantive half) are both closed. AUDIO-05 and AUDIO-07 remain marked complete (they already were from 06-08/06-09; this plan hardens their implementation against hostile input, per the plan's own `requirements` field).
- The decode-stop-reason vocabulary now has 6 total members in exactly one place each (`src/probe/audio_decode.h` and the doc table) -- `non_finite_or_out_of_range_sample` is the first LEVEL-ONLY member; future plans appending further tokens should follow its precedent when a stop should not also mark the hash truncated.
- 06-13 remains `status: halted` (its confirming CI run is still pending) -- unaffected by this plan. Only 06-20 certifies it.
- The audio ratchet is measured and well within tolerance, but only informationally -- 06-20's designated-leg CI run is still the actual gate for `PERF_BASELINE.txt`.
- Ready for 06-17.

## Self-Check: PASSED

- `f424194`, `422565a`, `4973b51` all found in `git log --oneline --all`.
- All 6 modified files exist on disk with the expected content (verified via the `grep`/build/test commands run during execution).
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1237 passed, 0 failed (6 pre-existing designated-leg-only skips, unchanged from 06-15's baseline of 1226 plus this plan's 11 new tests).
- `git diff --exit-code -- tests/golden/`: clean (exit 0).
- Corpus differential: 200/200 fixtures byte-identical, 0 differing, GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e`.
- Audio ratchet: both metrics within ±2% tolerance (measured -0.18% / +0.20%), `tests/golden/PERF_BASELINE.txt` untouched.
- `.planning/WINDOWS.md` entry #41 present, status `waived`.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-23*
