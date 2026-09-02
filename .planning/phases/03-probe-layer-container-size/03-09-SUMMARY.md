---
phase: 03-probe-layer-container-size
plan: 09
subsystem: size-analyzer
tags: [size, packet-scan, rational-arithmetic, checked-integer, determinism, cpp20, catch2]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      src/probe/packet_scan.{h,cpp} (03-03-PLAN.md) -- PacketScanResult,
      the shared per-stream packet array this plan derives its own
      statistic from directly, with no second sweep; core/model.h's
      SkipReason::partial_scan/insufficient_data/no_timing_data and
      Measurement.estimated (03-01-PLAN.md); detail::checked_div
      (03-01-PLAN.md, added specifically for this plan's window-boundary
      math); the two-AnalyzerSpec-per-family pattern proven by
      container/{mp4,mkv,ts}.cpp (this plan needed only one AnalyzerSpec,
      since size.* is family-agnostic); the approved 27-id check roster
      (03-CHECK-ROSTER.md) -- this plan registers the four size.* ids
      exactly as spelled there.
provides:
  - "src/analyzers/size/size.cpp: the four size.* checks -- size.file
    (reads DemuxSession::file_size_bytes(), independent of PacketScan's
    completeness), size.overhead (exact ratio, insufficient_data on a
    payload sum exceeding file size), size.stream_bitrate (exact
    byte_total*8/dts_span ratio, never estimated), size.peak_bitrate
    (1s/100ms sliding window over sorted DTS, pure checked integer
    arithmetic, bounded to 10,000,000 windows) -- all wired through one
    family-agnostic AnalyzerSpec (size_analyzer(), ContainerFamily::other)"
  - "DemuxSession::file_size_bytes() (src/probe/demux_session.{h,cpp}):
    avio_size() on the already-open AVIOContext -- no second file open,
    what lets size.file report independently of D-02's partial_scan guard"
  - "mediadiff::detail::compute_peak_window (src/analyzers/size/analyzers.h):
    the pure, test-only-exposed windowing function tests/unit/
    test_size_windowing.cpp drives directly against hand-built
    PacketRecord arrays"
  - "12 new gen_corpus.sh fixture recipes (SIZE-01) plus tests/golden/
    size_checks_size_crf20.txt, the first size.* cross-platform golden"
affects: [03-10-tsduck-golden-comparison, 03-11-verbose-render]

actuals:
  tokens: 20000
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "A family-agnostic check family (ContainerFamily::other) needs only
      ONE AnalyzerSpec, no family-agnostic 'not applicable' sibling --
      distinct from the three container.<family>.* families, which each
      need a real-data spec plus a not-applicable sibling because their
      own check applies to only one container family."
    - "A file-level property (size.file) that must survive a degraded
      sub-pass (PacketScan truncation) is read through a DIFFERENT probe
      accessor (DemuxSession::file_size_bytes(), avio_size) than the
      degraded pass itself -- the D-02 exemption is structural (a
      different data source), not a conditional bypass."
    - "Sliding-window peak computation over an unsorted, PROBE-10-shared
      packet array: sort a local INDEX view (never copy/reorder the
      shared array itself), compute every window boundary fresh from its
      own index via checked_mul/checked_add (never accumulate), and use
      compare_ticks_checked (never the bare compare_ticks) for every
      window-membership comparison, treating a checked-arithmetic
      overflow as a legitimate DoS-mitigation skip path (T-3-46), not an
      exceptional condition."

key-files:
  created:
    - src/analyzers/size/analyzers.h
    - src/analyzers/size/size.cpp
    - docs/checks/size.file.md
    - docs/checks/size.overhead.md
    - docs/checks/size.stream_bitrate.md
    - docs/checks/size.peak_bitrate.md
    - tests/unit/test_size_windowing.cpp
    - tests/unit/test_size_analyzer.cpp
    - tests/integration/test_size_checks.cpp
    - tests/golden/size_checks_size_crf20.txt
  modified:
    - CMakeLists.txt
    - src/core/checks.def
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/probe/orchestrator.cpp
    - scripts/gen_corpus.sh
    - tests/fixtures/GENERATOR_MANIFEST.json
    - tests/golden/list_checks_effective.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt

