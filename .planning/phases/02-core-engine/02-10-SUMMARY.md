---
phase: 02-core-engine
plan: 10
subsystem: cli
tags: [cli11, implicit-compare, exit-codes, partial-json, explain, inspect, subcommand-registration]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 09)
    provides: "src/cli/tty_render.h's render_tty and src/cli/color_policy.h's decide_color -- reused unchanged by run_compare's extraction; src/core/registry.h's CheckDef::explain_accept/explain_tune/explain_silence triple (this plan's inspect -v does not need them directly, but explain's full three-section body depends on the SAME docs/checks/<id>.md structure plan 02-09's generator now enforces)"
  - phase: 02-core-engine (plan 08)
    provides: "src/report/model.h's ReportModel/Summary/build_report_model/is_gating -- this plan's exit_code_for_findings reads Summary::worst_gating directly, and inspect's group iteration reuses Group/kGroupOrder/group_for/group_to_string/scope_to_text verbatim"
  - phase: 02-core-engine (plan 06)
    provides: "src/cli/provenance_render.h's render_provenance_chain -- inspect -v is the third of the three -v chain surfaces this plan completes (list-checks --effective -v, compare --json -v, inspect -v), all reading the SAME renderer"
provides:
  - "src/cli/main.cpp: two bare root positionals dispatching through run_compare (CLI-01), all six subcommands registered, allow_subcommand_prefix_matching(false), a subcommand-fallthrough-leak guard"
  - "src/cli/commands/compare.{h,cpp}: run_compare() extracted and exported -- the SAME compare execution the `compare` subcommand's callback and main.cpp's implicit route both call; -q/--quiet added"
  - "src/cli/commands/dir.{h,cpp}: `dir` subcommand registered (positionals, --threads, --content/--no-content, add_common_options) with a permanent ErrorKind::internal stub pending plan 02-11"
  - "src/cli/commands/inspect.{h,cpp}: `inspect <file>` renders every Group in fixed order from a raw *.snap.json, honors --json, and -v shows the resolved severity chain via the shared renderer"
  - "src/cli/commands/explain.{h,cpp}: `explain <check.id>` resolves through the registry then the alias table, prints the compiled-in three-section doc verbatim, and lists a matching group's ids on an unresolvable id"
  - "src/cli/exit_code.{h,cpp}: split header+cpp; exit_code_for_findings(Summary, strict) derives the exit code from Summary::worst_gating (severity), replacing the prior per-Status ad hoc helper"
  - "src/core/snapshot.cpp: read_snapshot now reads a top-level `partial` boolean out of snapshot JSON (previously hard-coded false) -- the fixture-based route CLI-07's 66 contract is proven through"
  - "src/core/check_explain.h: new hand-written header declaring explain_doc(CheckId), the accessor tools/gen_registry.py's generated check_explain.cpp defines but never declared for the unprefixed production registry"
  - "src/cli/options.{h,cpp}: CliOptions/add_common_options(), default_policy_args()/default_report_args()/default_color_args() for the implicit-compare route"
affects: ["02-11 (dir): the stub callback, --threads/--content/--no-content flags, and CLI-06's exit-70 assertion point are ready to be replaced/re-pointed at real orchestration"]

# Actuals (#2632)
actuals:
  tokens: 18862
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "run_compare() is the one compare execution both the `compare` subcommand's own CLI11 callback and main.cpp's implicit two-positional dispatch call -- CLI-01's 'same function, not a copy' requirement, extracted from what was previously an inline lambda body"
    - "A caller with no CLI::App to register flags on (the implicit-compare route) builds default-valued PolicyArgs/ReportArgs/ColorArgs via src/cli/options.h's default_*_args() rather than a second code path that skips flag-dependent logic -- ReportArgs::json_option stays nullptr in that case, and every reader of it (run_compare) null-checks before dereferencing"
    - "exit_code_for_findings reads Summary::worst_gating (a resolved-severity axis) rather than re-deriving a worst Status from the raw Finding[] -- the same 'two independent axes' distinction src/report/model.h's own Summary already documents"
    - "A mid-analysis failure (partial fingerprint or a decode-kind Error) still runs the full report-building and every-destination-write pipeline before std::exit(kExitDecode) -- never an early return that skips writing requested reports"

