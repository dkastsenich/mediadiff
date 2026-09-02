---
quick_id: 260902-it6
slug: migrate-cli-option-binding
type: quick
created: 2026-09-02
branch: chore/cli-option-binding
forked_from: 64c5c42
implements: [D-05, D-06]
autonomous: true
requirements: [CLI-01, CLI-04, CLI-08, CLI-10]
user_setup: []

files_modified:
  - src/cli/options.h
  - src/cli/options.cpp
  - src/cli/main.cpp
  - src/cli/commands/compare.cpp
  - src/cli/commands/dir.cpp
  - src/cli/commands/snapshot.cpp
  - src/cli/commands/list_checks.cpp
  - src/cli/commands/explain.cpp
  - src/cli/commands/inspect.cpp

estimate:
  tokens: 80000
  raw_tokens: 55000
  tasks: 6
  confidence: low   # no calibration samples for this repo yet

must_haves:
  truths:
    - "Every CLI flag, default, description, parse error and exit code behaves exactly as it did at 64c5c42 (D-05)."
    - "`ctest` reports exactly 303 tests, 0 failed, 1 skipped — the same count as the baseline."
    - "`tests/integration/test_exit_codes.cpp` passes with zero edits to the file."
    - "`mediadiff --help` and all six subcommand `--help` outputs are byte-identical to the captured baseline."
    - "`grep -rc make_shared src/cli --include='*.cpp'` totals exactly 1, and that 1 is the deliberate `--threads` binding."
  artifacts:
    - .planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt
    - src/cli/options.h        # PolicyArgs/ReportArgs/ColorArgs/CliOptions hold CLI::Option*
    - src/cli/options.cpp      # opt_string / opt_flag / opt_strings accessors
  key_links:
    - "options.h struct member types -> every consumer in compare/dir/inspect/list_checks/main (one atomic change)."
    - "opt_strings() count()==0 guard -> parse_cli_overrides / parse_report_destinations receiving {} not {\"\"}."
    - "dir.cpp --threads bound int -> CLI::ParseError at parse time -> exit 64 usage contract."
---

<objective>
Replace the `shared_ptr`-per-option lifetime hack in `src/cli` with borrowed `CLI::Option*`,
per **D-05**. The `App` already owns its options (`std::vector<Option_p> options_`,
`App.hpp:174`), so an `Option*` outlives the registration function for free — no allocation,
no refcount.

**D-06** makes this a standalone prerequisite task on its own branch, completed *before*
Phase 3 execution begins, so Phase 3 starts from the clean pattern without owning a refactor
of Phase 2's CLI.

Purpose: this is a **pure refactor**. Same flags, same defaults, same parse errors, same exit
codes, same help text. The entire value of the task is that it is *provably*
behavior-preserving, so the verification story below is the real deliverable — the code change
is mechanical.

Output: 38 `make_shared` sites reduced to exactly 1 deliberate residual.
</objective>

<context>
@.planning/phases/03-probe-layer-container-size/03-CONTEXT.md   # D-05 and D-06 are the contract
@src/cli/options.h
@src/cli/options.cpp
</context>

<research_findings>
## Verified against the pinned CLI11 (2.6.2, `vcpkg/packages/cli11_x64-linux/include/CLI/`)

These were confirmed by compiling and running a probe against the pinned headers + `libCLI11.a`,
not by reading docs. **Two of them are landmines. Read this section before writing any code.**

| Call on an **unset** option | Actual result | Consequence |
|---|---|---|
| `as<std::string>()` | `""`, `count()==0` | Safe — matches today's `make_shared<std::string>()` default |
| `as<bool>()` | `false`, `count()==0` | Safe, but D-05 mandates `->count()` for flags |
| `as<std::vector<std::string>>()` | **`{""}` — size 1, containing one empty string** | ⚠ **LANDMINE 1** |
| `as<int>()` | `0` (empty string converts to 0, no throw) | Safe *for the unset case only* |
| bare `--json` (`->expected(0,1)`) | `count()==1`, `as<std::string>()` → `""` | Safe — preserves the existing "flag given, no path" distinction |

