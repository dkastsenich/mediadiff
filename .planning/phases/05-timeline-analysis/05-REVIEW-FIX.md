---
phase: 05-timeline-analysis
fixed_at: 2026-09-19T07:49:25Z
review_path: .planning/phases/05-timeline-analysis/05-REVIEW.md
iteration: 1
findings_in_scope: 1
fixed: 1
skipped: 0
status: all_fixed
---

# Phase 5: Code Review Fix Report

**Fixed at:** 2026-09-19T07:49:25Z
**Source review:** .planning/phases/05-timeline-analysis/05-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 1 (WR-01; `fix_scope: critical_warning`, IN-01 out of scope)
- Fixed: 1
- Skipped: 0

## Fixed Issues

### WR-01: Mixed joined/unjoined container-DTS substitution on one stream is not modeled as a distinct evidence/skip state

**Files modified:** `src/probe/ts_scan.h`, `src/probe/ts_scan.cpp`, `src/probe/orchestrator.cpp`, `src/probe/packet_scan.h`, `src/analyzers/timeline/analyzers.h`, `src/analyzers/timeline/monotonic.cpp`, `docs/checks/timeline.dts_monotonic.md`, `tests/unit/test_ts_scan.cpp`, `tests/unit/test_timeline_monotonic.cpp` (new), `tests/unit/CMakeLists.txt`, `tests/integration/test_timeline_structure.cpp`
**Commit:** `c281bf3`

**Applied fix:** Implemented the human-approved orchestrator fix specification (2026-09-19 addendum to WR-01 in 05-REVIEW.md) verbatim, not the reviewer's own option (a) — the specification's own measurement showed option (a) would also skip every M2TS stream, since every TS audio stream is mixed-join by construction (only PES-start packets join; split-out frames never do).

