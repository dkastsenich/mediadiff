---
phase: quick/260902-it6-migrate-cli-option-binding-from-shared-p
plan: it6
subsystem: cli
tags: [cli11, cpp20, refactor]

requires:
  - phase: 02-core-engine
    provides: "src/cli's shared_ptr-per-option CLI11 binding shape (compare/dir/inspect/list-checks/snapshot/explain), 303-test baseline, exit-code contract"
provides:
  - "Every src/cli option struct/local binds through borrowed CLI::Option* (D-05), read via opt_string/opt_flag/opt_strings"
  - "38 -> 1 make_shared reduction in src/cli, with the single residual (dir.cpp --threads) named and justified"
  - "Clean CLI::Option* pattern Phase 3 (03-probe-layer-container-size) can build new CLI surface on top of, per D-06"
affects: [03-probe-layer-container-size]

actuals:
  tokens: 13200
  tasks: 6
  commits: 6

tech-stack:
  added: []
  patterns:
    - "Borrowed CLI::Option* + opt_string/opt_flag/opt_strings accessors replace shared_ptr-per-flag for all new/migrated CLI11 bindings"
    - "Vector-valued repeatable flags via the untargeted add_option(name, desc) overload require an explicit ->expected(-1, -1) -- CLI11 does not infer 'unlimited occurrences' without a bound container"
    - "String-typed options via the untargeted overload require an explicit ->type_name(\"TEXT\") to keep help output identical -- CLI11 only infers the type name from a bound variable's type"

key-files:
  created:
    - .planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt
  modified:
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/main.cpp
    - src/cli/commands/compare.cpp
    - src/cli/commands/dir.cpp
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/list_checks.cpp
    - src/cli/commands/explain.cpp
    - src/cli/commands/inspect.cpp

key-decisions:
  - "opt_strings() guards on count()==0 before calling as<vector<string>>() -- CLI11 2.6.2 returns a one-element {\"\"} for an unset vector option, not {}, which would otherwise turn every unset --set/--tol/--report into a spurious usage error (Landmine 1, D-05)."
  - "--threads keeps its bound shared_ptr<int> and targeted add_option overload verbatim, per D-05's typed-numeric exception -- the untargeted overload drops parse-time type validation (Landmine 2)."
  - "dir.cpp materializes baseline_dir/candidate_dir/content/strict/quiet/verbose into locals once, before WorkerPool starts, so no Option::results() read can ever drift into the concurrent worker region."

patterns-established:
  - "New CLI11 options in this repo bind via untargeted add_option(name, desc)/add_flag(name, desc) + opt_string/opt_flag/opt_strings, not shared_ptr-per-flag."

requirements-completed: [CLI-01, CLI-04, CLI-08, CLI-10]

coverage:
  - id: D1
    description: "PolicyArgs/ReportArgs/ColorArgs/CliOptions hold borrowed CLI::Option* instead of shared_ptr-per-flag; every consumer reads through opt_string/opt_flag/opt_strings"
    requirement: "CLI-01"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (303 tests, 0 failed, 1 skipped)"
        status: pass
      - kind: other
        ref: "grep -c shared_ptr src/cli/options.h == 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "--set/--tol/--report repeatable-flag accumulation preserved after dropping the vector<string> binding"
    requirement: "CLI-04"
    verification:
      - kind: manual_procedural
        ref: "mediadiff compare a b --json=path --report md=path --report junit=path exits 0 and writes all three files"
        status: pass
    human_judgment: false
  - id: D3
    description: "make_shared reduced from 38 to exactly 1 (dir.cpp --threads), named and justified"
    requirement: "CLI-10"
    verification:
      - kind: other
        ref: "grep -rhc make_shared src/cli --include='*.cpp' | paste -sd+ | bc == 1"
        status: pass
    human_judgment: false
  - id: D4
    description: "Help output byte-identical to the pre-refactor baseline for root + all 6 subcommands"
    requirement: "CLI-08"
    verification:
      - kind: other
        ref: "diff against .planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt (HELP_DIFF_CLEAN)"
        status: pass
    human_judgment: false

duration: 19min
completed: 2026-09-02
status: complete
---

# Quick Task 260902-it6: Migrate CLI Option Binding to CLI::Option* Summary

