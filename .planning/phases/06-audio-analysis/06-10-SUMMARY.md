---
phase: 06-audio-analysis
plan: 10
subsystem: audio-analysis
tags: [ffmpeg, audio-decode, libav, error-handling, exit-codes, fuzz-testing]

requires:
  - phase: 06-audio-analysis
    provides: shared AudioDecodeState decode sweep (hash sink + LoudnessSink + silence/dropout sink), plans 06-06/06-08/06-09
provides:
  - meta.decode_errors registered check (tol/count/int64, tolerance=0) counting recoverable per-stream decode errors as a gating finding
  - AudioDecodeState::finalize() derivation of undecodable as a pure post-drain fact (total_samples==0 && decode_error_count>0), replacing the prior eager mid-sweep guess
  - StreamAudioDecode::first_error_reason (av_strerror-rendered libav failure text, first error only)
  - container_meta_decode_errors_analyzer() -- the one place Fingerprint::partial is populated from live probe data (previously only via snapshot round-trip)
  - partial_scan skip-reason tier extended to every decode-dependent audio check (sample_hash, loudness, silence) when the stream is undecodable
  - three-fixture corrupt/undecodable corpus triple (audio_corrupt_clean/frames/undecodable.mp4) via PRNG mdat-payload corruption
  - unit.probe_fuzz_smoke extended to cover the audio decode pass (PROBE-09 never-crash contract, Security Domain V5)
affects: [07-report-and-policy, any future phase reading Fingerprint::partial or exit code 66]

actuals:
  tokens: 20250
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "undecodable computed as a pure post-drain derivation (total_samples==0 && decode_error_count>0) rather than an eager mid-sweep flag -- correctly handles a stream that hits the consecutive-error DoS limit after already decoding real samples"
    - "first-error-only reason capture via a shared record_first_error() helper wrapping av_strerror(), called at every distinct libav failure site (alloc, send_packet, receive_frame) without disturbing the DoS-mitigation feed-stopping behavior"
    - "MP4 mdat payload corruption for fixture generation: framing lives entirely in stsz outside mdat, so raw PRNG byte corruption anywhere inside mdat corrupts access-unit content without ever disturbing demux-time packet boundaries -- no sample-table parsing needed"

key-files:
  created:
    - docs/checks/meta.decode_errors.md
    - tests/integration/test_audio_decode_errors.cpp
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - src/analyzers/container/meta.cpp
    - src/analyzers/container/analyzers.h
    - src/core/checks.def
    - src/probe/orchestrator.cpp
    - src/analyzers/content/sample_hash.cpp
    - src/analyzers/audio/loudness.cpp
    - src/analyzers/audio/silence.cpp
    - src/analyzers/audio/analyzers.h
    - src/analyzers/content/analyzers.h
    - claude_docs/01-core-concepts.md
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_video_inspect_section.cpp
    - tests/unit/test_probe_fuzz_smoke.cpp
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "D-09 amendment approved at Task 1's checkpoint: a recoverable decode error is a counted, gating meta.decode_errors finding at exit 1; only a wholly undecodable stream (zero decoded frames across the whole sweep) still marks the fingerprint partial and exits 66"
  - "fp.partial = true lives in src/analyzers/container/meta.cpp, not src/probe/audio_decode.cpp -- probe/ holds only sink outputs, never fingerprint-level state, per that layer's own convention; Task 2's literal acceptance-criterion grep target named the wrong file, verified by substance instead (exactly one fp.partial assignment, gated on decode.undecodable)"
  - "undecodable derived post-drain from final counters (total_samples_ == 0 && decode_error_count_ > 0), not guessed eagerly mid-sweep, so a stream that hits the 64-consecutive-error DoS limit after already decoding real samples is correctly NOT undecodable"
  - "partial_scan reused as the skip reason for every decode-dependent audio check when a stream is undecodable (sample_hash, loudness, silence/dropouts), rather than requires_decode or insufficient_data, per Test 4's explicit requirement -- a deliberate semantic reuse of an existing SkipReason value"

