---
phase: 02-core-engine
plan: 08
subsystem: report
tags: [report-model, json-schema, markdown, junit, cli-flags, json-schema-validator]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 01)
    provides: "core/model.h's Finding/Status/Severity/SkipReason, core/serializer.h's value_to_json/serialize_document (D-08's single canonical Value<->JSON conversion), and the pre-existing render_json stub this plan rewrites"
  - phase: 02-core-engine (plan 05)
    provides: "core/policy.h's Policy/ResolvedCheck/PolicyProvenance and resolve_severity's per_check-authoritative dual-mode -- this plan's JSON severity_chain and accurate per-finding tolerance both read policy.per_check directly"
  - phase: 02-core-engine (plan 06)
    provides: "The full four-layer resolve_policy (config/CLI included), src/cli/options.h's PolicyArgs/add_policy_flags/parse_cli_overrides, and src/cli/provenance_render.cpp's text severity-chain renderer this plan's JSON severity_chain mirrors field-for-field"
  - phase: 02-core-engine (plan 07)
    provides: "src/core/serializer.h's value_to_json (reused by both json.cpp and junit.cpp's baseline/candidate rendering), the full Envelope shape, and tests/baseline/report-1.0.json (the frozen TRUST-08 oracle this plan's own schema change required regenerating)"
provides:
  - "src/report/model.{h,cpp}: the shared ReportModel every renderer (JSON, Markdown, JUnit -- TTY in a later plan) builds from -- Group/group_for/group_to_string, Summary, RenderOptions, GroupBlock, build_report_model, is_gating, scope_to_text"
  - "src/report/json.{h,cpp}: render_json(ReportModel, CheckRegistry, Policy, verbose) against the fixed doc-01-section-9 key order, with accurate per-finding tolerance/unit sourced from the real resolved Policy and an optional severity_chain under -v"
  - "docs/schema/report-1.0.json: the shipped JSON Schema (draft 2020-12), additionalProperties:false at the finding level, proven in CI to actually reject a malformed document"
  - "src/report/markdown.{h,cpp}: render_markdown -- summary table + per-group <details>, kMarkdownBudgetBytes=60000 (UTF-8 bytes, headroom under GitHub's 65,536-character limit), the lowest-severity-first overflow fold that never drops a Severity::fail finding"
  - "src/report/junit.{h,cpp}: render_junit -- one <testsuite> per non-empty group, one <testcase> per gating-capable finding, --strict-sensitive warn handling, full XML escaping"
  - "src/cli/options.h's ReportArgs/ReportDestination/add_report_flags/parse_report_destinations, and compare's `--json[=path]` + repeatable `--report <md|junit>=<path>` end-to-end wiring through util/fs.h's fopen_utf8"
affects: ["02-09/02-10 (dir mode and inspect can now assume ReportModel/render_json/render_markdown/render_junit exist and take a resolved Policy)", "A later TTY renderer plan reuses RenderOptions.show_pass=false for doc 01's 'only non-pass by default' contract, which this plan defines but never sets"]

