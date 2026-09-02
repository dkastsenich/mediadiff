---
phase: 03-probe-layer-container-size
plan: 05
subsystem: probe-layer
tags: [iso-bmff, mp4, box-parser, ffmpeg, libavformat, cpp20, catch2, bounds-checking]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      src/probe/{pass.h, demux_session.{h,cpp}, orchestrator.{h,cpp}} and the
      Pass/AnalyzerSpec/ProbeResults pass-declaration seam (03-02-PLAN.md),
      src/probe/packet_scan.{h,cpp} (03-03-PLAN.md, consumed here for the
      fragment_duration median), Measurement.skip_reason (03-04-PLAN.md,
      reused verbatim for both not_applicable_container and
      unparsed_mechanism), and the approved 27-id check roster
      (03-01-PLAN.md) -- this plan registers the six container.mp4.* ids
      exactly as 03-CHECK-ROSTER.md spells them.
provides:
  - "src/probe/bmff_scan.{h,cpp}: a bounded, libav-free ISO-BMFF top-level box walker (BoxRecord/EditListEntry/BmffTrack/BmffScanResult, run_bmff_scan) -- the first hand-rolled parser in this project operating on attacker-influenced binary structure"
  - "Pass::bmff_scan wired into the pass union; ProbeResults::bmff"
  - "src/analyzers/container/mp4.cpp: container.mp4.faststart/brands/fragmentation/fragment_duration/edit_list/timescale, split across container_mp4_analyzer() (ContainerFamily::mp4) and container_mp4_not_applicable_analyzer() (ContainerFamily::other) -- see key-decisions for why the split is structurally required"
  - "10 new fixture recipes in scripts/gen_corpus.sh (mp4_faststart*/nofaststart, mp4_fragmented*, mp4_editdelay/edittrim, mp4_ts_a/b), each confirmed against direct box-offset/elst inspection before being relied on by a test"
affects: [03-06-mkv, 03-07-parser-scan, 03-09-size, 03-10-cross-scanner-degradation, 03-11-verbose-render]

actuals:
  tokens: 31000
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "A bounded reader (BoundedReader in bmff_scan.cpp) that validates every seek/read against the known file length BEFORE touching the file handle, and a single walk_boxes<Fn> template shared by the top-level walk AND every nested descent (moov's children, one trak's children, mdia's children, edts's children) -- the SAME bounds/advance discipline governs every level of the box tree, not just the top, and every callback receives an already-validated (content_offset, end_offset) range rather than re-deriving it from raw header fields."
    - "Every box-size and entry-count computation routes through core/rational.h's detail::checked_add/checked_mul (12 call sites in bmff_scan.cpp) -- the same overflow-checked arithmetic time computation uses elsewhere in this project, applied to byte offsets because the inputs (every box header field) are equally file-controlled and untrusted."
    - "An explicit, standalone strictly-increasing-offset guard (end_offset <= offset) after every box header is read -- not left as an emergent property of total_size >= header_size > 0 -- per the plan's own mandate that the infinite-loop mitigation (T-3-20) be a named, standalone check a reader can see is deliberate, not incidental."
    - "Two AnalyzerSpecs for one check family, scoped to different ContainerFamily values, is the pattern for any future check family whose scanner pass must never run on a non-matching container AND whose checks must still render as an explicit skipped:not_applicable_container (not silently absent) on that non-matching container -- see key-decisions for the full reasoning; container.mkv.*/container.ts.* (plans 03-06/03-08) will need the identical shape."
    - "A test-only sentinel-vs-real-value construction (test_bmff_scan.cpp's version-1 moov/tkhd/mdhd test) proves a parser reads the CORRECT byte offset for a version-dependent field, not merely a plausible-looking number -- plant 0xDEADBEEF at the offset a buggy version-0-assuming reader would read, the real value at the true version-1 offset, and assert the real value (and NOT the sentinel) came back."

