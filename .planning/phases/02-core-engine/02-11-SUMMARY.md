---
phase: 02-core-engine
plan: 11
subsystem: cli
tags: [dir-mode, worker-pool, corpus-report, json-schema, junit, markdown, tty, policy-resolution]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 10)
    provides: "src/cli/commands/dir.h's registered subcommand (positionals, --threads, --content/--no-content, add_common_options) with the ErrorKind::internal stub this plan replaces; src/cli/exit_code.h's exit_code_for/exit_code_for_findings, reused unchanged for the corpus's own exit-code decision"
  - phase: 02-core-engine (plan 08)
    provides: "src/report/model.h's ReportModel/Summary/GroupBlock/build_report_model/is_gating/scope_to_text -- build_corpus_model calls build_report_model once per file and flattens the result rather than re-deriving grouping/ordering; the four renderers' existing single-file logic is extended, not replaced"
  - phase: 02-core-engine (plan 06)
    provides: "src/core/policy.h's Policy/ResolvedCheck/PolicyProvenance, resolve_policy's apply_severity_rules/apply_tolerance_rules internals (now shared with resolve_policy_for_file), and config/toml_load.h's OverrideBlock/ConfigFile -- this plan's per-file policy resolution is layer three (path-scoped overrides) resolve_policy's own comment had explicitly deferred to this phase"
provides:
  - "src/cli/dir_pairing.h/.cpp: pair_directories(baseline_root, candidate_root) -- the full, byte-wise sorted FilePair vector, computed once before any worker starts; symlinks never followed; a walk-depth/entry-count bound"
  - "src/cli/worker_pool.h/.cpp: WorkerPool(thread_count).run_indexed(job_count, job) -- 0/1 runs synchronously with no thread created; a thrown exception is contained at the pool boundary and never propagates"
  - "src/core/glob.h/.cpp: glob_matches_path/validate_path_glob -- the same no-regex, segment-wise grammar as the existing check-id glob_matches/validate_glob, split on '/' instead of '.', for `[override.\"<path-glob>\"]` blocks"
  - "src/core/policy.h/.cpp: resolve_policy_for_file(base, registry, config, relative_path, cli_overrides) -- derives one file's own Policy from a shared, read-only base plus path-matching override blocks (config layer) and cli_overrides (layer four), mutating nothing shared"
  - "src/config/toml_load.h/.cpp: DirBlock::threads -- `[dir] threads` parsed and validated positive at config-load time (previously an empty placeholder struct)"
  - "src/report/model.h/.cpp: FileResult/FileBlock/CorpusModel/build_corpus_model/combine_summary -- the corpus-scale sibling of ReportModel, built by calling build_report_model once per file"
  - "src/report/json.{h,cpp}, markdown.{h,cpp}, junit.{h,cpp}, src/cli/tty_render.{h,cpp}: CorpusModel overloads -- JSON's files[] layer, Markdown's per-file summary table, one JUnit <testsuite> per file, TTY's per-file summary lines plus a worst-N table"
  - "docs/schema/report-1.0.json: extended with a top-level oneOf(findings, files) and a shared $defs/summary, so the single-file document's own shape is unchanged and a corpus document validates too"
  - "src/cli/commands/dir.cpp: real DIR-01..05 orchestration replacing the 02-10 stub -- the shared base Policy and the full sorted pair list resolved once, one WorkerPool job per pair, index-addressed results, a structural exit-code priority (hard error > any-partial > findings-based) decided only after every requested report is written"
affects: ["This is the final plan of Phase 2 -- no downstream Phase 2 plan depends on this one. Phase 3's probe layer is what --content's own diagnostic (\"accepted but has no effect yet\") names as the future consumer of DIR-02's pass-selection flag."]

