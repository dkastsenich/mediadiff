---
phase: 06-audio-analysis
verified: 2026-09-22T17:30:00Z
status: gaps_found
score: 3/5 roadmap success criteria verified (2 partial/failed)
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "SC2: `audio.priming` stays stable across an MP4 → MKV → MP4 round trip"
    status: failed
    reason: >
      Measured directly against the real binary (linked FFmpeg 8.1): `mediadiff compare
      tests/fixtures/audio_prime_base.mp4 tests/fixtures/audio_prime_roundtrip2.mp4 --json`
      reports `audio.priming` baseline "1024" vs candidate "1014", status "fail" — reproduced
      3x per the phase's own SUMMARY, and this exact fail result is now the ASSERTED expected
      behavior of `tests/integration/test_audio_priming.cpp` Test #947 (title: "DEVIATION:
      audio_prime_base.mp4 vs audio_prime_roundtrip2.mp4 (the MP4->MKV->MP4 round trip)
      reports a genuine, measured non-pass"). Root cause: MKV stores priming as CodecDelay in
      nanoseconds; converting 1024/44100s to ns and back to an MP4 edit-list media_time loses
      ~10 samples to rounding. `audio.priming` is `semantic=exact` over a string (D-14), so no
      tolerance can absorb this. `.planning/WINDOWS.md` entry #36 records this itself as
      "Open, not confirmed closeable (contradicts ROADMAP SC2's 'closing the priming: unknown
      gap' framing)." A single MP4→MKV hop (no return trip) IS stable (Test #953, pass) — only
      the literal double-hop round trip the roadmap names is not.
    artifacts:
      - path: "src/analyzers/audio/priming.cpp"
        issue: "Correctly implements the precedence chain and D-14's `unknown`-compares-as-itself rule; the residual is an unavoidable CodecDelay-ns rounding loss on re-materialization to MP4, not a missing mechanism."
    missing:
      - "No numeric-tolerance mechanism exists for this value shape (by design, D-14), and no fixture substitution is possible (the fixture already has a committed digest line). Closing this needs either a documented, reviewed relaxation of D-14 for the round-trip case, or accepting the residual via an explicit override."
  - truth: "SC2 (second clause): closing the `priming: unknown` gap Phase 5's `timeline.av_offset`/`av_drift` shipped with"
    status: failed
    reason: >
      `.planning/WINDOWS.md` entry #32 is still marked `open` (not `fixed`) after this phase.
      D-16's own shared-basis fix (`src/analyzers/timeline/av_sync.cpp`) verifiably closes the
      gap for real MP4-vs-MP4 priming pairs (`span_basis=adjusted` on both sides, zero drift),
      but the two MP4-to-TS target pairs the original ticket named
      (`timeline_start_base.mp4` vs `timeline_start_shift.ts` / `timeline_avoffset_unknown.ts`)
      still report `fail` on `timeline.av_drift` and `timeline.av_drift.pattern` — because the
      TS side's priming really is unreadable (no skip_samples, no initial_padding, no edit
      list survives the remux), so the shared-basis rule correctly falls back to raw-to-raw
      per D-11 rather than fabricating a basis, and that raw comparison still reports a
      residual as a false positive on payload that (per the ticket's own text) genuinely did
      not change. The ticket's own text says this is left "OPEN, not fixed."
    missing:
      - "A decode-based priming-detection mechanism for MPEG-TS, explicitly filed as a follow-up in WINDOWS.md #32 and not built in this phase."
  - truth: "SC3 / SC5: the audio decode path is a mechanically enforced determinism/correctness guarantee (phase goal framing), not merely a shared vocabulary"
    status: failed
    reason: >
      `.planning/phases/06-audio-analysis/06-REVIEW.md` (committed at HEAD, `status:
      issues_found`, 5 critical + 16 warning findings, 0 addressed by any later commit) reports
      5 unresolved critical defects. I independently re-derived 4 of the 5 by reading the cited
      source directly rather than trusting the review's narrative (see Anti-Patterns /
      Behavioral Spot-Checks below for the exact lines and the confirming commands): (1)
      `AudioDecodeState::ensure_initialized`/`consume_frame` seed the sink sample rate from
      `codecpar.sample_rate` and only fall back to the decoder's real `frame.sample_rate` when
      `codecpar` reported nothing at all (`src/probe/audio_decode.cpp:678`, `:716-719`) — for
      any real-world (non-hand-written) implicitly-signalled HE-AAC stream where `codecpar`
      carries the undoubled core rate, every loudness window, true-peak oversample, silence
      threshold and hash block-duration is computed at half the real rate; (2)
      `LoudnessSink`/silence detector state (channel count, sample format) is pinned to the
      FIRST decoded frame and never re-validated against later frames
      (`src/probe/audio_decode.cpp:695-764`), so a mid-stream channel/format narrowing is a
      heap over-read via `ebur128_add_frames_*` reading `frames * state->channels` against a
      scratch buffer sized for the current (smaller) frame; (3) `-1.0 dBTP` ceiling escalation
      (`src/compare/tol.cpp:299-312`, `src/analyzers/audio/loudness.cpp:148-150`) is a bare
      threshold on the quantised milli-dB value with no deadband — confirmed by direct read: a
      0.0002 dB change straddling the threshold produces an unconditional `Status::fail` that
      bypasses the registered 0.3 dB tolerance and any profile severity entirely, which is the
      exact "check that fires on media that did not meaningfully change" failure class
      PROJECT.md names as this project's P0; (4) `probe_implicit_sbr_via_second_open`
      deliberately keeps the wall-clock interrupt budget ARMED
      (`src/probe/demux_session.cpp:278-280`, confirmed real `std::chrono::steady_clock`-based
      budget at `demux_session.cpp:55-116`), so `audio.profile`'s COMPARED value can flip to
      `"... (sbr: unknown)"` purely because of host timing — a direct, confirmed violation of
      `checks.def`'s own documented invariant that this value "must never depend on which
      passes ran" and of the project's hard determinism constraint (a value that can jitter
      between two runs of the same input). None of these four is exercised by the existing
      1208-test suite (all green) because the only implicit-SBR fixture in the corpus is a
      hand-written stream whose container-declared rate happens to already equal the decoded
      rate (verified live: `core_rate_hz` and `effective_rate_hz` both read 88200 for
      `audio_sbr_implicit.mp4`), so the corpus does not contain a case that exercises the
      divergent-codecpar path. A 5th critical (float/double PCM normalization UB on
      non-finite/out-of-range samples, `src/probe/audio_decode.cpp:207-225`) is reported by the
      review and its cited code (no `std::isfinite`/clamp before `std::llround`) is present
      exactly as described, though I did not additionally construct a crafted fixture to
      trigger it.
    artifacts:
      - path: "src/probe/audio_decode.cpp"
        issue: "Sample-rate seeding order (CR-01), no mid-stream re-validation (CR-02), unclamped float normalization (CR-03) — all confirmed present in the committed HEAD code."
      - path: "src/compare/tol.cpp"
        issue: "Ceiling-crossing escalation has no deadband (CR-04) — confirmed present."
      - path: "src/probe/demux_session.cpp"
        issue: "SBR probe keeps the wall-clock interrupt armed, making a compared value host-timing-dependent (CR-05) — confirmed present."
    missing:
      - "Fixes for CR-01 through CR-05 (06-REVIEW.md gives concrete patches for each), plus a fixture that actually exercises a real-world (non-hand-written) implicit-SBR stream whose codecpar rate diverges from the decoded rate, so CR-01 has regression coverage once fixed."
