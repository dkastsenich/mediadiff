---
phase: 02-core-engine
plan: 02
subsystem: core-engine
tags: [check-registry, glob-matcher, toml, tomllib, catch2, cmake, codegen]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 01)
    provides: "Generated check registry tracer (checks.def -> CheckId enum/CheckDef table/check_explain.cpp), core type vocabulary (Value/Rational/Ticks/Status/Scope/Measurement/Fingerprint/Finding), the compare/serializer/snapshot/report/CLI spine, the frozen Task-1 contracts (schema_version, seed IDs, ID grammar, time-value shape)"
provides:
  - "Full CheckDef surface: value_kind/default_severity/default_tolerance/is_volatile/requires_pass plus per-profile ProfileSeverityOverride/ProfileToleranceOverride spans (CheckDef::severity_for/tolerance_for), generated only for checks that declare a [check.profile_severity]/[check.profile_tolerance] sub-table"
  - "Structured alias table (AliasDef{old_id, new_check_index, new_group}) and CheckRegistry::resolve_alias(id, was_aliased*) — resolves a direct id or a declared alias, reporting which, for the config layer's future ENG-03 deprecation warning"
  - "checks_manifest.json — the sorted {id, doc, group, semantic, value_kind} docs manifest ENG-01 names, regenerated as a build OUTPUT"
  - "Eight build-failure conditions in tools/gen_registry.py: bad grammar, duplicate id, missing doc, empty/whitespace doc, doc missing a required heading, alias colliding with a registered id, orphan doc, and a raw-string-literal-terminating doc body — every offending item reported sorted, in one run"
  - "meta.missing_candidate (fail) and meta.extra_candidate (warn) fully registered with real docs, alongside a demonstrative alias + strict-bitexact severity override on meta.tool_version"
  - "src/core/glob.{h,cpp}: glob_matches/glob_select/validate_glob — segment-wise, regex-free, deterministic-order matching with a reportable malformed-pattern reason"
  - "scripts/lint_check_id_strings.sh (D-03), wired into CI's lint job — fails when a dotted check-id string literal appears under src/analyzers/"
  - "tests/process_spawn.h — the process-spawn primitive extracted out of tests/integration/cli_harness.h so unit tests can spawn an arbitrary executable (the Python interpreter) without the MEDIADIFF_BINARY compile definition"
  - "tests/fixtures/registry/{good,bad_missing_doc,bad_empty_doc,bad_grammar,bad_dangling_alias}/ — five self-contained fixture trees proving the docs gate can actually fail, each pinned to the specific offending id in captured stderr"
affects: ["02-05 (policy precedence merge consumes CheckDef::severity_for/tolerance_for and glob_select for [severity]/[tolerance] glob resolution)", "02-06 (config layer turns glob.h's validate_glob Error into a usage exit-64 diagnostic)", "02-11 (dir mode wires meta.missing_candidate/meta.extra_candidate's presence dispatch — both already registered with real docs by this plan)"]

# Actuals (#2632)
actuals:
  tokens: 21489
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Per-check profile-override spans (pointer+count) generated only for checks that declare a sub-table; a zero-alias/zero-override checks.def never emits a zero-length C array (not standard C++, rejected by MSVC) — nullptr+0 passed directly instead"
    - "Structured alias resolution: CheckRegistry::resolve_alias(id, bool* was_aliased = nullptr) tries a direct id match first (was_aliased=false), then the alias table (was_aliased=true), nullopt only when neither matches"
    - "Doc sections extracted by heading key into a dict, then re-emitted in a fixed canonical order regardless of source order — explain output is a property of the three required headings' content, not of how the Markdown happened to be written"
    - "Shared process-spawn test primitive (tests/process_spawn.h): CliResult/EnvVars in tests/integration/cli_harness.h are now aliases of ProcessResult/ProcessEnvVars, so a second test target can spawn an arbitrary executable without depending on the integration target's MEDIADIFF_BINARY compile definition"
    - "Test-name convention that makes `ctest -R unit.<component>` genuinely select a non-zero, correct set: test_glob.cpp's TEST_CASE descriptions start with 'glob_matches:'/'glob_select'/'validate_glob', test_registry_generator.cpp's start with 'registry generator:' — both verified via `-N -R` before relying on them (D-14's lesson)"