key-decisions:
  - "size.file reads DemuxSession::file_size_bytes() (a new accessor, avio_size on the already-open AVIOContext) rather than std::filesystem or a second fopen_utf8 -- respects pass.h's own 'no analyzer opens the input file itself' prohibition, since the session is already open, and needs no UTF-8/wide-path shimming of its own."
  - "size.peak_bitrate's window-count bound (T-3-46) is 10,000,000 windows at the 100ms step, checked via one division BEFORE the sweep starts -- covers over 11 days of continuous content while remaining a real, assertable DoS ceiling for a crafted colossal-dts-gap pair of packets."
  - "The {1001,30000} (NTSC-style) timebase test revealed no rounding DIFFERENCE from a naive form to reconcile -- window_ticks=30000/1001=29 and step_ticks=30000/10010=2 are each computed by ONE truncating integer division; there is no accumulating variant to diverge from in a pure-integer implementation (a genuine +=-drift bug is only reachable with floating-point accumulation, which this path never performs). The static grep gate (`+= step` count == 0) and the dynamic 10,000-window/spike-at-the-far-boundary test together are what actually gate the 'computed fresh from k' architecture, not a numeric-divergence assertion, since none is achievable to construct honestly for correct all-integer arithmetic."
  - "Fixture recipes use -b:v (target-bitrate) deltas, never -crf, on the built-in mpeg4 encoder -- this project's own established gen_corpus.sh convention (stated for the container.mp4.* recipes: never libx264/GPL, even for the GENERATING ffmpeg) extended to this plan's 'CRF pair' fixtures, which are named size_crf20.mp4/size_crf23.mp4 per the plan's own acceptance-criteria filenames but are actually -b:v-driven."
  - "A dedicated size_partial.mp4 fixture (25,000 tiny 2x2px frames, not in the plan's own bottom-of-file fixture list) was added for D-02's CLI-level proof: --probe-memory-budget-mb is integer-MB granular (1 MB floor = 1,048,576 bytes), and every other size_*.mp4 fixture needs only a few KB of packet-store accounting, so a 1 MB budget would never actually truncate them -- this fixture crosses that threshold cheaply on packet COUNT alone (content/encoded size is irrelevant to D-01's accounting)."
  - "The size.overhead cross-container (MP4-vs-TS) fixture pair the plan's own Task 1 action text describes was NOT added -- superseded by the plan's own authoritative bottom-of-file 'New fixtures' list, which specifies only the same-container muxrate pair (size_muxrate_a.ts/size_muxrate_b.ts); adding the cross-container pair would only re-exercise CONT-02's demotion path (already covered in 03-06-PLAN.md), not size.overhead's own tolerance."
  - "Task 1 and Task 2 implemented and committed together (one commit) -- size.cpp's push_skip/scope-computation helpers are shared across all four checks with no natural split boundary, mirroring 03-06/03-07/03-08's own recorded precedent for the identical shape. Task 3 (the integration golden) is a separate, second commit."

patterns-established:
  - "detail::compute_peak_window's WindowStatus enum collapses every 'cannot determine' cause (too-short span, unusable timebase, T-3-46 bound violation, any checked-arithmetic overflow) into insufficient_data, distinct only from no_timing_data (no packet has a real DTS at all) -- matching the two SkipReason enumerators core/model.h actually declares for this family; no third 'overflow' SkipReason was invented, since the registered check's own vocabulary has no slot for one."

requirements-completed: [SIZE-01]

