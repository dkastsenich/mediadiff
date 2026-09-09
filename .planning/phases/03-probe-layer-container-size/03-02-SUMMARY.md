---
phase: 03-probe-layer-container-size
plan: 02
subsystem: probe-layer
tags: [ffmpeg, libavformat, cpp20, catch2, cli11, thread-local]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      SkipReason extended to 14 enumerators, Measurement.estimated/Finding.evidence,
      detail::checked_div, D-03 tolerance widening, and the approved 27-id Phase-3
      check-id roster (03-01-PLAN.md) -- this plan registers container.format
      exactly as that roster spells it.
provides:
  - "src/probe/{pass.h, demux_session.{h,cpp}, orchestrator.{h,cpp}} -- the probe layer's shared infrastructure every later Phase-3 analyzer builds on"
  - "fingerprint_input: the single entry point all four CLI commands (snapshot, compare, dir, inspect) now call instead of read_snapshot"
  - "container.format: the phase's tracer check, registered, documented, producing real pass/fail findings against synthesized MP4/MKV fixtures"
  - "DemuxSession's wall-clock budget (kDefaultProbeBudgetMs=30000, --probe-timeout / [probe] timeout_seconds) and single-process libav log capture with thread-local attribution"
  - "PROBE-08's pass union: each declared Pass runs exactly once per file, analyzers scoped by ContainerFamily before the union is computed, all_analyzers() assembled as an explicit hand-written list"
  - "scripts/gen_corpus.sh recipes for tracer_a.mp4/tracer_a_copy.mp4/tracer_a.mkv"
affects: [03-03-packetscan, 03-04-topology-meta, 03-05-mp4, 03-06-mkv, 03-08-ts, 03-09-size]

actuals:
  tokens: 23600
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "DemuxSession is a PImpl-by-forward-declaration RAII type (opaque `struct AVFormatContext;`) so no libav header crosses src/probe/demux_session.h's public surface -- src/analyzers/**/*.cpp reaches libav-derived data only through its typed accessors."
    - "PassSet is a 6-bit hand-rolled bitset over the Pass enum, iterated via for_each in enumerator order -- not a std::set, since the whole domain is six known bits."
    - "all_analyzers() is an explicit, hand-written std::vector assembled once in orchestrator.cpp from each family's own named `<family>_analyzer()` accessor -- never a self-registering static list, since cross-translation-unit static-init order is unspecified and TRUST-05 requires byte-identical --json across runs and platforms."
    - "A runtime-mutable process-wide default (default_wall_clock_budget_ms/set_default_wall_clock_budget_ms, relaxed atomic) lets DemuxOptions's own default member initializer pick up a CLI-configured --probe-timeout without src/probe/orchestrator.cpp's single `DemuxSession::open(path, DemuxOptions{})` call site ever needing to change again."
    - "detail::run_probe (src/probe/orchestrator.cpp) is the test-only internal entry point behind fingerprint_input's frozen 2-argument public signature -- takes an explicit analyzer list and an optional PassExecutionLog*, letting tests/unit/test_pass_union.cpp inject synthetic AnalyzerSpecs without touching tests/support/stub_analyzer.h's separate D-11 mechanism."

key-files:
  created:
    - src/probe/pass.h
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/probe/orchestrator.h
    - src/probe/orchestrator.cpp
    - src/analyzers/container/analyzers.h
    - src/analyzers/container/topology.cpp
    - docs/checks/container.format.md
    - tests/unit/test_demux_session.cpp
    - tests/unit/test_pass_union.cpp
    - tests/integration/test_probe_tracer.cpp
    - tests/fixtures/probe/not_media.txt
  modified:
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/compare.{cpp,h}
    - src/cli/commands/dir.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/main.cpp
    - src/cli/options.{h,cpp}
    - src/cli/exit_code.h
    - src/config/toml_load.{h,cpp}
    - src/core/checks.def
    - src/report/model.{h,cpp}
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - .gitignore
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/unit/test_report_model.cpp
    - tests/baseline/report-1.0.json
    - tests/golden/{dir_worst_n,json_schema_basic,list_checks_effective}.txt