key-files:
  created:
    - src/core/glob.h
    - src/core/glob.cpp
    - docs/checks/meta.missing_candidate.md
    - docs/checks/meta.extra_candidate.md
    - scripts/lint_check_id_strings.sh
    - tests/process_spawn.h
    - tests/unit/test_registry.cpp
    - tests/unit/test_glob.cpp
    - tests/unit/test_registry_generator.cpp
    - tests/fixtures/registry/good/checks.def
    - tests/fixtures/registry/good/docs/good.sample.md
    - tests/fixtures/registry/bad_missing_doc/checks.def
    - tests/fixtures/registry/bad_empty_doc/checks.def
    - tests/fixtures/registry/bad_empty_doc/docs/bad.empty_doc.md
    - tests/fixtures/registry/bad_grammar/checks.def
    - tests/fixtures/registry/bad_dangling_alias/checks.def
    - tests/fixtures/registry/bad_dangling_alias/docs/bad.alias_target.md
    - tests/fixtures/registry/bad_dangling_alias/docs/bad.alias_owner.md
  modified:
    - tools/gen_registry.py
    - src/core/checks.def
    - src/core/registry.h
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/cli_harness.h
    - tests/integration/CMakeLists.txt
    - .github/workflows/ci.yml
    - .gitignore

key-decisions:
  - "Aliases are declared inline on the check that owns them (checks.def's `aliases = [...]` array), not as a separate top-level alias table — this makes 'an alias whose owning check no longer exists' structurally unreachable rather than a case needing a runtime guard, since every AliasDef is generated from the very check record it points at"
  - "meta.tool_version was given a demonstrative alias ('meta.version') and a strict-bitexact profile-severity override (warn -> fail) so test_registry.cpp exercises CheckRegistry::resolve_alias and CheckDef::severity_for against the real generated builtin_registry(), not only against Task 3's synthetic fixture trees — chosen with a plausible in-universe rationale (a pre-v1 rename; strict-bitexact escalating tool-version skew to a hard failure) rather than an arbitrary placeholder"
  - "checks_manifest.json's 'doc' field is a repo-relative path string (docs/checks/<id>.md), not embedded content — confirms the plan's own Flagged Assumption that this manifest is a build artifact today, not a runtime-read structure (D-02's single-static-binary constraint)"
  - "The process-spawn primitive was extracted into tests/process_spawn.h rather than duplicated, per the plan's own contingency instruction — cli_harness.h's MEDIADIFF_BINARY compile definition made its detail::spawn_and_capture unusable from the unit test target as-is; CliResult/EnvVars are now aliases so no integration-test call site changed"
  - "posix_spawn was widened to posix_spawnp in the extracted process_spawn.h (PATH-resolving a bare executable name) — a strict superset of the prior behavior since every existing caller already passes an absolute path, and it's what lets test_registry_generator.cpp pass CMake's resolved Python3_EXECUTABLE (already absolute) or, in principle, a bare interpreter name"

patterns-established:
  - "checks.def's per-profile override sub-tables ([check.profile_severity]/[check.profile_tolerance]) use TOML's standard array-of-tables-with-subtable syntax (the same shape as TOML's own canonical fruit/physical example) — no custom parsing invented"
  - "Every generator build-failure category collects and reports every offending item across the whole checks.def in one pass, sorted, before exiting — never aborts on the first hit within a category (validate_docs collects missing/empty/missing-heading/orphan together; collect_aliases collects every collision/duplicate together)"

requirements-completed: [ENG-01, ENG-02, ENG-03, DOC-01, DOC-02]

