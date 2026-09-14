---
phase: 04-video-analysis
plan: 12
subsystem: video-analysis
tags: [ffmpeg, dolby-vision, hdr, comparison-semantics, doc03, inspect]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-11's HDR extraction seam (resolve_hdr_source/could_carry_frame_level_hdr) and mdcv/cll families this plan's video.hdr.dovi and video.hdr.coherence reuse; 04-05's hand-written dvcC box fixtures; 04-04's coherence fixture corpus and its read-back table"
provides:
  - "video.hdr.dovi / video.hdr.dovi.config: Dolby Vision configuration-record presence and exact comparison, read from codecpar->coded_side_data (AV_PKT_DATA_DOVI_CONF), configuration record only (no per-frame RPU, v1 scope)"
  - "A new, 8th comparison semantic (`state`) added additively to the registry/codegen/comparator-dispatch layers, for checks whose OWN shared value is itself the finding (an `exact` semantic would fold shared incoherence into an invisible `pass`)"
  - "video.hdr.coherence: a non-gating info check classifying a stream's transfer-characteristic/HDR-metadata agreement into a closed four-value vocabulary (coherent, hdr_meta_sdr_transfer, pq_without_mdcv, indeterminate), corrected mid-phase from the plan's original proposal (HLG folds into `coherent`, not `pq_without_mdcv`; shared incoherence reports `info`, not an invisible `pass`)"
  - "A corpus-wide inspect-section integration test proving all nine ROADMAP SC2 checks render (in both --json and text) for every fixture with a video stream, enumerated from the fixtures directory rather than a hand list"
  - "An HLG-without-MDCV corpus fixture (video_hdr_hlg_nomdcv.mp4) and three new DOC-03 declared pairs"
affects: [video-analysis, comparison-engine, check-registry, corpus-generation]

actuals:
  tokens: 25141
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "8th comparison Semantic (`state`): compares each side's value independently against a check's own `flagged_values` set rather than baseline==candidate, so two sides sharing a flagged value still produce a non-pass Finding at the check's own severity. Wired through registry.h (CheckDef.flagged_values/_count), semantics.h/state.cpp, exact.cpp's comparator_for(), and tools/gen_registry.py's codegen (validation + kFlaggedValues_<name>[] emission)."
    - "TransferBucket intermediate enum (pq/hlg/indeterminate/sdr) + total switch (no default arm) for classifying a closed value vocabulary by construction, mirroring the could-carry-HDR precedent from 04-11."

key-files:
  created:
    - src/compare/state.cpp
    - docs/checks/video.hdr.dovi.md
    - docs/checks/video.hdr.dovi.config.md
    - docs/checks/video.hdr.coherence.md
    - tests/integration/test_video_inspect_section.cpp
  modified:
    - src/analyzers/video/hdr.cpp
    - src/core/checks.def
    - src/core/registry.h
    - src/compare/semantics.h
    - src/compare/exact.cpp
    - tools/gen_registry.py
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/analyzers/video/analyzers.h
    - tests/unit/test_video_hdr.cpp
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/list_checks_effective.txt
    - .planning/phases/04-video-analysis/04-CHECK-ROSTER.md

key-decisions:
  - "Task 2's checkpoint decision was dispatched pre-resolved and OVERRODE the plan's own proposed design: shared incoherence (including a byte-identical copy) must report a non-gating `info` finding rather than comparing invisibly `pass` — this is not achievable under the plan's proposed `exact` semantic, so a new `state` semantic was added additively (Decision 1)."
  - "HLG (arib-std-b67) with no mastering-display metadata is `coherent`, not `pq_without_mdcv` — HLG is scene-referred and legitimately ships without MDCV under ITU-R BT.2100; `pq_without_mdcv` names PQ specifically (Decision 2). The plan's originally-proposed vocabulary definitions were corrected accordingly."
  - "The `state` semantic never reads Measurement::evidence — it classifies purely on Value against CheckDef.flagged_values, keeping it a general-purpose 8th semantic rather than a one-off special case for this check."

requirements-completed: [VIDEO-01, VIDEO-10]