# Actuals (#2632)
actuals:
  tokens: 28735
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: ["json-schema-validator (find_package(nlohmann_json_schema_validator CONFIG REQUIRED), nlohmann_json_schema_validator::validator) -- linked into tests/integration only, never into libmediadiff or mediadiff"]
  patterns:
    - "ReportModel is the single grouping/ordering/filtering/summarisation pass every renderer consumes -- Group is derived from a check id's own first dot-segment (pure string parsing, no registry lookup), deliberately distinct from CheckDef::group (a different, finer-grained taxonomy already used for other purposes)"
    - "render_json takes the fully-resolved Policy (not just the CheckRegistry) so a finding's rendered tolerance/unit reflect what was ACTUALLY applied this run (config/CLI overrides included), not merely the registry's own baseline declaration -- severity_chain (under -v) is an id-matched linear scan over policy.per_check, the same dual-mode lookup resolve_severity itself performs"
    - "Every renderer signature grew a parameter beyond the plan's own literal sketch where the sketch omitted data the action text's own requirements needed: render_json gained Policy+verbose (severity_chain, accurate tolerance), render_markdown/render_junit gained a bool strict (the fold's gating-tier rule and JUnit's warn-vs-failure shape both need it, and ReportModel itself intentionally carries no RenderOptions member for a renderer to recover it from)"
    - "delta and evidence render as JSON null unconditionally: core/model.h's Finding has neither field (only Measurement carries evidence, and compare/tol.cpp folds its computed delta into Finding::message's text only) -- rendering null rather than re-deriving either in the report layer avoids a second delta computation that could drift from compare/tol.cpp's own exact cross-multiplication (D-08's 'one canonical place' principle); both keys are present and schema-nullable so a future plan can populate them with no schema change"
    - "Markdown's fold is two-tier by removal priority, not simple oldest-first: tier 1 (dropped first, from the canonical end backward) is every finding whose severity is NOT part of this run's gating set (Severity::fail is never gating-set-eligible for removal; Severity::warn joins the gating set only when --strict is given); Severity::fail is never dropped under any circumstance, matching the stronger of the two readings of the plan's own prohibition"
    - "JUnit XML well-formedness is proven by a hand-rolled, dependency-free stack-based tag-balance + entity-only-ampersand scanner in the test file itself, since this project has no XML parsing library in vcpkg.json and adding one solely to assert test output would be a disproportionate new dependency"

key-files:
  created:
    - src/report/model.h
    - src/report/model.cpp
    - src/report/markdown.h
    - src/report/markdown.cpp
    - src/report/junit.h
    - src/report/junit.cpp
    - docs/schema/report-1.0.json
    - tests/unit/test_report_model.cpp
    - tests/unit/test_markdown_budget.cpp
    - tests/unit/test_junit.cpp
    - tests/integration/test_json_schema.cpp
    - tests/integration/test_report_flags.cpp
    - tests/golden/markdown_basic.txt
    - tests/golden/junit_basic.txt
    - tests/golden/json_schema_basic.txt
  modified:
    - vcpkg.json
    - src/report/json.h
    - src/report/json.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/commands/compare.h
    - src/cli/commands/compare.cpp
    - tests/integration/test_determinism.cpp
    - tests/baseline/report-1.0.json
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt

key-decisions:
  - "Git history was shaped to match the plan's own task boundaries (mirroring 02-05/02-07's precedent): CMakeLists.txt/tests/*/CMakeLists.txt were edited and committed incrementally, each task's commit independently built (cmake --build) and ran green (ctest) before that commit landed, rather than committing the final integrated three-task state in one shot"
  - "Task 1's render_json signature change (Finding[] -> ReportModel, plus Policy+verbose) forced a minimal -v-flag/render-call adaptation in src/cli/commands/compare.cpp even though that file is not in Task 1's own files_modified list -- otherwise Task 1's own <verify> command (which needs the whole project to build) could not run. Task 3 then replaces that minimal adaptation with the full --json[=path]/--report kind=path rework its own files_modified list actually names"
  - "tests/baseline/report-1.0.json (TRUST-08's frozen cross-release oracle, committed in 02-07) was regenerated against the new report shape via the exact `mediadiff compare tracer_a.snap.json tracer_b_clean.snap.json --json` command its own file header prescribes -- this is the schema's OWN authoritative change this plan makes, not an unrelated test-fitting rewrite, and is recorded here per that file's 'do not casually rewrite it' guidance"
  - "tolerance/unit in the JSON report are sourced from the resolved Policy (policy.per_check, matched by Finding::id) rather than the CheckRegistry's bare baseline declaration, so a config/CLI tolerance override is reflected exactly as applied to this run"
  - "gating (JSON) is structural -- severity in {warn, fail} -- independent of --strict, matching doc 01's own 'gating-capable' phrasing for JUnit's selection criterion; --strict only affects the exit code and (in Markdown's fold and JUnit's element-shape choice) which findings/shapes are treated as protected, never whether a finding carries `gating: true`"

