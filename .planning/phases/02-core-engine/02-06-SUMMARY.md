---
phase: 02-core-engine
plan: 06
subsystem: config
tags: [toml, tomlplusplus, policy-resolution, cli11, provenance, catch2]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 05)
    provides: "Policy{profile, per_check, transform_expectation}, ResolvedCheck{id, severity, tolerance, chain}, PolicyProvenance{layer, detail, value}, resolve_policy's builtin+profile two-layer pass, apply_severity_override -- the exact structure and mechanism this plan's config/CLI layers extend"
  - phase: 02-core-engine (plan 02)
    provides: "core/glob.h's glob_matches/glob_select/validate_glob (segment-wise check-id glob resolution for [severity]/[tolerance]/--set/--tol globs) and CheckRegistry::resolve_alias"
provides:
  - "src/config/toml_load.{h,cpp}: discover_and_load -- the --config/./mediadiff.toml/none discovery order, tomlplusplus string-entry-point parsing through fopen_utf8, and full shape validation (unknown sections, out-of-set severities, unit-mismatched tolerances checked against the production registry, bad profile names, malformed globs, wrong TOML types) mapped to ErrorKind::usage naming the offending key and source position"
  - "core/policy.h's resolve_policy extended to four layers: config's [severity]/[tolerance] then [override.*] blocks in file order (layer 3), CLI --set/--tol in argv order (layer 4) -- both defaulted so every pre-existing two-argument call site (compare/engine.cpp, every 02-04/02-05-era comparator test) kept compiling unchanged. apply_tolerance_override is the new tolerance-write mirror of apply_severity_override (no provenance chain entry, matching the builtin/profile layer's own established no-chain-for-tolerance behavior)"
  - "core/registry.h: severity_from_string/severity_to_string -- the shared severity-word vocabulary toml_load.cpp, cli/options.cpp, policy.cpp and report/json.cpp all now consume instead of each keeping a private copy"
  - "src/cli/options.{h,cpp}: add_policy_flags (the shared --profile/--config/--set/--tol registration compare and list-checks both use), parse_cli_overrides (argv-ordered CliOverride construction with syntax-only validation), resolve_profile_selection (--profile > config profile= > kDefaultProfile)"
  - "src/cli/provenance_render.{h,cpp}: render_provenance_chain -- the one severity-chain text renderer list-checks --effective -v (this plan) and plan 02-10's inspect -v will both call, one line per PolicyProvenance entry in resolution order, layer padded to 7 / value padded to 8 / detail in parentheses, no styled or glyph text"
  - "src/cli/commands/list_checks.{h,cpp}: register_list_checks_command -- list-checks (registry dump) and list-checks --effective [-v] (the merged policy dump, via the SAME resolve sequence compare.cpp's callback runs)"
  - "src/cli/commands/compare.cpp: now loads mediadiff.toml once per run, parses CLI overrides, resolves --profile/config profile=/default, and copies a declared [transform] expectation into Policy::transform_expectation -- replacing the 02-01 tracer's hardcoded sw-encoder Policy"
affects: ["02-07 (snapshot envelope work can now assume compare's Policy is the fully-resolved four-layer one, not the tracer's bare Policy{profile})", "02-08 (compare --json -v's severity_chain JSON array renders the SAME PolicyProvenance chain this plan's text renderer proved, just as structured JSON instead of text)", "02-09/02-10 (the global --no-color/--ascii flags and add_common_options helper are NOT wired into list-checks/compare by this plan -- render_provenance_chain already emits zero styled/glyph text, so nothing needs to change there once those flags land)", "02-11 (dir mode is what actually applies an [override.*] block's PATH GLOB against a per-file relative path; this plan's resolve_policy applies every declared override block unconditionally, since it has no file-path parameter to filter by -- 02-11 either pre-filters config->overrides before calling resolve_policy, or resolve_policy grows a path parameter then)"]

