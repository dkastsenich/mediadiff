---
phase: 05-timeline-analysis
verified: 2026-09-19T07:57:28Z
status: passed
score: 5/5 success criteria verified
behavior_unverified: 0
overrides_applied: 0
overrides: []
re_verification:
  previous_status: gaps_found
  previous_score: 2/5 success criteria verified
  gaps_closed:
    - "SC1 — `step` unreachable: amended (UD-1, human decision at 05-21's blocking-human checkpoint) to `narrow-vocabulary` + `span:declared`; `DriftPattern::step` removed from the code, SC1/TIME-07/roster/docs amended and attributed; the amended wording (constant-offset / linear-drift / irregular, with a spliced trim documented as `irregular`) is verified against the real binary."
    - "SC2 — MPEG-TS 33-bit wraparound not unwrapped in start_duration/stream_params/size/jitter_vfr/av_sync: fixed by the promoted TimelinePacketView (05-16/05-17/05-18); the no-change wrap pair now produces exactly the declared set (the genuine applied -output_ts_offset, plus the pre-existing, both-sides-shared duration.coherence artifact, plus wrap_events x2) and nothing else."
    - "SC4 — priming units bug (samples misread as ms) in av_sync.cpp: fixed by 05-14's sample-rate rescale; the NTSC MP4-vs-MKV pair now reports zero timeline.* non-pass findings."
    - "dts_monotonic inferred tie on MPEG-TS: fixed by 05-15/05-20's container-DTS PES-header join, further hardened by the code-review fix (commit c281bf3) making the join stride-aware and judging only joined packets; MP4-to-TS pair's video dts_monotonic now reports pass with dts_source=container_pes, container_joined=100."
    - "jitter/vfr_profile false warn on NTSC MP4-to-MKV (WINDOWS.md #28): fixed by 05-19's quantization-aware binning/sigma; the pair's whole-report declared set now contains zero timeline.* ids."
    - "Memory safety — sorted_pts_with_span out-of-bounds read (CR-01) and its untestable anonymous-namespace placement (WR-01): both resolved in 05-14; WR-01's own follow-on review finding (mixed joined/unjoined DTS axis, a latent false-positive risk) fixed in the post-execution code review round (commit c281bf3, human-authorized 'fix now')."
  gaps_remaining: []
  regressions: []
---

# Phase 5: Timeline Analysis Verification Report

**Phase Goal:** The family mediadiff is judged on — every `timeline.*` check and the A/V drift algorithm — computed on integer/rational math with false positives designed out.
**Verified:** 2026-09-19
**Status:** passed
**Re-verification:** Yes — after gap closure (plans 05-14 through 05-25, plus a post-execution code-review round and its fix, commit c281bf3)

## Verification Method

Build rebuilt from clean cache at HEAD `9216e24` (`cmake --build --preset x64-linux`; ninja reported no stale work, confirming the committed binary already reflected `c281bf3`). Ran, and independently re-read the output of: `ctest --preset x64-linux --output-on-failure` (1007/1007, 6 pre-existing designated-leg-only skips), `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux -R golden` (15/15), `bash scripts/assert_corpus_digest.sh` (162 lines compared, 3 documented Opus exclusions), and direct `mediadiff compare --json` reproductions of every fixture pair named in the prior VERIFICATION.md's gaps and in this task's re-verification instructions. Also used `gh run view` to independently re-fetch and grep the designated-leg CI log for run `35391084761` (not relayed from any SUMMARY) to confirm the perf ratchet and job conclusion first-hand.

## Previous Gaps: Status