### ⚠ Landmine 1 — the vector case is NOT an empty vector

`Option::results(T&)` (`Option.hpp:735-741`) does `res.emplace_back()` when `results_` is empty
and no default string is set, producing **one empty string**, then lexical-converts. For
`std::vector<std::string>` that yields `{""}`, not `{}`.

`--set`, `--tol` and `--report` are all `std::vector<std::string>`. An unguarded
`as<std::vector<std::string>>()` would hand `parse_cli_overrides` a single malformed argument
`""` → "malformed --set argument ''" → **exit 64 on every single invocation**.

**Mitigation:** `opt_strings()` MUST short-circuit on `count() == 0`. This is why Task 2 exists
as its own reviewable commit.

*Silver lining:* the existing suite catches this loudly if botched — a `{""}` breaks essentially
every compare test at once, not subtly.

### ⚠ Landmine 2 — untargeted `add_option` drops parse-time type validation

Probe results for `--threads`:

```
BOUND      --threads abc  -> ParseError(ConversionError) at parse    -> main.cpp maps to exit 64
UNTARGETED --threads abc  -> parse OK; as<int>() throws IN CALLBACK  -> different diagnostic
BOUND      --threads 3.5  -> ParseError(ConversionError) at parse
UNTARGETED --threads 3.5  -> parse OK; as<int>() throws IN CALLBACK
```

`ConversionError` derives from `CLI::ParseError`, and CLI11 runs subcommand callbacks *inside*
`app.parse()` — so `main.cpp:118`'s `catch (const CLI::ParseError&)` would still catch it and
still return 64. **The exit code survives; the diagnostic does not.** The failure also moves out
of parse and into the middle of the callback, past other side effects.

D-05's exception is therefore honored exactly as written: **`--threads` keeps its bound
variable.** Do not convert it. Do not "fix" it with `->check(CLI::PositiveNumber)` either —
`--threads -1` and `--threads 0` currently parse fine and are rejected by a *runtime* check with
the message `mediadiff: --threads must be a positive integer` (`dir.cpp:236`); a validator would
move that to parse time with different text, which is a behavior change and out of scope.

Note: no test currently asserts `--threads` error text, so nothing mechanically enforces this.
It is honored because D-05 says so.

### Design consequence — `default_*_args()` and null pointers

`main.cpp:171` calls `run_compare(..., default_report_args(), default_policy_args(),
default_color_args())` for the CLI-01 implicit two-positional route. Those factories build the
structs with **no App and therefore no options at all**. Once members are `CLI::Option*` they are
all `nullptr`, and every reader must tolerate null.

This is not new: `ReportArgs::json_option` is already `nullptr` there today, and both
`compare.cpp:200` and `inspect.cpp:192` already guard with
`json_option != nullptr && json_option->count() > 0`. The migration generalizes that existing,
proven shape to every member — which is what makes the three accessors in Task 2 the right seam.

### Thread-safety note for `dir.cpp`

`Option::results()` writes to the `mutable proc_results_` cache, so concurrent `as<>()` calls on
the same `Option` are a data race. Today `dir.cpp`'s worker `job` lambda reads no options
(all ~15 `options.*` reads sit outside the `WorkerPool` region), so no race exists.
**Task 5 must preserve that** by materializing option values into locals once at the top of the
callback rather than calling `as<>()` at each use site.
</research_findings>

<artifacts_produced>
## Artifacts this task produces

**New symbols in `src/cli/options.h` / `options.cpp`:**

| Symbol | Signature | Behavior |
|---|---|---|
| `opt_string` | `std::string opt_string(const CLI::Option* o)` | `(o && o->count() > 0) ? o->as<std::string>() : std::string{}` |
| `opt_flag` | `bool opt_flag(const CLI::Option* o)` | `o != nullptr && o->count() > 0` |
| `opt_strings` | `std::vector<std::string> opt_strings(const CLI::Option* o)` | `(o && o->count() > 0) ? o->as<std::vector<std::string>>() : std::vector<std::string>{}` — **the `count()` guard is mandatory, see Landmine 1** |