# Actuals (#2632)
actuals:
  tokens: 24572
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: ["tomlplusplus (vcpkg tomlplusplus:x64-linux@3.4.0, already pinned in vcpkg.json; find_package(tomlplusplus CONFIG REQUIRED) and target_link_libraries(... tomlplusplus::tomlplusplus) added to CMakeLists.txt for the first time this plan)"]
  patterns:
    - "toml++'s toml::table is backed by a std::map<toml::key, ...> ordered by KEY TEXT, not declaration order -- GlobRule::file_order and OverrideBlock::file_order are derived from each toml::key's own source_region (line, column) via entries_in_file_order(), sorted explicitly, rather than trusted from table iteration order, which would silently scramble doc 01 section 6's file-order resolution rule"
    - "Tolerance overrides are last-writer-wins-and-forget: no layer's tolerance write, including resolve_policy's own original builtin/profile-layer write from plan 02-05, has ever appended a PolicyProvenance chain entry. Only SEVERITY writes build the chain. This matches every worked example and JSON surface (severity_chain) named across docs 01 and this plan -- 'the resolved chain' means the severity chain throughout"
    - "A --tol argument's own unit grammar cannot be validated at argv-capture time (src/cli/options.cpp) because doing so requires knowing which check(s) the glob resolves to and each one's declared Unit -- that resolution only happens inside resolve_policy, which is why CLI-10's diagnostic is reachable only as an ErrorKind::usage returned from resolve_policy itself, not from parse_cli_overrides"
    - "compare.cpp and list_checks.cpp intentionally duplicate the same five-step sequence (discover_and_load -> parse_cli_overrides -> resolve_profile_selection -> resolve_policy -> copy transform expectation) rather than sharing a helper in cli/options.h -- kept inline in each command's own callback so list-checks --effective's dump is visibly running the identical code compare.cpp's callback runs (T-2-23), not a call into a third function neither owns"

key-files:
  created:
    - src/config/toml_load.h
    - src/config/toml_load.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/provenance_render.h
    - src/cli/provenance_render.cpp
    - src/cli/commands/list_checks.h
    - src/cli/commands/list_checks.cpp
    - tests/unit/test_toml_load.cpp
    - tests/unit/test_policy_merge.cpp
    - tests/integration/test_list_checks.cpp
    - tests/fixtures/config/ (9 fixtures)
    - tests/golden/list_checks_effective.txt
  modified:
    - src/core/policy.h
    - src/core/policy.cpp
    - src/core/registry.h
    - src/cli/commands/compare.cpp
    - src/cli/main.cpp
    - src/report/json.cpp
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - .gitignore

key-decisions:
  - "resolve_policy's config/cli_overrides parameters are BOTH defaulted (std::nullopt, {}) rather than requiring every caller to pass four arguments -- this is what let compare/engine.cpp's own internal resolve_policy(registry, policy.profile) call and every 02-04/02-05-era comparator unit test (test_profiles.cpp, test_volatile.cpp) keep compiling and behaving identically, with zero changes to files outside this plan's own files_modified list"
  - "PolicyProvenance's chain remains scoped to SEVERITY resolution only -- tolerance writes (config [tolerance], override [tolerance], --tol) replace ResolvedCheck::tolerance directly via the new apply_tolerance_override, with no chain entry -- because the ORIGINAL builtin/profile-layer tolerance write (plan 02-05) never had one either, and every worked example/JSON-surface reference in doc 01 and this plan's own text says 'the resolved severity chain', never 'the resolved tolerance chain'"
  - "[override.*] blocks are applied by resolve_policy UNCONDITIONALLY (every declared block, regardless of any file path), because resolve_policy's signature has no path parameter to filter by -- this plan's own must_haves explicitly require testing that an override block takes effect over [severity] and is itself overridden by a CLI override, so 'apply only in dir mode' (Task 1's own framing) is dir mode's eventual FILTERING responsibility, not a reason for THIS plan's resolve_policy to skip them entirely"
  - "tests/fixtures/config/complete_valid.toml's override block targets meta.missing_candidate rather than meta.tool_version (changed after Task 1, during Task 3): Task 3's own worked scenario reuses this fixture and requires meta.tool_version's resolved chain to be EXACTLY three entries (builtin, config, cli) -- an override block ALSO matching meta.tool_version would have added a fourth entry and broken that specific assertion. tests/unit/test_toml_load.cpp's Task-1 assertions were updated to match (Rule 1 fix)"
  - "severity_from_string/severity_to_string were promoted from three separate private copies (toml_load.cpp's own, report/json.cpp's own, plus policy.cpp's write-only severity_name) into one shared pair in core/registry.h, discovered as a genuine ambiguous-overload build error the moment policy.cpp's own copy and registry.h's new one both matched an unqualified call inside namespace mediadiff (Rule 1 fix)"