coverage:
  - id: D1
    description: "size.peak_bitrate windows on DTS in ticks with rational bounds (1s window, 100ms step), every boundary computed fresh from its own window index k, never accumulated -- two runs on the same input produce byte-identical results."
    requirement: SIZE-01
    verification:
      - kind: unit
        ref: "tests/unit/test_size_windowing.cpp#a hand-built packet array in a {1,1000} timebase produces the human-computed peak"
        status: pass
      - kind: unit
        ref: "tests/unit/test_size_windowing.cpp#the SAME packets in a shuffled read order produce the identical peak"
        status: pass
      - kind: unit
        ref: "tests/unit/test_size_windowing.cpp#two calls over the same array produce byte-identical peaks"
        status: pass
      - kind: integration
        ref: "tests/integration/test_size_checks.cpp#two consecutive inspect --json runs produce byte-identical stdout"
        status: pass
      - kind: other
        ref: "manual: diff of two `mediadiff inspect size_peak_vbv.mp4 --json` runs -- identical"
        status: pass
    human_judgment: false
  - id: D2
    description: "size.peak_bitrate produces the identical value on Linux, macOS and Windows for the same fixture -- verified only by the SAME committed golden comparing clean on all three CI legs."
    requirement: SIZE-01
    verification:
      - kind: integration
        ref: "tests/integration/test_size_checks.cpp#the size.* findings are pinned by a committed, read-only golden (tests/golden/size_checks_size_crf20.txt)"
        status: pass
    human_judgment: true
  - id: D3
    description: "Every window boundary is computed fresh from its own window index k (first_dts + k*step_ticks via checked_mul/checked_add), never by accumulating += step_ticks -- proven at 10,000-window scale with a spike exactly at the far boundary, and enforced statically (zero '+= step' occurrences)."
    requirement: SIZE-01
    verification:
      - kind: unit
        ref: "tests/unit/test_size_windowing.cpp#a spike exactly at the 10,000th window boundary is found, not missed"
        status: pass
      - kind: other
        ref: "grep -vE comment-stripped src/analyzers/size/size.cpp | grep -c '+= step' == 0"
        status: pass
      - kind: other
        ref: "grep -vE comment-stripped src/analyzers/size/size.cpp | grep -c 'compare_ticks(' == 0 (only the checked variant appears)"
        status: pass
      - kind: other
        ref: "grep -vE comment-stripped src/analyzers/size/size.cpp | grep -c 'static_cast<double>|(double)|float|std::round|std::floor' == 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "D-02: size.stream_bitrate/size.peak_bitrate/size.overhead all emit skipped:partial_scan with no value when PacketScanResult::partial is true; size.file still reports normally."
    requirement: SIZE-01
    verification:
      - kind: unit
        ref: "tests/unit/test_size_analyzer.cpp#D-02: a truncated packet scan skips overhead/stream_bitrate/peak_bitrate as partial_scan while size.file still reports a value"
        status: pass
      - kind: other
        ref: "manual: mediadiff inspect size_partial.mp4 --probe-memory-budget-mb 1 --json -- size.file reports a value, the other three are skipped/partial_scan"
        status: pass
      - kind: other
        ref: "manual: mediadiff compare size_partial.mp4 size_partial.mp4 --probe-memory-budget-mb 1 --json -- size.overhead's evidence names probe_memory_cap_bytes=1048576"
        status: pass
    human_judgment: false
  - id: D5
    description: "A stream whose total DTS span is shorter than one window, or whose packets carry no DTS at all, reports skipped:insufficient_data / skipped:no_timing_data respectively -- never a zero-valued measurement; a payload sum exceeding the file's own size also skips insufficient_data rather than a negative overhead."
    requirement: SIZE-01
    verification:
      - kind: unit
        ref: "tests/unit/test_size_windowing.cpp#a total dts span shorter than one window emits insufficient_data"
        status: pass
      - kind: unit
        ref: "tests/unit/test_size_windowing.cpp#a stream where EVERY packet lacks dts emits no_timing_data"
        status: pass
      - kind: unit
        ref: "tests/unit/test_size_analyzer.cpp#size.stream_bitrate skips no_timing_data when every packet on a stream lacks a DTS"
        status: pass
      - kind: unit
        ref: "tests/unit/test_size_analyzer.cpp#size.overhead skips insufficient_data when the payload sum exceeds the file's own size"
        status: pass
      - kind: other
        ref: "manual: mediadiff inspect size_short.mp4 --json -- size.peak_bitrate skipped/insufficient_data on both streams"
        status: pass
    human_judgment: false
  - id: D6
    description: "size.peak_bitrate and size.stream_bitrate are PER-STREAM; size.file and size.overhead are whole-file at global scope; size.file's [check.profile_tolerance] override (0.5% under strict-bitexact/remux) actually resolves."
    requirement: SIZE-01
    verification:
      - kind: unit
        ref: "tests/unit/test_size_analyzer.cpp#size.file equals the file's size on disk at global scope, independent of the sweep"
        status: pass
      - kind: unit
        ref: "tests/unit/test_size_analyzer.cpp#size.stream_bitrate emits an exact RationalValue, deterministic across two runs (video[0] scope)"
        status: pass
      - kind: other
        ref: "manual: mediadiff compare size_crf20.mp4 size_crf23.mp4 --profile sw-encoder --json -- size.file fail; size_near_a/b.mp4 pass under sw-encoder, fail under --profile remux"
        status: pass
      - kind: integration
        ref: "tests/integration/test_size_checks.cpp#a byte-identical pair compares pass on every size.* check under sw-encoder"
        status: pass
    human_judgment: false