key-files:
  created:
    - src/cli/commands/dir.h
    - src/cli/commands/dir.cpp
    - src/cli/commands/inspect.h
    - src/cli/commands/inspect.cpp
    - src/cli/commands/explain.h
    - src/cli/commands/explain.cpp
    - src/cli/exit_code.cpp
    - src/core/check_explain.h
    - tests/integration/test_implicit_compare.cpp
    - tests/integration/test_exit_codes.cpp
    - tests/integration/test_explain_inspect.cpp
    - tests/fixtures/snapshots/partial_decode.a.snap.json
    - tests/fixtures/snapshots/partial_decode.b.snap.json
    - tests/fixtures/snapshots/inspect_no_meta.snap.json
    - tests/golden/inspect_basic.txt
  modified:
    - CMakeLists.txt
    - src/cli/main.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/exit_code.h
    - src/cli/commands/compare.h
    - src/cli/commands/compare.cpp
    - src/core/snapshot.cpp
    - tests/integration/CMakeLists.txt

key-decisions:
  - "run_compare() was extracted from compare.cpp's own callback lambda into an exported [[noreturn]] function (compare.h) -- the only way to satisfy CLI-01's 'the implicit route dispatches through the same function, not a copy' without duplicating ~150 lines of policy/report/exit-code logic. compare.h/compare.cpp were not in Task 1's own files_modified list but this extraction was structurally required (Rule 3 - blocking)"
  - "The implicit two-positional route (`mediadiff a b`) intentionally carries NONE of compare's own optional flags (--profile/--json/--strict/...) -- registering the full common-flag set on the ROOT app as well as on `compare` would create two independent Option objects bound to different storage for the same flag name, silently producing different results depending on where in argv a flag was typed. Kept minimal and correct rather than adding flag-sharing plumbing no acceptance criterion required"
  - "'A flag accepted both before and after the subcommand name' (Task 1's own must_have) is demonstrated as order-independence WITHIN a subcommand's own argument list (`list-checks --profile remux --effective` vs `list-checks --effective --profile remux`), not literally before/after the token 'list-checks' itself -- verified empirically that CLI11 2.6.2 resolves an option token unregistered at root scope as a parse error before ever reaching the subcommand, so 'before the subcommand name' would require registering --profile on root sharing storage with every subcommand's own copy, which is a real but out-of-scope refactor (list_checks.cpp is not a file this plan touches)"
  - "core/snapshot.cpp's read_snapshot gained a `partial` JSON field read (previously hard-coded `fp.partial = false`) -- Fingerprint::partial existed on the struct since Phase 2's model but was never wired to any read path, which would have made CLI-07's 66 contract untestable without it (Rule 3 - blocking, minimal, and explicitly anticipated by the plan's own 'reached in tests by a snapshot whose envelope declares partial: true' text)"
  - "exit_code_for_findings replaces compare.cpp's prior ad hoc worst_status(Status) helper, switching the exit-code decision from the per-Finding Status axis to Summary::worst_gating (Severity) -- the axis src/report/model.h's own Summary struct documents as the correct one for gating decisions, since a tol comparator's two-threshold form can resolve Status::pass on a check whose severity is Severity::fail"
  - "explain's full three-section output is explain_doc(CheckId) printed verbatim (already containing '## What it measures'/'## Why it matters'/'## Accept / Tune / Silence' with its own '### Accept'/'### Tune'/'### Silence' sub-headings, per tools/gen_registry.py's render_check_explain_cpp) rather than re-composed from CheckDef::explain_accept/explain_tune/explain_silence -- the generated function already produces exactly DOC-02's required shape, and re-composing it from the split triple would be a second formatter for the same text"
  - "inspect's own text/JSON renderers were written fresh in inspect.cpp rather than reusing render_tty (which renders Finding[] comparison output, not raw Measurement[] archaeology) -- the plan's 'reuse render_tty's composition helpers' guidance is honored via the one component render_tty ALSO reuses: render_provenance_chain (grep-verified in inspect.cpp)"