key-files:
  created:
    - src/probe/bmff_scan.h
    - src/probe/bmff_scan.cpp
    - src/analyzers/container/mp4.cpp
    - docs/checks/container.mp4.faststart.md
    - docs/checks/container.mp4.brands.md
    - docs/checks/container.mp4.fragmentation.md
    - docs/checks/container.mp4.fragment_duration.md
    - docs/checks/container.mp4.edit_list.md
    - docs/checks/container.mp4.timescale.md
    - tests/unit/test_bmff_scan.cpp
    - tests/unit/test_mp4_analyzer.cpp
    - tests/integration/test_container_mp4.cpp
  modified:
    - src/probe/pass.h
    - src/probe/orchestrator.cpp
    - src/analyzers/container/analyzers.h
    - src/core/checks.def
    - scripts/gen_corpus.sh
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/golden/list_checks_effective.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "The mp4 check family is split into TWO AnalyzerSpecs, not one scoped to ContainerFamily::mp4 -- discovered as a structural requirement (not a stylistic choice) while implementing Task 2: `inspect`/`compare` only ever render a check that has a real Measurement (src/cli/commands/inspect.cpp iterates Fingerprint::measurements directly, never the full CheckRegistry), so a single mp4-scoped analyzer would leave container.mp4.* entirely ABSENT from a non-MP4 report -- not explicitly skipped -- failing the plan's own literal acceptance criterion that `inspect topo_chapters.mkv --json` show all six as skipped:not_applicable_container. Declaring Pass::bmff_scan on a family-agnostic analyzer instead would run the scanner on every container, violating the plan's own prohibition. container_mp4_analyzer() (scope=mp4, declares Pass::bmff_scan) emits real data; container_mp4_not_applicable_analyzer() (scope=other, declares only Pass::demux_header) is a no-op when the file IS mp4 and emits all six as an explicit skip otherwise. Both requirements hold simultaneously only with two narrowly-scoped specs."
  - "container.mp4.fragment_duration's median is ALWAYS derived from the shared PacketScan array's keyframe DTS deltas, never from sidx -- bmff_scan's own approved Task 1 shape ('count moof + collect sidx presence') never parses sidx's segment_duration entries, only its presence as a boolean. Recorded per this plan's own <output> instruction: sidx never supplied the median for any fixture in this plan; `has_sidx` rides in evidence as a presence signal only."
  - "moov's own trak order is trusted to match DemuxSession's AVStream order by INDEX (compute_track_scopes in mp4.cpp) -- confirmed against this pinned FFmpeg's libavformat/mov.c (mov_read_trak appends to s->streams in box-encounter order, no later reordering) rather than assumed. This is what lets container.mp4.edit_list/timescale scope each track's own measurement without any new bmff-side stream-identity field."
  - "The canonical elst encoding (container.mp4.edit_list's compared string value) is `segment_duration={} media_time={} media_rate={}+{}` per entry, semicolon-joined across entries -- enters committed snapshots (reversibility: costly, per the plan's own note). Flagged here per the plan's own <output> instruction for 03-06/Phase-4 consumers (codec_delay, timeline work) that may want a consistent encoding convention."
  - "container.mp4.faststart/fragment_duration both use Measurement::skip_reason = not_applicable_container (not merely 'no measurement') for their own not-applicable cases (fragmented file for faststart; progressive file for fragment_duration) -- reusing 03-04-PLAN.md's established mechanism verbatim, matching container.chapters' own precedent, rather than inventing a second convention."
  - "A truncated fixture's exact bmff_scan stop_offset always lands at the failing box's OWN header offset (never a byte inside it) -- proven exactly, not just complete==false, in every one of test_bmff_scan.cpp's behavior-3/4/5 adversarial cases via a hand-constructed byte buffer (never generated media, per this plan's own instruction)."

patterns-established:
  - "Two ContainerFamily-scoped AnalyzerSpecs (one real-data, one family-agnostic not-applicable) is the required shape for any future scanner-backed, family-specific check whose scanner must never run on a non-matching container AND whose checks must still render an explicit skip there -- container.mkv.* (03-06) and container.ts.* (03-08) will need this exact split."

requirements-completed: [PROBE-04, CONT-05]

