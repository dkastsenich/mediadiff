---
phase: 07-content-quality
plan: 09
subsystem: compare
tags: [tol, path-incomparable, precondition, trust-04, content-05, pairing, lockstep, dir, d-02, d-04, huffyuv]
status: complete

requires:
  - phase: 07-content-quality
    plan: 08
    provides: "content.video.perceptual evidence carrying scaler_path and decode_path_signature; PairScorer; fingerprint_pair"
  - phase: 07-content-quality
    plan: 03
    provides: "the one exact frame-pairing rule (src/core/frame_pairing.h)"
provides:
  - "kTolPreconditionKeys = {scaler_path, decode_path_signature} and first_tol_precondition_mismatch in compare_tol: a differing or one-sided key is skipped:path_incomparable, even inside tolerance"
  - "CONTENT-05 (as amended by D-02) proven on real fixtures: dropped frame, 60 to 30 fps rate change, MP4 vs MKV and TS remuxes, raw MPEG-2 ES index fallback; duplicate and empty side on synthetic tapped frames"
  - "dir --content probes each pair through fingerprint_pair (lockstep) with the per-side cap derived from 2 * resolved_threads"
  - "fixtures video_perc_60.mkv and video_perc_30.mkv (LGPL, bitexact), appended to CORPUS_DIGEST.txt and listed in CORPUS_DIGEST_PROVISIONAL.txt"
affects: [07-10, 07-14, 07-15]

actuals:
  tokens: 9400
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "An evidence-driven precondition table in a comparator (no check id named): a key on one side only counts as a mismatch, and a mismatch skips even when the magnitudes agree"
    - "A per-side resource cap observed through the evidence a partial-scan skip already carries (probe_memory_cap_bytes), so a CLI-level test proves the cap without a production test hook"

key-files:
  created:
    - tests/unit/test_tol_path_signature.cpp
    - tests/integration/test_lockstep_pairing.cpp
  modified:
    - src/compare/tol.cpp
    - src/compare/semantics.h
    - src/cli/commands/dir.cpp
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_pair_scorer.cpp
    - tests/integration/CMakeLists.txt
    - tests/integration/test_dir_mode.cpp
    - docs/checks/content.video.perceptual.md
    - claude_docs/00-design-and-requirements.md
    - claude_docs/01-core-concepts.md
    - .planning/ROADMAP.md

key-decisions:
  - "TRUST-04's guard lives in compare_tol as a generic evidence table, never keyed on a check id; codecs are never a precondition, so an H.264-vs-HEVC pair from one build still scores (D-04)"
  - "The precondition check runs right after the tolerance parse and before any magnitude work, so a mismatch skips regardless of what the two magnitudes read"
  - "video_perc_30.mkv is decimated with select+setpts, not the fps filter the plan named: fps over Matroska's 1 ms timestamps keeps neighbouring frames at some ticks and would not score exactly 1000000"
  - "dir --content derives the per-side cap from 2 * resolved_threads and leaves the no-content cap at budget / threads"

patterns-established:
  - "Observe a derived cap through probe_memory_cap_bytes in partial-scan evidence at a budget/thread count chosen so the halved cap is crossed and the unhalved cap is not"

requirements-completed: [CONTENT-05, TRUST-04]

coverage:
  - id: D1
    description: "compare_tol skips with path_incomparable when scaler_path or decode_path_signature differs or is one-sided, even inside tolerance; evidence-free tol checks unchanged"
    requirement: TRUST-04
    verification:
      - kind: unit
        ref: "tests/unit/test_tol_path_signature.cpp#tol_path_signature - scaler differs / decode path differs / one-sided key / inside tolerance still skips / equal paths / no keys"
        status: pass
      - kind: integration
        ref: "ctest --preset x64-linux (1458 tests, full suite, every existing tol check unchanged)"
        status: pass
    human_judgment: false
  - id: D2
    description: "A dropped frame leaves exactly one baseline frame unpaired and every later frame pairs (minimum 1000000); a 60 fps baseline vs its 30 fps decimation scores 90 pairs with 90 unpaired"
    requirement: CONTENT-05
    verification:
      - kind: integration
        ref: "tests/integration/test_lockstep_pairing.cpp#lockstep_pairing - dropped frame / rate change"
        status: pass
    human_judgment: false
  - id: D3
    description: "MP4 vs MKV and TS remuxes pair every frame; a raw MPEG-2 ES pair falls back to index pairing with a recorded reason"
    requirement: CONTENT-05
    verification:
      - kind: integration
        ref: "tests/integration/test_lockstep_pairing.cpp#lockstep_pairing - remux rounding / raw es fallback"
        status: pass
    human_judgment: false
  - id: D4
    description: "A duplicated candidate frame pairs once and is counted in unpaired_candidate; an empty side yields zero pairs, a counted tail and no summary"
    requirement: CONTENT-05
    verification:
      - kind: unit
        ref: "tests/unit/test_pair_scorer.cpp#pair_scorer - duplicate / empty side"
        status: pass
    human_judgment: false
  - id: D5
    description: "dir --content scores each pair in lockstep (live content.video.perceptual) inside the halved per-side cap, and the output is byte-identical across runs and thread counts"
    verification:
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp#dir_mode - content lockstep / content halves the cap / content deterministic"
        status: pass
    human_judgment: false