**Changed symbols (types only — every function signature is unchanged):**

| Struct | Before | After |
|---|---|---|
| `PolicyArgs` | 4 × `shared_ptr` | `CLI::Option* profile, config_path, set_flags, tol_flags` (all `= nullptr`) |
| `ReportArgs` | 2 × `shared_ptr` + `Option* json_option` | `CLI::Option* json_option, report_flags` — `json_path` **removed**, folded into `json_option` |
| `ColorArgs` | 2 × `shared_ptr` | `CLI::Option* no_color, ascii` |
| `CliOptions` | 3 structs + 3 × `shared_ptr` | 3 structs + `CLI::Option* strict, quiet, verbose` |

`default_policy_args()` / `default_report_args()` / `default_color_args()` collapse to
`return {};` — all-null structs the accessors already handle.

`ReportArgs::json_path` disappearing is deliberate: `json_option` alone now answers both
questions it previously took two members to answer — "was the flag given" (`count() > 0`) and
"with what path" (`opt_string`, `""` meaning stdout).

**Removed:** 37 of 38 `std::make_shared` calls in `src/cli`.
</artifacts_produced>

<tasks>

<task type="auto">
  <name>Task 1: Capture the behavioral baseline before touching any code</name>
  <files>.planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt</files>
  <read_first>
    - `tests/golden/README.md` — goldens are read-only in CI; `UPDATE_GOLDENS` is local-only (Phase 2 D-12). This task does NOT touch goldens.
  </read_first>
  <precondition>`build/x64-linux/mediadiff` exists and is built from `64c5c42` with no working-tree source edits. This capture is irreproducible once migration starts.</precondition>
  <action>
    Build at the current HEAD, then capture the complete help surface into ONE delimited file so
    the post-refactor comparison is a single `diff`.

    Build: `cmake --build --preset x64-linux`.

    Write `baseline-help.txt` by appending, for each of `""` (root), `compare`, `inspect`,
    `explain`, `list-checks`, `snapshot`, `dir`: a delimiter line `===== help: <name> =====`
    followed by the combined stdout+stderr of `mediadiff <name> --help`.

    Record the baseline test count in the same file under a `===== ctest baseline =====`
    delimiter, from `ctest --test-dir build/x64-linux` (expect: 303 tests, 0 failed, 1 skipped).

    Also record `grep -rc make_shared src/cli --include='*.cpp'` per-file output under a
    `===== make_shared baseline =====` delimiter (expect the 38-site table).

    Commit this file. It is the evidence artifact the whole task is verified against — do not
    delete it in a later task.
  </action>
  <verify>
    <automated>test -s .planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt &amp;&amp; grep -c '^===== help:' .planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt | grep -qx 7 &amp;&amp; grep -q 'Total Tests: 303' .planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt</automated>
  </verify>
  <acceptance_criteria>
    - [ ] `baseline-help.txt` contains exactly 7 `===== help:` sections (root + 6 subcommands). **Locally provable**
    - [ ] The file records `Total Tests: 303` and a 38-total `make_shared` census. **Locally provable**
    - [ ] Zero files under `src/` are modified by this task. **Locally provable**
  </acceptance_criteria>
  <done>The pre-refactor help surface and test/site census are committed and diffable.</done>
</task>

