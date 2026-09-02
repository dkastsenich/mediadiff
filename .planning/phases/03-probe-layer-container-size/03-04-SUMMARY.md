---
phase: 03-probe-layer-container-size
plan: 04
subsystem: container-analyzers
tags: [ffmpeg, libavformat, cpp20, catch2, utf8, container-topology, metadata]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      src/probe/{pass.h, demux_session.{h,cpp}, orchestrator.{h,cpp}} and the
      Pass/AnalyzerSpec/ProbeResults pass-declaration seam (03-02-PLAN.md),
      src/probe/packet_scan.{h,cpp} (03-03-PLAN.md, unconsumed by this plan),
      Measurement.estimated/Finding.evidence and the approved 27-id Phase-3
      check-id roster (03-01-PLAN.md) -- this plan registers the six ids the
      roster spells for 03-04 exactly.
provides:
  - "container.track_count/track_types/track_order/chapters -- the four remaining container-agnostic topology checks, expanding src/analyzers/container/topology.cpp beyond the tracer's own container.format"
  - "meta.tags/meta.tags.language -- src/analyzers/container/meta.cpp, a new analyzer family sharing one per-dictionary walk"
  - "Measurement::skip_reason (core/model.h) -- a new, general propagation seam letting an analyzer explicitly mark a measurement as not applicable, read by src/compare/engine.cpp (short-circuits to Status::skipped ahead of normal comparator dispatch) and src/cli/commands/inspect.cpp (renders status/skip_reason in --json and text)"
  - "DemuxSession::stream_info/chapters/container_tags/stream_tags -- per-stream codec/media-type/timecode/caption accessors and chapter/tag-dictionary readers (src/probe/demux_session.{h,cpp}), the accessor surface 03-02-SUMMARY.md's own 'Next Phase Readiness' flagged as this plan's job to add"
  - "18 new fixture recipes in scripts/gen_corpus.sh (topo_*, tags_*, lang_*)"
affects: [03-05-mp4, 03-06-mkv, 03-08-ts, 03-09-size, 03-11-verbose-render]

actuals:
  tokens: 27300
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Measurement::skip_reason (default SkipReason::none) mirrors Measurement::estimated's own shape (03-01-PLAN.md precedent): a field the comparison layer and single-file renderer both read ahead of their normal dispatch, rather than a Value-level encoding. Chosen because Absent alone is ambiguous between 'measured and genuinely empty' and 'this check does not apply here' -- container.chapters on TS needs the second, distinguishable meaning."
    - "compare/engine.cpp's unpaired-measurement path (a key present in neither baseline_by_key nor candidate_by_key) was verified, not assumed, to silently drop the check with NO Finding at all -- confirmed by reading the code, matching the plan's own explicit instruction to check before assuming the engine already handles this."
    - "Per-stream meta.tags/meta.tags.language Scope::index is the stream's own rank AMONG STREAMS OF THE SAME MEDIA TYPE (audio stream 0, audio stream 1, ...), not its raw overall stream-array position -- Claude's Discretion, chosen for the same stability reason 03-CHECK-ROSTER.md's CONT-08 note prefers TS program_number over array position."
    - "DemuxSession's new accessors (StreamInfo, ChapterInfo, container_tags/stream_tags) stay libav-header-free at the public surface -- StreamMediaType/ChapterInfo are plain project types; only demux_session.cpp itself includes libavcodec/libavutil headers to populate them."
    - "sanitize_utf8/kVolatileTagKeys stay meta.cpp-local (anonymous namespace); two `detail::*_for_test` free functions in the same file expose them to tests/unit/test_meta_tags.cpp without widening the analyzer's real public surface -- mirrors src/probe/packet_scan.h's detail::make_packet_record precedent for the identical 'no fixture can reliably exercise this edge case' problem shape."