patterns-established:
  - "Pattern: raw PRNG byte corruption of an MP4's mdat payload as a fixture-generation recipe for decode-error testing -- scattered runs for recoverable errors, full replacement for undecodable -- avoids needing sample-table (stsz/stco) parsing since mdat carries no self-framing"
  - "Pattern: first-error-only capture (record_first_error, guarded by !out->empty()) as the template for any future per-stream diagnostic that should report cause without becoming a growing/expensive log"

requirements-completed: [AUDIO-08, AUDIO-10]

coverage:
  - id: D1
    description: "meta.decode_errors registered as a gating check counting recoverable decode errors per stream; a stream with some decode errors still reports hash/loudness/silence measurements and gates at exit 1"
    requirement: "AUDIO-08"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_decode_errors.cpp (Behavior Tests 1-3, 5-8)"
        status: pass
      - kind: manual_procedural
        ref: "mediadiff compare tests/fixtures/audio_corrupt_clean.mp4 tests/fixtures/audio_corrupt_frames.mp4 -- exit=1, meta.decode_errors fail 0->7, other audio findings present"
        status: pass
    human_judgment: false
  - id: D2
    description: "Only a wholly undecodable stream (zero decoded frames) marks Fingerprint::partial and exits 66; every decode-dependent audio check reports skipped/partial_scan"
    requirement: "AUDIO-08"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_decode_errors.cpp (Behavior Test 4)"
        status: pass
      - kind: manual_procedural
        ref: "mediadiff compare tests/fixtures/audio_undecodable.mp4 tests/fixtures/audio_corrupt_clean.mp4 -- exit=66, partial=true, six decode-dependent checks skipped/partial_scan"
        status: pass
    human_judgment: false
  - id: D3
    description: "The D-09 amendment to doc 01 section 11's exit-66 rule is recorded in the open, in both docs/checks/meta.decode_errors.md and claude_docs/01-core-concepts.md section 11"
    requirement: "AUDIO-08"
    verification:
      - kind: manual_procedural
        ref: "grep confirms amendment text present in both files; human-verified in Task 3 checkpoint"
        status: pass
    human_judgment: true
    rationale: "Human confirmed both docs state the same amendment as part of Task 3's checkpoint review (documentation consistency is a judgment call, not purely mechanical)"
  - id: D4
    description: "The audio decode pass survives the existing PRNG-seeded byte-flip fuzz smoke (PROBE-09 never-crash contract, Security Domain V5) without crashing, OOB reads, or hangs"
    requirement: "AUDIO-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_probe_fuzz_smoke.cpp -- 'probe_fuzz_smoke - fixed-seed PRNG byte flips never crash the audio decode pass'"
        status: pass
      - kind: other
        ref: "ctest --test-dir build/x64-linux -R unit.probe_fuzz_smoke (13/13)"
        status: pass
    human_judgment: false
  - id: D5
    description: "AUDIO-10 single-decode-sweep guarantee preserved: meta.decode_errors reads decode_error_count/undecodable from the existing shared AudioDecodeState, no second decode pass added"
    requirement: "AUDIO-10"
    verification:
      - kind: other
        ref: "full ctest suite (1186/1186 passing), including pre-existing read_frame_call_count invariance tests unaffected"
        status: pass
    human_judgment: false

duration: 90min
completed: 2026-09-20
status: complete
---

# Phase 06 Plan 10: meta.decode_errors -- Recoverable vs. Undecodable Summary

**`meta.decode_errors` counts recoverable per-stream audio decode errors as a gating exit-1 finding, narrowing the prior "any decode error means exit 66" rule (D-09) to only a wholly undecodable stream (zero decoded frames across the whole sweep).**

## Performance

- **Duration:** 90 min
- **Started:** 2026-09-20T20:19:22Z
- **Completed:** 2026-09-20T21:28:59Z
- **Tasks:** 3
- **Files modified:** 22 (2 created, 20 modified)

