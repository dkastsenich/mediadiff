---
phase: 05-timeline-analysis
reviewed: 2026-09-18T00:00:00Z
depth: standard
files_reviewed: 26
files_reviewed_list:
  - src/analyzers/size/size.cpp
  - src/analyzers/timeline/analyzers.h
  - src/analyzers/timeline/av_sync.cpp
  - src/analyzers/timeline/jitter_vfr.cpp
  - src/analyzers/timeline/monotonic.cpp
  - src/analyzers/timeline/start_duration.cpp
  - src/analyzers/timeline/unwrap.cpp
  - src/analyzers/timeline/unwrap.h
  - src/analyzers/video/stream_params.cpp
  - src/core/checks.def
  - src/probe/demux_session.cpp
  - src/probe/demux_session.h
  - src/probe/orchestrator.cpp
  - src/probe/packet_scan.h
  - src/probe/ts_scan.cpp
  - src/probe/ts_scan.h
  - tests/integration/test_timeline_av_sync.cpp
  - tests/integration/test_timeline_jitter.cpp
  - tests/integration/test_timeline_start_duration.cpp
  - tests/integration/test_timeline_structure.cpp
  - tests/unit/test_av_drift.cpp
  - tests/unit/test_av_sync.cpp
  - tests/unit/test_demux_session.cpp
  - tests/unit/test_jitter_vfr.cpp
  - tests/unit/test_pass_union.cpp
  - tests/unit/test_timeline_unwrap.cpp
  - tests/unit/test_ts_scan.cpp
findings:
  critical: 0
  warning: 1
  info: 1
  total: 2
status: issues_found
---

# Phase 5: Code Review Report (Gap-Closure, plans 05-14 through 05-25)

**Reviewed:** 2026-09-18
**Depth:** standard
**Files Reviewed:** 26
**Status:** issues_found

## Summary

This review covers the gap-closure changes (05-14 through 05-25) layered on top of the code already reviewed at commit `381c243`. The diff is large but disciplined: it (1) fixes the two prior-review findings, (2) promotes `TimelinePacketView` as the single unwrap path for every timestamp-derived consumer (av_sync, jitter_vfr, monotonic, start_duration, stream_params, size), (3) adds an overflow-corrected second-open re-probe for MPEG-TS declared durations, (4) adds a bounds-checked PES-header parser and a container-DTS join that substitutes libavformat's own DTS read-back inference with PES-header truth on MPEG-TS, and (5) narrows `timeline.av_drift.pattern`'s vocabulary by removing the unreachable `step` classification.

I traced every changed arithmetic path (unwrap, epoch shift, priming-to-ticks conversion, PES timestamp decode, container-DTS join, least-squares drift fit) for overflow safety, bounds safety on untrusted container bytes, and determinism (no floating point, no iteration-order-dependent state). All checked-arithmetic guards are present and consistent with the project's `expected<T,Error>`/rational-everywhere conventions. Every new code path I could find is covered by a corresponding unit or integration test, several of which are pinned against real fixture measurements rather than implementation-derived expectations, which is the right discipline for a project whose stated P0 is false positives.

I found no BLOCKER-class defects. Two items are worth recording: a WARNING about a latent false-positive risk in the new container-DTS join when a stream's PES timestamps only partially join (mixed-source DTS axis), and an INFO item about `reprobe_ts_declared_durations`'s use of `DemuxOptions{}` (default wall-clock budget) rather than the caller's own configured budget, which could surprise a caller running with a very small `--timeout`.

## Prior Review Findings

- **CR-01** (critical: `sorted_pts_with_span` computed `neighbor = i - 1` on a one-entry stream, underflowing `std::size_t` to `SIZE_MAX` and reading 16 bytes before `entries.data()`): **RESOLVED.** `src/analyzers/timeline/av_sync.cpp` (`detail::sorted_pts_with_span`, ~line 204) now uses `std::optional<std::size_t> neighbor`, populated only when `i + 1 < entries.size()` or `i > 0`, and falls back to `effective_duration = 0` when no neighbour exists. Verified by reading the current implementation directly; the unsigned-underflow path no longer exists in the source.
- **WR-01** (warning: `sorted_pts_with_span` was anonymous-namespace-only and unreachable from unit tests): **RESOLVED.** `PtsSpan` and `detail::sorted_pts_with_span` were moved to `src/analyzers/timeline/analyzers.h`'s `detail` namespace (05-14-PLAN.md Gap 6), and `tests/unit/test_av_sync.cpp` now drives it directly, including the exact CR-01 regression case ("sorted_pts_with_span over 1 undeclared entry (duration 0) never reads out of bounds") plus 0-entry, 1-declared-entry, and 1-sentinel-entry edge cases.

