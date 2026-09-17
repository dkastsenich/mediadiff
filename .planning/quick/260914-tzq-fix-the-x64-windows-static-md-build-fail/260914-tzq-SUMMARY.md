---
phase: quick
plan: 260914-tzq
subsystem: testing
tags: [ci, msvc, catch2, getenv, lint, ffmpeg-decode-only-lgpl]

requires:
  - phase: quick-260913-wuy
    provides: Windows ffmpeg pin CRLF fix, drafted PR #5's designated-leg CI plumbing
provides:
  - "tests/unit/test_golden.cpp's DesignatedLegGuard reads MEDIADIFF_DESIGNATED_LEG through mediadiff::getenv_utf8 instead of a raw std::getenv call, removing the x64-windows-static-md leg's only MSVC C2220/C4996 build error under /W4 /WX"
  - "scripts/lint_getenv_shim.sh: a permanent, self-testing, bash-3.2-clean CI lint that blocks any future first-party raw C environment accessor call outside src/util/fs.h"
affects: [ci, build-05, phase-04-pr-5]

actuals:
  tokens: 3960
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Sandbox-based negative-control demonstration for a lint (mktemp -d + cp + git show <sha>:<path>, never re-dirtying the tracked working tree) to prove a gate catches a fixed defect"

key-files:
  created:
    - scripts/lint_getenv_shim.sh
  modified:
    - tests/unit/test_golden.cpp
    - .github/workflows/ci.yml

key-decisions:
  - "DesignatedLegGuard's constructor rewritten to call mediadiff::getenv_utf8(\"MEDIADIFF_DESIGNATED_LEG\") (qualified, since the call site is at global anonymous-namespace scope, not inside namespace mediadiff::test) instead of std::getenv; had_value = existing.has_value() and previous = *existing preserve the exact same unset-vs-set-to-empty distinction the old null-pointer test made."
  - "scripts/lint_getenv_shim.sh's matcher uses four boundary-anchored awk checks (bare/std::/::-qualified getenv, getenv_s, _dupenv_s, _wdupenv_s) rather than six, since the boundary technique (^|[^A-Za-z0-9_]) already makes one bare-getenv( pattern cover all three getenv(/std::getenv(/::getenv( spellings without a separate alternative, and the same boundary class prevents _dupenv_s( from double-firing on a _wdupenv_s( line."
  - "The lint's allowlist is exactly one file, src/util/fs.h, matching the live grep recorded in PLAN.md's design_decisions verbatim; guarded by a non-vacuous-allowlist check that re-runs the matcher against src/util/fs.h alone and refuses if it stops firing."
  - "The negative control runs against a mktemp -d sandbox populated via git show ad12765:tests/unit/test_golden.cpp, never by re-dirtying the tracked working tree."

requirements-completed: [BUILD-05, CLI-09, TRUST-06]

coverage:
  - id: D1
    description: "tests/unit/test_golden.cpp's DesignatedLegGuard reads MEDIADIFF_DESIGNATED_LEG exclusively through mediadiff::getenv_utf8; unset/empty/non-empty restore semantics are bit-for-bit unchanged"
    requirement: "BUILD-05"
    verification:
      - kind: unit
        ref: "ctest --preset x64-linux -R golden (5 unit.golden: cases pass by name, including the four-state boundary case)"
        status: pass
      - kind: unit
        ref: "ctest --preset x64-linux --output-on-failure (771 tests, 0 failures)"
        status: pass
    human_judgment: false
  - id: D2
    description: "scripts/lint_getenv_shim.sh permanently blocks any future raw C environment accessor call outside src/util/fs.h, with a demonstrated negative control against the pinned pre-fix source"
    requirement: "TRUST-06"
    verification:
      - kind: other
        ref: "bash scripts/lint_getenv_shim.sh (clean, exit 0, self-test OK, 205 files scanned)"
        status: pass
      - kind: other
        ref: "mktemp -d sandbox against git show ad12765:tests/unit/test_golden.cpp (PASS_NEGATIVE_CONTROL, names tests/unit/test_golden.cpp:112, exit 1)"
        status: pass
      - kind: other
        ref: "same sandbox with the current fixed test_golden.cpp substituted (exit 0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The new lint is wired into the CI lint job directly after the bash-3.2 portability step, and is itself bash-3.2 clean"
    requirement: "CLI-09"
    verification:
      - kind: other
        ref: "bash scripts/lint_bash4_builtins.sh (Scanned 19 file(s), clean)"
        status: pass
      - kind: other
        ref: "python3 -c 'import yaml; yaml.safe_load(open(\".github/workflows/ci.yml\"))' (parses)"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-09-14
