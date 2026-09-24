---
phase: 06-audio-analysis
plan: 01
subsystem: audio-analysis
tags: [ffmpeg, libavcodec, xxh3, hash-chain, decode-determinism, cli11, probe-layer]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: PacketScan's fused-sweep precedent (parser_scan), run_probe/ProbeResults contract, HashChain's original shape
provides:
  - "06-CHECK-ROSTER.md: the approved 14-id Phase-6 audio/meta check roster, the D-05 signature composition, and the D-09 error-check spelling decision"
  - "Pass::audio_decode and the audio decode sweep (src/probe/audio_decode.{h,cpp}, audio_config.{h,cpp}) fused into run_packet_scan's existing av_read_frame loop"
  - "content.audio.sample_hash: the phase's registered, documented, DOC-03-paired tracer check"
  - "HashChain::block_digests/element_stride extension, round-tripping through the snapshot contract -- the shape Phase 7's video hashing inherits"
  - "compose_decode_path_signature()'s D-05 triplet+cpuflags extension and the decode_path_class evidence convention"
  - "src/compare/hash.cpp's D-03 divergence-locator report and the compare/engine.cpp evidence-merge fix it depends on"
  - "The shared resolve_content_enabled resolver and ProbeOptions plumbing into compare/snapshot/dir/inspect"
affects: [06-03-audio-identity, 06-04-audio-profile, 06-05-audio-decoder-extensions, 06-06-audio-priming, 06-08-audio-loudness, 06-09-audio-silence, 06-10-meta-decode-errors, 07-video-hashing]

# Actuals (#2632)
actuals:
  tokens: 41200
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Decode-sink fusion: a new decode pass is fused INSIDE run_packet_scan's existing av_read_frame loop (mirrors parser_scan's own precedent) rather than a second sweep -- packet bytes are never retained past one loop iteration, so a genuinely separate pass could not re-read them."
    - "Decoder-class evidence gating: decode_path_class carries a machine-independent 'class1' string for bit-exact codecs (PCM, fixed-point siblings) and a full D-05 version+triplet+cpuflags signature for 'class2 <sig>' otherwise -- src/compare/hash.cpp's existing precondition system skips class1-vs-class2 comparisons as hash_incomparable even when the underlying digest happens to match, never silently trusting a cross-path comparison."
    - "Three-way CLI flag resolver: resolve_content_enabled(args, command_default) with a must_decode command class that turns --no-content itself into a usage error, alongside the ordinary decode_by_default/opt_in classes -- mirrors resolve_probe_timeout_ms's own explicit-flag > default precedence shape."

key-files:
  created:
    - .planning/phases/06-audio-analysis/06-CHECK-ROSTER.md
    - src/probe/audio_decode.h
    - src/probe/audio_decode.cpp
    - src/probe/audio_config.h
    - src/probe/audio_config.cpp
    - src/analyzers/content/analyzers.h
    - src/analyzers/content/sample_hash.cpp
    - docs/checks/content.audio.sample_hash.md
    - tests/integration/test_audio_sample_hash.cpp
    - tests/unit/test_audio_decode.cpp
  modified:
    - src/probe/pass.h
    - src/probe/orchestrator.h
    - src/probe/orchestrator.cpp
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/core/value.h
    - src/core/serializer.cpp
    - src/core/checks.def
    - src/util/version.h
    - src/util/version.cpp
    - src/compare/hash.cpp
    - src/compare/engine.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/commands/compare.cpp
    - src/cli/commands/compare.h
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/dir.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/main.cpp
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/integration/test_doc03_coverage.cpp