# Actuals (#2632)
actuals:
  tokens: 28380
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: ["CMake Threads package (find_package(Threads REQUIRED), Threads::Threads) -- src/cli/worker_pool.cpp's plain std::thread needs an explicit pthread link on some libstdc++ configurations"]
  patterns:
    - "A per-file corpus computation reuses the EXISTING single-file build_report_model/render_* logic rather than a parallel implementation: build_corpus_model calls build_report_model once per FileResult and flattens the GroupBlock[] it returns into one FileBlock; render_markdown's fold/budget machinery threads an extra_section parameter through so the corpus overload can insert its own per-file table without touching the fold algorithm; render_junit's corpus overload reuses the exact same per-testcase element-shape helper, only changing what becomes the <testsuite> boundary (file instead of group)"
    - "A worker-pool job never mutates shared state: WorkerPool::run_indexed's job closure reads a single shared, immutable base Policy by const reference and calls resolve_policy_for_file to get its OWN Policy copy every time, rather than any job writing into a shared Policy or a shared ConfigFile's override list"
    - "An unpaired file's meta.missing_candidate/meta.extra_candidate finding is synthesized via the EXISTING compare_presence comparator (compare/presence.cpp, unchanged) over two hand-built Measurements representing the file's own presence/absence, rather than a bespoke finding-construction path -- this is what makes `--set meta.extra_candidate=ignore` behave identically to overriding any other check"
    - "dir mode's own exit-code decision is a structural priority computed AFTER every requested report has been written (mirroring compare.cpp's own partial-decode pattern): the first hard per-file error (in pairing order) wins over any-file-partial (kExitDecode) wins over the ordinary findings-based decision -- never an early abort that discards other files' already-computed results"

key-files:
  created:
    - src/cli/dir_pairing.h
    - src/cli/dir_pairing.cpp
    - src/cli/worker_pool.h
    - src/cli/worker_pool.cpp
    - tests/unit/test_dir_pairing.cpp
    - tests/unit/test_worker_pool.cpp
    - tests/integration/test_dir_mode.cpp
    - tests/golden/dir_worst_n.txt
  modified:
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - src/core/glob.h
    - src/core/glob.cpp
    - src/core/policy.h
    - src/core/policy.cpp
    - src/config/toml_load.h
    - src/config/toml_load.cpp
    - src/report/model.h
    - src/report/model.cpp
    - src/report/json.h
    - src/report/json.cpp
    - src/report/markdown.h
    - src/report/markdown.cpp
    - src/report/junit.h
    - src/report/junit.cpp
    - src/cli/tty_render.h
    - src/cli/tty_render.cpp
    - src/cli/commands/dir.cpp
    - docs/schema/report-1.0.json
    - tests/integration/test_exit_codes.cpp

key-decisions:
  - "dir mode's per-pair compare pipeline is a fresh, direct sequence of read_snapshot -> resolve_policy_for_file -> compare_fingerprints, NOT a literal call into src/cli/commands/compare.cpp's run_compare() -- that function is [[noreturn]] (it writes reports and calls std::exit() once, per single compare), which is structurally incompatible with aggregating N files into ONE corpus report and exiting ONCE at the end. PLAN.md's own Task action text and key_links name only pair_directories/WorkerPool/CorpusModel, never run_compare(); the orchestrator's prior_wave_context blurb's 'same function, not a copy' language is honored in spirit by calling the exact same underlying library functions run_compare() itself calls (read_snapshot, the resolve_policy family, compare_fingerprints), never a parallel reimplementation of any of them"
  - "Per-file Policy resolution splits the four-layer chain into a shared, path-independent BASE (builtin -> profile -> config's own top-level [severity]/[tolerance], config's [override.*] blocks and --set/--tol deliberately excluded) computed once before the pool starts, plus resolve_policy_for_file applying path-matching override blocks (config layer) then cli_overrides (layer four, always last) onto a COPY of that base -- this is what makes 'no job mutates shared state' true by construction rather than by convention, and correctly reproduces doc 01 section 6's layer order per file (previously resolve_policy applied every [override.*] block unconditionally, having no relative path to filter by -- deferred to this plan by 02-06's own design comment)"
  - "A hard per-file error or a partial-decode marker never aborts the corpus run mid-flight: every OTHER file's own result still lands in the report, and the run's exit code escalates (first hard error in pairing order > any-partial > ordinary findings-based decision) only after every requested report destination has been written -- mirrors compare.cpp's own 'finish building the report, write every destination, THEN exit specially' pattern for its own partial-decode case, generalized across a corpus"
  - "A genuine internal-error path for CLI-06's exit-70 contract is a test-only environment variable (MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR) a per-file job checks first, before any real work -- re-points 02-10's own exit-70 assertion (previously only reachable via the now-removed dir stub) at real orchestration rather than leaving that contract point permanently unobservable"
  - "The extended docs/schema/report-1.0.json uses a top-level oneOf({required:[findings]}, {required:[files]}) plus a shared $defs/summary $ref (both the top-level summary and each file_block's own summary reference it) -- keeps the single-file document's own shape byte-for-byte unchanged (verified: every pre-existing integration.json_schema test still passes unmodified) while a corpus document validates under the SAME schema file, never a second schema"
  - "core/glob.h's new path-glob matcher (glob_matches_path/validate_path_glob) is a deliberately SEPARATE pair of functions from the existing dot-delimited glob_matches/validate_glob, not a delimiter-parameterized refactor of the same internals -- avoids any risk to the existing, already-tested check-id glob matcher for a plan whose own scope is dir-mode orchestration, not a glob-engine rewrite"

