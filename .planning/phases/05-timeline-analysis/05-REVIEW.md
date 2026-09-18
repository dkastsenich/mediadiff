---
phase: 05-timeline-analysis
reviewed: 2026-09-18T00:00:00Z
depth: standard
files_reviewed: 68
files_reviewed_list:
  - CMakeLists.txt
  - docs/checks/timeline.av_drift.md
  - docs/checks/timeline.av_drift.pattern.md
  - docs/checks/timeline.av_offset.md
  - docs/checks/timeline.discontinuities.flagged.md
  - docs/checks/timeline.discontinuities.md
  - docs/checks/timeline.dts_monotonic.md
  - docs/checks/timeline.duration.coherence.md
  - docs/checks/timeline.duration.md
  - docs/checks/timeline.gaps.md
  - docs/checks/timeline.jitter.md
  - docs/checks/timeline.pts_unique.md
  - docs/checks/timeline.start.md
  - docs/checks/timeline.timecode.md
  - docs/checks/timeline.timecode.value.md
  - docs/checks/timeline.vfr_profile.md
  - docs/checks/timeline.wrap_events.md
  - docs/checks/video.frame_rate.measured.md
  - .github/workflows/ci.yml
  - scripts/gen_corpus.sh
  - scripts/measure_timeline_perf.sh
  - src/analyzers/timeline/analyzers.h
  - src/analyzers/timeline/av_sync.cpp
  - src/analyzers/timeline/discontinuities.cpp
  - src/analyzers/timeline/jitter_vfr.cpp
  - src/analyzers/timeline/monotonic.cpp
  - src/analyzers/timeline/start_duration.cpp
  - src/analyzers/timeline/timecode.cpp
  - src/analyzers/timeline/unwrap.cpp
  - src/analyzers/timeline/unwrap.h
  - src/analyzers/video/stream_params.cpp
  - src/compare/tol.cpp
  - src/core/checks.def
  - src/core/exact_int.h
  - src/core/rational.h
  - src/probe/cadence.cpp
  - src/probe/cadence.h
  - src/probe/demux_session.cpp
  - src/probe/demux_session.h
  - src/probe/orchestrator.cpp
  - src/probe/packet_scan.cpp
  - src/probe/packet_scan.h
  - src/probe/ts_scan.cpp
  - src/probe/ts_scan.h
  - tests/fixtures/GENERATOR_MANIFEST.json
  - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
  - tests/golden/CORPUS_DIGEST.txt
  - tests/golden/list_checks_effective.txt
  - tests/golden/PERF_BASELINE.txt
  - tests/integration/CMakeLists.txt
  - tests/integration/test_doc03_coverage.cpp
  - tests/integration/test_timeline_av_sync.cpp
  - tests/integration/test_timeline_jitter.cpp
  - tests/integration/test_timeline_start_duration.cpp
  - tests/integration/test_timeline_structure.cpp
  - tests/integration/test_timeline_timecode.cpp
  - tests/integration/test_video_yuvj.cpp
  - tests/integration/timeline_findings.h
  - tests/unit/CMakeLists.txt
  - tests/unit/test_av_drift.cpp
  - tests/unit/test_av_sync.cpp
  - tests/unit/test_cadence.cpp
  - tests/unit/test_compare_semantics.cpp
  - tests/unit/test_exact_int.cpp
  - tests/unit/test_jitter_vfr.cpp
  - tests/unit/test_packet_scan.cpp
  - tests/unit/test_rational_wide.cpp
  - tests/unit/test_timecode.cpp
  - tests/unit/test_timeline_start_duration.cpp
  - tests/unit/test_timeline_unwrap.cpp
  - tests/unit/test_tolerance.cpp
  - tests/unit/test_ts_continuity.cpp
  - tests/unit/test_video_stream_params.cpp
  - tools/bench/timeline_overhead.cpp
  - tools/gen_ts_discontinuity.py
findings:
  critical: 1
  warning: 1
  info: 0
  total: 2
