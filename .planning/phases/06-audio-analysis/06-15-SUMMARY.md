---
phase: 06-audio-analysis
plan: 15
subsystem: audio
tags: [audio-decode, sample-rate, heap-over-read, cr-01, cr-02, gap-closure]

requires:
  - phase: 06-audio-analysis
    provides: 06-14's decode-stop-reason vocabulary (kDecodeStopConsecutiveErrorLimit), latch_decode_truncation, and the level-check partial_scan skip branches that this plan's decoded_* tokens reuse unchanged
provides:
  - "src/probe/audio_decode.h/.cpp: StreamAudioDecode::declared_sample_rate (codecpar's rate at ensure_initialized, diagnostic only); sample_rate is now documented and implemented as the DECODED output rate; the four decoded_* stop-token constants (kDecodeStopChannelsChanged/SampleFormatChanged/SampleRateChanged/ChannelLayoutChanged); detail::AudioDecodeState::consume_frame_for_test test seam"
  - "consume_frame's per-frame re-validation: every decoded frame after lazy-init is compared, in order, against the recorded channel count, packed-equivalent format, effective rate and described layout -- the first mismatch latches its own token and returns before the frame reaches any sink"
  - "docs/checks/content.audio.sample_hash.md's decoded-rate basis and the four new Decode stop reasons rows; audio.loudness.integrated.md/audio.silence.edges.md each note the decoded-rate basis for their own rate-derived math"
  - "WINDOWS.md ledger entry #40: the CR-01 residual (a stream whose first packets fail inside find_stream_info but decode later) recorded as unrun-verify, not claimed"
affects: [06-16-cr03-non-finite-guards, 06-18-wr-09-naming, 06-20-designated-leg-confirmation]

actuals:
  tokens: 9053
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A configuring value seeded from a decoded frame, not a container header field, even when the header field is usually right -- CR-01's own generalizable lesson: an 'ahead of decode' field is trustworthy only when a documented, in-code reason explains why (here, only after find_stream_info's own internal decode already corrected it)."
    - "A per-frame shape re-validation held to the FIRST frame's own recorded configuration, checked in a fixed, named order (channels, format, rate, layout) so first-match-wins is deterministic and testable independent of the corpus."
    - "A test seam (consume_frame_for_test) that forwards directly to a private method, named with the project's own _for_test convention (src/analyzers/container/meta.cpp's sanitize_utf8_for_test precedent), used when a real fixture structurally cannot construct the state under test."

key-files:
  created: []
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - tests/unit/test_audio_decode.cpp
    - docs/checks/content.audio.sample_hash.md
    - docs/checks/audio.loudness.integrated.md
    - docs/checks/audio.silence.edges.md
    - .planning/WINDOWS.md

key-decisions:
  - "CR-01: promote the decoded rate to primary; codecpar's rate becomes declared_sample_rate, diagnostic-only, never configuring anything -- keeping the declared rate as primary would have preserved exactly the load-bearing external invariant CR-01 exists to remove (per the plan's own assumption_delta_decision)."
  - "CR-01's RED/GREEN captured mechanically (06-14's own precedent): temporarily reverting the fix's two behavior lines, rebuilding, running the new test in isolation to record the failure, then restoring the fix -- rather than attempting a literal step-by-step mid-implementation RED that the header/cpp split makes awkward to reproduce exactly."
  - "CR-02's comparison runs unconditionally right after the lazy-init block (including on the very first frame, a trivial self-match) rather than in an if/else split -- simpler code, identical behavior, and matches the plan's own action-item ordering literally."
  - "The corpus differential found the two binaries' snapshot output BYTE-IDENTICAL with no masking needed at all -- this project's snapshot schema does not yet embed a tool-version or build-identity field, so there was nothing to mask; recorded as an observation, not treated as suspicious."

