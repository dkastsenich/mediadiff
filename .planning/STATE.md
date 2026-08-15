---
gsd_state_version: 1.0
milestone: v0.6.1
milestone_name: milestone
current_phase: 02
current_phase_name: core-engine
status: executing
stopped_at: Completed 02-09-PLAN.md
last_updated: "2026-08-15T19:04:06.685Z"
last_activity: 2026-08-15
last_activity_desc: Phase 02 execution started
progress:
  total_phases: 2
  completed_phases: 1
  total_plans: 16
  completed_plans: 14
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-12)

**Core value:** A media-aware diff CI can trust — a no-change re-run under the right profile is clean out of the box, every real regression is caught, explained, and actionable. False positives are P0.
**Current focus:** Phase 02 — core-engine

## Current Position

Phase: 02 (core-engine) — EXECUTING
Plan: 10 of 11
Status: Ready to execute
Last activity: 2026-08-15 — Phase 02 execution started

Progress: [█████████░] 88%

## Performance Metrics

**Velocity:**

- Total plans completed: 0
- Average duration: —
- Total execution time: 0.0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

**Recent Trend:**

- Last 5 plans: —
- Trend: —

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 01 P01 | 50min | 3 tasks | 14 files |
| Phase 01 P02 | ~25min | 2 tasks | 5 files |
| Phase 01 P04 | 30min | 2 tasks | 20 files |
| Phase 01 P05 | 35min | 2 tasks | 1 files |
| Phase 02 P01 | 15min | 4 tasks | 30 files |
| Phase 02 P02 | 24min | 3 tasks | 29 files |
| Phase 02 P03 | 22min | 3 tasks | 38 files |
| Phase 02 P04 | 90min | 3 tasks | 71 files |
| Phase 02 P05 | 55min | 3 tasks | 16 files |
| Phase 02 P06 | 70min | 3 tasks | 31 files |
| Phase 02 P07 | 65min | 3 tasks | 26 files |
| Phase 02 P08 | 95min | 3 tasks | 27 files |
| Phase 02 P09 | 30min | 2 tasks | 34 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Roadmap]: ROADMAP phases are numbered 1–7; design-doc phases are 0–6. Fixed offset — ROADMAP Phase N = `claude_docs/0(N−1)-*.md`. GSD treats phase 0 as a sentinel, so it cannot be used.
- [Roadmap]: `size.*` (SIZE-01) pulled forward from the final phase into Phase 3 — depends only on PacketScan (research: ARCHITECTURE §7).
- [Roadmap]: Interval statistics (PROBE-10) extracted as a shared probe-level primitive in Phase 3, consumed by both Phase 4 video and Phase 5 timeline — resolves the frame-rate ordering inversion without reordering phases (hazard A).
- [Roadmap]: Trust requirements (TRUST-01..09) distributed across Phases 2/3/6/7 rather than collected into a trailing trust phase.
- [Phase 1 scope]: Two open decisions must be recorded during Phase 1 — FFmpeg major baseline (9.0 vs 8.1, BUILD-10) and the `expected<T,E>` implementation (backport library vs hand-rolled, BUILD-07).
- [Phase 1]: FFmpeg pinned to 8.1 (port-version 4) via vcpkg.json overrides — recorded in PROJECT.md per BUILD-10
- [Phase 1]: mediadiff::expected<T,E> aliases tl-expected 1.3.1 in src/util/expected.h, the sole permitted tl::expected naming site — recorded in PROJECT.md per BUILD-07
- [Phase ?]: BUILD-04 negative control built locally (gcc --no-as-needed against apt libavcodec60) since no naturally dynamically-linked-against-FFmpeg binary existed in the sandbox
- [Phase ?]: Integration Catch2 test names carry no TEST_PREFIX (unlike unit's 'unit.'); the required -R version_output/-R vmaf_absent filters match directly via literal TEST_CASE name substrings
- [Phase ?]: gen_corpus version parser explicitly accepts git-describe 'N-<count>-g<hash>' snapshot ffmpeg builds (this machine's real ffmpeg: N-126086-ge5ecfe8970-20260812) as satisfying the 6.1 floor, rather than rejecting them for lacking a bare MAJOR.MINOR
- [Phase ?]: ENG-16 lint's SCAN_DIRS list drawn from CMakeLists.txt's actual add_library(libmediadiff ...) membership (includes src/util, excludes src/cli), not an assumed directory convention
- [Phase ?]: 01-05: CI matrix authored (5 legs, NuGet/GitHub-Packages vcpkg cache); windows-2022 pinned explicitly (not windows-latest, which now resolves to VS2026); nasm/mono installed explicitly on every non-Windows leg after runner-image READMEs showed neither confirmed present
- [Phase ?]: 01-05: fork-PR cache reads use github.token (read-only, auto-downgraded); write path exclusively uses VCPKG_PAT_TOKEN, structurally unavailable to fork-triggered pull_request runs
- [Phase ?]: 02-01: Froze contracts — schema_version="1.0", seed check IDs (meta.tool_version registered; meta.missing_candidate/extra_candidate approved for 02-11), time value {num,den,tb,ms}, ID grammar [a-z0-9_]+(\.[a-z0-9_]+)*
- [Phase ?]: 02-01: nlohmann_json linked explicitly via find_package/target_link_libraries rather than the FFMPEG-include-dir backdoor Phase 1 used implicitly for tl-expected
- [Phase ?]: 02-01: compare's exit code is set via std::exit() inside the CLI11 subcommand callback (sanctioned — ENG-16 lint excludes src/cli/ and names exit() the CLI's prerogative)
- [Phase ?]: 02-02: Aliases declared inline on owning check (checks.def aliases=[...]); meta.tool_version carries demonstrative alias + strict-bitexact severity override so resolve_alias/severity_for are exercised against the real registry
- [Phase ?]: 02-02: Process-spawn primitive extracted into tests/process_spawn.h (cli_harness.h now a thin wrapper) so the unit test target can spawn the Python interpreter without MEDIADIFF_BINARY
- [Phase ?]: 02-03: tools/gen_registry.py gained --symbol-prefix so the same generator produces a second, independent registry (TestCheckId/test_registry()); its --out-dir basename now derives each generated .cpp's include path instead of a hard-coded 'core/' literal
- [Phase ?]: 02-03: D-14/D-15/D-16 fail-first coverage gate and permanent canary built as Wave-0-style infrastructure before any real comparison semantic beyond exact exists; six not-yet-implemented semantics tracked in a self-verifying allow list in test_fail_first_coverage.cpp
- [Phase ?]: 02-03: the cli_harness.h EINTR-as-EOF/pipe-leak/waitpid fix actually landed in tests/process_spawn.h -- 02-02 already extracted the POSIX spawn loop there, so cli_harness.h itself needed no change
- [Phase ?]: 02-03: D-17 fail-first discipline (fixture-pair-per-semantic, semantic-crossed-with-status, permanent canary) recorded in .planning/PROJECT.md's new Conventions section as a binding project rule for Phases 3-7
- [Phase ?]: 02-04: Two-threshold tolerance grammar accepts an optional matching warn-side suffix (both 3,5ms and 3ms,5ms parse identically); Unit::count's suffix is bytes plus the bare no-suffix form
- [Phase ?]: 02-04: D-09 value_kind guard lives in compare/engine.cpp itself (compare_fingerprints), not only in core/serializer.cpp's value_from_json -- covers in-process Measurements a future analyzer constructs with no snapshot round-trip
- [Phase ?]: 02-04: tests/support/test_checks.def extended with t.tol_info/t.dist_bins_fail/t.int64_count (plus tolerance/severity edits to t.tol_ms/t.dist_bins/t.span_runs) so every (semantic, status) coverage cell has a real fixture despite one severity per check
- [Phase ?]: 02-05: resolve_severity is dual-mode (per_check-authoritative when populated, fresh builtin+profile recompute when empty) so every 02-04-era comparator and the pre-02-06 CLI path stayed untouched while a later-layer override still gates
- [Phase ?]: 02-05: the volatile rule is applied at resolve_policy's builtin layer (unconditional ignore in all five profiles) rather than a post-pass, so only an explicit later apply_severity_override can promote it
- [Phase ?]: 02-05: transform_affected added to registry.h/gen_registry.py beyond the plan's files_modified list (Rule 2) since Task 3's action text required generator support; no shipped check declares it until Phase 4
- [Phase ?]: 02-06: resolve_policy's config/cli_overrides parameters default to nullopt/{} so every pre-existing two-argument call site (engine.cpp, 02-04/02-05-era comparator tests) kept compiling unchanged
- [Phase ?]: 02-06: PolicyProvenance chain stays severity-only; tolerance overrides (config/override/--tol) replace ResolvedCheck::tolerance directly via new apply_tolerance_override with no chain entry, matching the original builtin/profile-layer tolerance write's own no-chain precedent
- [Phase ?]: 02-06: [override.*] blocks apply unconditionally inside resolve_policy (no file-path parameter to filter by); dir-mode path-glob filtering is deferred to plan 02-11
- [Phase ?]: 02-07: serialize_document's newline rule keyed on 'has any earlier sibling placed a scalar', not 'not first child' -- a leading empty-container sibling would otherwise produce a phantom blank line, breaking lines(doc)==scalars(doc)
- [Phase ?]: 02-07: double_to_json stores a native double; std::to_chars formatting happens exactly once, inside serialize_document -- the one and only place a float becomes text
- [Phase ?]: 02-07: decode_path/sampling stay raw nlohmann::ordered_json on Envelope (no typed struct) since no analyzer populates either until Phase 3
- [Phase ?]: 02-07: mediadiff snapshot dispatches on whether <file> reads as a valid *.snap.json (re-materialize) vs anything else (honest probe-layer-not-yet-present refusal) -- makes SNAP-07's gate CLI-testable this phase with no stub analyzer in the shipped binary
- [Phase ?]: 02-07: added util/fs.h::rename_replace_utf8 (Rule 3) -- MSVC CRT rename() does not replace an existing destination the way POSIX rename() does, which would have silently broken --force's overwrite semantics on Windows
- [Phase ?]: 02-07: added t.real_ratio (value_kind=real) to tests/support/test_checks.def (Rule 2) -- no check previously exercised Value's double alternative through the registry-dispatched read/write path this plan's own must_haves require covering
- [Phase ?]: 02-08: ReportModel derives Group from a check id's own first dot-segment, deliberately distinct from CheckDef::group; render_json takes the fully-resolved Policy (not just CheckRegistry) so tolerance/severity_chain reflect config/CLI overrides actually applied this run
- [Phase ?]: 02-08: Markdown's fold is two-tier (non-gating findings dropped from the canonical end first, warn-under-strict second); Severity::fail is never dropped under any circumstance
- [Phase ?]: 02-08: tests/baseline/report-1.0.json (TRUST-08 frozen oracle) regenerated against this plan's own new report shape via the exact command its file header prescribes -- the schema itself is this task's deliverable
- [Phase ?]: 02-09: CheckDef::explain_accept/explain_tune/explain_silence carried directly on CheckDef (not through the enum-typed explain_doc(CheckId) accessor) so render_tty works unchanged against both builtin_registry() and test_registry()
- [Phase ?]: 02-09: TTY prints to stdout only when --json was not requested in any form; the summary line and accept/tune/silence hint text are word-wrapped to terminal_width while a finding row's value column is elided instead

### Pending Todos

None yet.

### Blockers/Concerns

- **VIDEO-11 placement is a judgment call.** `video.closed_captions` is mapped to Phase 7 (needs the decode pass) rather than Phase 4 where its namespace lives. Phase 4 still registers the check and ships the `skipped:requires_decode` path. Revisit if Phase 4 planning finds a parser-level detection route.
- **Priming extraction spike is open.** Research flagged (v2 EXT-05) whether lightweight audio-priming extraction from container metadata is feasible ahead of the Phase 6 decode path. Until answered, Phase 5's `timeline.av_offset`/`av_drift` ship with `priming: unknown` on the common case — covered by TIME-10 fixtures, not closed.
- **Phase 2 is large** (48 requirements). Expect it to decompose into several plans; it is one phase because doc 01 is one acceptance unit and no analyzer can be tested before it lands.
- BUILD-01/BUILD-05/BUILD-06 remain unproven: .github/workflows/ci.yml was authored and passes every locally-verifiable check (YAML validity, both tasks' automated verify scripts, all grep-based acceptance criteria), but no commit was pushed to origin during 01-05's execution, so the matrix actually reporting green, the two-run vcpkg cache restore proof, and fork-PR read/write behavior are all unverified pending a real CI run

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260815-m5g | Pin Python to 3.11+ in CI so the Phase 2 registry generator can rely on stdlib tomllib | 2026-08-15 | 2a628fd | [260815-m5g-pin-python-to-3-11-in-ci-so-the-phase-2-](./quick/260815-m5g-pin-python-to-3-11-in-ci-so-the-phase-2-/) |

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| *(none)* | | | |

## Session Continuity

Last session: 2026-08-15T19:04:06.671Z
Stopped at: Completed 02-09-PLAN.md
Resume file: None
