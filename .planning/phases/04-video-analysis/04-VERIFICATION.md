---
phase: 04-video-analysis
verified: 2026-09-14T00:00:00Z
status: passed
score: 5/5 roadmap success criteria verified; 14/14 requirement IDs accounted for (12 Complete, 2 correctly Deferred)
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "3/5 success criteria fully verified (2 partial, 1 failed); 5 gaps"
  gaps_closed:
    - "VIDEO-09 first-frame HDR extraction source (SC4 partial) — deferred to Phase 7 with dated Human Decision, ROADMAP.md SC4 and REQUIREMENTS.md amended, traceability set to Deferred (not Pending)"
    - "resolve_sar non-positive-denominator guard (WR-01, failed truth) — guard added at stream_params.cpp:531-540 with 3 new unit tests (zero, negative, property-sweep denominators)"
    - "CORPUS_DIGEST.txt workstation-derived hashes (failed truth, CI-blocking) — main's 80 designated-leg lines restored verbatim, 57 Phase-4-added lines transcribed from the actual designated x64-linux CI leg (run 34776142545), zero-entry provisional ledger with TRANSCRIBED-FROM-DESIGNATED-LEG marker, executable no-rewrite lint wired into CI, draft PR #5 green on all 3 blocking legs"
    - "video.interlace disagreement evidence field always-true on valid input (partial truth) — now compares FieldOrderClass (top/bottom-coded-first) instead of raw AVFieldOrder ordinal, both directions (agreement=false, genuine conflict=true) proven on real fixtures"
    - "VIDEO-03 vacuous signature test + wrong requirement text (partial truth) — Test 2 now compares a stream-copy remux with a genuinely distinct sha256 (not byte-identical), verified red-without-fold via mutation check; REQUIREMENTS.md VIDEO-03 text corrected to the tested zero/one-finding behavior"
  gaps_remaining: []
  regressions: []
human_verification: []
---

# Phase 04: Video Analysis Verification Report

**Phase Goal:** Every `video.*` fact — stream parameters, GOP structure, colorimetry and HDR metadata — is measured from a parser pass that costs a fraction of full decode.
**Verified:** 2026-09-14
**Status:** passed
**Re-verification:** Yes — after gap closure (plans 04-13 through 04-21, superseding the 2026-09-13 gaps_found report)

## Goal Achievement