duration: 12min
completed: 2026-10-01
---

# Phase 7 Plan 09: Tol Path Preconditions, Pairing Proofs and dir Lockstep Summary

**compare_tol refuses to compare across differing scaler or decode paths via a generic evidence table (skipped:path_incomparable, even inside tolerance), CONTENT-05's time pairing is proven on drops, rate changes, remuxes and raw streams, and `dir --content` scores pairs in lockstep inside a halved per-side memory cap.**

## Performance

- **Duration:** about 12 min
- **Started:** 2026-10-01T18:16:00Z
- **Completed:** 2026-10-01T18:28:00Z
- **Tasks:** 3
- **Files modified:** 16

## Accomplishments
- TRUST-04 / D-04: `kTolPreconditionKeys = {scaler_path, decode_path_signature}` with the same one-sided-key rule as `compare_hash`. A mismatch is `skipped:path_incomparable` with a remediation hint, before any magnitude work. A grep confirmed only `src/probe/lockstep.cpp` emits either key, so every timeline, audio and meta `tol` finding is unchanged; the full suite (1458 tests) is green.
- CONTENT-05 as amended by D-02, on real fixtures: `video_loc_huffyuv.mkv` vs its packet-40-dropped copy scores `1000000` with `unpaired_baseline == 1`; `video_perc_60.mkv` vs `video_perc_30.mkv` scores 90 pairs with 90 unpaired; MP4 vs MKV pairs by time and vs TS by index, every frame paired; a raw MPEG-2 ES pair falls back to index pairing with `pairing_fallback` recorded.
- `dir --content` now calls `fingerprint_pair`, so `content.video.perceptual` is a live finding per file (it was `requires_media`), with the per-side cap `derive_per_file_cap_bytes(budget, 2 * resolved_threads)`.
- ROADMAP Phase 7 criteria 2 and 3 carry the D-02 and D-04 amendments; doc 00, doc 01 and the perceptual check doc are updated.

## Task Commits

1. **Task 1: TRUST-04, a generic path precondition table in compare_tol** - `bd94123` (feat)
2. **Task 2: CONTENT-05 pairing proven on real fixtures and synthetic tapped frames** - `9a15b63` (test)
3. **Task 3: dir --content in lockstep inside the halved per-side budget** - `b2895b2` (feat)

**Plan metadata:** recorded in the docs commit that follows this file.

_Note: tdd="true" tasks were written test-and-implementation together per task (one commit each), not as separate RED/GREEN commits; see Deviations._