coverage:
  - id: D1
    description: "video.hdr.dovi / video.hdr.dovi.config compare a Dolby Vision configuration record read from coded_side_data, with per-frame RPU explicitly out of scope for v1"
    requirement: "VIDEO-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp (Task 1 Tests 1-6)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp declared_pairs() video.hdr.dovi / video.hdr.dovi.config"
        status: pass
    human_judgment: false
  - id: D2
    description: "video.hdr.coherence reports a non-gating info state from a closed four-value vocabulary, visible even when both files share the incoherence, via a new additive `state` comparison semantic"
    requirement: "VIDEO-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp (Task 3 Tests 1-7, incl. Decision 1/Decision 2/mutation-proof tests)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp declared_pairs() video.hdr.coherence"
        status: pass
    human_judgment: false
  - id: D3
    description: "inspect renders all nine ROADMAP SC2 video-identity checks for every corpus fixture with a video stream, in both --json and text output"
    requirement: "VIDEO-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_video_inspect_section.cpp (both TEST_CASEs, corpus-wide)"
        status: pass
    human_judgment: false
  - id: D4
    description: "VIDEO-09's first-frame side-data precedence arm (the D-08 requires_decode path exercised against a real decode pass) remains seam-only, not yet decode-backed"
    verification: []
    human_judgment: true
    rationale: "Deliberately held open — see Requirement Marking Notes below. Requires the Phase 7 decode pass to close; not something this plan's tests can prove either way."

duration: ~35min (approximate — this execution was resumed after a context compaction; the pre-compaction authoring/exploration time is not reflected in the timestamp span used here)
completed: 2026-09-13
status: complete
---

# Phase 4 Plan 12: Dolby Vision Configuration, HDR Coherence via a New `state` Semantic, and Corpus-Wide Inspect Proof Summary

**Dolby Vision configuration-record comparison, a new 8th comparison semantic (`state`) built specifically so shared HDR incoherence is never invisible, and a corpus-wide proof that `inspect` renders all nine SC2 checks for every fixture.**

## Performance