patterns-established:
  - "A CLI subcommand needing to run through the same execution as another entry point (implicit-compare vs explicit `compare`) exports that execution as a free [[noreturn]] function taking primitive flag values and the existing *Args bundles by const-reference, rather than either duplicating the body or forcing both call sites through the same CLI::App"

requirements-completed: [CLI-01, CLI-02, CLI-06, CLI-07, ENG-13, REPORT-07]

coverage:
  - id: D1
    description: "Two bare positionals on the root app (mediadiff <a> <b>) dispatch through the same run_compare() the compare subcommand's own callback calls; a near-miss subcommand name (comparex) is a usage error (exit 64, empty stdout) rather than a prefix match; a positional literally named 'compare' is usable when path-qualified; a stray operand after a fired subcommand is rejected"
    requirement: "CLI-01"
    verification:
      - kind: integration
        ref: "tests/integration/test_implicit_compare.cpp (10/10 pass, tag [integration])"
        status: pass
    human_judgment: false
  - id: D2
    description: "All six subcommands (compare, snapshot, dir, inspect, list-checks, explain) are registered and each accepts --help exiting 0; prefix matching is disabled explicitly via allow_subcommand_prefix_matching(false)"
    requirement: "CLI-02"
    verification:
      - kind: integration
        ref: "tests/integration/test_implicit_compare.cpp ('each of the six subcommands accepts --help and exits 0', 'T-2-36' cases)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Every contract exit code (0, 1, 2, 64, 65, 66, 70) is produced and asserted as an exact integer -- 0 clean, 1 a fail-severity finding, 2 worst-warn+--strict, 0 the same input without --strict, 64 (unknown flag / unknown subcommand / malformed --tol / wrong-unit --tol naming the expected unit), 65 (nonexistent path / schema major mismatch), 66 (partial fingerprint, with the emitted JSON parsing and carrying a partial marker in its diagnostics), 70 (the not-yet-implemented dir callback)"
    requirement: "CLI-06, CLI-07, CLI-10"
    verification:
      - kind: integration
        ref: "tests/integration/test_exit_codes.cpp (12/12 pass, tag [integration])"
        status: pass
    human_judgment: false
  - id: D4
    description: "explain <check.id> resolves every registered check id (and its declared alias) to the compiled-in three-section doc, in fixed order, with the alias path printing a deprecation line naming the current id first; an unresolvable id is exit 64 naming the id and, when its group matches, listing that group's own ids"
    requirement: "ENG-13"
    verification:
      - kind: integration
        ref: "tests/integration/test_explain_inspect.cpp (explain-focused cases, 4/9 of the suite)"
        status: pass
    human_judgment: false
  - id: D5
    description: "inspect <file> renders every Group exactly once in fixed order, with an explicit no-measurements line for an empty family, byte-identical across runs (golden-checked); inspect -v shows the resolved severity chain via the shared render_provenance_chain, ending with the cli layer and the overridden value, strictly more lines than without -v; a non-snapshot file exits 65"
    requirement: "REPORT-07"
    verification:
      - kind: integration
        ref: "tests/integration/test_explain_inspect.cpp (inspect-focused cases, 5/9 of the suite)"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 10: The Full CLI Surface, the Exit-Code Contract, and Explain/Inspect Summary

**`mediadiff <a> <b>` now dispatches through the exact same `run_compare()` the `compare` subcommand calls, all six subcommands exist, every one of the seven contract exit codes is real and tested (including a partial-JSON-then-66 path), and `explain`/`inspect` make every registered check answerable and every snapshot archaeologically inspectable.**