1. **Stride-aware join** (spec point 3): `detail::apply_container_dts` (`src/probe/ts_scan.h`/`.cpp`) now takes the container's own packet stride (`TsScanResult::stride` — 188/192/204) and joins on `record.offset == packet.pos + (ts_packet_size - 188)`, keeping the existing PTS-equality requirement unchanged (a wrong stride can only fail to join, never produce a false one). Default parameter value `188` keeps every pre-existing 2-arg call site and test behaving identically. `ts_192.ts` and `ts_204.ts` — previously 0/50 video and 0/39 audio joined on every packet — now join fully (50/50 video, 39/39 PES-start audio), matching `ts_single.ts` exactly (measured and pinned in a new integration test).
2. **Zero-joined → `container_unavailable`** (spec point 2): a new pure function `detail::resolve_dts_source(ContainerDtsJoin)` (`src/probe/ts_scan.h`) reports `DtsSource::container_unavailable` when `joined == 0`, `container_pes` otherwise. Wired into `src/probe/orchestrator.cpp`'s post-pass, replacing the previous unconditional `dts_source = DtsSource::container_pes`. This is defense-in-depth beyond the stride fix — after fix (1), no fixture in the corpus actually exercises this path with joined==0, since every stream's video packets and every PES-start audio packet join — but it is the contract the spec explicitly asked for and is proven at the unit level.
3. **Judge only joined packets** (spec point 1): `StreamPacketScan` gains a `std::vector<bool> dts_joined` field (`src/probe/packet_scan.h`), populated by the orchestrator from `apply_container_dts`'s new `joined_mask` out-parameter whenever `dts_source == container_pes`. A new pure function `detail::filter_axis_to_joined` (`src/analyzers/timeline/analyzers.h`/`monotonic.cpp`) filters the (already sentinel-excluded, already TS-unwrapped) DTS axis view down to only the packets whose own dts is PES-header truth, in read order, before `count_dts_violations` ever runs. `emit_dts_monotonic` applies this filter exactly when `is_ts && dts_source == DtsSource::container_pes`; non-TS inputs and `container_unavailable` streams (already skipped ahead of this point) are untouched. The excluded count is reported via a new `excluded_from_judgment` evidence key, present only on `container_pes` measurements.
4. **Other DTS-axis consumers unchanged** (spec point 4): `size.stream_bitrate`, `size.peak_bitrate` and `derive_cadence`'s DTS fallback all read `PacketRecord::dts` directly and were not touched — the stride fix improves the *substitution itself* for every consumer equally (a strict correctness improvement, not new scope), but only `timeline.dts_monotonic`'s *judged set* changes.
5. **Tests** (spec point 5): added to `tests/unit/test_ts_scan.cpp` — stride-aware join at 192/204 offsets, a deliberately-wrong-stride case proving it only fails to join (never a false join), `joined_mask` correctness, and `resolve_dts_source`'s two branches. Added a new file `tests/unit/test_timeline_monotonic.cpp` (registered in `tests/unit/CMakeLists.txt`) proving the core false-positive claim directly: a joined packet's corrected dts followed by split-out frames whose inferred dts ties with it produces 2 violations judged unfiltered vs. 0 violations judged through `filter_axis_to_joined`, with the excluded count reported — plus all-joined, zero-joined, sentinel-interaction, and defensive out-of-bounds-index cases. Added an integration test to `tests/integration/test_timeline_structure.cpp` proving `ts_192.ts`/`ts_204.ts` now join every packet `ts_single.ts` joins (counts compared against the reference at runtime, not hand-transcribed, so a future fixture regen can't silently desync the assertion). Updated `docs/checks/timeline.dts_monotonic.md`'s "MPEG-TS decode timestamps (UD-3)" section and `dts_source` evidence contract so "never judges that inferred value on MPEG-TS" is now unconditionally true, and documented the new `excluded_from_judgment` key.
6. **Constraints honored**: integer math only (the stride adjustment is a plain `checked_add`, guarded); `--json` verified byte-identical across two repeated `compare --profile remux --json ts_192.ts ts_192.ts` runs; no fixtures regenerated; `tests/golden/CORPUS_DIGEST.txt`/`PERF_BASELINE.txt` untouched; the three TSDuck-derived `ts_scan_golden` goldens (`ts_single.ts`, `ts_204.ts`, `ts_multiprogram.ts`) verified byte-identical via `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux -R golden` (15/15 passing) — `run_ts_scan` itself was not touched, only the downstream `apply_container_dts` consumer, so these goldens could not have changed regardless.

**Verdict changes:** None. Every one of the 994 baseline tests still passes unchanged (`ctest --preset x64-linux`: 1007/1007 passing after adding 13 new test cases, same 6 pre-existing skips). No existing declared finding set changed status. `ts_192.ts`/`ts_204.ts` had no prior test coverage of their `timeline.dts_monotonic` evidence (only `run_ts_scan`-level golden/stride tests existed for these two fixtures), so there was no pre-existing "pass"/"fail" verdict on them to change — the new integration test establishes their `container_pes`/full-join behavior for the first time, matching the spec's own prediction.

**`dts_source` evidence recorded per stream** (self-compare, `compare --profile remux --json`, measured after the fix):

| Fixture | Scope | `source` | `container_joined` | `unjoined_with_pos` | `excluded_from_judgment` |
|---|---|---|---|---|---|
| `ts_192.ts` | video | `container_pes` | 50 | 0 | 0 |
| `ts_192.ts` | audio | `container_pes` | 39 | 0 | 38 |
| `ts_204.ts` | video | `container_pes` | 50 | 0 | 0 |
| `ts_204.ts` | audio | `container_pes` | 39 | 0 | 38 |
| `ts_single.ts` | video | `container_pes` | 50 | 0 | 0 |
| `ts_single.ts` | audio | `container_pes` | 39 | 0 | 38 |
| `timeline_start_shift.ts` | video | `container_pes` | 100 | 0 | 0 |
| `timeline_start_shift.ts` | audio | `container_pes` | 13 | 0 | 161 |

`ts_192.ts`/`ts_204.ts` now match `ts_single.ts` exactly, per stream — the expectation the orchestrator fix specification named. The audio `excluded_from_judgment` counts (38, 38, 161) are the split-out multi-frame-PES AAC frames that never carry their own PES header to join against; they were always excluded from the *join* (visible pre-fix as the gap between total audio packets and `container_joined`), and are now also excluded from `timeline.dts_monotonic`'s *judgment*, which is the fix's entire point.

## Skipped Issues

None — the sole in-scope finding (WR-01) was fixed. IN-01 (`reprobe_ts_declared_durations`'s default wall-clock budget) is out of scope for this run (`fix_scope: critical_warning`) and untouched.

---

_Fixed: 2026-09-19T07:49:25Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