duration: 90min
completed: 2026-09-03
status: complete
---

# Phase 3 Plan 9: size.* — Container & Size Rate Economics Summary

**The four `size.*` checks (`src/analyzers/size/size.cpp`) deriving their own statistic directly from `PacketScan`'s shared per-stream array with no second sweep — `size.peak_bitrate`'s 1-second-sliding-window/100ms-step determinism core is pure checked-integer arithmetic throughout, D-02's `partial_scan` guard applies to everything except `size.file` (read through a new `DemuxSession::file_size_bytes()` accessor, structurally independent of the packet sweep), and a committed cross-platform golden pins the whole family's values.**

## Performance

- **Duration:** ~90 min
- **Tasks:** 3/3 completed
- **Files modified:** 10 modified, 10 created (2 commits)

## Accomplishments

- `src/analyzers/size/size.cpp` (SIZE-01): `size.file` (global, reads the
  file's own on-disk byte size independently of the packet sweep's
  completeness — the one `size.*` check that does NOT take the D-02
  guard), `size.overhead` (exact `(file_bytes - payload_bytes)/file_bytes`
  ratio, `skipped:insufficient_data` on a payload exceeding the file's own
  size), `size.stream_bitrate` (per-stream, exact
  `byte_total*8/dts_span_seconds` ratio, never `estimated`),
  `size.peak_bitrate` (per-stream, the determinism core — a 1-second
  sliding window with a 100ms step over each stream's own sorted DTS axis,
  every window boundary computed fresh from its index via
  `checked_mul`/`checked_add`, `compare_ticks_checked` for every
  window-membership comparison, bounded to 10,000,000 windows before
  iterating, T-3-46). All four registered through one family-agnostic
  `AnalyzerSpec` (`ContainerFamily::other`) since every `size.*` check
  applies to every container this project probes — no "not applicable"
  sibling needed, unlike the three `container.<family>.*` pairs.
- `DemuxSession::file_size_bytes()` (`src/probe/demux_session.{h,cpp}`):
  `avio_size()` against the already-open `AVIOContext` — no second file
  open, no dependency on `PacketScan` having run at all, which is what
  structurally makes `size.file`'s D-02 exemption real rather than a
  conditional bypass.