<task type="auto">
  <name>Task 2: Add the three Option* accessors (purely additive, no type changes)</name>
  <files>src/cli/options.h, src/cli/options.cpp</files>
  <read_first>
    - `src/cli/options.h` — the four structs and every doc comment describing the `shared_ptr`-per-flag shape.
    - The **Landmine 1** subsection above — the `count()` guard in `opt_strings` is the entire point of this commit.
  </read_first>
  <action>
    Add `opt_string`, `opt_flag` and `opt_strings` to `options.h` (declarations) and
    `options.cpp` (definitions), with the exact bodies given in "Artifacts this task produces".

    Change nothing else. No struct member types change in this commit, no call site changes.
    The build and all 303 tests must pass with the accessors present but unused.

    Document on `opt_strings` **why** the `count() == 0` early return is mandatory and not
    defensive padding: cite `Option.hpp:735-741`'s `res.emplace_back()` and state that an
    unguarded `as<std::vector<std::string>>()` on an unset option returns a one-element vector
    holding an empty string, which `parse_cli_overrides` would reject as a malformed argument.

    Isolating this as its own commit is deliberate: it is the one genuinely subtle line in the
    refactor, and it gets to be reviewed on its own rather than buried in Task 3's large diff.

    Note for the reviewer: unused-function warnings are not emitted for non-static functions with
    external linkage, so this compiles clean under `-Wall -Wextra -Werror` despite having no
    callers yet.
  </action>
  <verify>
    <automated>cmake --build --preset x64-linux 2>&amp;1 | grep -qiE 'warning|error' &amp;&amp; exit 1; ctest --test-dir build/x64-linux 2>&amp;1 | grep -q '303 tests' </automated>
  </verify>
  <acceptance_criteria>
    - [ ] `opt_strings` returns `{}` (empty) when the option is null or `count()==0`; it never returns `{""}`. **Locally provable**
    - [ ] Clean build under `-Wall -Wextra -Werror`. **Locally provable**
    - [ ] 303 tests, 0 failed, 1 skipped — unchanged. **Locally provable**
    - [ ] No struct member type changed in this commit. **Locally provable**
  </acceptance_criteria>
  <done>The three accessors exist, are documented, and the tree still builds green with them unused.</done>
</task>

<task type="auto">
  <name>Task 3: Flip the four shared structs to CLI::Option* and update every consumer</name>
  <files>src/cli/options.h, src/cli/options.cpp, src/cli/commands/compare.cpp, src/cli/commands/dir.cpp, src/cli/commands/inspect.cpp, src/cli/commands/list_checks.cpp, src/cli/main.cpp</files>
  <read_first>
    - `src/cli/options.h` — all four structs plus the `default_*_args()` contract comment.
    - `src/cli/options.cpp:25-46` (`add_policy_flags`), `:119-132` (`add_report_flags`), `:192-202` (`add_color_flags`), `:204-243` (`default_*_args`, `add_common_options`), `:271-280` (`read_color_inputs`).
    - `src/cli/commands/compare.cpp:109-111, 128-129, 196-200` — the `run_compare` signature and the existing null-`json_option` guard.
    - `src/cli/commands/dir.cpp:139, 153-211` — read sites only; dir's own local options are Task 5.
    - `src/cli/commands/inspect.cpp:143, 163-194` and `src/cli/commands/list_checks.cpp:93`.
    - `src/cli/main.cpp:171` — the `default_*_args()` call for the CLI-01 implicit route.
  </read_first>
  <action>
    **This commit is atomic by necessity** — a half-migrated shared struct does not compile, so
    the type flip and every consumer update land together per **D-05**.

    1. In `options.h`, change all four structs' members to `CLI::Option*`, each defaulted to
       `nullptr`. Delete `ReportArgs::json_path` (folded into `json_option`).
    2. In `options.cpp`, rewrite `add_policy_flags` / `add_report_flags` / `add_color_flags` /
       `add_common_options` to use the untargeted overloads — `add_option(name, description)` and
       `add_flag(name, description)` — assigning the returned `Option*` to the struct member.
       **Preserve every description string byte-for-byte** and keep `--json`'s `->expected(0, 1)`.
    3. `default_policy_args()` / `default_report_args()` / `default_color_args()` become
       `return {};`.
    4. `read_color_inputs` reads `opt_flag(args.no_color)` / `opt_flag(args.ascii)`. Its signature
       does not change.
    5. Update every consumer read: `*x.profile` → `opt_string(x.profile)`,
       `*x.set_flags` → `opt_strings(x.set_flags)`, `*options.strict` → `opt_flag(options.strict)`,
       and so on. The `json_requested` expressions at `compare.cpp:200`, `dir.cpp:207` and
       `inspect.cpp:192` already have the right shape — they simplify to
       `opt_flag(report_args.json_option)`. `*options.report.json_path` becomes
       `opt_string(options.report.json_option)`.
    6. Update the struct doc comments in `options.h`. They currently describe the
       `shared_ptr`-per-flag shape and explicitly claim a `shared_ptr<std::string>` alone cannot
       distinguish "flag absent" from "flag given, no path" — that rationale is now obsolete and
       must be rewritten to describe the borrowed-`Option*` shape and the all-null
       `default_*_args()` case. Leaving stale comments here would actively mislead Phase 3.
    7. Leave `#include <memory>` in `options.h` alone unless the build is clean without it.
       Removing a header that other TUs get transitively is a classic way to break one toolchain
       and not another; toolchain parity is a hard constraint and this repo has already lost two
       gap-closure rounds to a GCC-vs-MSVC conflict. Only drop it if nothing else in the header
       needs it, and treat any doubt as "leave it".

    Do **not** touch `dir.cpp`'s `--threads` binding or any command's own local options — those
    are Tasks 4 and 5. Do not add, remove or rename any option.
  </action>
  <verify>
    <automated>cmake --build --preset x64-linux &amp;&amp; ctest --test-dir build/x64-linux 2>&amp;1 | grep -q '303 tests, 0 failed' &amp;&amp; ! grep -q 'shared_ptr' src/cli/options.h</automated>
  </verify>
  <acceptance_criteria>
    - [ ] `src/cli/options.h` contains zero occurrences of `shared_ptr`. **Locally provable**
    - [ ] `grep -c make_shared src/cli/options.cpp` returns 0. **Locally provable**
    - [ ] Clean build under `-Wall -Wextra -Werror`. **Locally provable**
    - [ ] 303 tests, 0 failed, 1 skipped. **Locally provable**
    - [ ] `test_exit_codes.cpp` passes with the file unmodified (`git diff --exit-code tests/`). **Locally provable**
    - [ ] Every option description string is byte-identical to `64c5c42`. **Locally provable** (Task 6 diff)
    - [ ] Struct doc comments describe the `Option*` shape; no comment still claims a `shared_ptr` is needed for lifetime or for the `--json` distinction. **Locally provable**
  </acceptance_criteria>
  <done>The shared option structs carry borrowed `Option*`s, every consumer reads through the accessors, and the suite is green at 303.</done>