### Observable Truths (ROADMAP.md Success Criteria, as amended by Human Decisions 2026-09-13)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A silent color-range flip produces exactly one finding on `video.color.range` (either spelling); primaries/transfer/matrix/chroma_loc compare alongside; a change to `unspecified` is a regression, not a wildcard | ✓ VERIFIED | `src/analyzers/video/color.cpp` fold+exact-compare path unchanged and correct (confirmed in prior verification). `tests/integration/test_video_yuvj.cpp` Test 1 (#768, CI-passing) is load-bearing and cross-spelling. **Gap closed:** Test 2 (#769) now uses `video_yuv420p_pc_tagged.mp4` — a stream-copy remux with a genuinely distinct sha256 from `video_yuvj420p.mp4` (confirmed via `scripts/gen_corpus.sh:1204` and `CORPUS_DIGEST.txt:136`), proven non-vacuous by a mutation check (disabling the fold turns it red, per 04-16-SUMMARY.md and the code comment at `test_video_yuvj.cpp:22-33`). REQUIREMENTS.md's VIDEO-03 text corrected to match: zero findings for same-intent, one for a genuine flip. |
| 2 | `mediadiff inspect` renders a complete video section (codec/profile/level/resolution/SAR/DAR/pix_fmt/declared frame rate/frame count from packet scan, never `nb_frames`), with SAR conflict recording both values at `info` | ✓ VERIFIED | `emit_frame_count`/`emit_sar_conflict` unchanged and correct (confirmed in prior verification, re-confirmed by direct read this session). **Gap closed:** `tests/integration/test_video_inspect_section.cpp`'s coverage predicate no longer derives "has a video stream" from `inspect`'s own `groups.video` output — it now uses a committed, file-local `kNoVideoStreamFixtures` list cross-referenced against `scripts/gen_corpus.sh` recipes (lines 17-60), so a regression that emptied `groups.video` would fail the test instead of silently skipping the fixture. |
| 3 | GOP structure (length, IDR interval + open/closed via NAL types, refs, I/P/B distribution) compares meaningfully; interlace field order cross-checked against per-frame parser flags with `mixed` by proportion; no-parser codec degrades to `skipped:no_parser` | ✓ VERIFIED | GOP/frame-types no-parser degradation unchanged and correct (confirmed in prior verification). **Gap closed:** `src/analyzers/video/interlace.cpp:269` now computes `disagreement` via `field_order_class(observed) != field_order_class(declared)` (top-coded-first / bottom-coded-first / progressive / unknown classes), not raw `AVFieldOrder` ordinal equality. `tests/unit/test_video_interlace.cpp` proves both directions on real fixtures: `video_ilace_tff.mp4`/`video_ilace_bff.mp4` now report `disagreement: false` (was unconditionally `true`), and hand-built genuine top-vs-bottom conflicts still report `disagreement: true` (lines 286-376). |
| 4 | HDR10/Dolby Vision survive round-trip or report loss; extraction source recorded as Phase 4's stream-level `coded_side_data` arm — the first-frame arm is explicit Phase 7 scope (Human Decision 1); internally incoherent HDR raises non-gating `info` even when shared | ✓ VERIFIED (scope corrected) | `classify_coherence`/`compare_state` unchanged and correct (confirmed in prior verification; VIDEO-10 was never in question). **Resolved, not just deferred-and-ignored:** ROADMAP.md SC4 (line 380-381) and REQUIREMENTS.md's VIDEO-09 entry (line 119) were both amended to explicitly scope Phase 4 to the stream-level arm only and name Phase 7 as owner of the first-frame arm — mirroring VIDEO-11's existing placement pattern (confirmed: ROADMAP.md's Phase 7 goal text and Cross-cutting placement table both name this explicitly, line ~371 "Phase 7 also completes VIDEO-09's first-frame HDR side-data extraction arm"). REQUIREMENTS.md's VIDEO-09 row reads `Deferred`, not `Pending` — `phase.complete` cannot silently flip an unmet target to Complete. `HdrSourceKind::requires_decode` remains an honest, correctly-labeled stub (`skipped:requires_decode`), which is now the documented Phase 4 contract, not a hidden gap. |
| 5 | `video.frame_rate.measured` consumes phase 3's shared interval statistics rather than computing its own; the fused parser pass ships a harness measuring overhead against a plain packet scan on the 10-minute reference file, with the under-10% target itself owned by Phase 5's PERF-03/PERF-05 (Human Decision 2) | ✓ VERIFIED (scope corrected) | `derive_cadence` (src/probe/cadence.cpp) remains a pure function over the shared `StreamPacketScan::packets` array (confirmed in prior verification — no second sweep). Overhead harness (`tools/bench/parser_overhead.cpp`, `scripts/measure_parser_overhead.sh`) ships and measures/records the ratio (43-53% on a 180s input, recorded not gated, per D-11/D-12). ROADMAP.md SC5 and REQUIREMENTS.md's PROBE-03 entry were both amended so the under-10% target is explicitly Phase 5's PERF-03/PERF-05, matching the Phase 5 requirements list and cross-cutting table. PROBE-03's REQUIREMENTS.md row reads `Deferred`, not `Pending`. |