key-decisions:
  - "Approved the 14-id Phase-6 roster, the D-05 signature format, and the D-09 exit-code amendment via the Task 1 checkpoint before any check id was written to checks.def."
  - "Recorded the decoder-class-3 interpretation in 06-CHECK-ROSTER.md: 'class 3, hash disabled' means no decoder was available in this build AT ALL, never 'a successfully-decoded stream's hash is discarded' -- PCM and every other successfully-opened codec (incl. flac) still produce a real, comparable same-machine hash chain."
  - "The audio decode sweep is fused inside run_packet_scan's existing loop (packet_scan.h/.cpp), not a separate pass -- a structural requirement of AUDIO-10/PROBE-08 discovered while implementing Task 2, since PacketRecord never retains packet byte data past one loop iteration."
  - "src/compare/engine.cpp's evidence assignment changed from an unconditional overwrite to a merge-if-object -- a comparator (src/compare/hash.cpp's new D-03 divergence report) can now populate finding.evidence itself before returning, without the engine silently discarding it."

requirements-completed: [AUDIO-08, AUDIO-10, TRUST-01]

coverage:
  - id: D1
    description: "content.audio.sample_hash hashes the decoder's untrimmed output; an MP4, its MKV stream copy and its MPEG-TS stream copy of the same AAC payload produce an equal chain digest (D-01)"
    requirement: AUDIO-08
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - an MP4, its MKV stream copy and its MPEG-TS stream copy of the same AAC payload all produce the SAME HashChain digest and element_count"
        status: pass
    human_judgment: false
  - id: D2
    description: "The fixed-block chain steps over rate-derived sample-count blocks, not decoder frames; one PCM payload hashes equal across WAV/MOV/two FLAC block sizes (D-02)"
    requirement: AUDIO-08
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - WAV stream-copied to MOV (both decoder_class 1) reports pass on the audio hash"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - one PCM payload written as WAV and independently encoded to FLAC at two block sizes all produce the SAME underlying HashChain digest (D-02), even though the comparator itself reports skipped:hash_incomparable across decoder classes"
        status: pass
    human_judgment: false
  - id: D3
    description: "A digest mismatch reports the first divergent block as a sample range plus a time, the divergent block ranges, and a total divergent-block count -- identically whether the baseline is media or a snapshot (D-03)"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - comparing two different tones reports the first divergent block, sample range, time and divergent-block count"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - a snapshot baseline produces the IDENTICAL divergence evidence as a live media baseline"
        status: pass
    human_judgment: false
  - id: D4
    description: "HashChain carries a per-block digest array and round-trips through write_snapshot/read_snapshot byte-identically (D-04)"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - a HashChain's block_digests array round-trips through write_snapshot/read_snapshot byte-identically"
        status: pass
    human_judgment: false
  - id: D5
    description: "Loudness/silence/hashing share ONE decode sweep per file; enabling the decode pass leaves read_frame_call_count unchanged versus a packet-scan-only run (AUDIO-10, PROBE-08)"
    requirement: AUDIO-10
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - enabling decode_audio does not change read_frame_call_count"
        status: pass
    human_judgment: false
  - id: D6
    description: "compose_decode_path_signature() carries the libav version triples plus the vcpkg triplet plus av_get_cpu_flags(); a class-1 measurement's decode_path_class is path-independent while a class-2 measurement's carries the full signature (D-05, TRUST-01, TRUST-02)"
    requirement: TRUST-01
    verification:
      - kind: integration
        ref: "tests/integration/test_schema_version.cpp#schema_version - compose_decode_path_signature composes three distinct version triples, plus 06-01-PLAN.md's D-05 triplet and cpuflags fields"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_decode.cpp#audio_decode - AAC selects the aac_fixed sibling decoder"
        status: pass
    human_judgment: false
  - id: D7
    description: "--content/--no-content reach the probe layer through ProbeOptions on compare, snapshot, dir and inspect; snapshot's own must-decode rule rejects --no-content as a usage error"
    verification:
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - snapshot always decodes, and --no-content exits 64 naming the flag"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - dir does not decode by default, and --content enables it"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - inspect --content renders decode-derived facts, --no-content marks the same section not measured, never blank"
        status: pass
      - kind: integration
        ref: "tests/integration/test_audio_sample_hash.cpp#audio_sample_hash - --content and --no-content together exits 64 naming the conflict, on compare, dir and inspect"
        status: pass
    human_judgment: false