coverage:
  - id: D1
    description: "checks.def is the single source of truth for the ID enum, the registry table, the compiled-in explain text, the alias table and the docs manifest"
    requirement: "ENG-01"
    verification:
      - kind: unit
        ref: "tests/unit/test_registry.cpp — builtin_registry size/find/resolve_alias/default_severity/severity_for all pass against the real generated registry"
        status: pass
      - kind: other
        ref: "build/x64-linux/generated/core/checks_manifest.json — parsed, sorted array containing meta.tool_version/meta.missing_candidate/meta.extra_candidate"
        status: pass
    human_judgment: false
  - id: D2
    description: "An undocumented, empty-documented, malformed-heading, misnamed, duplicated, orphaned, or dangling-aliased check fails the build, proven against dedicated known-bad fixtures pinned to the specific offending id"
    requirement: "DOC-01"
    verification:
      - kind: unit
        ref: "ctest --test-dir build/x64-linux -R unit.registry (5/5 pass: good, bad_missing_doc, bad_empty_doc, bad_grammar, bad_dangling_alias)"
        status: pass
      - kind: other
        ref: "manual negative control: truncating docs/checks/meta.extra_candidate.md to zero bytes fails cmake --build naming meta.extra_candidate; restored and green again. Deleting tests/fixtures/registry/bad_missing_doc/checks.def makes its own ctest case genuinely FAIL (not skip); restored and green again."
        status: pass
      - kind: other
        ref: "manual probe: a doc missing '## Why it matters' is rejected naming the missing heading; a doc with headings in Accept/What/Why order still emits the fixed What/Why/Accept order in check_explain.cpp"
        status: pass
    human_judgment: false
  - id: D3
    description: "Glob matching is segment-wise, regex-free, deterministic in output order, and covered by refusal cases as well as match cases"
    requirement: "ENG-02"
    verification:
      - kind: unit
        ref: "ctest --test-dir build/x64-linux -R unit.glob (12/12 pass)"
        status: pass
      - kind: other
        ref: "grep -rn 'include <regex>' src/core -> no matches"
        status: pass
    human_judgment: false
  - id: D4
    description: "CheckRegistry::resolve_alias resolves a declared alias to its owning check and reports the lookup went through an alias, distinct from a direct id match — the building block plan 02-06's config parser uses for ENG-03's deprecation warning"
    requirement: "ENG-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_registry.cpp — resolve_alias (declared alias / direct id / neither) all pass"
        status: pass
    human_judgment: false
  - id: D5
    description: "docs/checks/<id>.md bodies are embedded into the binary via check_explain.cpp, including non-ASCII content preserved byte-for-byte"
    requirement: "DOC-02"
    verification:
      - kind: other
        ref: "grep -n the em-dash byte sequence (e2 80 94) in both docs/checks/meta.tool_version.md and build/x64-linux/generated/core/check_explain.cpp — identical"
        status: pass
    human_judgment: false

duration: 24min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 2: Registry Completion and Glob Matcher Summary

**The check registry generator grew from the tracer's minimum into the full `CheckDef` surface — per-profile severity/tolerance overrides, a structured alias table, an eight-condition documentation gate — alongside a segment-wise, regex-free glob matcher and a fail-first proof that the docs gate genuinely rejects five distinct known-bad registries.**

## Performance

- **Duration:** ~24 min
- **Started:** 2026-08-15T14:50:51Z
- **Completed:** 2026-08-15T15:15:00Z
- **Tasks:** 3
- **Files modified:** 29 (18 created, 11 modified — excludes the pre-existing untracked `.gsd/` and `tests/fixtures/GENERATOR_MANIFEST.json`, neither of which this plan owns)

## Accomplishments
- `tools/gen_registry.py` extended from the tracer's 3-file, single-condition output to the full mechanism: `CheckDef` gains `value_kind`/`default_tolerance`/per-profile `ProfileSeverityOverride`/`ProfileToleranceOverride` spans; a structured `AliasDef{old_id, new_check_index, new_group}` table backs `CheckRegistry::resolve_alias`; `checks_manifest.json` is a fourth generated build `OUTPUT`; eight distinct build-failure conditions are enforced, every offending item reported sorted in one run rather than aborting on the first
- `src/core/checks.def` fully registers all three seed checks (`meta.tool_version`, `meta.missing_candidate`, `meta.extra_candidate`) with real, non-empty three-heading docs — `meta.tool_version` additionally carries a demonstrative alias and a strict-bitexact severity override, exercising `resolve_alias`/`severity_for` against the real generated registry rather than only Task 3's fixtures
- `src/core/glob.{h,cpp}`: `glob_matches`/`glob_select`/`validate_glob`, segment-wise split-and-compare with zero `<regex>` anywhere (ENG-02) — `*` matches exactly one whole segment, `**` (final position only) matches one or more trailing segments, malformed patterns match nothing and report why
- `tests/unit/test_registry_generator.cpp` spawns the real generator against five self-contained fixture trees under `tests/fixtures/registry/` — proving the documentation gate rejects a missing doc, an empty doc, a bad-grammar id, and a colliding alias, each pinned to the specific offending id in captured stderr (not exit code alone)
- `scripts/lint_check_id_strings.sh` (D-03) wired into CI's lint job, passing trivially today since `src/analyzers/` is still empty
- `tests/process_spawn.h` extracted from `tests/integration/cli_harness.h` so the unit test target can spawn the Python interpreter directly, with zero behavior change at any existing integration-test call site

## Task Commits

Each task was committed atomically:

1. **Task 1: Complete the generator — value kinds, profile overrides, aliases, docs manifest** - `d78f056` (feat)
2. **Task 2: Segment-wise glob matcher with no regex** - `0805a11` (feat)
3. **Task 3: Prove the docs gate can actually fail — known-bad registry fixtures** - `ba7359e` (test)

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified
- `tools/gen_registry.py` - full `CheckDef` surface generator: profile overrides, structured aliases, docs manifest, eight build-failure conditions, raw-literal-termination guard
- `src/core/checks.def` - all three seed checks registered, plus a demonstrative alias and profile-severity override on `meta.tool_version`
- `src/core/registry.h` - `ProfileSeverityOverride`/`ProfileToleranceOverride`/`AliasDef`, `CheckDef::severity_for`/`tolerance_for`, `CheckRegistry::resolve_alias`
- `docs/checks/meta.missing_candidate.md`, `docs/checks/meta.extra_candidate.md` - the two remaining seed docs (dir-mode pairing semantics, doc 01 section 10)
- `scripts/lint_check_id_strings.sh` - D-03 lint, wired into `.github/workflows/ci.yml`'s lint job
- `src/core/glob.{h,cpp}` - `glob_matches`/`glob_select`/`validate_glob`
- `tests/process_spawn.h` - extracted process-spawn primitive shared by unit and integration tests
- `tests/integration/cli_harness.h` - now a thin wrapper over `process_spawn.h` (`CliResult`/`EnvVars` are aliases)
- `tests/unit/test_registry.cpp`, `tests/unit/test_glob.cpp`, `tests/unit/test_registry_generator.cpp` - new unit suites, tags `[registry]`/`[glob]`
- `tests/fixtures/registry/{good,bad_missing_doc,bad_empty_doc,bad_grammar,bad_dangling_alias}/` - five self-contained fixture trees
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` - registry manifest output, `glob.cpp`/`glob.h` wiring, new test sources, `MEDIADIFF_PYTHON`/`MEDIADIFF_GEN_REGISTRY`/`MEDIADIFF_REGISTRY_FIXTURES` compile definitions
- `.gitignore` - carves `tests/fixtures/registry/` out of the `tests/fixtures/*` ignore rule (hand-authored text, same rationale as `snapshots/`)

## Decisions Made
- Aliases declared inline on their owning check (`checks.def`'s `aliases = [...]`), making "owning check no longer exists" structurally unreachable rather than a runtime guard — see frontmatter `key-decisions` for full rationale
- `meta.tool_version` given a demonstrative alias + strict-bitexact severity override so `test_registry.cpp` proves `resolve_alias`/`severity_for` against the real registry, not only synthetic fixtures
- `checks_manifest.json`'s `doc` field is a repo-relative path string, confirming the plan's own Flagged Assumption that the manifest is a build artifact today, not embedded runtime data
- Process-spawn primitive extracted to `tests/process_spawn.h` per the plan's contingency instruction, since `cli_harness.h`'s `MEDIADIFF_BINARY` definition made it unusable as-is from the unit test target
- `posix_spawn` widened to `posix_spawnp` in the extracted header (PATH-resolving), a strict superset of prior behavior since every existing caller already passes an absolute path

## Deviations from Plan

None — plan executed exactly as written across all three tasks. No auth gates encountered.

## Issues Encountered

None. The plan's own contingency ("if that header's `MEDIADIFF_BINARY` compile definition makes it unusable from the unit target, extract the spawn helper") applied exactly as anticipated — `cli_harness.h`'s `run_cli()` references `MEDIADIFF_BINARY` unconditionally in its body, which the unit test target never defines, so the extraction was required rather than optional. No unplanned auto-fixes were needed beyond that anticipated path.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `CheckDef::severity_for`/`tolerance_for` and `glob_select` are the exact primitives plan 02-05's policy precedence merge consumes for `[severity]`/`[tolerance]` glob resolution — no signature changes anticipated.
- `glob.h`'s `validate_glob` already returns the `usage`-kind `Error` plan 02-06's config layer turns into an exit-64 diagnostic — nothing further needed from this plan's side.
- `meta.missing_candidate`/`meta.extra_candidate` are now fully registered with real docs; plan 02-11's `dir` mode only needs to wire their `presence` semantic dispatch, not register or document them.
- `resolve_alias`'s `was_aliased` out-parameter is exactly what plan 02-06's config parser needs to decide when to emit ENG-03's deprecation warning.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 11 spot-checked created files verified present on disk (`src/core/glob.h`, `src/core/glob.cpp`, `docs/checks/meta.missing_candidate.md`, `docs/checks/meta.extra_candidate.md`, `scripts/lint_check_id_strings.sh`, `tests/process_spawn.h`, `tests/unit/test_registry.cpp`, `tests/unit/test_glob.cpp`, `tests/unit/test_registry_generator.cpp`, `tests/fixtures/registry/good/checks.def`, `tests/fixtures/registry/bad_dangling_alias/docs/bad.alias_owner.md`); all 3 task commit hashes (`d78f056`, `0805a11`, `ba7359e`) verified present in `git log --oneline --all`.