key-files:
  created:
    - src/analyzers/container/meta.cpp
    - docs/checks/container.track_count.md
    - docs/checks/container.track_types.md
    - docs/checks/container.track_order.md
    - docs/checks/container.chapters.md
    - docs/checks/meta.tags.md
    - docs/checks/meta.tags.language.md
    - tests/unit/test_topology_analyzer.cpp
    - tests/unit/test_meta_tags.cpp
    - tests/integration/test_container_topology.cpp
  modified:
    - src/analyzers/container/topology.cpp
    - src/analyzers/container/analyzers.h
    - src/probe/orchestrator.cpp
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/core/model.h
    - src/core/snapshot.cpp
    - src/compare/engine.cpp
    - src/report/json.cpp
    - src/cli/commands/inspect.cpp
    - src/core/checks.def
    - scripts/gen_corpus.sh
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/golden/list_checks_effective.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "container.track_types canonical encoding: comma-joined media-type tokens in stream index order, preserving duplicates (e.g. 'video,audio,audio,subtitle'); 'other' AVMediaType values fold into the 'data' token, matching container.track_count's own histogram-bin fold."
  - "container.track_order canonical encoding: comma-joined 'media_type:codec_name' entries in stream index order, using avcodec_get_name (never the numeric AVCodecID) so the signature never shifts across an FFmpeg codec-ID enum renumber. Both encodings enter committed snapshots -- reversibility rating 'costly', per the plan's own note."
  - "A tmcd timecode track is identified via codecpar->codec_tag == MKTAG('t','m','c','d') (confirmed against libavformat/mov.c's own mov_read_tmcd dispatch table); a caption data track via codecpar->codec_id == AV_CODEC_ID_EIA_608. Both are named in container.track_types' evidence under keys containing the literal substrings 'tmcd'/'caption' (tmcd_streams/caption_streams), since compare_exact's own message is a fixed 'values differ' with no per-check text."
  - "container.chapters' skipped:not_applicable_container path did NOT fall out of the engine's existing unpaired-measurement handling -- verified by reading src/compare/engine.cpp: a check key absent from BOTH sides of a compare never enters `all_keys` at all, producing no Finding whatsoever (not even a fabricated pass), and this is also unreachable from `inspect`'s per-file JSON view, which has no Status/Finding concept at all. Closed with a new, general Measurement::skip_reason field (see tech-stack) rather than a container.chapters-specific hack, since the same 'this check does not apply here' need recurs for every later not-applicable-container case (container.mp4.*/container.mkv.*/container.ts.* skip differently across families)."
  - "meta.tags' four demonstrated volatile keys (creation_time, encoder, handler_name, encoding_tool) match case-insensitively over ASCII only; major_brand is deliberately absent (scoped to container.mp4.brands instead, per doc 02's own parenthetical), documented inline in meta.cpp's kVolatileTagKeys comment."
  - "meta.tags.language normalization (absent -> 'und', ASCII-lowercased) happens at measurement construction in meta.cpp, never in compare_exact -- the comparator has no per-check knowledge and a snapshot written pre-normalization would disagree with a fresh probe forever. ISO 639-2 T/B aliases (fra/fre, deu/ger) are deliberately NOT resolved."

patterns-established:
  - "A field on Measurement (skip_reason) that both the compare engine and a single-file renderer (inspect) must read ahead of their own normal per-check dispatch: propagate it once, at the model level, rather than teaching every consumer a bespoke Absent-value convention."

requirements-completed: [CONT-01, CONT-03, CONT-04, CONT-09]

