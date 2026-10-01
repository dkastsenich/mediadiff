---
phase: 07-content-quality
plan: 04
subsystem: content
tags: [sampling, skip-reason, sampling-mismatch, compare-hash, cli, content.video.frame_hash, d-08]

requires:
  - phase: 07-content-quality
    plan: 01
    provides: "the video hash sink, HashChain::element_ticks, the sampling_state evidence key"
  - phase: 07-content-quality
    plan: 03
    provides: "the time-aligned frame locator, whose half-interval window depends on frame_interval"
  - phase: 07-content-quality
    plan: 01
    provides: "07-CHECK-ROSTER.md, the approved spellings of the four skip reasons"
provides:
  - "SkipReason::not_requested, sampling_conflict, path_incomparable, geometry_mismatch in every site hash_disabled occupies (enum, both conversions, junit switch, report schema enum, round-trip table)"
  - "--sample N on compare, snapshot, dir and inspect: every frame is still decoded, only frames whose decode index is a multiple of N are hashed and stored"
  - "sampling_state = sampled:N on content.video.frame_hash and envelope sampling.video_frame_stride = N (both absent for N = 1)"
  - "compare_hash's sampling_mismatch branch: skipped:sampling_mismatch for any unequal pairing with a sampled side, after the truncated rule and before the generic precondition rule"
  - "sampling_state_sampled(int) and parse_sampled_stride(string_view) in core/model.h; SampleArgs, add_sample_flag, resolve_sample_stride in cli/options"
affects: [07-08, 07-09, 07-10, 07-11, 07-14, 07-15]

actuals:
  tokens: 17500
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A stride that thins only what it owns: digests and ticks are stored every Nth frame, while error, corrupt-frame, geometry and timestamp bookkeeping still sees every decoded frame, so nothing outside the hash depends on --sample"
    - "A skipped frame is still checked for hashability (video_frame_hashable) so meta.decode_errors is identical with and without --sample"
    - "A stored sampling_state is parsed canonical-only (sampled: plus a positive int, no sign, no leading zero), so a hostile string can only fall through to the generic mismatch, never to a pass"

key-files:
  created:
    - tests/unit/test_sampling_mismatch.cpp
    - tests/integration/test_video_sampling.cpp
  modified:
    - src/core/model.h
    - src/report/junit.cpp
    - docs/schema/report-1.0.json
    - src/probe/orchestrator.h
    - src/probe/orchestrator.cpp
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/probe/video_decode.h
    - src/probe/video_decode.cpp
    - src/analyzers/content/video_frame_hash.cpp
    - src/compare/hash.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/commands/compare.h
    - src/cli/commands/compare.cpp
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/dir.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/main.cpp
    - tests/unit/test_measurement_provenance.cpp
    - tests/unit/test_video_decode.cpp
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - docs/checks/content.video.frame_hash.md
    - claude_docs/00-design-and-requirements.md
    - claude_docs/06-content-and-size-analysis.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "frame_count on StreamVideoDecode now means every decoded frame; the stored count is frame_digests.size(), and the chain's element_count and the zero-frame skip read the stored count, so at stride 1 nothing changes"
  - "frame_interval evidence is the stream interval times the stride (an int64 product), so the 07-03 locator's half-interval window matches stored-frame spacing with no change to the pairing rule"
  - "Timestamp usability (timestamps: pts/unusable) is decided over every decoded frame, not only stored ones, so a sampled chain reports the same timestamps evidence a full one would"
  - "A sampled locator report adds evidence sample_stride and a message note that frame numbers index stored frames (decode frame = N x number); absent for a full chain so no 07-03 report changes"
  - "describe_sampling renders a side as 'sampled:N' (stride N) or 'full' (stride 1), and quotes any unparseable stored state verbatim rather than guessing a stride"
  - "--sample is read as TEXT and validated by resolve_sample_stride, so 0, -2, abc, 2x and beyond-int values all reach a usage error naming --sample instead of a CLI11 parse message"

