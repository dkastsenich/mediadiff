---
phase: 02-core-engine
plan: 01
subsystem: core-engine
tags: [nlohmann-json, tl-expected, cli11, tomllib, cmake, catch2]

# Dependency graph
requires:
  - phase: 01-foundation-toolchain
    provides: "expected<T,E> alias (src/util/expected.h), fopen_utf8 (src/util/fs.h), CLI11-wired src/cli/main.cpp, ENG-16 lint, warnings-as-errors build"
provides:
  - "Generated check registry (checks.def -> CheckId enum, CheckDef table, --explain doc embedding) with build-time grammar and missing-doc gates"
  - "Core type vocabulary: Rational/Ticks/compare_ticks, Value (9-alt variant), Status/Scope/Measurement/Fingerprint/Finding, Semantic/ValueKind/Unit/Severity/ProfileId/CheckDef/CheckRegistry"
  - "D-08 canonical Value<->JSON serializer (value_to_json/value_from_json) with D-09 kind-mismatch refusal"
  - "read_snapshot(): *.snap.json -> Fingerprint, refusing an unregistered check ID (T-2-04)"
  - "compare_fingerprints(): (check_index,scope)-paired dispatch through comparator_for, exact semantic implemented, six semantics stubbed as internal errors"
  - "render_json(): the --json report body, fixed key order, skip_reason always present (ENG-14)"
  - "src/cli/exit_code.h + compare subcommand: mediadiff compare a.snap.json b.snap.json --json working end to end with the real exit-code contract"
affects: ["02-02 (presence semantic, remaining seed check IDs)", "02-05 (policy precedence merge replaces resolve_severity's body)", "02-07 (full envelope, schema_version major-mismatch refusal)", "02-11 (meta.missing_candidate/meta.extra_candidate registration)"]

# Actuals (#2632)
actuals:
  tokens: 21468
  tasks: 4
  commits: 3

# Tech tracking
tech-stack:
  added: ["nlohmann_json (explicit find_package + link, was previously only reachable via FFMPEG's include-dir backdoor)"]
  patterns:
    - "Build-time codegen via add_custom_command + CONFIGURE_DEPENDS glob, atomic write-then-os.replace in the generator"
    - "FILE_SET HEADERS + VERIFY_INTERFACE_HEADER_SETS ON for every new core/compare/report header, so a header nothing includes still compiles standalone"
    - "Exhaustive enum switch with NO default: arm + trailing fallback return (satisfies -Wswitch exhaustiveness AND -Wreturn-type simultaneously)"
    - "std::exit() sanctioned inside src/cli/commands/*.cpp callbacks (ENG-16 lint explicitly excludes src/cli/ and names exit() 'the CLI's prerogative')"

key-files:
  created:
    - tools/gen_registry.py
    - src/core/checks.def
    - src/core/error.h
    - src/core/rational.h
    - src/core/value.h
    - src/core/model.h
    - src/core/registry.h
    - src/core/policy.h
    - src/core/serializer.h
    - src/core/serializer.cpp
    - src/core/snapshot.h
    - src/core/snapshot.cpp
    - src/compare/semantics.h
    - src/compare/engine.h
    - src/compare/engine.cpp
    - src/compare/exact.cpp
    - src/report/json.h
    - src/report/json.cpp
    - src/cli/exit_code.h
    - src/cli/commands/compare.h
    - src/cli/commands/compare.cpp
    - docs/checks/meta.tool_version.md
    - tests/fixtures/snapshots/tracer_a.snap.json
    - tests/fixtures/snapshots/tracer_b_clean.snap.json
    - tests/fixtures/snapshots/tracer_b_skew.snap.json
    - tests/integration/test_compare_tracer.cpp
  modified:
    - CMakeLists.txt
    - .planning/PROJECT.md
    - .gitignore
    - src/cli/main.cpp
    - tests/integration/CMakeLists.txt