coverage:
  - id: D1
    description: "container.track_count/track_types/track_order/chapters each produce a measurement for every supported container family this plan's fixtures exercise (MP4/MKV), with container.chapters auto-skipping as skipped:not_applicable_container on MPEG-TS."
    requirement: "CONT-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_topology_analyzer.cpp - container.track_count is a five-bin Histogram in fixed order, zero bins present"
        status: pass
      - kind: unit
        ref: "tests/unit/test_topology_analyzer.cpp - container.chapters on MPEG-TS is explicitly skipped, not an empty set"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_topology.cpp - container.chapters auto-skips as not_applicable_container on a TS input"
        status: pass
    human_judgment: false
  - id: D2
    description: "Subtitle track presence and a dropped tmcd/caption track are each demonstrated by dedicated fixture pairs, not inferred from a generic stream count; the lost track is named explicitly."
    requirement: "CONT-09"
    verification:
      - kind: unit
        ref: "tests/unit/test_topology_analyzer.cpp - subtitle presence is proven by a dedicated pair, not a generic stream count"
        status: pass
      - kind: unit
        ref: "tests/unit/test_topology_analyzer.cpp - a tmcd timecode track is named in container.track_types evidence"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_topology.cpp - a dropped tmcd timecode track is named literally in track_types"
        status: pass
    human_judgment: false
  - id: D3
    description: "meta.tags excludes the four built-in volatile keys from the compared StringSet while retaining each one's own value in evidence; a non-volatile difference (title) still fails and names the key."
    requirement: "CONT-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_meta_tags.cpp - the compared StringSet excludes every built-in volatile key"
        status: pass
      - kind: unit
        ref: "tests/unit/test_meta_tags.cpp - an ignored-but-present volatile key rides in evidence with its own value"
        status: pass
      - kind: unit
        ref: "tests/unit/test_meta_tags.cpp - the volatile list is exactly creation_time/encoder/handler_name/encoding_tool, member by member"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_topology.cpp - CONT-03: volatile-only differences compare clean, naming the ignored key"
        status: pass
    human_judgment: false
  - id: D4
    description: "meta.tags.language treats und and an absent language tag as equal; a real language change (eng vs fra) still differs; the normalization survives a snapshot round trip."
    requirement: "CONT-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_meta_tags.cpp - und and an absent language tag produce the identical value"
        status: pass
      - kind: unit
        ref: "tests/unit/test_meta_tags.cpp - eng vs fra differ"
        status: pass
      - kind: other
        ref: "manual: mediadiff snapshot lang_absent.mp4 --out ...; mediadiff compare <snap> lang_und.mp4 exits 0"
        status: pass
    human_judgment: false
  - id: D5
    description: "A stream-reorder with identical membership fails container.track_order while container.track_count/track_types (order-insensitive/multiset) still pass -- doc 02's 'a move, not add+remove'."
    requirement: "CONT-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_topology_analyzer.cpp - a same-type stream swap changes track_order while track_count/track_types agree"
        status: pass
      - kind: integration
        ref: "tests/integration/test_container_topology.cpp - a same-type stream reorder fails track_order while track_count stays clean"
        status: pass
    human_judgment: false
  - id: D6
    description: "All 391 tests (356 baseline + 35 new) pass; all four lint scripts pass; two consecutive compare --json runs are byte-identical."
    verification:
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure: 391/391 pass"
        status: pass
      - kind: other
        ref: "scripts/lint_check_id_strings.sh, lint_dead_code_after_fail.sh, lint_eng16.sh, lint_fixture_case_collisions.sh: all exit 0"
        status: pass
      - kind: other
        ref: "manual: two `mediadiff compare topo_subs.mp4 topo_nosubs.mp4 --json` runs byte-identical"
        status: pass
    human_judgment: false

duration: ~150min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 4: Container-Agnostic Topology + Metadata Checks Summary

**The four remaining container-agnostic topology checks (`container.track_count/track_types/track_order/chapters`) and both metadata checks (`meta.tags`, `meta.tags.language`) — six new registered, documented, fixture-covered checks — plus a new general `Measurement::skip_reason` propagation seam the compare engine and `inspect` both needed but did not yet have.**

## Performance

- **Duration:** ~150 min
- **Tasks:** 3/3 completed (Tasks 2 and 3 implemented and committed together — see Deviations)
- **Files modified:** 17 modified, 10 created (across both commits)

## Accomplishments

