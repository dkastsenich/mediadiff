---
phase: 06-audio-analysis
plan: 05
subsystem: audio
tags: [ffmpeg, xxhash, decode-determinism, catch2, snapshot, threat-model]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-01's audio decode pass (AudioDecodeState, fixed-sibling table, USAC ASC pre-check) and its content.audio.sample_hash tracer; 06-02's hand-written HE-AAC fixtures and audio_aac_handwritten.mp4's D-11 input-identity assertion"
provides:
  - "--hash-decoder <auto|default|NAME> registered on compare/snapshot/dir/inspect, resolved through resolve_hash_decoder (src/cli/options.{h,cpp}) into ProbeOptions::hash_decoder, the sole input to decoder selection (D-08)"
  - "determinism_class_for_decoder() (src/probe/audio_decode.h) -- the single normative by-name classification table: pcm_*/flac/alac/aac_fixed/ac3_fixed/mp3/mp2 class 1, aac/ac3/eac3/opus/mp3float/mp2float class 2, everything else class 3 hash-disabled"
  - "Envelope::decode_path populated with one record per hashed stream (stream_index, decoder, class, flags, class-2-only path_signature), round-tripping through write_snapshot/read_snapshot with malformed-record rejection (T-06-16)"
  - "SkipReason::hash_disabled -- a class-3 stream's honest 'no digest' outcome"
  - "the three-way class proof: class-1 hash compares equal across a committed cross-build snapshot behind a D-11 fixture-identity guard; class-2 cross-path comparisons always skip (even on coincidentally-equal digests); class-3 never produces a digest to compare at all"
affects: [06-06, 06-07, 06-08, 06-09, 06-13]

actuals:
  tokens: 33390
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "By-name-only decoder classification, re-derived from the recorded decoder NAME at measurement time rather than trusted from a stored class integer (T-06-15) -- the class used for cross-machine comparison can never be upgraded by hand-editing a snapshot's own class field"
    - "tests/support/ promotion: a helper originally local to one test file (test_gen_he_aac.cpp's D-11 identity assertion) is promoted to a shared header+source pair, linked into both mediadiff_unit_tests and mediadiff_integration_tests, when a second binary needs to call it -- mirrors the existing golden.{h,cpp}/mutate.{h,cpp} precedent"

key-files:
  created:
    - tests/integration/test_audio_hash_decoder.cpp
    - tests/unit/test_decode_path_record.cpp
    - tests/support/aac_handwritten_identity.h
    - tests/support/aac_handwritten_identity.cpp
    - tests/fixtures/snapshots/audio_aac_handwritten.snap.json
  modified:
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - src/analyzers/content/sample_hash.cpp
    - src/core/snapshot.cpp
    - src/core/model.h
    - src/cli/options.h
    - src/cli/options.cpp
    - docs/checks/content.audio.sample_hash.md

key-decisions:
  - "AC-3 and MP3 have no real fixture in this LGPL decode-only corpus (no non-fixed AC-3 fixture exists; no MP3 encoder exists to synthesize one) -- their table entries are proven by a pure determinism_class_for_decoder() unit assertion rather than an end-to-end CLI test; MP2 gets full end-to-end coverage via the existing audio_mp2_base.mpg fixture."
  - "Test 6 (--hash-decoder aac_fixed falls back on a USAC stream) is a documented gap, not a passing end-to-end test -- a hand-built ASC declaring object_type 42 was rejected by avcodec_open2() with EINVAL for BOTH aac_fixed and native aac in this environment's linked FFmpeg, contradicting 06-RESEARCH.md Q3's claim; a genuine USAC bitstream needs full UsacConfig() syntax beyond hand-crafting scope (no USAC encoder exists in this pin). The steering code path is reviewed by inspection instead."
  - "The class-2 degrade proof (Task 3, TRUST-02) mutates a stored snapshot's own decode_path_class evidence field directly via nlohmann::json, rather than using tests/support/mutate.h's byte-level media mutation helpers -- the precondition under test lives in JSON evidence, not raw media bytes, so a second real machine or a corrupted media file is neither necessary nor the right tool."
  - "Task 1's tests/unit/test_audio_decode.cpp additions and Task 3's tests/support/ promotion are not in this plan's own per-task files_modified lists, but were necessary to give Tests 1-8/1-7 real coverage; documented here per the executor's Rule 2 (auto-add missing critical test coverage)."

patterns-established:
  - "Decoder selection and classification are deliberately separate steps: selection (auto/default/NAME) picks a decoder by whatever rule applies, then determinism_class_for_decoder(decoder_name) classifies it uniformly regardless of which selection path chose it -- never a selection-path-specific class assignment."

