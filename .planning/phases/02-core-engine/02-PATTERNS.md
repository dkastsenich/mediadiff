# Phase 2: Core Engine - Pattern Map

**Mapped:** 2026-08-15
**Files analyzed:** ~45 (new files across core/, config/, compare/, report/, cli/, tools/, tests/)
**Analogs found:** 5 strong in-repo analogs / 0 pre-existing files in the target directories (core/, config/, compare/, report/ are empty `.gitkeep` scaffolds — this phase is greenfield for those trees)

## Context

Phase 1 left `src/core/`, `src/config/`, `src/compare/`, `src/report/` empty (only `.gitkeep`). There is **no in-repo precedent** for a controller/service/registry/comparator file in this codebase yet. The closest real analogs are the four files Phase 1 actually built: `src/util/expected.h`, `src/util/fs.h`, `src/util/version.{h,cpp}`, and `src/cli/main.cpp`, plus the test suite scaffolding in `tests/unit/` and `tests/integration/`. These establish the project's concrete conventions for: header style, `expected<T,E>` usage, the ENG-16 library/CLI boundary, Catch2 test structure, and CLI11 wiring — every new file in this phase must follow these conventions even though none is a role-for-role analog.

Because there is no existing registry generator, `variant`-based value type, comparator, or report renderer anywhere in the repo, RESEARCH.md's Architecture Patterns section (Patterns 1-5, Anti-Patterns, Don't Hand-Roll) is the primary source of concrete shape for those files, not codebase analogs. This PATTERNS.md cites both: repo-verified conventions (imports, error handling, test structure) from real files, and RESEARCH.md's verified library-API shapes for the genuinely new patterns.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/core/error.h` | model/error-type | request-response (all layers) | `src/util/expected.h` | role-match (only existing error-vehicle file) |
| `src/core/value.h` | model | transform | none in-repo | no-analog (RESEARCH Pattern: D-06 variant) |
| `src/core/rational.h` | model | transform | none in-repo | no-analog (RESEARCH D-07) |
| `src/core/measurement.h`, `finding.h`, `fingerprint.h` | model | transform | none in-repo | no-analog |
| `src/core/checks.def`, `check_id.h` (generated), `check_explain.cpp` (generated) | config/generated model | batch (codegen) | `src/util/version.{h,cpp}` (only other file with generated/compiled-in string data — `MEDIADIFF_VERSION` macro, `AV_STRINGIFY`) | role-match |
| `tools/gen_registry.py` | utility (build-time codegen) | batch | none in-repo (first Python file) | no-analog (RESEARCH Pattern 1) |
| `src/core/policy.{h,cpp}`, `profiles.h` | service | CRUD-like merge | none in-repo | no-analog |
| `src/core/tolerance.{h,cpp}` | utility/parser | transform | none in-repo | no-analog |
| `src/core/glob.{h,cpp}` | utility | transform | none in-repo | no-analog (explicitly "no regex" — see Don't Hand-Roll) |
| `src/core/serializer.{h,cpp}` | service | transform (canonical write) | `src/util/fs.h`'s `fopen_utf8` (the file-I/O boundary all serialized output must go through) | role-match |
| `src/config/toml_load.{h,cpp}` | service | CRUD (load+merge) | none in-repo | no-analog |
| `src/compare/engine.{h,cpp}`, `semantics.h`, `exact.cpp`/`tol.cpp`/`set.cpp`/`presence.cpp`/`hash.cpp`/`dist.cpp`/`span.cpp` | service | transform (pure fn) | none in-repo | no-analog |
| `src/report/json.{h,cpp}` | service/renderer | transform | none in-repo | no-analog |
| `src/report/markdown.{h,cpp}` | service/renderer | transform | none in-repo | no-analog |
| `src/report/junit.{h,cpp}` | service/renderer | transform | none in-repo | no-analog |
| `src/cli/commands/*.cpp` (compare, snapshot, dir, inspect, list-checks, explain) | controller | request-response | `src/cli/main.cpp` | exact (same file, being extended) |
| `src/cli/tty_render.{h,cpp}` | component/renderer | transform | `src/util/version.cpp` (fmt-based text composition) | role-match |
| `src/cli/exit_code.h` | utility/mapping | transform | `src/cli/main.cpp` (existing ad hoc exit-code returns, e.g. line 88 `return 64;`) | role-match |
| `tests/unit/test_*.cpp` (new: value, rational, glob, tolerance, policy_merge, semantics_*, serializer, snapshot_roundtrip) | test | — | `tests/unit/test_expected.cpp` | exact |
| `tests/integration/test_*.cpp` (new: cli subcommand tests) | test | — | `tests/integration/test_version_output.cpp` | exact |
| `tests/golden/*` | test/fixture | batch | none in-repo (new tree) | no-analog (RESEARCH: Jest/insta/ApprovalTests precedent, D-12) |
| `tests/fixtures/snapshots/*.snap.json` | test/fixture | file-I/O | `tests/fixtures/GENERATOR_MANIFEST.json` (existing JSON fixture in the same tree) | role-match |
| `CMakeLists.txt` (modified) | config | batch | itself (Phase 1 version) | exact — extend, don't replace |

## Pattern Assignments

### `src/core/error.h` (model/error-type, request-response)

**Analog:** `src/util/expected.h` (full file, 27 lines — reproduced above in this session's Read)

**Pattern to copy:**
- Single-purpose header, `#pragma once`, one `namespace mediadiff { ... }` block.
- Heavy doc-comment above the type explaining *why* the indirection exists (not just what it does) — matches this repo's comment density; new `Error{kind, ...}` type should carry the same "why" comment tying it to ENG-15/D-... decisions.
- `mediadiff::expected<T, E>` is the ONLY error-propagation vehicle in `core/` — every new function in `core/`, `config/`, `compare/` that can fail returns `expected<T, Error>`, never throws, per `src/util/expected.h`'s own header comment and PROJECT.md's "no exceptions across the lib boundary."

**Concrete import convention** (from `expected.h` lines 16-26):
```cpp
#include <tl/expected.hpp>

namespace mediadiff {

template <typename T, typename E>
using expected = tl::expected<T, E>;

template <typename E>
using unexpected = tl::unexpected<E>;

}  // namespace mediadiff
```
`Error.h` should define `enum class Error::Kind` and a plain aggregate/struct, consumed everywhere as `mediadiff::expected<T, mediadiff::Error>`.

---

### `src/cli/exit_code.h` (utility/mapping, transform)

**Analog:** `src/cli/main.cpp` lines 84-89 (the one existing ad hoc exit-code site)

```cpp
std::fputs("mediadiff: argument ", stderr);
std::fputs(std::to_string(i).c_str(), stderr);
std::fputs(" is not valid UTF-16 and cannot be converted to UTF-8.\n", stderr);
return 64;  // usage — the argument is malformed, no input was opened
```

**Pattern to copy:** every exit-code return site names *why* that code, inline, as a trailing comment (`// usage — ...`). Per RESEARCH.md Pattern 3 / Pitfall 1, `exit_code.h` must be the **single** place `CLI::ParseError` → `64` translation happens; do NOT trust `app.exit(e)` (CLI11's own 100-127 range). Replace the current bare `CLI11_PARSE(app, argc, argv); return 0;` in `main.cpp` (lines 36-37) with an explicit try/catch calling into `exit_code.h`'s mapping function.

---

### `src/cli/commands/compare.cpp` etc. (controller, request-response)

**Analog:** `src/cli/main.cpp` (full file, 115 lines)

**Imports pattern** (lines 1-19):
```cpp
#include <CLI/CLI.hpp>

#include "util/fs.h"
#include "util/version.h"

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#endif
```
Convention: system/library headers first, blank line, project headers as quoted `"relative/from/src.h"` (no `../`), blank line, platform-conditional headers last. New `src/cli/commands/*.cpp` files should `#include "core/..."`, `#include "compare/..."`, `#include "report/..."` the same quoted way.

**Subcommand wiring pattern** (line 30, and RESEARCH Pattern 2):
```cpp
app.require_subcommand(0, 1);
app.set_version_flag("--version", []() { return mediadiff::compose_version_string(); });
```
This already anticipates Phase 2's `add_subcommand("compare", ...)` etc. — extend `run()` in `main.cpp`, not replace it. Each subcommand's implementation should live in `src/cli/commands/<name>.cpp` and export a `register_<name>_command(CLI::App&)` or equivalent free function that `main.cpp`'s `run()` calls, keeping `main.cpp` itself thin (mirrors how `run()` currently just wires `--version` to `compose_version_string()` rather than inlining the composition).

**ENG-16 boundary pattern** (lines 83-89): CLI-layer code is the only place permitted to write to stderr/stdout or return process exit codes; `libmediadiff` (everything under `core/`, `config/`, `compare/`, `report/`) must never do so — enforced today by `scripts/lint_eng16.sh`, unchanged in Phase 2 per RESEARCH.md's Project Constraints section.

---

### `src/util/version.cpp`-style composition → analog for `src/cli/tty_render.cpp` and `src/report/*.cpp`

**Analog:** `src/util/version.cpp` (full file, 53 lines)

**Core pattern** (lines 36-50): a pure function that takes no I/O, returns a fully-composed `std::string` via `fmt::format`, callable independent of any stream — this is exactly the shape `report/json.cpp`, `report/markdown.cpp`, `report/junit.cpp` and `cli/tty_render.cpp` need (per RESEARCH's "four report formats... pure functions of `Finding[]`" architecture note). `compose_version_string()` is unit-testable with no CLI/stdout involved (comment on line 11 of `version.h`) — new renderers should be equally testable in isolation, matching `tests/unit/` (not `tests/integration/`) as their primary coverage.

**fmt usage convention:**
```cpp
#include <fmt/format.h>
...
return fmt::format("mediadiff {}\n" ..., MEDIADIFF_VERSION, ...);
```
For `tty_render.cpp` specifically, RESEARCH.md Pattern 4 adds `#include <fmt/color.h>` as a separate include (not bundled into `fmt/format.h`) for the `fmt::emphasis`/`fg()` styling CLI-08 needs.

---

### `src/core/serializer.{h,cpp}` (service, transform)

**Analog:** `src/util/fs.h`'s `fopen_utf8` (lines 93-121, 131-136)

**Pattern to copy:** fail-loud, no silent substitution — every failure path returns a sentinel (`nullptr`/empty) rather than guessing; heavy inline comment explaining *why* a naive alternative (e.g., `_wfopen` instead of `_wfopen_s`, or here: `snprintf("%g",...)` instead of `std::to_chars`) was rejected. The serializer's float-formatting path must use `std::to_chars` per RESEARCH's Anti-Patterns/Don't-Hand-Roll sections (rejecting `snprintf`/`ostringstream`), matching this file's established "always the deterministic, fail-loud primitive over the convenient one" convention. All snapshot/report file writes should route through `fopen_utf8` (per `fs.h`'s own header comment: "the single site where encoding conversion happens for mediadiff's OWN file I/O ... report files, mediadiff.toml, snapshot/compare file access").

---

### `tests/unit/test_*.cpp` (test)

**Analog:** `tests/unit/test_expected.cpp` (full file, 51 lines) + `tests/unit/CMakeLists.txt`

**Structure to copy:**
```cpp
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "util/expected.h"

namespace {
// test-local helpers
}  // namespace

TEST_CASE("<component> - <behavior described in plain English>", "[tag]") {
  ...
  REQUIRE(...);
}
```
- One `[tag]` category per component (`[expected]`, and new ones: `[value]`, `[glob]`, `[tolerance]`, `[semantics]`, `[serializer]`...).
- Every new `.cpp` test file must be added to `tests/unit/CMakeLists.txt`'s `add_executable(mediadiff_unit_tests ...)` list (currently 4 files, lines 3-11) — the `catch_discover_tests(... TEST_PREFIX "unit.")` call at the bottom auto-registers all `TEST_CASE`s, but only from files actually compiled into the target.
- Per D-14/D-15/D-16 (fail-first discipline): each new comparison semantic test file needs both a passing-case `TEST_CASE` and a must-fail-case `TEST_CASE`, and the coverage must include every status the semantic can emit (`pass|info|warn|fail|skipped|error`), not just pass/fail — this is a **new** convention this phase introduces, with no existing analog to copy verbatim, but it must be applied uniformly across every new `compare/*.cpp` test file.

---

### `tests/integration/test_*.cpp` (test)

**Analog:** `tests/integration/test_version_output.cpp` (full file, 129 lines) + `tests/integration/cli_harness.h`

**Pattern to copy:**
```cpp
#include <catch2/catch_test_macros.hpp>
#include "cli_harness.h"

using mediadiff::test::CliResult;
using mediadiff::test::EnvVars;
using mediadiff::test::run_cli;

TEST_CASE("<name> - <REQ-ID>: <behavior>", "[integration]") {
  CliResult result = run_cli({"--version"});
  REQUIRE(result.exit_code == 0);
  ...
}
```
- Test names embed the requirement ID (`CLI-05`, `BUILD-09`) directly in the `TEST_CASE` description string — new tests for CLI-01…CLI-10, SNAP-01…07, DIR-01…05 should follow the same `"<slug> - <REQ-ID>: <description>"` naming so `ctest -R` output is self-documenting.
- Exit codes are asserted as **exact literal values** (`result.exit_code == 0`), never loose `!= 0` checks — directly matches RESEARCH.md Pitfall 1's warning that a loose assertion would hide the CLI11-exit-code-range bug. New tests asserting CLI-06/CLI-10's 64/65/66/70 contract must use exact equality.
- **Known-deferred fix to apply here first:** `cli_harness.h`'s `EINTR`-treated-as-EOF bug (flagged in 02-CONTEXT.md's Specifics) should be closed before this phase's integration suite leans on it heavily — read `tests/integration/cli_harness.h` before writing new integration tests that capture large stdout/stderr blobs (JSON/report output will be far larger than `--version`'s output).

---

### `CMakeLists.txt` (modified, config)

**Analog:** itself (current Phase-1 state, lines 1-100 read this session)

**Pattern to copy:** the existing `add_library(libmediadiff STATIC src/util/version.cpp)` target (line 41) and `mediadiff_apply_warnings()` function (lines 73-84) are additive — every new `core/`, `config/`, `compare/`, `report/` `.cpp` file gets appended to `libmediadiff`'s source list, and `mediadiff_apply_warnings(libmediadiff)` (already called at line 86) automatically covers them; no new warnings-as-errors wiring needed per new file. RESEARCH Pattern 1's `add_custom_command`/`generate_registry` target attaches via `add_dependencies(libmediadiff generate_registry)`, following the same "extend the one target" convention rather than introducing a parallel build target.

---

## Shared Patterns

### Error handling
**Source:** `src/util/expected.h`
**Apply to:** every function in `core/`, `config/`, `compare/` that can fail (registry lookup, policy merge, tolerance grammar parse, TOML load, semantics dispatch, snapshot read/write). Return `mediadiff::expected<T, mediadiff::Error>`; never throw across the lib boundary; never call `exit()` outside `cli/`.

### File I/O
**Source:** `src/util/fs.h` (`fopen_utf8`)
**Apply to:** all new file-opening sites — `config/toml_load.cpp` (mediadiff.toml), `core/serializer.cpp` (snapshot read/write), `report/*.cpp` (report file output). Never call raw `fopen`/`std::ifstream` directly; never name `wchar_t`/`CP_UTF8` outside this file and `cli/main.cpp`.

### ENG-16 library/CLI boundary
**Source:** `src/cli/main.cpp` (only file with `stdout`/`stderr`/`exit()`), enforced by `scripts/lint_eng16.sh`
**Apply to:** every file under `core/`, `config/`, `compare/`, `report/`, `util/` — no `fprintf(stderr,...)`, no `std::cout`, no `exit()`/`std::terminate`. All rendering and process control happens in `cli/`.

### Header/doc-comment style
**Source:** `src/util/expected.h`, `src/util/fs.h` (every function has a multi-paragraph "why," not just a one-line "what")
**Apply to:** all new public headers, especially ones implementing locked decisions (D-06 variant, D-07 rational, D-08 serializer) — the comment should reference the decision ID it implements, matching the existing style of citing "D-02"/"D-04"/"D-07" inline.

### Test naming and registration
**Source:** `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt`
**Apply to:** every new test file — must be explicitly added to the relevant `add_executable(...)` source list; `TEST_PREFIX "unit."` / `"integration."` convention must be preserved so `ctest -R unit`/`-R integration` selects a non-zero count (Phase 1's lesson, D-14 codifies it).

## No Analog Found

Files with no close match in the codebase — planner should rely on RESEARCH.md's Architecture Patterns / Code Examples sections instead:

| File | Role | Data Flow | Reason |
|---|---|---|---|
| `tools/gen_registry.py` | utility (codegen) | batch | First Python file in the repo; use RESEARCH.md Pattern 1's CMake wiring shape and D-05's constraints |
| `src/core/value.h` (`std::variant<9>`) | model | transform | No existing variant-based type in repo; follow D-06 and RESEARCH's Don't-Hand-Roll guidance |
| `src/core/rational.h` | model | transform | No existing libav-free rational POD; follow D-07 |
| `src/compare/*.cpp` (seven semantics) | service | transform | No existing comparator/semantics dispatch code; follow RESEARCH Architecture Diagram + doc 01 §3 |
| `src/core/policy.{h,cpp}`, `profiles.h` | service | CRUD-merge | No existing config-precedence merger; follow ENG-06/ENG-11 and RESEARCH's policy-merge architecture note |
| `src/core/glob.{h,cpp}` | utility | transform | No existing matcher of any kind; explicitly must avoid `std::regex` per ENG-02 |
| `src/report/{json,markdown,junit}.cpp` | service/renderer | transform | No existing report renderer; follow RESEARCH's "pure function of Finding[]" architecture note and REPORT-01…07 |
| `tests/golden/*` | test/fixture | batch | New fixture tree; follow D-12 (`UPDATE_GOLDENS` local-only, CI read-only) and the Jest/insta/ApprovalTests precedent cited in RESEARCH |
| `docs/checks/<id>.md`, `docs/schema/report-1.0.json` | doc/schema | file-I/O | New doc trees; DOC-01/DOC-02 and REPORT-01 define their required shape, no existing precedent |

## Metadata

**Analog search scope:** `src/` (all subdirectories), `tests/unit/`, `tests/integration/`, `CMakeLists.txt`, `tests/CMakeLists.txt`
**Files scanned:** 4 non-test source files (`expected.h`, `fs.h`, `version.h`, `version.cpp`), `cli/main.cpp`, 5 test files, 2 test `CMakeLists.txt`, root `CMakeLists.txt`
**Pattern extraction date:** 2026-08-15