## Warnings

### WR-01: Mixed joined/unjoined container-DTS substitution on one stream is not modeled as a distinct evidence/skip state

**File:** `src/probe/ts_scan.cpp:373-396` (`detail::apply_container_dts`), `src/probe/orchestrator.cpp:353-405` (container-DTS post-pass), `src/analyzers/timeline/monotonic.cpp:265-269` (`emit_dts_monotonic`)

**Issue:** The container-DTS post-pass substitutes a packet's `dts` with the PES-header-derived value only when `packet.pos == record.offset && packet.pts == record.pts` (the join predicate in `apply_container_dts`). Any packet on the same stream that does not find a matching record (e.g. a frame the demuxer split out of a multi-frame PES with `pos < 0`, or a PES header this scanner reported `malformed`/`no_timestamps`/`truncated_in_packet` for) keeps its own, unmodified, libavformat-inferred `dts` — while `dts_source` for the whole stream is still reported as `container_pes` (not `container_unavailable`), and `timeline.dts_monotonic` proceeds to compute violations over this **mixed-provenance** DTS axis (part PES-header truth, part libavformat's own read-back inference). The two sources are not guaranteed to agree at the *boundary* between a joined run and an unjoined run — exactly the class of tie the whole feature exists to eliminate (per `05-VERIFICATION.md`'s Gap 4: "a `-c copy` MP4->TS remux ... makes libavformat's `compute_pkt_fields` fabricate a DTS tie that does not exist in the container"). A partially-unjoined stream could reintroduce a spurious `dts[i] <= dts[i-1]` violation at exactly such a boundary, which is a false positive on a `severity = "fail"` check — the class of defect this project's own CLAUDE.md calls P0. `dts_unjoined_with_pos` is surfaced in evidence (so a human reading the JSON can diagnose it after the fact), but nothing in `run()` widens the skip/insufficient_data gate to cover "some but not all packets joined," so the check still reports a hard pass/fail verdict from mixed-provenance data.

**Fix:** Either (a) treat any non-zero `dts_unjoined_with_pos` on a stream as `SkipReason::insufficient_data` for `timeline.dts_monotonic` on that stream (mirroring the existing `container_unavailable` gate, just widened to "fully joined or skip" rather than "attempted or skip"), or (at minimum) add an integration-test fixture that exercises a stream with a genuine mixed join (some PES headers unparsed/excluded, e.g. an audio stream muxed with private-stream framing) to prove no spurious violation is introduced at the joined/unjoined boundary before this ships as unconditionally trusted.

**Orchestrator analysis and fix specification (2026-09-19; the human chose "Fix now" for this finding):**

Measured on HEAD 486ea4a with `compare --profile remux --json <f> <f>`, reading `timeline.dts_monotonic`'s `dts_source` evidence per stream:
- On the 188-byte fixtures (timeline_start_shift.ts, timeline_ts_nowrap.ts, timeline_ts_wrap.ts, ts_single.ts, timeline_dts_backward.ts, timeline_ts_jump.ts), every video packet joins (100/100, or 50/50 on ts_single.ts). On audio, only the PES-start packets join (13, 14 or 39), with `unjoined_with_pos` 0. The remaining AAC frames are split out of multi-frame PES packets with no `pos`, so they keep libavformat's inferred DTS uncounted. The axis is therefore mixed on every TS audio stream, not only in edge cases. Reviewer option (a) does not cover this, because those frames never enter `unjoined_with_pos`.
- On ts_192.ts and ts_204.ts, nothing joins (video 0/50, audio 0/39, every packet `unjoined_with_pos`). Yet `dts_source` is still `container_pes`, and the check judges libavformat's inferred values only. That contradicts docs/checks/timeline.dts_monotonic.md, which says the check "never judges that inferred value on MPEG-TS". The cause is the constant stride offset 05-15 measured and recorded without adapting (05-15-SUMMARY.md): ts_scan's record offset minus libavformat's `PacketRecord::pos` is exactly +4 on ts_192.ts and +16 on ts_204.ts, over all 50 video records each. That is (TS packet size - 188).

Fix to implement (instead of reviewer option (a), which would also skip every M2TS stream):
1. On MPEG-TS, `timeline.dts_monotonic` judges only DTS values that came from a PES header. Its violation count runs over the joined packets of the stream, in decode order. Packets that did not join (split-out frames with no `pos`, and `unjoined_with_pos` packets) are excluded from the count, never judged, and their number is reported in evidence. Non-TS inputs are unchanged.
2. A TS stream on which zero packets joined gets `DtsSource::container_unavailable`, so `timeline.dts_monotonic` skips it with `insufficient_data` through the existing gate, instead of judging inferred values under a `container_pes` label.
3. `detail::apply_container_dts` joins on `record.offset == packet.pos + (ts_packet_size - 188)`, keeping the PTS-equality requirement, where ts_packet_size is the stride ts_scan detected (188, 192 or 204). ts_192.ts and ts_204.ts must then join fully (50/50 video, 39/39 PES-start audio), matching ts_single.ts. The PTS-equality requirement means a wrong offset can never produce a false join; it can only fail to join.
4. The DTS values other consumers read (size.stream_bitrate, size.peak_bitrate, derive_cadence's DTS fallback) keep today's substitution semantics. Only dts_monotonic's judged set changes. Keep the fix minimal.
5. Tests: unit tests for the stride-aware join (192/204 offsets on hand-built records, plus a wrong-offset case that must not join); a unit test proving that a mixed axis cannot produce a boundary violation (a joined packet whose DTS was corrected, followed by split-out frames whose inferred DTS would tie with it: 0 violations, excluded count reported); a zero-joined stream reports `container_unavailable` and skips; and the orchestrated evidence on ts_192.ts and ts_204.ts shows `container_pes` with full joins. Update docs/checks/timeline.dts_monotonic.md so the documented contract matches (it may keep its "never judges that inferred value" sentence only once that is true). Every existing declared set must stay green. Any verdict change on a fixture pair must be explained, and it can only be a removal of an inferred-DTS judgment.
6. Constraints: false positives are P0; integer math only; `--json` byte-identical across runs; never regenerate fixtures; never edit tests/golden/CORPUS_DIGEST.txt or PERF_BASELINE.txt. The three TSDuck-derived unit.ts_scan_golden goldens must stay byte-identical: verify with `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux -R golden` (15 tests), which is faithful locally because the fixtures match the designated leg.

## Info

### IN-01: `reprobe_ts_declared_durations` always uses the default wall-clock budget, not the caller's configured one

**File:** `src/probe/demux_session.cpp:227-238` (`DemuxSession::reprobe_ts_declared_durations`)

**Issue:** The second, overflow-corrected open is opened with `DemuxOptions{}` (default `wall_clock_budget_ms`) rather than the `DemuxOptions` the primary session itself was opened with. This is called out explicitly in the header doc comment as deliberate ("timing out under the same interrupt-budget mechanism as a fresh `DemuxOptions{}`"), so it is not a silent gap, but it does mean a caller who deliberately configures a very small `--timeout`/wall-clock budget to bound worst-case probe latency will still pay up to the *default* budget for this second open on any genuinely-wrapping MPEG-TS file, which could exceed the caller's own latency expectations for the whole `run_probe` call.

**Fix:** Consider threading the primary session's own `wall_clock_budget_ms` (or a fraction of the remaining budget) into `reprobe_ts_declared_durations`, or at minimum note this in the CLI's own `--timeout` documentation so operators aren't surprised by an MPEG-TS-specific latency floor that non-TS or non-wrapping inputs don't pay.

---

_Reviewed: 2026-09-18_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
