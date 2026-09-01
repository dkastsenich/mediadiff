---
phase: 02-core-engine
reviewed: 2026-09-01T18:48:45Z
depth: standard
files_reviewed: 24
files_reviewed_list:
  - src/cli/main.cpp
  - src/cli/tty_render.cpp
  - src/cli/options.h
  - src/cli/options.cpp
  - src/cli/color_policy.h
  - src/cli/color_policy.cpp
  - src/cli/commands/dir.cpp
  - src/cli/commands/inspect.cpp
  - src/cli/commands/snapshot.cpp
  - src/cli/worker_pool.cpp
  - src/compare/dist.cpp
  - src/compare/span.cpp
  - src/compare/tol.cpp
  - src/core/profiles.cpp
  - src/core/rational.h
  - src/core/serializer.h
  - src/core/serializer.cpp
  - src/core/snapshot.cpp
  - src/util/fs.h
  - .gitattributes
  - .github/workflows/ci.yml
  - scripts/lint_check_id_strings.sh
  - scripts/lint_dead_code_after_fail.sh
  - scripts/lint_fixture_case_collisions.sh
  - tools/gen_registry.py
  - tests/process_spawn.h
  - tests/support/golden.cpp
findings:
  critical: 1
  warning: 3
  info: 0
  total: 4
status: issues_found
---

# Phase 02: Code Review Report

**Reviewed:** 2026-09-01T18:48:45Z
**Depth:** standard
**Files Reviewed:** 24 read in full (see `files_reviewed_list`) out of 169 in scope
**Status:** issues_found

## Summary

This is a re-review focused on the 20+ production/tooling files that changed after the
2026-08-15 full review and its 2026-08-16 gap-closure follow-up, per this run's explicit
scope instructions, plus a spot-check of two high-priority test helpers
(`tests/process_spawn.h`, `tests/support/golden.cpp`) and the two lint scripts wired into
CI during the round-3/4 gap-closure work.

**Confirmed as fixed and correctly implemented:** all six findings from the 2026-08-15
review (CR-01 uncaught-exception/silent-pass on a type-poisoned snapshot, CR-02 the
`--json` float-formatting bypass, CR-03 unchecked int64 overflow in `tol`/`dist`, WR-01
digit-parsing overflow, WR-02 `NO_COLOR`/`GITHUB_ACTIONS` precedence, WR-03 the
`compare_ticks` overflow-as-tie fallback in `span.cpp`) are genuinely fixed in the current
tree, and the fixes are sound on direct reading — `core/rational.h`'s
`checked_mul`/`checked_sub`/`checked_add`/`checked_negate` are used consistently and
correctly throughout `compare/tol.cpp` and `compare/dist.cpp`; `compare_ticks_checked`'s
`TickOrder{0, true}` overflow signal is correctly threaded through every real-decision
comparison in `compare/span.cpp` and gates a `Status::error` verdict, never a fabricated
one. `core/serializer.cpp`'s `write_scalar`/`serialize_document`/`serialize_value_compact`
remain the single `std::to_chars` call site as claimed. IN-01 (TOCTOU on the snapshot
write gate) remains open exactly as recorded, deliberately unfixed — not re-reported.
T-2-33 (no control-byte filtering in `tty_render.cpp`) remains open exactly as recorded in
`02-SECURITY.md`; I found no additional unfiltered sink beyond the two already named there.

**New findings below.** The most consequential is CR-04: one of the two round-3/4
"permanent portability gates" wired into the required `lint (ENG-16 boundary)` merge check
has a false-negative in its own detection logic, verified by direct execution — it can
report a repository clean over the exact shape of dead-code-after-`FAIL()` it exists to
catch. Three further Warnings cover a missing magnitude/range validation gap on untrusted
snapshot values (type-checked but not range-checked, so CR-03's overflow guards prevent UB
but not a semantically wrong verdict from a crafted negative/zero magnitude), a Windows-only
command-line construction bug that can bypass the `snapshot` command's overwrite-protection
gate via argument injection, and an asymmetry between the two newly-added lint scripts (both
self-testing) and an older sibling lint that still has no self-test.

## What I did not reach

Given the file-scope's own stated priority order, I spent the review budget on `src/`
production files that changed since the last review, `tools/gen_registry.py`, the CI lint
wiring, and two named test helpers. I did **not** re-read in full: `src/report/{json,junit,
markdown,model}.cpp` (only grepped for the CR-02 pattern, not re-audited beyond that),
`src/core/{policy,tolerance,glob,value,model}.{h,cpp}`, `src/config/toml_load.cpp`,
`src/cli/commands/{compare,explain,list_checks}.cpp`, `src/cli/{dir_pairing,
provenance_render,exit_code}.cpp`, `src/util/version.cpp`, `src/compare/{engine,exact,
hash,presence,set}.cpp`, or any of the ~86 files under `tests/` beyond the two named above
(in particular `tests/integration/*.cpp`, `tests/unit/*.cpp`, and `tests/support/{stub_
analyzer,fixture_paths,utf8_path}.h` were not read). Fixtures and docs under `tests/
fixtures/`, `tests/golden/`, and `docs/checks/` were not read. This is a partial review;
treat anything not listed in `files_reviewed_list` as unaudited by this pass.