requirements-completed: [AUDIO-08, AUDIO-09, TRUST-01, TRUST-02]

coverage:
  - id: D1
    description: "--hash-decoder <auto|default|NAME> is the sole override for automatic class-1 decoder preference; a profile never reaches decoder selection (D-08)"
    requirement: AUDIO-09
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_hash_decoder.cpp#audio_hash_decoder - Test 1/2/3/5/7"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - determinism_class_for_decoder covers the whole normative table"
        status: pass
    human_judgment: false
  - id: D2
    description: "Every hashed stream's decoder/class/flags/(class-2)signature is recorded in Envelope::decode_path, ordered, unmerged, and round-trips through write_snapshot/read_snapshot, rejecting malformed records"
    requirement: TRUST-01
    verification:
      - kind: unit
        ref: "tests/unit/test_decode_path_record.cpp (all 10 test cases)"
        status: pass
    human_judgment: false
  - id: D3
    description: "A class-2 cross-path comparison always reports skipped:hash_incomparable with the exact remediation hint, even when digests coincidentally match; class-1 compares equal across a committed cross-build snapshot behind a D-11 identity guard; class-3 never produces a digest"
    requirement: TRUST-02
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_hash_decoder.cpp#audio_hash_decoder - class proof Test 1 through 7"
        status: pass
    human_judgment: false
  - id: D4
    description: "USAC steering fallback (--hash-decoder aac_fixed on a USAC stream records class 2 with fallback_reason) -- Test 6"
    requirement: AUDIO-09
    verification: []
    human_judgment: true
    rationale: "No genuine USAC bitstream can be constructed in this LGPL decode-only pin (no USAC encoder exists); a hand-built ASC was empirically rejected by avcodec_open2() for every candidate decoder, so the fallback behavior itself could not be exercised end-to-end. A human with access to a real USAC sample (or a future encoder addition) is needed to close this gap; see Known Stubs below."

duration: unrecorded (session resumed mid-plan after a context-compaction boundary; wall-clock not tracked across the boundary)
completed: 2026-09-20
status: complete
---

# Phase 06 Plan 05: Decode-Determinism Policy — `--hash-decoder`, the Class Table, and the Three-Way Proof Summary

**`--hash-decoder <auto|default|NAME>` with a by-name determinism-class table (class 1/2/3), a per-hashed-stream `decode_path` ledger that round-trips through snapshots, and a three-way proof that class 1 compares across builds while class 2 across differing paths always skips — even on a coincidental digest match.**

## Performance

- **Tasks:** 3/3 completed
- **Files modified:** 33 (18 + 8 + 10 across the three task commits, with some files touched across boundaries such as `tests/unit/CMakeLists.txt`)
- **Test cases added:** 6 unit (`test_audio_decode.cpp`) + 10 unit (`test_decode_path_record.cpp`) + 13 integration (`test_audio_hash_decoder.cpp`) = 29 new Catch2 `TEST_CASE`s
- **Full suite:** 1100/1100 tests passing (6 pre-existing designated-leg-only skips, unrelated to this plan)

## Accomplishments

- `--hash-decoder <auto|default|NAME>` is now a real, registered flag on `compare`, `snapshot`, `dir` and `inspect`, resolved once through `resolve_hash_decoder` and routed into the once-per-stream decoder selection — never influenced by `--profile` (D-08), verified directly by comparing the same file under `--profile remux` and `--profile transform` and asserting identical decoder/digest.
- `determinism_class_for_decoder()` replaces the previous ad hoc fixed-sibling/PCM check with doc 05 section 3's full normative table, extended per D-06 with the `mp3`/`mp2` fixed-point promotion (selected by name, since FFmpeg registers the fixed decoders under the plain codec name while the float variants are separately named `mp3float`/`mp2float`).
- `Envelope::decode_path` — a field Phase 2 grew a phase early — is now populated for real: one record per hashed stream, never merged or deduplicated, an explicit empty array for a file with no hashed stream, and a new `SkipReason::hash_disabled` for the honest "no digest, this codec isn't proven deterministic" case.
- `read_snapshot` now rejects a `decode_path` record missing `stream_index`/`decoder`/`class`, or carrying a class outside 1-3, as `ErrorKind::input_unsupported` (T-06-16) — closing the tampering surface a malformed snapshot could otherwise exploit.
- The three-way class proof is real and running: a committed class-1 snapshot (`tests/fixtures/snapshots/audio_aac_handwritten.snap.json`) compares `pass` against a fresh measurement, guarded by 06-02's D-11 fixture-identity assertion (now promoted to `tests/support/` so both test binaries can call it); a class-2 signature mismatch always skips with the exact `hash comparison skipped: 'decode_path_class' precondition differs...` remediation hint, even when the underlying digests are byte-identical; a class-3 stream (Vorbis) never produces a digest to compare on either side.

