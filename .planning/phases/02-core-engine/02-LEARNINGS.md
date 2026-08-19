---
phase: 02
phase_name: "core-engine"
project: "mediadiff"
generated: "2026-08-19"
counts:
  decisions: 10
  lessons: 12
  patterns: 10
  surprises: 8
missing_artifacts: []
---

# Phase 02 Learnings: core-engine

> Phase 2 ran 19 plans across four gap-closure rounds. The dominant story is not any single
> defect — it is that **each round's fix worked and immediately exposed the layer behind it**,
> because Windows and macOS had never executed the code the previous blocker was hiding. That
> shape is the most transferable thing in this document.

## Decisions

### D-01: Generate the registry from `checks.def` at build time
A Python 3 build-time generator emits the check enum, metadata table and `--explain` documents.

**Rationale:** Every check added in Phases 3–7 is declared through this mechanism. Rated **costly** to reverse — changing it later means rewriting `checks.def`, the generator, and potentially every call site.
**Source:** 02-CONTEXT.md (D-01, D-02, D-05)

### D-09: The registry's declared value kind is authoritative — a mismatch is an error, never a coercion
A measurement whose runtime type disagrees with its declared `value_kind` produces `Status::error`, not a silent conversion.

**Rationale:** Coercion would let a wrong-typed analyzer output pass as a valid comparison. Enforced at *two* layers after execution revealed one was insufficient (see Lessons).
**Source:** 02-CONTEXT.md (D-09); 02-04-SUMMARY.md

### D-14/D-15: Fail-first coverage is each semantic crossed with each status, not merely pass-versus-fail
Every comparison semantic must declare a fixture that passes and one that must not pass, and the harness fails if either is missing.

**Rationale:** "It can fail" is a weaker claim than "it can reach every status it declares." Rated **costly** — every check added in Phases 3–7 inherits the requirement.
**Source:** 02-CONTEXT.md (D-14, D-15)

### Gap-closure plans declare the requirement IDs they *restore*, not the phase's own
Plans 02-12…02-19 declare `[BUILD-01, BUILD-05]` — Phase-1-owned IDs — rather than re-claiming any of Phase 2's own IDs.

**Rationale:** Phase 2's code regressed Phase 1's "builds clean on 3 OSes / CI matrix green" guarantees without REQUIREMENTS.md being updated. The closure plans restore those, so citing them is accurate; re-claiming Phase 2's already-Complete IDs would be the error.
**Source:** 02-VERIFICATION.md §Requirements Coverage; 02-12 through 02-19 PLAN frontmatter

### Windows binary mode applies to both stdout and stderr, unconditionally
`_setmode(_fileno(stdout), _O_BINARY)` and the stderr equivalent are the first two statements of `wmain`.

**Rationale:** Chosen over stdout-only so no stream is left where Windows output silently differs from POSIX. Placed before `enable_vt_output()` and before any early error path can write.
**Source:** 02-17-PLAN.md Task 2 (checkpoint:decision, answered by human 2026-08-18); 02-17-SUMMARY.md

### `.gitattributes` and the stdout binary-mode fix ship in one commit
The two changes were deliberately coupled rather than sequenced cheapest-first.

**Rationale:** Four golden tests passed on Windows only because CRLF-on-disk matched CRLF-from-stdout — two bugs cancelling. Landing `.gitattributes` alone would have converted 3 failing tests into 4 different failing ones.
**Source:** 02-17-PLAN.md objective; 02-19-SUMMARY.md

### The `_fileno(stdout)` call is pinned to `src/cli/main.cpp`
Explicitly *not* placed in `src/util/fs.h` beside `enable_vt_output`, its natural-looking home.

**Rationale:** `scripts/lint_eng16.sh` scans `src/util` for the bare tokens `stdout`/`stderr`. Putting it there would have failed the **required** merge-gate context `lint (ENG-16 boundary)`.
**Source:** 02-17-PLAN.md hard constraints; 02-19-SUMMARY.md

### `test_explain_inspect.cpp` was deliberately left unmodified through round 4
The file carrying failing test #247 was excluded from every plan's `files_modified`.

**Rationale:** Designed experiment. Its `split_lines` splits on `'\n'` only, so if the stdout fix made it pass, that *proves* it was downstream; modifying it would have destroyed the signal. It passed.
**Source:** 02-17-PLAN.md; 02-19-SUMMARY.md

### Two CI legs are permanently deferred, not fixed
`CI-x64-osx` (cross-arch libs) and `CI-arm64-linux` (NuGet feed step) stay red and out of scope.