## Critical Issues

### CR-04: `lint_dead_code_after_fail.sh` fails to detect dead code that follows a `FAIL(...)` call on the *same* line — a proven false-negative in a required merge gate

**File:** `scripts/lint_dead_code_after_fail.sh:77-115` (the `AWK_PROGRAM` state machine)
**Issue:** The script's own purpose (its header, lines 3-25) is to permanently catch the
exact shape that already cost this phase two build-portability rounds: a statement
following a Catch2 `FAIL(...)` call before its enclosing block closes, which MSVC's
`/W4 /WX` promotes from `C4702` (unreachable code) to a hard build error (`C2220`) on a
toolchain leg this project's own CI has never actually compiled these test TUs with. The
detector is a three-state `awk` scan; state 1 ("consuming the failure statement") only
advances to state 2 when the **current line's own trimmed text** ends in `);` — i.e. it
assumes the `FAIL(...)`'s own closing `);` is the last thing on its line. When a statement
follows `FAIL(...)` on the *same physical line* and that trailing statement does not
itself happen to end in `);`, the scanner never reaches state 2 for that block, and the
violation is never printed. Verified directly by running the script's own `AWK_PROGRAM`
against a synthetic fixture:

```cpp
int f() {
  FAIL("x"); int y = 5;
  return y;
}
```