- `src/analyzers/container/topology.cpp` expanded from the tracer's single `container.format` measurement to five: `container.track_count` (a fixed-order, always-five-bin `Histogram`), `container.track_types` (an ordered, duplicate-preserving comma-joined string, with `tmcd`/caption streams named in evidence), `container.track_order` (an ordered `media_type:codec_name` signature using `avcodec_get_name`, never a numeric codec ID), and `container.chapters` (a `StringSet` of `start`/`end`/`timebase`/`title` entries, or an explicit `skipped:not_applicable_container` on MPEG-TS).
- `src/analyzers/container/meta.cpp` (new): `meta.tags` (container- and per-stream metadata dictionaries as `key=value` `StringSet`s, four built-in volatile keys excluded from comparison but retained in evidence) and `meta.tags.language` (per-stream `language` tag, `und`-equals-absent normalized at measurement construction, ASCII-case-insensitive, T/B aliases deliberately not resolved).
- `src/core/model.h` gained `Measurement::skip_reason` (default `none`) plus `skip_reason_to_string`/`skip_reason_from_string` — a general mechanism, not a `container.chapters`-only hack, since every later container-family check (`container.mp4.*`, `container.mkv.*`, `container.ts.*`) will need the identical "not applicable to this container family" expression. `src/core/snapshot.cpp` round-trips it; `src/compare/engine.cpp` short-circuits to `Status::skipped` ahead of normal comparator dispatch when either side sets it; `src/cli/commands/inspect.cpp` renders it in both `--json` and text.
- `src/probe/demux_session.{h,cpp}` gained `stream_info()` (`StreamMediaType`, stable `codec_name`, `is_timecode`, `is_caption`), `chapters()`, and `container_tags()`/`stream_tags()` — the accessor surface 03-02-SUMMARY.md's own "Next Phase Readiness" section flagged as this plan's job to add, still libav-header-free at the public surface.
- `scripts/gen_corpus.sh` gained 18 new fixture recipes (`topo_*`, `tags_*`, `lang_*`), each check backed by both a triggering and a clean pair per D-14/D-15's fail-first discipline.
- 35 new tests (`test_topology_analyzer.cpp`, `test_meta_tags.cpp`, `test_container_topology.cpp`) — full suite now 391/391.

## Task Commits

Tasks 2 and 3 were implemented and committed together (see Deviations for why); Task 1 stands alone. Both commits independently build, register their checks, and pass the full test suite when checked out in isolation (verified before committing, not merely asserted).

1. **Task 1: the four remaining container-agnostic topology checks (CONT-01, CONT-09)** - `6031d05` (feat)
2. **Tasks 2+3: meta.tags with the volatile ignore list + meta.tags.language with und-equals-absent normalization (CONT-03, CONT-04)** - `dfb66c9` (feat)

## Files Created/Modified

- `src/analyzers/container/topology.cpp` — four new measurements alongside `container.format`
- `src/analyzers/container/meta.cpp` (new) — `meta.tags`/`meta.tags.language`
- `src/analyzers/container/analyzers.h` — `container_meta_analyzer()` + two test-only `detail::*_for_test` seams
- `src/probe/orchestrator.cpp` — registers `container_meta_analyzer()` in `all_analyzers()`
- `src/probe/demux_session.{h,cpp}` — `StreamInfo`, `ChapterInfo`, `stream_info()`, `chapters()`, `container_tags()`, `stream_tags()`
- `src/core/model.h` — `Measurement::skip_reason`, `skip_reason_to_string`/`skip_reason_from_string`
- `src/core/snapshot.cpp` — `skip_reason` round trip
- `src/compare/engine.cpp` — skip-reason short-circuit ahead of D-09's mismatch check
- `src/report/json.cpp` — removed its own now-duplicate `skip_reason_to_string` (see Deviations)
- `src/cli/commands/inspect.cpp` — renders `status`/`skip_reason` for a skipped measurement, both renderers
- `src/core/checks.def` — six new `[[check]]` entries, matching 03-CHECK-ROSTER.md exactly
- `docs/checks/container.track_count.md`, `container.track_types.md`, `container.track_order.md`, `container.chapters.md`, `meta.tags.md`, `meta.tags.language.md` (new)
- `scripts/gen_corpus.sh` — 18 new fixture recipes
- `CMakeLists.txt` — `src/analyzers/container/meta.cpp` added to `libmediadiff`
- `tests/unit/{CMakeLists.txt,test_topology_analyzer.cpp,test_meta_tags.cpp}` (two new test files)
- `tests/integration/{CMakeLists.txt,test_container_topology.cpp}` (one new test file)
- `tests/golden/list_checks_effective.txt` — regenerated (six new rows, D-12 discipline)
- `tests/fixtures/GENERATOR_MANIFEST.json` — regenerated (`generated_at` only)

