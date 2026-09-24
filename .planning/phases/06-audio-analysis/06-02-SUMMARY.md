---
phase: 06-audio-analysis
plan: 02
subsystem: testing
tags: [ffmpeg, aac, sbr, ebur128, flac, mp4-elst, mkv-codec-delay, corpus-fixtures, xxhash]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-01's content.audio.sample_hash tracer (decode sweep, XXH3-128 chaining) and the corpus-digest/provisional-ledger conventions this plan extends"
provides:
  - "tools/gen_he_aac.py: a stdlib-only, hand-written HE-AAC explicit/implicit signaling pair and a non-silent class-1 two-build proof input (audio_aac_handwritten.mp4), byte-identical on every CI leg by construction"
  - "11 lossless (FLAC/PCM) loudness/true-peak/silence fixtures plus tests/golden/AUDIO_EBUR128_REFERENCE.txt, a committed ffmpeg -af ebur128 reference for 06-08's ±0.1 LU assertion"
  - "19 stream-parameter, layout and priming fixtures for AUDIO-01/02/03/04, including the mp4->mkv->mp4 priming round trip and the two container-mechanism edge cases (genuine multi-entry MP4 edit list, hand-patched fragmented MP4 with an edit list) 06-RESEARCH.md Q7 names"
affects: [06-03, 06-04, 06-05, 06-06, 06-08, 06-09, 06-13]