This reports **clean (exit 0)** — no violation — despite being exactly the
dead-code-after-`FAIL()` shape the lint exists to catch (and exactly the shape MSVC's flow
analysis would flag as unreachable). By contrast, a case where the trailing statement
*happens* to end in `);` (e.g. `FAIL("x"); int d = compute();`) is caught, purely
coincidentally, because the scanner's suffix match against `compute();` also satisfies its
"end of FAIL statement" condition. The script's own self-test control clause (lines
117-139) only exercises the multi-line-argument case (`FAIL(\n "x");`), never the
same-line-with-trailing-code case, which is exactly why this gap was never caught by the
lint's own anti-regression discipline — the same "verification that had only ever been run
one way" root cause this phase's own `02-13-PLAN.md` already names for a different lint.
No source file in `tests/` currently has this shape (verified via `grep`), so this is not
today masking a live defect — but it is a proven, reproducible hole in a required
status-check (`lint (ENG-16 boundary)`) whose entire stated purpose is to be the reliable
detector for this exact class of cross-toolchain break, on a Windows leg the project's own
`02-14-PLAN.md` documents has never compiled these test TUs. Per this project's own stated
position (a lint that can silently pass over its own target shape "launders a false
assurance into the merge gate," `02-SECURITY.md` T-02-14-02/T-02-15-03), this is Critical:
the gate can be green while the defect it is required to catch is present.

**Fix:** Broaden the state-1 termination condition so it fires the instant a line contains
a `FAIL(...)`-terminating `);`, regardless of what follows on the same line — split the
line at the first `);` after the `FAIL(` match and evaluate whatever trailing text remains
on that same line under state 2's own "blank / comment / `#` / leading `}`" rule, rather
than requiring `);` to be the line's own suffix. At minimum, add a second self-test fixture
covering the same-line case (`FAIL("x"); int y = 5;\n  return y;\n}`) so this specific gap
cannot silently regress once fixed, mirroring the discipline already applied to the
multi-line case.

## Warnings

### WR-04: Untrusted-snapshot values are type-checked but never range-checked, letting a crafted magnitude produce a semantically wrong (not UB) verdict

**File:** `src/core/serializer.cpp:291-321` (`value_from_json`'s `rational` and
`histogram` branches), `:347-364` (`histogram`); consumed at `src/compare/dist.cpp:85-102`
and `src/compare/tol.cpp:37-45`
**Issue:** CR-01's fix (confirmed correct on this reading) validates that `RationalValue.
num`/`.den`/`tb.num`/`tb.den` and `Histogram` bin `count` are JSON integers before calling
`.get<std::int64_t>()`, and CR-03's fix (also confirmed correct) routes every subsequent
cross-multiplication through overflow-checked arithmetic. Neither fix validates the
*magnitude* of these untrusted values: `RationalValue.den` can be parsed as `0` or
negative, and a `Histogram` bin's `count` can be negative — both pass every existing check.
`compare/dist.cpp:101-102` degrades a non-positive bin total to `a_total = 1`/`b_total = 1`
("no meaningful proportion... always agrees" — a deliberate design choice for the
zero-bins case), but this same fallback silently engages for a *negative* total produced
by a crafted snapshot with negative bin counts, coercing what should be a large or
meaningless proportion into "compare against denominator 1" — this can make a real
difference look disproportionately large (false fail) or a real difference look
negligible relative to a fabricated denominator of 1 (false pass) depending on which side
carries the negative counts. Similarly, a `RationalValue` with `den <= 0` breaks the
"num/den is the value's magnitude in the check's declared unit" invariant `compare/tol.
cpp`'s own header comment states, without being rejected. None of this is UB (CR-03's
guards hold), but it is exactly the "confident wrong answer" failure class this project's
own design principles single out, reachable from an untrusted `.snap.json` on the `dir`/
`compare` path with no crash and no diagnostic distinguishing it from a legitimate result.
**Fix:** Extend `value_from_json`'s `rational` branch to reject `den <= 0` (and, if the
project wants to bound `tb.den` too, the same there) and the `histogram` branch to reject
a negative `count`, both as `ErrorKind::input_unsupported` — mirroring the type-check
CR-01 already added at the same call sites, just one property deeper (range, not just
type).

### WR-05: `snapshot` command's Windows git-tracked check builds an unescaped `CreateProcessA` command line, allowing argument injection that can bypass the overwrite-protection gate

**File:** `src/cli/commands/snapshot.cpp:74-107` (`spawn_git_ls_files`, `_WIN32` branch)
**Issue:** The Windows implementation assembles `git`'s command line by naive string
concatenation with no escaping of characters with special meaning to Windows' own
argv-tokenization rules: `cmdline = "git -C \"" + target.dir + "\" ls-files
--error-unmatch -- \"" + target.filename + "\""`, then hands it to `CreateProcessA`
directly. `target.dir`/`target.filename` derive from `split_path(out_path)`, where
`out_path` is the resolved `--out` path (or a suffix-appended `<file>`), both ultimately
sourced from CLI argv the invoking process/script supplies. A filename or directory
component containing an embedded `"` is not escaped before concatenation, so it can
terminate the intended quoted argument early and inject additional tokens into `git`'s own
argv as parsed by its C-runtime startup (e.g. a filename `x" -C "C:\attacker\path` turns
into an extra `-C "C:\attacker\path"` argument redirecting which repository `git
ls-files --error-unmatch` consults) — silently changing which repository's tracked-file
state the SNAP-07 "refuse to overwrite a git-tracked snapshot without `--force`" gate
(`T-2-24` in `02-SECURITY.md`, recorded closed) checks against, potentially making a
tracked file appear untracked and letting `write_snapshot_gated` proceed without
`--force`. The POSIX branch two lines above (`posix_spawnp` with a `std::vector<char*>
argv`) is correctly immune to this — arguments are passed as a real argv array, never
tokenized from a single string — and the function's own header comment ("Both halves are
handed to `git` as argv/cwd, never through a shell, so no quoting/injection concern
applies either," lines 33-37) states a guarantee that is true for that POSIX branch but
false for this Windows one: `CreateProcessA`'s `lpCommandLine` is parsed by the *target
process's own* C-runtime argv tokenizer using the same quote/backslash rules a shell would
use, so a hand-built, unescaped command-line string is exactly the "no shell involved but
still injectable" case this comment's own reasoning misses.
**Fix:** Escape embedded `"` (and trailing `\` runs immediately before a quote, per the
MSVC/CRT argv-parsing rules) in `target.dir` and `target.filename` before concatenation,
or avoid manual quoting entirely by building the command line through a helper that
applies the documented Windows `CommandLineToArgvW`-compatible escaping (the same escaping
`tests/process_spawn.h`'s Windows `spawn_and_capture` would need for the same reason, if
its own args are ever attacker-influenced).

### WR-06: `lint_check_id_strings.sh` has no self-test control clause, unlike its two round-3/4 siblings wired into the same required gate

**File:** `scripts/lint_check_id_strings.sh`
**Issue:** This script is one of four steps in the required `lint (ENG-16 boundary)` job
(`.github/workflows/ci.yml`). The two lints added during this phase's round-3/4
gap-closure work (`lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`) both
run a synthetic known-bad fixture through their own matcher before the real scan and
refuse to proceed if the matcher doesn't fire — a discipline the project adopted
specifically in response to `02-SECURITY.md`'s T-02-14-02/T-02-15-03 ("a lint that has
silently stopped matching reports clean forever and launders a false assurance into the
merge gate"). `lint_check_id_strings.sh` predates that discipline and has no equivalent
self-test. Its own header comment states plainly that "Phase 2 has no analyzer sources
yet... this lint passes trivially today" — `src/analyzers/` holds only `.gitkeep` files,
so this lint's `PATTERN` regex has never actually been exercised against real matching
content in this repository; a latent regex defect (e.g. the same class of anchoring/suffix
mistake CR-04 above demonstrates in a sibling lint) would not surface until Phase 3's
first analyzer source lands, and would then have exactly the "silently stopped matching /
never actually started matching" failure mode the other two lints were built to guard
against.
**Fix:** Add a self-test control clause identical in shape to the other two scripts: write
a synthetic file under a temp directory containing an obviously-matching hardcoded dotted
check-id string literal (e.g. `const char* x = "meta.tool_version";` outside a comment),
run the same `grep` pattern against it, and refuse to report the real scan as clean if the
matcher does not fire against the synthetic fixture.

---

_Reviewed: 2026-09-01T18:48:45Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