## Decisions Made

See `key-decisions` in the frontmatter for the full list. Highlights:

- **The canonical encodings for `container.track_types` and `container.track_order`** (comma-joined tokens, described exactly in `key-decisions`) enter committed snapshots — this is the plan's own flagged `reversibility: costly` surface, recorded here per the plan's `<output>` instruction.
- **`tmcd`/caption identification**: `codec_tag == MKTAG('t','m','c','d')` (confirmed against `libavformat/mov.c`'s own dispatch table) and `codec_id == AV_CODEC_ID_EIA_608`, both verified against this pinned FFmpeg's actual headers before writing the code, not assumed from memory.
- **`skipped:not_applicable_container` did NOT fall out of the engine's existing behavior** — verified, not assumed, per the plan's own explicit instruction. See Deviations for the fix.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] `container.chapters`' required `skipped:not_applicable_container` path had no mechanism to produce it**
- **Found during:** Task 1, before any code was written — reading `src/compare/engine.cpp` as the plan's own action text instructed ("verify... and if it is not, emit the skip explicitly rather than assuming").
- **Issue:** `compare_fingerprints`'s unpaired-measurement handling only covers a check key present on ONE side of a compare (and even then, currently just `continue`s with no Finding at all — a pre-existing, differently-scoped gap this plan does not touch). A check key absent from BOTH sides never enters `all_keys` in the first place, so "emit no measurement on TS" (the plan's literal action text) would silently produce nothing — not a `skipped` Finding, not any Finding, and (separately) `inspect`'s single-file JSON view has no `Status`/`Finding` concept at all to hang a skip reason on, so even a `compare`-side fix would not satisfy the plan's own literal `inspect --json` acceptance criterion.
- **Fix:** Added `Measurement::skip_reason` (default `SkipReason::none`) to `core/model.h`, mirroring the already-established `Measurement::estimated` field from 03-01. The `container.chapters` analyzer, on a TS input, now emits a real `Measurement` (value `Absent{}`, `skip_reason = not_applicable_container`) rather than emitting nothing. `compare/engine.cpp` checks this field ahead of its normal dispatch and produces a `Status::skipped` `Finding` directly; `inspect.cpp` renders the same field as `status`/`skip_reason` keys in `--json` (and a `(skipped: ...)` marker in text), present only when non-default so every pre-existing golden/schema stays unaffected. Built as a general mechanism (not `container.chapters`-specific) since every later container-family check will need the identical expression.
- **Files modified:** `src/core/model.h`, `src/core/snapshot.cpp`, `src/compare/engine.cpp`, `src/cli/commands/inspect.cpp`.
- **Verification:** `mediadiff inspect topo_ts.ts --json` shows `container.chapters` with `"status": "skipped"`, `"skip_reason": "not_applicable_container"` (matches the plan's own acceptance criterion literally); `mediadiff compare topo_ts.ts topo_ts.ts --json` shows the same finding through the ordinary compare path; snapshot round trip proven in `tests/unit`.
- **Committed in:** `6031d05`.

**2. [Rule 1 - Bug, compile-blocking] `src/report/json.cpp`'s own local `skip_reason_to_string` collided with the new centralized one**
- **Found during:** First build attempt after adding `core/model.h`'s `skip_reason_to_string`.
- **Issue:** `report/json.cpp` already had an identical, independently-written `skip_reason_to_string(SkipReason)` in its own anonymous namespace (a Phase-2-era, pre-existing local copy, same switch, same spellings). Once both were visible unqualified inside `namespace mediadiff`, GCC reported the call in `finding_to_json` as ambiguous, and the now-unreferenced local copy as `-Werror=unused-function`.
- **Fix:** Removed `json.cpp`'s own copy; that call site now uses `core/model.h`'s centralized version (identical mapping, verified by diff before removing). `src/report/junit.cpp`'s differently-named `skip_reason_text` was left untouched — no collision, no reason to touch working code.
- **Files modified:** `src/report/json.cpp`.
- **Verification:** `ctest` 391/391; the JSON schema/golden tests covering `Finding.skip_reason` rendering are unaffected (same output text).
- **Committed in:** `6031d05`.

### Claude's Discretion (not a deviation, documented per the plan's own `<output>` instruction)

**Tasks 2 and 3 implemented and committed together.** Both extend the identical per-dictionary walk in one file (`src/analyzers/container/meta.cpp`) with no natural boundary in the code itself — `meta.tags.language` reuses `meta.tags`' own per-stream scope resolution (`compute_stream_scopes`) rather than introducing a second one. Splitting them into two commits would have meant either duplicating that scope-resolution logic across two smaller commits or committing dead code in the first. Both this plan's Task 1 (topology) commit and the combined Task 2+3 (meta) commit were independently verified to build, register their own checks, and pass the full test suite in isolation before being committed — the split honors atomicity even though it does not literally mirror the plan's three-task numbering.

**meta.tags' container-level `-metadata encoder=...` fixture recipe doesn't exercise `encoder` exclusion at the container scope.** FFmpeg's mov/mp4 muxer does not propagate a container-level `encoder` metadata key at all (confirmed via `ffprobe -show_entries format_tags`) — only `creation_time`/`major_brand`/`minor_version`/`compatible_brands` appear there for these fixtures; `encoder`/`handler_name` appear only at the STREAM level, where the exclusion is separately proven (`tags_volatile_a.mp4`'s video-stream dictionary genuinely carries all three, asserted directly in `test_meta_tags.cpp`). CONT-03's requirement (all four keys excluded, evidence retained) is fully proven either way; only the container-level fixture's own literal `-metadata encoder=...` flag didn't reach where I expected.