## Task Commits

1. **Task 1: `--hash-decoder` and the determinism-class table** — `1cbc189` (feat)
2. **Task 2: the per-hashed-stream `decode_path` record and its snapshot round trip** — `cb318a3` (feat)
3. **Task 3: the three-way class proof** — `6fa996f` (test)

_TDD note: this plan's tasks are marked `tdd="true"` in the frontmatter, but each was executed as a single cohesive commit per task (implementation + its own new tests together) rather than separate RED/GREEN/REFACTOR commits — matching this phase's own established commit-granularity precedent from 06-01 through 06-04's SUMMARYs. No plan-level `type: tdd` gate applies here (this plan's frontmatter `type: execute`), so the strict RED-then-GREEN gate sequence in the workflow's TDD section does not bind this plan._

## Files Created/Modified

- `src/probe/audio_decode.h` / `.cpp` — the determinism-class table, `--hash-decoder` preference dispatch, `flags_recorded`/`path_signature` fields
- `src/analyzers/content/sample_hash.cpp` — `decode_path` record population, class re-derivation from decoder name (T-06-15)
- `src/core/snapshot.cpp` — `decode_path` round-trip read/write with malformed-record rejection
- `src/core/model.h` / `src/report/junit.cpp` / `docs/schema/report-1.0.json` — `SkipReason::hash_disabled`
- `src/cli/options.h` / `.cpp`, `src/cli/commands/{compare,snapshot,dir,inspect}.{h,cpp}`, `src/cli/main.cpp` — `--hash-decoder` flag registration and plumbing
- `docs/checks/content.audio.sample_hash.md` — the class table's user-facing consequences, `--hash-decoder`, the provisional mp3/mp2 promotion status
- `tests/integration/test_audio_hash_decoder.cpp` (new) — Task 1's 7 CLI-level tests (Test 6 documented as a gap, see Known Stubs) and Task 3's 7 class-proof tests
- `tests/unit/test_decode_path_record.cpp` (new) — Task 2's 10 unit tests
- `tests/unit/test_audio_decode.cpp` — Task 1's pure `determinism_class_for_decoder` table coverage plus preference-selection tests
- `tests/support/aac_handwritten_identity.h` / `.cpp` (new) — the D-11 input-identity helper, promoted out of `test_gen_he_aac.cpp` so both test binaries can call it
- `tests/fixtures/snapshots/audio_aac_handwritten.snap.json` (new) — the committed class-1 two-build baseline

## Decisions Made