patterns-established:
  - "An additive enum member lands in every serialized site at once with a round-trip table that must be edited when the enum grows (19 enumerators now)"
  - "run_compare's resolved-option parameters are passed already-resolved (content_enabled, hash_decoder, sample_stride); main.cpp's implicit route passes the unflagged defaults"

requirements-completed: [CONTENT-03]

coverage:
  - id: D1
    description: "The four roster skip reasons exist in every site hash_disabled occupies and each round-trips through its string spelling; an unknown spelling is still rejected"
    verification:
      - kind: unit
        ref: "tests/unit/test_measurement_provenance.cpp#skip_reason: every SkipReason enumerator round-trips (19 rows), the schema-enum equality case and the inverse-parse cases"
        status: pass
      - kind: integration
        ref: "tests/integration/test_json_schema.cpp (whole file, run against the extended enum)"
        status: pass
    human_judgment: false
  - id: D2
    description: "D-08: --sample 4 on the 100-frame fixture decodes every frame but stores 25, each stored digest and tick equal to the full run's frame 4k; the measurement says sampled:4 and the envelope sampling object says video_frame_stride 4, through a real snapshot round trip"
    requirement: CONTENT-03
    verification:
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - stride stores every Nth"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_decode.cpp#video_decode - a stride decodes every frame, stores every Nth, and widens the frame interval"
        status: pass
    human_judgment: false
  - id: D3
    description: "CONTENT-03: a sampled snapshot against a full one reports skipped:sampling_mismatch (never hash_incomparable) in both directions and for unequal strides, and two --sample 2 snapshots of one file pass"
    requirement: CONTENT-03
    verification:
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - snapshot pair"
        status: pass
      - kind: unit
        ref: "tests/unit/test_sampling_mismatch.cpp#sampling_mismatch - sampled vs full, unequal strides, equal strides"
        status: pass
    human_judgment: false
  - id: D4
    description: "Truncation keeps precedence: a truncated side is hash_incomparable even against a sampled:N side; a malformed stored sampling_state never becomes a pass"
    requirement: CONTENT-03
    verification:
      - kind: unit
        ref: "tests/unit/test_sampling_mismatch.cpp#sampling_mismatch - truncated wins"
        status: pass
      - kind: unit
        ref: "tests/unit/test_sampling_mismatch.cpp#sampling_mismatch - a malformed stored state never becomes a pass"
        status: pass
    human_judgment: false
  - id: D5
    description: "--sample 1 is full: the snapshot is byte-identical to one taken without the flag, with an empty envelope sampling object"
    verification:
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - stride 1 is full"
        status: pass
    human_judgment: false
  - id: D6
    description: "D-08: sampling never changes a value it does not own - every non-hash measurement, the decode_path ledger, meta.decode_errors (nonzero on the corrupt fixture) and the frame_hash check's own error/corrupt/geometry evidence are identical with and without --sample 3; content.audio.sample_hash is identical on an audio+video file"
    verification:
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - other checks unchanged"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - audio is not sampled"
        status: pass
    human_judgment: false
  - id: D7
    description: "A sampled chain pairs correctly in the locator: --sample 2 of huffyuv vs its frame-40-damaged copy reports stored frame 20 at exactly 8/5 s, paired by time, with sample_stride 2; a damaged frame the stride skips is honestly not seen"
    verification:
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - locator under sampling"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - a damaged frame the stride skips is honestly not seen"
        status: pass
    human_judgment: false
  - id: D8
    description: "--sample 0, a negative, non-integer or oversize value, and --sample 2 with content decoding off exit 64 naming the flag, on compare, snapshot, dir and inspect; --help states the stride is not a faster decode"
    verification:
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - usage errors"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - --help explains what the stride does and does not do"
        status: pass
    human_judgment: false
  - id: D9
    description: "The frame-record budget charges only stored frames, and a stride past the frame count stores only frame 0"
    verification:
      - kind: unit
        ref: "tests/unit/test_video_decode.cpp#video_decode - the frame-record budget charges only stored frames"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_sampling.cpp#video_sampling - a stride past the frame count stores only frame 0"
        status: pass
    human_judgment: false
  - id: D10
    description: "Whole suite and the CI lint bundle are green; no golden, digest line or fixture changed"
    verification:
      - kind: other
        ref: "ctest --preset x64-linux (1338/1338, plain and under MEDIADIFF_DESIGNATED_LEG=1) + the ten ci.yml lint scripts + scripts/test_gen_corpus_pin_gate.sh"
        status: pass
    human_judgment: false
  - id: D11
    description: "The wording of the sampling_mismatch message and the --sample help text reads well to a CI log reader"
    verification: []
    human_judgment: true
    rationale: "The tests pin substrings (both strides named, --sample, not decoding faster); whether the phrasing is the right one is a judgment call."