key-decisions:
  - "fingerprint_input distinguishes 'JSON-shaped but explicitly rejected by read_snapshot' (schema_version major mismatch, unregistered check ID -- propagate that rejection unchanged) from 'not JSON at all' (fall through to a real probe) via a first-non-whitespace-byte peek (looks_like_json_document), discovered necessary when it broke two pre-existing Phase-2 integration tests."
  - "DemuxOptions::wall_clock_budget_ms defaults from a runtime-settable global rather than a compile-time constant, so orchestrator.cpp's probe path never needed a second edit to honor --probe-timeout once Task 2 added the CLI/config surface."
  - "all_analyzers() was implemented as the explicit hand-written list from Task 1 onward (not a self-registering-static list later replaced in Task 3) -- Task 3 instead focuses on the test-injection seam (detail::run_probe) and the tests proving the property."
  - "AnalyzerSpec::name is 'container_topology' (underscore), not 'container.format' (dotted) -- the dotted spelling is a check id and scripts/lint_check_id_strings.sh's D-03 scan flags any dotted-lowercase quoted string literal under src/analyzers/ regardless of which field it initializes."
  - "report/model.cpp's accumulate() fixed to gate Summary.worst_gating only on a finding whose STATUS itself signals a problem (warn/fail/error), never a Status::pass finding regardless of the check's own declared severity -- see Deviations."

patterns-established:
  - "A per-invocation CLI knob that must reach a deep, already-frozen internal call site without touching that call site again: back it with a runtime-mutable global/atomic the deep type's own default member initializer reads, resolved once on the calling thread before any worker starts."

requirements-completed: [PROBE-01, PROBE-08, CONT-01]

coverage:
  - id: D1
    description: "Real media (a synthesized MP4 and its byte-identical copy, and a synthesized MKV) flows end-to-end through fingerprint_input -> DemuxSession -> the pass union -> topology.cpp -> the Phase-2 compare engine -> a rendered report, from both `compare` and `inspect`, with all four CLI commands sharing the same entry point."
    requirement: "PROBE-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - PROBE-01: two clean real media files compare with a passing container.format"
        status: pass
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - CONT-01: a container-family mismatch fails container.format"
        status: pass
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - inspect renders container.format for a real media file"
        status: pass
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - opens a synthesized MP4 and reports format_name/stream_count"
        status: pass
    human_judgment: false
  - id: D2
    description: "fingerprint_input tries read_snapshot first; falls through to a real probe only when the input is not JSON-shaped and the failure was input_unsupported; an input_open (missing file) never falls through; a JSON-shaped file read_snapshot explicitly rejected (schema major mismatch, unregistered check id) propagates that rejection unchanged."
    requirement: "PROBE-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#orchestrator - fingerprint_input on a valid snapshot never probes"
        status: pass
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - a missing baseline file never falls through to a probe attempt"
        status: pass
      - kind: integration
        ref: "tests/integration/test_schema_version.cpp#schema_version - a schema_version MAJOR mismatch exits exactly 65 through the real CLI, naming both versions"
        status: pass
      - kind: integration
        ref: "tests/integration/test_type_poisoned_snapshot.cpp#type_poisoned_snapshot - CR-01 half 1: compare exits a clean input-error code, never crashes"
        status: pass
    human_judgment: false
  - id: D3
    description: "A per-file wall-clock budget (kDefaultProbeBudgetMs=30000ms) bounds DemuxSession::open via an AVIOInterruptCB; a 0ms budget fails immediately with ErrorKind::input_unsupported naming the budget; --probe-timeout (compare/dir/inspect/snapshot) and [probe] timeout_seconds configure it end to end through the real CLI."
    requirement: "PROBE-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - a 0ms wall-clock budget fails immediately, never hangs"
        status: pass
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - the default budget succeeds on a normal fixture"
        status: pass
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - --probe-timeout 0 exits 65 and stderr names the timeout"
        status: pass
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - --probe-timeout with a non-numeric argument exits 64"
        status: pass
    human_judgment: false
  - id: D4
    description: "The libav log callback is installed exactly once, process-wide, from main.cpp; DemuxSession::open attributes AV_LOG_WARNING-and-above lines to the calling thread's own accumulator via a thread_local pointer, with no bleed between two sequential opens on the same thread; the count folds into Fingerprint.envelope.diagnostics, always present in `inspect --json`."
    requirement: "PROBE-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - probe_log_callback counts WARNING-and-above lines into the current accumulator"
        status: pass
      - kind: unit
        ref: "tests/unit/test_demux_session.cpp#demux_session - two sequential accumulators on one thread never bleed into each other"
        status: pass
      - kind: integration
        ref: "tests/integration/test_probe_tracer.cpp#probe_tracer - inspect --json renders a diagnostics object"
        status: pass
    human_judgment: false
  - id: D5
    description: "The orchestrator's pass union runs each declared Pass exactly once per file (Pass::demux_header always, others only when an applicable analyzer requires them); an analyzer scoped to a non-matching ContainerFamily is skipped entirely, contributing no Measurement and no pass to the union; zero applicable analyzers still yields a valid, empty Fingerprint; all_analyzers() order is stable."
    requirement: "PROBE-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - the union of two overlapping declarations runs each pass exactly once"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - a family-scoped analyzer is skipped entirely for a non-matching container"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - zero applicable analyzers still yields a valid, empty Fingerprint"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - all_analyzers() order is stable across calls"
        status: pass
    human_judgment: false
  - id: D6
    description: "Bug fix (Rule 3, blocking): Summary.worst_gating no longer gates on a Status::pass finding's own declared severity, restoring 'a no-change re-run under the right profile is clean out of the box' now that a real fail-severity, commonly-passing check (container.format) exists."
    verification:
      - kind: unit
        ref: "tests/unit/test_report_model.cpp#report_model - worst_gating ignores a Status::pass finding regardless of the check's own severity"
        status: pass
      - kind: unit
        ref: "tests/unit/test_report_model.cpp#report_model - worst_gating is the maximum severity among findings that actually gate"
        status: pass
      - kind: unit
        ref: "tests/unit/test_report_model.cpp#report_model - worst_gating still gates on a Status::error finding"
        status: pass
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure (340/340 pass, 1 skipped console test)"
        status: pass
    human_judgment: false