patterns-established:
  - "A config/CLI-driven glob rule's provenance detail text is built from the ACTUAL file/argv position at construction time (1-based in the rendered text, 0-based in the underlying file_order/argv_index counters) -- config_glob_detail/config_override_glob_detail/cli_detail in core/policy.cpp are the one place this formatting happens, so a later JSON severity_chain renderer (plan 02-08) has an unambiguous plain-text detail string to reuse or reformat"

requirements-completed: [ENG-06, ENG-11, ENG-12, CLI-03]

coverage:
  - id: D1
    description: "mediadiff.toml is discovered (--config, else ./mediadiff.toml, else none), parsed once through tomlplusplus via fopen_utf8, and every malformed shape -- unknown section, bad severity word, unit-mismatched tolerance, bad profile name, malformed glob, wrong TOML type, or a parse failure itself -- becomes ErrorKind::usage naming the offending key and position rather than a crash or silent default fallback"
    requirement: "ENG-11"
    verification:
      - kind: unit
        ref: "tests/unit/test_toml_load.cpp (11/11 pass, tag [config]): complete-valid population, profile-only, empty file, unknown section, bad severity, unit-mismatched tolerance, malformed TOML, ascending file_order proof, UTF-8 override path survival, explicit-path-missing input_open, no-config-no-error"
        status: pass
      - kind: other
        ref: "grep -rn 'std::ifstream|[^_]fopen(' src/config -> no matches; bash scripts/lint_eng16.sh -> clean"
        status: pass
    human_judgment: false
  - id: D2
    description: "resolve_policy resolves all four layers (builtin, profile, config [severity]/[tolerance]/[override.*] in file order, CLI --set/--tol in argv order), last writer wins, with the full severity provenance chain surviving every layer and every pre-existing two-argument call site left unchanged"
    requirement: "ENG-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_policy_merge.cpp (10/10 pass, tag [policy]): single-builtin-entry baseline, argv-order last-writer-wins both directions, 3!-permutation invariance for non-overlapping entries, overlap-only-changes-overlap-index, --set/--tol dimension independence under interleaving, override-over-severity, cli-over-override, empty-CLI equivalence, --tol unit-mismatch usage error"
        status: pass
    human_judgment: false
  - id: D3
    description: "list-checks --effective dumps the SAME policy compare.cpp's callback resolves, in registry declaration order, byte-identically across runs, with the -v resolution chain visible per check (builtin/config/cli in resolution order, every field non-empty)"
    requirement: "ENG-12"
    verification:
      - kind: integration
        ref: "tests/integration/test_list_checks.cpp (6/6 pass, tag [integration]): row-count/id-order against kCheckIdStrings, golden byte-stability (tests/golden/list_checks_effective.txt), the config+CLI worked scenario asserting an exact builtin/config/cli chain sequence, -v strictly-more-lines mode distinction"
        status: pass
    human_judgment: false
  - id: D4
    description: "--set/--tol/--profile/--config are reachable from the compare command line, with a malformed --set argument, an out-of-set severity word, or a --tol value whose unit contradicts its target check each producing an ErrorKind::usage diagnostic naming the offending argument/expected unit"
    requirement: "CLI-03"
    verification:
      - kind: other
        ref: "manual CLI smoke test: --set meta.tool_version=fail (exit 1, gates), --profile strict-bitexact (exit 1, gates), --tol meta.tool_version=5ms (exit 64, names 'none'), --set meta.tool_version=critical (exit 64, names the four valid words), implicit ./mediadiff.toml discovery honored (exit 0, severity ignored via config)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_policy_merge.cpp's --tol unit-mismatch case (ErrorKind::usage) plus tests/unit/test_toml_load.cpp's bad-severity/bad-tolerance cases cover the same validation paths through toml_load.cpp; parse_cli_overrides' own argv-side validation (no '=', empty glob/value, bad severity word, malformed glob) has no dedicated automated test in this plan -- exercised only by the manual CLI smoke test above"
        status: pass
    human_judgment: false

duration: ~70min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 6: Configuration Precedence and the Effective-Policy Dump Summary

**`mediadiff.toml` is discovered, parsed and shape-validated exactly once per run; `resolve_policy` now merges all four layers of doc 01's precedence chain (builtin, profile, config, CLI `--set`/`--tol`) with a full severity provenance chain; and `list-checks --effective -v` renders that exact merged policy, in the same code path `compare` itself runs.**

## Performance