duration: 18min
completed: 2026-09-30
status: complete
---

# Phase 7 Plan 04: --sample N and the Phase 7 Skip Vocabulary Summary

**`--sample N` as an honest stride (every frame decoded, every Nth hashed and stored, recorded as `sampled:N` in the measurement and the envelope) with a `skipped:sampling_mismatch` branch in `compare_hash`, plus the four roster skip reasons landed in every site at once**

## Performance

- **Duration:** 18 min
- **Started:** 2026-09-30T21:45:03Z
- **Completed:** 2026-09-30T22:03Z
- **Tasks:** 3
- **Files modified:** 28 (2 created, 26 modified; no fixture, golden or digest line touched)

## Accomplishments

- **The skip vocabulary is complete for the phase.** `not_requested`, `sampling_conflict`, `path_incomparable` and `geometry_mismatch` are appended after `hash_disabled` in the enum, both conversion functions, the junit switch, the report schema's closed enum and the round-trip table (now 19 rows). Nothing was reordered or renamed, and 07-08 through 07-11 can use them.
- **`--sample N` is a real, honest stride.** The hash sink still decodes and inspects every frame, but hashes and stores a frame only when its decode index is a multiple of N. Stored frame `k` is decode frame `k x N`, the chain digest covers only stored digests, the per-frame record budget charges only stored frames, and `frame_interval` is widened N-fold so the 07-03 locator pairs a sampled chain correctly (stored frame 20 at exactly 8/5 s on the HuffYUV pair).
- **The fingerprint says it was sampled, in two places.** `sampling_state` is `sampled:N` on the measurement and `sampling.video_frame_stride` is N in the envelope. Both are absent at N = 1, and a `--sample 1` snapshot is byte-identical to one taken without the flag, so no existing snapshot or golden moved.
- **`compare_hash` refuses to compare different selections.** An unequal `sampling_state` pairing with a `sampled:N` side is `skipped:sampling_mismatch`, naming both strides and telling the user to re-run with the same `--sample`; never `hash_incomparable`, never a pass. Truncation keeps precedence, and a malformed stored string is never treated as sampled.
- **Sampling never changes a value it does not own.** On the corrupt MPEG-4 fixture every non-hash measurement, the decode-path ledger, `meta.decode_errors` (nonzero, so the test has teeth) and the hash check's own error/corrupt/geometry evidence are identical with and without `--sample 3`; audio hashing is identical on an audio+video file.
- **Usage errors are precise.** `--sample 0`, negatives, non-integers, beyond-`int` values, and `N >= 2` with content decoding off exit 64 naming the flag, on all four commands.

## Task Commits

1. **Task 1: The four additive skip reasons, in every site at once** - `e8b0035` (feat)
2. **Task 2: --sample N end to end, and the sampling_mismatch branch in compare_hash** - `2f8d4af` (feat)
3. **Task 3: Document --sample and the D-08 amendment; full suite and lints** - `f3c7572` (docs)

**Plan metadata:** recorded by the closing `docs(07-04)` commit.

## Files Created/Modified