</task>

<task type="auto">
  <name>Task 4: Migrate the four simple commands' local options</name>
  <files>src/cli/commands/snapshot.cpp, src/cli/commands/list_checks.cpp, src/cli/commands/explain.cpp, src/cli/commands/inspect.cpp</files>
  <read_first>
    - `src/cli/commands/snapshot.cpp:257-263` — `input_path`, `out_path`, `force_flag`.
    - `src/cli/commands/list_checks.cpp:85-95` — `effective_flag`, `verbose_flag`.
    - `src/cli/commands/explain.cpp:31-38` — `check_id_text`, `->required()`.
    - `src/cli/commands/inspect.cpp:140-149` — `file_path`, `->required()`.
  </read_first>
  <action>
    Migrate the 7 remaining local `make_shared` sites in these four files to `CLI::Option*`
    captured by value in the callback, reading through `opt_string` / `opt_flag` per D-05.

    `->required()` chains are unaffected — the untargeted `add_option(name, description)` returns
    the same `Option*` the targeted overload does, so `->required()` still applies and still
    produces the same parse-time `RequiredError` (and therefore the same exit 64).

    Capturing a raw `Option*` by value in the callback is exactly as safe as the `shared_ptr` was:
    the `App` owns the `Option` for the whole program lifetime and the callback only runs during
    `app.parse()`.

    These four files have no interaction with the shared structs beyond what Task 3 already
    landed, and no numerics — they are the mechanical remainder.
  </action>
  <verify>
    <automated>cmake --build --preset x64-linux &amp;&amp; ctest --test-dir build/x64-linux 2>&amp;1 | grep -q '303 tests, 0 failed' &amp;&amp; test "$(grep -hc make_shared src/cli/commands/snapshot.cpp src/cli/commands/list_checks.cpp src/cli/commands/explain.cpp src/cli/commands/inspect.cpp | paste -sd+ | bc)" = 0</automated>
  </verify>
  <acceptance_criteria>
    - [ ] Zero `make_shared` in snapshot.cpp, list_checks.cpp, explain.cpp, inspect.cpp. **Locally provable**
    - [ ] `tests/golden/list_checks_effective.txt` still matches without `UPDATE_GOLDENS`. **Locally provable**
    - [ ] Clean build under `-Wall -Wextra -Werror`; 303 tests, 0 failed. **Locally provable**
  </acceptance_criteria>
  <done>All four simple commands bind through `Option*`; goldens and exit codes unchanged.</done>