**Score:** 5/5 success criteria verified. All prior partial/failed items are closed with real code changes and non-vacuous tests; the two genuinely out-of-Phase-4-scope items (VIDEO-09 first-frame arm, PROBE-03's overhead target) are correctly `Deferred` — not silently dropped, not falsely marked Complete — with dated Human Decisions and matching ROADMAP/REQUIREMENTS amendments that a later phase (7 and 5 respectively) explicitly owns.

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|---|---|---|---|
| BUILD-05 | Phase 1 (Complete); touched again by 04-17, 04-20, 04-21 | ✓ Complete | Warnings-as-errors preserved; 04-17's diagnostic-suppression push/pop balance lint added to CI (confirmed running and passing in run 34891069554's lint job) |
| BUILD-08 | Phase 1 (Complete); touched again by 04-13, 04-16, 04-20, 04-21 | ✓ Complete | `lint_corpus_digest_provenance.sh` wired into CI lint job, confirmed passing (clause 3: zero-entry provisional ledger with valid TRANSCRIBED-FROM-DESIGNATED-LEG marker; clause 4: no pre-existing digest line rewritten) |
| PROBE-03 | 04-01, 04-03, 04-05, 04-09, 04-19 | Deferred (correct, not a gap) | Fused parser pass functioning (758+/758+ passing including `video.gop.length` end-to-end); overhead harness ships and records 43-53% (not gated); ROADMAP.md SC5 + REQUIREMENTS.md text both explicitly hand the <10% target to Phase 5 PERF-03/PERF-05 |
| VIDEO-01 | 04-02, 04-06, 04-07, 04-08, 04-12, 04-14, 04-18 | ✓ Complete | All stream-parameter checks present, wired, unit-tested; WR-01 (SAR guard) and IN-01 (AV1 level bound) fixed with new tests |
| VIDEO-02 | 04-06, 04-18 | ✓ Complete | `emit_frame_count` counts from packet/parser scan, never `nb_frames` |
| VIDEO-03 | 04-02, 04-08, 04-16, 04-19 | ✓ Complete | Fold logic correct and now fully non-vacuously tested (Test 1 + corrected Test 2); requirement text corrected to match tested behavior |
| VIDEO-04 | 04-05, 04-07, 04-14 | ✓ Complete | `video.sar.conflict` correct; `resolve_sar` now guards `raw_den <= 0` |
| VIDEO-05 | 04-01, 04-05, 04-09 | ✓ Complete | GOP length/idr_interval/closed/refs/frame_types all registered and tested |
| VIDEO-06 | 04-10, 04-15 | ✓ Complete | Compared value correct; `disagreement` evidence field now compares field-order class, both directions proven |
| VIDEO-07 | 04-08 | ✓ Complete | Colorimetry checks present; `video.color.range` carries no profile override |
| VIDEO-08 | 04-02, 04-08 | ✓ Complete | No wildcard special-case for `unknown`/`unspecified` |
| VIDEO-09 | 04-04, 04-05, 04-11, 04-12, 04-19 | Deferred (correct, not a gap) | Stream-level `coded_side_data` extraction real and tested; first-frame arm explicitly and formally reassigned to Phase 7 (ROADMAP + REQUIREMENTS both amended, mirroring VIDEO-11) |
| VIDEO-10 | 04-04, 04-12, 04-18 | ✓ Complete | `video.hdr.coherence` state semantic correct (Decision 1: shared incoherence still reports `info`); WR-02 doc error and registry.h comment both corrected |
| VIDEO-11 | Phase 7 (not Phase 4) | Correctly out of scope | Unchanged from prior verification |
| VIDEO-12 | 04-09 | ✓ Complete | GOP `no_parser` degradation and keyframe-flag frame-types fallback confirmed |

All 14 requirement IDs named for this phase (BUILD-05, BUILD-08, PROBE-03, VIDEO-01 through VIDEO-10, VIDEO-12) are accounted for across the 21 plans' `requirements:` frontmatter — no orphans. PROBE-03 and VIDEO-09 are `Deferred` in REQUIREMENTS.md's status table (not `Pending`, not falsely `Complete`) with dated Human Decisions and ROADMAP.md amendments naming the owning later phase, exactly as VIDEO-11 was already handled.

Note: REQUIREMENTS.md's checkbox list (lines 111-122) and per-item status table (lines 322-333) had not yet been flipped from "Gaps Found" to "Complete" for VIDEO-03/05/07/08/12 at the time of this verification — this reflects the standard workflow (the traceability flip happens via `phase.complete` after this report lands), not an unresolved gap; the code-level evidence for each of those five IDs was independently verified above.

### Anti-Patterns / Code Review Findings