patterns-established:
  - "A corpus-scale report model is built by calling the existing single-file model-builder once per corpus item and flattening/aggregating its output (build_corpus_model calling build_report_model per FileResult), rather than teaching the model-builder itself a second, corpus-aware code path -- the single-file and corpus shapes share one grouping/ordering/summarising implementation for the lifetime of the project"

requirements-completed: [DIR-01, DIR-02, DIR-03, DIR-04, DIR-05]

coverage:
  - id: D1
    description: "pair_directories walks both trees recursively, pairs by relative path, returns the union in byte-wise sorted (never locale-aware) order computed once before any worker starts; symlinks are never followed as a leaf or during recursion; a nonexistent/non-directory root is ErrorKind::input_open; two empty roots are not an error; a non-ASCII filename pairs correctly; separators are normalised to forward slash on every platform"
    requirement: "DIR-01, DIR-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_dir_pairing.cpp (11/11 pass, tag [dir])"
        status: pass
    human_judgment: false
  - id: D2
    description: "An unpaired baseline-only file produces exactly one meta.missing_candidate finding (severity fail); an unpaired candidate-only file produces exactly one meta.extra_candidate finding (severity warn); both are synthesized through the real compare_presence comparator and the real resolved per-file Policy, so `--set meta.extra_candidate=ignore` resolves it to ignore like any other check override"
    requirement: "DIR-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp ('one unpaired file each way exits 1 and the files[] array contains both meta findings under the right relative paths')"
        status: pass
      - kind: other
        ref: "./build/x64-linux/mediadiff list-checks --effective --set 'meta.extra_candidate=ignore' shows that check resolved to ignore"
        status: pass
    human_judgment: false
  - id: D3
    description: "WorkerPool::run_indexed runs every index exactly once across a range of thread/job counts including 0 and 1; a peak in-flight counter never exceeds the configured thread count across three thread counts; a thread count of one (and zero) creates no std::thread; results written by index stay in submission order despite deliberately inverted per-job completion delays; a throwing job is contained and does not prevent the remaining jobs from running"
    requirement: "DIR-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_worker_pool.cpp (6/6 pass, tag [pool])"
        status: pass
    human_judgment: false
  - id: D4
    description: "--threads 1 and --threads 8 produce byte-identical --json output on a 10-file corpus (compared as full strings); files[] is in byte-wise sorted relative-path order; corpus totals equal the element-wise sum of every file's own summary; two empty roots exit 0 with files: []; a hard per-file error (proven via a test-only injection env var) still lets the run complete and escalates the exit code to 70 only afterward"
    requirement: "DIR-01, DIR-03, DIR-04, DIR-05"
    verification:
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp (9/9 pass, tag [integration]); tests/integration/test_exit_codes.cpp's re-pointed exit-70 case"
        status: pass
    human_judgment: false
  - id: D5
    description: "The corpus JSON document (files[], each file's own relative_path/summary/findings using the exact single-file finding schema) validates against the extended docs/schema/report-1.0.json; the pre-existing single-file schema tests still pass unmodified; Markdown gains a per-file summary table; JUnit emits one <testsuite> per file named by relative path; TTY gains a per-file summary line plus a worst-N table (worst severity first, ties broken by relative path), golden-checked"
    requirement: "DIR-03"
    verification:
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp (schema validation, JUnit-per-file, and TTY worst-N golden cases); tests/integration/test_json_schema.cpp (6/6 pass, unmodified)"
        status: pass
    human_judgment: false

