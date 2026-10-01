---
phase: 07-content-quality
plan: 08
subsystem: video
tags: [perceptual, ssim, lockstep, rendezvous, std-thread, score-unit, d-01, d-02, d-03, content-04, content-07, content-11]
status: complete

requires:
  - phase: 07-content-quality
    plan: 01
    provides: "the fused video decode sweep (VideoDecodeState), the 128-wide luma thumbnail and the scaler record"
  - phase: 07-content-quality
    plan: 03
    provides: "pair_step / frame_time / pairing_window (src/core/frame_pairing.h) and ssim_plane_q24"
  - phase: 07-content-quality
    plan: 05
    provides: "the skip-reason convention for decode-fed checks (partial_scan / insufficient_data)"
provides:
  - "probe/lockstep.{h,cpp}: FrameSlot (single-slot rendezvous), ProducerJob/run_producer, consume(), assemble_perceptual(), probe_sequentially(), fingerprint_pair()"
  - "probe/pair_scorer.{h,cpp}: online time-or-index pairing, q24 SSIM per pair, min / floor-mean / first-below / worst-10"
  - "FrameTap published to from VideoDecodeState::consume_frame after every one-sided sink; TapEnd carries the per-side facts the scorer needs"
  - "content.video.perceptual registered (score unit, bare tolerance 0.015, fail in hw-encoder/transform, info in sw-encoder, ignore in strict/remux)"
  - "Unit::score, unit_label(), the tolerance parser's bare-number form"
affects: [07-14, 07-15]

actuals:
  tokens: 40400
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Two unchanged one-sided sweeps on two std::threads, one single-slot rendezvous per side, the consumer decides which side advances, so the two-file result is deterministic and only written after both threads join"
    - "A producer's catch-all and an RAII join guard make every exit path (early end, error, truncated side, scorer stop) release and join both producers"
    - "A live-only measurement is hidden from one-sided probes by a named skip, and the baseline records its own self-score so the engine compares like with like"

key-files:
  created:
    - src/probe/lockstep.h
    - src/probe/lockstep.cpp
    - src/probe/pair_scorer.h
    - src/probe/pair_scorer.cpp
    - src/analyzers/content/video_perceptual.cpp
    - docs/checks/content.video.perceptual.md
    - tests/integration/test_lockstep.cpp
    - tests/integration/test_video_single_sweep.cpp
    - tests/integration/test_perceptual.cpp
    - tests/unit/test_pair_scorer.cpp
  modified:
    - src/probe/video_decode.h
    - src/probe/video_decode.cpp
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/probe/orchestrator.h
    - src/probe/orchestrator.cpp
    - src/analyzers/content/analyzers.h
    - src/cli/commands/compare.cpp
    - src/cli/commands/list_checks.cpp
    - src/report/json.cpp
    - src/core/registry.h
    - src/core/tolerance.cpp
    - src/core/checks.def
    - tools/gen_registry.py
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt
    - tests/integration/coverage_pairs.h
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_audio_corpus_sweep.cpp
    - tests/integration/test_timeline_structure.cpp
    - tests/integration/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_tolerance.cpp
    - claude_docs/06-content-and-size-analysis.md
    - claude_docs/01-core-concepts.md
    - .planning/phases/07-content-quality/deferred-items.md

key-decisions:
  - "The compared value is the minimum pair score in millionths (num/1000000), baseline self-score exactly 1000000/1000000, so identical media delta is exactly 0 (D-03)"
  - "Frames pair by presentation time from each side's own first frame inside strictly less than half the finer interval; unknown first timestamp or unknown interval on either side pairs the whole comparison by index and records the named reason (D-02)"
  - "Live compare only (D-01): every one-sided probe, including the snapshot side of a compare, is skipped:requires_media; with a committed-snapshot baseline the check therefore does not measure, and a legacy snapshot yields no perceptual finding (the engine drops a measurement present on one side only)"
  - "The consumer decides which side advances; producers never race for the slot order, so --json is byte-identical across runs"
  - "A new Unit::score instead of reusing dB or none: a bare tolerance, with the usage error naming the bare form; unit_label() is the one display helper so JSON and list-checks render it"
  - "Colour range is not normalized: a range flip is a real pixel change video.color.range already reports"
  - "Pre-existing tol/dist comparators ignoring --tol / [tolerance] overrides is NOT fixed here (affects every tol check and changes existing configs); logged in deferred-items.md and stated in the check doc"

