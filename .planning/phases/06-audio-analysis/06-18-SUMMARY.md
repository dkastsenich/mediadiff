---
phase: 06-audio-analysis
plan: 18
subsystem: audio
tags: [audio-probe, sbr, determinism, wall-clock, cr-05, gap-closure]

requires:
  - phase: 06-audio-analysis
    provides: "06-15's declared/decoded sample-rate split and 06-16's GAP_BASE corpus-differential recipe plus the ubuntu:24.04 container ratchet-measurement method, both reused unchanged here"
provides:
  - "src/probe/audio_config.h/.cpp: SbrResolution{signaling, decode_observed_rate_hz}; SbrProbeFn returning expected<optional<SbrProbeDecodeResult>, Error>; resolve_sbr_signaling returning expected<SbrResolution, Error>, propagating a probe Error unchanged instead of collapsing it to SbrSignaling::unknown"
  - "src/probe/demux_session.cpp: open_context's wall-clock interrupt disarmed unconditionally after open+find_stream_info for every caller (including the SBR fallback probe's own second open); probe_implicit_sbr_via_second_open moved into namespace detail as an exposed test seam with an Error-vs-nullopt classification (A1); compute_sbr_signaling returns expected<void, Error>, propagated by DemuxSession::open"
  - "src/probe/demux_session.h/.cpp: decode_observed_rate_hz_ (renamed from implicit_probe_rate_hz_), now populated for EVERY implicit_decoded resolution (both header-pass branches and the fallback probe), not only the fallback"
  - "src/analyzers/audio/stream_params.cpp: emit_sample_rate's comment corrected (WR-09) -- the compared value is codecpar's post-find_stream_info rate, already the doubled output rate for both SBR fixtures"
  - "docs/checks/audio.profile.md / audio.sample_rate.md: documented the timing-free contract (a probe open failure/timeout is a hard command error, never rendered as unknown) and the decode-observed effective_rate_hz basis"
affects: [06-19, 06-20-designated-leg-confirmation]