duration: ~3h10min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 11: Dir Mode — Pairing, Bounded Parallelism, and the Corpus Report Model Summary

**`dir` mode is real: a corpus pairs by relative path in byte-wise sorted, platform-independent order; unpaired files become policy-resolvable `meta.missing_candidate`/`meta.extra_candidate` findings; a `--threads`-bounded worker pool runs every pair with index-addressed results that are structurally decoupled from completion order (`--threads 1` and `--threads 8` produce byte-identical `--json`); and corpus totals, a `files[]` JSON layer, a per-file Markdown table, one JUnit `<testsuite>` per file and a TTY worst-N table all render from one shared model.**

## Performance

- **Duration:** ~3h10min
- **Started:** 2026-08-15 (continuing directly from 02-10)
- **Completed:** 2026-08-15
- **Tasks:** 3
- **Files modified:** 30 (8 created, 22 modified)

## Accomplishments

- `src/cli/dir_pairing.{h,cpp}`: `pair_directories(baseline_root, candidate_root)` walks both trees recursively (never following a symlink, bounded by a maximum entry count and depth), pairs entries by relative path, and returns the full union in byte-wise sorted order — computed once, before any worker starts, and treated as `const` from that point on. Relative paths are always forward-slash-separated regardless of platform (`std::filesystem::path::generic_string()`), and UTF-8 conversion goes through `util/fs.h`'s existing `utf8_to_wide`/`wide_to_utf8` on Windows rather than `std::filesystem::path`'s own narrow-string constructor.
- `src/cli/worker_pool.{h,cpp}`: `WorkerPool(thread_count).run_indexed(job_count, job)` — a fixed-size pool over a pre-sorted, index-addressed job list. A thread count of 0 or 1 runs every job on the calling thread with no `std::thread` created; any other value spawns exactly that many workers pulling the next unclaimed index from a shared atomic counter. A job's thrown exception is caught at the pool boundary and never crosses back out, and never prevents any other index's job from running.
- `src/core/glob.{h,cpp}` gains `glob_matches_path`/`validate_path_glob`: the same no-regex, segment-wise grammar the existing check-id matcher uses, split on `/` instead of `.`, for `[override."<path-glob>"]` blocks — a deliberately separate implementation from the existing `glob_matches`/`validate_glob` so neither the check-id matcher's own tests nor its callers are put at any risk.
- `src/core/policy.{h,cpp}` gains `resolve_policy_for_file(base, registry, config, relative_path, cli_overrides)`: derives one file's own `Policy` from a shared, read-only base plus every path-matching `[override.*]` block (config layer) then `cli_overrides` (layer four, always last), onto a *copy* of the base — no job ever mutates shared state. `resolve_policy`'s own CLI-override tail was extracted into a shared `apply_cli_overrides` helper both functions now call, so the two entry points cannot independently drift.
- `src/config/toml_load.{h,cpp}`'s `DirBlock` gains `threads` (`[dir] threads`, validated positive at load time) — previously an empty placeholder struct declared before this plan existed to fill it.
- `src/report/model.{h,cpp}` gains `FileResult`/`FileBlock`/`CorpusModel`/`build_corpus_model`/`combine_summary`: the corpus-scale sibling of `ReportModel`, built by calling `build_report_model` once per file and flattening its `GroupBlock[]` into one `FileBlock` — the single-file and corpus paths share one grouping/ordering pass rather than two that could drift. `combine_summary` is the corpus totals' element-wise sum, exposed so its own correctness is directly testable.
- `src/report/json.{h,cpp}`, `markdown.{h,cpp}`, `junit.{h,cpp}`, `src/cli/tty_render.{h,cpp}` each gain a `CorpusModel` overload: JSON's `files[]` layer reuses the exact single-file `finding_to_json`; Markdown's fold/budget algorithm was refactored to thread an `extra_section` parameter through (the per-file summary table, never subject to the fold) rather than forking the algorithm; JUnit emits one `<testsuite>` per file (`name` = relative path, `classname` stays each finding's own group); TTY gains a per-file summary line plus a worst-N table (worst `worst_gating` first, ties broken by relative path — a total, deterministic order).
- `docs/schema/report-1.0.json`: extended with a top-level `oneOf({required:[findings]}, {required:[files]})` and a shared `$defs/summary`/`$defs/file_block` — the pre-existing single-file document shape is byte-for-byte unchanged (every `integration.json_schema` test still passes verbatim) while a corpus document validates under the same schema file.
- `src/cli/commands/dir.cpp`: real orchestration replacing the 02-10 stub. Resolves the shared base `Policy` and the full sorted pair list once; runs one `WorkerPool` job per pair with index-addressed `FileResult`/bookkeeping vectors; a hard per-file error or a partial-decode marker never discards any other file's own result — the exit-code decision (first hard error in pairing order > any-file-partial (66) > the ordinary findings-based 0/1/2 decision) is made only after every requested report destination has been written.

## Task Commits

1. **Task 1: Directory pairing and the bounded worker pool** — `06be615` (feat)
2. **Task 2: Per-file policy resolution for dir mode** — `18ad971` (feat)
3. **Task 3: dir-mode orchestration, corpus report model and renderers** — `c747a40` (feat)

Commits are staged by genuine file-dependency order rather than PLAN.md's own literal per-task `files_modified` split (see Deviations): `dir_pairing`/`worker_pool` (Task 1's own infra plus Task 2's own worker mechanism, since neither depends on `dir.cpp` and both build/test independently with the pre-existing stub still in place) landed first; `core/glob`/`core/policy`/`config/toml_load`'s per-file policy resolution landed second (also independently buildable, additive-only); the report model, all four renderer extensions, the schema, and `dir.cpp`'s own rewrite — the point at which `dir` mode actually becomes real — landed third. Each commit was verified against the FULL test suite (`ctest --test-dir build/x64-linux`) rather than only its own task's `<verify>` filter, since Tasks 1–3 turned out to be too tightly coupled at the `dir.cpp` call-site level for a strictly incremental per-task `dir.cpp` to be a meaningful intermediate state (see Deviations).

## Files Created/Modified

- `src/cli/dir_pairing.{h,cpp}`, `worker_pool.{h,cpp}` — new
- `src/core/glob.{h,cpp}` — `glob_matches_path`/`validate_path_glob` added
- `src/core/policy.{h,cpp}` — `resolve_policy_for_file`, `apply_cli_overrides` extraction
- `src/config/toml_load.{h,cpp}` — `DirBlock::threads`
- `src/report/model.{h,cpp}` — `FileResult`/`FileBlock`/`CorpusModel`/`build_corpus_model`/`combine_summary`
- `src/report/json.{h,cpp}`, `markdown.{h,cpp}`, `junit.{h,cpp}`, `src/cli/tty_render.{h,cpp}` — `CorpusModel` overloads
- `src/cli/commands/dir.cpp` — full rewrite, real orchestration
- `docs/schema/report-1.0.json` — `oneOf(findings, files)`, shared `$defs/summary`/`file_block`
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` — new sources, `Threads::Threads`
- `tests/unit/test_dir_pairing.cpp` (11 tests), `test_worker_pool.cpp` (6 tests) — new
- `tests/integration/test_dir_mode.cpp` (9 tests) — new
- `tests/integration/test_exit_codes.cpp` — exit-70 case re-pointed at a real internal-error path
- `tests/golden/dir_worst_n.txt` — new golden fixture for the TTY worst-N table

## Decisions Made

See frontmatter `key-decisions` for full rationale on: not calling `run_compare()` literally (structurally impossible for a `[[noreturn]]`, exit-once-per-invocation function to aggregate N files); the base-Policy-plus-per-file-overrides split that makes "no job mutates shared state" true by construction; the finish-then-escalate exit-code pattern for a per-file hard error or partial marker; the test-only internal-error injection env var; the schema's `oneOf` extension; and keeping the new path-glob matcher structurally separate from the existing check-id one.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `src/config/toml_load.{h,cpp}` needed `DirBlock::threads` added, though neither file was in any task's own `files_modified` list**
- **Found during:** Task 2, implementing `--threads`'s own resolution precedence
- **Issue:** Task 2's action text explicitly requires resolving the default thread count from `[dir] threads` in `mediadiff.toml` when present — but `DirBlock` was (deliberately, per its own 02-06-era comment) an empty placeholder struct with no `threads` field to read.
- **Fix:** Added `std::optional<int> threads` to `DirBlock`, parsed and validated positive at config-load time (`config/toml_load.cpp`'s existing `[dir]` table handling), matching every other config value's "validated once, at load time" contract.
- **Files modified:** `src/config/toml_load.h`, `src/config/toml_load.cpp`.
- **Commit:** `18ad971` (Task 2's own commit).

**2. [Rule 3 - Blocking] `src/core/glob.{h,cpp}` needed a path-glob matcher added, though neither file was in any task's own `files_modified` list**
- **Found during:** Task 2, implementing `resolve_policy_for_file`'s own `[override."<path-glob>"]`-block matching
- **Issue:** Applying an override block "per file by resolving a per-file policy... plus the overrides matching its relative path" (Task 2's own action text) requires evaluating a path glob against a relative path — the existing `glob_matches`/`validate_glob` split on `.` for a check id, an unrelated grammar to a `/`-delimited corpus path.
- **Fix:** Added `glob_matches_path`/`validate_path_glob`, the same no-regex segment-wise algorithm with `/` as the delimiter — a deliberately separate pair of functions from the existing dot-delimited ones, so neither the check-id matcher's own tests nor its callers are put at any risk.
- **Files modified:** `src/core/glob.h`, `src/core/glob.cpp`.
- **Commit:** `18ad971` (Task 2's own commit).

**3. [Rule 3 - Blocking, scoped] `src/core/policy.cpp`'s CLI-override tail extracted into a shared `apply_cli_overrides` helper**
- **Found during:** Task 2, implementing `resolve_policy_for_file`, which needs to apply layer four (CLI) in the SAME order/shape `resolve_policy`'s own tail already does
- **Issue:** Duplicating the ~15-line CLI-override loop verbatim into a second function would risk the two entry points drifting on a future change to that layer's own logic.
- **Fix:** Extracted the existing loop into `apply_cli_overrides(Policy&, registry, cli_overrides)`, called by both `resolve_policy`'s own tail (behavior unchanged, verified against every pre-existing `test_policy_merge.cpp` case) and the new `resolve_policy_for_file`.
- **Files modified:** `src/core/policy.cpp` (already in Task 2's own `files_modified` list; the extraction itself was not separately anticipated by the plan's action text but is the minimal DRY choice for the mechanism that text does require).
- **Commit:** `18ad971` (Task 2's own commit).

**4. [Process deviation, not a scope change] Commits staged by genuine buildability rather than PLAN.md's literal per-task `files_modified` split**
- **Found during:** planning the 3 commits after all code was written and the full suite verified green
- **Issue:** `src/cli/commands/dir.cpp` and the root/`tests/unit` `CMakeLists.txt` files appear in more than one task's own `files_modified` list. Splitting their diffs strictly by task boundary would have produced two intermediate commits that do not compile (`dir.cpp`'s Task 1/2 states would reference `WorkerPool`/`CorpusModel` types that do not exist until Task 3's own files land) — the opposite of the "each task's commit independently built" convention prior plans in this phase established.
- **Fix:** Re-grouped the three commits by actual dependency order instead: (1) `dir_pairing`+`worker_pool` (self-contained, `dir.cpp` untouched, still 02-10's stub — genuinely builds and runs its own unit tests standalone); (2) `core/glob`+`core/policy`+`config/toml_load`'s per-file policy resolution (additive-only, no existing call site changes behavior, also genuinely standalone-buildable); (3) the report model, every renderer's `CorpusModel` overload, and `dir.cpp`'s own rewrite together (the point at which `dir` mode becomes real). Each commit was verified against the FULL test suite, not a per-task filtered subset, since the per-task subsets themselves are not meaningful compile units given this dependency shape.
- **Files modified:** none beyond what each task's own action text already required — this is a staging/commit-grouping decision, not a code or scope change.
- **Commit:** all three (`06be615`, `18ad971`, `c747a40`).

---

**Total deviations:** 4 (3 Rule 3 — blocking, all minimal additions the plan's own stated mechanisms structurally required; 1 process deviation in how the 3 commits were grouped, disclosed above rather than silently departing from the plan's literal task-to-file mapping)
**Impact on plan:** None change scope, architecture, or any frozen contract from prior waves; all three code deviations are the minimal surface needed for this plan's own explicitly-stated mechanisms to compile and be testable at all, and the commit-grouping deviation changes only how the diff is organized in git history, not what the diff contains or how it was verified (full suite, every commit).

## Known Stubs

- **`--content` is plumbing only, matching this plan's own explicitly Flagged Assumption.** Setting it prints a diagnostic ("accepted but has no effect yet — the decode pass arrives with a later phase") and the run still completes on the default header-plus-packet pass set. Not a gap introduced by this plan — the decode pass itself does not exist until Phase 3's probe layer lands, and no acceptance criterion in this plan requires it to.
- **`resolve_policy_for_file` has no dedicated unit test file of its own.** Its behavior (path-scoped override application, correct four-layer ordering) is exercised end-to-end through `tests/integration/test_dir_mode.cpp`'s real `dir` invocations rather than a synthetic unit test constructing a `ConfigFile`/`OverrideBlock` by hand — this plan's own task list did not name a `test_policy_merge.cpp` addition, and the integration-level coverage (a real corpus with a path-scoped override) is the more representative proof of the actual doc 01 section 6 contract this function implements. Flagged here per the stub-tracking convention; a future plan touching `resolve_policy_for_file` directly should add unit-level coverage before extending it further.

## Issues Encountered

- **`ctest -R unit.pool` initially matched zero tests.** `ctest -R` is a regex, not a literal substring match — `unit.pool`'s embedded `.` matches any single character, so it requires EXACTLY one character between `unit` and `pool`. The first draft of `test_worker_pool.cpp` named every case `"worker_pool: ..."`, producing full names like `unit.worker_pool: ...` (multiple characters, `worker_`, between `unit` and `pool` — no match). Renamed every case to `"pool - ..."` (matching `test_report_model.cpp`'s own `"report_model - ..."` precedent for the sibling `-R unit.report` filter), which resolved it.
- **`docs/checks/meta.missing_candidate.md`/`meta.extra_candidate.md` needed no edits.** Both were already written, in a prior wave, to describe the `dir`-mode corpus case concretely (naming `[severity]` overrides, corpus-shrink/grow scenarios) — `checks.def`'s own comment confirms both checks were "approved for 02-11" ahead of this plan landing. This plan's own Task 1 action text asked to "update" them; no functional or textual gap was found, so neither file was touched.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Phase 2 (core-engine) is now complete: all 11 plans landed, all 48 requirements covered, `ctest` 285/285 green (up from the 259 baseline at this plan's start).
- `dir` mode's own compare pipeline (`read_snapshot` -> `resolve_policy_for_file` -> `compare_fingerprints`) is exactly the same set of library functions Phase 3's probe layer will need to route real media through once `--content`/the default pass set stop being plumbing-only — no dir-mode-specific rework anticipated when that lands.
- `CorpusModel`/`build_corpus_model` and the four renderers' `CorpusModel` overloads are stable, tested surfaces; a later phase adding a new check family needs no dir-mode-specific work to have it show up correctly in a `files[]` report — `build_report_model`'s own grouping/ordering already covers any new `Group` a future check's id resolves to.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 8 created files verified present on disk; all 3 task commit hashes (`06be615`, `18ad971`, `c747a40`) verified present in `git log --oneline --all`.