## Issues Encountered

- **GCC 13's `-O3 -Wmaybe-uninitialized` false positive on `core/value.h`'s `Value` `std::variant`**, the same class of issue 03-02-SUMMARY.md documented in a different translation unit. This time in production code (`topology.cpp`'s four new `emit_*` functions, each constructing and `push_back`-ing a real `Measurement`), so the "just avoid the construction" workaround 03-02 used for a test file was not available. Restructuring `emit_chapters` to use two separate local `Measurement`s per branch (rather than one shared, conditionally-reassigned local) reduced but did not eliminate it; resolved with a narrowly-scoped, GCC-only `#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"` at the top of `topology.cpp`, with a comment explaining why (a known compiler false positive on this exact type, not a real uninitialized-value bug — confirmed by reading every flagged construction site by hand).

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `DemuxSession`'s accessor surface now covers everything 03-05 (`container.mp4.*`)/03-06 (`container.mkv.*`) need for stream-level and chapter-level data; per-container-specific data (MP4 `ftyp`/`moov`/`moof` structure, MKV `Cues`/`SeekHead`) is still out of scope for `DemuxSession` itself and remains the raw-scanner (`bmff_scan`/`ebml_scan`) layer's job, per doc 02's own architecture.
- `Measurement::skip_reason` is ready for every later container-family-scoped check (`container.mp4.*` on non-MP4, `container.mkv.*` on non-MKV, `container.ts.*` on non-TS) to reuse verbatim — no new mechanism needed, just set the field.
- The `meta.tags`/`meta.tags.language` per-stream scope-by-media-type-rank convention (see `key-decisions`) should be followed by any later per-stream check that doesn't have its own more specific scoping rule (like TS's `program_number`), to stay consistent across the phase.
- No blockers identified for 03-05.

## Self-Check: PASSED

- `src/analyzers/container/meta.cpp` — FOUND
- `docs/checks/container.track_count.md` — FOUND
- `docs/checks/container.track_types.md` — FOUND
- `docs/checks/container.track_order.md` — FOUND
- `docs/checks/container.chapters.md` — FOUND
- `docs/checks/meta.tags.md` — FOUND
- `docs/checks/meta.tags.language.md` — FOUND
- `tests/unit/test_topology_analyzer.cpp` — FOUND
- `tests/unit/test_meta_tags.cpp` — FOUND
- `tests/integration/test_container_topology.cpp` — FOUND
- `6031d05` — FOUND in `git log --oneline --all`
- `dfb66c9` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