patterns-established:
  - "A check whose value exists only for a pair of live inputs: skip it one-sided with a named reason and give the baseline a self-value so the engine's one-side-only rule cannot produce a false positive"

requirements-completed: [CONTENT-04, CONTENT-07, CONTENT-11]

coverage:
  - id: D1
    description: "Identical pictures score exactly 1000000/1000000 (mp4 vs a ts stream copy, index pairing with candidate_interval_unknown); a degraded encode scores below the identical pair and the finding carries min, floor-mean, first-below (strict < 0.985) and the ten-entry worst list, each recomputed by an independent oracle over the collected thumbnails"
    requirement: CONTENT-04
    verification:
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - identical media"
        status: pass
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - degraded"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pair_scorer.cpp#pair_scorer - threshold boundary"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pair_scorer.cpp#pair_scorer - worst list order"
        status: pass
    human_judgment: false
  - id: D2
    description: "Time pairing leaves a dropped frame unpaired instead of shifting later pairs, each side measured from its own first frame; raw elementary streams with no frame interval pair by index with a named fallback reason"
    requirement: CONTENT-04
    verification:
      - kind: unit
        ref: "tests/unit/test_pair_scorer.cpp#pair_scorer - time pairing leaves a dropped frame unpaired"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pair_scorer.cpp#pair_scorer - each side is measured from its own first frame"
        status: pass
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - raw elementary streams pair by index"
        status: pass
    human_judgment: false
  - id: D3
    description: "One sweep per side: one read loop per side, the tap changes no one-sided value, it fires after the thumbnail, cover art is never the primary stream; one frame in flight per side (rendezvous occupancy never above one)"
    requirement: CONTENT-07
    verification:
      - kind: integration
        ref: "tests/integration/test_video_single_sweep.cpp#single_sweep - one read loop per side"
        status: pass
      - kind: integration
        ref: "tests/integration/test_video_single_sweep.cpp#single_sweep - tap does not change one-sided values"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - one frame in flight"
        status: pass
    human_judgment: false
  - id: D4
    description: "The lockstep machinery is deterministic and every exit path releases and joins both producers: early end, a side without video, a truncated side (skips, never scores a prefix), errors keep their order, close wakes a blocked publish, --no-content is sequential"
    requirement: CONTENT-11
    verification:
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - deterministic"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - early end"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - close releases producers"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - a truncated side skips instead of scoring"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - errors keep their order"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - one side without video"
        status: pass
    human_judgment: false
  - id: D5
    description: "Live-only semantics: every one-sided probe (snapshot, inspect, the snapshot side of a compare) is skipped:requires_media and --no-content is skipped:requires_decode; a snapshot written by a pre-plan binary produces no perceptual finding (state case 3, proven with a real snapshot from a scratch build of 47edaa2)"
    requirement: CONTENT-04
    verification:
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - one sided"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - snapshot side"
        status: pass
      - kind: integration
        ref: "tests/integration/test_lockstep.cpp#lockstep - no content is sequential"
        status: pass
    human_judgment: false
  - id: D6
    description: "Serialized fields have a reader: the live measurement survives a snapshot round trip (value, evidence, scaler_path and decode_path_signature compared by value); cross-resolution scores when thumbnails share a shape (128x104 both) and skips geometry_mismatch otherwise; --sample 2 scores about half the pairs and records sampled:2"
    requirement: CONTENT-04
    verification:
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - the live measurement survives a snapshot round trip"
        status: pass
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - cross resolution"
        status: pass
      - kind: integration
        ref: "tests/integration/test_perceptual.cpp#perceptual - sampling"
        status: pass
    human_judgment: false
  - id: D7
    description: "score unit: the bare tolerance parses, a suffixed one is a usage error naming the bare form; the check is registered with the roster's severities and tolerance, and list-checks --effective gains exactly one row"
    requirement: CONTENT-04
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#tolerance - score unit"
        status: pass
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp#tolerance - content.video.perceptual is registered with the roster's attributes"
        status: pass
      - kind: integration
        ref: "tests/integration/test_list_checks.cpp (golden list_checks_effective, +1 row)"
        status: pass
    human_judgment: false