</task>

<task type="auto">
  <name>Task 5: Migrate compare.cpp and dir.cpp locals, preserving the --threads exception</name>
  <files>src/cli/commands/compare.cpp, src/cli/commands/dir.cpp</files>
  <read_first>
    - `src/cli/commands/compare.cpp:97-118` — the two required positionals plus `--strict` / `-v` / `-q`, and the callback capture list.
    - `src/cli/commands/dir.cpp:125-145` — the two required positionals, `--threads` (bound + `threads_opt`), `--content`, `--no-content`.
    - `src/cli/commands/dir.cpp:228-248` — the `--threads` resolution ladder and its runtime positive-integer check.
    - `src/cli/commands/dir.cpp:352-458` — the `WorkerPool` region; confirm no `options.*` read moves inside the worker `job`.
    - The **Landmine 2** and **Thread-safety** subsections above.
  </read_first>
  <action>
    **compare.cpp:** migrate all 5 local sites (`baseline_path`, `candidate_path`, `strict_flag`,
    `verbose_flag`, `quiet_flag`) to `Option*`. `run_compare`'s signature is unchanged — it still
    takes `bool strict, bool verbose, bool quiet` plus the three structs by const-ref; the callback
    just supplies `opt_string(...)` / `opt_flag(...)` at the call. `main.cpp`'s implicit-compare
    route continues to pass literal `false`s and the now-empty `default_*_args()`.

    **dir.cpp:** migrate `baseline_dir`, `candidate_dir`, `content_flag`, `no_content_flag` to
    `Option*` (4 sites).

    **KEEP `auto threads = std::make_shared<int>(0);` and its targeted
    `add_option("--threads", *threads, ...)` exactly as they are.** This is D-05's explicit
    numeric exception, and Landmine 2 shows why: the untargeted overload is not templated, so
    `--threads abc` would stop failing at parse time and start throwing from inside the callback
    with a worse diagnostic. `threads_opt` is already an `Option*` and already drives the
    `count() > 0` branch at `dir.cpp:234` — that ladder stays byte-identical, including the
    runtime `mediadiff: --threads must be a positive integer` message. Do not add a `->check()`
    validator; that would move `--threads -1` and `--threads 0` rejection to parse time with new
    text, which is a behavior change. Do not touch the `--threads` clamp question (T-2-41) — Phase
    3 D-01 owns it.

    **Materialize option values into locals once at the top of the `dir` callback** (e.g.
    `const bool strict = opt_flag(options.strict);`) and use those locals at the ~15 downstream
    sites, instead of calling `as<>()` repeatedly. Two reasons: `Option::results()` writes a
    `mutable proc_results_` cache, so this structurally guarantees no `as<>()` call can ever drift
    into the `WorkerPool` region and become a data race; and it keeps the diff in the report-
    writing body to a simple identifier substitution.
  </action>
  <verify>
    <automated>cmake --build --preset x64-linux &amp;&amp; ctest --test-dir build/x64-linux 2>&amp;1 | grep -q '303 tests, 0 failed' &amp;&amp; test "$(grep -c make_shared src/cli/commands/compare.cpp)" = 0 &amp;&amp; test "$(grep -c make_shared src/cli/commands/dir.cpp)" = 1</automated>
  </verify>
  <acceptance_criteria>
    - [ ] `grep -c make_shared src/cli/commands/compare.cpp` returns 0. **Locally provable**
    - [ ] `grep -c make_shared src/cli/commands/dir.cpp` returns exactly 1, and it is the `--threads` binding. **Locally provable**
    - [ ] `--threads` is still registered via the **targeted** `add_option(name, bound_int, desc)` overload. **Locally provable**
    - [ ] `mediadiff dir a b --threads abc` still exits 64; `--threads 0` still prints `mediadiff: --threads must be a positive integer`. **Locally provable**
    - [ ] No `opt_*`/`as<>()` call appears inside `dir.cpp`'s worker `job` lambda. **Locally provable**
    - [ ] `tests/golden/dir_worst_n.txt` still matches; 303 tests, 0 failed. **Locally provable**
  </acceptance_criteria>
  <done>compare and dir bind through `Option*` except the one deliberate `--threads` variable; the exit-64 usage contract is intact.</done>