**Replaced all 38 `shared_ptr`-per-option sites in `src/cli` with borrowed `CLI::Option*` (D-05), reduced to exactly 1 deliberate residual (`dir.cpp`'s `--threads` bound int), with zero observable behavior change verified by a byte-identical help-text diff, an unmodified 303/0/1 test suite, and four green lint scripts.**

## Performance

- **Duration:** 19 min
- **Started:** 2026-09-02T13:45:45+02:00
- **Completed:** 2026-09-02T14:04:50+02:00
- **Tasks:** 6
- **Files modified:** 9 (plus 1 evidence artifact created)

## Accomplishments

- Added `opt_string`/`opt_flag`/`opt_strings` -- the three borrowed-`Option*` accessors D-05 specifies, with `opt_strings`' `count()==0` guard documented as mandatory (Landmine 1) rather than defensive padding.
- Flipped `PolicyArgs`/`ReportArgs`/`ColorArgs`/`CliOptions` to hold `CLI::Option*` (all defaulted `nullptr`); `ReportArgs::json_path` is gone -- `json_option` alone now answers both "was `--json` given" and "with what path".
- Migrated every local `make_shared` site across `compare.cpp`, `dir.cpp`, `snapshot.cpp`, `list_checks.cpp`, `explain.cpp`, `inspect.cpp`, and `main.cpp`'s implicit two-positional route.
- Preserved `--threads`'s bound `int` and targeted `add_option` overload verbatim (D-05's typed-numeric exception, Landmine 2) -- manually confirmed `--threads abc` and `--threads 0` both still exit 64 with identical diagnostic text.
- `dir.cpp`'s callback now materializes every option read into a local once, before `WorkerPool` starts, structurally preventing any `Option::results()` read from drifting into the concurrent worker region.
- Captured a pre-refactor help/test/`make_shared` baseline (Task 1) and proved the post-refactor state matches it byte-for-byte (Task 6).

## Task Commits

Each task was committed atomically:

1. **Task 1: Capture the behavioral baseline before touching any code** - `25db9be` (docs)
2. **Task 2: Add the three Option* accessors (purely additive)** - `293bbad` (feat)
3. **Task 3: Flip the four shared structs to CLI::Option* and update every consumer** - `ceead67` (feat)
4. **Task 4: Migrate the four simple commands' local options** - `6aa6ae7` (feat)
5. **Task 5: Migrate compare.cpp and dir.cpp locals, preserving the --threads exception** - `91f1377` (feat)
6. **Task 6: Prove behavior preservation and reconcile the residual count** - `fe78a58` (fix -- includes two deviation fixes, see below)

_No plan-metadata commit: this is a quick task under `.planning/quick/`, not a phase plan; STATE.md/ROADMAP.md are not touched here per the orchestrator's ownership boundary._

## Files Created/Modified

- `.planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt` - Pre- and post-refactor help/test/make_shared evidence, diffed to prove behavior preservation
- `src/cli/options.h` - Four structs flipped to `CLI::Option*`; three new accessor declarations
- `src/cli/options.cpp` - Registration functions rewritten to untargeted `add_option`/`add_flag`, with explicit `->expected(-1, -1)` on repeatable flags and `->type_name("TEXT")` on string-valued ones
- `src/cli/main.cpp` - Implicit two-positional route migrated to `CLI::Option*`
- `src/cli/commands/compare.cpp` - 5 local sites migrated
- `src/cli/commands/dir.cpp` - 4 local sites migrated; `--threads` kept as-is; option reads materialized into locals before `WorkerPool` starts
- `src/cli/commands/snapshot.cpp` - 3 local sites migrated
- `src/cli/commands/list_checks.cpp` - 2 local sites migrated
- `src/cli/commands/explain.cpp` - 1 local site migrated
- `src/cli/commands/inspect.cpp` - 1 local site migrated

## Decisions Made

- **`opt_strings()`'s `count()==0` guard is mandatory, not defensive.** CLI11 2.6.2's `Option::results()` (`Option.hpp:735-741`) returns a one-element `{""}` for an unset vector option, not `{}`. Isolated into its own commit (Task 2) per the plan so this one genuinely subtle line got independent review.
- **`--threads` keeps its bound `int`.** D-05's explicit typed-numeric exception: the untargeted `add_option` overload isn't templated and drops parse-time type validation, which would move a bad `--threads` value's failure from a `CLI::ParseError` at parse (exit 64, clean diagnostic) to an `as<int>()` throw inside the callback (still exit 64, worse diagnostic, later failure point).
- **`dir.cpp` materializes option reads into locals once, before the worker pool starts.** `Option::results()` writes a `mutable proc_results_` cache; concurrent `as<>()`/`opt_*()` calls on the same `Option*` would be a data race. This keeps the "no option read races" property structural rather than incidental.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Untargeted `add_option` silently changed repeatable-flag accumulation for `--set`/`--tol`/`--report`**
- **Found during:** Task 3 (verification after flipping the shared structs)
- **Issue:** CLI11's templated `add_option(name, vector<string>&, desc)` overload infers "unlimited occurrences" from the bound variable's type; the untargeted `add_option(name, desc)` overload this migration adopts runs no such inference and defaults to at-most-one occurrence. A second `--report` flag threw `ArgumentMismatch::AtMost` ("At most 1 required but received 2") instead of accumulating -- caught by `integration.report_flags` failing `64 != 0`.
- **Fix:** Added an explicit `->expected(-1, -1)` to `--set`, `--tol`, and `--report` -- CLI11's own public, documented shorthand for "at least 1 value if given, unlimited repetitions". `--json` already had its own explicit `->expected(0, 1)` and needed no change.
- **Files modified:** `src/cli/options.cpp`
- **Verification:** `mediadiff compare a b --json=path --report md=path --report junit=path` now exits 0 and writes all three files; full suite green at 303/0/1.
- **Committed in:** `ceead67` (Task 3 commit)