**Rationale:** Neither is blocking, both reproduce on `main`, and both predate Phase 2. Re-confirmed deferred by human decision at each of rounds 3 and 4 rather than assumed.
**Source:** 02-UAT.md §Pre-Existing CI Failures; `round_3_verdict`, `round_4_verdict`

### UAT test 2 (Windows console colour) deferred to Phase 3
Left open rather than blocking phase completion, once a Windows artifact finally existed.

**Rationale:** Needs a human at real Windows hardware; no automation closes it. The build artifact now exists at sha `d727425`, so it is schedulable rather than blocked.
**Source:** 02-UAT.md `round_4_verdict.uat_test_2`

---

## Lessons

### Grep-based acceptance criteria are content-blind and will match your own comments
This bit **four separate times**: a comment naming `strtod`/`atof`/`stod` to explain what the parser does *not* use; a comment naming `expect.frame_rate` to explain why it is deferred; a comment quoting `\+` to explain the BSD-sed trap; and a comment referencing `FAIL()`'s throw. Each tripped its own plan's zero-occurrence check.

**Context:** The fix was identical every time — describe the *property* without naming the literal grep target. Worth writing acceptance greps that scope to code, or worth knowing this failure mode on sight.
**Source:** 02-04-SUMMARY.md, 02-05-SUMMARY.md, 02-13-SUMMARY.md, 02-14-SUMMARY.md

### A first fix passing local scrutiny is not the same as a target CI leg going green
Every round predicted this in its own `scope_caveat`, and every round was right.

**Context:** Round 1→2 cleared six `getenv` sites, letting MSVC reach a translation unit it had never compiled. Round 2→3 cleared that, letting the Windows *test suite* run for the first time — 7 failures. Rounds 3→4 cleared those. Budget for the next layer rather than treating it as failure.
**Source:** 02-14-PLAN.md, 02-15-PLAN.md `scope_caveat`; 02-UAT.md rounds 2–4

### Two bugs can cancel out and look like passing tests
Four golden tests passed on Windows because the golden file was checked out CRLF and the spawned binary emitted CRLF.

**Context:** Found via a 7-of-7 producer correlation: the three goldens compared against an in-process render failed; the four compared against spawned-binary stdout passed. Same files, same read path — the producer was the only variable. Fixing one side alone would have broken the other four.
**Source:** 02-17-PLAN.md; 02-19-SUMMARY.md

### The exit-code table was correct; the argv classifier was the bug
`exit_code_for(input_open)` returned 65 unconditionally the whole time. CLI11's `allow_windows_style_options_` defaults to `true` under `_WIN32`, so `/no/such/file.snap.json` parsed as an *option* and never reached `read_snapshot`.

**Context:** The gap record originally pointed at the exit-code mapping. "Fixing" it would have broken a correct table to paper over a parsing bug — and every other `/`-prefixed path a Windows user passes would still have misparsed.
**Source:** 02-19-PLAN.md Task 1; 02-19-SUMMARY.md

### A byte-count delta can name its own cause
Test #95 expected 82500 bytes and got 85000 — exactly 2500 more, matching the newline count.

**Context:** That arithmetic identified LF→CRLF expansion precisely. But it pointed at the *Python child's* text-mode stdout, not mediadiff's — an early reading that grouped it with the golden failures was wrong.
**Source:** 02-16-SUMMARY.md; 02-18-SUMMARY.md

### Fresh git worktrees have no build tree and an uninitialized submodule
Hit in **four** separate plan executions (02-12, 02-14, 02-15, 02-17).

**Context:** `build/` is gitignored and not shared; `vcpkg` needs `git submodule update --init`. The pinned `ffmpeg` port's tree object is often unreachable under a shallow clone, requiring `git fetch --unshallow`. A shared `~/.cache/vcpkg/archives` turns a 15–40 min FFmpeg build into a ~11-second cache restore.
**Source:** 02-12, 02-14, 02-15, 02-17 SUMMARY.md

### `ctest -R` is a regex, not a substring match
`-R unit.pool` matched zero tests because `.` matches exactly one character, and the cases were named `unit.worker_pool: …`.

**Context:** Renaming cases to `"pool - …"` fixed it. A filter that silently matches nothing looks identical to a suite that passes.
**Source:** 02-11-SUMMARY.md

### GCC 13 rejects an exhaustive `enum class` switch with no `default:` and no trailing statement
`-Werror=return-type` fires even when every enumerator has a `case`.