coverage:
  - id: D1
    description: "bmff_scan records the doc-02 top-level box set (ftyp/moov/mdat/moof/sidx/free) with correct byte offsets, handling 32-bit sizes, the 64-bit largesize form, size==0 (extends to range end), and uuid skip -- without ever reading a box payload."
    requirement: "PROBE-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - a real MP4's top-level walk records ftyp before moov before mdat, complete"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - a 32-bit size of 1 is read as the 64-bit largesize form"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - a box with size 0 extends to the end of the file"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - a uuid box is skipped by its declared size without interpreting its extended type"
        status: pass
    human_judgment: false
  - id: D2
    description: "Every declared box size is validated against remaining bytes before use; an out-of-range or non-advancing size ends the walk with an exact recorded stop_offset, never an out-of-bounds read or infinite loop."
    requirement: "PROBE-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - a box declaring a size larger than the remaining bytes ends the walk at its own offset (+ the NOT-at-offset-0 variant)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - a 32-bit size of 4 / a largesize of 10 (both smaller than their own header) end the walk"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - elst entry_count is validated against remaining bytes BEFORE any entry is read"
        status: pass
    human_judgment: false
  - id: D3
    description: "bmff_scan's minimal moov descent (mvhd.timescale, per-trak tkhd.track_id, mdia/mdhd.timescale, edts/elst entries) is correct at BOTH FullBox version 0 and version 1 field widths -- proven via a sentinel-vs-real-value construction, not merely a plausible number."
    requirement: "PROBE-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - moov/trak/mdia descent at version 0 yields mvhd.timescale, tkhd.track_id, mdhd.timescale"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - moov/trak/mdia descent at version 1 reads the 64-bit-widened fields at the CORRECT offset, not the version-0 offset"
        status: pass
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - elst version 0/1 entries parse segment_duration/media_time at the correct width"
        status: pass
    human_judgment: false
  - id: D4
    description: "A truncated or structurally invalid MP4 yields skipped:unparsed_mechanism carrying the byte offset where the walk stopped -- never a crash, never a silent pass -- and never a numeric measurement derived from a partially-walked file."
    requirement: "PROBE-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_bmff_scan.cpp - truncating a valid MP4 (10%/50%/90%) never crashes and always yields complete=false"
        status: pass
      - kind: unit
        ref: "tests/unit/test_mp4_analyzer.cpp - a truncated MP4 whose bmff_scan is incomplete yields all six as skipped:unparsed_mechanism with stop_offset in evidence"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_mp4.cpp - a truncated MP4 degrades to exactly one of exit 65 or skipped:unparsed_mechanism, at every truncation point (1 byte/10%/50%/90%)"
        status: pass
    human_judgment: false
  - id: D5
    description: "All six container.mp4.* checks emit real, correct measurements on a real MP4 (faststart layout, brands, fragmentation mode, median fragment duration with a checked-tick-ordered median, per-track edit_list with empty-edit-vs-trim classification and profile-severity escalation, per-track timescale)."
    requirement: "CONT-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_mp4_analyzer.cpp - Tests 1-6 (faststart, brands, fragmentation, fragment_duration close/far, edit_list classification, timescale global-vs-track)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_topology.cpp-style CLI acceptance criteria, manually verified against the real binary (see Deviations/verification notes) and covered structurally by test_container_mp4.cpp's cross-format assertions"
        status: pass
    human_judgment: false
  - id: D6
    description: "container.mp4.edit_list resolves severity fail under --profile remux/strict-bitexact and warn under --profile sw-encoder, through the resolved-policy chain (never by reading the registry directly)."
    requirement: "CONT-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_mp4_analyzer.cpp - container.mp4.edit_list resolves fail under remux/strict-bitexact, warn under sw-encoder, through the resolved-policy chain"
        status: pass
    human_judgment: false
  - id: D7
    description: "All six container.mp4.* checks auto-skip as skipped:not_applicable_container on a non-MP4 input (MKV, TS), and the bmff_scan pass is never requested for those containers at all."
    requirement: "CONT-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_mp4_analyzer.cpp - all six container.mp4.* checks are skipped:not_applicable_container on an MKV input, and bmff_scan did not run (PassExecutionLog asserted directly)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_mp4.cpp - all six container.mp4.* findings are skipped:not_applicable_container on an MKV input / a TS input, none is pass"
        status: pass
    human_judgment: false
  - id: D8
    description: "Cross-format degradation: truncated, entirely-random (fixed-seed) and zero-byte MP4 inputs degrade to exactly one of exit 65 or skipped:unparsed_mechanism, never a crash; a permanently-red canary guards the degrade path itself."
    requirement: "PROBE-04"
    verification:
      - kind: integration
        ref: "tests/integration/test_container_mp4.cpp - a file of entirely random bytes (fixed seed) produces exit 65 cleanly, no crash"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_mp4.cpp - canary: a zero-byte input ALWAYS reports unparseable, never clean"
        status: pass
    human_judgment: false
  - id: D9
    description: "All 427 tests pass; all four lint scripts pass; two consecutive compare --json runs are byte-identical; the whole suite re-run twice produces identical degradation results."
    verification:
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure: 427/427 pass (391 baseline + 36 new)"
        status: pass
      - kind: other
        ref: "scripts/lint_check_id_strings.sh, lint_dead_code_after_fail.sh, lint_eng16.sh, lint_fixture_case_collisions.sh: all exit 0"
        status: pass
      - kind: other
        ref: "manual: two `mediadiff compare mp4_faststart.mp4 mp4_nofaststart.mp4 --json` runs byte-identical; integration.container_mp4 run twice, identical results both times"
        status: pass
    human_judgment: false