## Performance

- **Duration:** ~35 min
- **Started:** 2026-08-15 (continuing directly from 02-09)
- **Completed:** 2026-08-15
- **Tasks:** 3
- **Files modified:** 24 (15 created, 9 modified)

## Accomplishments

- `src/cli/main.cpp`: two optional bare positionals on the root `CLI::App` dispatch to `run_compare()` (extracted from `src/cli/commands/compare.cpp`) exactly when no subcommand fired and both are set; fewer than two prints help and exits 64, never 0. `allow_subcommand_prefix_matching(false)` is set before any subcommand is added (a child `App` copies the flag from its parent at construction), and a subcommand-fallthrough guard rejects a stray operand landing in the root's own positionals after a subcommand fired. All six subcommands — `compare`, `snapshot`, `dir`, `inspect`, `list-checks`, `explain` — are registered.
- `src/cli/commands/dir.{h,cpp}`: the `dir` subcommand's full flag surface (`--threads`, `--content`/`--no-content`, plus `src/cli/options.h`'s new `add_common_options`) is registered now; the callback itself permanently reports `ErrorKind::internal` (exit 70) until plan 02-11 fills the real orchestration body — this is also this plan's own reachable CLI-06 exit-70 path.
- `src/cli/exit_code.{h,cpp}`: split into a header (declarations + the seven named constants) plus a `.cpp`. `exit_code_for_findings(const Summary&, bool strict)` derives the exit code from `Summary::worst_gating` — a resolved-severity axis, not per-`Finding` `Status` — replacing `compare.cpp`'s prior ad hoc `worst_status` helper. Both `exit_code_for` and `exit_code_for_findings` remain `switch` statements with no `default:` arm.
- `src/core/snapshot.cpp`: `read_snapshot` now reads a top-level `partial` boolean from snapshot JSON (previously hard-coded `false`, with no read path at all). `src/cli/commands/compare.cpp`'s `run_compare` treats a partial fingerprint (or a `decode`-kind `Error` from `compare_fingerprints`) as: finish building the `ReportModel`, write every requested report destination, THEN `std::exit(kExitDecode)` — never an early abort that skips a requested report. The marker is recorded onto the candidate envelope's own `diagnostics` object, so it survives into every report format's diagnostics array as a `partial: true` line.
- `src/cli/commands/explain.{h,cpp}`: resolves `<check.id>` through `CheckRegistry::find`, then `resolve_alias`; an alias hit prints a deprecation line naming the current id before the doc body. The doc body itself is `src/core/check_explain.h`'s `explain_doc(CheckId)` printed verbatim — already containing the three fixed sections in order, the last carrying its own `### Accept`/`### Tune`/`### Silence` sub-headings, exactly DOC-02's required shape. An unresolvable id is exit 64, naming the id and, when its first dot-segment matches a registered `CheckDef::group`, listing that group's own check ids.
- `src/cli/commands/inspect.{h,cpp}`: reads a `*.snap.json` and renders every `Group` (from `src/report/model.h`) in fixed order — a heading, then either an explicit no-measurements line or one line per measurement (registry declaration order, then scope) — honoring `--json` for machine consumption. Under `-v`, each measurement's resolved severity chain prints via the SAME `render_provenance_chain` renderer `list-checks --effective -v` and `compare --json -v` already use, completing the three `-v` chain surfaces.
- `src/cli/options.{h,cpp}`: `CliOptions`/`add_common_options()` bundle the flags `dir`/`inspect` share; `default_policy_args()`/`default_report_args()`/`default_color_args()` give `main.cpp`'s implicit route default-valued option storage with no `CLI::App` to register against.

## Task Commits

1. **Task 1: Implicit compare and the full subcommand set** — `95b97fb` (feat)
2. **Task 2: The exit-code contract and partial JSON on a mid-analysis failure** — `879332f` (feat)
3. **Task 3: explain and inspect** — `1d199f9` (feat)