status: issues_found
---

# Phase 05: Code Review Report

**Reviewed:** 2026-09-18T00:00:00Z
**Depth:** standard
**Files Reviewed:** 68
**Status:** issues_found

## Summary

Reviewed the full timeline-analysis phase: the seven `timeline.*` analyzer
translation units, their shared header, the probe-layer primitives they
depend on (`cadence`, `demux_session`, `packet_scan`, `ts_scan`,
`orchestrator`), the two core numeric primitives the whole comparator chain
leans on (`core/rational.h`, `core/exact_int.h`), `compare/tol.cpp`,
`checks.def`, build wiring, and the unit/integration test suites for this
phase.

The codebase is unusually disciplined about checked arithmetic, bounded
parsers, and honest degrade-to-`insufficient_data` behavior almost
everywhere — that discipline is what makes the one real defect found here
stand out: `av_sync.cpp`'s `sorted_pts_with_span` helper (feeding
`timeline.av_offset` / `timeline.av_drift` / `timeline.av_drift.pattern`)
contains an unchecked array-index underflow that is reachable from ordinary,
non-malicious media (any video or audio stream whose packet scan captures
exactly one packet with a valid PTS and an undeclared duration). This is a
memory-safety bug (heap buffer underflow / potential crash) in code that
processes untrusted media files as a CI gate, and it directly contradicts
the bounds-checking discipline the rest of this phase follows rigorously
(e.g. `ts_scan.cpp`'s explicit "attacker-controlled length, validated before
any read" comments, `demux_session.cpp`'s `T-4-48` short-payload guards).

**Already tracked** (per review instructions, not re-reported below):
`.planning/WINDOWS.md` #26/#27/#30 (`correct_ts_overflow = 0` raw-axis
reads), #28 (`vfr_profile`/`jitter` NTSC MP4→MKV remux), #29 (tol message
renders the absolute delta labelled `%`), the `dts_monotonic` tie-counting
false positive on an MP4→TS remux (spec-faithful to doc 04), and
`av_drift.pattern`'s unreachable `step` classification.

## Critical Issues

### CR-01: Heap buffer underflow in `sorted_pts_with_span` on a single-packet stream

**File:** `src/analyzers/timeline/av_sync.cpp:200-235` (specifically lines 224-229)
**Issue:**

```cpp
for (std::size_t i = 0; i < entries.size(); ++i) {
  result.pts.push_back(entries[i].first);
  std::int64_t effective_duration = entries[i].second;
  if (effective_duration <= 0) {
    const std::size_t neighbor = (i + 1 < entries.size()) ? i + 1 : i - 1;
    if (i != neighbor) {
      std::int64_t interval = 0;
      const bool ok = (neighbor > i) ? detail::checked_sub(entries[neighbor].first, entries[i].first, &interval)
                                      : detail::checked_sub(entries[i].first, entries[neighbor].first, &interval);
      effective_duration = (ok && interval > 0) ? interval : 0;
    } else {
      effective_duration = 0;
    }
  }
  result.durations.push_back(effective_duration);
}
```

When `entries.size() == 1` (a stream whose packet scan captured exactly one
packet carrying a valid, non-sentinel PTS — a short/truncated clip, a
single-frame image-sequence stream, or simply a stream whose only packet
happens to sit past a truncated `PacketScan` cap), the only iteration is
`i == 0`. `i + 1 < entries.size()` is `1 < 1` → false, so the code falls to
`neighbor = i - 1 = 0 - 1`. `std::size_t` is unsigned, so this underflows to
`SIZE_MAX` rather than throwing or asserting. `i != neighbor` (`0 !=
SIZE_MAX`) is then true, `neighbor > i` is true, and the code calls
`entries[neighbor].first` — i.e. `entries[SIZE_MAX]`. Because
`sizeof(std::pair<int64_t,int64_t>) == 16`, `SIZE_MAX * 16 mod 2^64` wraps
to `-16`, so this indexes 16 bytes *before* `entries.data()` — a genuine
out-of-bounds heap read (CWE-125), not merely a huge, obviously-invalid
address. In a release build this most likely reads adjacent heap
metadata/allocator data as if it were a `PacketRecord`'s PTS, silently
poisoning `effective_duration` (and, downstream, the `timeline.av_offset` /
`timeline.av_drift` / `timeline.av_drift.pattern` measurements) with garbage
rather than crashing — exactly the "confidently wrong number" class of
defect this project's own `checked_*` arithmetic discipline elsewhere exists
to prevent. Under ASan, a hardened allocator, or a debug STL, it is instead
a reliable crash (denial of service against the CI gate on an ordinary,
non-adversarial input).