</task>

<task type="auto">
  <name>Task 6: Prove behavior preservation and reconcile the residual count</name>
  <files>.planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt</files>
  <read_first>
    - `.planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/baseline-help.txt` — the Task 1 capture.
  </read_first>
  <precondition>Tasks 1-5 are committed and the working tree is clean.</precondition>
  <action>
    Close the loop on the one claim this task exists to make: nothing observable changed.

    1. **Help diff.** Re-capture the same 7 help outputs using the identical delimiter format and
       `diff` them against the `===== help: =====` sections of `baseline-help.txt`. The diff must
       be empty. This is the check that catches an accidentally reworded description or a dropped
       default that no test asserts. If it is non-empty, the refactor is wrong — fix the code,
       never the baseline.
    2. **Test count.** `ctest --test-dir build/x64-linux` reports exactly **303 tests, 0 failed,
       1 skipped**. The count must not move in either direction: this refactor adds no behavior
       and therefore no tests, so a higher number means something was added and a lower number
       means something was lost.
    3. **Exit-code contract.** Confirm `git diff 64c5c42 --stat -- tests/` is empty — every test
       file, `test_exit_codes.cpp` above all, passes unmodified. Its `== 65` × 2 and `== 64` × 4
       assertions are the ones this refactor could most plausibly have broken.
    4. **Lints.** All four scripts exit 0: `lint_eng16.sh`, `lint_check_id_strings.sh`,
       `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`. (`lint_eng16.sh` scans
       `src/util` for bare `stdout`/`stderr`; this task touches no file under `src/util`, so it
       should be trivially green — run it anyway.)
    5. **Residual census.** `grep -rc make_shared src/cli --include='*.cpp'` must total **1**,
       down from 38. Append the post-refactor census to `baseline-help.txt` under a
       `===== make_shared after =====` delimiter, naming the single survivor explicitly:

       > `src/cli/commands/dir.cpp` — 1 site: the `--threads` bound `int`, retained per D-05's
       > typed-numeric exception so that a non-integer argument keeps failing as a parse-time
       > `CLI::ParseError` (exit 64) rather than throwing from inside the callback.

       This makes the residual a deliberate, named figure rather than an unexplained remainder.
    6. Note in the commit message that MSVC `/W4 /WX` is the sole **CI-only** criterion.
  </action>
  <verify>
    <automated>test "$(grep -rhc make_shared src/cli --include='*.cpp' | paste -sd+ | bc)" = 1 &amp;&amp; git diff 64c5c42 --stat -- tests/ | grep -q . &amp;&amp; exit 1; for s in eng16 check_id_strings dead_code_after_fail fixture_case_collisions; do ./scripts/lint_$s.sh || exit 1; done; ctest --test-dir build/x64-linux 2>&amp;1 | grep -q '303 tests, 0 failed'</automated>
  </verify>
  <acceptance_criteria>
    - [ ] Root + 6 subcommand `--help` outputs are byte-identical to the Task 1 baseline. **Locally provable**
    - [ ] Exactly 303 tests, 0 failed, 1 skipped. **Locally provable**
    - [ ] `git diff 64c5c42 -- tests/` is empty — no test file was modified. **Locally provable**
    - [ ] All four lint scripts exit 0. **Locally provable**
    - [ ] `make_shared` in `src/cli/**/*.cpp` totals exactly 1, named and justified in the artifact. **Locally provable**
    - [ ] Clean build under `-Wall -Wextra -Werror` on GCC. **Locally provable**
    - [ ] Clean build under MSVC `/W4 /WX`, AppleClang and Clang. **CI-only** — the 3-OS matrix is the only place this is observable.
  </acceptance_criteria>
  <done>The refactor is demonstrated behavior-preserving by help diff, test count, untouched exit-code tests, four green lints, and a justified residual of 1.</done>