- `src/core/model.h` - the four enum members, conversions, `sampling_state_sampled`, `parse_sampled_stride`
- `src/probe/video_decode.{h,cpp}` - the stride in `VideoDecodeState`, `video_frame_hashable`, `StreamVideoDecode::sample_stride`
- `src/analyzers/content/video_frame_hash.cpp` - `sampled:N`, the envelope key, the stored-frame chain and the stored-count zero-frame skip
- `src/compare/hash.cpp` - the sampling branch, `describe_sampling`, `sample_stride` evidence on a sampled locator report
- `src/cli/options.{h,cpp}` and the four command files - `SampleArgs`, `add_sample_flag`, `resolve_sample_stride`, the `--content` help text
- `tests/unit/test_sampling_mismatch.cpp` (8 cases) and `tests/integration/test_video_sampling.cpp` (11 cases)
- `docs/checks/content.video.frame_hash.md`, `claude_docs/00-design-and-requirements.md`, `claude_docs/06-content-and-size-analysis.md` - the `--sample` paragraphs and the D-08 amendment

## Decisions Made

See `key-decisions`. The two that later plans lean on: `StreamVideoDecode::frame_count` is now the decoded count and `frame_digests.size()` is the stored count, and a sampled locator report carries `sample_stride` so a frame number is read as a stored-frame index.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Files the plan's `files_modified` list omitted had to change**
- **Found during:** Task 2
- **Issue:** The plan says the stride is "forwarded in `run_probe`" and into the sink but lists neither `src/probe/orchestrator.cpp` nor `src/probe/packet_scan.cpp`, both of which carry the `ProbeOptions`-to-`PacketScanRequest` and `PacketScanRequest`-to-`VideoDecodeState` hand-offs. `src/cli/commands/compare.h` (the `run_compare` declaration) and `tests/unit/test_video_decode.cpp` (sink-level stride and budget tests) also changed.
- **Fix:** Edited those four files; each change is the one-line forwarding the plan describes.
- **Files modified:** `src/probe/orchestrator.cpp`, `src/probe/packet_scan.cpp`, `src/cli/commands/compare.h`, `tests/unit/test_video_decode.cpp`
- **Committed in:** `2f8d4af`

**2. [Rule 1 - Bug] The plan's zero-frame check would mis-skip a sampled stream**
- **Found during:** Task 2, reading the analyzer
- **Issue:** `decode.frame_count == 0` decides `insufficient_data`. With the stride, `frame_count` counts every decoded frame while the chain holds only stored ones, so a stream whose only stride-multiple frames were unhashable would build an empty `HashChain`.
- **Fix:** The check now reads `frame_digests.empty()`. At stride 1 the two are identical.
- **Files modified:** `src/analyzers/content/video_frame_hash.cpp`
- **Committed in:** `2f8d4af`

**3. [Rule 2 - Missing critical] A skipped frame must still count as a decode error when it would have**
- **Found during:** Task 2
- **Issue:** D-08 says `meta.decode_errors` must be identical with and without `--sample`. An unhashable frame (no CPU plane data, unknown format) raises `decode_error_count` only when hashing fails, and a stride skips hashing.
- **Fix:** `video_frame_hashable` (the precondition block of `hash_video_frame`, extracted) is applied to a frame the stride skips, so the count is unchanged. Timestamp usability is likewise decided over every decoded frame.
- **Files modified:** `src/probe/video_decode.{h,cpp}`
- **Committed in:** `2f8d4af`

### Plan statements that did not hold as written