**Context:** Resolved with a trailing fallback `return` after the switch and still no `default:` label — satisfying `-Wswitch` exhaustiveness and `-Wreturn-type` simultaneously. Verified with a standalone compile probe before writing the real code.
**Source:** 02-01-SUMMARY.md

### `-Wmissing-field-initializers` fires on pre-existing call sites when a struct grows a trailing field
Adding fields to `Policy` and `CheckDef` broke four files that were out of scope to edit.

**Context:** Giving the new fields in-class default member initializers suppresses the warning at every existing call site with zero edits to them. Used twice (02-05, 02-09).
**Source:** 02-05-SUMMARY.md, 02-09-SUMMARY.md

### `std::to_chars` omits the decimal point for whole doubles, and nlohmann then reads them as integers
`1.0` → `"1"`, `-0.0` → `"-0"`; integers have no signed zero, so `-0.0` would silently round-trip to `0.0`.

**Context:** Fixed by appending `.0` to any float token lacking a decimal/exponent marker — deterministic and idempotent across repeated write-read-write cycles. Directly load-bearing for the determinism guarantee.
**Source:** 02-07-SUMMARY.md

### MSVC's `rename()` does not replace an existing destination; POSIX's does
`snapshot --force`'s overwrite idempotency would have silently failed on Windows the first time a target existed.

**Context:** Resolved with `MoveFileExW` + `MOVEFILE_REPLACE_EXISTING` on Windows behind a `util/fs.h` helper, keeping wide-character types confined to that header.
**Source:** 02-07-SUMMARY.md

### `std::variant`'s `operator==` requires every alternative to be equality-comparable
`Value`'s six aggregate alternatives had no `operator==` when first declared, so the first comparator that needed `a.value == b.value` failed to compile.

**Context:** A foreseeable completion of the type's design rather than a later-task scope violation — worth designing in when the variant is first written.
**Source:** 02-01-SUMMARY.md

---

## Patterns

### Verify library behavior by reading the library, not by recalling it
toml++'s table iteration order was confirmed by reading `toml++/impl/table.hpp` directly; the vcpkg package/target names for the JSON schema validator were confirmed against the pinned baseline submodule; CLI11's `allow_windows_style_options_` default was confirmed against v2.6.2 source.

**When to use:** Any time an implementation decision depends on third-party behavior that "everyone knows." All three of these would have been wrong from memory.
**Source:** 02-06-SUMMARY.md, 02-08-SUMMARY.md, 02-19-PLAN.md

### Compile-probe an unfamiliar toolchain rule before writing the real code
The GCC exhaustive-switch rule was settled by compiling both the failing and passing forms standalone first.

**When to use:** When a warnings-as-errors interaction is suspected but not certain — cheaper than discovering it mid-implementation across many call sites.
**Source:** 02-01-SUMMARY.md

### Default member initializers instead of editing out-of-scope files
Adding a trailing struct field breaks aggregate-init call sites; a default initializer restores them all without touching any.

**When to use:** Whenever a shared struct grows a field and the call sites belong to other plans. Keeps a plan inside its declared `files_modified`.
**Source:** 02-05-SUMMARY.md, 02-09-SUMMARY.md

### Deliberately leave a file unmodified to preserve a diagnostic signal
`test_explain_inspect.cpp` was excluded from every round-4 plan so its eventual pass would *prove* it was downstream rather than coincide with a fix.

**When to use:** When you suspect failure B is caused by defect A. Fixing only A converts B into an experiment with a real answer; fixing both destroys the information.
**Source:** 02-17-PLAN.md; 02-19-SUMMARY.md

### Lint scripts carry a self-test control clause
Both new portability lints fire on a synthetic known-bad fixture before every real run, and refuse to report clean on a zero-file scan.

**When to use:** Any permanent gate wired into CI. A lint that always passes looks exactly like coverage while providing none — the failure mode this project treats as P0.
**Source:** 02-14-SUMMARY.md, 02-15-SUMMARY.md

### Describe the property, never name the literal grep target
The recurring fix for comments colliding with their own acceptance greps.

**When to use:** Writing any comment in a file whose plan has a zero-occurrence check. Applies to CI YAML as much as to C++.
**Source:** 02-04, 02-05, 02-13, 02-14 SUMMARY.md

### Reuse the main checkout's `vcpkg_installed` from a worktree
`-DVCPKG_INSTALLED_DIR=<main>/build/x64-linux/vcpkg_installed -DVCPKG_MANIFEST_INSTALL=OFF` skips a 15–40 minute dependency build.