duration: ~140min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 5: bmff_scan — Bounded Box Walk + container.mp4.* Checks Summary

**A bounded, libav-free ISO-BMFF box walker (`src/probe/bmff_scan.{h,cpp}`) with overflow-checked arithmetic and exact stop-offset reporting, feeding the six `container.mp4.*` checks (`src/analyzers/container/mp4.cpp`) — split across two `AnalyzerSpec`s so a non-MP4 file gets an explicit `skipped:not_applicable_container` rather than the scanner running on bytes it cannot interpret, or the check simply vanishing from the report.**

## Performance

- **Duration:** ~140 min
- **Tasks:** 3/3 completed
- **Files modified:** 10 modified, 12 created (across two commits)

## Accomplishments

- `src/probe/bmff_scan.{h,cpp}` (PROBE-04): a bounded `BoundedReader` (validates every seek/read against the known file length before touching the handle) drives one shared `walk_boxes<Fn>` template for the top-level walk AND every nested descent. Records `ftyp/moov/mdat/moof/sidx/free` with byte offsets; handles 32-bit sizes, the 64-bit `largesize` form, `size==0` (extends to the current range's end), and `uuid` skip without interpreting the extended type. Descends `moov` minimally: `mvhd.timescale`, per-`trak` `tkhd.track_id`/`mdia`/`mdhd.timescale`/`edts`/`elst` entries, correct at both FullBox version 0 and version 1 field widths. Every offset/entry-count computation is overflow-checked (12 `detail::checked_add`/`checked_mul` call sites); an out-of-range or non-advancing size ends the walk with an exact `stop_offset`, proven exactly (not just `complete==false`) for every adversarial case.
- `src/analyzers/container/mp4.cpp` (CONT-05): `container.mp4.faststart` (moov-vs-first-mdat offset, skips on fragmented), `container.mp4.brands` (StringSet of major+compatible brands), `container.mp4.fragmentation` (progressive/fragmented mode), `container.mp4.fragment_duration` (median fragment duration via `compare_ticks_checked`, never `compare_ticks`, derived from the shared PacketScan array's keyframe DTS deltas — `sidx` is presence-only per bmff_scan's own approved scope, never a duration source), `container.mp4.edit_list` (per-track canonical `elst` string, empty-edit-vs-trim classified in evidence, `fail` under `remux`/`strict-bitexact` via `[check.profile_severity]`), and `container.mp4.timescale` (global `mvhd` + per-track `mdhd`).
- **Architectural discovery (Task 2):** a single `AnalyzerSpec` scoped to `ContainerFamily::mp4` cannot satisfy both "the scanner never runs on a non-MP4 file" AND "`inspect`/`compare` show an explicit `skipped:not_applicable_container` on a non-MP4 file" simultaneously, because `inspect`/`compare` only ever render a check that has a real `Measurement` — a family-scoped-only analyzer would leave `container.mp4.*` silently absent, not skipped, on an MKV/TS file. Resolved with two `AnalyzerSpec`s: `container_mp4_analyzer()` (scope=mp4, real data) and `container_mp4_not_applicable_analyzer()` (scope=other, no-op on mp4, explicit skip everywhere else) — see key-decisions.
- 10 new fixture recipes in `scripts/gen_corpus.sh`, each confirmed against direct box-offset/`elst`-content inspection (a Python script, scratch-only, never committed) before being relied on by a test — including the discovery that `-frag_duration` alone does not reliably set fragment length (it only rounds up to the next keyframe) and that `-af adelay` does NOT produce an empty edit (only `-itsoffset` does).
- 36 new tests (`test_bmff_scan.cpp`: 19, `test_mp4_analyzer.cpp`: 12, `test_container_mp4.cpp`: 5) — full suite now 427/427.

## Task Commits

Tasks 1 and 2 were implemented and committed together (see Deviations for why); Task 3 stands alone.

1. **Tasks 1+2: bmff_scan bounded box walk + the six container.mp4.* checks (PROBE-04, CONT-05)** - `d1d5cc5` (feat)
2. **Task 3: cross-format skip correctness + mp4 fuzz-adjacent degradation pair** - `c4991cb` (test)

## Files Created/Modified

- `src/probe/bmff_scan.{h,cpp}` (new) — `BoxRecord`, `EditListEntry`, `BmffTrack`, `BmffScanResult`, `run_bmff_scan`, `BoundedReader`, `walk_boxes`
- `src/probe/pass.h` — `Pass::bmff_scan` (pre-existing enumerator, now consumed); `ProbeResults::bmff`
- `src/probe/orchestrator.cpp` — `Pass::bmff_scan` execution arm; both mp4 `AnalyzerSpec`s registered in `all_analyzers()`
- `src/analyzers/container/mp4.cpp` (new) — the six checks, `container_mp4_analyzer()`, `container_mp4_not_applicable_analyzer()`
- `src/analyzers/container/analyzers.h` — the two new accessor declarations, with the split's full rationale
- `src/core/checks.def` — six new `[[check]]` entries, matching 03-CHECK-ROSTER.md exactly, including `container.mp4.edit_list`'s `[check.profile_severity]` overrides
- `docs/checks/container.mp4.{faststart,brands,fragmentation,fragment_duration,edit_list,timescale}.md` (new)
- `scripts/gen_corpus.sh` — 10 new fixture recipes
- `CMakeLists.txt` — `src/probe/bmff_scan.{h,cpp}`, `src/analyzers/container/mp4.cpp` added
- `tests/unit/{CMakeLists.txt,test_bmff_scan.cpp,test_mp4_analyzer.cpp}` (two new test files)
- `tests/integration/{CMakeLists.txt,test_container_mp4.cpp}` (one new test file)
- `tests/golden/list_checks_effective.txt` — regenerated (six new rows, D-12 discipline)
- `tests/fixtures/GENERATOR_MANIFEST.json` — regenerated (`generated_at` only)

## Decisions Made

See `key-decisions` in the frontmatter for the full list. Highlights:

- **The two-`AnalyzerSpec` split** (`container_mp4_analyzer`/`container_mp4_not_applicable_analyzer`) is the plan's single most significant structural discovery — not merely a workaround, but the pattern 03-06 (`container.mkv.*`) and 03-08 (`container.ts.*`) will need to reuse verbatim, since every later container-family-specific scanner (`ebml_scan`, `ts_scan`) has the identical "must never run on the wrong container, but must still explicitly skip there" requirement.
- **`sidx` never supplies `fragment_duration`'s median** — bmff_scan's own Task-1-approved struct shape only records `has_sidx` (a boolean), never parsed segment-duration entries, so the median is always derived from `PacketScan`'s keyframe DTS deltas. Recorded here per the plan's own `<output>` instruction.
- **`trak` order trusted to match `AVStream` order by index** — confirmed against libavformat/mov.c's own `mov_read_trak` (appends in box-encounter order, no reordering) rather than assumed, letting `edit_list`/`timescale` resolve per-track `Scope` with no new bmff-side identity field.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] A single mp4-scoped analyzer cannot satisfy the plan's own literal `inspect`-on-MKV acceptance criterion**
- **Found during:** Task 2, before writing `mp4.cpp`'s `run()` — reading `src/cli/commands/inspect.cpp` to confirm how a skip would actually be rendered (the same "verify, don't assume" discipline 03-04-SUMMARY.md's own deviation entry documents for the identical class of problem).
- **Issue:** The plan's literal design (`required_passes = {Pass::demux_header, Pass::bmff_scan, ...}`, scope `ContainerFamily::mp4`) is exactly right for keeping `bmff_scan` off non-MP4 bytes, but `inspect`'s JSON renderer (`entries_for_group`) iterates `Fingerprint::measurements` only — it never consults the full `CheckRegistry`. An analyzer that simply never RUNS for a non-matching family therefore contributes ZERO measurements for that file, meaning `container.mp4.*` would be entirely ABSENT from an MKV/TS report, not `skipped` — directly failing the plan's own acceptance criterion (`mediadiff inspect topo_chapters.mkv --json` must show all six as `skipped`/`not_applicable_container`).
- **Fix:** Split into two `AnalyzerSpec`s (`container_mp4_analyzer`, scope=mp4, real data; `container_mp4_not_applicable_analyzer`, scope=other, no-op on mp4 else explicit skip). Both reuse `Measurement::skip_reason = not_applicable_container` (03-04-PLAN.md's mechanism) rather than inventing anything new. No changes needed to `inspect.cpp`/`compare/engine.cpp` — the existing skip-reason rendering already does the right thing once a real `Measurement` exists to carry it.
- **Files modified:** `src/analyzers/container/mp4.cpp`, `src/analyzers/container/analyzers.h`, `src/probe/orchestrator.cpp`.
- **Verification:** `mediadiff inspect topo_chapters.mkv --json` and `mediadiff inspect topo_ts.ts --json` both show all six `container.mp4.*` entries as `"status": "skipped"`, `"skip_reason": "not_applicable_container"` (matches the plan's own literal acceptance criterion); `tests/unit/test_mp4_analyzer.cpp`'s MKV test additionally asserts `Pass::bmff_scan` never entered the `PassExecutionLog` for that file.
- **Committed in:** `d1d5cc5`.

### Claude's Discretion (not a deviation, documented per the plan's own `<output>` instruction)

**Tasks 1 and 2 implemented and committed together.** `mp4.cpp` is `bmff_scan`'s only consumer, and both share the same `CMakeLists.txt`/`checks.def` edit regions with no natural split boundary — splitting would have meant either duplicating those edits across two smaller commits or committing a scanner with zero real consumers in the first. Both this plan's Task-1-only build (bmff_scan alone, before mp4.cpp existed) and the combined Task-1+2 state were independently verified to build and pass the full suite before committing. Mirrors 03-04-SUMMARY.md's own precedent for this exact shape.

**`-frag_duration` alone does not control fragment length reliably.** The plan's own action text suggested `-frag_duration` for the fragmented-pair fixture recipes; direct testing (a scratch Python box/keyframe inspection) showed FFmpeg's `frag_keyframe` muxing only rounds UP to the next keyframe boundary — three different `-frag_duration` values (500ms/600ms/2000ms) against the default GOP interval all produced the same `moof_count`. Switched to `-g` (keyframe interval, in frames) instead, which directly and reliably controls fragment length; confirmed via keyframe-DTS inspection before relying on it in `mp4_fragmented(.mp4/_close.mp4/_far.mp4)`.

**`-af adelay` does not produce an empty edit (`media_time=-1`).** The plan's own action text names `adelay` for the empty-edit fixture; direct `elst` inspection showed `adelay` only adds silence samples to the audio stream, never shifting its start time, so the muxer never emits an empty edit for it. `-itsoffset` on the audio input does produce one (confirmed by direct box inspection) and was used instead for `mp4_editdelay.mp4`.

## Issues Encountered

- **GCC 13's `-O3 -Wmaybe-uninitialized` false positive on `core/value.h`'s `Value` `std::variant`** — the same class of false positive `topology.cpp`'s own top-of-file comment documents (03-02-SUMMARY.md's "Issues Encountered" in a third translation unit) — reproduced in `mp4.cpp`'s six `emit_*` functions. Resolved identically: a narrowly-scoped, GCC-only `#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"` immediately before the `probe/*.h` includes, with the same explanatory comment `topology.cpp` carries.
- **No sanitizer CMake preset exists in this repository** (confirmed: `CMakePresets.json` has no ASan/UBSan configuration) — matches 03-03-SUMMARY.md's own note that the one ASan run performed there was a one-off scratch build, not a committed preset. Per this plan's own acceptance criterion's explicit permission ("if none does, record that in the summary rather than claiming it"), no sanitizer run was performed for `bmff_scan.cpp` in this plan; recorded honestly here rather than claimed. Every bounds-violation behavior IS proven functionally (exact `stop_offset` assertions on hand-crafted adversarial buffers), just not additionally under ASan/UBSan instrumentation. Logged to `.planning/WINDOWS.md` (kind: `unrun-verify`) for future-phase visibility.

## User Setup Required

None — no external service configuration required (the `MEDIADIFF_FFMPEG`/system-`ffmpeg`-≥6.1 precondition was already satisfied in this environment; `scripts/gen_corpus.sh` ran successfully).

## Next Phase Readiness

- The two-`AnalyzerSpec`-per-family pattern (real-data analyzer scoped to the family, family-agnostic not-applicable sibling) is proven end-to-end and ready for 03-06 (`container.mkv.*`/`ebml_scan`) and 03-08 (`container.ts.*`/`ts_scan`) to reuse verbatim — this was the plan's own biggest open design question and is now closed with a working, tested reference implementation.
- `BoundedReader`/`walk_boxes<Fn>` in `bmff_scan.cpp` are file-local (anonymous namespace), not exposed as a shared parsing utility — 03-06/03-08's `ebml_scan`/`ts_scan` are structurally different formats (vint-length EBML, fixed-188/204-byte TS packets) and doc 02's own architecture treats each as an independent hand-rolled scanner, so no shared box-walking abstraction was extracted; flagging this as a deliberate non-decision rather than an oversight, in case a later plan reconsiders once a second and third scanner exist to compare against.
- `container.mp4.edit_list`'s canonical encoding is now the reference shape for any later per-entry canonical-string check (Phase 4's `codec_delay`/timeline work was flagged by this plan's own `<output>` instruction as a likely consumer).
- No blockers identified for 03-06.

## Self-Check: PASSED

- `src/probe/bmff_scan.h` — FOUND
- `src/probe/bmff_scan.cpp` — FOUND
- `src/analyzers/container/mp4.cpp` — FOUND
- `docs/checks/container.mp4.faststart.md` — FOUND
- `docs/checks/container.mp4.brands.md` — FOUND
- `docs/checks/container.mp4.fragmentation.md` — FOUND
- `docs/checks/container.mp4.fragment_duration.md` — FOUND
- `docs/checks/container.mp4.edit_list.md` — FOUND
- `docs/checks/container.mp4.timescale.md` — FOUND
- `tests/unit/test_bmff_scan.cpp` — FOUND
- `tests/unit/test_mp4_analyzer.cpp` — FOUND
- `tests/integration/test_container_mp4.cpp` — FOUND
- `d1d5cc5` — FOUND in `git log --oneline --all`
- `c4991cb` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