key-decisions:
  - "Task 1 checkpoint frozen as proposed: schema_version=\"1.0\", seed IDs meta.tool_version/meta.missing_candidate/meta.extra_candidate (only the first registered this plan), time value = {num,den,tb:{num,den},ms}, ID grammar [a-z0-9_]+(\\.[a-z0-9_]+)*"
  - "nlohmann_json linked explicitly via find_package + target_link_libraries rather than relying on the FFMPEG_INCLUDE_DIRS backdoor Phase 1 used implicitly for tl-expected"
  - "Snapshot JSON shape (envelope keys + measurements[] array with id/scope/value/evidence) is this plan's own design — doc 01 fixes the envelope's field set and the Value on-disk shapes but not the top-level measurement-array layout"
  - "compare's exit code is decided by std::exit() inside the CLI11 subcommand callback, not by a captured out-parameter read after app.parse() returns — sanctioned by ENG-16's own commentary naming exit() 'the CLI's prerogative'"
  - "exit_code_for()'s exhaustive switch keeps NO default: arm (so -Wswitch catches a future unmapped ErrorKind) but adds a trailing fallback return after the switch (so -Wreturn-type doesn't also fire) — verified both properties hold via a standalone GCC 13 compile probe before writing the real file"
  - "tests/fixtures/snapshots/ carved out of the tests/fixtures/* gitignore rule: hand-authored JSON text (D-10), not generated media"

patterns-established:
  - "One canonical Value<->JSON visitor (D-08); no second ad hoc serialization path is ever added, including inside report/json.cpp which reuses value_to_json"
  - "Comparator dispatch as a plain function-pointer table (compare/semantics.h), not std::function — the semantic set is fixed and build-time-known"
  - "Registry declaration order + scope order as the canonical Finding emission order, via a std::set<PairKey> merge of both fingerprints' measurement keys"

requirements-completed: [ENG-15, ENG-16, SNAP-02, TRUST-05]

coverage:
  - id: D1
    description: "Check registry generated from checks.def at build time; missing-doc and bad-ID-grammar gates both fail the build loudly and name the offending ID"
    verification:
      - kind: integration
        ref: "cmake --build --preset x64-linux --target all_verify_interface_header_sets"
        status: pass
      - kind: other
        ref: "manual probe: doc removed + checks.def touched -> build fails naming meta.tool_version; grammar violation (Meta.ToolVersion) -> gen_registry.py exits 1 naming the ID"
        status: pass
    human_judgment: false
  - id: D2
    description: "Core type vocabulary (error.h, rational.h, value.h, model.h, registry.h) compiles standalone under -Wall -Wextra -Werror, no libav header reachable from src/core or src/compare"
    verification:
      - kind: integration
        ref: "cmake --build --preset x64-linux --target all_verify_interface_header_sets (12/12 headers)"
        status: pass
      - kind: other
        ref: "grep -rnE '#include *<libav' src/core src/compare -> no matches"
        status: pass
    human_judgment: false
  - id: D3
    description: "Serializer (D-08/D-09), snapshot reader (SNAP-02, T-2-04) and compare engine (exact semantic, TRUST-05 ordering) — libmediadiff_core.a defines and archives value_to_json/value_from_json/read_snapshot/comparator_for/compare_exact/compare_fingerprints"
    verification:
      - kind: unit
        ref: "nm -C build/x64-linux/libmediadiff_core.a | grep -E 'value_to_json|value_from_json|read_snapshot|comparator_for|compare_exact|compare_fingerprints'"
        status: pass
      - kind: other
        ref: "python3 fixture self-assertion: tracer_a == tracer_b_clean, tracer_a != tracer_b_skew (meta.tool_version)"
        status: pass
    human_judgment: false
  - id: D4
    description: "mediadiff compare <baseline> <candidate> --json [--strict] runs registry -> snapshot -> exact -> policy -> JSON report -> exit code end to end, byte-deterministic, with a real exit-64 usage-error path"
    verification:
      - kind: integration
        ref: "tests/integration/test_compare_tracer.cpp#compare_tracer - SNAP-02: clean compare exits 0 with one pass finding"
        status: pass
      - kind: integration
        ref: "tests/integration/test_compare_tracer.cpp#compare_tracer - TRUST-05: skew compare with --strict exits 2 on meta.tool_version"
        status: pass
      - kind: integration
        ref: "tests/integration/test_compare_tracer.cpp#compare_tracer - TRUST-05: identical runs produce byte-identical JSON"
        status: pass
      - kind: integration
        ref: "tests/integration/test_compare_tracer.cpp#compare_tracer - CLI-06: unrecognized flag exits 64, not CLI11's own range"
        status: pass
    human_judgment: false

duration: 15min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 1: Tracer — Registry, Serializer, Compare Engine, CLI Summary