- **Duration:** ~70 min
- **Started:** 2026-08-15 (session start, continuing directly from 02-05)
- **Completed:** 2026-08-15
- **Tasks:** 3
- **Files modified:** 31 (13 created, 18 modified)

## Accomplishments

- `src/config/toml_load.{h,cpp}`: the single place `mediadiff.toml` is read -- `discover_and_load` implements `--config`/`./mediadiff.toml`/none discovery, parses through tomlplusplus's string entry point via `fopen_utf8`, and shape-validates every section (unknown keys, out-of-set severities, unit-mismatched tolerances checked against the real production registry, bad profile names, malformed check-id globs, wrong TOML types) into `ErrorKind::usage` naming the offending key and source position
- `core/policy.cpp`'s `resolve_policy` grew from two layers to four: config's `[severity]`/`[tolerance]` then every `[override.*]` block in file order (layer 3), then CLI `--set`/`--tol` in argv order (layer 4) -- both new parameters defaulted so `compare/engine.cpp` and every prior comparator/profile unit test kept compiling unchanged
- `src/cli/options.{h,cpp}` and `src/cli/commands/compare.cpp`: `compare` now actually resolves `--profile`, `--config`, `--set`, `--tol` end to end -- verified live against the built binary (severity escalation, profile switching, unit-mismatch and malformed-argument diagnostics, implicit `./mediadiff.toml` discovery)
- `src/cli/provenance_render.{h,cpp}` and `src/cli/commands/list_checks.{h,cpp}`: `list-checks`/`list-checks --effective [-v]`, the latter running the identical resolve sequence `compare`'s callback runs and rendering each check's severity provenance chain under `-v` in the plan's exact worked format

## Task Commits

Each task was committed atomically:

1. **Task 1: mediadiff.toml discovery, parse and section extraction** - `a1d699f` (feat)
2. **Task 2: the four-layer precedence merge and argv-order CLI overrides** - `dc2000f` (feat)
3. **Task 3: list-checks --effective and the -v resolution chain** - `84e1220` (feat)

## Files Created/Modified