deferred: []
human_verification: []
---

# Phase 6: Audio Analysis Verification Report

**Phase Goal:** The audio decode path and every `audio.*` check, moving the decode-determinism classes from shared vocabulary to a mechanically enforced guarantee.
**Verified:** 2026-09-22
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `inspect` renders a complete audio section; `5.1` vs `5.1(side)` is a regression, not a count match | ✓ VERIFIED | Live binary run: `audio.layout` fail baseline="5.1" candidate="5.1(side)" on `audio_51.flac` vs `audio_51_side.flac`. `inspect` (both JSON and human-readable) renders codec/profile/sample_rate/sample_fmt/bit_depth/channels/layout for every audio stream. `audio_sbr_implicit.mp4`/`audio_sbr_explicit.mp4` render `HE-AAC (sbr: implicit)` / `HE-AAC (sbr: explicit)` respectively. |
| 2 | `audio.priming` resolves through the precedence chain and stays stable across an MP4 → MKV → MP4 round trip, closing the `timeline.av_offset`/`av_drift` `priming: unknown` gap | ✗ FAILED | Precedence chain, `unknown`-as-own-value, and MP4→MKV (single hop) stability are all verified (tests #948-955, #953 pass). The literal MP4→MKV→MP4 round trip FAILS ("1024" vs "1014"), asserted as expected in the phase's own test #947. `WINDOWS.md` #36 records this as contradicting SC2's own framing. `WINDOWS.md` #32 (the av_drift/av_offset gap) is still `open`, not `fixed`, for the two MP4-to-TS pairs the original ticket named. See gaps. |
| 3 | Loudness matches `ffmpeg -af ebur128` within ±0.1 LU; true peak fails asymmetrically on an upward −1.0 dBTP crossing; silence/dropout spans reported | ✓ VERIFIED (feature); ✗ implementation has an unguarded false-positive path | `test_audio_loudness.cpp` Test 945 (every `AUDIO_EBUR128_REFERENCE.txt` fixture within ±0.1 LU) passes; Test 946 confirms the rule is asymmetric (no escalation on the reverse crossing); live `audio_dropout.flac` comparison reports the introduced dropout as a span. **However**, direct source reading confirms the escalation has no deadband (CR-04) — a 0.0002 dB change can hard-fail regardless of the registered 0.3 dB tolerance, the exact "fires on unchanged media" P0 class this project's own CLAUDE.md/PROJECT.md core value statement names. See gaps. |
| 4 | `sample_hash` locates the first divergent sample; `aac_fixed` cross-build equality; float cross-path hash reports `skipped:hash_incomparable` with a remediation hint; decoder name/class/flags/path signature recorded per hashed stream | ✓ VERIFIED | `src/compare/hash.cpp` divergence locator confirmed by direct read (`first_divergent_block`, `sample_range`, `first_divergent_time_ms`, `divergent_ranges`) and by a real divergent-tone test (`test_audio_sample_hash.cpp` Test 6). `aac_fixed` cross-architecture equality is a real, CI-confirmed proof (`WINDOWS.md` #39, CI run 35735099865 on arm64-osx). `skipped:hash_incomparable` with the exact remediation string is asserted end-to-end (`test_audio_hash_decoder.cpp` class proof Test 3/4). `decode_path` envelope array (decoder, class, flags, and class-2 path_signature) confirmed live via `mediadiff snapshot --content`. |
| 5 | Loudness/silence/hashing share one decode sweep; a 10-minute reference sweep completes in <4s | ✓ VERIFIED | Architecturally confirmed: `orchestrator.cpp` lists `content_audio_sample_hash_analyzer`, `audio_loudness_analyzer`, `audio_silence_analyzer` all consuming one `ProbeResults::audio_decode` slot; `06-01-PLAN.md`'s own must-have (`read_frame_call_count` unchanged) is asserted by test. `scripts/measure_audio_perf.sh` run locally: full leg (all 3 sinks) = 3.24s wall clock on the 10-minute reference file, under the 4s budget; the enforced gate (instruction-count ratchet) is transcribed from a real designated-leg CI run (`e5a1677`), matching Phase 5's own established convention (measured/printed, not asserted, per D-13/D-14). |

**Score:** 3/5 roadmap success criteria cleanly verified; 2 have real, evidenced failures (SC2 fully; SC3's true-peak implementation has a confirmed false-positive defect layered on an otherwise-verified feature).

### Requirements Coverage

All 13 requirement IDs assigned to Phase 6 (`AUDIO-01`..`AUDIO-10`, `TRUST-01`, `TRUST-02`, `PERF-04`) are claimed by at least one of the 13 plans' `requirements:` frontmatter; no orphaned IDs found against `.planning/REQUIREMENTS.md`'s Phase 6 mapping.

| Requirement | Status | Evidence |
|---|---|---|
| AUDIO-01 | ✓ SATISFIED | Stream-parameter checks confirmed live and by test (06-03, 06-11). |
| AUDIO-02 | ✓ SATISFIED | `audio.layout` 5.1 vs 5.1(side) confirmed live. |
| AUDIO-03 | ✓ SATISFIED | SBR implicit/explicit confirmed live; WINDOWS #38 (pass-dependence bug) fixed and confirmed (`audio_hash_base.mp4` now reports plain `"LC"`). |
| AUDIO-04 | ⚠ PARTIALLY SATISFIED | Precedence chain and single-hop MKV round trip verified; the MP4→MKV→MP4 round trip (ROADMAP SC2's literal example) fails, honestly documented as unresolved (WINDOWS #36). |
| AUDIO-05 | ✓ SATISFIED (with caveat) | ±0.1 LU match confirmed by real test against committed ffmpeg reference; underlying sample-rate-seeding defect (CR-01) not exercised by any existing fixture. |
| AUDIO-06 | ⚠ PARTIALLY SATISFIED | Asymmetric ceiling rule implemented and tested; confirmed missing deadband (CR-04) is a real false-positive risk. |
| AUDIO-07 | ✓ SATISFIED | Silence/dropout span detection confirmed live and by test. |
| AUDIO-08 | ✓ SATISFIED | Divergence locator confirmed by direct read and live divergent-tone test. |
| AUDIO-09 | ✓ SATISFIED | `aac_fixed`/`ac3_fixed` preference, `--hash-decoder default` opt-out, class-2 fallback recording all confirmed; mp3/mp2 correctly demoted back to class 2 (WINDOWS #39) rather than promoted on unproven evidence. |
| AUDIO-10 | ✓ SATISFIED | Single shared decode sweep architecturally confirmed. |
| TRUST-01 | ✓ SATISFIED | decoder name/class/flags/path_signature confirmed live in `decode_path` envelope. |
| TRUST-02 | ✓ SATISFIED | `skipped:hash_incomparable` with exact remediation hint confirmed end-to-end. |
| PERF-04 | ✓ SATISFIED | <4s confirmed locally (3.24s); CI instruction-count ratchet transcribed from a real run per Phase 5's established convention. |

### Anti-Patterns / Independently-Confirmed Code Review Findings

The phase's own `06-REVIEW.md` (committed at HEAD, after all 13 plans, `status: issues_found`, 5 critical / 16 warning findings) is a phase artifact documenting unresolved defects. Per this verifier's mandate not to trust claims without checking the code, I independently re-derived 4 of the 5 critical findings by reading the cited source directly:

| File:Line | Finding | Independently confirmed? | Severity |
|---|---|---|---|
| `src/probe/audio_decode.cpp:678`, `:716-719` | Sink sample rate seeded from `codecpar.sample_rate`, only overridden by the decoder's real `frame.sample_rate` when `codecpar` reported nothing — wrong rate for real (non-hand-written) implicit-SBR HE-AAC | ✓ Confirmed by direct read; not reproducible with existing fixtures (the one implicit-SBR fixture's `codecpar` already carries the doubled rate by construction) | 🛑 Blocker (affects AUDIO-05/06/07/10 correctness on untested real-world content) |
| `src/probe/audio_decode.cpp:695-764` | Channel count / sample format pinned to first decoded frame, never re-validated; `ebur128_add_frames_*` can read past `interleave_scratch_`'s current-frame sizing on a mid-stream channel narrowing | ✓ Confirmed by direct read (scratch resized per-frame; `LoudnessSink::init` bakes `channels` once) | 🛑 Blocker (heap over-read on hostile/malformed input; contradicts 06-10-PLAN.md's own declared fuzz-safety must-have) |
| `src/probe/audio_decode.cpp:207-225` | `normalize_amplitude_q15`'s float/double paths have no `std::isfinite`/range guard before `std::llround` | ✓ Confirmed by direct read (no guard present) | 🛑 Blocker (UB on crafted/corrupt `pcm_f32le`/`pcm_f64le` input) |
| `src/compare/tol.cpp:299-312`, `src/analyzers/audio/loudness.cpp:148-150` | `-1.0 dBTP` ceiling escalation is a bare threshold with no deadband — a sub-tolerance quantization-noise-level change hard-fails, bypassing the registered tolerance | ✓ Confirmed by direct read | 🛑 Blocker (P0 false-positive class per PROJECT.md's own core value statement) |
| `src/probe/demux_session.cpp:278-280`, `:55-116` | SBR probe keeps the wall-clock interrupt budget armed; `audio.profile`'s compared value can flip to `(sbr: unknown)` under host timing pressure | ✓ Confirmed by direct read (real `std::chrono::steady_clock` budget, deliberately left armed) | 🛑 Blocker (determinism violation; contradicts `checks.def`'s own documented "must never depend on which passes ran" invariant and TRUST-05's spirit) |

No `TBD`/`FIXME`/`XXX` debt markers found in any phase-6-touched file (`grep` swept the full `src/analyzers/audio/`, `src/probe/audio_*`, `src/compare/hash.cpp` set).

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| 5.1 vs 5.1(side) is a regression | `mediadiff compare audio_51.flac audio_51_side.flac --json` | `audio.layout` fail, baseline/candidate differ, channels both 6 | ✓ PASS |
| Implicit/explicit SBR both detected | `mediadiff inspect audio_sbr_{implicit,explicit}.mp4` | `HE-AAC (sbr: implicit)` / `HE-AAC (sbr: explicit)` | ✓ PASS |
| Asymmetric true-peak ceiling crossing | `mediadiff compare audio_loud_floor.flac audio_peak_over.flac --content --json` | fail, message names "asymmetric ceiling crossing" | ✓ PASS (feature present; see CR-04 for the unguarded trigger condition) |
| Dropout reported as a span | `mediadiff compare audio_silence_none.flac audio_dropout.flac --content --json` | `audio.silence.dropouts` fail, one span [3018ms, 3313ms) | ✓ PASS |
| Cross-container hash equality (MP4/MKV/TS) | `mediadiff compare audio_hash_base.{mp4,mkv,ts} --content --json` | all three pairs `pass`, `digests match` | ✓ PASS |
| TRUST-01 evidence fields | `mediadiff snapshot audio_hash_base.mp4 --content` (+ `--hash-decoder default`) | `decode_path`: `{stream_index, decoder, class, flags}` (+`path_signature` for class 2) | ✓ PASS |
| MP4→MKV→MP4 priming round trip | `mediadiff compare audio_prime_base.mp4 audio_prime_roundtrip2.mp4 --json` | `audio.priming` fail, "1024" vs "1014" | ✗ FAIL (this is the expected/documented result — the round trip is NOT stable) |
| Recoverable decode errors gate, don't mark partial | `mediadiff compare audio_corrupt_clean.mp4 audio_corrupt_frames.mp4 --content --json` | `meta.decode_errors` fail (0 vs 7), no `partial:true` | ✓ PASS |
| WINDOWS #38 fix (SBR pass-dependence) | `mediadiff inspect audio_hash_base.mp4` | `audio.profile` = `"LC"` (not `"LC (sbr: unknown)"`) | ✓ PASS |
| PERF-04 wall clock | `scripts/measure_audio_perf.sh` (default mode) | full leg 3.243488s, plain leg 0.006032s | ✓ PASS (under 4s budget) |
| Full ctest suite | `ctest --test-dir build/x64-linux` | 1208/1208 passed, 6 skipped (all expected: designated-leg/platform-conditional goldens) | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` convention exists in this project; this phase does not declare probe scripts. N/A.

### Human Verification Required

None. All roadmap success criteria and the code-review findings above were resolved by direct code reading and live binary execution rather than requiring subjective/visual judgment.

## Gaps Summary

Phase 6 delivers a substantial, well-tested audio decode path: stream parameters, SBR signaling, priming resolution, loudness/true-peak/silence detection, and sample hashing with a careful determinism-class framework, all sharing one decode sweep. Four of five roadmap success criteria's core mechanisms are genuinely implemented and independently confirmed against the real binary with real fixtures — not just claimed in SUMMARY.md.

However, the phase goal text is "moving the decode-determinism classes from shared vocabulary to a **mechanically enforced guarantee**." Two classes of evidence contradict that framing being fully achieved at this commit:

1. **SC2's own literal example (MP4 → MKV → MP4 priming round trip) measurably fails**, and the team's own ledger (`WINDOWS.md` #36) says so in as many words: "contradicts ROADMAP SC2's ... framing." The related `av_offset`/`av_drift` gap (`WINDOWS.md` #32) is also still open for the two MP4-to-TS pairs the original ticket named. This is an honestly-documented, deeply-investigated residual — not a stub or an oversight — but it is a real, unclosed gap against the roadmap text as written.

2. **A same-day code review (`06-REVIEW.md`), itself a phase artifact, found 5 critical defects and none have been fixed as of HEAD.** I independently confirmed 4 of the 5 by reading the exact cited source lines (sample-rate seeding order, missing mid-stream re-validation / heap over-read risk, unclamped float normalization UB, missing deadband on the true-peak ceiling escalation, and a wall-clock-timing-dependent compared value). None of these is exercised by the existing 1208-passing-test corpus — the corpus's only implicit-SBR fixture happens not to trigger the sample-rate bug, and no fixture forces a mid-stream reconfiguration, a non-finite float sample, a knife-edge ceiling crossing, or a slow probe. This is the textbook "task completion ≠ goal achievement" case: every declared task is done, every existing test is green, and the phase still ships committed, reviewer-identified, source-confirmed correctness and robustness defects in the exact code path this phase's goal describes as a "mechanically enforced guarantee."

Recommended next step: route `06-REVIEW.md`'s 5 critical findings (and, at minimum, WR-02/WR-03/WR-07 among the warnings, which also touch correctness) through a gap-closure plan before considering Phase 6 complete, and make an explicit, reviewed decision on SC2's round-trip clause (either accept the residual via a recorded override, given the depth of the investigation, or file the D-14-relaxation / decode-based-TS-priming follow-up work SC2 implies).

---

_Verified: 2026-09-22T17:30:00Z_
_Verifier: Claude (gsd-verifier)_