patterns-established:
  - "A report renderer that needs data beyond {ReportModel, CheckRegistry} (accurate tolerance, severity_chain, --strict-aware fold/element-shape rules) takes that data as an explicit extra parameter rather than growing ReportModel or RenderOptions to carry it, keeping ReportModel a pure, renderer-agnostic intermediate"

requirements-completed: [REPORT-01, REPORT-04, REPORT-05, REPORT-06, CLI-04]

coverage:
  - id: D1
    description: "One ReportModel groups findings in the fixed container/video/timeline/audio/content/size/meta order (derived from a check id's own first segment, unrecognized segments falling back to meta), orders within a group by registry declaration index then scope, and computes Summary counts across every finding including ones a RenderOptions filter later hides from display"
    requirement: "REPORT-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_report_model.cpp (11/11 pass, tag [report])"
        status: pass
    human_judgment: false
  - id: D2
    description: "render_json renders the shared ReportModel against a fixed key order and a shipped, draft-2020-12 JSON Schema that provably rejects a malformed document (an unknown status value); every finding carries an accurately-resolved tolerance/unit from the real Policy, and under -v an optional severity_chain whose layer sequence agrees with list-checks --effective -v's own text rendering for the same check; two identical runs are byte-identical"
    requirement: "REPORT-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_json_schema.cpp (6/6 pass, tag [integration]); json-schema-validator linked into the integration test target only (nm -c mediadiff | grep -ci json_schema -> 0)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_report_model.cpp + docs/schema/report-1.0.json parses as JSON with additionalProperties:false at the finding level"
        status: pass
    human_judgment: false
  - id: D3
    description: "Markdown renders a summary table (explicit zeros, no details when a run has zero findings) plus one <details> block per non-empty group; the 60000-byte (UTF-8) budget is real and its overflow fold, proven to actually trigger on an oversized model, drops the lowest-severity findings first, states the real withheld count, and never drops a Severity::fail finding, folding on a UTF-8 character boundary in the pathological case"
    requirement: "REPORT-04, REPORT-05"
    verification:
      - kind: unit
        ref: "tests/unit/test_markdown_budget.cpp (7/7 pass, tag [markdown]); golden tests/golden/markdown_basic.txt"
        status: pass
    human_judgment: false
  - id: D4
    description: "JUnit renders one <testsuite> per non-empty group and one <testcase> per gating-capable finding (severity warn/fail), with a Status-driven element shape (skipped/failure/error/bare pass) that is --strict-sensitive for warn, full XML escaping proven via a re-parse check, and honest tests/failures/errors/skipped counts including a well-formed zero-tests document"
    requirement: "REPORT-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_junit.cpp (8/8 pass, tag [junit]); golden tests/golden/junit_basic.txt"
        status: pass
    human_judgment: false
  - id: D5
    description: "`--json[=path]` and repeatable `--report <md|junit>=<path>` write independent files through independent fopen_utf8 handles; a malformed --report argument, an unrecognized kind, or two destinations naming the same path (including --json's own path) are ErrorKind::usage (exit 64); bare --json still writes stdout"
    requirement: "CLI-04"
    verification:
      - kind: integration
        ref: "tests/integration/test_report_flags.cpp (6/6 pass, tag [integration])"
        status: pass
      - kind: other
        ref: "./build/x64-linux/mediadiff compare <a> <b> --json=/tmp/a.json --report md=/tmp/b.md --report junit=/tmp/c.xml -> three distinct non-empty files, exit 0; --report sarif=/tmp/x -> exit 64"
        status: pass
    human_judgment: false

duration: ~95min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 8: Shared Report Model, JSON/Markdown/JUnit Renderers, and Report CLI Wiring Summary

**One `ReportModel` now owns grouping, ordering, filtering and summarisation for every report format; `--json` renders against a shipped, CI-enforced JSON Schema with accurate per-finding tolerance and an optional `-v` severity chain; Markdown stays inside a real 60,000-byte budget with a fold that never silently drops a failing finding; JUnit gives Jenkins/GitLab zero-integration visibility; and `--json[=path]` plus repeatable `--report kind=path` write independent files end to end.**