# Actuals (#2632) — pairs with the plan's `estimate` to calibrate future estimates.
actuals:
  tokens: 23918
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Hand-written bitstream fixtures for encoder-absent or SIMD-unstable codecs (tools/gen_he_aac.py, mirroring tools/gen_video_fixtures.py's BitWriter/selftest/write_atomic precedent) instead of depending on an encoder that doesn't exist in the pin or isn't byte-stable"
    - "NOISE_BT (AAC codebook 13) scalefactor bands as a zero-spectral-bits, PRNG-synthesized non-silent payload for a hand-written AAC raw_data_block"
    - "python3 heredoc box/EBML surgery on an already-generated carrier (mkv_tscale precedent) extended to ISOBMFF fragmented-MP4 edit-list injection, including the tfhd absolute base_data_offset fixup a naive insert-only patch silently breaks"
    - "A sub-frame-boundary -itsoffset value (not aligned to the codec's own frame duration) reliably makes ffmpeg's own MP4 muxer emit a genuine multi-entry elst, avoiding a hand-patch entirely for the multiedit edge case"
key-files:
  created:
    - tools/gen_he_aac.py
    - tests/unit/test_gen_he_aac.cpp
    - tests/golden/AUDIO_EBUR128_REFERENCE.txt
  modified:
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/README.md
    - tests/integration/test_video_inspect_section.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - "NOISE_BT scalefactor magnitude must clear int16 quantization noise (empirically tuned to 330, not the initially-tried 266) to produce audibly non-zero PCM, not just a technically-nonzero one"
  - "Padded the implicit ASC to the explicit ASC's byte length so the two HE-AAC fixtures differ in exactly one contiguous byte span, making the byte-identity proof exact rather than a fuzzy tolerance"
  - "True-peak fixtures needed ~19-20.5dB of gain, not ~1-2dB, because ffmpeg's sine= lavfi source peaks at -21dBTP with no gain applied, not 0dBFS (confirmed empirically, not assumed)"
  - "Every loudness/silence/priming fixture is FLAC, PCM, or (for the float-format sibling) native FFmpeg vorbis -- never aac/ac3/eac3 -- since only these proved byte-stable across SIMD dispatch levels"
  - "audio_flt_base is .ogg/vorbis, not the plan's literal .flac: FLAC's decoder can never emit a float sample format (confirmed against flacdec.c), so a .flac file cannot be the float-format sibling the fixture needs"
  - "The two priming container-mechanism edge cases (multiedit, fragmented+editlist) are both producible/patchable without inventing new machinery: the multiedit case is a genuine ffmpeg CLI side effect of a sub-frame -itsoffset, and the fragmented case reuses the mkv_tscale python3-heredoc-patch precedent extended to ISOBMFF boxes"
  - "New fixture hash lines were added to CORPUS_DIGEST.txt (never rewriting an existing line) for all three tasks, following tests/golden/README.md's own committed provenance policy and satisfying lint_corpus_digest_provenance.sh's clause 3 -- overriding Task 3's own acceptance-criteria prose, which said the opposite and would have failed its own required verify gate"

patterns-established:
  - "Deviation note pattern for plan/reality naming mismatches: when a plan names a fixture using a container/codec that structurally cannot deliver the stated intent (e.g. FLAC for a float sample format), fix the vehicle and document the mismatch with a code-level citation (flacdec.c/vorbisdec.c), not a silent rename"

requirements-completed: [AUDIO-02, AUDIO-03, AUDIO-04, AUDIO-05, AUDIO-06, AUDIO-07]

coverage:
  - id: D1
    description: "tools/gen_he_aac.py hand-writes an HE-AAC explicit/implicit signaling pair (byte-identical on every leg) plus a non-silent class-1 proof input, with a --selftest that round-trip-verifies against the linked decoders and refuses malformed input"
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "python3 tools/gen_he_aac.py --selftest"
        status: pass
      - kind: unit
        ref: "tests/unit/test_gen_he_aac.cpp -- unit.gen_he_aac input-identity tests"
        status: pass
    human_judgment: false
  - id: D2
    description: "11 lossless loudness/true-peak/silence fixtures with a committed ffmpeg -af ebur128 text reference (tests/golden/AUDIO_EBUR128_REFERENCE.txt), documented in tests/golden/README.md"
    requirement: "AUDIO-05, AUDIO-06, AUDIO-07"
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_bash4_builtins.sh && bash scripts/lint_corpus_digest_provenance.sh && test -s tests/golden/AUDIO_EBUR128_REFERENCE.txt"
        status: pass
    human_judgment: false
  - id: D3
    description: "19 stream-parameter, layout and priming fixtures including the multi-edit and fragmented MP4 container-mechanism edge cases (AUDIO-01/02/04)"
    requirement: "AUDIO-02, AUDIO-04"
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_bash4_builtins.sh && bash scripts/lint_corpus_digest_provenance.sh && bash scripts/lint_fixture_case_collisions.sh"
        status: pass
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (1033/1033 passed)"
        status: pass
    human_judgment: false

duration: ~2h (across a compacted session; wall-clock start not recorded)
completed: 2026-09-20
status: complete
---

# Phase 06 Plan 02: HE-AAC Signaling, Loudness/Silence, and Stream-Parameter/Priming Fixtures Summary

**Hand-written HE-AAC explicit/implicit signaling pair, 11 lossless loudness/true-peak/silence fixtures with a committed ffmpeg ebur128 reference, and 19 stream-parameter/layout/priming fixtures including a genuine multi-entry MP4 edit list and a hand-patched fragmented+edit-list MP4**

## Performance

- **Duration:** ~2h (session was compacted mid-execution; exact wall-clock start not captured)
- **Tasks:** 3
- **Files modified:** 11 (2 new source files, 1 new golden reference, 1 generator script, 5 golden/test-metadata files, 1 test-support wiring, plus the auto-regenerated GENERATOR_MANIFEST.json)

## Accomplishments
- `tools/gen_he_aac.py`: a stdlib-only Python 3.11 hand-written AAC/HE-AAC bitstream and minimal MP4 muxer, producing a byte-identical-on-every-leg explicit/implicit HE-AAC signaling pair and a non-silent class-1 two-build proof input, with a `--selftest` that decodes its own output through the linked decoders before ever writing a fixture to the corpus.
- 11 new FLAC/PCM fixtures for loudness, true-peak, and silence, each measured with the pinned generator's own `ffmpeg -af ebur128` and committed to `tests/golden/AUDIO_EBUR128_REFERENCE.txt` so 06-08's tolerance assertion runs on all five CI legs without needing the pinned ffmpeg present at test time.
- 19 new fixtures covering sample format/bit-depth/channel-count/codec/layout parameters and the full MP4->MKV->MP4 AAC priming round trip, including the two container-mechanism edge cases (a genuine multi-entry MP4 edit list and a hand-patched fragmented MP4 carrying an edit list) 06-RESEARCH.md Q7 flagged as needing dedicated fixtures.
- All new fixtures verified deterministic (byte-identical across repeat `scripts/gen_corpus.sh` runs) and the full 1033-test ctest suite passes with them in place.

## Task Commits

Each task was committed atomically:

1. **Task 1: `tools/gen_he_aac.py` -- hand-written HE-AAC signaling pair and class-1 proof input** - `2678e63` (feat)
2. **Task 2: lossless loudness, true-peak and silence fixtures with the committed `ffmpeg -af ebur128` text reference** - `5eba26a` (feat)
3. **Task 3: stream-parameter, layout and priming fixtures, including the two container-mechanism edge cases** - `9e907b1` (feat)

_No plan-metadata-only commit was needed beyond the final commit this executor makes below._

## Files Created/Modified
- `tools/gen_he_aac.py` - hand-written HE-AAC/AAC bitstream + minimal MP4 muxer + `--selftest`
- `tests/unit/test_gen_he_aac.cpp` - records `audio_aac_handwritten.mp4`'s recorded XXH3-128 input identity for 06-05's two-build proof
- `tests/unit/CMakeLists.txt` - wires the new test file into the unit-test binary
- `scripts/gen_corpus.sh` - all three tasks' fixture recipes (HE-AAC generator invocation, 11 loudness/silence recipes + ebur128 capture, 19 parameter/priming recipes including the fragmented-MP4 python3 box-surgery patch)
- `tests/golden/AUDIO_EBUR128_REFERENCE.txt` - committed `ffmpeg -af ebur128` reference for the 11 loudness/silence fixtures
- `tests/golden/README.md` - documents `AUDIO_EBUR128_REFERENCE.txt`'s provenance and regeneration rule
- `tests/golden/CORPUS_DIGEST.txt` - real SHA-256 lines added for all 30 new fixtures (5+11+19-5 dup-hash lines counted once), never rewriting an existing line
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - all 30 new fixture names recorded, pending designated-leg transcription (38 total entries)
- `tests/integration/test_video_inspect_section.cpp` - `kNoVideoStreamFixtures` extended to 19 entries (5 from Task 1, 6 from Task 3) so the pre-existing SC2-coverage test correctly classifies the new audio-only `.mp4`/`.mkv`/`.ts` fixtures

## Decisions Made
- NOISE_BT (AAC codebook 13) scalefactor bands synthesize non-zero PCM with zero spectral Huffman bits — the mechanism that makes a hand-written, non-silent, class-1 AAC fixture possible without implementing spectral coding.
- Padded the implicit-signaling ASC to the explicit ASC's byte length so the two HE-AAC fixtures differ in exactly one contiguous byte span (exact proof, not a tolerance-bounded one).
- `sine=` lavfi source peaks at ~-21dBTP with no gain, not 0dBFS — every true-peak/loudness gain value in the recipes was set from this measured baseline, not an assumed full-scale one.
- `audio_flt_base` is `.ogg`/native-vorbis, not the plan's literal `.flac` (see Deviations) — FLAC structurally cannot decode to a float sample format.
- The MP4 multi-entry edit list (`audio_prime_multiedit.mp4`) needed no byte-level patch at all: a sub-AAC-frame `-itsoffset` value makes ffmpeg's own muxer emit a genuine two-entry `elst`.
- The fragmented+edit-list MP4 (`audio_prime_fragmented.mp4`) does need a byte-level patch (ffmpeg never writes an `elst` for `frag_keyframe+empty_moov`), extending the project's existing `mkv_tscale` python3-heredoc-patch precedent to ISOBMFF box surgery.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `audio_flt_base` renamed from `.flac` to `.ogg` (native vorbis)**
- **Found during:** Task 3
- **Issue:** The plan's action text names this fixture `audio_flt_base.flac`, intended as `audio.sample_fmt`'s "float-format sibling." FFmpeg's own FLAC decoder (`flacdec.c`) only ever sets `AV_SAMPLE_FMT_S16(P)`/`S32(P)` — a `.flac` file can never decode to a float sample format, so the fixture as literally specified cannot deliver its own stated purpose.
- **Fix:** Built the fixture as `audio_flt_base.ogg` using FFmpeg's own native `vorbis` encoder/decoder (not `libvorbis`, no external library, LGPL-safe), whose decoder (`vorbisdec.c`) unconditionally sets `AV_SAMPLE_FMT_FLTP` — confirmed empirically via the fixture's own `content.audio.sample_hash` evidence (`normalization":"untrimmed;fmt=flt;...`).
- **Files modified:** `scripts/gen_corpus.sh`
- **Verification:** `mediadiff inspect --content` on the fixture reports `decoder_name":"vorbis"` and `fmt=flt`.
- **Committed in:** `9e907b1` (Task 3 commit)

**2. [Rule 3 - Blocking] `mediadiff inspect --content` needed for the 5.1(side) channel-count verify step**
- **Found during:** Task 3
- **Issue:** The plan's own `<verify>` block runs `mediadiff inspect tests/fixtures/audio_51_side.flac | head -30` and requires the output to "name a 6-channel audio stream," but plain (no-`--content`) `inspect` output carries no channel-count information at all as of this plan (the `audio.channels` analyzer doesn't exist until 06-03) — the literal command as written fails its own stated pass condition.
- **Fix:** Ran the equivalent check with `--content` (which decodes and reports `ch=6` in `content.audio.sample_hash`'s evidence) instead of the plain, no-decode invocation. The fixture itself (built via `pan`+`channelmap`) is unaffected — only the verification command needed correcting.
- **Files modified:** none (verification-only correction)
- **Verification:** `mediadiff inspect tests/fixtures/audio_51_side.flac --content | grep ch=6` matches; ffprobe-equivalent inspection (the acceptance criterion's own literal wording) independently confirms both `audio_51.flac`/`audio_51_side.flac` report 6 channels, `5.1` vs `5.1(side)`.
- **Committed in:** N/A (no code change; documented here for traceability of why the plan's literal verify text was not run as written)

**3. [Rule 1 - Bug] Matroska `SegmentUID` randomization made `audio_prime_roundtrip.mkv` non-reproducible**
- **Found during:** Task 3
- **Issue:** `audio_prime_roundtrip.mkv`/`audio_prime_roundtrip2.mp4`/`audio_prime_copy.ts`'s `-c copy` remux commands were missing `-flags +bitexact -fflags +bitexact`. FFmpeg's matroska muxer seeds a PRNG from `av_get_random_seed()` for its `SegmentUID` unless `AVFMT_FLAG_BITEXACT` is set (confirmed directly against `matroskaenc.c`) — this is a container-level randomization independent of codec bitexactness, so `-c copy` alone did not suppress it. Regenerating the corpus twice produced two different `audio_prime_roundtrip.mkv` byte streams, violating the plan's own top-level verification requirement ("repeat generation leaves every new fixture... byte-identical").
- **Fix:** Added `-flags +bitexact -fflags +bitexact` to all three `-c copy` remux commands in this chain, matching the existing `audio_hash_base.mkv`/`.ts` and `audio_pcm_base.mov` precedents elsewhere in `scripts/gen_corpus.sh`.
- **Files modified:** `scripts/gen_corpus.sh`
- **Verification:** Regenerated the corpus 3 times in a row; `audio_prime_roundtrip.mkv` and the other 12 new Task 3 fixtures are byte-identical across all runs. The underlying priming values (`codec_delay_ns=23219955`, elst `media_time=1024`/`1014`) are unchanged by the fix.
- **Committed in:** `9e907b1` (Task 3 commit)

**4. [Rule 1 - Bug] `test_video_inspect_section.cpp`'s `kNoVideoStreamFixtures` needed extending twice**
- **Found during:** Task 1 and Task 3
- **Issue:** The pre-existing "every fixture with a video stream renders all nine SC2 checks" integration test enumerates every `.mp4`/`.mkv`/`.ts`/`.webm`/`.h264`/`.hevc` fixture under `tests/fixtures/` and asserts video-check presence unless the fixture's name is in a committed, sorted, no-video allow-list. The five Task 1 fixtures (`audio_sbr_*`, `audio_aac_handwritten*`) and the six Task 3 fixtures with a matching extension (`audio_prime_base.mp4`, `audio_prime_copy.ts`, `audio_prime_fragmented.mp4`, `audio_prime_multiedit.mp4`, `audio_prime_roundtrip.mkv`, `audio_prime_roundtrip2.mp4`) are all audio-only and were missing from that list, failing the test on each.
- **Fix:** Added all 11 names to `kNoVideoStreamFixtures` (grown from 8 to 13 entries in Task 1, then to 19 in Task 3), keeping the array's required sort order — an intermediate attempt placed the Task 3 names in the wrong sort position (`audio_prime_*` after `audio_sbr_*`, which is alphabetically wrong since `p` < `s`), caught immediately by the test's own `is_sorted_no_video_list()` self-check and corrected before committing.
- **Files modified:** `tests/integration/test_video_inspect_section.cpp`
- **Verification:** Full ctest suite: 1033/1033 passed after each fix.
- **Committed in:** `2678e63` (Task 1), `9e907b1` (Task 3)

**5. [Rule 1 - Bug] Task 3's acceptance-criteria prose contradicts its own required verify gate on `CORPUS_DIGEST.txt`**
- **Found during:** Task 3
- **Issue:** Task 3's action text and one acceptance-criteria bullet say new fixture names go only into `CORPUS_DIGEST_PROVISIONAL.txt`, and explicitly that "none appears as a NEW line in `tests/golden/CORPUS_DIGEST.txt`." But `scripts/lint_corpus_digest_provenance.sh` (itself in Task 3's own `<verify>` block, and the project's own committed provenance mechanism per `tests/golden/README.md`) requires clause 3 to fail unless every provisional-ledger entry corresponds to a real, existing digest line in `CORPUS_DIGEST.txt` — confirmed by running it, which failed with 13 "does not name any fixture line" errors.
- **Fix:** Followed the same pattern already used (and passing) for Task 1 and Task 2 in this same plan: added real SHA-256 hash lines for all 19 new Task 3 fixtures to `CORPUS_DIGEST.txt` (in their correct sorted position, never rewriting an existing line), alongside their names in `CORPUS_DIGEST_PROVISIONAL.txt`.
- **Files modified:** `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`
- **Verification:** `bash scripts/lint_corpus_digest_provenance.sh` passes all 4 clauses (38 provisional entries, all corresponding to real digest lines; 80 pre-existing lines from the pinned historical commit unchanged).
- **Committed in:** `9e907b1` (Task 3 commit)

---

**Total deviations:** 5 auto-fixed (4 Rule 1 bug fixes, 1 Rule 3 blocking-verify correction)
**Impact on plan:** All fixes were necessary for the fixtures to deliver their stated purpose, for the corpus/digest provenance mechanism to stay internally consistent, or for the pre-existing test suite to keep passing. No scope creep — no new fixtures or checks were added beyond what the plan specified.

## Issues Encountered
None beyond the deviations documented above — all were caught and resolved via the plan's own `<verify>` blocks (or the project's pre-existing lint/test suite) before committing.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All fixtures 06-03 (`audio.codec`/`sample_rate`/`sample_fmt`/`bit_depth`/`channels`/`layout`), 06-04 (`audio.profile`), 06-05 (class-1 two-build proof), 06-06 (`audio.priming`), 06-08 (`audio.loudness.*`), and 06-09 (`audio.silence.*`) need now exist in the corpus, byte-identical and lint-clean.
- `audio_prime_roundtrip2.mp4`'s observed `media_time=1014` (vs. the base's `1024`) is a real, empirically-confirmed ~10-sample rounding artifact of the MKV `CodecDelay` intermediate's own ns-granularity — 06-06 should treat this as data to measure a tolerance against, not assume away.
- No blockers for the next wave of plans.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

All 9 claimed files confirmed present on disk; all 3 task commit hashes (`2678e63`, `5eba26a`, `9e907b1`) confirmed present in git log.