- **Duration:** ~35min (approximate, see note above)
- **Completed:** 2026-09-13
- **Tasks:** 3/3 completed
- **Files modified:** 34 (28 in the final Task 3 commit; 6 in Tasks 1-2's commits)

## Accomplishments

- Registered `video.hdr.dovi` (presence) and `video.hdr.dovi.config` (exact) reading the Dolby Vision configuration record from `codecpar->coded_side_data` (`AV_PKT_DATA_DOVI_CONF`), reusing 04-11's `resolve_hdr_source`/`could_carry_frame_level_hdr` seam — no decode, no Dolby Vision tooling anywhere in the build.
- Added a brand-new, 8th comparison `Semantic` (`state`) additively across `src/core/registry.h`, `tools/gen_registry.py`, `src/compare/semantics.h`, `src/compare/state.cpp`, and `src/compare/exact.cpp`'s dispatch — required because the plan's originally-proposed `exact` semantic cannot make two files that SHARE an incoherent state report anything but an invisible `pass`.
- Registered `video.hdr.coherence` (`state`/`info`, `flagged_values=["hdr_meta_sdr_transfer","pq_without_mdcv"]`, no tolerance, no profile overrides) with the checkpoint-corrected four-value vocabulary (HLG-without-MDCV is `coherent`, not `pq_without_mdcv`).
- Wrote a corpus-wide `tests/integration/test_video_inspect_section.cpp` proving `inspect` renders all nine ROADMAP SC2 checks for every fixture with a video stream, enumerated from `tests/fixtures/` (not a hand list), in both `--json` and text output.
- Added the `video_hdr_hlg_nomdcv.mp4` fixture via `scripts/gen_corpus.sh` and regenerated `CORPUS_DIGEST.txt`.
- Added three DOC-03 declared pairs (`video.hdr.dovi`, `video.hdr.dovi.config`, `video.hdr.coherence`) to `tests/integration/test_doc03_coverage.cpp`.
- Refreshed `tests/golden/list_checks_effective.txt` (three new rows appended, nothing else changed).

## Task Commits

Each task was committed atomically:

1. **Task 1: video.hdr.dovi and video.hdr.dovi.config** - `e1f2c8c` (feat)
2. **Task 2: Confirm video.hdr.coherence's id spelling and value vocabulary (checkpoint, pre-resolved)** - `7195eb4` (docs)
3. **Task 3: video.hdr.coherence via the new `state` semantic, corpus-wide inspect test, DOC-03 pairs** - `8f52409` (feat)

**Plan metadata:** committed separately after this SUMMARY (see below).

## Files Created/Modified

- `src/probe/demux_session.h` / `.cpp` - Extracts the Dolby Vision configuration record's fields (version, profile, level, RPU/EL/BL flags, signal-compatibility id, metadata-compression value) from `coded_side_data`.
- `src/analyzers/video/analyzers.h` - `detail::dovi_payload_too_short()` / `kDoviConfigRecordSize`, a directly-testable seam for the short-payload boundary (T-4-53) no real fixture can reach, mirroring the `quantize_chromaticity` precedent.
- `src/analyzers/video/hdr.cpp` - Adds the Dolby Vision family (`emit_dovi`, `emit_dovi_config`) and the coherence guard (`TransferBucket`, `bucket_transfer`, `classify_coherence`, `coherence_reason`, `emit_coherence`) reusing the mdcv/cll extraction and could-carry decision.
- `src/core/checks.def` - Registers `video.hdr.dovi`, `video.hdr.dovi.config`, `video.hdr.coherence` (with `flagged_values`).
- `src/core/registry.h` - Adds `Semantic::state` and `CheckDef.flagged_values`/`flagged_values_count`.
- `src/compare/semantics.h`, `src/compare/state.cpp` - Declares/implements `compare_state()`.
- `src/compare/exact.cpp` - Dispatches `Semantic::state` to `compare_state`.
- `tools/gen_registry.py` - Validates and emits `flagged_values` codegen for `state`-semantic checks.
- `src/cli/commands/list_checks.cpp` - `semantic_to_string()` handles `state` (fixes `-Werror=switch`).
- `tests/unit/test_fail_first_coverage.cpp`, `tests/unit/test_support_registry.cpp`, `tests/support/test_checks.def`, `tests/support/docs/t.state_flag.md`, `tests/fixtures/snapshots/t.state_flag__{pass,fail,error}.{a,b}.snap.json` - Fail-first per-(semantic,status) and per-semantic presence coverage for the new `state` semantic via the test-only registry.
- `docs/checks/video.hdr.dovi.md`, `docs/checks/video.hdr.dovi.config.md`, `docs/checks/video.hdr.coherence.md` - New `--explain` docs.
- `tests/unit/test_video_hdr.cpp` - 13 new TEST_CASEs (6 Task 1 + 7 Task 3), including the Decision 1/Decision 2 proofs and the T-4-54 cross-check against `video.color.transfer`.
- `tests/integration/test_video_inspect_section.cpp` - New corpus-wide inspect-rendering proof.
- `tests/integration/test_doc03_coverage.cpp` - Three new declared pairs; running-total comment updated to sixty-one.
- `tests/integration/CMakeLists.txt` - Registers the new integration test file.
- `scripts/gen_corpus.sh` - Adds the `video_hdr_hlg_nomdcv.mp4` recipe.
- `tests/golden/CORPUS_DIGEST.txt` - Regenerated for the enlarged corpus.
- `tests/golden/list_checks_effective.txt` - Refreshed via `UPDATE_GOLDENS=1` (three rows appended, nothing else changed — confirmed via `git diff`).
- `.planning/phases/04-video-analysis/04-CHECK-ROSTER.md` - Records Task 2's resolved decision, the corrected vocabulary, and the approval date (2026-09-13).

## Decisions Made

- **Decision 1 (dispatch-mandated, overrides the plan's proposal):** shared incoherence (including a byte-identical copy) must report a non-gating `info` finding, never an invisible `pass`. This is structurally impossible under the plan's originally-proposed `exact` semantic (which compares baseline==candidate), so a new, additive `state` semantic was implemented instead — the minimum change that satisfies the requirement without touching any existing comparator, the JSON/snapshot schema shape, or `list_checks_effective.txt` beyond appending rows.
- **Decision 2 (dispatch-mandated, overrides the plan's proposal):** HLG (`arib-std-b67`) with no mastering-display metadata is `coherent`, not `pq_without_mdcv` — HLG is scene-referred and ships without MDCV under ITU-R BT.2100 by design; `pq_without_mdcv` names PQ specifically. The plan's own checkpoint text (`video.hdr.coherence`'s `coherent` definition: "either a PQ or HLG transfer with mastering-display metadata present") was corrected in the roster and in `classify_coherence()`.
- **Corrected closed vocabulary registered:** `coherent`, `hdr_meta_sdr_transfer`, `pq_without_mdcv`, `indeterminate` — HDR transfers are exactly `{smpte2084 (PQ), arib-std-b67 (HLG)}`; every other transfer with a specified value is SDR.
- The `state` semantic deliberately never reads `Measurement::evidence` — classification is purely `Value` against `CheckDef.flagged_values`, keeping it a reusable general-purpose semantic rather than a one-off special case wired only to this check.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - missing critical functionality] The `state` semantic's own supporting infrastructure was not named in the plan's `files_modified`**
- **Found during:** Task 3 (before Task 2's checkpoint was even dispatched, the plan's own proposed `exact` semantic could not satisfy Decision 1 once it arrived)
- **Issue:** The plan's `files_modified` frontmatter lists only `src/analyzers/video/hdr.cpp`, `src/core/checks.def`, three doc files, `tests/unit/test_video_hdr.cpp`, `tests/integration/test_video_inspect_section.cpp`, `tests/integration/CMakeLists.txt`, and `tests/integration/test_doc03_coverage.cpp`. None of `src/core/registry.h`, `src/compare/semantics.h`, `src/compare/state.cpp`, `src/compare/exact.cpp`, `CMakeLists.txt`, `tools/gen_registry.py`, `src/cli/commands/list_checks.cpp`, `tests/unit/test_fail_first_coverage.cpp`, `tests/unit/test_support_registry.cpp`, or `tests/support/test_checks.def` appear — because the plan (authored before Task 2's dispatch-mandated override) expected `exact`, an already-existing semantic requiring no new infrastructure.
- **Fix:** Added the `state` semantic completely and additively across all eight files above, following the exact pattern the other seven semantics already establish (registry enum entry, codegen validation/emission in `gen_registry.py`, comparator implementation + dispatch, `-Werror=switch` fixes in every exhaustive switch over `Semantic`, fail-first coverage-table entries, and a test-only `t.state_flag` check proving the semantic through the independent test registry).
- **Files modified:** `src/core/registry.h`, `src/compare/semantics.h`, `src/compare/state.cpp` (new), `src/compare/exact.cpp`, `CMakeLists.txt`, `tools/gen_registry.py`, `src/cli/commands/list_checks.cpp`, `tests/unit/test_fail_first_coverage.cpp`, `tests/unit/test_support_registry.cpp`, `tests/support/test_checks.def`, `tests/support/docs/t.state_flag.md`, six `t.state_flag__*.snap.json` fixtures.
- **Verification:** `ctest --test-dir build/x64-linux --output-on-failure` — 758/758 passed, exactly the 6 designated skips (`console_vt`, `inspect_container` golden, three `ts_scan_golden` cases, `size_checks` golden). No existing comparator's tests changed behavior; `list_checks_effective.txt`'s diff is exactly three appended rows.
- **Committed in:** `8f52409` (part of the Task 3 commit).

**2. [Rule 2 - missing critical functionality] Missing HLG-without-MDCV fixture**
- **Found during:** Task 3, required by the dispatch's `<test_evidence_guard>` ("You must add an HLG fixture — none exists")
- **Issue:** No corpus fixture exercised Decision 2's specific case (HLG transfer, no mastering-display metadata).
- **Fix:** Added `video_hdr_hlg_nomdcv.mp4` to `scripts/gen_corpus.sh` (mpeg4/testsrc2, `color_trc=arib-std-b67`, `color_primaries=bt2020`, no MDCV/CLL side data), regenerated it and `tests/golden/CORPUS_DIGEST.txt`.
- **Files modified:** `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST.txt`, `tests/fixtures/GENERATOR_MANIFEST.json`.
- **Verification:** `tests/unit/test_video_hdr.cpp`'s Decision 2 test (line ~495) asserts `video.hdr.coherence == coherent` for this fixture; the T-4-54 cross-check test also runs the fixture through both the HDR and colour analyzers.
- **Committed in:** `8f52409`.

---

**Total deviations:** 2 auto-fixed (both Rule 2 — missing critical functionality required by the dispatch-mandated decisions, neither discretionary).
**Impact on plan:** No scope creep beyond what Decisions 1 and 2 themselves required. The `state` semantic is general-purpose (any future check needing "does either side hit a flagged value" can reuse it), not a one-off hack.

## Mutation Testing (test_evidence_guard)

Three mutations were required and performed, each proving a specific test would catch the regression it targets, then restored to the pristine, committed state and re-verified via `diff` against backups (never `git checkout --`, to keep the mutation fully isolated from any other in-flight edit):

**(a) Revert `video.hdr.coherence` to the `exact` semantic (make `state` degrade to baseline==candidate).**
Mutated `compare_state()` to short-circuit to `Status::pass` whenever `baseline.value == candidate.value` (the `exact`-equivalent rule). Re-ran `tests/unit/test_video_hdr.cpp`'s VIDEO-10-E1 test ("video_hdr_sdr_mdcv.mp4 vs its byte-identical copy... both sides showing the shared incoherence") — **it FAILED** (`coherence->status == Status::info` assertion failed, `pass` was reported instead), proving Decision 1 is load-bearing. Reverted the mutation; `diff` against the pristine `src/compare/state.cpp` confirmed byte-identical restoration; re-ran the same test — **passed**.

**(b) Classify HLG-without-MDCV as `pq_without_mdcv`.**
Mutated `classify_coherence()`'s `TransferBucket::hlg` arm to return `kCoherencePqWithoutMdcv` instead of `kCoherenceCoherent`. Re-ran the Decision 2 test (`video_hdr_hlg_nomdcv.mp4 (HLG, no MDCV) reports video.hdr.coherence as coherent`) — **it FAILED**, proving Decision 2 is load-bearing. Reverted; `diff` against the pristine `src/analyzers/video/hdr.cpp` confirmed byte-identical restoration; re-ran — **passed**.

**(c) Always return `coherent` regardless of input.**
Mutated `classify_coherence()` to unconditionally `return kCoherenceCoherent;` as its first line. Re-ran the `sdr_mdcv` (hdr_meta_sdr_transfer) and `pq_nomdcv` (pq_without_mdcv) trigger tests — **both FAILED**, proving the flagged-value classification itself (not just the two Decisions) is load-bearing. Reverted; `diff`-confirmed restoration; re-ran — **passed**.

After all three mutations were reverted and re-verified, the full committed state (`8f52409`) was rebuilt and the entire suite re-run clean (758/758, 6 designated skips) before this SUMMARY was written.

## Issues Encountered

None beyond the deviations documented above. `-Werror=switch` surfaced in three files (`list_checks.cpp`, `test_fail_first_coverage.cpp`, plus the `test_support_registry.cpp` `kAllSemantics` completeness gate) as an immediate, expected consequence of adding an 8th `Semantic` enumerator — each fixed by adding the `state` arm, per the additive-semantic pattern the other seven semantics already establish.

## User Setup Required

None - no external service configuration required.

## Requirement Marking Notes

Per the `<requirement_marking_guard>`, `gsd-tools query requirements.ready-ids` was run against this plan's `requirements` list (`VIDEO-01, VIDEO-09, VIDEO-10`) AFTER this SUMMARY was written, and only ids reported in its `.ready[]` array were passed to `requirements.mark-complete`:

- **VIDEO-01** — marked complete. All nine named SC2 ids (`video.codec`, `video.profile`, `video.level`, `video.resolution`, `video.sar`, `video.dar`, `video.pix_fmt`, `video.frame_rate.declared`, `video.frame_count`) are registered with DOC-03 pairs (confirmed directly against `tests/integration/test_doc03_coverage.cpp`'s `declared_pairs()` before marking), and the corpus-wide inspect-rendering test (`test_video_inspect_section.cpp`) passes.
- **VIDEO-09 — deliberately NOT marked.** VIDEO-09 is ready structurally (all three HDR families — mdcv/cll from 04-11, dovi/dovi.config from this plan — are registered and DOC-03-paired), but its first-frame side-data precedence arm is seam-only until the Phase 7 decode pass: `resolve_hdr_source`'s `requires_decode` classification is exercised by tests as a named skip, never by an actual decode confirming or overriding stream-level metadata against real per-frame side data. That arm cannot be proven until Phase 7 exists.
- **VIDEO-10** — marked complete. Both Decision 1 (shared incoherence fires) and Decision 2 (HLG-without-MDCV is coherent) were proven load-bearing by mutation checks (a) and (b) above; the flagged-value classification mechanism itself was proven by mutation check (c).

## Self-Check: PASSED

- `src/compare/state.cpp` — FOUND
- `docs/checks/video.hdr.coherence.md` — FOUND
- `docs/checks/video.hdr.dovi.md` — FOUND
- `docs/checks/video.hdr.dovi.config.md` — FOUND
- `tests/integration/test_video_inspect_section.cpp` — FOUND
- Commit `e1f2c8c` — FOUND in `git log --oneline --all`
- Commit `7195eb4` — FOUND in `git log --oneline --all`
- Commit `8f52409` — FOUND in `git log --oneline --all`

## Next Phase Readiness

Phase 4 (video-analysis) is now complete — this was its final plan (12 of 12). All nine ROADMAP SC2 checks render for every corpus fixture with a video stream, in both output modes, proven corpus-wide rather than by spot check. VIDEO-09 remains partially open (its decode-backed precedence arm) pending Phase 7; this is a known, explicitly-recorded gap rather than a silent one.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*
