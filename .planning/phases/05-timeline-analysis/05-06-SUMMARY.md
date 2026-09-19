---
phase: 05-timeline-analysis
plan: 06
subsystem: timeline-analysis
tags: [ffmpeg, mpegts, mp4, packet-scan, 33-bit-wrap, span-list, state-semantic, doc03, doc04, setts, output_ts_offset]

requires:
  - phase: 05-timeline-analysis
    provides: "05-02's unwrap_ts_timestamps (33-bit TS unwrap), 05-03's derive_cadence (mode_interval_ticks), 05-04's detail::reconstruct_packet_durations, 05-05's timeline_monotonic_analyzer() per-stream loop and push_skip/compute_stream_scopes pattern"
provides:
  - "timeline.gaps: span_list of presentation-order holes where interval > max(2 x nominal, declared_duration + 1 tick), nominal from the one shared derive_cadence primitive, subtitle-excluded"
  - "timeline.wrap_events: state-semantic string per stream (ts_33bit_wrap / no_wrap) over the raw ContainerFamily::ts PTS axis, not_applicable_container on non-TS inputs"
  - "AVFormatContext::correct_ts_overflow=0 in src/probe/demux_session.{h,cpp} -- the probe-layer fix that makes this project's own doc-04-section-1.2 unwrap rule reachable on any real MPEG-TS wrap at all (previously libav's own generic heuristic silently pre-corrected every wrap before mediadiff ever saw it)"
  - "Four new fixtures proving both anomalies: timeline_gap.mp4 (setts PTS-only shift), timeline_ts_wrap.ts/timeline_ts_nowrap.ts/timeline_ts_nowrap_copy.ts (direct -output_ts_offset encodes)"
  - "DOC-03 declared_pairs() rows and DOC-04 whole-report/per-check declared-set assertions for both new checks, running total sixty-six -> sixty-eight"
affects: [timeline-analysis, doc03-coverage, doc04-no-others, probe-layer]

actuals:
  tokens: 16055
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "AVFormatContext::correct_ts_overflow=0, set before avformat_open_input in demux_session.cpp -- the same 'the probe layer must see the container's own reality' principle already applied to AVFMT_FLAG_GENPTS, now applied to the one other libav timestamp-repair knob this project needs to turn off by hand so its own doc-04 asymmetric unwrap rule is what runs on a genuine wrap, not libav's generic heuristic."
    - "PTS-only setts shift (DTS untouched) to isolate timeline.gaps from MP4's own stts-derived declared duration, which otherwise self-heals around a combined PTS+DTS shift and cancels the intended gap by exactly one tick."
    - "-output_ts_offset placed AFTER -i (an output-side option) on a fresh direct encode straight to MPEG-TS, not a -c copy remux, to engineer a genuine mid-file 33-bit wrap while avoiding the pre-existing MP4-to-TS tracer pair's own collateral dts[1]==dts[0] artifact."
    - "unwrapped_copy pattern in emit_timeline_gaps(): build a PacketRecord vector with .pts overwritten from the prepared AxisSample view before calling the shared derive_cadence/reconstruct_packet_durations primitives, so a wrapping TS stream's presentation order sorts correctly without a second, re-implemented interval statistic."

key-files:
  created:
    - docs/checks/timeline.gaps.md
    - docs/checks/timeline.wrap_events.md
  modified:
    - src/analyzers/timeline/monotonic.cpp
    - src/analyzers/timeline/analyzers.h
    - src/core/checks.def
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - scripts/gen_corpus.sh
    - tests/fixtures/GENERATOR_MANIFEST.json
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/golden/list_checks_effective.txt
    - tests/integration/test_timeline_structure.cpp
    - tests/integration/test_doc03_coverage.cpp