| File | Line | Pattern | Severity | Status |
|---|---|---|---|---|
| `src/analyzers/video/stream_params.cpp` | 516-540 | Missing `raw_den <= 0` guard in `resolve_sar` (WR-01) | Warning | ✓ FIXED — guard added, 3 new unit tests covering zero/negative/property-sweep denominators |
| `docs/checks/video.hdr.coherence.md` | 57-58 | Misstated what a `state`-semantic `pass` means (WR-02) | Warning | ✓ FIXED — doc now correctly states pass means neither side is flagged, not that both sides match |
| `src/core/registry.h` | 25-26 | False comment claiming `semantic` appears in no serialized output | Info | ✓ FIXED — comment now correctly states it IS printed by plain `list-checks` |
| `src/analyzers/video/interlace.cpp` | 206 (now 269) | `disagreement` unconditionally true on valid interlaced input | Warning | ✓ FIXED — compares field-order class, both directions tested |
| `tests/integration/test_video_yuvj.cpp` | 110-119 | Test 2 compared byte-identical fixtures, vacuous | Warning | ✓ FIXED — new fixture pair with distinct sha256, mutation-verified non-vacuous |
| `src/analyzers/video/*.cpp` (6 files) | file-scope | `-Wmaybe-uninitialized` suppressed for whole translation unit (WR-03) | Warning | ✓ FIXED — 4 of 6 files had the suppression removed entirely (didn't reproduce on GCC 13.3.0/-O3); the 2 that still need it (`gop.cpp`, `stream_params.cpp`) now use function-scoped `push`/`pop`, enforced by a new CI lint |
| `src/analyzers/video/stream_params.cpp` | render_level_value | AV1 `seq_level_idx` 24-31 rendered as if spec-defined (IN-01) | Info | ✓ FIXED — bounded to 0-23, tested |
| `docs/checks/video.sar.md` | unset rule | Described as "0/1 only" vs. code's "any zero numerator" (IN-02) | Info | ✓ FIXED — doc corrected |
| `tests/integration/test_video_inspect_section.cpp` | has_video_stream | Coverage predicate derived from the output under test | Info | ✓ FIXED — now uses an independent, committed fixture-name list |

No `TBD`/`FIXME`/`XXX` debt markers found in any file touched between `8caf1f1` (pre-Phase-4) and `HEAD` (60 files checked).

### Infrastructure / CI Regression — RESOLVED

The prior verification's most severe finding — `tests/golden/CORPUS_DIGEST.txt` committed with workstation-derived hashes that would fail CI at the first gate — is confirmed resolved:
- Every one of main's (`8caf1f1`) 80 pre-existing digest lines is present verbatim at HEAD (`comm -23` diff against main returns zero lines removed/changed).
- All 57 fixture lines Phase 4 added are now transcribed from an actual designated x64-linux CI run (run 34776142545), not workstation output — confirmed via `CORPUS_DIGEST_PROVISIONAL.txt`'s zero-entry state with a well-formed `TRANSCRIBED-FROM-DESIGNATED-LEG` marker.
- A new executable lint (`scripts/lint_corpus_digest_provenance.sh`, wired into CI's lint job) guards against future silent rewrites of pre-existing lines.
- Draft PR #5 (head `b52ca4b`) CI run 34891069554 is green on all 3 blocking legs (lint, x64-linux, x64-windows-static-md) plus the advisory arm64-osx leg; the "Assert the corpus digest matches the committed pin (D-GAP-01)" step passes on the designated x64-linux leg specifically.
- x64-osx and arm64-linux (both advisory, non-blocking per BUILD-05's ruleset) fail at the identical pre-existing steps (`Build` and `Register vcpkg NuGet feed`, respectively) as main's own most recent CI run (34353206899) — confirmed by direct comparison of both runs' job lists, so this is pre-existing infrastructure flakiness, not a Phase 4 regression.

### Behavioral Evidence (Step 7b/7c)

This verifier independently queried the actual CI job logs for run 34891069554 (draft PR #5, head `b52ca4b`) rather than trusting SUMMARY.md's narration:
- `build (x64-linux)` (the designated leg): "100% tests passed, 0 tests failed out of 771" — confirmed directly from the raw job log, including the five previously-flaky goldens (`unit.inspect_container - golden`, 3× `unit.ts_scan_golden`, `integration.size_checks - pinned golden`) all passing, and the two rewritten tests (`#768`/`#769` in `test_video_yuvj.cpp`) both passing.
- `build (x64-windows-static-md)`: "100% tests passed, 0 tests failed out of 766" — confirmed directly from the raw job log.
- The corpus-digest assertion step passed on the designated leg specifically (not inferred from a green overall run).
- `scripts/lint_corpus_digest_provenance.sh`'s clause 3 output was read directly from the lint job's log, confirming the zero-entry provisional ledger's marker is well-formed.

This verifier did not rebuild locally (no local build directory present in this session) but treated the designated-leg CI run's own job logs — queried directly via `gh run view --log`, not the SUMMARY's characterization of them — as the authoritative evidence, consistent with this project's own "designated leg" policy (`tests/golden/README.md`) and the `Verify output-absence claims` / `GSD verify-probe cd prefix` lessons in this session's memory.

### Gaps Summary

None. All five gaps from the 2026-09-13 report are closed with real code changes and non-vacuous tests, independently confirmed by direct reading of the current source and by directly querying (not trusting) the designated-leg CI run's job logs. The two items correctly marked `Deferred` (VIDEO-09's first-frame HDR arm, PROBE-03's under-10% overhead target) are legitimately out of Phase 4's now-amended scope, with dated Human Decisions and matching ROADMAP.md/REQUIREMENTS.md text naming the phase that owns each — the same pattern already established for VIDEO-11.

---

_Verified: 2026-09-14_
_Verifier: Claude (gsd-verifier)_