- `src/config/toml_load.{h,cpp}` - mediadiff.toml discovery, parse, shape validation (ENG-11)
- `src/core/policy.{h,cpp}` - four-layer resolve_policy, apply_tolerance_override, CliOverride
- `src/core/registry.h` - severity_from_string/severity_to_string
- `src/cli/options.{h,cpp}` - add_policy_flags, parse_cli_overrides, resolve_profile_selection
- `src/cli/provenance_render.{h,cpp}` - render_provenance_chain
- `src/cli/commands/list_checks.{h,cpp}` - the `list-checks` subcommand
- `src/cli/commands/compare.cpp`, `src/cli/main.cpp` - wired to the new config/policy/CLI machinery
- `src/report/json.cpp` - de-duplicated its private severity_to_string onto core/registry.h's shared one
- `tests/unit/test_toml_load.cpp`, `tests/unit/test_policy_merge.cpp`, `tests/integration/test_list_checks.cpp` - new suites, tags `[config]`/`[policy]`/`[integration]`
- `tests/fixtures/config/` - 9 hand-authored fixtures; `tests/golden/list_checks_effective.txt` - the new golden
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt`, `.gitignore` - tomlplusplus wiring, new sources, `tests/fixtures/config/` carve-out

## Decisions Made

See frontmatter `key-decisions` for full rationale on: defaulted `resolve_policy` parameters preserving every pre-existing call site; the provenance chain staying severity-only (tolerance is last-write-and-forget); `[override.*]` blocks applying unconditionally in this plan (dir-mode path filtering is plan 02-11's job); the `complete_valid.toml` fixture retarget to keep Task 3's exact 3-entry chain assertion true; and consolidating three separate `severity_from_string`/`severity_to_string` copies into one shared pair in `core/registry.h`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Ambiguous-overload build error from a duplicate severity_to_string**
- **Found during:** Task 2, first build after adding `core/registry.h::severity_to_string`
- **Issue:** `src/report/json.cpp` already had its own private `severity_to_string` in an anonymous namespace; once `core/registry.h` gained a same-named, same-signature free function in `namespace mediadiff`, every unqualified call inside `json.cpp` (itself in `namespace mediadiff`) became ambiguous, and the now-shadowed private copy also became unused (`-Werror=unused-function`).
- **Fix:** Removed `json.cpp`'s private `severity_to_string`; it now calls the shared one from `core/registry.h` (already transitively included).
- **Files modified:** `src/report/json.cpp`.
- **Commit:** `dc2000f` (Task 2's own commit, since this was discovered and fixed before that commit landed).

**2. [Rule 1 - Bug] `complete_valid.toml`'s override block collided with Task 3's own worked scenario**
- **Found during:** Task 3, while manually verifying the `-v` chain worked example against the fixture built in Task 1
- **Issue:** The Task 1 fixture's `[override."fixtures/**"]` block targeted `meta.tool_version` -- the SAME check Task 3's own must-have scenario targets with `--set meta.tool_version=fail` -- producing FOUR chain entries (builtin, config, config, cli) instead of the exact THREE (builtin, config, cli) Task 3's acceptance criteria require.
- **Fix:** Retargeted the fixture's override block to `meta.missing_candidate` instead, and updated `tests/unit/test_toml_load.cpp`'s Task-1 assertions (glob/value) to match. Task 1's own coverage (an override block parses with its own nested `[severity]`/`[tolerance]`) is unaffected -- only which check it names changed.
- **Files modified:** `tests/fixtures/config/complete_valid.toml`, `tests/unit/test_toml_load.cpp`.
- **Commit:** `84e1220` (Task 3's own commit).

---

**Total deviations:** 2 auto-fixed (both Rule 1)
**Impact on plan:** Both were necessary for correctness (the first, a genuine build break; the second, a genuine test-scenario collision) and neither changes scope, architecture, or any frozen contract from prior waves.

## Issues Encountered

- **toml++'s `toml::table` iterates in KEY-TEXT order (a `std::map`), not declaration order.** This was not assumed going in -- confirmed by reading `toml++/impl/table.hpp` directly before writing any parsing code. `GlobRule::file_order`/`OverrideBlock::file_order` are derived from each `toml::key`'s own `source_region` (line, column) via an explicit sort, never from raw table iteration, which is what makes doc 01 section 6's file-order resolution rule actually hold.
- **`[override.*]` blocks' "dir mode only" framing (Task 1's own comment) doesn't map to a hard SKIP inside this plan's `resolve_policy`**, since the function has no file-path parameter to filter overrides by, and this plan's own must-haves require testing that an override block DOES take effect. Resolved by applying every declared override block unconditionally in this plan, with the path-based filtering left as an explicit, documented open question for plan 02-11 (see frontmatter `affects`).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 02-07 (snapshot envelope) can assume `compare`'s `Policy` is now the fully-resolved four-layer one (profile/config/CLI merged), not the 02-01 tracer's hardcoded `Policy{sw_encoder}`.
- Plan 02-08's `compare --json -v` `severity_chain` JSON array renders the exact same `PolicyProvenance` data this plan's `render_provenance_chain` already proves end to end as text -- no new data-structure work needed there, only a JSON serialization of the same three fields.
- Plan 02-09/02-10's `--no-color`/`--ascii`/`add_common_options` work is NOT wired into `list-checks`/`compare` by this plan (out of scope, and those flags don't exist yet anywhere in the codebase) -- `render_provenance_chain` already emits zero styled or glyph-bearing text, so the "identical under `--no-color`/`--ascii`" property already holds structurally and needs no rework once those flags land.
- Plan 02-11 (`dir` mode) needs to decide how `[override.*]` blocks' path-glob filtering actually happens relative to `resolve_policy` -- either pre-filtering `ConfigFile::overrides` per file before calling `resolve_policy`, or adding a path parameter to `resolve_policy` itself. Flagged explicitly in this SUMMARY's frontmatter `affects` and in `core/policy.h`'s module comment; not a blocker for 02-07/02-08 since neither touches `dir` mode.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 13 created files verified present on disk (`src/config/toml_load.h`, `src/config/toml_load.cpp`, `src/cli/options.h`, `src/cli/options.cpp`, `src/cli/provenance_render.h`, `src/cli/provenance_render.cpp`, `src/cli/commands/list_checks.h`, `src/cli/commands/list_checks.cpp`, `tests/unit/test_toml_load.cpp`, `tests/unit/test_policy_merge.cpp`, `tests/integration/test_list_checks.cpp`, `tests/golden/list_checks_effective.txt`, this SUMMARY); all 3 task commit hashes (`a1d699f`, `dc2000f`, `84e1220`) verified present in `git log --oneline --all`.
