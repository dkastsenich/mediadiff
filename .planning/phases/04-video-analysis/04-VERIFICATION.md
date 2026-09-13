---
phase: 04-video-analysis
verified: 2026-09-13T08:14:39Z
status: gaps_found
score: 3/5 roadmap success criteria fully verified (1 deferred to Phase 5, 1 failed/partial)
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "SC4 — HDR10/Dolby Vision extraction source (stream-level vs first-frame) is recorded"
    status: partial
    reason: >
      Only the stream-level `coded_side_data` extraction arm is implemented. The first-frame
      side-data arm (VIDEO-09's second precedence source) is a named seam only
      (`HdrSourceKind::requires_decode`, src/analyzers/video/hdr.cpp) that always degrades to
      `skipped:requires_decode` rather than actually extracting from the first frame and
      recording that source. This is architecturally impossible without Phase 7's decode pass
      (design decision D-08, 04-CONTEXT.md), the same underlying reason VIDEO-11 was fully
      reassigned to Phase 7 in ROADMAP.md — but VIDEO-09 itself is still mapped to Phase 4 in
      REQUIREMENTS.md and marked Pending there, and no Phase 7 success criterion explicitly names
      HDR first-frame extraction the way ROADMAP.md:382 explicitly names VIDEO-11.
    artifacts:
      - path: "src/analyzers/video/hdr.cpp"
        issue: "resolve_hdr_source's requires_decode branch is a stub for the first-frame arm; only the stream-level arm produces a real extraction"
    missing:
      - "DECIDED 2026-09-13 (user): defer the first-frame side-data source to Phase 7. Amend ROADMAP.md and REQUIREMENTS.md so Phase 4's VIDEO-09 scope is the stream-level coded_side_data source and the first-frame source is explicitly Phase 7 scope (with the decode pass, mirroring VIDEO-11's placement note), and set VIDEO-09's traceability Status to Deferred so phase.complete cannot flip it to Complete."
  - truth: "resolve_sar must never let a non-positive denominator reach video.sar/video.sar.conflict evidence (SC2's SAR-conflict clause, correctness hardening)"
    status: failed
    reason: >
      Code-review finding WR-01, confirmed by direct reading: `detail::resolve_sar`
      (src/analyzers/video/stream_params.cpp:516-519) only special-cases raw_num == 0; a
      raw_num != 0 with raw_den <= 0 (a malformed pasp box or corrupt VUI) passes straight
      through with unset=false, violating the EffectiveSar contract documented in
      analyzers.h:159-172 ("num/den are always a valid, positive-denominator rational"). Not
      currently reachable by any fixture in the committed corpus, so no active false
      positive/negative exists today, but it is a real, unguarded correctness gap in shipped code.
    artifacts:
      - path: "src/analyzers/video/stream_params.cpp"
        issue: "resolve_sar (lines 516-519) missing `raw_den <= 0` guard present in every sibling extraction in the same file (compute_dar, quantize_chromaticity, hdr.cpp's luminance checks)"
    missing:
      - "DECIDED 2026-09-13 (user): fix in gap closure. Add the raw_den <= 0 guard (04-REVIEW.md WR-01's suggested fix) with a unit test that fails without it."
  - truth: "The committed tests/golden/CORPUS_DIGEST.txt matches what the designated CI leg (x64-linux) will independently compute"
    status: failed
    reason: >
      Confirmed by the orchestrator this session and independently corroborated here: commit
      21c7a0f (04-01) replaced all 75 non-trivial fixture hashes in CORPUS_DIGEST.txt with this
      workstation's own ffmpeg-encode output, and later Phase 4 plans appended more
      workstation-derived hashes for new fixtures. HEAD's digest matches only this workstation's
      output. `.github/workflows/ci.yml`'s "Assert the corpus digest matches the committed pin
      (D-GAP-01)" step runs BEFORE vcpkg bootstrap/build, so pushing this branch is expected to
      fail CI on the very first gating step, before any test executes. This is a real, unfixed,
      confirmed regression this phase introduced into shared corpus infrastructure (not scoped to
      Phase 4's own new fixtures only).
    artifacts:
      - path: "tests/golden/CORPUS_DIGEST.txt"
        issue: "committed hashes are workstation-derived, not designated-CI-leg-derived, for the 75 fixtures 04-01 rewrote plus every fixture Phase 4 added"
    missing:
      - "DECIDED 2026-09-13 (user), in this order: (1) the first gap-closure step restores every pre-existing fixture line from main (8caf1f1), which are CI-runner hashes for unchanged recipes; (2) finish all other gap closure, which may add fixtures, and no plan may rewrite an existing CORPUS_DIGEST.txt line (new fixture lines stay provisional); (3) push gsd/phase-04-video-analysis and open a DRAFT PR to main so CI runs, with the orchestrator confirming with the user immediately before the push and again before opening the PR; (4) transcribe the designated leg's scripts/corpus_digest.sh listing from that run, cross-checking its pre-existing lines against main's; (5) push the corrected digest and confirm the designated leg is green, including the five designated-leg goldens."
  - truth: "video.interlace's disagreement evidence signal is meaningful (SC3's cross-check clause)"
    status: partial
    reason: >
      Confirmed by direct reading: src/analyzers/video/interlace.cpp:206 compares
      `AVFieldOrder` ordinals across two disjoint domains — the container's `fiel` atom
      (AV_FIELD_TB/BT) versus every registered parser's own per-frame output
      (AV_FIELD_TT/BB/PROGRESSIVE/UNKNOWN). A correctly-encoded, non-conflicting interlaced file
      therefore reports `disagreement: true` unconditionally on the classification::single path,
      making the field noise rather than signal (the source's own comment calling this "harmless
      by design" records the defect as intent, but TT vs TB differ in field CODING not field
      ORDER). This is evidence-only — the compare engine never reads `disagreement`, and the
      actual COMPARED value (tff/bff/mixed/unknown) is independently confirmed correct — so it
      does not produce a false positive/negative in `compare`, but it does undercut SC3's
      "cross-checked" framing at the evidence layer users see under `-v`.
    artifacts:
      - path: "src/analyzers/video/interlace.cpp"
        issue: "line 206 disagreement computation compares raw ordinals from two different AVFieldOrder subsets"
    missing:
      - "DECIDED 2026-09-13 (user): fix in gap closure. Compare temporal field order only (TT≡TB, BB≡BT) instead of raw ordinal equality, correct the misleading comment at lines 49-52, and add tests proving a consistent interlaced file reports disagreement=false while a genuinely conflicting one reports true."
  - truth: "VIDEO-03's own signature test for the 'range flag' spelling half of SC1 is non-vacuous"
    status: partial
    reason: >
      Confirmed by direct reading of tests/integration/test_video_yuvj.cpp: Test 2 ('yuvj420p vs
      yuv420p-full-range ... produces ZERO non-pass findings') compares two BYTE-IDENTICAL
      fixtures (video_yuvj420p.mp4 == video_yuv420p_pc.mp4, sha256-confirmed in
      deferred-items.md), so it passes trivially regardless of whether the fold logic is correct.
      Test 1 (yuvj420p vs yuv420p-tv) IS load-bearing and does cover a cross-spelling flip (one
      side pix_fmt-implied, one side an explicit range flag), which is the strongest available
      evidence for SC1's "whether it was spelled as a yuvj420p pix_fmt or as a range flag"
      clause — but no fixture pair in the corpus isolates a PURE range-flag flip with neither
      side using the yuvj alias, because the pinned mjpeg encoder always normalizes
      `-color_range pc` to a yuvj* name. VIDEO-03's own literal text ("A yuvj420p -> yuv420p +
      full-range change produces exactly one finding") also contradicts the correct,
      tested behavior (it should produce ZERO findings, since both sides fold to the same
      state) — deferred-items.md already records this as a decision owed to a human.
    artifacts:
      - path: "tests/integration/test_video_yuvj.cpp"
        issue: "Test 2 (line 110) is vacuous; its fixture pair is byte-identical"
    missing:
      - "DECIDED 2026-09-13 (user): fix in gap closure. Replace Test 2 with a test that can fail (a fixture pair whose two spellings survive distinctly, proven non-vacuous by a mutation check), and correct VIDEO-03's wording to match the fold: the same intent spelled two ways produces zero findings, while a real range flip produces exactly one finding on video.color.range."
  - truth: "Phase 4's shipped docs, comments and tests are accurate and can fail (04-REVIEW.md findings plus orchestrator findings)"
    status: partial
    reason: >
      04-REVIEW.md WR-02, WR-03, IN-01 and IN-02, plus two items the review missed: the false
      "semantic appears in no serialized output" comment in src/core/registry.h:25-26, and
      tests/integration/test_video_inspect_section.cpp deciding "has a video stream" from the same
      inspect output it checks, so a regression that emptied groups.video for some fixtures would
      skip them rather than fail. None of these changes a compared value or a check contract.
    artifacts:
      - path: "docs/checks/video.hdr.coherence.md"
        issue: "line 58 says pass means both files are in the SAME state; compare_state passes whenever neither side is flagged (WR-02)"
      - path: "src/analyzers/video"
        issue: "-Wmaybe-uninitialized is suppressed for the whole translation unit, without push/pop, in all six new analyzer files (WR-03)"
      - path: "src/analyzers/video/stream_params.cpp"
        issue: "render_level_value accepts AV1 seq_level_idx 24-31, which have no defined level (IN-01)"
      - path: "docs/checks/video.sar.md"
        issue: "describes the unset rule as 0/1 only; the code treats any zero numerator as unset (IN-02)"
      - path: "src/core/registry.h"
        issue: "lines 25-26 claim the semantic name appears in no serialized output; plain list-checks prints semantic=<name>"
      - path: "tests/integration/test_video_inspect_section.cpp"
        issue: "has_video_stream() is derived from the output under test"
    missing:
      - "DECIDED 2026-09-13 (user): fix all of the above in gap closure. The inspect test must decide which fixtures carry a video stream independently of inspect's own video group."
deferred:
  - truth: "SC5 — the parser pass measures at under 10% overhead over a plain packet scan on the 10-minute reference file"
    addressed_in: "Phase 5"
    evidence: >
      REQUIREMENTS.md maps PERF-03 ("The parser pass adds < 10% over plain PacketScan, and full
      timeline analysis adds < 15%") and PERF-05 ("Performance targets are measured in CI on the
      reference file with regression tracking over time") to Phase 5, and ROADMAP.md's own
      cross-cutting placement table states explicitly: "PERF-01, PERF-03, PERF-05 | 5 | The first
      perf targets land with timeline; the CI perf-tracking harness ships with them rather than
      at the end." Phase 4's own plan list (ROADMAP.md:278) titles 04-03 "Parser-overhead
      measurement harness, recorded not gated (D-11/D-12)" — the deferral was planned, not an
      oversight. Measured result on this workstation: 43-53% overhead on a 180s mpeg4 input
      (tools/bench/parser_overhead.cpp via scripts/measure_parser_overhead.sh), well above 10%,
      but the absolute delta is ~1.5ms — recorded per D-11, not gated, and PROBE-03 is correctly
      left Pending in REQUIREMENTS.md rather than falsely marked Complete.
    decision: "CONFIRMED 2026-09-13 (user): the under-10% target is owned by PERF-03/PERF-05 in Phase 5. Gap closure amends ROADMAP.md SC5 and REQUIREMENTS.md so PROBE-03's Phase 4 scope is the fused parser pass plus its measurement harness, and sets PROBE-03's traceability Status to Deferred so phase.complete cannot flip it to Complete."
human_verification: []
---

# Phase 04: Video Analysis Verification Report

**Phase Goal:** Every `video.*` fact — stream parameters, GOP structure, colorimetry and HDR metadata — is measured from a parser pass that costs a fraction of full decode.
**Verified:** 2026-09-13T08:14:39Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A silent color-range flip produces exactly one finding on `video.color.range` (either spelling); primaries/transfer/matrix/chroma_loc compare alongside; a change to `unspecified` is a regression, not a wildcard | ✓ VERIFIED (with a recorded test-quality caveat) | `src/analyzers/video/color.cpp` `fold_pix_fmt_range`/`emit_color_range` route both spellings through the same fold+exact-compare path; `tests/integration/test_video_yuvj.cpp` Test 1 (load-bearing, cross-spelling) confirms exactly one finding; `emit_primaries/transfer/matrix/chroma_loc` use ordinary `exact` string comparison with no wildcard case for `unknown`/`unspecified`, confirmed in `tests/unit/test_video_color.cpp` (bt709 vs unspecified tests). **Caveat:** Test 2 in the same file is vacuous (byte-identical fixtures) — see gaps. |
| 2 | `mediadiff inspect` renders a complete video section (codec/profile/level/resolution/SAR/DAR/pix_fmt/declared frame rate/frame count from packet scan, never `nb_frames`), with SAR conflict recording both values at `info` | ✓ VERIFIED | `src/analyzers/video/stream_params.cpp:254-270` (`emit_frame_count`) counts from `packet_scan`/`parser_scan`, never `AVStream::nb_frames`; `emit_sar_conflict` (lines 220-241) is its own `info`-severity check recording both container and bitstream values (`04-CHECK-ROSTER.md`'s "SAR-conflict resolution: own check id" decision, implemented verbatim); `04-REVIEW.md` reviewed all six stream-param checks and found no correctness bug. **Caveat:** `tests/integration/test_video_inspect_section.cpp`'s corpus-wide assertion (`fixtures_with_video > 0`) only guards total exclusion, not per-fixture regressions — a coverage-quality note, not a functional gap, since individual field checks are separately unit-tested. |
| 3 | GOP structure (length, IDR interval + open/closed via NAL types, refs, I/P/B distribution) compares meaningfully; interlace field order cross-checked against per-frame parser flags with `mixed` by proportion; no-parser codec degrades to `skipped:no_parser` | ✓ VERIFIED (with a recorded evidence-field defect) | `src/analyzers/video/gop.cpp:235-345` implements `no_parser` skip paths for GOP checks; `src/analyzers/video/frame_types.cpp:124-145` falls back to keyframe-flag granularity (VIDEO-12); `src/analyzers/video/interlace.cpp` correctly reports the observed (cross-checked) field order as the COMPARED value, with `mixed` reported via exact integer proportions. **Caveat:** the `disagreement` evidence field (interlace.cpp:206) compares `AVFieldOrder` ordinals across two disjoint domains and is unconditionally `true` on any valid interlaced file — evidence-only, never read by the compare engine, but undercuts the cross-check's visibility under `-v` (deferred-items.md, decision owed). |
| 4 | HDR10/Dolby Vision survive round-trip or report loss; extraction source (stream-level vs first-frame) recorded; internally incoherent HDR raises non-gating `info` even when shared | ✗ PARTIAL / FAILED on the extraction-source clause | `src/analyzers/video/hdr.cpp`'s `classify_coherence`/`compare_state` (`src/compare/state.cpp`) exactly implement the corrected 04-CHECK-ROSTER.md vocabulary, including Decision 1 (shared incoherence still reports `info`) — confirmed directly in code and via `tests/unit/test_video_hdr.cpp` lines 511, 532. **Gap:** the first-frame extraction arm (VIDEO-09's second precedence source) is not implemented — `HdrSourceKind::requires_decode` is a named stub that always yields `skipped:requires_decode`; only the stream-level `coded_side_data` arm produces a real extraction. REQUIREMENTS.md correctly marks VIDEO-09 Pending (not falsely Complete), but no Phase 7 success criterion explicitly claims this the way ROADMAP.md:382 explicitly does for VIDEO-11. |
| 5 | `video.frame_rate.measured` consumes phase 3's shared interval statistics rather than computing its own; parser pass measures under 10% overhead vs plain packet scan on the 10-minute reference file | ✓ VERIFIED (first clause) / DEFERRED (second clause, see Deferred Items) | `src/probe/cadence.h`/`cadence.cpp`'s `derive_cadence` is a pure function over `StreamPacketScan::packets` — the SAME shared array Phase 3's `PacketScan` builds (PROBE-10) — never a second sweep or a duplicated statistic; this satisfies "shared, not recomputed" even though the concrete form is a pure function over a shared array rather than a precomputed struct (PROBE-10's own explicitly rejected alternative). Overhead: measured 43-53% on a 180s mpeg4 input (`scripts/measure_parser_overhead.sh`), well above 10% and not on the "10-minute reference file" — explicitly recorded-not-gated per D-11/D-12, with the actual gate legitimately owned by Phase 5's PERF-03/PERF-05. |

**Score:** 3/5 success criteria fully verified without qualification; 2 more (SC1, SC3) verified with a recorded non-blocking defect each; SC4 is genuinely partial (extraction-source clause); SC5's overhead clause is legitimately deferred to Phase 5.

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|---|---|---|---|
| PROBE-03 | 04-01, 04-03, 04-05, 04-09 | Pending (deferred to Phase 5 gate) | `ParserScan` fused into `PacketScan` and functioning correctly (confirmed via `video.gop.length` end-to-end and 758/758 passing tests); the "<10% overhead" clause is measured (43-53%) but not gated here by design — see Deferred Items |
| VIDEO-01 | 04-02, 04-06, 04-07, 04-08, 04-12 | ✓ Complete | Stream-parameter checks (codec/profile/level/resolution/sar/dar/pix_fmt/frame_rate/frame_count) all present, wired, unit-tested |
| VIDEO-02 | 04-06 | ✓ Complete | `emit_frame_count` counts from packet/parser scan, never `nb_frames` |
| VIDEO-03 | 04-02, 04-08 | ✓ Complete, with a recorded test defect | Fold logic correct and load-bearing-tested (Test 1/#689/#690/#686); Test 2/#688 is vacuous (see gaps) and VIDEO-03's own text conflicts with the tested/correct behavior — decision owed to a human per deferred-items.md |
| VIDEO-04 | 04-05, 04-07 | ✓ Complete | `video.sar.conflict` is its own `info` check recording both values (see resolve_sar caveat above, WR-01) |
| VIDEO-05 | 04-01, 04-05, 04-09 | ✓ Complete | GOP length/idr_interval/closed/refs/frame_types all registered and tested |
| VIDEO-06 | 04-10 | ✓ Complete, with a recorded evidence defect | Compared value correct; `disagreement` evidence field always-true bug recorded (deferred-items.md, decision owed) |
| VIDEO-07 | 04-08 | ✓ Complete | Colorimetry checks (range/primaries/transfer/matrix/chroma_loc) all present; `video.color.range` deliberately carries no profile override (fails in every profile) |
| VIDEO-08 | 04-02, 04-08 | ✓ Complete | No wildcard special-case for `unknown`/`unspecified` in `exact` comparator; confirmed by `test_video_color.cpp`'s unspecified-direction tests |
| VIDEO-09 | 04-04, 04-05, 04-11, 04-12 | Pending (genuine gap) | Stream-level extraction real; first-frame arm is a stub (`requires_decode`) per D-08 — see gaps |
| VIDEO-10 | 04-04, 04-12 | ✓ Complete | `video.hdr.coherence`'s `state` semantic exactly implements the corrected vocabulary and Decision 1 (shared incoherence still reports `info`) |
| VIDEO-11 | (Phase 7, not Phase 4) | Correctly out of scope | ROADMAP.md:382 and 04-CHECK-ROSTER.md both confirm this is a deliberate, recorded placement decision, not an oversight |
| VIDEO-12 | 04-09 | ✓ Complete | GOP `no_parser` degradation and keyframe-flag frame-types fallback both confirmed in code |

No orphaned requirements: every Phase-4-mapped ID in REQUIREMENTS.md appears in at least one plan's `requirements:` frontmatter field.

### Anti-Patterns / Code Review Findings

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| `src/analyzers/video/stream_params.cpp` | 516-519 | Missing `raw_den <= 0` guard in `resolve_sar`, inconsistent with every sibling extraction in the same phase | Warning (WR-01, 04-REVIEW.md; confirmed by direct read) | Latent correctness gap — not reachable by any current fixture, but violates `EffectiveSar`'s own documented contract |
| `docs/checks/video.hdr.coherence.md:57-58` | — | States `pass` means "both files are in the SAME state" — false; `compare_state` never compares baseline==candidate, only "is either side flagged" | Warning (WR-02, 04-REVIEW.md; confirmed against `src/compare/state.cpp:67-91`) | User-facing documentation error, not a code defect |
| `src/core/registry.h:25-26` | — | Comment claims "the `semantic` field itself appears in no serialized output" — false; plain `mediadiff list-checks` prints `semantic=<name>` (`src/cli/commands/list_checks.cpp:104-106`) | Info (missed by 04-REVIEW.md, confirmed here) | Comment-only inaccuracy; the change itself is additive and not gated by any test on this column |
| `src/analyzers/video/interlace.cpp:206` | 206 | `disagreement` compares `AVFieldOrder` ordinals across two disjoint domains, unconditionally true on valid interlaced input | Warning (deferred-items.md, decision owed; confirmed by direct read) | Evidence-only field, never read by compare engine; undercuts `-v` signal quality |
| `tests/integration/test_video_yuvj.cpp:110-119` | — | Test 2 compares byte-identical fixtures, passes vacuously | Warning (deferred-items.md, decision owed; confirmed by direct read) | Test provides no evidence for the "same intent, two spellings" claim it names |

No `TBD`/`FIXME`/`XXX` unreferenced debt markers found in the files this phase touched (04-REVIEW.md's 99-file review and this verifier's own spot checks agree).

### Infrastructure / CI Regression (not a `video.*` logic defect, but blocks shippability)

`tests/golden/CORPUS_DIGEST.txt` was rewritten during this phase (commit 21c7a0f, 04-01) with this workstation's own ffmpeg-encode hashes for all 75 non-trivial fixtures, plus workstation hashes for every fixture Phase 4 added. `.github/workflows/ci.yml`'s digest-assertion step runs before vcpkg bootstrap/build on the designated x64-linux leg, so pushing this branch is expected to fail CI at the first gate. This is confirmed (not speculative): this workstation's own `scripts/corpus_digest.sh` output matches HEAD's committed digest with 0 differing lines, while the pre-phase (8caf1f1) committed digest — CI-runner-derived — matches only 5 of its 81 lines against this workstation. Fixing this requires an actual designated-leg CI run to transcribe correct hashes; it cannot be resolved locally or by human judgment alone.

### Behavioral Evidence (Step 7b/7c, performed this session)

The full test suite was run once, by the orchestrator, at this exact HEAD (995fc01) this session: `ctest --preset x64-linux --output-on-failure` → 758/758 passed, 6 skipped by design (designated-leg-only goldens). A differential build (pre-phase 8caf1f1 vs HEAD 7a75f31) confirmed all five designated-leg goldens are byte-identical old-vs-new, i.e. Phase 4's code changes did not alter their outputs. This verifier did not re-run the suite (no new evidence would result from a second full run of the same HEAD; the constraint against filtering full runs per must-have applies), and instead spot-verified the specific code paths named in every gap/truth above by direct reading of the committed source.

### Gaps Summary

Ten of twelve Phase-4-mapped requirements are genuinely complete and well-tested. The two Pending requirements are handled honestly in REQUIREMENTS.md (neither was falsely marked Complete), but one of them — VIDEO-09's first-frame HDR extraction arm — has no explicit later-phase success criterion claiming it the way VIDEO-11 was explicitly reassigned, so it is reported here as a gap requiring a human scope decision rather than silently deferred. PROBE-03's overhead clause is cleanly deferred to Phase 5 per explicit PERF-03/PERF-05 requirement mapping and the phase's own "recorded not gated" plan title. Three further defects were confirmed by direct code reading (SAR guard, interlace evidence field, vacuous yuvj test) that the phase's own deferred-items.md already flags as "decisions owed to a human" — none of these produce an active false positive/negative in `compare` against the current fixture corpus, but all three are real and unresolved. Separately, and more urgently for shippability, the corpus digest committed during this phase is confirmed wrong for the designated CI leg and will fail CI on push — this needs an actual CI run to fix, not a code change or a human decision.

## Human Decisions (2026-09-13)

Taken by the user after this report, before gap planning. They supersede every "decision owed to a human" above and in `deferred-items.md`. Gap planning must implement them as written.

| # | Question | Decision | What gap closure must do |
|---|---|---|---|
| 1 | VIDEO-09's first-frame HDR side-data source needs a decode pass | **Defer to Phase 7** | Amend ROADMAP.md and REQUIREMENTS.md: Phase 4's VIDEO-09 scope is the stream-level `coded_side_data` source; the first-frame source is explicitly Phase 7 scope, mirroring VIDEO-11's placement note. Set VIDEO-09's traceability Status to **Deferred**. |
| 2 | PROBE-03's under-10% overhead target (measured 43–53% on a 180 s input) | **Defer the target to Phase 5** (PERF-03/PERF-05) | Amend ROADMAP.md SC5 and REQUIREMENTS.md: PROBE-03's Phase 4 scope is the fused parser pass plus its measurement harness. Set PROBE-03's traceability Status to **Deferred**. |
| 3 | Which confirmed defects to fix now | **All four:** interlace evidence; the vacuous yuvj test plus VIDEO-03's wording; the review warnings and docs (WR-01, WR-02, WR-03, IN-01, IN-02, registry.h comment); the inspect-test predicate | See each gap's `missing` entry above. Every new or changed test must be shown able to fail (mutation check, or a non-byte-identical fixture pair). |
| 4 | When to capture the designated leg's corpus digest | **Draft PR after the gap fixes** | Restore main's (`8caf1f1`) pre-existing fixture lines first. Rewrite no existing digest line. Finish gap closure. Then push the branch and open a draft PR to main. The orchestrator confirms with the user before the push and again before the PR. Transcribe the designated leg's listing and push the corrected digest. Re-verify with that CI run green. |

**Why decisions 1 and 2 must set Deferred rather than leave Pending.** `phase.complete` flips every Phase 4 traceability row reading Pending, In Progress or Gaps Found to Complete. It leaves Out, Deferred and Blocked rows unchanged. A Pending VIDEO-09 or PROBE-03 row would therefore be falsely marked Complete when the phase closes.

**Standing guard for every gap-closure plan that touches fixtures.** `git diff -- tests/golden/CORPUS_DIGEST.txt` must show no removed or changed hash line for a pre-existing fixture. The local `scripts/assert_corpus_digest.sh` passing is not evidence: it compares this workstation's corpus against a digest this workstation wrote.

---

_Verified: 2026-09-13T08:14:39Z_
_Verifier: Claude (gsd-verifier)_