**`mediadiff compare a.snap.json b.snap.json --json` runs end to end through a generated check registry, the canonical Value serializer, the exact-semantic compare engine, and a real exit-code contract — byte-deterministic across two runs.**

## Performance

- **Duration:** ~15 min (task-commit span; excludes Task 1's blocking-checkpoint wait, already resolved before this executor was spawned)
- **Started:** 2026-08-15T14:41:29Z
- **Completed:** 2026-08-15T14:48:16Z
- **Tasks:** 4 (Task 1 checkpoint resolved by the orchestrator; Tasks 2-4 executed)
- **Files modified:** 30

## Accomplishments
- Build-time check-registry generator (`tools/gen_registry.py`) reading TOML-syntax `checks.def` via stdlib `tomllib`, emitting `check_id.h`/`check_registry.cpp`/`check_explain.cpp`, with build-failing gates for a missing `docs/checks/<id>.md` and for a check ID that violates the dotted-lowercase grammar
- The full core type vocabulary Tasks 3-4 (and every later Phase-2 plan) build on: `Rational`/`Ticks`/`compare_ticks` (overflow-checked, no double conversion), the 9-alternative `Value` variant, `Status`/`Scope`/`Measurement`/`Fingerprint`/`Finding`, and the registry's `Semantic`/`ValueKind`/`Unit`/`Severity`/`ProfileId`/`CheckDef`/`CheckRegistry`
- One canonical `Value<->JSON` serializer (`value_to_json`/`value_from_json`) enforcing D-09 (a registry-kind mismatch is an `Error`, never a silent coercion) and D-08 (a time value serializes exactly as Task 1's checkpoint froze it)
- `read_snapshot()` reading a `*.snap.json` into a `Fingerprint`, refusing (not coercing) a measurement naming an unregistered check ID (T-2-04)
- `compare_fingerprints()` pairing measurements by `(check_index, scope)` and dispatching through `comparator_for()`; `exact` is fully implemented, the other six semantics report `ErrorKind::internal` rather than silently mis-comparing
- `render_json()` + the `compare` CLI subcommand + `src/cli/exit_code.h`'s exhaustive `ErrorKind -> exit code` switch — the full tracer path is wired, tested, and byte-deterministic

## Task Commits

Each task was committed atomically:

1. **Task 2: Generated check registry and the core type vocabulary** - `cdd8e6e` (feat)
2. **Task 3: Serializer, snapshot reader and the compare engine** - `19d51ed` (feat)
3. **Task 4: End-to-end "compare two snapshots" — one path only** - `9dadd4d` (feat)

(Task 1 was a `checkpoint:decision` — no executor commit; resolved `proposed` before this executor started, per the orchestrator's `<checkpoint_already_resolved>` instruction.)

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified
- `tools/gen_registry.py` - Python 3.11+ registry generator (TOML `checks.def` -> 3 generated files)
- `src/core/checks.def` - seeds `meta.tool_version` per the frozen Task 1 decision
- `src/core/error.h`, `rational.h`, `value.h`, `model.h`, `registry.h` - core type vocabulary
- `src/core/policy.h` - `Policy{ProfileId}` + `resolve_severity` stub (plan 02-05 fills the body)
- `src/core/serializer.{h,cpp}` - D-08's one canonical `Value<->JSON` visitor
- `src/core/snapshot.{h,cpp}` - `read_snapshot()`
- `src/compare/semantics.h`, `exact.cpp`, `engine.{h,cpp}` - comparator dispatch + `compare_fingerprints()`
- `src/report/json.{h,cpp}` - `render_json()`
- `src/cli/exit_code.h` - exit-code constants + `exit_code_for()`
- `src/cli/commands/compare.{h,cpp}` - the `compare` subcommand
- `src/cli/main.cpp` - registers `compare`, replaces `CLI11_PARSE` with an explicit try/catch mapping to `kExitUsage`
- `docs/checks/meta.tool_version.md` - the explain doc `check_explain.cpp` embeds
- `tests/fixtures/snapshots/tracer_{a,b_clean,b_skew}.snap.json` - hand-authored fixtures
- `tests/integration/test_compare_tracer.cpp` - the 4-case end-to-end tracer test
- `CMakeLists.txt` - registry codegen wiring, `nlohmann_json` link, `FILE_SET HEADERS`, new sources
- `.planning/PROJECT.md` - Python >= 3.11 build prerequisite (D-05)
- `.gitignore` - un-ignores `tests/fixtures/snapshots/`
- `tests/integration/CMakeLists.txt` - adds the tracer test + `MEDIADIFF_FIXTURES_DIR`

## Decisions Made
- Task 1's checkpoint frozen exactly as proposed (see frontmatter `key-decisions` for the verbatim contract: `schema_version="1.0"`, seed check IDs, time-value shape, ID grammar)
- `nlohmann_json` linked explicitly via `find_package`/`target_link_libraries` rather than continuing to rely on the FFMPEG-include-dir backdoor Phase 1 used implicitly for `tl-expected` — a small, justified Rule 2 addition (correct dependency declaration) since this plan is the first to actually need `nlohmann::ordered_json` outside that backdoor
- The snapshot JSON's top-level shape (`schema_version`, `tool_version`, `measurements[]` with `id`/`scope`/`value`/`evidence`) is this plan's own design — doc 01 fixes the envelope's field set and Value's on-disk shapes, but leaves the measurement-array layout to the planner's discretion (02-CONTEXT.md's "Claude's Discretion" section)
- `compare`'s exit code is decided via `std::exit()` inside the CLI11 subcommand callback (not a captured out-parameter read after `app.parse()` returns) — `scripts/lint_eng16.sh`'s own commentary explicitly names `exit()` "the CLI's prerogative," and `src/cli/commands/` sits outside its scanned subtrees
- `exit_code_for()`'s switch keeps NO `default:` arm (so `-Wswitch` catches a future unmapped `ErrorKind`) but adds a trailing fallback `return` after the switch body (so `-Wreturn-type` doesn't ALSO fire) — verified both properties independently via a standalone GCC 13 compile probe before writing the real file

## Deviations from Plan

None — plan executed exactly as written for Tasks 2-4. Task 1's checkpoint was pre-resolved by the orchestrator per this executor's `<checkpoint_already_resolved>` instruction; no re-presentation occurred.

## Issues Encountered

- **`std::variant`'s `operator==` requires every alternative to itself be equality-comparable**, and `Value`'s `Absent`, `RationalValue`, `Histogram`, `Span`, `SpanList` and `HashChain` aggregates had no `operator==` when first written (Task 2 focused on declaring the type vocabulary, not on comparison). Task 3's `compare_exact` needed `baseline.value == candidate.value` to compile, so `= default` comparison operators were added to those six types as part of Task 2's own file (`src/core/value.h`, committed in Task 2's commit — a foreseeable completion of the type's design, not a later-task scope violation, since Task 3's files list does not include `value.h`).
- **`src/core/snapshot.h` initially omitted `#include "core/error.h"`**, relying on a transitive include that doesn't exist (`core/model.h` does not itself define `Error`) — caught immediately by the Task 3 build and fixed before committing.
- **GCC 13 rejects an exhaustive `enum class` switch with no `default:` and no trailing statement** (`-Werror=return-type`, "control reaches end of non-void function") even when every enumerator has a `case`. Verified via a standalone compile probe (both the failing and passing forms) before writing `src/cli/exit_code.h`, landing on "trailing fallback `return` after the switch, still no `default:` label" — confirmed this satisfies `-Wswitch` exhaustiveness AND `-Wreturn-type` simultaneously.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The full registry -> snapshot -> semantics -> policy -> report -> CLI spine is proven end to end; every later Phase-2 plan (semantics 02-02..02-04, policy 02-05..02-06, snapshots 02-07, reports 02-08..02-10, `dir` 02-11) extends this machinery rather than co-designing it.
- `compare/semantics.h`'s `comparator_for()` already declares all seven `Semantic` enumerators; plans 02-02 through 02-04 replace `compare_unimplemented` with real dispatch, one function pointer at a time, with no signature changes required upstream.
- `src/core/policy.h`'s `resolve_severity()` signature is locked; plan 02-05 replaces only its body.
- No blockers. `meta.missing_candidate`/`meta.extra_candidate` remain approved-but-unregistered per Task 1's own scope boundary — `compare/engine.cpp`'s pairing loop already documents exactly where plan 02-11 plugs them in.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 26 created files verified present on disk; all 3 task commit hashes (`cdd8e6e`, `19d51ed`, `9dadd4d`) verified present in `git log --oneline --all`.