key-decisions:
  - "correct_ts_overflow=0 (a Rule 1/2 deviation, not in the plan's declared files_modified): libavformat's generic wrap correction (demux.c's update_wrap_reference/wrap_timestamp) runs on every MPEG-TS stream by default and silently pre-corrects a 33-bit wrap before this project's own probe layer ever sees raw values -- meaning unwrap_ts_timestamps' own doc-04-section-1.2 asymmetric rule could never fire on a real wrap without this fix. Confirmed directly against the linked FFmpeg 8.1 source and empirically (ffprobe -correct_ts_overflow 0 shows the wrap; default ffprobe does not). Verified safe via a full 834/834 ctest run before this plan's own new tests existed."
  - "timeline_gap.mp4's setts shift touches PTS only, never DTS: an initial combined PTS+DTS shift built successfully and looked correct under ffprobe, but produced gap_count=0 -- MP4's own stts box computes each sample's declared duration from the actual DTS delta at mux time, so shifting DTS along with PTS makes the muxer's own bookkeeping self-heal around the injected gap and the threshold's declared_duration+1 arm grows to exactly cancel it. Diagnosed via a temporary debug fprintf in emit_timeline_gaps (removed before commit)."
  - "timeline_ts_wrap.ts is a fresh direct encode to MPEG-TS with -output_ts_offset, not the plan's literal -c copy remux of timeline_start_base.mp4: that remux carries the same pre-existing collateral dts[1]==dts[0] tie 05-05-SUMMARY.md already documents as under review, which would have made this fixture prove the wrong thing at the very start of the file instead of at the wrap boundary."
  - "The wrap trigger pair (timeline_ts_nowrap.ts vs timeline_ts_wrap.ts) in test_timeline_structure.cpp is deliberately NOT asserted via expect_declared_set's whole-report count -- see Deviations below. Declaring the corrupted evidence from unrelated, out-of-scope checks as 'expected' would violate the project's own FALSE POSITIVES ARE P0 directive."

patterns-established:
  - "A genuinely-wrapping fixture is a probe-layer stress test for every OTHER check that reads raw TS PTS/DTS, not just the checks a plan is adding -- worth a scan across the whole analyzer surface before declaring a wrap-fixture pair's finding set complete."

requirements-completed: [TIME-02, TIME-04, DOC-04]