**When to use:** Any parallel-worktree execution on this project. Only the worktree's own sources then need compiling.
**Source:** 02-15-SUMMARY.md

### Select a CI run by headSha equality, never "the most recent"
Every round-3 and round-4 CI read matched `headSha` against the pushed HEAD before reading a single log line.

**When to use:** Always. This phase was misled once by a run that predated its fixes and spent a full round on a false premise.
**Source:** 02-16-PLAN.md, 02-19-PLAN.md; 02-UAT.md round-2 evidence

### Label every acceptance criterion locally-provable or CI-only
Adopted from round 2 onward, after a round where local success was mistaken for platform success.

**When to use:** Any cross-platform project whose dev host cannot exercise all targets. Forces the plan to state what it can and cannot prove instead of blurring it.
**Source:** 02-14, 02-15, 02-16, 02-17, 02-18, 02-19 PLAN.md

### Enforce a type invariant at every construction path, not just the deserialization one
The D-09 value-kind guard was added to `compare/engine.cpp`'s pairing loop after it emerged that `read_snapshot`'s gate only covered measurements loaded from disk, not ones constructed in-process — the path every analyzer from Phase 3 onward uses.

**When to use:** When a guard sits at an I/O boundary but the type can also be built in memory.
**Source:** 02-04-SUMMARY.md

---

## Surprises

### The Windows test suite had never executed in the project's history
The build stopped at step `[32/97]` for two full rounds. Only after round 3 did any Windows test run at all — and 7 of 297 failed immediately.

**Impact:** Reframed rounds 3 and 4 as discovery rather than repair, and made "a new first failure is budgeted, not a regression" an explicit planning assumption.
**Source:** 02-UAT.md G-02-3 `scope_caveat`; 02-16-SUMMARY.md

### Four required CI contexts had never been green simultaneously until the final run
Run 32153890395 was the first in the project's history with `lint`, `x64-linux`, `arm64-osx` and `x64-windows-static-md` all passing at once.

**Impact:** The merge gate had never actually been satisfiable before this phase closed.
**Source:** 02-19-SUMMARY.md; 02-VERIFICATION.md

### Byte-identical `--json` was a stated project constraint with no test behind it
Nothing asserted it, and no CI leg could observe it — file destinations opened binary while stdout did not.

**Impact:** Now asserted on all three blocking legs by a dedicated parity test. A headline determinism guarantee was unverified for the whole phase.
**Source:** 02-VERIFICATION.md (round-3 finding); 02-17-SUMMARY.md

### `CLI-05` and `CLI-09` turned out to be Phase-1-owned, not Phase 2's
Discovered during the sealing verification by reading REQUIREMENTS.md directly rather than trusting the phase's stated ID list.

**Impact:** Phase 2's true CLI set is 8 IDs, not 10. Corrected in the sealing report.
**Source:** 02-VERIFICATION.md (round-4)

### Adding two lint steps nearly orphaned a required merge gate
The CI job is named `lint (ENG-16 boundary)`, and that exact string is a required context in the branch ruleset. Adding non-ENG-16 lints to it makes a tidy-up rename look obviously correct.

**Impact:** Forbidden explicitly, asserted by a grep, and documented as a comment inside `ci.yml` so the next person sees the trap before making it.
**Source:** 02-16-PLAN.md; 02-19-SUMMARY.md

### A one-line CI fix was blocked by an OAuth scope, not by the fix
Pushing any commit touching `.github/workflows/` requires the `workflow` OAuth scope over HTTPS; the available token lacked it, and the default SSH key authenticated as an account without write access.

**Impact:** Halted a plan mid-execution twice. Worked around per-push with an SSH identity override; still unresolved at the environment level.
**Source:** 02-16-SUMMARY.md; 02-19 dispatch record

### The registry generator's own fixture became invalid when the generator got stricter
`tests/fixtures/registry/good/docs/good.sample.md` — the known-good fixture — was correctly rejected by the new structure check it predated.

**Impact:** A reminder that a generator's own fixtures are consumers of its rules and must migrate with them.
**Source:** 02-09-SUMMARY.md

### CLI11 raises `ExtrasError` before the fallthrough path the research had described
`mediadiff compare a b c` produces exit 64 through CLI11's own extras handling, not through the parent-positional fallthrough the plan anticipated.

**Impact:** The guard written for the documented path is empirically unreached today. Kept as defensive coverage against a future CLI11 version, with the discrepancy recorded rather than silently dropped.
**Source:** 02-10-SUMMARY.md