## Performance

- Full suite: 1443 tests, 0 failed (1404 at 47edaa2 plus the 39 this plan added), also under `MEDIADIFF_DESIGNATED_LEG=1`.
- Lint scripts: the ten ci.yml scripts, `test_gen_corpus_pin_gate.sh` and `check_corpus.sh` (244 fixtures) all pass.
- ThreadSanitizer scratch build (not committed; `-fsanitize=thread`, run under `setarch -R` because this kernel's address-space layout aborts a TSAN binary otherwise): lockstep, perceptual and single-sweep suites (23 cases, 1474 assertions) and a real two-file compare reported no data race.
- A `compare` of two files now decodes both on two threads, one frame in flight per side.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Display helper outside the plan's file list**
- **Found during:** Task 2
- **Issue:** `Unit::score` has no suffix, so the report and `list-checks` printed an empty unit.
- **Fix:** `unit_label()` in `registry.h`, used by `src/report/json.cpp` and `src/cli/commands/list_checks.cpp`.
- **Commit:** 4c1ebd0

**2. [Rule 3 - Blocking] Missing include in a new test**
- **Found during:** Task 3
- **Issue:** `test_lockstep.cpp` used `DemuxSession` / `DemuxOptions` through a forward declaration.
- **Fix:** `#include "probe/demux_session.h"`.
- **Commit:** 5472ff0

### Re-baselines (each explained by the perceptual addition alone)

- `tests/golden/list_checks_effective.txt`: one new row, `content.video.perceptual  severity=info  tolerance=15/1000 score`; nothing else changed.
- `test_audio_corpus_sweep.cpp`, the `video_black_tv.mkv` / `video_black_pc.mkv` pair declares `content.video.perceptual`: the range flip changes every luma sample and the score does not normalize range (real binary: min 0.024771, status info under sw-encoder).
- `test_timeline_structure.cpp`, the `dts_backward`, `pts_dupe` and `gap` remux pairs each declare `content.video.perceptual` by name: the shifted presentation timeline leaves 38 / 1 / 3 frames per side unpaired under D-02 time pairing and scores neighbours across the shift (0.750640 / 0.946106 / 0.701355 read off the real binary). No existing value changed in any of them; the new id is the only addition to each set.
- `tests/golden/CORPUS_DIGEST.txt`: two new fixture lines (`video_perc_degraded.mp4`, `video_perc_upscaled.mp4`) with their local hashes and a new summary line; no existing fixture line was removed or rewritten; both names added to the provisional ledger. The `mkv_opus` pair is left as committed.

### Deferred

- **tol/dist comparators ignore `--tol` and `[tolerance]` overrides** (found while testing a loose `--tol` on this check; confirmed identical on a 47edaa2 binary). It affects every `tol`/`dist` check from Phase 2 on, changes what existing configs do, and wants its own plan with a per-comparator test and a look at the Phase 2 goldens. Entry added to `deferred-items.md`; the check doc states the limitation and points at severity tuning.

## Auth gates

None.

## Known Stubs

None.

## Threat Flags

None. The only new surface is a second thread inside the process; it touches no file, network or trust boundary and is joined on every path.

## Issues Encountered

- A fixture-level note for later plans: with a committed-snapshot baseline the `hw-encoder` gate on this check is `requires_media`, a D-01 consequence the user-facing doc states ("keep the baseline media around").
- The ASLR layout of this host aborts a TSAN binary at start; `setarch -R` (including around the build, because Catch2 test discovery runs the binary) works around it for the scratch build only.

## Self-Check: PASSED

- Created files present: lockstep.{h,cpp}, pair_scorer.{h,cpp}, video_perceptual.cpp, content.video.perceptual.md, four test files.
- Commits present: 4c1ebd0, 5472ff0, e4383d8.