## Performance

- **Duration:** ~95 min
- **Started:** 2026-08-15 (continuing directly from 02-07)
- **Completed:** 2026-08-15
- **Tasks:** 3
- **Files modified:** 27 (15 created, 12 modified)

## Accomplishments

- `src/report/model.{h,cpp}`: `Group`/`group_for`/`group_to_string` (derived from a check id's own first dot-segment, independent of the registry), `Summary` (per-Status counts plus `worst_gating`, the maximum resolved `Severity` across every finding regardless of status), `RenderOptions`, `GroupBlock`, and `build_report_model` — a single pass that groups, sorts (registry declaration index then scope) and counts every finding BEFORE applying any display filter, so a hidden finding is still counted and an empty group still carries an explicit zero rather than being omitted.
- `src/report/json.{h,cpp}` rewritten to render `ReportModel` against doc 01 section 9's fixed key order (`schema_version`, `tool_version`, `profile`, `summary`, `diagnostics`, `findings`); every finding carries the full schema-declared key set, with `tolerance`/`unit` sourced from the real, fully-resolved `Policy` (config/CLI overrides included, not just the registry baseline) and an optional `severity_chain` under `-v` that mirrors `list-checks --effective -v`'s own text provenance rendering field-for-field.
- `docs/schema/report-1.0.json`: a draft-2020-12 JSON Schema, `additionalProperties: false` at the finding level, proven in CI (via `json-schema-validator`, vcpkg-pinned at 2.4.0, linked into the integration test target only) to actually reject a document with an unknown `status` value — not merely accept anything.
- `src/report/markdown.{h,cpp}`: a summary table plus one `<details>` block per non-empty group; `kMarkdownBudgetBytes = 60000` (UTF-8 bytes, real headroom under GitHub's 65,536-character comment limit); an overflow fold — proven to actually trigger on an oversized model, not merely assumed reachable — that drops the lowest-severity findings first (never a `Severity::fail` finding), states the real withheld count, and folds on a UTF-8 character boundary in the pathological all-droppable-content-gone case.
- `src/report/junit.{h,cpp}`: one `<testsuite>` per non-empty group, one `<testcase>` per gating-capable finding, a `Status`-driven element shape (`skipped`/`failure`/`error`/bare pass) that is `--strict`-sensitive for `warn`, full XML escaping proven via a dependency-free re-parse check, and honest `tests`/`failures`/`errors`/`skipped` counts including a well-formed zero-tests document.
- `src/cli/options.{h,cpp}` gains `ReportArgs`/`ReportDestination`/`add_report_flags`/`parse_report_destinations`; `src/cli/commands/compare.cpp` replaces the old bare `--json` bool flag with `--json[=path]` (CLI11's `->expected(0, 1)` idiom) and writes every requested destination — JSON, Markdown, JUnit — through its own independent `fopen_utf8` handle, so three requested reports mean three distinct files and no shared buffer.

## Task Commits

Git history shaped to match the plan's own task boundaries (mirroring 02-05/02-07's precedent) — each task's commit independently built (`cmake --build`) and ran fully green (`ctest`) before landing:

1. **Task 1: The shared report model, the JSON renderer, and the shipped schema** — `eccd91b` (feat)
2. **Task 2: Markdown with a real byte budget and an honest overflow fold** — `87c3e72` (feat)
3. **Task 3: JUnit XML and the report flag wiring** — `a0ff652` (feat)

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified

- `src/report/model.{h,cpp}` — `Group`, `Summary`, `RenderOptions`, `GroupBlock`, `ReportModel`, `build_report_model`, `is_gating`, `scope_to_text`
- `src/report/json.{h,cpp}` — rewritten `render_json(ReportModel, CheckRegistry, Policy, verbose)`
- `docs/schema/report-1.0.json` — the shipped JSON Schema
- `src/report/markdown.{h,cpp}` — `render_markdown`, `kMarkdownBudgetBytes`
- `src/report/junit.{h,cpp}` — `render_junit`
- `src/cli/options.{h,cpp}`, `src/cli/commands/compare.{h,cpp}` — `--json[=path]`/`--report kind=path` end-to-end wiring
- `tests/unit/test_report_model.cpp`, `tests/unit/test_markdown_budget.cpp`, `tests/unit/test_junit.cpp` — new unit suites, tags `[report]`/`[markdown]`/`[junit]`
- `tests/integration/test_json_schema.cpp`, `tests/integration/test_report_flags.cpp` — new integration suites
- `tests/integration/test_determinism.cpp` — adapted to `render_json`'s new signature (Rule 3)
- `tests/baseline/report-1.0.json` — regenerated against the new report shape (Rule 1)
- `tests/golden/markdown_basic.txt`, `tests/golden/junit_basic.txt`, `tests/golden/json_schema_basic.txt` — new golden fixtures
- `vcpkg.json` — `json-schema-validator` dependency
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` — new sources, `nlohmann_json_schema_validator` wiring, `MEDIADIFF_REPORT_SCHEMA`

## Decisions Made

See frontmatter `key-decisions` for full rationale on: shaping git history to the plan's task boundaries with each intermediate commit independently green; the minimal Task-1 `compare.cpp` adaptation Task 3 later replaces; regenerating the frozen `tests/baseline/report-1.0.json` against this plan's own schema change (not a casual rewrite); sourcing `tolerance`/`unit` from the resolved `Policy` rather than the bare registry; and `gating`'s structural (severity-only) definition versus `--strict`'s narrower effect on the exit code and on Markdown/JUnit's own protection rules.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `tests/integration/test_determinism.cpp` adapted to `render_json`'s new signature**
- **Found during:** Task 1, first build after rewriting `render_json`
- **Issue:** `render_json` changed from `(findings, envelope, registry)` to `(ReportModel, registry, Policy, verbose)` — this plan's own explicit Task 1 mandate ("Rewrite `src/report/json.{h,cpp}` to render the `ReportModel` rather than a raw finding span"). The pre-existing `test_determinism.cpp` (02-03-era, not in this plan's `files_modified`) called the old three-argument form directly and would no longer compile.
- **Fix:** `render_compare()`'s helper now resolves a full `Policy` (mirroring what `src/cli/commands/compare.cpp` itself does), builds a `ReportModel` via `build_report_model`, and calls the new `render_json` signature. Both existing determinism assertions (byte-identity across two runs; order-independence) still pass unchanged.
- **Files modified:** `tests/integration/test_determinism.cpp`.
- **Commit:** `eccd91b` (Task 1's own commit).

**2. [Rule 1 - Regression this plan's own schema change caused] Regenerated `tests/baseline/report-1.0.json`**
- **Found during:** Task 1, full-suite verification after Task 1's own commit
- **Issue:** `tests/integration/test_idempotence.cpp`'s TRUST-08 test compares a live `compare --json` render byte-for-byte against the frozen `tests/baseline/report-1.0.json` (committed in 02-07, capturing the OLD `{total,fail,warn}` summary shape and the pre-this-plan finding key set). This plan's own Task 1 deliberately changes that shape (`profile`, `diagnostics`, `group`, `gating`, `delta`, `tolerance`, `unit`, `evidence`, the new `summary` shape) — the frozen baseline is stale by construction of this plan's own mandate, not an unrelated test-fitting rewrite.
- **Fix:** Regenerated via the exact command the file's own header comment prescribes: `mediadiff compare tracer_a.snap.json tracer_b_clean.snap.json --json`, byte-for-byte (no trailing newline, matching the original file's own convention).
- **Files modified:** `tests/baseline/report-1.0.json`.
- **Commit:** `eccd91b` (Task 1's own commit).

**3. [Rule 3 - Blocking, scoped minimally] `src/cli/commands/compare.cpp` gained a minimal `-v` flag and adapted `render_json` call ahead of Task 3's own full rework**
- **Found during:** Task 1, running its own `<verify>` command (`ctest -R "unit.report|integration.json_schema"`), which requires the whole project to build first
- **Issue:** `compare.cpp` is not in Task 1's `files_modified` list (it belongs to Task 3), but Task 1's own `render_json` signature change made the pre-existing call site fail to compile, and Task 1's own acceptance criteria (`compare --json -v` must validate against the schema) require `-v` to already exist on the CLI.
- **Fix:** Added the minimal `-v/--verbose` flag and adapted the `render_json` call to build a `ReportModel` and pass the resolved `Policy` — no `--json[=path]`/`--report` rework yet. Task 3's own commit later replaces this with the full CLI-04 wiring.
- **Files modified:** `src/cli/commands/compare.cpp`, `src/cli/commands/compare.h` (comment only).
- **Commit:** `eccd91b` (Task 1's own commit); superseded by `a0ff652` (Task 3's own commit).

---

**Total deviations:** 3 auto-fixed (1 Rule 1, 2 Rule 3)
**Impact on plan:** All three were necessary to keep the build green and this plan's own acceptance criteria checkable at each task boundary; none changes scope, architecture, or any frozen contract from prior waves beyond the report JSON shape this plan itself was tasked with changing.

## Known Stubs

- **`delta` and `evidence` render as JSON `null` on every finding.** `core/model.h`'s `Finding` carries neither field: `Measurement.evidence` exists but `compare_fingerprints` (02-01/02-04-era, out of this plan's scope) does not thread it onto the `Finding` it produces, and `compare/tol.cpp`'s computed delta is folded into `Finding::message`'s human-readable text only, never a structured field. Both JSON keys are present (satisfying `docs/schema/report-1.0.json`'s required-but-nullable declaration) and schema-nullable specifically so a future plan can populate them from a real structured source without a schema change. Not fabricated data, and not blocking this plan's own goal (a complete, schema-valid report) — recorded here per the stub-tracking convention and in `.planning/WINDOWS.md`.

## Issues Encountered

- **`-Wswitch` caught an incomplete `Status` handling in `junit.cpp`'s `shape_for`:** the initial switch omitted `Status::info`, which IS reachable for a gating-capable finding (severity warn/fail) a comparator nonetheless classified as informational this run. Resolved by treating `Status::info` the same as `Status::pass` (a bare `<testcase>`, no child element) — there is no regression to report either way.
- **A test assertion bug, not an implementation bug:** an early `unit.junit` zero-tests test asserted `rendered.find("<testsuite") == npos`, which also (incorrectly) matches the root `<testsuites ...>` element's own opening tag as a substring. Fixed to search for `"<testsuite "` (trailing space, a child element's own tag) instead.
- **`nlohmann_json_schema_validator`'s exact vcpkg CMake package/target names were not assumed from memory** — verified directly against this repository's own pinned `builtin-baseline` submodule commit (the port resolves `json-schema-validator` 2.4.0, install-usage text confirms `find_package(nlohmann_json_schema_validator CONFIG REQUIRED)` / `nlohmann_json_schema_validator::validator`) before writing any CMake code against it.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `ReportModel`/`render_json`/`render_markdown`/`render_junit` are all stable, tested surfaces later plans (dir mode, `inspect`) can build on directly; each renderer's signature already accepts everything a caller building a fuller CLI surface needs (a resolved `Policy`, a `strict` flag, `verbose`).
- A future TTY renderer plan can set `RenderOptions.show_pass = false` for doc 01's "only non-pass by default" contract — the filtering mechanism is built and unit-tested (`tests/unit/test_report_model.cpp`) but never invoked with that setting by this plan's own three renderers, which is the deliberately scoped boundary this plan draws (JSON/Markdown/JUnit never hide a pass finding; only TTY does).
- `delta`/`evidence` are the one explicitly flagged, schema-compatible deferral (see Known Stubs) — a later plan extending `Finding` with structured delta/evidence data needs no schema change to populate these keys for real.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 15 created files verified present on disk; all 3 task commit hashes (`eccd91b`, `87c3e72`, `a0ff652`) verified present in `git log --oneline --all`.