</task>

</tasks>

<verification>
## Verification summary

| Criterion | Expected | Where provable |
|---|---|---|
| Test count | 303 tests, 0 failed, 1 skipped | **Locally provable** |
| `test_exit_codes.cpp` | passes, file unmodified (`== 65` ×2, `== 64` ×4) | **Locally provable** |
| Help output, root + 6 subcommands | byte-identical to baseline | **Locally provable** |
| Goldens (`list_checks_effective.txt`, `dir_worst_n.txt`, …) | match without `UPDATE_GOLDENS` | **Locally provable** |
| `lint_eng16.sh`, `lint_check_id_strings.sh`, `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh` | all exit 0 | **Locally provable** |
| `make_shared` census in `src/cli/**/*.cpp` | 38 → **1** (`dir.cpp` `--threads`) | **Locally provable** |
| `-Wall -Wextra -Werror`, GCC | clean | **Locally provable** |
| MSVC `/W4 /WX`, AppleClang, Clang | clean | **CI-only** |

**Residual `make_shared` reconciliation**

| File | Before | After | Reason for residual |
|---|---:|---:|---|
| `src/cli/options.cpp` | 19 | 0 | — |
| `src/cli/commands/compare.cpp` | 5 | 0 | — |
| `src/cli/commands/dir.cpp` | 5 | **1** | `--threads` keeps a bound `int` per D-05's typed-numeric exception |
| `src/cli/commands/snapshot.cpp` | 3 | 0 | — |
| `src/cli/commands/list_checks.cpp` | 2 | 0 | — |
| `src/cli/main.cpp` | 2 | 0 | — |
| `src/cli/commands/explain.cpp` | 1 | 0 | — |
| `src/cli/commands/inspect.cpp` | 1 | 0 | — |
| **Total** | **38** | **1** | |

**Not a threat-model task.** No new attack surface, no new input handling, no new parsing — the
same argv reaches the same validators. `<threat_model>` is deliberately omitted.
</verification>

<success_criteria>
- All 38 `shared_ptr` option sites are migrated to borrowed `CLI::Option*` except the single
  documented `--threads` exception, per D-05.
- Zero behavior change: same flags, defaults, descriptions, parse errors, exit codes, help text.
- 303 tests, 0 failed, 1 skipped — unchanged from `64c5c42`.
- `tests/` is byte-identical to `64c5c42`.
- Four lint scripts green; clean `-Wall -Wextra -Werror` build locally; MSVC `/W4 /WX` green in CI.
- Phase 3 can be planned assuming the clean `Option*` pattern is already in place (D-06).
</success_criteria>

<out_of_scope>
Do not touch, per the task contract:
- `src/cli/exit_code.{h,cpp}` — the mapping is correct and verified.
- `src/cli/tty_render.cpp` — T-2-33 control-byte filtering is assigned to Phase 3.
- `src/report/json.cpp` — WINDOWS.md window #1 (null `delta`/`evidence`) is a later phase.
- The `--threads` clamp (T-2-41 residual) — Phase 3 D-01 gives it a home.
- Adding, removing or renaming any CLI option.
- `src/util/**` — nothing here needs it, and `lint_eng16.sh` guards it.
</out_of_scope>

<output>
Create `.planning/quick/260902-it6-migrate-cli-option-binding-from-shared-p/SUMMARY.md` when done.
</output>