patterns-established:
  - "Corpus-wide GAP_BASE-vs-HEAD differential recipe: git archive the base commit into scratch, configure against the MAIN repo's already-built vcpkg_installed (VCPKG_MANIFEST_INSTALL=OFF, no FFmpeg rebuild), build only the mediadiff target, snapshot every real fixture from the main repo's tests/fixtures/ with both binaries, diff pairs, delete scratch afterward. Reusable by any future gap-closure plan needing a no-corpus-change proof."

requirements-completed: [AUDIO-08, AUDIO-10]

coverage:
  - id: D1
    description: "CR-01: the sweep configures block_samples, the loudness sink and the silence/dropout windows from the first DECODED frame's rate, never codecpar's declared rate -- proven on the one real stream in this corpus (audio_sbr_implicit.mp4, opened before find_stream_info) where the two disagree (44100 declared vs 88200 decoded, oracle-verified)."
    requirement: "AUDIO-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - the sweep is configured from the decoded frame's rate even when codecpar declares a different one (CR-01)"
        status: pass
    human_judgment: false
  - id: D2
    description: "CR-01: the normal DemuxSession open path (find_stream_info already ran) reports sample_rate == declared_sample_rate == 88200 on the same fixture, so no real corpus output moves."
    requirement: "AUDIO-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - the normal open path decodes and declares the same rate"
        status: pass
    human_judgment: false
  - id: D3
    description: "CR-02: a mid-stream channel count, sample format, sample rate, or channel layout change stops the sweep with its own decoded_* token, in that fixed check order, and feeds nothing further to any sink (closing the T-06-49 heap-over-read)."
    requirement: "AUDIO-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a mid-stream channel count change stops the sweep"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a mid-stream sample format change stops the sweep"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a mid-stream sample rate change stops the sweep"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a mid-stream channel layout change stops the sweep"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a frame changing both channels and format reports the channels token first"
        status: pass
    human_judgment: false
  - id: D4
    description: "CR-02 (D-02): a planar and a packed frame of the SAME format holding the same values are NOT a change -- both frames are counted and the chain_digest equals an all-packed run of the same values."
    requirement: "AUDIO-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - a planar and a packed frame of the same format are not a change"
        status: pass
    human_judgment: false
  - id: D5
    description: "CR-02: once latched, a later frame matching the original configuration is still not counted, and feed_packet is a total no-op (decode_error_count never moves)."
    requirement: "AUDIO-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - after a latch, a matching frame is not counted and feed_packet is a no-op"
        status: pass
    human_judgment: false
  - id: D6
    description: "The decoded-rate basis and the four decoded_* tokens are documented where mediadiff explain surfaces them, and the corpus-wide differential proves this plan changed no real fixture's output."
    verification:
      - kind: integration
        ref: "full ctest --test-dir build/x64-linux (1226 passed, 0 failed) + mediadiff explain content.audio.sample_hash + a 200-fixture GAP_BASE-vs-HEAD snapshot differential (0 differing)"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-09-23
status: complete
---

# Phase 06 Plan 15: Decoded-frame configuration and per-frame re-validation (CR-01/CR-02) Summary

**The audio sweep now configures its sinks from the decoder's actual output rate instead of an undocumented external invariant in codecpar, and every decoded frame is re-validated against the first frame's shape before reaching any sink -- both closed with in-process tests since no real corpus fixture reproduces either divergence.**

## Performance

- **Duration:** 35 min
- **Started:** 2026-09-23T21:45:00Z
- **Completed:** 2026-09-23T22:12:04Z
- **Tasks:** 3
- **Files modified:** 7 (3 source/test, 3 docs, 1 ledger)

## Accomplishments