- **`ctest --test-dir build/x64-linux -R "unit\.skip_reason..."`** matches nothing useful: the Catch2-discovered test names are the full case names, so the verify ran as `ctest --preset x64-linux -R "skip_reason|json_schema|junit"` (and `-R "sampling_mismatch|video_sampling"` for Task 2), then the whole suite.
- **Test 3 of Task 1** ("an unknown spelling is rejected") needed a function to call: `skip_reason_from_string` had no direct test, so the inverse-parse round trip over all 19 members was added beside it.
- **`--sample 2 --no-content` on `snapshot`** exits 64 naming `--no-content` only, because snapshot's own must-decode rule fires first. The plan's combined rule is asserted on `compare`, `inspect` and `dir`; the snapshot case is still exit 64.
- **An evidence key and a message note beyond the plan:** a sampled locator report adds `sample_stride` and "frame numbers index the stored frames ... (decode frame = N x number)", so "frame 20" is not misread. The plan's Test 10 states "stored frame 20 (decode frame 40)" but does not say how a reader learns that.
- **The ReportModel/junit vocabulary sites in the plan's Task 1 list are exactly the six `hash_disabled` touches**; no further site needed the four new members (MSVC switch completeness is covered by the junit switch and `skip_reason_to_string`, both extended).

---

**Total deviations:** 3 auto-fixed (1 Rule 1, 1 Rule 2, 1 Rule 3) plus 5 plan statements that did not hold
**Impact on plan:** All three fixes are needed for D-08's own guarantees (nothing the stride does not own may change). No scope creep, and no existing report, snapshot or golden changed.

## TDD Gate Compliance

Tasks 1 and 2 are `tdd="true"` but there is **no RED commit**: the enum members and the stride had to exist before the new test files compiled, so tests and code are committed together per task. As in 07-03, this records the violation rather than hiding it.

Substitute evidence that the tests can fail: the stride-3 budget test only passes because stored frames alone are charged (charging every frame would truncate at 40 records of 100); the `other checks unchanged` test asserts `meta.decode_errors` is nonzero on the corrupt fixture, so a stride that hid errors fails it; the `snapshot pair` test fails if the new branch is removed (the same pair then reports `hash_incomparable`).

## Issues Encountered

- The `x64-linux` rebuild after a doc edit relinks ~50 objects because `docs/checks/*.md` feed `explain`; nothing to fix.
- A `cmake --build` in the foreground exceeds the tool's 120 s limit on a header edit, so full rebuilds were run in the background and awaited.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None. The placeholder sampling paragraph 07-01 left in `docs/checks/content.video.frame_hash.md` is now written.

## Threat Flags

None. `resolve_sample_stride` (T-07-13) and the canonical-only `parse_sampled_stride` with the sampling branch (T-07-14) are the mitigations the plan's threat model names, and both are tested (oversize and malformed inputs; a hostile stored state never passes). The new stored field `sampling.video_frame_stride` is proven by a `read_snapshot` round trip and a known-value assertion; `sampled:N` by the same round trip and by the comparator that reads it.

## Next Phase Readiness

- 07-08 through 07-11 can use `path_incomparable`, `geometry_mismatch`, `not_requested` and `sampling_conflict` as already-registered, schema-valid spellings. `sampling_conflict` (VMAF under `--sample N`, CONTENT-09) now has a real `ProbeOptions::sample_stride` to read.
- A8's edge row stays `unresolved` per the probe's contract, though all three of its real edges are tested: N = 1 (byte-identical), N beyond the frame count (only frame 0 stored), and a sampled side against a truncated side (truncated wins).
- No blockers. `tests/golden/CORPUS_DIGEST*.txt`, the fixtures and the five designated-leg goldens are untouched (the designated-leg run passes 1338/1338).

## Self-Check: PASSED

- `test_sampling_mismatch.cpp` and `test_video_sampling.cpp` exist; commits `e8b0035`, `2f8d4af` and `f3c7572` exist on `gsd/phase-07-content-quality`.
- Acceptance commands re-run against HEAD: the four-spelling greps (model.h 4 each, schema/junit/test 1 each), `git diff main -- src/core/model.h` removes 0 enum lines, `SkipReason::sampling_mismatch` sits after the truncated check and before `first_precondition_mismatch`, `add_sample_flag` is called once per command file, `compare --sample 0` exits 64, `compare --help` contains `not decoding faster`, the doc 00 and doc 06 anchors each match once.
- Full ctest 1338/1338, ten ci.yml lints and `test_gen_corpus_pin_gate.sh` green.

---
*Phase: 07-content-quality*
*Completed: 2026-09-30*