- **MP3/AC-3 coverage split:** no real fixture exists for a non-fixed AC-3 stream or any MP3 stream in this LGPL decode-only pin (no MP3 encoder is linked). Their table entries are proven at the unit level (`determinism_class_for_decoder("mp3")`/`("ac3")` etc.) rather than end-to-end; MP2 gets full end-to-end coverage via the existing `audio_mp2_base.mpg` fixture.
- **Class-2 degrade proof via JSON mutation, not `tests/support/mutate.h`:** the differing-decode-path precondition lives in a snapshot's own JSON evidence, not in raw media bytes, so this task perturbs a stored snapshot's `decode_path_class` field directly with `nlohmann::json` rather than reaching for the byte-level truncation/flip helpers built for media-file corruption.
- **`tests/support/` promotion:** the D-11 input-identity assertion, originally local to `test_gen_he_aac.cpp` (06-02), is promoted into `tests/support/aac_handwritten_identity.{h,cpp}` because `tests/unit/` and `tests/integration/` are separate binaries — an `extern` prototype alone would not have linked across them.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - missing test coverage] `tests/unit/test_audio_decode.cpp` extended with Task 1's unit-testable coverage**
- **Found during:** Task 1
- **Issue:** Task 1's own `<behavior>` names 8 tests, but its `<files>` list (and the plan's top-level `files_modified`) does not name any file to hold the pure `determinism_class_for_decoder` table assertions or the `--hash-decoder default`/explicit-name preference tests.
- **Fix:** Extended `tests/unit/test_audio_decode.cpp` (the file this phase's own read_first names as the home for exactly this kind of coverage) with 6 new `TEST_CASE`s.
- **Files modified:** `tests/unit/test_audio_decode.cpp`
- **Committed in:** `1cbc189` (part of Task 1's commit)

**2. [Rule 2 - missing test coverage] shared `tests/support/aac_handwritten_identity.{h,cpp}` created**
- **Found during:** Task 3
- **Issue:** Task 3's two-build proof needs to call 06-02's D-11 input-identity assertion, but that helper had `extern` (non-namespace) linkage local to `tests/unit/test_gen_he_aac.cpp` — `tests/unit/` and `tests/integration/` are separate binaries, so it would not link into `mediadiff_integration_tests`.
- **Fix:** Promoted the constant and assertion function into a new shared header/source pair under `tests/support/`, linked into both test targets (mirrors the existing `golden.{h,cpp}`/`mutate.{h,cpp}` precedent); updated `test_gen_he_aac.cpp` to call the promoted version instead of defining it locally.
- **Files modified:** `tests/support/aac_handwritten_identity.h` (new), `tests/support/aac_handwritten_identity.cpp` (new), `tests/unit/test_gen_he_aac.cpp`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt`
- **Committed in:** `6fa996f` (part of Task 3's commit)

**3. [Rule 1 - test now correctly asserts new behavior] `test_audio_sample_hash.cpp`'s WAV-vs-FLAC test updated**
- **Found during:** Task 3 (running the full `ctest` suite after Task 1's determinism-class table landed)
- **Issue:** An existing 06-01 test asserted `skipped:hash_incomparable` for a WAV(class1)-vs-FLAC test pair. This plan's own D-06 table promotes `flac` to class 1, so the pair now shares a precondition and the comparator correctly renders `pass` instead of skipping — the old assertion was testing behavior this plan deliberately changed.
- **Fix:** Updated the test's title, comments and assertions to expect `pass` with matching digests, and documented that this realizes D-02's own original claim ("a lossless WAV to FLAC transcode compares equal") rather than merely proving equal-but-incomparable digests.
- **Files modified:** `tests/integration/test_audio_sample_hash.cpp`
- **Verification:** full `ctest` run went from 1 failure to 0 failures (1100/1100 passing)
- **Committed in:** `6fa996f` (part of Task 3's commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 2, 1 Rule 1)
**Impact on plan:** All three are necessary consequences of implementing the plan's own literal design (the class table's own promotion, and the coverage those tests require) — no scope creep, no architectural changes.

## Known Stubs

- **Task 1 behavior Test 6** (`--hash-decoder aac_fixed` on a USAC stream falls back to the default decoder and records class 2 with `fallback_reason`) is NOT exercised end-to-end. A hand-built `AudioSpecificConfig` declaring `object_type` 42 (USAC), constructed the same way `tools/gen_he_aac.py` hand-builds HE-AAC's simpler SBR signaling, was rejected by `avcodec_open2()` with `EINVAL` (-22) for BOTH `aac_fixed` and native `aac` in this environment's linked FFmpeg — contradicting `06-RESEARCH.md` Q3's claim that open succeeds unconditionally for `aac_fixed` on USAC content. A sanity-check plain `AOT_AAC_LC` ASC built identically opened successfully (`rc=0`) against `aac`, confirming the test harness itself is sound; the gap is that a genuine USAC bitstream requires full `UsacConfig()` syntax beyond what a hand-crafted `audioSpecificConfig()`-shaped buffer can stand in for, and this LGPL decode-only pin has no USAC encoder to synthesize one properly. The steering code path itself (`src/probe/audio_decode.cpp`'s `is_usac` check, set before either open attempt) exists and is reviewed by inspection, but is not proven by a passing test.
  - **File:** `src/probe/audio_decode.cpp` (steering logic present, not proven by an end-to-end test); `tests/integration/test_audio_hash_decoder.cpp` (Test 6 intentionally omitted, documented in that file's own top comment)
  - **Reason:** no USAC encoder or valid hand-buildable USAC bitstream is available in this project's environment as of this plan
  - **Resolution path:** a future plan with access to a real USAC sample (or a USAC-capable encoder addition) should add the missing end-to-end proof

## Issues Encountered

None beyond the documented deviations above.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `--hash-decoder`, the determinism-class table, `decode_path` and the three-way proof are all in place and passing; 06-06 onward (loudness/silence/dropout checks) can build on the SAME `AudioDecodeState`/`StreamAudioDecode` shape without re-deriving decoder selection.
- **Flagged for 06-13:** the `mp3`/`mp2` class-1 promotion is provisional — proven bit-exact on x86_64 only. `docs/checks/content.audio.sample_hash.md` documents this; 06-13's arm64 CI leg is where it gets confirmed or forces a documented demotion back to class 2 (06-RESEARCH.md A1).
- The USAC fallback gap (Known Stubs above) does not block downstream work — the steering code exists and is reviewed by inspection — but should be closed opportunistically if a real USAC sample becomes available.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

All claimed created/modified files verified present on disk; all three task commit hashes (`1cbc189`, `cb318a3`, `6fa996f`) verified present in `git log`.