status: complete
---

# Quick Task 260914-tzq: Fix the x64-windows-static-md build failure — Summary

**Routed `test_golden.cpp`'s one remaining raw `std::getenv` call through the project's existing `mediadiff::getenv_utf8` shim (removing draft PR #5's only MSVC `/W4 /WX` build error) and added a permanent, self-testing CI lint that makes this class of defect impossible to reintroduce.**

## Performance

- **Duration:** ~35 min
- **Tasks:** 2/2 completed
- **Files modified:** 3 (`tests/unit/test_golden.cpp`, new `scripts/lint_getenv_shim.sh`, `.github/workflows/ci.yml`)

## Accomplishments

- `DesignatedLegGuard`'s constructor in `tests/unit/test_golden.cpp` no longer calls `std::getenv` directly; it now reads `MEDIADIFF_DESIGNATED_LEG` through `mediadiff::getenv_utf8` (`src/util/fs.h`), the repository's single permitted first-party call site for the raw C environment accessor. This was the only line in the run named by draft PR #5's MSVC error (`test_golden.cpp(112): error C2220` from `warning C4996`, run 34886767317, job 104119231073, step 23 "Build").
- The `had_value`/`previous` restore semantics are unchanged byte-for-byte: `getenv_utf8`'s `std::optional<std::string>` return distinguishes "unset" from "set to the empty string" exactly as the old null-pointer test did. The four-state boundary test (`unset` / `""` / `"1"` / `"x64-linux"`) passes by name, unmoved.
- Added `scripts/lint_getenv_shim.sh`: a new, permanent CI lint modeled on `scripts/lint_bash4_builtins.sh`'s shape (location-anchored, bash-3.2-safe file enumeration, one shared awk matcher, a self-test control clause run before every real scan, and a non-vacuous-allowlist guard). It flags any first-party call to `getenv(`/`std::getenv(`/`::getenv(`/`getenv_s(`/`_dupenv_s(`/`_wdupenv_s(` outside `src/util/fs.h`.
- Wired one new two-line step, "Run getenv shim lint (MSVC C4996 guard)", into the CI `lint` job directly after the existing bash-3.2 portability step. The protected job name (`lint (ENG-16 boundary)`, a required status-check context on ruleset 20862843) was not touched.

## Task Commits

Each task was committed atomically:

1. **Task 1: Route test_golden.cpp's designated-leg read through the getenv_utf8 shim** - `dd5be50` (fix)
2. **Task 2: Add scripts/lint_getenv_shim.sh and wire it into the CI lint job** - `47f02c4` (fix)

No separate plan-metadata commit was made per the harness contract (docs artifacts are committed by the orchestrator in Step 8, not by this executor).

## Deviations from Plan

None — plan executed exactly as written. All four scoped files (`tests/unit/test_golden.cpp`, `scripts/lint_getenv_shim.sh`, `.github/workflows/ci.yml`, plus this SUMMARY.md) match the plan's `files_modified` list exactly; no other file was touched.

## Verification Evidence

All commands run from the repository root, in the order the plan's `<verification>` section specifies.

### 1. Build

```
$ cmake --build --preset x64-linux
[0/2] Re-checking globbed directories...
[1/2] Building CXX object tests/unit/CMakeFiles/mediadiff_unit_tests.dir/test_golden.cpp.o
[2/2] Linking CXX executable tests/unit/mediadiff_unit_tests
```

### 2. Full ctest sweep

```
$ ctest --preset x64-linux --output-on-failure
...
100% tests passed, 0 tests failed out of 771

Total Test time (real) =   6.27 sec

The following tests did not run:
	 57 - unit.console_vt - virtual-terminal processing is enabled on a real console (Skipped)
	158 - unit.inspect_container - golden: the container+meta section for one representative fixture per family (Skipped)
	490 - unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden (Skipped)
	491 - unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden (Skipped)
	492 - unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden (Skipped)
	751 - integration.size_checks - the size.* findings are pinned by a committed, read-only golden (Skipped)
```

771 tests, 0 failures — unchanged from the plan's stated baseline. The six Skipped tests are the pre-existing D-GAP-01 designated-leg/TSDuck baseline, confirmed not a regression.

### 3. `-R golden` sweep

```
$ ctest --preset x64-linux -R golden --output-on-failure
...
100% tests passed, 0 tests failed out of 15
```

All five `unit.golden:` cases (including "MEDIADIFF_DESIGNATED_LEG must be set AND non-empty to claim the leg", the four-state boundary case) pass by name. The five designated-leg/TSDuck goldens remain Skipped, matching the pre-existing baseline exactly.

### 4. `lint_getenv_shim.sh` — negative control

```
$ D=$(mktemp -d) && mkdir -p "$D/scripts" "$D/src/util" "$D/tests/unit" "$D/tools" \
  && cp scripts/lint_getenv_shim.sh "$D/scripts/" \
  && cp src/util/fs.h "$D/src/util/" \
  && git show ad12765:tests/unit/test_golden.cpp > "$D/tests/unit/test_golden.cpp" \
  && bash "$D/scripts/lint_getenv_shim.sh"
lint_getenv_shim.sh: self-test OK -- the real defect's verbatim text was flagged, the same accessor spelled inside a leading-// comment was correctly ignored, the shim call itself stayed clean, and all four remaining raw-accessor spellings were independently flagged.
getenv-shim violation: a raw C environment accessor was found outside its single permitted call site (src/util/fs.h):
tests/unit/test_golden.cpp:112: getenv( (bare, std::getenv( or ::getenv( -- raw C environment accessor)
Replace the flagged call with mediadiff::getenv_utf8 (src/util/fs.h). This class of defect fails MSVC's /W4 /WX build with C2220/C4996 (BUILD-05) -- not a style preference.
[exit 1]
```

Result: **PASS_NEGATIVE_CONTROL** — non-zero exit, names `tests/unit/test_golden.cpp:112` exactly.

### 5. `lint_getenv_shim.sh` — positive control (same sandbox, current fixed file substituted)

```
$ cp tests/unit/test_golden.cpp "$D2/tests/unit/test_golden.cpp" && bash "$D2/scripts/lint_getenv_shim.sh"
lint_getenv_shim.sh: self-test OK -- the real defect's verbatim text was flagged, the same accessor spelled inside a leading-// comment was correctly ignored, the shim call itself stayed clean, and all four remaining raw-accessor spellings were independently flagged.
lint_getenv_shim.sh: clean. Scanned 1 file(s) under src tests tools/ (*.cpp,*.h,*.hpp; src/util/fs.h excluded as the single permitted call site); no first-party use of the raw C environment accessor found.
[exit 0]
```

Confirms the sandbox itself is not unconditionally red — it correctly distinguishes the pre-fix blob from the fixed tree.

### 6. `lint_getenv_shim.sh` — real scan against the committed tree

```
$ bash scripts/lint_getenv_shim.sh
lint_getenv_shim.sh: self-test OK -- the real defect's verbatim text was flagged, the same accessor spelled inside a leading-// comment was correctly ignored, the shim call itself stayed clean, and all four remaining raw-accessor spellings were independently flagged.
lint_getenv_shim.sh: clean. Scanned 205 file(s) under src tests tools/ (*.cpp,*.h,*.hpp; src/util/fs.h excluded as the single permitted call site); no first-party use of the raw C environment accessor found.
[exit 0]
```

205 files scanned (206 found under `src`, `tests`, `tools` minus the one allowlisted `src/util/fs.h`), matching the plan's live-grep-derived expectation exactly.

### 7. `lint_bash4_builtins.sh`

```
$ bash scripts/lint_bash4_builtins.sh
lint_bash4_builtins.sh: self-test OK -- a known-bad construct was flagged, the same construct inside a comment was correctly ignored, and a known-good line stayed clean.
lint_bash4_builtins.sh: clean. Scanned 19 file(s) under scripts/*.sh; no bash-3.2-incompatible construct found.
[exit 0]
```

Reports "Scanned 19 file(s)" as required — the new `scripts/lint_getenv_shim.sh` is itself bash-3.2 clean and is included in the scan.

### 8. `lint_dead_code_after_fail.sh`

```
$ bash scripts/lint_dead_code_after_fail.sh
lint_dead_code_after_fail.sh: clean. Scanned 96 file(s) under tests; no statement follows a FAIL() call before its enclosing block closes.
[exit 0]
```

### 9. YAML parse check

```
$ python3 -c 'import yaml; yaml.safe_load(open(".github/workflows/ci.yml"))'
[exit 0, no output]
```

`.github/workflows/ci.yml` still parses as valid YAML after the insertion.

### 10. `git diff --stat` baseline (src unchanged)

```
$ git diff --stat 8caf1f1 HEAD -- src | tail -1
 24 files changed, 5533 insertions(+), 42 deletions(-)
```

Unchanged from the planning-time baseline — confirms no `src/` file was touched by either task.

### 11. Grep-based acceptance criteria (Task 1)

```
mediadiff::getenv_utf8("MEDIADIFF_DESIGNATED_LEG") call count: 1
#include "util/fs.h" count: 1
#include <cstdlib> count: 1 (preserved for setenv/unsetenv/_putenv_s)
raw getenv(...) on a non-comment line: 0
same pipeline over git show ad12765:tests/unit/test_golden.cpp: 1
git diff --name-only (Task 1): tests/unit/test_golden.cpp
```

### 12. Grep-based acceptance criteria (Task 2)

```
grep -cF 'run: bash scripts/lint_getenv_shim.sh' .github/workflows/ci.yml → 1
grep -cF 'name: Run getenv shim lint (MSVC C4996 guard)' .github/workflows/ci.yml → 1
grep -cF 'name: lint (ENG-16 boundary)' .github/workflows/ci.yml → 1 (protected job name untouched)
```

## Commit Hashes

- `dd5be50` — `fix(quick-260914-tzq): read MEDIADIFF_DESIGNATED_LEG through the getenv_utf8 shim`
- `47f02c4` — `fix(quick-260914-tzq): add getenv shim lint and wire it into the CI lint job`

## What Is NOT Proven Here

Per the plan's own success criteria: whether the `x64-windows-static-md` leg actually reaches and passes Build is provable only by a real CI run on a pushed commit, which this task deliberately does not perform (no push was made; per constraints, this task never pushes). What IS proven locally, with real command output recorded above: the single C4996 site the run named (`test_golden.cpp:112`) is gone, the x64-linux build and full 771-test sweep are unaffected, and a permanent lint now exists that would catch this exact defect (by file:line, against the pinned pre-fix source) if it were ever reintroduced.

## Known Stubs

None.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or schema changes at trust boundaries were introduced. The two files changed beyond the test file (a new lint script and a CI workflow step) are internal developer tooling with no runtime attack surface, matching the plan's own `T-TZQ-SC` disposition (not applicable, no package installed).

## Self-Check: PASSED

- `tests/unit/test_golden.cpp` — FOUND (modified, confirmed via `git diff --name-only`)
- `scripts/lint_getenv_shim.sh` — FOUND (mode 0755, confirmed via `test -x`)
- `.github/workflows/ci.yml` — FOUND (modified, confirmed via `git diff --name-only`)
- `dd5be50` — FOUND in `git log --oneline`
- `47f02c4` — FOUND in `git log --oneline`