- `StreamAudioDecode::sample_rate` is now the DECODED output rate (the first decoded frame's own `sample_rate`), with `declared_sample_rate` split out as a diagnostic-only field carrying codecpar's rate at `ensure_initialized()`. Every sink (`block_samples`, the loudness sink, the silence/dropout window lengths) follows the decoded rate, closing CR-01's "correct by accident" load-bearing external invariant on `avformat_find_stream_info`.
- A new CR-01 test drives `AudioDecodeState` directly against `audio_sbr_implicit.mp4`'s codecpar read BEFORE `avformat_find_stream_info` runs (declared 44100), cross-checked against an independent bare `aac_fixed` oracle decode of the same packets (88200) -- the one real stream in this corpus where the two values disagree. A second test confirms the normal `DemuxSession` open path is unaffected: `sample_rate == declared_sample_rate == 88200`, so no real corpus output moves.
- Every decoded frame is now held to the configuration the first frame recorded -- channel count, packed-equivalent sample format, effective sample rate, and described channel layout, checked in that fixed order. The first mismatch calls `latch_decode_truncation` with its own `decoded_*` token and returns before the frame reaches `total_samples`, the hash chain, `LoudnessSink`, or the silence detector -- closing T-06-49's heap-over-read shape (a mid-stream channel narrowing previously kept feeding a shrunk buffer to a sink still sized for the original channel count).
- `feed_packet`'s early-return guard now also fires once a CR-02 mismatch has latched, so the decoder is never fed again after any stop -- mirroring 06-14's consecutive-error-limit latch behavior exactly.
- A new `consume_frame_for_test()` seam drives hand-built `AVFrame`s directly, since none of CR-02's mismatch shapes (a mid-stream channel/format/rate/layout change) are constructible from real media. Seven new tests cover all four tokens individually, the channels-vs-format check-order tiebreak, the D-02 planar/packed non-mismatch (byte-identical `chain_digest` against an all-packed run), and the post-latch no-op guarantee.
- Documented the decoded-rate basis in `content.audio.sample_hash.md` (plus the reason: an implicitly-signalled HE-AAC stream's header rate is the undoubled core rate) and appended the four `decoded_*` rows to the "Decode stop reasons" table; `audio.loudness.integrated.md`/`audio.silence.edges.md` each gained one sentence noting the same decoded-rate basis for their own rate-derived math.
- A corpus-wide differential (GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e` vs HEAD) over all 200 non-hidden fixtures in `tests/fixtures/` found **0 differing snapshots** -- every pair was byte-identical with no masking needed (this snapshot schema carries no tool-version/build-identity field to mask). Scratch tree deleted afterward.
- Recorded the CR-01 residual (a stream whose first packets fail inside `avformat_find_stream_info` but decode later, not constructible with real media) as WINDOWS.md ledger entry #40, naming the CR-01 oracle test that covers its end state in-process.
- Full `ctest`: 1226 passed, 0 failed (9 new tests added over 06-14's 1217 baseline; 6 pre-existing designated-leg-only skips unchanged). `git diff --exit-code -- tests/golden/` exits 0 throughout -- no fixture was generated or regenerated.

## Task Commits

Each task was committed atomically:

1. **Task 1: The sweep configures every sink from the decoded frame's rate, proven on the one real stream whose header rate disagrees** - `258df55` (feat)
2. **Task 2: Every decoded frame is held to the recorded configuration; a mismatch stops the sweep and feeds nothing** - `d4c1a05` (feat)
3. **Task 3: Document the decoded-rate basis and the new tokens; prove the corpus unchanged; record the CR-01 residual** - `ef9cae9` (docs)

## Files Created/Modified

- `src/probe/audio_decode.h` - `StreamAudioDecode::declared_sample_rate`; the four `decoded_*` stop-token constants; `AudioDecodeState`'s `configured_packed_format_`/`declared_sample_rate_` private members; the `consume_frame_for_test` test seam declaration
- `src/probe/audio_decode.cpp` - `ensure_initialized` records `declared_sample_rate_` instead of seeding `sample_rate_`; `consume_frame`'s lazy-init sets `sample_rate_` unconditionally from the frame (falling back to `declared_sample_rate_`); the new per-frame re-validation block; `feed_packet`'s shared early-return guard; `finalize()` surfaces `declared_sample_rate`; both move operations carry the two new members
- `tests/unit/test_audio_decode.cpp` - `make_pcm_s16le_codecpar`/`make_test_frame` helpers; the CR-01 oracle test and normal-open-path test; five CR-02 mismatch-token tests, the D-02 planar/packed equivalence test, and the post-latch no-op test
- `docs/checks/content.audio.sample_hash.md` - decoded-rate basis sentence; four new "Decode stop reasons" rows; a CR-02 check-order paragraph
- `docs/checks/audio.loudness.integrated.md`, `docs/checks/audio.silence.edges.md` - one decoded-rate-basis sentence each
- `.planning/WINDOWS.md` - ledger entry #40 (unrun-verify, the CR-01 residual)

## Decisions Made

- **Promote the decoded rate to primary (CR-01).** `declared_sample_rate` is diagnostic-only and never configures anything -- keeping the declared rate as the configuring value "alongside" the decoded one would have preserved exactly the load-bearing external invariant this plan exists to remove.
- **RED captured mechanically, mirroring 06-14's own precedent:** for both CR-01 and CR-02, the fix's behavior-changing lines were temporarily reverted, the binary rebuilt, the new test run in isolation to record the real failure output, then the fix restored and the full filter re-run to confirm GREEN. This produced exact, verified RED numbers (CR-01: `sample_rate` 44100/`block_samples` 4410; CR-02: `total_samples` 2048/`loudness_measured` true) rather than predicted-but-unverified ones.
- **CR-02's comparison runs on every frame, including the first** (a trivial self-match against what lazy-init just recorded from it), rather than being gated to "frame 2+" via an if/else split -- simpler, and behaviorally identical since the first frame's own freshly-recorded configuration cannot mismatch itself.
- **The corpus differential found zero raw byte differences with no masking needed at all.** The plan anticipated masking tool-version/build-identity fields; inspection showed this project's snapshot schema does not yet emit either (schema/tool version are fixed literals, `path_signature` is class-2-only and absent from the class-1 fixtures compared here in the field that would differ) -- there was nothing to mask, and the result is stronger than "identical after masking": byte-identical outright.

## Deviations from Plan

None - plan executed exactly as written. One clarification worth recording: the plan's action text for Task 3's cmake invocation reads `-DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/...` and `-DVCPKG_INSTALLED_DIR=$PWD/build/x64-linux/vcpkg_installed`, which only resolves correctly when `$PWD` is the MAIN repo root (not the scratch extraction) with `-S`/`-B` pointed at the scratch tree separately -- `git archive` does not include submodule content, so the scratch copy's own `vcpkg/` directory is empty. This matches the referenced memory doc's own established recipe (`designated-leg-golden-differential.md`: "Configure each with ... `<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake` ... `<repo>/build/x64-linux/vcpkg_installed`") and required no source-code change, just running the configure command from the correct working directory.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- CR-01 and CR-02 are both closed; the WR-09 naming concern is explicitly deferred to 06-18 per this plan's own scope note (the field keeps its `sample_rate`/`declared_sample_rate` names; renaming to `decoded_sample_rate` is WR-09's own fix, out of this plan's scope).
- The `decoded_*` stop-token vocabulary now has 5 total members (06-14's `consecutive_decode_error_limit` plus this plan's four) in exactly one place each (`src/probe/audio_decode.h` and the doc table) -- ready for any future plan to append further tokens without repurposing existing ones.
- 06-13 remains `status: halted` (its confirming CI run is still pending) -- unaffected by this plan.
- Ready for 06-16 (CR-03 non-finite guards), which also measures the audio perf ratchet after both hot-path plans have landed (this plan's own flagged Perf flag: one integer comparison per frame for channels/format/rate, plus one layout describe into a stack buffer, ~25,800 frames on the 600s reference).

## Self-Check: PASSED

- `258df55`, `d4c1a05`, `ef9cae9` all found in `git log --oneline --all`.
- All 7 modified files exist on disk with the expected content (verified via the `grep`/build/test commands run during execution).
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1226 passed, 0 failed (6 pre-existing designated-leg-only skips, unchanged from 06-14's baseline).
- `git diff --exit-code -- tests/golden/`: clean (exit 0).
- Corpus differential: 200/200 fixtures byte-identical, 0 differing, GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e`.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-23*