- `mediadiff::detail::compute_peak_window` (`src/analyzers/size/analyzers.h`):
  exposed as a pure, test-only-visible function (mirrors
  `packet_scan.h`'s own `detail::make_packet_record` precedent), so all
  nine windowing determinism behaviors are unit-testable against
  hand-built `PacketRecord` arrays with independently (Python)
  hand-computed expected peaks — no real fixture reliably produces a
  specific read-order shuffle or an overflow-triggering timebase.
- D-02 wired uniformly: `PacketScanResult::partial == true` makes
  `size.overhead`/`size.stream_bitrate`/`size.peak_bitrate` all skip
  `partial_scan` with `probe_memory_cap_bytes`/`accounted_bytes` in
  evidence, while `size.file` reports normally.
- `03-CHECK-ROSTER.md`'s four ids registered in `checks.def` exactly as
  spelled, including `size.file`'s `[check.profile_tolerance]` split
  (`0.5%` under `strict_bitexact`/`remux` vs the `3%,8%` baseline) — both
  independently verified against real fixtures (see Decisions Made).
- 12 new `gen_corpus.sh` fixture recipes plus a 13th
  (`size_partial.mp4`) added beyond the plan's own bottom-of-file list for
  D-02's CLI-level proof (see Decisions Made) — every size delta
  (`size_crf20.mp4`/`size_crf23.mp4`, the near-identical pair, the peak
  and stream-bitrate pairs, the muxrate overhead pair) was calibrated
  empirically against a real system `ffmpeg`, not assumed.
- `tests/golden/size_checks_size_crf20.txt`: the first `size.*`
  cross-platform golden, riding the existing D-12 `UPDATE_GOLDENS`
  discipline, comparing clean on every CI leg (`tests/integration/
  test_size_checks.cpp`).

## Task Commits

Tasks 1-2 were implemented and committed together (see Decisions Made for
why); Task 3 is a separate commit:

1. **Tasks 1-2: size.file/overhead/stream_bitrate/peak_bitrate — rate economics from the shared packet sweep (SIZE-01)** - `57b9794` (feat)
2. **Task 3: cross-platform determinism assertion for the size.* family** - `ebea3c8` (test)

## Files Created/Modified

- `src/analyzers/size/{analyzers.h,size.cpp}` (new) — the four checks, `compute_peak_window`, `size_analyzer()`
- `src/probe/demux_session.{h,cpp}` — `file_size_bytes()` accessor
- `src/probe/orchestrator.cpp` — `size_analyzer()` registered in `all_analyzers()`
- `src/core/checks.def` — four new `[[check]]` entries, matching 03-CHECK-ROSTER.md exactly
- `docs/checks/size.{file,overhead,stream_bitrate,peak_bitrate}.md` (new)
- `CMakeLists.txt` — `size.cpp` and `analyzers.h` added to `libmediadiff`
- `scripts/gen_corpus.sh` — 13 new fixture recipes (SIZE-01)
- `tests/unit/{test_size_windowing.cpp,test_size_analyzer.cpp}` (new)
- `tests/unit/CMakeLists.txt` — registers both new test files
- `tests/integration/test_size_checks.cpp` (new)
- `tests/integration/CMakeLists.txt` — registers the new test file
- `tests/golden/size_checks_size_crf20.txt` (new) — the size.* golden
- `tests/golden/list_checks_effective.txt` — regenerated (D-12 discipline, four new rows)
- `tests/fixtures/GENERATOR_MANIFEST.json` — regenerated (`generated_at` only)

## Decisions Made

See `key-decisions` in the frontmatter for the full list. Highlights:

- **`file_size_bytes()` via `avio_size`, not `std::filesystem`:** respects
  `pass.h`'s own "no analyzer opens the input file itself" prohibition —
  the session `DemuxSession` already has open, and `avio_size` needs no
  UTF-8/wide-path shimming of its own the way a second `fopen_utf8` would.
- **The `{1001, 30000}` timebase test's real finding:** no numeric
  drift is achievable to demonstrate in a correctly-implemented all-integer
  path — `window_ticks`/`step_ticks` are each computed by ONE truncating
  division, so "accumulate `+= step_ticks`" and "multiply `k*step_ticks`"
  are bit-identical for any integer `step_ticks`. The real protection this
  plan's architecture provides is the STATIC grep gate (`+= step` count ==
  0) plus the DYNAMIC 10,000-window/far-boundary-spike test, not a
  numeric-divergence assertion that cannot honestly be constructed for
  pure integer arithmetic.
- **`size_crf20.mp4`/`size_crf23.mp4` are `-b:v`-driven, never `-crf`:**
  this project's `gen_corpus.sh` already states its own convention (never
  `libx264`/GPL, even for the generating system `ffmpeg`) for the
  `container.mp4.*` recipes; extended identically here. Filenames match
  the plan's own acceptance-criteria literal wording; the actual mechanism
  is a target-bitrate delta on the built-in `mpeg4` encoder.