## Files Created/Modified
- `src/compare/tol.cpp` - `kTolPreconditionKeys`, `first_tol_precondition_mismatch`, and the `path_incomparable` branch in `compare_tol`
- `src/compare/semantics.h` - comment on the new `compare_tol` contract
- `tests/unit/test_tol_path_signature.cpp` - six TRUST-04 proofs on synthetic evidence
- `tests/integration/test_lockstep_pairing.cpp` - four real-fixture pairing proofs
- `tests/unit/test_pair_scorer.cpp` - empty-side and duplicate-frame cases
- `scripts/gen_corpus.sh` - `video_perc_60.mkv`, `video_perc_30.mkv` recipes
- `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - two new lines (existing lines untouched) and the two names in the provisional ledger
- `src/cli/commands/dir.cpp` - `fingerprint_pair` in the job lambda; halved cap
- `tests/integration/test_dir_mode.cpp` - three `dir --content` cases
- `docs/checks/content.video.perceptual.md`, `claude_docs/00-design-and-requirements.md`, `claude_docs/01-core-concepts.md`, `.planning/ROADMAP.md` - the amendments

## Decisions Made
- The guard is a table of evidence keys that names no check id; it sits after the tolerance parse and before the magnitude extraction.
- `video_perc_30.mkv` uses `select=not(mod(n\,2)),setpts=N/(30*TB)` with `-r 30`. Its framemd5 equals the even-indexed frames of `video_perc_60.mkv` (90 of 90). The plan's `fps=30` kept 60 fps frames 0 2 5 6 8 11 12 ... because Matroska stores 1 ms timestamps.
- The no-content `dir` cap stays `budget / threads`; only content decode doubles the sweeps per job.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] fps filter does not decimate the 1 ms-rounded Matroska stream cleanly**
- **Found during:** Task 2 (the plan's own framemd5 confirmation step)
- **Issue:** `-vf fps=30` over `video_perc_60.mkv` produced frames that are not a pure decimation, so the plan's expected minimum of `1000000` would have failed.
- **Fix:** decimate with `select` plus `setpts` (both LGPL); recorded the framemd5 check in the recipe comment.
- **Files modified:** `scripts/gen_corpus.sh`
- **Verification:** 90 of 90 framemd5 lines equal the even-indexed 60 fps frames; `lockstep_pairing - rate change` passes.
- **Committed in:** `9a15b63`

**2. [Rule 3 - Blocking] The plan's Test 2 could not read a `probe_memory_cap_bytes` diagnostic from a dir run**
- **Found during:** Task 3
- **Issue:** `dir --json` carries no fingerprint envelope diagnostics, and the cap lives in the child process, so the plan's "each fingerprint's diagnostic" is unobservable.
- **Fix:** observe the cap through the `probe_memory_cap_bytes` evidence that partial-scan skips already carry: `video_perc_60.mkv` (180 decoded-frame records, 17280 bytes) at `--probe-memory-budget-mb 1 --threads 32 --content` overflows the halved 16384-byte cap and reports exactly that value. A temporary revert of the halving made this test fail (non-vacuity checked). The no-content half asserts no cap evidence appears (the unhalved cap is not crossed); it cannot read the unhalved value itself.
- **Files modified:** `tests/integration/test_dir_mode.cpp`
- **Committed in:** `b2895b2`

**3. [Rule 3 - Blocking] Test 3 of Task 2 names a 1.4 s TS remux that pairs by index, not time**
- **Found during:** Task 2
- **Issue:** the plan says the MP4 vs TS pair "pairs every frame despite the 1.4 s offset"; 07-08 already established that MPEG-TS declares no frame rate, so this pair index-pairs (`candidate_interval_unknown`). Every frame is still paired.
- **Fix:** the test asserts `unpaired_* == 0`, minimum `1000000`, and records `pairing: index` with that reason for the TS and `pairing: time` for the MKV.
- **Committed in:** `9a15b63`

**4. [Process] TDD tasks committed as one commit each rather than RED then GREEN**
- Tests and implementation were written together per task, so the RED-fails-first gate was not observed for Tasks 1 and 3; the Task 3 cap test was verified non-vacuous by reverting the change. The pairing proofs in Task 2 test code that already existed from 07-03 and 07-08.

---

**Total deviations:** 3 auto-fixed (1 bug, 2 blocking) plus 1 process note
**Impact on plan:** none on scope; the fixture recipe and two test shapes differ from the plan's wording, the requirement outcomes do not.

## Issues Encountered
- A first full `bash scripts/gen_corpus.sh` on this workstation changed many existing local fixtures (encoder thread count, timezone). It was undone by re-running `TZ=UTC taskset -c 0-3 bash scripts/gen_corpus.sh`, which restored the local digest to its prior state plus the two new fixtures. Only the two new digest lines were added to `CORPUS_DIGEST.txt`; `git diff 1696d28` shows additions and the summary line only.

## Known Stubs

None.

## Threat Flags

None. T-07-28 (a score compared across differing paths) is mitigated by `kTolPreconditionKeys`; T-07-29 (`dir --content` doubling peak memory) is mitigated by the `2 * resolved_threads` cap and asserted through `probe_memory_cap_bytes`.

## Caveats for the verifier
- TRUST-04 is proven on synthetic evidence (A19): under D-01 both sides come from one build in one run, so no real fixture pair can trip the guard today. The requirement text also names `quality.*` checks, which do not exist yet; the table is generic, so they inherit the guard if they emit either evidence key.
- The new digest lines hold locally computed hashes; the two names are in `CORPUS_DIGEST_PROVISIONAL.txt` until plan 07-15 transcribes designated-leg values.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- The two-file trust story is complete; later plans (07-10 onward) that emit `quality.*` two-file scores need only write `scaler_path` and `decode_path_signature` evidence to be guarded.

## Self-Check: PASSED

- Files: `src/compare/tol.cpp`, `tests/unit/test_tol_path_signature.cpp`, `tests/integration/test_lockstep_pairing.cpp` found.
- Commits: `bd94123`, `9a15b63`, `b2895b2` found.
- Full suite 1458/1458, all 11 CI lint scripts exit 0, `git diff main -- tests/golden/CORPUS_DIGEST.txt` removes no fixture line.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