duration: ~110min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 2: Probe Layer Tracer — DemuxSession, Orchestrator, container.format Summary

**Real MP4/MKV fixtures flow end-to-end through a new `fingerprint_input` seam — `DemuxSession` (wall-clock-bounded, libav-log-capturing) → the `PassSet`/`AnalyzerSpec` pass union → `container.format` → the Phase-2 compare engine — from all four CLI commands, plus a project-wide `worst_gating` bug fix this tracer's own acceptance criteria exposed.**

## Performance

- **Duration:** ~110 min
- **Tasks:** 3/3 completed (plus one out-of-plan blocking-issue fix, see Deviations)
- **Files modified:** 25 modified, 12 created

## Accomplishments

- `src/probe/pass.h` declares `Pass`/`PassSet`/`ContainerFamily`/`ProbeResults`/`AnalyzerSpec`/`all_analyzers()` — the seam every Phase-3 analyzer family builds on. `parser_scan` is declared now (Phase 4) so a later analyzer compiles without reopening the enum.
- `src/probe/demux_session.{h,cpp}` (PROBE-01): a move-only, PImpl-by-forward-declaration `DemuxSession` opened via `avformat_open_input` + `avformat_find_stream_info`, `AVFMT_FLAG_GENPTS` never touched. A per-file wall-clock budget (`kDefaultProbeBudgetMs=30000`) bounds the open via an `AVIOInterruptCB`; a `0`ms budget fails immediately naming "budget". The libav log is captured through a single process-wide callback (installed exactly once, `src/cli/main.cpp`) attributed per-open-call via a `thread_local` accumulator.
- `src/probe/orchestrator.{h,cpp}`: `fingerprint_input(path, registry)` — tries `read_snapshot` first; falls through to a real probe only when the input is *not* JSON-shaped (a first-non-whitespace-byte peek) and the failure was `input_unsupported`. A JSON-shaped file `read_snapshot` explicitly rejected (schema major mismatch, unregistered check id) keeps its own diagnostic. The probe path (`detail::run_probe`, the test-injection seam behind the frozen public signature) opens a `DemuxSession`, derives `ContainerFamily`, unions applicable analyzers' `required_passes` (`Pass::demux_header` always runs), executes each pass once, then runs every applicable analyzer.
- `src/analyzers/container/{analyzers.h,topology.cpp}`: `container.format`, registered in `checks.def` (`group=container`, `semantic=exact`, `severity=fail`, matching 03-CHECK-ROSTER.md exactly), documented at `docs/checks/container.format.md`. Extracted MP4 format name: `"mov"`. Matroska: `"matroska"`.
- All four CLI commands (`snapshot`, `compare`, `dir`, `inspect`) call `fingerprint_input` instead of `read_snapshot` — zero remaining `read_snapshot` call sites in any of the four files (comment-stripped grep).
- `--probe-timeout SECONDS` (consumed) and `--probe-memory-budget-mb MB` (registered, explicitly left unconsumed for a later plan, per the plan's own instruction) registered on all four commands; `[probe] timeout_seconds` added to `mediadiff.toml`'s shape validation.
- `scripts/gen_corpus.sh` gained `tracer_a.mp4`/`tracer_a_copy.mp4`/`tracer_a.mkv` recipes (`mpeg4`/`aac`, never `libx264`/GPL, matching this project's decode-only LGPL constraint even for the corpus-generating system ffmpeg).
- PROBE-08 proven: `tests/unit/test_pass_union.cpp` exercises `detail::run_probe` directly with synthetic `AnalyzerSpec`s to assert the pass union runs each pass exactly once, scopes by container family, and handles the zero-applicable-analyzers case without error.

## Task Commits

Each task was committed atomically (plus one out-of-plan fix commit — see Deviations):

1. **[Fix] worst_gating must not gate on a Status::pass finding's own check severity** - `f79c07b` (fix) — Rule 3 blocking-issue fix, discovered by Task 1's own acceptance criteria; committed first since it's independent of the tracer feature.
2. **Task 1+2: real media through fingerprint_input -> DemuxSession -> container.format (tracer + wall-clock budget + log capture)** - `905d9b4` (feat)
3. **Task 3: prove the pass union runs each pass exactly once, scoped by container family** - `3a48129` (test)

## Files Created/Modified

- `src/probe/pass.h` - `Pass`, `PassSet`, `ContainerFamily`, `ProbeResults`, `AnalyzerSpec`, `all_analyzers()`
- `src/probe/demux_session.{h,cpp}` - `DemuxSession`, `DemuxOptions`, wall-clock budget, libav log capture
- `src/probe/orchestrator.{h,cpp}` - `fingerprint_input`, `detail::run_probe`, `PassExecutionLog`
- `src/analyzers/container/{analyzers.h,topology.cpp}` - `container.format` analyzer
- `docs/checks/container.format.md` - required Accept/Tune/Silence doc
- `src/core/checks.def` - `container.format` registration
- `src/cli/commands/{snapshot,compare,dir,inspect}.cpp` - `fingerprint_input` rewiring + `--probe-timeout` wiring
- `src/cli/commands/compare.h` - `run_compare` gained a `ProbeArgs` parameter
- `src/cli/main.cpp` - installs the libav log callback exactly once
- `src/cli/options.{h,cpp}` - `ProbeArgs`, `add_probe_flags`, `resolve_probe_timeout_ms`, `default_probe_args`
- `src/cli/exit_code.h` - comment correction (worst_gating semantics)
- `src/config/toml_load.{h,cpp}` - `[probe] timeout_seconds`
- `src/report/model.{h,cpp}` - `accumulate()` bug fix (see Deviations)
- `CMakeLists.txt` - new sources/headers added to `libmediadiff`'s `FILE_SET`
- `scripts/gen_corpus.sh` - tracer fixture recipes
- `.gitignore` - `tests/fixtures/probe/` tracked exception
- `tests/unit/test_demux_session.cpp`, `tests/unit/test_pass_union.cpp` - new
- `tests/integration/test_probe_tracer.cpp` - new
- `tests/fixtures/probe/not_media.txt` - new, hand-authored, tracked
- `tests/unit/test_report_model.cpp` - `worst_gating` fix coverage
- `tests/baseline/report-1.0.json`, `tests/golden/{dir_worst_n,json_schema_basic,list_checks_effective}.txt` - regenerated (see Deviations)

## Decisions Made

- **JSON-shape peek for the fallthrough condition:** `fingerprint_input` distinguishes "JSON-shaped but explicitly rejected" from "not JSON at all" via `looks_like_json_document` (first non-whitespace byte is `{`) rather than sniffing `read_snapshot`'s error message text — discovered necessary when the naive "any `input_unsupported` falls through to a probe" design broke two pre-existing Phase-2 tests (schema-version-major-mismatch, type-poisoned-snapshot) that assert `read_snapshot`'s own diagnostic survives to stderr.
- **Runtime-mutable default budget, not a threaded parameter:** `DemuxOptions::wall_clock_budget_ms` defaults from `default_wall_clock_budget_ms()` (a relaxed atomic, write-once-per-invocation) rather than being threaded through `fingerprint_input`'s signature — keeps the plan's literal `fingerprint_input(path, registry)` signature frozen across all three tasks and means Task 2's CLI/config wiring never had to touch `orchestrator.cpp` again.
- **`all_analyzers()` built as an explicit hand-written list from Task 1**, not a self-registering-static list later replaced — Task 3 instead adds the test-injection seam (`detail::run_probe`) and the tests proving the property, since the "explicit list" design was already correct.
- **`AnalyzerSpec::name = "container_topology"`** (underscore), not the dotted `"container.format"` — `scripts/lint_check_id_strings.sh`'s D-03 scan flags any dotted-lowercase quoted string literal under `src/analyzers/` regardless of which field it initializes; the actual check id is referred to only through the generated `CheckId` enum in `run()`.
- **DemuxSession's exposed accessor surface is `format_name()`/`stream_count()`/`warning_count()` only** — per-stream `codecpar`, `programs()`, `chapters()` and per-stream/container metadata accessors (named in the plan's action text as eventual surface) are deferred to whichever plan first consumes them (03-04's topology/meta expansion), since no analyzer in this plan needs them and speculative accessors with no test coverage would be exactly the kind of unverified surface this project's discipline avoids.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking, cross-cutting] `report/model.cpp`'s `accumulate()` gated `worst_gating` on a check's declared severity regardless of finding status**
- **Found during:** Task 1's own acceptance criterion (`compare tracer_a.mp4 tracer_a_copy.mp4 --json` must exit `0`).
- **Issue:** Every comparator (`compare/exact.cpp`, `compare/tol.cpp`, ...) sets `Finding.severity` to the check's own resolved severity *unconditionally*, before deciding pass/warn/fail. `accumulate()` folded that severity into `Summary.worst_gating` even for a `Status::pass` finding, so `worst_gating` (and therefore the exit code) reflected "the ceiling severity of any check evaluated," not "did anything actually go wrong." `container.format` (severity=`fail`, passes whenever two files share a container family — the common case) is the first check in this codebase to expose this: no pre-Phase-3 check ever produced a `Status::pass` finding with a `fail`/`warn` severity through the real CLI path (`meta.tool_version` is `warn`-severity and exits 0 either way without `--strict`; `meta.missing_candidate`/`extra_candidate` are presence checks that never produce a `pass` finding at all in the matched-pair path). This behavior was also explicitly locked by an existing Phase-2 unit test (`report_model - worst_gating is the maximum severity across all findings, independent of status`), confirming it was deliberate, not an oversight — just never exercised end-to-end against a routinely-passing fail-severity check.
- **Fix:** `accumulate()` now only folds severity into `worst_gating` for a finding whose *status* itself signals a problem (`Status::warn`, `Status::fail`, or `Status::error` — CR-03's "never a fabricated verdict" overflow path must still gate). `Status::pass`/`info`/`skipped` never gate, regardless of the check's own ceiling severity. Updated the one Phase-2 test that encoded the old behavior (renamed/re-scoped it to cover the *actually-gating* case, plus two new tests: a `Status::pass` finding never gates, and `Status::error` still does) and the doc comments on `Summary::worst_gating` / `exit_code_for_findings`.
- **Files modified:** `src/report/model.cpp`, `src/report/model.h`, `src/cli/exit_code.h`, `tests/unit/test_report_model.cpp`, `tests/baseline/report-1.0.json`, `tests/golden/dir_worst_n.txt`, `tests/golden/json_schema_basic.txt` (regenerated via the project's own `UPDATE_GOLDENS=1` discipline — a deliberate, reviewed change to a pre-1.0 placeholder baseline, not drift).
- **Verification:** All 340 tests pass (`ctest --test-dir build/x64-linux --output-on-failure`); `tests/golden/list_checks_effective.txt` also gained a `container.format` row (unrelated to this fix — that's the new check's own registration).
- **Committed in:** `f79c07b` (standalone fix commit, before the tracer feature commit).
- **Recorded:** `.planning/WINDOWS.md` entry #2 (`kind: deviation`, immediately marked `fixed` since the fix landed in this same plan).

**2. [Rule 1 - Bug, in new code before it ever shipped] `fingerprint_input`'s naive fallthrough broke two pre-existing Phase-2 tests**
- **Found during:** First full `ctest` run after Task 1's implementation.
- **Issue:** The literal plan text ("falls through to the probe path ONLY when the error kind is `input_unsupported`") is too coarse — `read_snapshot` returns `ErrorKind::input_unsupported` for three distinct causes (not JSON at all, a schema_version major mismatch, an unregistered check ID), and the naive rule reinterpreted the latter two as "try probing it as media," masking `read_snapshot`'s own, more informative rejection with a generic `DemuxSession::open` failure.
- **Fix:** Added `looks_like_json_document` (first-non-whitespace-byte-is-`{`) as an additional gate before falling through — see Decisions above.
- **Files modified:** `src/probe/orchestrator.{h,cpp}`.
- **Verification:** `tests/integration/test_schema_version.cpp` and `tests/integration/test_type_poisoned_snapshot.cpp` (both pre-existing, unmodified) pass unchanged.
- **Committed in:** `905d9b4` (part of the tracer feature commit — discovered and fixed before the first commit of this work, so it was never a separate historical state).

**3. [Rule 3 - Blocking, lint] `AnalyzerSpec::name`'s dotted literal tripped `scripts/lint_check_id_strings.sh`**
- **Found during:** Running the required lint scripts before finalizing.
- **Issue:** `container_topology_analyzer()`'s `AnalyzerSpec::name` field was initialized with the literal `"container.format"` — a dotted-lowercase quoted string under `src/analyzers/`, which the D-03 lint flags regardless of which field it initializes.
- **Fix:** Renamed the label to `"container_topology"` (underscore) with a comment explaining why; the actual check id is referred to only through the generated `CheckId` enum in `run()`.
- **Files modified:** `src/analyzers/container/topology.cpp`.
- **Verification:** `bash scripts/lint_check_id_strings.sh` exits 0.
- **Committed in:** `905d9b4`.

---

**Total deviations:** 3 auto-fixed (1 cross-cutting Rule-3 blocking fix touching Phase-2-owned files, 2 narrower Rule-1/Rule-3 fixes fully within this plan's own new code).
**Impact on plan:** All three were necessary for this plan's own literal acceptance criteria to be satisfiable at all. The `worst_gating` fix has a real, positive impact beyond this plan (restores the project's stated Core Value for every future fail-severity check that commonly passes) but touches files outside this plan's nominal scope (`src/report/model.{h,cpp}`) — flagged prominently here and in `.planning/WINDOWS.md` rather than buried in a routine deviation note, given its magnitude.

## Issues Encountered

- **GCC `-O3 -Wmaybe-uninitialized` false positive on `Value`'s `std::variant`:** constructing a `Measurement` (whose `Value` includes a `std::set` alternative) and `push_back`-ing it into a `std::vector` inside a small, fully-inlined test translation unit (`tests/unit/test_pass_union.cpp`) triggered a compiler false positive unrelated to the test's actual intent (`src/analyzers/container/topology.cpp` constructs the same type in a larger TU with no such warning). Worked around by having the synthetic test analyzer set a plain `bool` instead of constructing a `Measurement` — sufficient to prove "did `run()` get called," the only thing that test needed. Not a real bug; documented inline in the test file.
- **Log-capture unit tests use a direct `av_log()` call, not a fixture:** no deterministic recipe for making a real, `-flags +bitexact`-synthesized fixture reliably trigger an `AV_LOG_WARNING` line was identified within this plan's scope. `tests/unit/test_demux_session.cpp`'s log-capture tests instead install the real callback and invoke `av_log()` directly, proving the exact counting/thread-local-attribution mechanism `DemuxSession::open` uses internally, deterministically and fast. `DemuxSession::warning_count()` on a real, clean tracer fixture is separately asserted to be `0`.
- **Retained-line-text simplification:** the plan's action text says the log callback should "cap the retained line count, keep counting past the cap." No consumer of retained line *text* exists yet (only the total count folds into `Fingerprint.envelope.diagnostics`), so `ProbeDiagnostics` retains no line text at all (cap = 0, always counting past it) — simpler than building unused storage. A future plan that wants retained diagnostic text extends `ProbeDiagnostics` rather than reinventing the counting half.
- **Plan's per-task `<files>` lists undercounted some files** (e.g. Task 2's list omits the four `src/cli/commands/*.cpp` files, `src/probe/orchestrator.{h,cpp}`, despite Task 2's own action text requiring `--probe-timeout` registered on all four commands and the libav diagnostics folded into the envelope those files/functions own). All such files ARE present in the plan's top-level `files_modified` frontmatter list, so this is treated as a per-task shortlist imprecision, not scope creep — consistent with 03-01-SUMMARY.md's own precedent for this class of discrepancy.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `DemuxSession`, `PassSet`/`AnalyzerSpec`/`all_analyzers()`, and `fingerprint_input` are proven end-to-end and ready for 03-03's `PacketScan` and 03-04's `container.track_count`/`track_types`/`track_order`/`chapters`/`meta.tags`/`meta.tags.language` expansion.
- `DemuxSession`'s accessor surface (`format_name()`, `stream_count()`, `warning_count()`) will need per-stream `codecpar` access, `programs()`, `chapters()`, and metadata accessors before 03-04 can implement its own checks — flagged as a known, deliberate gap (see Decisions), not a blocker, since 03-04 is the plan that will actually consume them.
- The wall-clock budget and libav log-capture mechanism are complete (`kDefaultProbeBudgetMs`, `--probe-timeout`, `[probe] timeout_seconds`, thread-local attribution) — 03-03's `PacketScan` inherits the same `DemuxSession` session, no new budget plumbing needed.
- `--probe-memory-budget-mb` is registered and CLI11-validated but genuinely unconsumed, exactly as instructed — 03-03 (D-01's global memory-budget model) is expected to wire it up.
- No blockers identified for 03-03.

## Self-Check: PASSED

- `src/probe/pass.h` — FOUND
- `src/probe/demux_session.h` — FOUND
- `src/probe/demux_session.cpp` — FOUND
- `src/probe/orchestrator.h` — FOUND
- `src/probe/orchestrator.cpp` — FOUND
- `src/analyzers/container/analyzers.h` — FOUND
- `src/analyzers/container/topology.cpp` — FOUND
- `docs/checks/container.format.md` — FOUND
- `tests/unit/test_demux_session.cpp` — FOUND
- `tests/unit/test_pass_union.cpp` — FOUND
- `tests/integration/test_probe_tracer.cpp` — FOUND
- `tests/fixtures/probe/not_media.txt` — FOUND
- `f79c07b` — FOUND in `git log --oneline --all`
- `905d9b4` — FOUND in `git log --oneline --all`
- `3a48129` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