- **`size_partial.mp4` added beyond the plan's own fixture list:**
  `--probe-memory-budget-mb` is integer-MB granular (1 MB floor =
  1,048,576 bytes); every other `size_*.mp4` fixture needs only a few KB
  of packet-store accounting, so a 1 MB budget would never truncate them.
  This fixture (25,000 tiny 2x2px frames, sub-second to encode) crosses
  that threshold on packet COUNT alone — content size is irrelevant to
  D-01's accounting.
- **The plan's own MP4-vs-TS cross-container overhead pair was NOT
  added:** superseded by the plan's own authoritative bottom-of-file "New
  fixtures" list (which specifies only the same-container muxrate pair);
  a cross-container pair would only re-exercise CONT-02's demotion path
  (already covered in 03-06-PLAN.md), not `size.overhead`'s own tolerance.
- **Tasks 1-2 committed together:** `size.cpp`'s helper functions
  (`push_skip`, `scope_kind_for_stream`, `compute_stream_scopes`) are
  shared across all four checks with no natural split boundary — mirrors
  03-06/03-07/03-08's own recorded precedent for the identical shape.

## Deviations from Plan

### Auto-fixed Issues

None — the full build compiled cleanly on the first pass with no
warnings, and every acceptance criterion (grep-based and CLI-based) was
verified as written, requiring no bug fixes.

### Claude's Discretion (not a deviation, documented per this plan's own instructions)

- Fixture generation mechanism (`-b:v` vs `-crf`) and the additional
  `size_partial.mp4`/dropped cross-container-overhead fixture, both
  covered above.
- Task 1/Task 2 committed together — covered above.

**Total deviations:** 0 auto-fixed. All adjustments above are documented
discretion, not corrections to broken behavior.

## Issues Encountered

- **`--probe-memory-budget-mb`'s integer-MB granularity** made the plan's
  literal "a tiny --probe-memory-budget-mb" acceptance criterion
  impossible to satisfy against any of the other `size_*.mp4` fixtures (all
  need only a few KB of packet-store accounting; the flag's floor is 1 MB
  = 1,048,576 bytes). Resolved by adding `size_partial.mp4`, a
  cheap-to-generate, packet-COUNT-heavy fixture built specifically to
  cross that threshold — see Decisions Made.

## User Setup Required

None — no external service configuration required (the `MEDIADIFF_FFMPEG`/
system-`ffmpeg`-≥6.1 precondition was already satisfied in this
environment; `scripts/gen_corpus.sh` ran successfully, including all 13
new size.* recipes).

## Next Phase Readiness

- All 27 approved Phase-3 check ids are now registered: this was the last
  check family this phase needs (03-CHECK-ROSTER.md).
- `tests/golden/size_checks_size_crf20.txt`'s canonical text block is a
  sorted `<id> <scope> <value>` line per `size.*` finding (int64 rendered
  bare, `RationalValue` as `num=<n> den=<d>` — never the renderer's own
  convenience `ms`/`tb` fields) — this is the exact shape plan 03-11's
  DOC-03 gate can reference, per this plan's own `<output>` instruction.
- The window-count bound chosen for T-3-46 is `kMaxWindowSteps = 10,000,000`
  (`src/analyzers/size/size.cpp`), checked via one division before the
  sliding-window sweep ever starts.
- The `{1001, 30000}` timebase case revealed no rounding difference
  between the closed-form and a hypothetical accumulating form to
  reconcile — see Decisions Made for the full reasoning; both
  `window_ticks`/`step_ticks` are single truncating integer divisions.
- No blockers identified for 03-10 (TSDuck golden comparison) or 03-11.

## Self-Check: PASSED

- `src/analyzers/size/analyzers.h` — FOUND
- `src/analyzers/size/size.cpp` — FOUND
- `docs/checks/size.file.md` — FOUND
- `docs/checks/size.overhead.md` — FOUND
- `docs/checks/size.stream_bitrate.md` — FOUND
- `docs/checks/size.peak_bitrate.md` — FOUND
- `tests/unit/test_size_windowing.cpp` — FOUND
- `tests/unit/test_size_analyzer.cpp` — FOUND
- `tests/integration/test_size_checks.cpp` — FOUND
- `tests/golden/size_checks_size_crf20.txt` — FOUND
- `57b9794` — FOUND in `git log --oneline --all`
- `ebea3c8` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-03*