| # | Prior gap | Status now | Evidence |
|---|---|---|---|
| 1 | SC1 — `step` unreachable | **Resolved (amended)** | `DriftPattern::step` removed from `av_sync.cpp` (grep confirms no `step` enum value remains; only comments referencing the withdrawal). ROADMAP SC1, TIME-07, `05-CHECK-ROSTER.md`, `docs/checks/timeline.av_drift.pattern.md` all carry the `amended 2026-09-18, UD-1` attribution, citing `05-STEP-DESIGN.md`'s `## Decision` (human chose "Narrow vocabulary" + "span:declared" at a `gate=blocking-human` checkpoint). `ctest -R "ROADMAP SC1"` passes: constant-offset, linear-drift, and the spliced-trim pair (now asserted `irregular`, `step_time_ms` absent from evidence) each declare their complete expected finding set. |
| 2 | SC2 — TS 33-bit wrap not unwrapped in start_duration/stream_params/size/jitter_vfr/av_sync | **Resolved** | Reproduced `compare --profile remux tests/fixtures/timeline_ts_nowrap.ts tests/fixtures/timeline_ts_wrap.ts`: non-pass set is exactly `timeline.start` (global, the genuine applied `-output_ts_offset 95440.34`, per-stream `timeline.start` stays pass), `timeline.wrap_events` x2 (info, the state semantic this pair exists to prove), `timeline.duration.coherence` (info, a pre-existing both-sides-shared artifact). No `video.frame_rate.measured`, `size.stream_bitrate`, `timeline.av_offset`/`av_drift` corruption. `ctest -R "wrap trigger pair"` passes. |
| 3 | SC4 — priming-samples-to-ticks units bug | **Resolved** | Reproduced `compare --profile remux tests/fixtures/timeline_ntsc_base.mp4 tests/fixtures/timeline_ntsc_remux.mkv`: zero `timeline.*` findings in the non-pass set at all (only `container.format`, `size.file`, `size.overhead`, `meta.tags` x3 — all real container-remux effects). `ctest -R "NTSC MP4-to-MKV stream copy declares"` passes. |
| 4 | dts_monotonic MPEG-TS read-back inference | **Resolved, further hardened** | Reproduced `compare --profile remux tests/fixtures/timeline_start_base.mp4 tests/fixtures/timeline_start_shift.ts`: `timeline.dts_monotonic` absent from the non-pass set; video-scope evidence shows `dts_source={"source":"container_pes","container_joined":100,"unjoined_with_pos":0}` matching 05-20-SUMMARY.md's own recorded evidence exactly. The post-execution code review (05-REVIEW.md WR-01) found a latent false-positive risk in the original join (mixed joined/unjoined DTS on one stream, judged as if uniform); fixed same-round (commit `c281bf3`, human chose "fix now") via a stride-aware join, `container_unavailable` on zero-joined streams, and judging only joined packets. `ts_192.ts`/`ts_204.ts` (previously 0/50 and 0/39 joined) now join fully (50/50, 39/39), matching `ts_single.ts` — confirmed directly via `compare --json` self-compare. New unit suites `test_timeline_monotonic.cpp` (5/5) and the extended `test_ts_scan.cpp` PES-join cases (10/10 relevant) pass. |
| 5 | jitter/vfr_profile on NTSC MP4→MKV (WINDOWS.md #28) | **Resolved** | Same reproduction as gap 3 above — zero `timeline.jitter`/`timeline.vfr_profile` findings in the non-pass set. `ctest -R "NTSC MP4-to-MKV stream copy declares"` (which asserts this explicitly as "the #28 regression guard") passes. |
| 6 | Memory safety — `sorted_pts_with_span` OOB read (CR-01) / untestable (WR-01) | **Resolved** | `av_sync.cpp`'s `sorted_pts_with_span` now computes `neighbor` as `std::optional<std::size_t>`, populated only when a real neighbour exists; the `SIZE_MAX`-underflow path no longer exists in the source (confirmed by reading the function directly). `detail::sorted_pts_with_span` is exposed in `analyzers.h` and unit-tested for 0/1/2-entry cases (`test_av_sync.cpp`). WR-01's own follow-on review finding (mixed-provenance DTS axis) is the same fix folded into gap 4 above. |

## Known, Human-Waived Residual (WINDOWS.md #32 — evaluated, not a gap)

On the lossless MP4-to-TS remux pairs (`timeline_start_base.mp4` vs `timeline_start_shift.ts`, and vs `timeline_avoffset_unknown.ts`), `timeline.av_drift` and `timeline.av_drift.pattern` still fail (`pattern=irregular`, confirmed by direct reproduction above: candidate `timeline.av_drift` fail, `timeline.av_drift.pattern` fail). Cause (per `05-STEP-DESIGN.md`'s Orchestrator note and WINDOWS.md #32): MPEG-TS declares no per-stream duration, so the checkpoint span uses libavformat's *estimated* TS audio duration, which includes AAC priming/padding samples with no edit list to exclude them. Neither `span:declared` (today's shipped behavior, `irregular`/`42ms` residual) nor `span:observed` (a different false `linear-drift`/`39ms`) removes the residual — it needs a priming/padding-aware span, which requires work out of this phase's scope.

The human explicitly reviewed this trade-off at the 05-21 blocking-human checkpoint (offered alongside the `step`-vocabulary decision) and chose to keep `span:declared`, accepting the residual with a filed follow-up (WINDOWS.md #32, `status: waived`, reason recorded verbatim). This is **not silently absorbed**: `tests/integration/test_timeline_start_duration.cpp`'s own declared-set test pins `timeline.av_drift`/`timeline.av_drift.pattern` as an *expected* fail on this exact pair, with a causal comment — a future regression (a different magnitude, or a spurious pass) would be caught. Judgment: this residual does not block the phase goal. It is scoped to exactly one check (`av_drift`) on exactly one narrow container-pairing class (MP4→TS with no edit list), was investigated in depth (05-21's calibrated research evaluated two alternative checkpoint-mapping designs before concluding the span-source trade-off is unavoidable without decode-level priming/padding data), is transparently tracked with a real follow-up, and was a fully-informed human decision — not an overlooked defect.

A second, smaller item on the same pairs: `size.stream_bitrate` (audio) reports `warn` (`byte_total` 34890→36108 bytes, +3.49%, against a 3% warn threshold). The declared-set test's own comment attributes this to "MPEG-TS's own PES/PSI overhead"; per this task's re-verification instructions the more precise cause is ADTS framing (TS-muxed AAC carries a 7-byte ADTS header per frame that MP4's `stsd`-based framing does not). The verdict itself (`warn`, a real 3.49% byte-size increase) is correct and not a false positive — only the test comment's causal attribution is imprecise. Non-blocking; a follow-up to correct the comment text is appropriate but not required for this phase's goal.

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria, as amended, verified literally)

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---|---|---|
| SC1 | (amended 2026-09-18, UD-1) 0.1% drift → `linear-drift`; a spliced 100ms trim with a timestamp discontinuity → non-pass `irregular` with residual max; a seamlessly re-timestamped trim documented as undetectable from timestamps alone until Phase 6; a pure offset → `constant-offset`; each fixture fires exactly the intended finding and nothing else; K=32 trajectory stored in fingerprint | ✓ VERIFIED | `ctest -R "ROADMAP SC1"` passes (constant-offset/linear-drift/irregular, each with its complete declared set, `step_time_ms` absent from evidence). `docs/checks/timeline.av_drift.pattern.md` documents the seamless-trim-undetectable limitation and the dropout-vs-sync-step ambiguity (A1). `ctest -R "TIME-08"` passes (K=32 trajectory round-trips a snapshot unchanged). |
| SC2 | `timeline.start`, the duration triple, `dts_monotonic`, `pts_unique`, `gaps`, `discontinuities` all work on presentation timelines; `AV_NOPTS_VALUE` a first-class `absent`; MPEG-TS 33-bit wraparound unwrapped, not mistaken for a backward discontinuity | ✓ VERIFIED | Wrap pair (`timeline_ts_nowrap.ts` vs `timeline_ts_wrap.ts`) reproduces the declared set exactly (see gap 2 above); mid-file self-wrap test (`ctest -R "genuine mid-file"`) passes (zero false gaps/dts_monotonic violations). MP4→TS `dts_monotonic` no longer a false tie (see gap 4 above), further hardened by the stride-aware join fix. Full suite (1007/1007) includes unaffected `pts_unique`/`gaps`/`discontinuities` coverage, all passing. |
| SC3 | VFR stream classified VFR, jitter `skipped:vfr`; CFR stream reports jitter σ and max deviation; shared interval statistics with `video.frame_rate.measured` | ✓ VERIFIED | `compare --profile sw-encoder timeline_start_base.mp4 timeline_vfr.mp4`: video `skipped:vfr`, audio jitter pass 0ms — unchanged from initial verification, no regression from gap-closure work (full suite green). |
| SC4 | `timeline.av_offset` reports a signed, priming-adjusted offset; on non-zero-priming fixtures with unknown priming the finding carries `priming: unknown` with an unadjusted value | ✓ VERIFIED | NTSC MP4→MKV pair (non-44.1kHz-matching timebase) now reports zero `timeline.*` non-pass findings — the units bug that previously misread 1024 samples as 1024ms is fixed (see gap 3 above). Priming-unknown pair (`timeline_avoffset_unknown.ts` vs `timeline_avoffset_video_shift.mp4`) unchanged: `priming.state=unknown`, `comparison_basis=raw`. |
| SC5 | `timeline.timecode` reports presence, SMPTE start value, drop-frame flag; metadata+timeline analysis of the 10-min 1080p reference completes ≤3s (recorded, not asserted); measured in CI with regression tracking via committed instruction-count baseline ratchet | ✓ VERIFIED | `timeline.timecode.value` renders `00:00:10;00` vs `00:00:10:00` correctly (`ctest -R timeline_timecode`, 4/4 pass; no gap-closure plan touched `timecode.cpp`, no regression). Local wall-clock: `full=33532us` (33.5ms), comfortably under the 3s budget, recorded not asserted (D-13). Perf ratchet independently re-confirmed via `gh run view 35391084761` (fetched and grepped directly, not relayed): `metric 'plain_instructions' within tolerance -- baseline=257709408, measured=257624066`, `metric 'full_instructions' within tolerance -- baseline=344956981, measured=345832718`, both well inside ±2%. |

**Score:** 5/5 success criteria verified. behavior_unverified: 0 — every truth was directly exercised against the real, freshly-built binary or a directly-fetched CI log, not inferred from presence/wiring alone.

### Pending CI Confirmation — Important Caveat (not a gap)

The designated `x64-linux` CI leg is confirmed green on head `d40c040` (runs `35389474602` and `35391084761`, both independently re-fetched via `gh run view` for this re-verification) — the state of every plan through 05-25. The subsequent code-review round (commits `486ea4a`, `ae28732`, `c281bf3`, `9216e24`, ending at current HEAD `9216e24`) is **not yet pushed** (`git ls-remote` confirms the remote branch `gsd/phase-05-timeline-analysis` is still at `d40c040`), so CI has not independently run on `c281bf3`'s WR-01 fix or the docs commits after it. Locally, this verification rebuilt from clean cache, ran the full suite (1007/1007) and the designated-leg goldens (15/15) faithfully — per the established local/CI-parity precedent (WINDOWS.md #25, fixtures regenerated with `TZ=UTC taskset -c 0-3`, digest asserted) — and independently re-derived the PES-join/dts_monotonic evidence the fix claims. `c281bf3` touches only MPEG-TS code paths (`ts_scan.*`, `orchestrator.cpp`, `monotonic.cpp`, `packet_scan.h`, `analyzers.h`) — the PERF-01/03/05 reference input is a plain MP4, so this change cannot plausibly move the instruction-count ratchet. This is a process caveat for whoever runs `/gsd-ship` next (push and confirm the designated leg on the final head before merging), not a functional gap in this phase's goal achievement.

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/analyzers/timeline/monotonic.cpp` | dts_monotonic / pts_unique / wrap_events, unwrap-aware, joined-only judgment on TS | ✓ VERIFIED | `detail::filter_axis_to_joined` restricts judgment to PES-header-joined packets on TS; unit-tested (5/5). |
| `src/analyzers/timeline/discontinuities.cpp` | gaps / discontinuities, unwrap-aware | ✓ VERIFIED | Unaffected by gap-closure, full suite green. |
| `src/analyzers/timeline/start_duration.cpp` | timeline.start / duration(.coherence), unwrap-aware | ✓ VERIFIED | Consumes `TimelinePacketView`; wrap pair reproduces the declared set exactly. |
| `src/analyzers/timeline/jitter_vfr.cpp` | jitter / vfr_profile, unwrap-aware, quantization-aware bins | ✓ VERIFIED | NTSC MP4→MKV pair: zero jitter/vfr_profile non-pass findings. |
| `src/analyzers/timeline/av_sync.cpp` | av_offset / av_drift(.pattern), priming-adjusted, unwrap-aware, memory-safe, narrowed vocabulary | ✓ VERIFIED | Priming units bug fixed; `sorted_pts_with_span` uses `std::optional` neighbor (no OOB); `DriftPattern::step` removed. |
| `src/probe/ts_scan.cpp`/`.h` | PES-header PTS/DTS seam, stride-aware container-DTS join | ✓ VERIFIED | `detail::apply_container_dts` joins on `record.offset == packet.pos + (ts_packet_size - 188)`; `ts_192.ts`/`ts_204.ts` now join fully; unit-tested (18/18 relevant). |
| `tests/golden/PERF_BASELINE.txt` | committed instruction-count ratchet | ✓ VERIFIED | Matches CI run `35391084761` exactly (independently re-fetched). |
| `docs/checks/timeline.av_drift.pattern.md`, `.av_drift.md`, `.dts_monotonic.md`, others | SC1/Gap4 contract docs | ✓ VERIFIED | Narrowed vocabulary and dropout-vs-step ambiguity documented; dts_monotonic's "never judges that inferred value" now unconditionally true. |
| `.planning/WINDOWS.md` #26/#27/#28/#30/#31 | closed | ✓ VERIFIED | All five `status: fixed`, `resolved_at` populated; #32 `status: waived` with recorded reason. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `start_duration.cpp`/`stream_params.cpp`/`size.cpp`/`jitter_vfr.cpp`/`av_sync.cpp` | `unwrap.cpp`'s `TimelinePacketView` | shared unwrap primitive | ✓ WIRED | All five now consume the promoted view; wrap pair proves no corruption on any of them. |
| `orchestrator.cpp` post-pass | `ts_scan.cpp`'s `apply_container_dts` | stride-aware PES-header join | ✓ WIRED | `resolve_dts_source` wired to set `container_unavailable` on zero-joined streams; `joined_mask` threaded to `StreamPacketScan::dts_joined`. |
| `monotonic.cpp`'s `emit_dts_monotonic` | `analyzers.h`'s `detail::filter_axis_to_joined` | judged-set filter | ✓ WIRED | Applied exactly when `is_ts && dts_source == container_pes`; excluded count reported in evidence. |
| `av_sync.cpp` priming path | `resolve_priming`'s sample count | sample-rate rescale | ✓ WIRED | NTSC MKV pair confirms correct conversion (zero timeline.* non-pass findings). |

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|---|---|---|---|
| TIME-01 | 05-01, 05-02, 05-04, 05-05 | ✓ SATISFIED | Unchanged from initial verification, unaffected by gap-closure regressions. |
| TIME-02 | 05-02, 05-06, 05-07, 05-16, 05-17, 05-18 | ✓ SATISFIED | Wraparound now unwrapped across every consumer (gap 2 resolved). |
| TIME-03 | 05-01, 05-04, 05-16, 05-17 | ✓ SATISFIED | timeline.start/duration correct on wrap pair. |
| TIME-04 | 05-05, 05-06, 05-07, 05-15, 05-20 | ✓ SATISFIED | dts_monotonic/pts_unique/gaps/discontinuities all unwrap-aware; MPEG-TS inferred-tie false positive fixed and hardened. |
| TIME-05 | 05-03, 05-08, 05-19 | ✓ SATISFIED | Jitter/vfr_profile quantization-aware; NTSC pair clean. |
| TIME-06 | 05-09, 05-14 | ✓ SATISFIED | Priming units bug fixed. |
| TIME-07 | 05-10, 05-21, 05-22, 05-23 | ✓ SATISFIED | `step` withdrawn (amended, UD-1), narrowed vocabulary implemented and documented. |
| TIME-08 | 05-10 | ✓ SATISFIED | K=32 trajectory round-trip verified. |
| TIME-09 | 05-09, 05-14 | ✓ SATISFIED | Same priming fix corrects the adjusted path. |
| TIME-10 | 05-09 | ✓ SATISFIED | Non-zero-priming path fixture-covered; the fixture that previously masked the units bug (MKV) is now the regression guard proving it's fixed. |
| TIME-11 | 05-11 | ✓ SATISFIED | No gap-closure plan touched `timecode.cpp`; presence/SMPTE/drop-frame all re-confirmed with no regression. REQUIREMENTS.md's table still shows "Gaps Found" for this row — a bookkeeping lag from the blanket revert at gap-closure start, not a real defect; recommend updating to Complete. |
| DOC-04 | all plans | ✓ SATISFIED | No-others discipline holds on every pair including the previously-excluded wrap pair (now a full `expect_declared_set` assertion). |
| PERF-01 | 05-12 | ✓ SATISFIED | ≤3s budget met (33.5ms measured locally), recorded not asserted. REQUIREMENTS.md's table still shows "Gaps Found" — same bookkeeping lag as TIME-11; recommend updating to Complete. |
| PERF-03 | 05-12 | ✓ SATISFIED | Ratchet passed on the designated leg (independently re-confirmed via `gh run view`); amended text (D-13/D-14) accurately reflects the ratchet-not-absolute-ratio contract. Same REQUIREMENTS.md bookkeeping lag as above. |
| PERF-05 | 05-12, 05-13, 05-24, 05-25 | ✓ SATISFIED | Designated-leg CI confirmed green twice post-gap-closure, independently re-verified this round via direct log fetch. |

No orphaned requirements: every ID in the phase's requirement list is claimed by at least one plan's frontmatter, and every ID above has direct evidence.

**Bookkeeping note (non-blocking):** `.planning/REQUIREMENTS.md`'s traceability table still marks TIME-11, PERF-01 and PERF-03 as "Gaps Found" (a blanket revert applied at the start of gap-closure, per commit `ec852ce`, that other rows were individually flipped back to "Complete" as their specific gap-closure plans landed, but no plan specifically re-touched these three). This verification finds no functional gap in any of the three — recommend a documentation-only follow-up to flip these three rows to "Complete" the same way DOC-04/PERF-05/TIME-01..10 already were.

### Anti-Patterns Found

None. No `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` markers in any file touched between the initial review baseline (`381c243`) and current HEAD (`9216e24`), across `src/`.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| linear-drift classification | `compare --profile sw-encoder timeline_drift_base.mp4 timeline_drift_linear.mp4` | `av_drift.pattern` = `linear-drift` | ✓ PASS |
| constant-offset classification | `compare --profile sw-encoder timeline_start_base.mp4 timeline_avoffset_video_shift.mp4` | `av_drift.pattern` = `constant-offset` | ✓ PASS |
| spliced-trim classification (SC1, amended) | `compare --profile sw-encoder timeline_start_base.mp4 timeline_drift_step.mp4` | `av_drift.pattern` = `irregular`, `step_time_ms` absent from evidence | ✓ PASS |
| TS-wrap unwrap (SC2) | `compare --profile remux timeline_ts_nowrap.ts timeline_ts_wrap.ts` | Declared set exactly matched (genuine offset + wrap_events x2 + pre-existing duration.coherence); no corruption | ✓ PASS |
| VFR/CFR jitter classification (SC3) | `compare --profile sw-encoder timeline_start_base.mp4 timeline_vfr.mp4` | video `skipped:vfr`, audio jitter=0 pass | ✓ PASS |
| priming-unknown av_offset (SC4a) | `compare --profile remux timeline_avoffset_unknown.ts timeline_avoffset_video_shift.mp4` | baseline priming.state=unknown, basis=raw | ✓ PASS |
| priming-adjusted av_offset correctness (SC4b, Gap 3) | `compare --profile remux timeline_ntsc_base.mp4 timeline_ntsc_remux.mkv` | zero timeline.* non-pass findings | ✓ PASS |
| drop-frame timecode (SC5/TIME-11) | `compare --profile sw-encoder timeline_tc_df.mp4 timeline_tc_ndf.mp4` | `00:00:10;00` vs `00:00:10:00` | ✓ PASS |
| dts_monotonic on MP4→TS (Gap 4) | `compare --profile remux timeline_start_base.mp4 timeline_start_shift.ts` | dts_monotonic absent from non-pass set; dts_source=container_pes, container_joined=100 | ✓ PASS |
| stride-aware join on 192/204-byte TS (WR-01 fix) | `compare --profile remux ts_192.ts ts_192.ts` (self-compare) | dts_monotonic pass both streams; container_joined=50/39 matching ts_single.ts | ✓ PASS |
| TIME-08 trajectory snapshot fidelity | `ctest -R "TIME-08"` | 1/1 passed | ✓ PASS |
| Mid-file wrap self-comparison (doc 04 §5) | `ctest -R "genuine mid-file"` | 1/1 passed | ✓ PASS |
| WR-01 mixed-axis false-positive proof | `ctest -R "timeline_monotonic"` | 5/5 passed | ✓ PASS |
| Designated-leg goldens, local | `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux -R golden` | 15/15 passed | ✓ PASS |
| Full local suite | `ctest --preset x64-linux --output-on-failure` | 1007/1007 passed, 6 pre-existing skips | ✓ PASS |
| Corpus digest | `bash scripts/assert_corpus_digest.sh` | 162 lines compared, 3 documented exclusions | ✓ PASS |
| Perf ratchet, designated CI leg (independently re-fetched) | `gh run view 35391084761 --job 105749349968 --log` | `plain_instructions` and `full_instructions` both within ±2% tolerance | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` files exist in this repository. SKIPPED (no runnable probes declared or conventionally located).

### Human Verification Required

None. Every observable truth was directly exercised against the real, freshly-built binary or a directly-fetched CI log.

### Gaps Summary

None. All six previously-identified gaps (SC1's unreachable `step`, SC2's TS-wrap corruption across five analyzers, SC4's priming units bug, the dts_monotonic MPEG-TS read-back inference, the NTSC jitter/vfr_profile false positive, and the CR-01/WR-01 memory-safety/testability defects) are resolved and independently re-verified against the real binary and, where relevant, an independently-refetched CI log. One residual is knowingly carried forward with explicit, documented human sign-off (WINDOWS.md #32 — MP4-to-TS `av_drift` false positive from unavailable priming/padding-aware span, filed as a follow-up) and does not block this phase's goal for the reasons given above. A non-blocking documentation lag (REQUIREMENTS.md still shows "Gaps Found" for TIME-11/PERF-01/PERF-03 despite no functional gap) and a non-blocking test-comment attribution inaccuracy (`size.stream_bitrate`'s PES/PSI-vs-ADTS-framing cause) are recorded for optional follow-up. The designated CI leg has not yet run on the final code-review-fix commit (`c281bf3`) because it has not been pushed — flagged as a process step for whoever ships this phase next, not a functional gap; local evidence (full suite + designated-leg goldens, both reproduced faithfully in this session) covers the same code.

---

*Verified: 2026-09-19*
*Verifier: Claude (gsd-verifier)*