duration: 44min (from the Task 1 roster commit to the Task 3 commit; upstream implementation work for Task 2 spanned a prior, separately-tracked session)
completed: 2026-09-20
status: complete
---

# Phase 6 Plan 1: content.audio.sample_hash Tracer Summary

**New fused audio-decode probe pass producing an XXH3-128 fixed-block hash chain, proven byte-equal across MP4/MKV/TS remuxes of one AAC payload and across WAV/MOV/two-FLAC-blocksize packetizations of one PCM payload, wired behind `--content`/`--no-content` on all four commands.**

## Performance

- **Duration:** 44 min (Task 1 roster approval through the Task 3 commit; Task 2's underlying implementation work spanned a separately-tracked prior session)
- **Completed:** 2026-09-20
- **Tasks:** 3 (checkpoint:decision, tracer, auto)
- **Files modified:** 46 (10 created, 36 modified) across 3 commits

## Accomplishments

- Approved and committed the 14-id Phase-6 audio/meta check-id roster (`06-CHECK-ROSTER.md`), the D-05 decode-path-signature composition, and the D-09 error-check spelling — the frozen source of truth plans 06-03 through 06-10 register from.
- Implemented a new audio decode pass (`src/probe/audio_decode.{h,cpp}`, `audio_config.{h,cpp}`) fused directly inside `run_packet_scan`'s existing `av_read_frame` loop — proven, not merely asserted, to leave `read_frame_call_count` unchanged (AUDIO-10, PROBE-08).
- Registered `content.audio.sample_hash`: an XXH3-128 fixed-block hash chain over the decoder's untrimmed output (`AV_CODEC_FLAG_BITEXACT` + `AV_CODEC_FLAG2_SKIP_MANUAL`), empirically verified byte-equal across an MP4/MKV/TS trio of one AAC payload (D-01) and across a WAV/MOV/two-FLAC-block-size quartet of one PCM payload (D-02).
- Extended `HashChain` with `block_digests`/`element_stride`, round-tripping byte-identically through the snapshot contract — the shape Phase 7's video hashing is planned against (D-04).
- Added a D-03 per-block divergence-locator report to `src/compare/hash.cpp` (first divergent block, sample range, time, divergent ranges, total count), and fixed a pre-existing bug in `src/compare/engine.cpp` that silently discarded a comparator's own `finding.evidence`.
- Extended `compose_decode_path_signature()` with `triplet/` and `cpuflags/` fields (D-05), backing the `decode_path_class` evidence convention TRUST-01/TRUST-02 depend on.
- Wired `--content`/`--no-content` into `compare` (Task 2) and `snapshot`/`dir`/`inspect` (Task 3) through a new shared `resolve_content_enabled` resolver, including `snapshot`'s own must-decode usage-error rule and the removal of `dir.cpp`'s stale "no effect yet" diagnostic.
- Synthesized nine new bitexact fixtures and updated the `CORPUS_DIGEST`/`CORPUS_DIGEST_PROVISIONAL` ledger, the `content.audio.sample_hash` DOC-03 coverage pair (running total 77 → 78), and six pre-existing tests whose reports now correctly include the new check.

## Task Commits

Each task was committed atomically:

1. **Task 1: Approve the Phase-6 audio check-id roster** - `190b300` (docs)
2. **Task 2: `content.audio.sample_hash` end to end** - `7c4655e` (feat)
3. **Task 3: `--content`/`--no-content` across `snapshot`, `dir` and `inspect`** - `b4c08c7` (feat)

_Note: this plan's tasks carried `tdd="true"` but were not executed as a literal RED→GREEN→REFACTOR sequence — see "Deviations from Plan" below._

## Files Created/Modified

- `src/probe/audio_decode.{h,cpp}` — the decode-sink state machine (`AudioDecodeState`), decoder selection (fixed-point sibling table, USAC steering), block digesting, `run_audio_decode`
- `src/probe/audio_config.{h,cpp}` — mediadiff's own bounds-checked MPEG-4 AudioSpecificConfig bit reader (USAC detection, T-06-02's mitigation)
- `src/probe/packet_scan.{h,cpp}` — fused the decode sweep into the existing `av_read_frame` loop (`decode_audio` request flag, `audio_decode` output slot) — a necessary deviation beyond Task 2's stated file list, see below
- `src/probe/pass.h` — `Pass::audio_decode`, `ProbeResults::audio_decode`, `PassSet::clear`
- `src/probe/orchestrator.{h,cpp}` — `ProbeOptions{content_enabled, hash_decoder}`, the 3-arg `fingerprint_input` overload, `Pass::audio_decode` implication/removal logic
- `src/core/value.h`, `src/core/serializer.cpp` — `HashChain::block_digests`/`element_stride`
- `src/core/checks.def`, `docs/checks/content.audio.sample_hash.md` — the registered check and its `--explain` doc
- `src/analyzers/content/{analyzers.h,sample_hash.cpp}` — the analyzer itself
- `src/util/version.{h,cpp}` — `compose_decode_path_signature()`'s D-05 extension
- `src/compare/hash.cpp` — the D-03 divergence report
- `src/compare/engine.cpp` — the evidence-merge bug fix (a necessary deviation beyond Task 2's stated file list)
- `src/cli/options.{h,cpp}` — `ContentArgs`, `ContentCommandDefault`, `resolve_content_enabled`
- `src/cli/commands/{compare,snapshot,dir,inspect}.cpp`, `compare.h`, `main.cpp` — `--content`/`--no-content` wiring
- `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST*.txt` — nine new fixtures and their ledger entries
- `tests/unit/test_audio_decode.cpp`, `tests/integration/test_audio_sample_hash.cpp` — the plan's own 18 named behavior tests (12 from Task 2, 6 from Task 3)
- `tests/integration/test_doc03_coverage.cpp` — the new check's coverage pair, running total 77 → 78
- Six pre-existing tests updated for the new check's correct appearance in their reports: `test_list_checks.cpp`'s golden (`tests/golden/list_checks_effective.txt`), `test_schema_version.cpp`, `test_timeline_jitter.cpp`, `test_timeline_av_sync.cpp`, `test_timeline_start_duration.cpp`, `test_timeline_structure.cpp`, `test_video_inspect_section.cpp`

## Decisions Made

- **06-CHECK-ROSTER.md's decoder-class-3 interpretation:** "class 3, hash disabled" means no decoder was available in this build at all — never "a successfully-decoded stream's hash is discarded." PCM and every other successfully-opened codec (including flac) still produce a real, comparable same-machine hash chain; class only gates CROSS-MACHINE comparability via the `decode_path_class` evidence string.
- **The audio decode sweep is fused inside `run_packet_scan`'s existing loop**, not a separate pass — `packet_scan.h`/`.cpp` needed direct modification (beyond Task 2's stated `files_modified` list) because `PacketRecord` never retains packet byte data past one loop iteration; a genuinely separate decode pass could not re-read bytes already consumed without re-opening the file (explicitly prohibited).
- **`src/compare/engine.cpp`'s evidence assignment changed from unconditional overwrite to merge-if-object**, discovered via manual CLI verification when the D-03 divergence keys silently vanished from `compare --json` output. Verified via grep that no other comparator sets `finding.evidence` directly, so the fix is additive and backward-compatible.
- **Content.audio.sample_hash's own precondition behavior**: a class-1 (PCM) vs class-2 (FLAC) comparison of the SAME underlying audio always reports `skipped:hash_incomparable` even when the digest values are literally identical — this is the precondition system working as designed (TRUST-01/TRUST-02), not a defect; D-02's claim is about the digest VALUE the fixed-block chain produces, observable in the finding's own `baseline`/`candidate` fields regardless of the comparator's skip decision.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical Functionality] Fused audio decode into `run_packet_scan` instead of a separate pass**
- **Found during:** Task 2 (architecture design, before any code was written)
- **Issue:** The plan's own AUDIO-10/PROBE-08 "one shared sweep" requirement is structurally unsatisfiable by a separate decode pass, since `PacketRecord` in `packet_scan.h` never retains packet payload bytes — a second pass reading `av_read_frame` again would see nothing (EOF) or require re-opening the file (explicitly prohibited).
- **Fix:** Fused `detail::AudioDecodeState` directly into `run_packet_scan`'s existing loop, mirroring `parser_scan`'s own precedent exactly.
- **Files modified:** `src/probe/packet_scan.h`, `src/probe/packet_scan.cpp` (not in Task 2's stated `files_modified` list)
- **Verification:** `tests/unit/test_audio_decode.cpp`'s `read_frame_call_count` equality test
- **Committed in:** `7c4655e` (Task 2 commit)

**2. [Rule 1 - Bug] Fixed `compare/engine.cpp` silently discarding comparator-set evidence**
- **Found during:** Task 2, manual CLI verification of the D-03 divergence report
- **Issue:** `compare_fingerprints` unconditionally did `finding->evidence = std::move(evidence);` after calling the comparator, overwriting whatever the comparator itself had already set on `finding->evidence` — the new D-03 divergence keys were silently vanishing from `compare --json` output.
- **Fix:** Changed to merge-into-existing-object when `finding->evidence` is already an object, verified no other comparator sets `finding.evidence` directly (additive, backward-compatible).
- **Files modified:** `src/compare/engine.cpp` (not in Task 2's stated `files_modified` list)
- **Verification:** Re-ran the same CLI command; all 5 divergence keys now present alongside `baseline`/`candidate`.
- **Committed in:** `7c4655e` (Task 2 commit)

**3. [Rule 3 - Blocking] `compare.h`/`main.cpp`'s `content_enabled` parameter was left uncommitted after Task 2**
- **Found during:** Starting Task 3, `git status` review
- **Issue:** Task 2's own `--content` wiring on `compare` required updating `run_compare`'s declaration (`src/cli/commands/compare.h`) and its one call site (`src/cli/main.cpp`), but these two files were never included in Task 2's commit — an oversight, not a deliberate scope decision. The repository built and tested green throughout because the uncommitted diff was still present in the working tree, but Task 2's commit alone would not build if checked out in isolation.
- **Fix:** Included both files in Task 3's commit with an explicit note in the commit message.
- **Files modified:** `src/cli/commands/compare.h`, `src/cli/main.cpp`
- **Verification:** Full build + `ctest` green after the Task 3 commit.
- **Committed in:** `b4c08c7` (Task 3 commit)

**4. [Rule 1 - Bug] Six pre-existing tests updated for the new check's correct appearance**
- **Found during:** Full `ctest` run after Task 2
- **Issue:** `content.audio.sample_hash` is a real, correctly-computed new finding that legitimately fires non-pass on several pre-existing fixture pairs whose audio genuinely differs (timeline jitter, av-drift, duration-short, dts-backward splice, TS-jump splice) — these pairs' own DOC-04 "declares its complete expected finding set" tests needed the new id added to their declared sets. `test_schema_version.cpp` asserted `compose_decode_path_signature()` produced exactly 3 tokens (now 5, D-05's own sanctioned extension). `test_list_checks.cpp`'s golden and `test_video_inspect_section.cpp`'s `kNoVideoStreamFixtures` list both needed the new check/fixtures reflected.
- **Fix:** Added `content.audio.sample_hash` to each declared non-pass set with an evidence-verified comment explaining the real cause; regenerated the `list_checks_effective.txt` golden via `UPDATE_GOLDENS=1`; widened the signature-token assertion to 5; added the seven new video-less fixtures to `kNoVideoStreamFixtures`.
- **Files modified:** `tests/integration/test_schema_version.cpp`, `test_timeline_jitter.cpp`, `test_timeline_av_sync.cpp`, `test_timeline_start_duration.cpp`, `test_timeline_structure.cpp`, `test_video_inspect_section.cpp`, `tests/golden/list_checks_effective.txt`
- **Verification:** Full `ctest` suite green (1031/1031, 6 pre-existing unrelated skips) after the fix.
- **Committed in:** `7c4655e` (Task 2 commit)

---

**Total deviations:** 4 auto-fixed (1 missing critical functionality, 2 bugs, 1 blocking)
**Impact on plan:** All four were necessary for correctness or for the plan's own stated acceptance criteria to hold; none represent scope creep beyond what AUDIO-10/PROBE-08/D-03 already required.

## TDD Gate Compliance

Both Task 2 and Task 3 carry `tdd="true"` but were not executed as a literal RED→GREEN→REFACTOR commit sequence — no standalone `test(...)` commit precedes a `feat(...)` commit for either task. This reflects how the work was actually carried out: the underlying implementation (decode pass, hashing, CLI wiring) was built and empirically verified against real fixtures via manual `mediadiff` CLI invocations first, with the permanent Catch2 regression tests (`test_audio_decode.cpp`, `test_audio_sample_hash.cpp`) written afterward to capture those same, already-proven behaviors. Every one of the plan's 18 named `<behavior>` tests (12 from Task 2, 6 from Task 3) exists as a passing Catch2 assertion in the final commits — the coverage the TDD gate exists to guarantee is present — but the gate's own commit-ordering discipline was not followed literally.

## Issues Encountered

- **Doc-05-vs-fixture tension on decoder class 3:** doc 05's literal table would imply PCM/FLAC streams never get hashed at all, but the plan's own Test 1/Test 2 behaviors require exactly those codecs to hash and compare equal. Resolved by recording an explicit interpretive decision in `06-CHECK-ROSTER.md` rather than silently applying it (see Decisions Made above).
- **Class-1-vs-class-2 precondition mismatch on PCM-vs-FLAC:** the original Task 2 integration test asserted `status == pass` for a WAV-vs-FLAC comparison; the real behavior is `skipped:hash_incomparable` (a class1-vs-class2 precondition mismatch), even though the underlying digest values are identical. Corrected the test to assert digest-value equality directly from the finding's `baseline`/`candidate` fields instead of the comparator's own pass/fail decision, and added a same-class (WAV-vs-MOV) pair to prove the ordinary pass path too.
- **Cross-container `container.format` differences inflating `exit_code`:** several D-01/D-02 cross-container test assertions initially expected `exit_code == 0`, which fails whenever the two fixtures are genuinely different containers (a real, correct `container.format` fail unrelated to the audio hash). Corrected to assert the `content.audio.sample_hash` finding's own status rather than the whole report's exit code.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The 14-id roster, the D-05 signature format, and the `decode_path_class`/`sampling_state`/`normalization` precondition-key convention are frozen and ready for plans 06-03 through 06-10 to register against.
- `Pass::audio_decode`/`ProbeResults::audio_decode`/`ProbeOptions` are the one shared decode-sink seam every later audio analyzer (loudness, silence, priming) reads from — no later plan should open its own decoder.
- `HashChain::block_digests`/`element_stride` are committed to the snapshot contract; Phase 7's video hashing is planned against the same shape.
- No blockers. The full `ctest` suite (1031 tests) and all four plan-level lints (`lint_eng16.sh`, `lint_pragma_scope.sh`, `lint_bash4_builtins.sh`, `lint_corpus_digest_provenance.sh`) are green.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-20*

## Self-Check: PASSED

All 10 files listed under "Files Created/Modified" (created subset) and this SUMMARY.md itself were verified present on disk via `[ -f ... ]`. All 3 task commit hashes (`190b300`, `7c4655e`, `b4c08c7`) were verified present via `git log --oneline --all`. No missing items.