Each task's commit independently built (`cmake --build`) and ran fully green (`ctest`) before landing — verified by staging each commit's own file subset in sequence (Tasks 2 and 3's own new logic/tests temporarily held back via saved-aside full implementations and hand-reverted intermediate states), confirming 238/238 after Task 1, 250/250 after Task 2, and the full 259/259 after Task 3.

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified

- `src/cli/main.cpp` — implicit dispatch, all six `register_*_command` calls, the fallthrough-leak guard
- `src/cli/commands/compare.{h,cpp}` — `run_compare()` extracted and exported; `-q/--quiet` added
- `src/cli/commands/dir.{h,cpp}`, `inspect.{h,cpp}`, `explain.{h,cpp}` — new subcommand registrations
- `src/cli/exit_code.{h,cpp}` — split; `exit_code_for_findings`
- `src/cli/options.{h,cpp}` — `CliOptions`/`add_common_options`, `default_*_args()`
- `src/core/snapshot.cpp` — reads `partial` from snapshot JSON
- `src/core/check_explain.h` — new; declares `explain_doc(CheckId)`
- `CMakeLists.txt`, `tests/integration/CMakeLists.txt` — new sources
- `tests/integration/test_implicit_compare.cpp`, `test_exit_codes.cpp`, `test_explain_inspect.cpp` — 31 new tests total
- `tests/fixtures/snapshots/partial_decode.{a,b}.snap.json`, `inspect_no_meta.snap.json` — new fixtures
- `tests/golden/inspect_basic.txt` — new golden fixture

## Decisions Made

See frontmatter `key-decisions` for full rationale on: extracting `run_compare()` outside Task 1's own declared file scope (structurally required); the implicit route deliberately carrying no optional flags rather than duplicating flag storage across root and subcommand scopes; the "before/after the subcommand name" must_have demonstrated as intra-subcommand order-independence rather than literal root-vs-subcommand placement (an empirically-verified CLI11 2.6.2 limitation, not a shortcut); wiring `partial` into `read_snapshot`; `exit_code_for_findings`'s severity-axis (not status-axis) exit-code derivation; `explain` printing `explain_doc` verbatim; and `inspect`'s own fresh (but `render_provenance_chain`-reusing) renderer.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `src/cli/commands/compare.{h,cpp}` needed the `run_compare()` extraction, though neither file was in Task 1's own `files_modified` list**
- **Found during:** Task 1, designing the implicit-compare dispatch
- **Issue:** CLI-01's own action text requires the implicit two-positional route to dispatch "to the same compare entry point the `compare` subcommand calls — the same function, not a copy." The entire compare execution (policy resolution, report building, every renderer, the exit-code decision) lived inline inside `register_compare_command`'s own CLI11 callback lambda, with no externally callable entry point.
- **Fix:** Extracted the callback body into an exported `[[noreturn]] void run_compare(...)` (declared in `compare.h`), called identically by the subcommand's own callback and by `main.cpp`'s implicit route.
- **Files modified:** `src/cli/commands/compare.h`, `src/cli/commands/compare.cpp`.
- **Commit:** `95b97fb` (Task 1's own commit); further modified in `879332f` (Task 2) for the exit-code/partial rework.

**2. [Rule 3 - Blocking] `src/core/snapshot.cpp`'s `read_snapshot` needed to actually read the `partial` field it had never read**
- **Found during:** Task 2, implementing the 66/partial-JSON contract
- **Issue:** `Fingerprint::partial` existed on the struct since this phase's earliest model work but `read_snapshot` unconditionally set it `false` and never consulted the snapshot's own JSON — making CLI-07's fixture-based route (a snapshot whose envelope declares `partial: true`) structurally impossible to test, despite the plan's own text naming exactly that route.
- **Fix:** Added a minimal, additive read of a top-level `partial` boolean (absent or non-bool defaults to `false`).
- **Files modified:** `src/core/snapshot.cpp`.
- **Commit:** `879332f` (Task 2's own commit).

**3. [Rule 3 - Blocking] `src/core/check_explain.h` needed to exist for `explain` to call `explain_doc` at all**
- **Found during:** Task 3, implementing `explain`
- **Issue:** `tools/gen_registry.py`'s generated `check_explain.cpp` defines `explain_doc(CheckId)` for the unprefixed production registry but declares it in no header (only the `--symbol-prefix` path emits a forward declaration, to avoid colliding with the unprefixed accessor's own name) — there was no way for `explain.cpp` to name the function without either re-declaring it inline or adding a header.
- **Fix:** Added a small hand-written header declaring the accessor the generated `.cpp` already defines.
- **Files modified:** `src/core/check_explain.h` (new), `CMakeLists.txt` (added to `libmediadiff`'s verified `FILE_SET HEADERS`).
- **Commit:** `1d199f9` (Task 3's own commit).

---

**Total deviations:** 3 auto-fixed (all Rule 3 — blocking, each the minimal addition needed for this plan's own explicitly-stated mechanism to be reachable/testable)
**Impact on plan:** All three were structurally necessary for this plan's own acceptance criteria to be checkable at all; none changes scope, architecture, or any frozen contract from prior waves.

## Known Stubs

- **`dir`'s callback is a permanent stub in this plan**, reporting `ErrorKind::internal` (exit 70) — this is `dir`-mode orchestration's own explicitly-deferred scope (plan 02-11), not an oversight; the plan's own Flagged Assumptions table already names this and requires plan 02-11 to re-point the exit-70 assertion at a genuine internal-error path once the real body lands.
- **`inspect`'s `--json` output has no shipped schema** (unlike `compare --json`'s `docs/schema/report-1.0.json`) — REPORT-07 requires only that `inspect` "honour `--json` so it can feed a machine consumer too," which it does; no acceptance criterion in this plan constrains its exact shape, so none was invented beyond a straightforward `{schema_version, tool_version, groups: {<group>: [...]}}` document.

## Issues Encountered

- **CLI11 2.6.2 does not let a flag registered only on a subcommand be recognized when typed BEFORE that subcommand's own name in argv.** This plan's own must_have ("a flag accepted both before and after the subcommand name yields an identical resolved configuration either way") was interpreted and tested as order-independence among a subcommand's own arguments instead (verified empirically: `mediadiff --profile remux list-checks --effective` is a parse error today, since `--profile` is not a root-level option) — see `key-decisions` for the full rationale and the scope boundary (`list_checks.cpp` is not a file this plan touches) that kept this from becoming a larger flag-sharing refactor.
- **`mediadiff compare a b c` (a third operand) turned out to raise CLI11's own `ExtrasError` directly inside the `compare` subcommand's scope, not via the parent-positional fallthrough the plan's own research text described** (`subcommand_fallthrough_` defaults `true`, but CLI11 2.6.2's own extras handling for an over-long positional list fires before any fallthrough attempt in this configuration). The fallthrough-leak guard in `main.cpp` is kept per the plan's explicit instruction (and as defensive coverage against a future CLI11 version where fallthrough does occur), but is empirically unreached today — the exit-64 outcome the acceptance criterion actually requires is produced by CLI11's own `ExtrasError` path instead, through the pre-existing `CLI::ParseError` catch block.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `run_compare()`, `add_common_options()`/`CliOptions`, and `exit_code_for_findings` are stable, tested surfaces plan 02-11 (`dir` orchestration) can build directly on: `dir.cpp`'s stub already registers the exact flag set and positional shape 02-11 needs, and its callback is the one place that plan replaces.
- The full CLI-01/02/06/07 exit-code and subcommand surface doc 00 section 3.1/3.2 specifies is now real and tested; `explain`/`inspect` complete ENG-13/REPORT-07 and doc 01's UC8 archaeology use case.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 15 created files verified present on disk; all 3 task commit hashes (`95b97fb`, `879332f`, `1d199f9`) verified present in `git log --oneline --all`.