coverage:
  - id: D1
    description: "timeline.gaps registered as a span-semantic check: nominal from the shared derive_cadence mode interval, declared_duration from the shared reconstruct_packet_durations helper, both magnitudes via checked_mul/checked_add, subtitle-excluded, unwrapped presentation order on TS inputs"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (838/838 passing)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp (Test 3: gap trigger pair complete declared set)"
        status: pass
    human_judgment: false
  - id: D2
    description: "timeline.wrap_events registered as a state-semantic check flagging ts_33bit_wrap with wrap_count/first_wrap_index/first_wrap_raw/first_wrap_unwrapped evidence; not_applicable_container on non-TS; correct_ts_overflow=0 makes a genuine wrap reachable at all"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp (Test 4: wrap trigger pair's own finding; Test 5: state-semantic clean pair)"
        status: pass
    human_judgment: false
  - id: D3
    description: "A mid-file 33-bit wrap produces zero false timeline.gaps spans and zero false timeline.dts_monotonic violations -- doc 04 section 5's own acceptance criterion, proven directly"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp (Test 6: doc 04 section 5 criterion, timeline_ts_wrap.ts vs itself)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Four fixtures generated and proven by ffprobe read-back transcripts to carry (or not carry) the anomaly named; corpus provenance gate green, no pre-existing digest line rewritten"
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (all 4 clauses OK)"
        status: pass
    human_judgment: false
  - id: D5
    description: "DOC-03 declared_pairs() rows for both new ids and DOC-04 declared-set assertions matching the real binary's output exactly, running total sixty-six -> sixty-eight"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "ctest -R integration\\.doc03_coverage"
        status: pass
    human_judgment: false
  - id: D6
    description: "The correct_ts_overflow=0 fix exposes a real, pre-existing gap in timeline.start/timeline.duration/timeline.duration.coherence/video.frame_rate.measured/size.stream_bitrate (raw un-unwrapped axis reads on any genuinely-wrapping TS file) -- out of this plan's declared scope, requires a human decision on which follow-up plan owns the fix"
    verification: []
    human_judgment: true
    rationale: "This is a cross-cutting architectural gap spanning three analyzer files/groups (timeline, video, size) outside timeline_monotonic_analyzer(). Fixing it correctly requires applying the shared unwrap to each of those checks' own axis reads, which is a real, scoped follow-up plan, not a task this plan's own file list covers. A human should confirm the WINDOWS.md ledger entry (#26) and decide which phase/plan absorbs it."

duration: ~50min
completed: 2026-09-16
status: complete
---

# Phase 5 Plan 6: timeline.gaps and timeline.wrap_events Summary

**Span-list gap detection over the unwrapped presentation timeline and a state-semantic 33-bit MPEG-TS wrap flag, unblocked by a probe-layer fix (`correct_ts_overflow=0`) that makes a genuine wrap visible to mediadiff's own unwrap rule at all.**

## Performance

- **Duration:** ~50 min
- **Completed:** 2026-09-16T21:49:25Z
- **Tasks:** 3
- **Files modified:** 12 (10 modified, 2 created source files, plus 4 gitignored generated fixtures)

## Accomplishments
- `timeline.gaps` and `timeline.wrap_events` implemented in `src/analyzers/timeline/monotonic.cpp`, extending `timeline_monotonic_analyzer()` in place (same translation unit as the other two TIME-04 checks) rather than registering a second `AnalyzerSpec`.
- Discovered and fixed a foundational, previously-invisible bug: `AVFormatContext::correct_ts_overflow` (libav default 1) silently pre-corrects every 33-bit MPEG-TS wrap using its own generic heuristic before mediadiff's probe layer ever sees raw packet timestamps — meaning `unwrap_ts_timestamps`'s own doc-04-section-1.2 asymmetric rule could never fire on any real wrap. Cleared to 0 in `src/probe/demux_session.{h,cpp}`, confirmed safe via a full pre-existing `ctest` run.
- Four fixtures generated and proven via `ffprobe -show_packets` read-back: `timeline_gap.mp4` (one genuine {1960ms,2120ms} gap), `timeline_ts_wrap.ts` (a genuine mid-file 33-bit wrap between video packets 50/51), `timeline_ts_nowrap.ts`/`timeline_ts_nowrap_copy.ts` (byte-identical, unwrapped clean pair).
- Proved doc 04 §5's own acceptance criterion directly: `timeline_ts_wrap.ts` compared against itself reports `timeline.gaps` `gap_count=0` on both streams and `timeline.dts_monotonic` `pass` with 0 violations, while `timeline.wrap_events` correctly reports the state-semantic "both flagged" non-pass finding with reconstructible evidence.
- `tests/integration/test_timeline_structure.cpp` grows from 3 to 7 TEST_CASEs; `test_doc03_coverage.cpp`'s running total advances from sixty-six to sixty-eight.
- Discovered (and documented, not silently absorbed) that this plan's own `correct_ts_overflow` fix exposes a real, pre-existing gap in five OTHER checks across three analyzer files that read raw, un-unwrapped TS PTS/DTS values directly — recorded as WINDOWS.md ledger entry #26, out of this plan's declared scope.

## Task Commits

Each task was committed atomically:

1. **Task 1: The gap and TS-wrap fixtures** - `c2d6dd0` (feat)
2. **Task 2: `timeline.gaps` and `timeline.wrap_events` implementation** - `0582e5e` (feat)
3. **Task 3: DOC-03 rows and DOC-04 declared sets** - `146ae4f` (test)

**Plan metadata:** (this commit)

## Files Created/Modified
- `src/analyzers/timeline/monotonic.cpp` - `emit_timeline_gaps()`, `emit_wrap_events()`, `kGapNominalMultiplier`/`kGapDeclaredDurationSlackTicks` constants, extended per-stream loop
- `src/analyzers/timeline/analyzers.h` - doc comment extended for the two new ids
- `src/core/checks.def` - `timeline.gaps` (span/ms/fail), `timeline.wrap_events` (state/none/info, `flagged_values=["ts_33bit_wrap"]`)
- `docs/checks/timeline.gaps.md`, `docs/checks/timeline.wrap_events.md` - `--explain` docs
- `src/probe/demux_session.h`, `src/probe/demux_session.cpp` - `ctx->correct_ts_overflow = 0` before `avformat_open_input`, with a citation comment
- `scripts/gen_corpus.sh` - four new fixture recipes (`timeline_gap.mp4`, `timeline_ts_wrap.ts`, `timeline_ts_nowrap.ts`, `timeline_ts_nowrap_copy.ts`)
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` - four new fixture hash/name entries, no pre-existing line touched (verified via `lint_corpus_digest_provenance.sh`'s 4 clauses)
- `tests/golden/list_checks_effective.txt` - two new rows (regenerated via `UPDATE_GOLDENS=1`, a renderer golden, not fixture-derived)
- `tests/integration/test_timeline_structure.cpp` - 4 new TEST_CASEs (gap trigger declared set, wrap trigger finding, wrap clean pair, doc-04-§5 criterion), plus `timeline.gaps` added to the pre-existing dts_backward pair's declared set
- `tests/integration/test_doc03_coverage.cpp` - `declared_pairs()` rows for both new ids, running total to sixty-eight

## Decisions Made
See `key-decisions` in frontmatter — most significantly: `correct_ts_overflow=0` as a Rule 1/2 probe-layer fix (needed for TIME-02 to be reachable at all), a PTS-only `setts` shift for the gap fixture (DTS-inclusive shifts self-heal against MP4's own `stts` box), and a fresh direct encode (not a remux) for the wrap fixture.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1/2 - Probe-layer bug, blocking] `AVFormatContext::correct_ts_overflow` left at libav's default (1) silently defeats TIME-02's own unwrap rule**
- **Found during:** Task 2, while investigating why `-output_ts_offset`-crafted wrap fixtures read as already-continuous, pre-corrected timelines via plain `av_read_frame`/default `ffprobe`.
- **Issue:** `libavformat/demux.c`'s generic `update_wrap_reference`/`wrap_timestamp` machinery auto-corrects any stream whose `pts_wrap_bits < 64` (MPEG-TS sets exactly 33) the moment the first reference DTS falls within roughly the last 60s of the wrap cycle — using its own heuristic, not doc 04 §1.2's normative asymmetric rule. Left at its default, `timeline.wrap_events`/TIME-02 could never observe a genuine wrap on any real file: libav would have already silently "fixed" it upstream.
- **Fix:** `ctx->correct_ts_overflow = 0;` set in `src/probe/demux_session.cpp` before `avformat_open_input`, with an extensive citation comment in both `demux_session.h` and `.cpp` explaining the mechanism and why it mirrors the existing `AVFMT_FLAG_GENPTS` paragraph's "the probe layer must see the container's own reality" principle.
- **Files modified:** `src/probe/demux_session.h`, `src/probe/demux_session.cpp`
- **Verification:** Full `ctest --test-dir build/x64-linux` run (834/834, pre-existing baseline) confirmed safe before this plan's own new tests existed; `timeline_ts_wrap.ts` vs itself now correctly reports the wrap via `timeline.wrap_events` with zero false `timeline.gaps`/`timeline.dts_monotonic` findings.
- **Committed in:** `0582e5e` (Task 2 commit)

**2. [Rule 1 - Fixture recipe correction] `timeline_gap.mp4`'s `setts` shift changed from PTS+DTS to PTS-only**
- **Found during:** Task 1/2, debugging why the initial fixture reported `gap_count=0` despite a visible PTS interval jump under `ffprobe`.
- **Issue:** MP4's own `stts` box computes each sample's declared duration from the ACTUAL DTS delta to the next sample at mux time. Shifting DTS along with PTS (the plan's literal "shifted together" framing) makes the muxer's own bookkeeping report packet 49's declared duration as already matching the injected gap, growing the gap rule's `declared_duration + 1 tick` threshold arm to exactly cancel the intended gap by one tick (`interval(2048) !> threshold(2049)`).
- **Fix:** Changed the `setts` filter to shift only PTS (`setts=pts='if(gte(N\,50)\,PTS+3\,PTS)'`), leaving DTS untouched. This keeps the declared duration nominal (512 ticks) while the presentation interval grows to 2048 ticks, correctly triggering `interval(2048) > threshold(max(1024,513)=1024)`.
- **Files modified:** `scripts/gen_corpus.sh`
- **Verification:** `ffprobe` read-back (see below); `mediadiff compare --json` evidence shows exactly one `timeline.gaps` span on the video stream.
- **Committed in:** `c2d6dd0` (Task 1 commit)

**3. [Rule 1 - Fixture recipe correction, plan's own literal instruction structurally impossible] `timeline_ts_wrap.ts` changed from a `-c copy` remux to a fresh direct encode**
- **Found during:** Task 1, after remuxing `timeline_start_base.mp4` per the plan's literal instruction produced a `timeline.dts_monotonic` violation at the START of the file, not at the wrap boundary.
- **Issue:** The pre-existing MP4-to-TS tracer pair carries a genuine, already-documented (05-05-SUMMARY.md, "under review") `dts[1] <= dts[0]` tie at the very start of the video stream — a real structural property of ffmpeg's own MPEG-TS muxer/demuxer round-trip. Remuxing it would make this fixture prove the wrong thing.
- **Fix:** A fresh direct `testsrc2`/`sine`/`mpeg4`/`aac` encode straight to MPEG-TS with `-output_ts_offset 95440.34` (an output-side option, placed after `-i`) applied at encode time. Confirmed via `ffprobe` this avoids the pre-existing tie; the real binary reports `timeline.dts_monotonic`/`timeline.pts_unique` both cleanly `pass` across the wrap.
- **Files modified:** `scripts/gen_corpus.sh`
- **Verification:** `ffprobe` read-back (see below); `timeline_ts_wrap.ts` vs itself: `timeline.dts_monotonic` pass, `timeline.gaps` `gap_count=0` on both streams.
- **Committed in:** `c2d6dd0` (Task 1 commit)

**4. [Rule 1 - Collateral fix to a pre-existing declared set, exposed by the new check] `timeline_start_base.mp4` vs `timeline_dts_backward.ts` now also fires `timeline.gaps` on audio**
- **Found during:** Task 3, full `ctest` run after registering `timeline.gaps`.
- **Issue:** The same two-segment splice discontinuity that already corrupts `timeline.duration`/`.coherence` on this pair (05-05's own fixture) genuinely opens one real hole in the candidate's AUDIO presentation timeline at the splice point (verified via evidence: candidate `gap_count=1`, span `{1678ms,1701ms}`, baseline `gap_count=0`). The VIDEO stream's own `gap_count` stays 0 on both sides.
- **Fix:** Added `"timeline.gaps"` (once) to `tests/integration/test_timeline_structure.cpp`'s dts_backward `TEST_CASE`'s declared set with a full causal-reason comment.
- **Files modified:** `tests/integration/test_timeline_structure.cpp`
- **Verification:** `ctest -R "integration\.timeline_structure"` — 7/7 passing.
- **Committed in:** `146ae4f` (Task 3 commit)

### Discovered, Not Fixed (Out of Scope — see Known Issues below)

**5. [Rule 1 class, deliberately deferred — cross-cutting, out of this plan's declared files] Raw-axis corruption in five unrelated checks on any genuinely-wrapping TS file**

See "Known Issues" section below. Recorded as WINDOWS.md ledger entry #26 rather than silently declared as "expected" in a permanent test assertion.

---

**Total deviations:** 4 auto-fixed (1 blocking probe-layer bug required for TIME-02 to be reachable, 2 fixture-recipe corrections, 1 collateral declared-set fix), 1 discovered-and-documented-but-deliberately-out-of-scope gap.
**Impact on plan:** All four auto-fixes were necessary for correctness — without deviation 1, this plan's own two checks could never observe a real wrap; without 2 and 3, the fixtures would not prove what they claim to prove; deviation 4 declares a real, causally-explained finding rather than hiding it. The discovered-but-not-fixed gap (5) is recorded loudly per the project's own FALSE POSITIVES ARE P0 directive rather than papered over. No scope creep into the three unrelated analyzer files that gap touches.

## Known Issues

**`correct_ts_overflow=0` exposes raw-axis reads in `timeline.start`/`timeline.duration`/`timeline.duration.coherence` (`src/analyzers/timeline/start_duration.cpp`), `video.frame_rate.measured` (`src/analyzers/video/stream_params.cpp`), and `size.stream_bitrate` (`src/analyzers/size/size.cpp`).**

These five checks all read `PacketRecord::pts`/`dts` (or a `derive_cadence` call over raw, un-unwrapped packets) directly, never through the shared `unwrap_ts_timestamps`/`prepare_axis` machinery `timeline.gaps`/`timeline.wrap_events`/`timeline.dts_monotonic`/`timeline.pts_unique` use. Before this plan, this was invisible: `correct_ts_overflow`'s libav default (1) silently pre-corrected every wrap before ANY check ever saw raw values, so these five checks were "accidentally correct" by riding on libav's own generic heuristic. This plan's own fix (required so `timeline.wrap_events`/TIME-02 can observe a genuine wrap at all — see Deviation 1) removes that safety net project-wide, not just for this plan's own two new checks.

Observed impact on `timeline_ts_nowrap.ts` vs `timeline_ts_wrap.ts` (a pair that genuinely differs by ~95440s of `-output_ts_offset`): `timeline.start`, `timeline.duration`, `timeline.duration.coherence`, and `video.frame_rate.measured` all report wildly incorrect values computed from the wrap file's raw (near-zero, post-wrap) axis rather than its true unwrapped values; `size.stream_bitrate` goes further and reports `status: error` (`int64_t` cross-multiplication overflow in its own tolerance comparator, `dts_span_ticks: 8589930992`).

This is a real, pre-existing gap this plan's own fix newly makes reachable on any genuinely-wrapping real-world MPEG-TS file. It is NOT fixed here — the affected code lives in three analyzer files/groups (`timeline`, `video`, `size`) entirely outside `timeline_monotonic_analyzer()`, well beyond this plan's declared `files_modified`. Recorded as **WINDOWS.md ledger entry #26** (`kind: deviation`, `phase: 05`, `status: open`). `tests/integration/test_timeline_structure.cpp`'s own wrap-trigger-pair `TEST_CASE` deliberately asserts only `timeline.wrap_events`'s own finding rather than a whole-report `expect_declared_set`, to avoid baking this corrupted evidence into a permanent "expected" declaration (FALSE POSITIVES ARE P0). A follow-up plan should extend the shared doc-04-§1.2 unwrap to these five checks' own axis reads.

## Issues Encountered

See Deviations above — the `correct_ts_overflow` investigation and the two fixture-recipe corrections each required substantial empirical debugging (a temporary debug `fprintf` in `emit_timeline_gaps`, removed before commit; direct `ffprobe -correct_ts_overflow 0` comparisons to distinguish genuine on-the-wire bytes from libav's own runtime correction).

## Fixture Read-Back Proof (Task 1 acceptance criteria)

**Generator:** `.ffmpeg-pinned/linux-x86_64/ffmpeg` (FFmpeg 9.0.1, martin-riedl.de build) for all committed fixture bytes. **Read-back tool:** system `ffprobe` (read-only verification only, never writes committed bytes — same documented gap as 05-05-SUMMARY.md).

### `timeline_gap.mp4` — video stream, packets 47-52 (0-indexed), `pts,dts,duration`

```
47  23552,23552,512
48  24064,24064,512
49  24576,24576,512
50  25088,25088,512
51  27136,25600,512   <- PTS jumps 25088->27136 (interval 2048); DTS stays 25600 (interval 512, untouched)
52  27648,26112,512
```

`mediadiff compare tests/fixtures/timeline_start_base.mp4 tests/fixtures/timeline_gap.mp4 --profile remux --json` confirms exactly one `timeline.gaps` span on the video stream: `{start: 1960ms, end: 2120ms}`, evidence `nominal_interval_ticks: 512, gap_count: 1`. Audio stream stays `pass` (untouched by the recipe).

### `timeline_ts_wrap.ts` — video stream, packets 47-52, raw PTS/DTS (`ffprobe -correct_ts_overflow 0`)

```
47  8589922200,8589922200
48  8589925800,8589925800
49  8589929400,8589929400
50  8589933000,8589933000
51  2008,2008            <- raw PTS wraps: 8589933000 (1592 below 2^33) -> 2008 (post-wrap)
52  5608,5608
```

Unwrap check: `(2008 + 2^33) - 8589933000 = 3600` ticks — exactly one frame at 90kHz/25fps, confirming a clean, correctly-reconstructible wrap (no lost or duplicated presentation interval).

### `timeline_ts_nowrap.ts` — same packet window, no offset (confirms no wrap)

```
47  293690,293690
48  297290,297290
49  300890,300890
50  304490,304490
51  308090,308090
52  311690,311690
```

Monotonically increasing throughout — no crossing of `2^33` anywhere in the file.

### `timeline_ts_nowrap_copy.ts` — byte-identical to `timeline_ts_nowrap.ts`

`cmp tests/fixtures/timeline_ts_nowrap.ts tests/fixtures/timeline_ts_nowrap_copy.ts` — no output, confirmed byte-identical.

### `mediadiff compare` proof of doc 04 §5's own acceptance criterion (`timeline_ts_wrap.ts` vs itself)

```json
{"id":"timeline.dts_monotonic","scope":{"kind":"video"},"status":"pass", ...}
{"id":"timeline.dts_monotonic","scope":{"kind":"audio"},"status":"pass", ...}
{"id":"timeline.gaps","scope":{"kind":"video"},"status":"pass","evidence":{"baseline":{"gap_count":0},"candidate":{"gap_count":0}}}
{"id":"timeline.gaps","scope":{"kind":"audio"},"status":"pass","evidence":{"baseline":{"gap_count":0},"candidate":{"gap_count":0}}}
{"id":"timeline.wrap_events","scope":{"kind":"video"},"status":"info","baseline":"ts_33bit_wrap","candidate":"ts_33bit_wrap","message":"both values are flagged","evidence":{"baseline":{"wrap_count":1,"first_wrap_index":50,"first_wrap_raw":2008,"first_wrap_unwrapped":8589936600}}}
{"id":"timeline.wrap_events","scope":{"kind":"audio"},"status":"info","baseline":"ts_33bit_wrap","candidate":"ts_33bit_wrap", ...}
```

Zero false gaps, zero false monotonicity violations, and the wrap itself correctly made visible — doc 04 §5's own acceptance criterion, met on a real file.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Both `timeline.gaps` and `timeline.wrap_events` are registered, documented, tested, and DOC-03/DOC-04 compliant. The full suite is green (838/838, same 6 pre-existing unrelated skips). The `correct_ts_overflow=0` probe-layer fix is a permanent, project-wide correctness improvement (not scoped to this plan's own checks) — but it surfaces a real, documented gap (WINDOWS.md #26) in five other checks that a future plan must close before any workflow relies on those checks' correctness against a genuinely-wrapping real-world MPEG-TS file. The `ffprobe`-pinning gap noted in 05-05-SUMMARY.md remains open and unaffected by this plan.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-16*

## Self-Check: PASSED

All claimed created/modified files verified present on disk (`docs/checks/timeline.gaps.md`, `docs/checks/timeline.wrap_events.md`, `tests/integration/test_timeline_structure.cpp`, `tests/integration/test_doc03_coverage.cpp`, `src/probe/demux_session.cpp`, this SUMMARY). All three task commit hashes (`c2d6dd0`, `0582e5e`, `146ae4f`) verified present in `git log --oneline --all`.