## Accomplishments
- Registered `meta.decode_errors` (`group=meta`, `tol/count/int64`, `tolerance="0"`), riding the existing shared audio decode sweep with no second decode pass (AUDIO-10).
- Refactored `AudioDecodeState::finalize()` to derive `undecodable` as a pure post-drain fact (`total_samples_ == 0 && decode_error_count_ > 0`) instead of an eager mid-sweep guess, correctly excluding a stream that hit the consecutive-error DoS limit after already decoding real samples.
- Added `StreamAudioDecode::first_error_reason` (first-error-only, `av_strerror`-rendered libav failure text) via a shared `record_first_error()` helper covering allocation failures, `avcodec_send_packet`, and `avcodec_receive_frame`.
- `container_meta_decode_errors_analyzer()` is the one place `Fingerprint::partial` is now populated from live probe data -- previously it was only ever set via snapshot round-trip. Set exclusively when a stream is wholly undecodable.
- Extended the `partial_scan` skip reason to every decode-dependent audio check (`sample_hash`, `loudness.integrated`/`true_peak`, `silence.edges`/`dropouts`) when the stream is undecodable, so a wholly undecodable stream produces a uniform, machine-readable skip signal across the whole audio-check family rather than a mix of `requires_decode`/`insufficient_data`.
- New three-fixture corpus (`audio_corrupt_clean.mp4`, `audio_corrupt_frames.mp4`, `audio_undecodable.mp4`) generated via PRNG corruption of the MP4 `mdat` payload -- scattered 24-byte runs for the recoverable case (measured `decode_error_count=7`), full replacement for the undecodable case.
- Extended `tests/unit/test_probe_fuzz_smoke.cpp`'s fixed-seed PRNG byte-flip harness to drive the audio decode pass directly (8 offsets), closing PROBE-09's never-crash contract over the new pass per 06-RESEARCH.md Security Domain V5.
- 9 new integration behavior tests (`tests/integration/test_audio_decode_errors.cpp`, 108 assertions) proving both exit-code directions against the real binary, plus a DOC-03 coverage pair.
- Both exit-code directions independently confirmed by a human at Task 3's checkpoint against real CLI output (not just tests).

## Task Commits

Each task was committed atomically:

1. **Task 1: Record D-09's approved amendment (checkpoint resolution)** - `1f68c51` (docs)
2. **Task 2: Implement meta.decode_errors, wire undecodable to Fingerprint::partial, new fixtures, tests** - `ed4ee32` (feat)
3. **Task 3: Human-verify checkpoint** - confirmed by human (`user_response: confirmed`), no separate commit (checkpoint task produced no file changes beyond what Task 2 already delivered)

**Plan metadata:** (this commit) - `docs(06-10): complete meta.decode_errors plan`