This function is called unconditionally and unguarded in
`run_timeline_av_sync` for **both** the video stream
(`sorted_pts_with_span(video_stream.packets)`, right after confirming the
video stream has at least one valid PTS but before any `size() >= 2` check)
and the audio stream (`sorted_pts_with_span(audio_stream.packets)`), so
either side of an `timeline.av_offset`/`timeline.av_drift` comparison can
trigger it — this is not a narrow, decode-path-only edge case.

**Fix:**

```cpp
if (effective_duration <= 0) {
  std::optional<std::size_t> neighbor;
  if (i + 1 < entries.size()) {
    neighbor = i + 1;
  } else if (i > 0) {
    neighbor = i - 1;
  }
  if (neighbor.has_value()) {
    std::int64_t interval = 0;
    const bool ok = (*neighbor > i) ? detail::checked_sub(entries[*neighbor].first, entries[i].first, &interval)
                                     : detail::checked_sub(entries[i].first, entries[*neighbor].first, &interval);
    effective_duration = (ok && interval > 0) ? interval : 0;
  } else {
    effective_duration = 0;
  }
}
```

Add a unit-testable regression case (see WR-01 below) with `entries.size()
== 1` and `duration <= 0` to lock this in.

## Warnings

### WR-01: `sorted_pts_with_span` is unreachable from unit tests, unlike every sibling pure helper in this phase

**File:** `src/analyzers/timeline/av_sync.cpp:172-272`
**Issue:** Every other non-trivial pure helper in this phase
(`detail::first_presented_pts`, `detail::global_origin_ticks`,
`detail::reconstruct_packet_durations` in `start_duration.cpp`,
`detail::build_axis_view`/`count_dts_violations`/`count_pts_duplicates` in
`monotonic.cpp`, `detail::compute_sorted_axis_intervals`/`classify_vfr_bin`
in `jitter_vfr.cpp`) is deliberately exposed via the `detail::` namespace in
`analyzers.h` specifically so `tests/unit/` can drive it directly against
hand-built inputs without needing a real fixture on disk — this is stated
explicitly, repeatedly, in this file's own doc comments as the established
convention. `sorted_pts_with_span`, `clamp_into_nearest_packet`,
`nearest_tick`, and `index_proportional_raw_ticks` in `av_sync.cpp` are the
one exception: they live in an anonymous namespace with no `detail::`
exposure, so `tests/unit/test_av_sync.cpp` can only test `resolve_priming`
and `primary_video_stream` from this file, never the checkpoint-construction
math that CR-01 lives in. This gap in the project's own established
testability convention is almost certainly why the single-packet underflow
in CR-01 was never caught — `tests/integration/test_timeline_av_sync.cpp`'s
fixtures are all multi-packet, multi-second clips, so this class of edge
case was structurally unreachable from the test suite regardless of intent.
**Fix:** Move `sorted_pts_with_span` (and, if useful, `clamp_into_nearest_packet`/
`index_proportional_raw_ticks`) into `namespace detail` in `analyzers.h`
(mirroring the existing declarations), and add
`tests/unit/test_av_sync.cpp` cases for a single-entry span, an all-zero-duration
span, and a two-entry span with a zero duration on the last entry — the same
class of hand-built-input coverage this project already gives every sibling
helper.

---

_Reviewed: 2026-09-18T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