**2. [Rule 1 - Bug] Untargeted `add_option` silently dropped every string option's "TEXT" help-text type name**
- **Found during:** Task 6 (help-diff verification)
- **Issue:** The templated `add_option(name, variable, desc)` overload sets the help-text type annotation via `detail::type_name<T>()`. The untargeted overload used everywhere in this migration has no bound variable to infer from, so every string-valued option (`--profile`, `--config`, `--set`, `--tol`, `--json`, `--report`, `--out`, and every string positional) silently dropped "TEXT" from its help line -- a real help-text change the hard constraint "byte-identical help output" forbids.
- **Fix:** Added an explicit `->type_name("TEXT")` to every affected `add_option` call across `options.cpp`, `compare.cpp`, `dir.cpp`, `inspect.cpp`, `explain.cpp`, `snapshot.cpp`, and `main.cpp`'s implicit positionals.
- **Files modified:** `src/cli/options.cpp`, `src/cli/commands/compare.cpp`, `src/cli/commands/dir.cpp`, `src/cli/commands/inspect.cpp`, `src/cli/commands/explain.cpp`, `src/cli/commands/snapshot.cpp`, `src/cli/main.cpp`
- **Verification:** Re-ran the Task 1 help capture and diffed against the baseline -- clean, byte-identical.
- **Committed in:** `fe78a58` (Task 6 commit)

**3. [Rule 1 - Bug] `main.cpp`'s implicit two-positional route was never migrated by any task's explicit action text**
- **Found during:** Task 6 (make_shared residual census -- total was 3, not the plan's stated target of 1)
- **Issue:** `main.cpp` is listed in this plan's own `files_modified` and the plan's residual-reconciliation table calls for `main.cpp: 2 -> 0`, but no task's action text named `implicit_baseline`/`implicit_candidate` explicitly (Task 3's action list covered the shared structs and their consumers, not `main.cpp`'s own local positionals).
- **Fix:** Migrated `implicit_baseline_opt`/`implicit_candidate_opt` to `CLI::Option*`, read via `opt_string()`, with the same `->type_name("TEXT")` fix applied.
- **Files modified:** `src/cli/main.cpp`
- **Verification:** `grep -rhc make_shared src/cli --include='*.cpp' | paste -sd+ | bc` now returns exactly 1.
- **Committed in:** `fe78a58` (Task 6 commit)

---

**Total deviations:** 3 auto-fixed (all Rule 1 - bug)
**Impact on plan:** All three were genuine behavior/verification-contract breaks the plan's own hard constraints forbid (repeatable-flag semantics, byte-identical help text, the stated 38->1 residual target). None represent scope creep -- all three were caught by the plan's own verification steps (Task 3's test run, Task 6's help diff and residual census) and fixed within the task where they surfaced.

## Issues Encountered

None beyond the three deviations documented above, all caught and closed by the plan's own verification loop before completion.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 3 (`03-probe-layer-container-size`) can now be planned assuming the clean `CLI::Option*` pattern is already in place (D-06) -- no shared_ptr-per-option refactor debt carries forward.
- The `opt_string`/`opt_flag`/`opt_strings` accessors and the `->expected(-1, -1)`/`->type_name("TEXT")` idioms discovered here are the established pattern for any new CLI11 surface Phase 3 adds.
- No blockers.

---
*Phase: quick/260902-it6-migrate-cli-option-binding-from-shared-p*
*Completed: 2026-09-02*

## Self-Check: PASSED

All 9 modified source files and 1 created evidence artifact confirmed present on disk. All 6 task commits (25db9be, 293bbad, ceead67, 6aa6ae7, 91f1377, fe78a58) confirmed in git log.