## Files Created/Modified
- `docs/checks/meta.decode_errors.md` - full check documentation, including the D-09 amendment and the narrow `undecodable` derivation
- `tests/integration/test_audio_decode_errors.cpp` - 9 behavior tests proving both exit-code directions and doc coverage
- `src/probe/audio_decode.h` / `.cpp` - `first_error_reason` field, `record_first_error()` helper, post-drain `undecodable` derivation, unconditional drain in `finalize()`
- `src/analyzers/container/meta.cpp` / `analyzers.h` - `run_meta_decode_errors()`, `container_meta_decode_errors_analyzer()` (the sole `fp.partial = true` site)
- `src/core/checks.def` - `meta.decode_errors` registration
- `src/probe/orchestrator.cpp` - analyzer registered in `all_analyzers()`
- `src/analyzers/content/sample_hash.cpp`, `src/analyzers/audio/loudness.cpp`, `src/analyzers/audio/silence.cpp` (+ their `analyzers.h` doc comments) - `undecodable` -> `partial_scan` skip wiring
- `claude_docs/01-core-concepts.md` - amendment note against section 11
- `scripts/gen_corpus.sh` - the new corrupt/undecodable fixture generation recipe (PRNG `mdat` corruption)
- `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - three new fixture lines (locally-computed hashes, provisional pending designated-leg transcription), no pre-existing line rewritten
- `tests/golden/list_checks_effective.txt` - regenerated (`meta.decode_errors` row added) via `check_golden()`, the safe-to-regenerate class
- `tests/integration/CMakeLists.txt` - new test file registered
- `tests/integration/test_doc03_coverage.cpp` - `meta.decode_errors` declared fixture pair (running total 90 -> 91, closing Phase 6's full 14-id roster)
- `tests/integration/test_video_inspect_section.cpp` - the 3 new audio-only fixtures added to `kNoVideoStreamFixtures`
- `tests/unit/test_probe_fuzz_smoke.cpp` - new fuzz test case driving the audio decode pass directly
- `tests/fixtures/GENERATOR_MANIFEST.json` - regenerated timestamp (byproduct of running `gen_corpus.sh`)

## Decisions Made
- `fp.partial = true` placed in `src/analyzers/container/meta.cpp` rather than `src/probe/audio_decode.cpp` (Task 2's literal acceptance-criterion grep target), since `probe/` holds only sink outputs, never fingerprint-level state, per that layer's established convention. Verified by substance: exactly one `fp.partial = true;` assignment exists, gated on `decode.undecodable`, never on `decode_error_count` directly.
- `undecodable` computed post-drain from final counters rather than eagerly mid-sweep, to correctly exclude a stream that hits the 64-consecutive-error DoS limit after already decoding real samples from being misclassified as undecodable.
- `partial_scan` reused (not a new `SkipReason` value) as the skip signal for every decode-dependent audio check when undecodable, per Test 4's explicit requirement -- a deliberate semantic reuse of an existing enum value rather than adding a new one.
- Fixture corruption targets the MP4 `mdat` payload directly with raw PRNG bytes rather than parsing/rewriting the sample table, since `mdat`'s access units carry no self-framing (framing lives entirely in `stsz`) -- byte corruption there never disturbs demux-time packet boundaries.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `src/probe/orchestrator.cpp` omitted from the plan's `files_modified` list**
- **Found during:** Task 2
- **Issue:** The new `container_meta_decode_errors_analyzer()` must be registered in `all_analyzers()` for the check to ever run; the plan's file list didn't include this file.
- **Fix:** Registered the analyzer at the end of `all_analyzers()`, after `audio_silence_analyzer()`.
- **Files modified:** `src/probe/orchestrator.cpp`
- **Verification:** `meta.decode_errors` appears in `--json` output and `list-checks --effective`.
- **Committed in:** `ed4ee32` (Task 2 commit)

**2. [Rule 1 - Bug] `content.audio.sample_hash` reported `requires_decode` instead of `partial_scan` when undecodable**
- **Found during:** Task 2, while implementing Test 4 ("every decode-dependent audio check reports skipped:partial_scan")
- **Issue:** `src/analyzers/content/sample_hash.cpp`'s existing `undecodable` branch used `SkipReason::requires_decode`, which the plan's own Test 4 explicitly requires to be `partial_scan`.
- **Fix:** Changed the skip reason to `SkipReason::partial_scan` with an explanatory comment.
- **Files modified:** `src/analyzers/content/sample_hash.cpp`
- **Verification:** `tests/integration/test_audio_decode_errors.cpp` Behavior Test 4 passes.
- **Committed in:** `ed4ee32` (Task 2 commit)

**3. [Rule 1 - Bug] `audio.loudness.*` and `audio.silence.*` silently absorbed `undecodable` as `insufficient_data`**
- **Found during:** Task 2, same Test 4 requirement
- **Issue:** `src/analyzers/audio/loudness.cpp` and `silence.cpp` had no explicit `undecodable` branch; an undecodable stream fell through to the generic `!loudness_measured`/`!silence_measured` check and reported `insufficient_data`, not `partial_scan`.
- **Fix:** Added an explicit `if (decode.undecodable) { push_skip(...partial_scan...); continue; }` branch before the existing measured-check in both files, plus doc-comment updates in `src/analyzers/audio/analyzers.h` and `src/analyzers/content/analyzers.h` describing the new skip-reason tier.
- **Files modified:** `src/analyzers/audio/loudness.cpp`, `src/analyzers/audio/silence.cpp`, `src/analyzers/audio/analyzers.h`, `src/analyzers/content/analyzers.h`
- **Verification:** `tests/integration/test_audio_decode_errors.cpp` Behavior Test 4 passes (all six decode-dependent checks report `partial_scan`).
- **Committed in:** `ed4ee32` (Task 2 commit)

**4. [Rule 1 - Bug] `test_video_inspect_section.cpp`'s whole-corpus enumeration failed on the 3 new audio-only fixtures**
- **Found during:** Full suite run after Task 2's initial implementation
- **Issue:** This pre-existing test enumerates every `.mp4`/`.mkv`/`.ts` fixture and expects 9 video-identity checks unless the fixture is in the hand-maintained `kNoVideoStreamFixtures` list; the 3 new audio-only fixtures (no video stream) weren't in that list, causing 27 failed assertions.
- **Fix:** Added `audio_corrupt_clean.mp4`, `audio_corrupt_frames.mp4`, and `audio_undecodable.mp4` to `kNoVideoStreamFixtures`, each with an explanatory comment.
- **Files modified:** `tests/integration/test_video_inspect_section.cpp`
- **Verification:** Full test case re-run, 3480 assertions in 2 test cases pass.
- **Committed in:** `ed4ee32` (Task 2 commit)

---

**Total deviations:** 4 auto-fixed (1 blocking/Rule 3, 3 bugs/Rule 1)
**Impact on plan:** All four fixes were required either to make the check run at all (orchestrator registration) or to satisfy the plan's own explicit Test 4 requirement and keep the full test suite green. No scope creep -- every fix is a direct, necessary consequence of this plan's new check and new fixtures.

## Issues Encountered
- Investigated an apparent "ms = num * 1000" rendering anomaly in `audio.silence.edges`/`dropouts` JSON output while sanity-checking output shape. Reproduced it against a pre-existing, untouched Phase 06-09 fixture pair (`audio_silence_none.flac` vs `audio_silence_lead.flac`), confirming the anomaly pre-dates this plan and is unrelated to `meta.decode_errors`. Left untouched per the deviation rules' scope boundary (pre-existing issues unrelated to the current task are out of scope); not part of this plan's deliverables.
- A full-corpus `scripts/corpus_digest.sh` diff initially showed nearly every pre-existing fixture's hash differing from the committed `CORPUS_DIGEST.txt`. Correctly diagnosed as expected (this workstation is not the designated CI leg; AAC/video encoder output is not byte-stable across hosts even with `-flags +bitexact`, per 06-CONTEXT.md's own documented finding) -- resolved by adding only the 3 new fixtures' locally-computed hashes as new lines, per the project's own stated policy, leaving all 80+ pre-existing lines untouched (verified via `lint_corpus_digest_provenance.sh`'s clause-4 no-rewrite guard).

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- `meta.decode_errors` closes the D-09 amendment loop: recoverable decode errors are now counted and gate at exit 1; only a genuinely undecodable stream exits 66. Phase 6's full 14-id check roster is now complete (DOC-03 coverage: 91/91).
- `Fingerprint::partial` now has its first real (non-snapshot-round-trip) populator, established as the pattern any future "this stream/track could not be analyzed at all" case should follow.
- The `partial_scan` skip-reason tier is now consistently applied across every decode-dependent audio check family (hash, loudness, silence/dropouts) -- the template for any future audio check consuming `StreamAudioDecode`.
- No blockers identified for continuing Phase 06 (plans 11-13 remain).

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

All 24 files claimed as created/modified verified present on disk (`[ -f ... ]`). All 3 commits verified present in `git log --oneline --all`: `1f68c51` (Task 1), `ed4ee32` (Task 2), `bfcbaee` (docs: Task 3 confirmation + summary).