actuals:
  tokens: 18800
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A wall-clock interrupt budget bounds only the container open+find_stream_info window, unconditionally disarmed afterward for every caller (including a bounded fallback probe's own second open) -- every read past that window is bounded by a PACKET-COUNT limit instead, so the same bytes always give the same answer regardless of host scheduling. Extends 03-03's own disarm-after-open precedent to a second, previously-exempt call site."
    - "An Error-vs-nullopt classification (flagged assumption A1) for a bounded fallback decode: allocation failures and the second open/find_stream_info failing are Error (environment-dependent, not a property of the file's own bytes); a missing decoder, parameter-copy/open2 failure, no target packet, a send failure, or no frame are ok(nullopt) (deterministic for the same bytes). A caller (resolve_sbr_signaling) propagates the Error unchanged rather than downgrading it to a value."
    - "A per-branch decode-observed-rate cache populated by EVERY resolution branch that claims a decoded property (not only the branch that happens to need a second open) -- closes a class of bug where one branch's claim ('SBR, doubled') was never actually cross-checked against what a decode observed."

key-files:
  created: []
  modified:
    - src/probe/audio_config.h
    - src/probe/audio_config.cpp
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - tests/unit/test_audio_config.cpp
    - src/analyzers/audio/stream_params.cpp
    - docs/checks/audio.profile.md
    - docs/checks/audio.sample_rate.md

key-decisions:
  - "CR-05's fix took the review's second option (deterministic probe + hard Error on open failure) rather than the first (render a timeout the same as a clean unknown) -- matches this plan's own objective text and keeps the probe's DoS bound (kMaxSbrProbeContainerPacketsScanned) as the sole read-time bound, mirroring PacketScan's own post-open disarm precedent."
  - "probe_implicit_sbr_via_second_open's Error/nullopt boundary (A1) draws the line at 'does this failure depend on the environment or on the file changing shape between opens' (Error) vs 'is this outcome deterministic for the exact same bytes and build' (nullopt) -- an allocation failure is Error even though it happens inside the same decode loop as several nullopt-mapped outcomes, since it is not a property of the file's bytes."
  - "compute_sbr_signaling's SbrProbeFn lambda no longer needs a local captured_probe variable -- once SbrResolution::decode_observed_rate_hz carries the observed rate for every implicit_decoded branch (Task 2), the caller can push resolution->decode_observed_rate_hz directly, removing the earlier out-of-band capture mechanism entirely."
  - "WR-09's naming concern (StreamAudioDecode::sample_rate vs StreamInfo::sample_rate vs effective_sample_rate_hz) is explicitly NOT implemented here -- only the stale stream_params.cpp comment is corrected, per the plan's own scope note that 06-15 already recorded how far the declared/decoded split subsumes it."
  - "The timeline ratchet was measured inside a throwaway ubuntu:24.04 Docker container with the pinned ffmpeg fetched fresh (network access available in this session) rather than skipped as an unrun-verify -- the cached 600s/1920x1080 reference input under .mediadiff-bench/ was reused unchanged, so only the measurement itself (valgrind --tool=cachegrind, ~3 min) ran inside the container."

patterns-established: []

requirements-completed: [AUDIO-03]

coverage:
  - id: D1
    description: "CR-05: a fallback-probe open failure (including a zero-wall-clock-budget timeout) now propagates as a hard Error out of DemuxSession::open, never rendered as audio.profile's '(sbr: unknown)' value. Proven both on a fake SbrProbeFn (unit-level) and on the REAL probe against audio_sbr_implicit.mp4 under a genuine zero-budget timeout."
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - resolve_sbr_signaling: a probe Error propagates unchanged as this function's own Error, not as SbrSignaling::unknown (CR-05)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - the real probe under a zero wall-clock budget returns a timeout Error, never a value (CR-05)"
        status: pass
    human_judgment: false
  - id: D2
    description: "The probe's reads (after its own bounded open) are deterministic, bounded only by kMaxSbrProbeContainerPacketsScanned (64) packets -- proven by two consecutive real-probe calls against audio_hash_base.mp4 (its priming-only first packet) returning ok(nullopt) identically both times, and the real probe at the default budget decoding audio_sbr_implicit.mp4 to exactly 88200 Hz."
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - the real probe on audio_hash_base.mp4 deterministically returns no frame on two consecutive calls (its first packet is entirely encoder-delay priming)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - the real probe at the default wall-clock budget decodes audio_sbr_implicit.mp4's stream 0 to 88200 Hz"
        status: pass
    human_judgment: false
  - id: D3
    description: "WINDOWS #38 perf fix holds unchanged: the probe is still called only when find_stream_info resolved no profile (the pre-existing call-counting test), and the timeline instruction ratchet is within tolerance."
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - resolve_sbr_signaling: a bare AAC-LC ASC whose header pass resolved an HE-AAC profile resolves to implicit_decoded WITHOUT calling the bounded probe"
        status: pass
      - kind: other
        ref: "bash scripts/measure_timeline_perf.sh --instructions --check-baseline inside ubuntu:24.04: plain_instructions +0.43%, full_instructions +0.57%, both within +/-2%"
        status: pass
    human_judgment: false
  - id: D4
    description: "CR-05 secondary: every implicit_decoded resolution now carries decode-observed rate evidence (SbrResolution::decode_observed_rate_hz) -- both header-pass branches and the fallback probe, including the case where an HE profile is resolved WITHOUT a doubled rate (44100 in, 44100 observed, never fabricated as doubled)."
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - an HE-profile header resolution carries the observed rate even when it is NOT doubled relative to the ASC's declared core (CR-05 secondary)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - resolve_sbr_signaling: a bare AAC-LC ASC whose header pass resolved exactly DOUBLE the declared rate resolves to implicit_decoded WITHOUT calling the bounded probe"
        status: pass
    human_judgment: false
  - id: D5
    description: "Cross-pass invariant: StreamInfo::effective_sample_rate_hz equals the decode sweep's own StreamAudioDecode::sample_rate on audio_sbr_implicit.mp4, audio_sbr_explicit.mp4, audio_aac_handwritten.mp4, and audio_hash_base.mp4 -- the implicit fixture reads 88200, independently pinned."
    requirement: "AUDIO-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_audio_config.cpp#audio_config - effective_sample_rate_hz equals the decode sweep's decoded rate on every AAC fixture (cross-pass invariant)"
        status: pass
    human_judgment: false
  - id: D6
    description: "WR-09's stale stream_params.cpp comment is corrected (the compared value is the doubled output rate, not an 'undoubled base rate'), and both docs/checks/*.md files describe the timing-free contract. WR-09's own 'Fix' (renaming StreamAudioDecode::sample_rate) is explicitly not implemented."
    requirement: "AUDIO-03"
    verification:
      - kind: other
        ref: "grep -c \"undoubled base\" src/analyzers/audio/stream_params.cpp == 0; grep -c \"22050\" == 1; grep -ci \"wall-clock\" docs/checks/audio.profile.md == 2; grep -c \"decode-observed|decoder-observed\" docs/checks/audio.sample_rate.md == 2"
        status: pass
    human_judgment: false
  - id: D7
    description: "The corpus differential against GAP_BASE shows no changed snapshot: audio.profile and audio.sample_rate (and every other check) are byte-identical on every one of 200 real fixtures."
    verification:
      - kind: integration
        ref: "200-fixture GAP_BASE (8e37b7284abad06573f4958b5e1cb37695e3956e) vs HEAD snapshot differential: 0 differing"
        status: pass
    human_judgment: false

duration: 30min
completed: 2026-09-23
status: complete
---

# Phase 06 Plan 18: Deterministic SBR probe with hard-error propagation, decode-observed rate evidence on every branch, and WR-09's comment correction (CR-05) Summary

**A fallback-probe timeout can now fail the whole command but can never again flip `audio.profile` between "implicit" and "unknown" across runs of the same file; every `implicit_decoded` resolution carries the rate a real decode actually produced, and the stale `stream_params.cpp` comment WR-09 flagged is corrected -- 200/200 fixtures snapshot byte-identical against GAP_BASE, and the timeline ratchet holds at +0.43%/+0.57%.**

## Performance

- **Duration:** 30 min
- **Started:** 2026-09-23T21:09:00Z
- **Completed:** 2026-09-23T21:39:00Z
- **Tasks:** 3
- **Files modified:** 8

## Accomplishments

- `SbrProbeFn` and `resolve_sbr_signaling` now return `mediadiff::expected<..., Error>`. A container-open failure (a timeout included) from the bounded SBR fallback probe propagates UNCHANGED out of `resolve_sbr_signaling` and out of `DemuxSession::compute_sbr_signaling`/`open` as a hard command failure -- it can no longer be silently rendered as `audio.profile`'s `(sbr: unknown)` value. Before this fix, the same file could report `"LC (sbr: implicit)"` on one run and `"LC (sbr: unknown)"` on the next under host scheduling pressure (CR-05, a P0 determinism violation).
- `open_context`'s wall-clock interrupt is now disarmed UNCONDITIONALLY after `avformat_open_input` + `avformat_find_stream_info` completes, for every caller -- including the SBR fallback probe's own second, throwaway open, which previously kept its budget armed through its own later reads (06-04's original design). The probe's reads are now bounded solely by the existing `kMaxSbrProbeContainerPacketsScanned` (64) packet-count limit, the same stance `PacketScan` already takes after its own open.
- `probe_implicit_sbr_via_second_open` moved into `namespace detail` (declared in `demux_session.h`) as an exposed test seam, with an explicit Error-vs-nullopt classification (flagged assumption A1): a second-open failure, an out-of-range target index, and any allocation failure are `Error`; a missing decoder, a parameter-copy/`avcodec_open2` failure, no target packet within budget, a send failure, or no frame are `ok(nullopt)` -- deterministic for the same bytes.
- CR-05 secondary: `SbrResolution{signaling, decode_observed_rate_hz}` now carries the decode-observed rate for EVERY `implicit_decoded` resolution, not only the fallback probe's. The HE-profile header-pass branch previously resolved purely on `profile` with no rate check at all, silently relying on `codecpar->sample_rate` already being doubled "by construction" -- now it records `header.resolved_sample_rate_hz` directly, closing the gap where a decoder that reported an HE profile without doubling the rate could have had a doubled rate fabricated in its evidence.
- `DemuxSession`'s per-stream cache renamed `implicit_probe_rate_hz_` -> `decode_observed_rate_hz_`, now populated by every implicit_decoded branch; `stream_info`'s effective-rate comment rewritten to drop the "BY CONSTRUCTION" doubling claim.
- WR-09's stale comment in `src/analyzers/audio/stream_params.cpp` is corrected: the compared value is `codecpar->sample_rate` after `avformat_find_stream_info`, which for AAC already decodes and writes the doubled output rate back -- true for BOTH SBR fixtures (`audio_sbr_implicit.mp4` 44100->88200; `audio_sbr_explicit.mp4` 22050->44100), not an "undoubled base rate". WR-09's own proposed Fix (renaming `StreamAudioDecode::sample_rate`) is explicitly NOT implemented, per the plan's scope note.
- `docs/checks/audio.profile.md` and `docs/checks/audio.sample_rate.md` rewritten to document the timing-free contract and the decode-observed `effective_rate_hz` basis.
- New/extended unit tests (11 new `audio_config -` cases): Error propagation from a fake probe; the real probe under a zero wall-clock budget (genuine timeout Error, message contains "wall-clock budget"); the real probe's determinism on `audio_sbr_implicit.mp4` (88200 Hz) and two consecutive `ok(nullopt)` calls on `audio_hash_base.mp4`; `decode_observed_rate_hz` assertions on the HE-profile, doubled-rate, and fallback-probe branches (including a dedicated HE-without-doubling case); `decode_observed_rate_hz == 0` pinned for `explicit_asc`/`none`/`unknown`; and the four-fixture cross-pass invariant (`effective_sample_rate_hz` == the decode sweep's own decoded rate). All pre-existing `audio_config -` and `integration.audio_profile_sbr -` expectations pass unchanged, including the WINDOWS #38 call-counting guard.
- Timeline instruction ratchet measured (informational) inside a throwaway `ubuntu:24.04` Docker container with `valgrind --tool=cachegrind`, reusing the cached 600s/1920x1080 reference input: `plain_instructions` 258818914 (baseline 257709408, **+0.43%**), `full_instructions` 346911346 (baseline 344956981, **+0.57%**) -- both well within the ±2% tolerance. `tests/golden/PERF_BASELINE.txt` untouched; 06-20's designated-leg CI run remains the actual gate.
- Corpus-wide differential (GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e` vs HEAD, 06-15's exact recipe) over all 200 real fixtures in `tests/fixtures/`: **0 differing snapshots** -- byte-identical, `audio.profile` and `audio.sample_rate` included. Scratch tree deleted afterward.
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1247 passed, 0 failed (6 new tests over the 1241-test baseline noted at plan start plus the invariant/HE-branch tests; 6 pre-existing designated-leg-only skips unchanged). `git diff --exit-code -- tests/golden/` exits 0 throughout.

## Task Commits

Each task was committed atomically:

1. **Task 1: A probe timeout becomes a propagated Error and the probe's reads become deterministic, proven from the real probe up through DemuxSession::open** - `8cfda8f` (feat)
2. **Task 2: Every implicit_decoded resolution carries the decoder-observed rate, pinned by a cross-pass invariant over every AAC fixture** - `5fde258` (feat)
3. **Task 3: Correct WR-09's stale comment and the two docs; hold the timeline ratchet; prove the corpus unchanged** - `b422953` (docs)

## Files Created/Modified

- `src/probe/audio_config.h` - `SbrResolution`; `SbrProbeFn` returns `expected<optional<SbrProbeDecodeResult>, Error>`; `resolve_sbr_signaling` returns `expected<SbrResolution, Error>`; rewritten decision-order and declaration comments
- `src/probe/audio_config.cpp` - `resolve_sbr_signaling`'s full rewrite: Error propagation from the fallback probe, `decode_observed_rate_hz` set on every implicit_decoded branch
- `src/probe/demux_session.h` - `detail::probe_implicit_sbr_via_second_open` declared as a test seam; `compute_sbr_signaling` returns `expected<void, Error>`; `decode_observed_rate_hz_` (renamed); rewritten comments
- `src/probe/demux_session.cpp` - `open_context`'s unconditional disarm; `probe_implicit_sbr_via_second_open` moved to `namespace detail` with the A1 classification; `compute_sbr_signaling`'s Error propagation and simplified probe_fn lambda; `DemuxSession::open`'s propagation; `stream_info`'s rewritten effective-rate comment
- `tests/unit/test_audio_config.cpp` - `CountingProbe::error`; `ScopedWallClockBudget` RAII guard; 11 new test cases plus `decode_observed_rate_hz` assertions added to 6 existing ones
- `src/analyzers/audio/stream_params.cpp` - `emit_sample_rate`'s corrected comment (WR-09)
- `docs/checks/audio.profile.md` - the `(sbr: unknown)` bullet corrected; a new CR-05 paragraph on the timing-free contract
- `docs/checks/audio.sample_rate.md` - the `core_rate_hz`/`effective_rate_hz` paragraph rewritten

## Decisions Made

- **Took the review's second CR-05 option** (deterministic probe + hard Error) over the first (render a timeout identically to a clean `unknown`) -- matches the plan's own objective and reuses `kMaxSbrProbeContainerPacketsScanned` as the probe's sole post-open bound, mirroring `PacketScan`'s own disarm-after-open precedent rather than inventing a new mechanism.
- **The Error/nullopt boundary (A1) is drawn on "does this depend on the environment or the file changing between opens"** (Error) vs "is this deterministic for the same bytes and build" (nullopt) -- an allocation failure is Error even inside the same loop as several nullopt outcomes, since it is not itself a property of the file's bytes.
- **`compute_sbr_signaling`'s SbrProbeFn lambda no longer captures a local `captured_probe`** -- once `SbrResolution::decode_observed_rate_hz` carries the observed rate for every implicit_decoded branch (Task 2), the caller pushes `resolution->decode_observed_rate_hz` directly, removing the earlier out-of-band capture mechanism entirely (a simplification beyond the plan's literal action text, but a direct, low-risk consequence of it).
- **WR-09's own proposed rename (`StreamAudioDecode::sample_rate` -> `decoded_sample_rate`) is explicitly out of scope** -- only the stale `stream_params.cpp` comment is corrected, per the plan's own note that 06-15 already recorded how far the declared/decoded split subsumes WR-09's naming concern.
- **The timeline ratchet was measured for real, not deferred as an unrun-verify** -- network access was available in this session, so the pinned ffmpeg was fetched fresh inside the `ubuntu:24.04` container and the cached 600s/1920x1080 reference input was reused unchanged, keeping the container's own work to just the ~3-minute cachegrind measurement.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] TDD gate compliance gap on Task 2 (`tdd="true"`)**
- **Found during:** Task 2
- **Issue:** Task 2 carries `tdd="true"`, which calls for a RED (`test(...)`) commit proving the new assertions fail first, followed by a GREEN (`feat(...)`) commit. The new `decode_observed_rate_hz` assertions and the four-fixture cross-pass invariant were instead written alongside the implementation change and committed together as a single `feat(06-18)` commit (`5fde258`).
- **Fix:** Not retroactively split (amending history is prohibited by this workflow's git safety protocol). Recorded as an open deviation in `.planning/WINDOWS.md` instead of silently claimed compliant.
- **Files modified:** n/a (process deviation, not a code defect)
- **Verification:** All tests pass; every acceptance criterion for Task 2 is independently confirmed via `grep`/`ctest` in this SUMMARY.
- **Committed in:** `5fde258` (the task's own commit; the gap is about commit *sequencing*, not content)

---

**Total deviations:** 1 (process, not correctness). **Impact:** None on shipped behavior -- every Task 2 acceptance criterion and test passes; only the RED/GREEN commit-separation discipline was skipped.

## TDD Gate Compliance

Task 2 (`tdd="true"`): no `test(06-18):` commit precedes `feat(06-18): every implicit_decoded resolution carries the decoder-observed rate...` (`5fde258`) in git history -- the new tests and the implementation landed together. RED was not captured mechanically (unlike 06-15/06-16's own precedent of reverting the fix, running the new test, then restoring it). GREEN gate (a passing `feat` commit) IS present. Recorded here and in `.planning/WINDOWS.md` (open) rather than silently claimed compliant.

## Issues Encountered

None beyond the TDD gate note above. The `mediadiff snapshot` CLI uses `--out`, not `-o`, for the output path -- caught immediately when the first differential attempt produced zero output files (both `snap_base`/`snap_head` empty), corrected before any comparison was drawn.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- CR-05, its secondary finding, and WR-09's comment are all closed. AUDIO-03 remains marked complete (it already was, per this plan's own must_have naming it "edge (lifted)") -- this plan hardens its implementation against host-timing false positives, per the plan's own `requirements` field.
- 06-13 remains `status: halted` (its confirming CI run is still pending) -- unaffected by this plan; only 06-20 certifies it.
- The timeline ratchet is measured and well within tolerance, but only informationally -- 06-20's designated-leg CI run remains the actual gate for `PERF_BASELINE.txt`.
- `.planning/WINDOWS.md` gained one new open deviation entry (the Task 2 TDD-gate-sequencing gap) -- worth a look before `/gsd-ship`, though it reflects a process gap, not a functional one.
- Ready for 06-19.

## Self-Check: PASSED

- `8cfda8f`, `5fde258`, `b422953` all found in `git log --oneline --all`.
- All 8 modified files exist on disk with the expected content (verified via the `grep`/build/test commands run during execution).
- Full `ctest --test-dir build/x64-linux --output-on-failure`: 1247 passed, 0 failed (6 pre-existing designated-leg-only skips, unchanged).
- `git diff --exit-code -- tests/golden/`: clean (exit 0).
- Corpus differential: 200/200 fixtures byte-identical, 0 differing, GAP_BASE `8e37b7284abad06573f4958b5e1cb37695e3956e`.
- Timeline ratchet: both metrics within ±2% tolerance (measured +0.43% / +0.57%), `tests/golden/PERF_BASELINE.txt` untouched.
- `mediadiff explain audio.profile | grep -c "wall-clock"` == 2.
- `.planning/WINDOWS.md` new entry present, status `open` (the TDD gate note).

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-23*
