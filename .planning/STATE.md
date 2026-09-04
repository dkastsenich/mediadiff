---
gsd_state_version: 1.0
milestone: v0.6.1
current_phase: 03
current_phase_name: probe-layer-container-size
status: executing
stopped_at: Completed 03-13-PLAN.md (mp4 fragment-duration CR-01/CR-02 gap closed, WR-02/WR-03 fixed)
last_updated: "2026-09-04T19:28:47.237Z"
last_activity: 2026-09-04
last_activity_desc: Phase 03 execution started
state_head: 24e9fae2eb6f6d8fdfa5cacd67b97ff913783360
progress:
  total_phases: 7
  completed_phases: 2
  total_plans: 39
  completed_plans: 37
milestone_name: milestone
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-12)

**Core value:** A media-aware diff CI can trust — a no-change re-run under the right profile is clean out of the box, every real regression is caught, explained, and actionable. False positives are P0.
**Current focus:** Phase 03 — probe-layer-container-size

## Current Position

Phase: 03 (probe-layer-container-size) — EXECUTING
Plan: 3 of 15
Status: Ready to execute
Last activity: 2026-09-04 — Phase 03 execution started

Progress: [█████████░] 92%

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
| Phase 02-core-engine P10 | 35min | 3 tasks | 24 files |
| Phase 02 P11 | 3h10min | 3 tasks | 30 files |
| Phase 03 P01 | 50min | 4 tasks | 13 files |
| Phase 03 P02 | 110min | 3 tasks | 37 files |
| Phase 03 P03 | 100min | 3 tasks | 23 files |
| Phase 03 P04 | 150min | 3 tasks | 27 files |
| Phase 03 P05 | 140 | 3 tasks | 22 files |
| Phase 03 P06 | 37min | 3 tasks | 25 files |
| Phase 03 P07 | 40min | 3 tasks | 10 files |
| Phase 03 P08 | 30min | 3 tasks | 22 files |
| Phase 03 P09 | 90min | 3 tasks | 20 files |
| Phase 03 P10 | 80min | 3 tasks | 20 files |
| Phase 03 P11 | 45min | 4 tasks | 23 files |
| Phase 03 P12 | 55min | 3 tasks | 14 files |
| Phase 03 P13 | 55min | 3 tasks | 7 files |

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
- [Phase ?]: 02-10: run_compare() extracted from compare.cpp's callback and exported so the implicit two-positional route (mediadiff a b) and the compare subcommand dispatch through the exact same function, never a parallel copy
- [Phase ?]: 02-10: exit_code_for_findings derives the exit code from Summary::worst_gating (severity), replacing the prior per-Status worst_status helper -- matches report/model.h's own documented axis distinction
- [Phase ?]: 02-10: core/snapshot.cpp's read_snapshot now reads a top-level partial boolean from snapshot JSON (previously hard-coded false) -- makes CLI-07's exit-66 fixture route testable ahead of a real probe/decode layer
- [Phase ?]: dir mode's per-pair compare pipeline calls read_snapshot/resolve_policy_for_file/compare_fingerprints directly rather than run_compare() (which is [[noreturn]], structurally incompatible with aggregating N files into one corpus report)
- [Phase ?]: Per-file policy resolution splits into a shared base Policy (builtin/profile/config-top-level, no path overrides, no CLI) plus resolve_policy_for_file applying path-matching config overrides then CLI overrides onto a copy per file, so no worker-pool job mutates shared state
- [Phase ?]: A hard per-file error or partial-decode marker never aborts the corpus run mid-flight; exit-code escalation (hard error > any-partial > findings-based) is decided only after every requested report destination has been written
- [Phase ?]: docs/schema/report-1.0.json extended with top-level oneOf(findings, files) and a shared $defs/summary, keeping the single-file document shape byte-for-byte unchanged while a corpus document validates under the same schema file
- [Phase ?]: 03-01: SkipReason extended to 14 enumerators; Measurement.estimated/Finding.evidence added with single-seam propagation; detail::checked_div added; D-03 3x tolerance widening implemented; 27-id Phase-3 check roster approved (approve-as-proposed)
- [Phase ?]: fingerprint_input distinguishes JSON-shaped-but-rejected snapshots from non-JSON via a first-non-whitespace-byte peek (looks_like_json_document), so a real probe never masks read_snapshot's own diagnostic
- [Phase ?]: DemuxOptions::wall_clock_budget_ms defaults from a runtime-mutable atomic (default_wall_clock_budget_ms), so --probe-timeout reaches DemuxSession::open without orchestrator.cpp's frozen 2-arg fingerprint_input call site ever changing
- [Phase ?]: Fixed report/model.cpp accumulate(): Summary.worst_gating no longer gates on a Status::pass finding's own declared severity -- only Status::warn/fail/error gate, restoring a clean exit on a routinely-passing fail-severity check
- [Phase ?]: 03-03: Rule-1 fix — DemuxSession's AVIOInterruptCB heap-owned (std::unique_ptr) and disarmed in place, not by clearing the AVFormatContext field, since ffmpeg's avio layer copies the callback into its own URLContext at avio_open2() time independent of AVFormatContext::interrupt_callback thereafter (found via AddressSanitizer stack-use-after-return).
- [Phase ?]: 03-03: D-01 byte budget accounted globally across all of one file's streams (a single running total), not per-stream — matches 'peak accounted bytes per in-flight file'; doc 02's per-stream 5M-packet ceiling stays a separate, independently-scoped mitigation.
- [Phase ?]: 03-03: kMaxDirThreads(=32) relocated to src/config/toml_load.h as one named constant shared by dir.cpp's --threads path and toml_load.cpp's [dir] threads path, closing T-2-41.
- [Phase ?]: [Phase 3, 03-04]: Measurement.skip_reason added (mirrors 03-01's Measurement.estimated) so an analyzer can explicitly mark a check not-applicable-here; compare/engine.cpp short-circuits to skipped ahead of normal dispatch, inspect.cpp renders it -- the engine's pre-existing unpaired-measurement path was verified (not assumed) to drop such a check with no Finding at all
- [Phase ?]: [Phase 3, 03-04]: container.track_types/track_order canonical string encodings locked (comma-joined type tokens; comma-joined media_type:codec_name using avcodec_get_name) -- costly to change, enters committed snapshots
- [Phase ?]: [Phase 3, 03-04]: per-stream meta.tags/meta.tags.language Scope.index is the stream's rank among same-media-type streams, not raw stream-array position (Claude's Discretion, mirrors CONT-08's program_number stability rationale)
- [Phase ?]: container.mp4.* split into two AnalyzerSpecs (family-scoped real-data + family-agnostic not-applicable sibling) so bmff_scan never runs on non-MP4 bytes while inspect/compare still show an explicit skipped:not_applicable_container there -- the required pattern for 03-06/03-08's mkv/ts scanners too
- [Phase ?]: container.mp4.fragment_duration's median always derives from PacketScan keyframe DTS deltas, never sidx -- bmff_scan's approved scope only records sidx presence, not segment durations
- [Phase ?]: BoundedReader duplicated (not shared) between ebml_scan.cpp and bmff_scan.cpp -- the binary grammars diverge enough that sharing would add indirection without removing real duplication
- [Phase ?]: Segment walk stops at first Cluster; a trailing Cues is located exclusively via a guarded single-hop SeekHead-follow (bounds-check + ID-verify, no recursion)
- [Phase ?]: codec_delay ns-to-samples conversion is exclusively checked-integer arithmetic (checked_mul then checked_div), never floating point
- [Phase ?]: container_family_token centralized as a shared core primitive so probe's ContainerFamily and compare's cross-container demotion can never disagree
- [Phase ?]: 03-07: container_family.cpp needed no change -- confirmed empirically (inspect reports container.format==mpegts) that the existing mapping already covers this scanner's fixtures
- [Phase ?]: 03-07: mux-rate estimate derived from the first valid consecutive same-PID PCR pair only (not averaged), kept as an unreduced exact rational, never divided in this scanner
- [Phase ?]: 03-07: PSI section reassembly across packet boundaries is explicitly out of scope (T-3-33) -- a PAT/PMT section not fully contained in one packet is discarded and counted, never parsed from a truncated buffer
- [Phase ?]: 03-08: Rule 1 fix -- ts_scan.cpp's mux-rate estimate carried an erroneous *8 bytes-to-bits factor (bytes_per_second_num/den was actually bits/sec); removed, companion test corrected.
- [Phase ?]: 03-08: ProgramRateContext derives a per-program, PID-filtered mux-rate estimate in ts.cpp to sidestep ts_scan's flat list-adjacency limitation on interleaved multi-program PCR PIDs (03-07-SUMMARY.md's own flagged '03-08 owns' gap).
- [Phase ?]: 03-08: CONT-08 unpaired-program topology-fail path added to compare/engine.cpp -- generic over Scope::Kind::program, Status::fail unconditional on severity policy, mirrors the cross_container demotion shape.
- [Phase ?]: 03-09: DemuxSession::file_size_bytes() (avio_size on the already-open AVIOContext) added so size.file reads independently of PacketScan's own completeness -- the structural D-02 exemption for the one size.* check that is a property of the file, not the scan.
- [Phase ?]: 03-09: size.peak_bitrate's window-count bound (T-3-46) is kMaxWindowSteps=10,000,000, checked via one division before the sliding-window sweep starts; the {1001,30000} timebase test found no accumulation-vs-closed-form numeric divergence is constructible for correct all-integer arithmetic -- the static '+= step' grep gate plus the 10,000-window/far-boundary-spike test are what actually gate the architecture.
- [Phase ?]: 03-09: size_crf20.mp4/size_crf23.mp4 fixtures are -b:v (target-bitrate) driven, never -crf, extending gen_corpus.sh's own established never-libx264/GPL convention; a 13th fixture (size_partial.mp4, 25,000 tiny frames) was added beyond the plan's own fixture list since --probe-memory-budget-mb's 1 MB integer floor exceeds every other size_*.mp4 fixture's real packet-store need.
- [Phase ?]: 03-10: Mutation offsets derive from raw std::mt19937 output modulo range, never uniform_int_distribution (T-3-52) -- unspecified mapping across standard libraries would make a red CI leg unreproducible cross-platform
- [Phase ?]: 03-10: PRNG-seeded byte-flip test asserts 'never crashes, stays in bounds' rather than 'always degrades' -- empirically confirmed real scanners correctly tolerate most payload-region single-byte flips as valid (if anomalous) data
- [Phase ?]: 03-10: TS truncation at 10/50/90% needs truncate_to_fraction_off_stride -- ts_single.ts's 1270-packet count is evenly divisible by 10, so naive byte-fraction truncation coincidentally lands on packet boundaries and produces a validly-short (not corrupt) stream
- [Phase ?]: 03-10: TSDuck goldens captured via scripts/capture_tsduck_golden.sh (developer-only), compared read-only in CI via scripts/lint_tsduck_goldens.sh -- TSDuck never linked/installed on CI (TRUST-09, D-04 closed)
- [Phase ?]: 03-11: sanitize_for_display sanitizes before elision (never after), so an invisible escape sequence in a truncated tag value cannot corrupt width accounting or reappear unescaped past the ellipsis
- [Phase ?]: 03-11: inspect_render.h is a deliberate 4th sanitize_for_display call site (inspect's own text render), following Task 2's specific action text over the plan's summary verification line -- grep now shows 4 render call sites, which is correct
- [Phase ?]: 03-11: json.cpp/junit.cpp deliberately NOT sanitized -- wire-level JSON/XML escaping already applies; double-escaping would corrupt goldens
- [Phase ?]: 03-11: TRUST-06's corruption-catches-a-regression proof done manually once against a non-committed scratch fixture, per the plan's own acceptance criterion not to commit the corruption
- [Phase ?]: 03-11: DOC-03's gate calls the real CLI binary for every declared pair, matching every sibling integration test's convention; every trigger/clean pair verified empirically against the real binary before being written into the table
- [Phase ?]: [03-12]: kMaxProbeMemoryBudgetMb=1048576 MB / kMaxProbeTimeoutSeconds=86400s, bounded at both TOML loader and CLI parse boundary; resolve_probe_memory_budget_bytes is the single MB-to-bytes conversion point for all four command entry points.
- [Phase 03]: 03-13: Same-timebase tick-value ordering replaces compare_ticks_checked in container.mp4.fragment_duration's median (CR-02) -- valid because every duration compared shares one already-positive-validated timebase by construction, never a general substitute for cross-timebase comparisons
- [Phase 03]: 03-13: WR-02 (ts.cpp byte-offset checked_sub) and WR-03 (pass.h/orchestrator.cpp stale ts_scan consumer comments) fixed in the same plan as CR-01/CR-02 since both touch files this plan already opened; sanitizer (ASan/UBSan) build remains deferred per this plan's own flagged_assumptions

### Pending Todos

None yet.

### Blockers/Concerns

- **VIDEO-11 placement is a judgment call.** `video.closed_captions` is mapped to Phase 7 (needs the decode pass) rather than Phase 4 where its namespace lives. Phase 4 still registers the check and ships the `skipped:requires_decode` path. Revisit if Phase 4 planning finds a parser-level detection route.
- **Priming extraction spike is open.** Research flagged (v2 EXT-05) whether lightweight audio-priming extraction from container metadata is feasible ahead of the Phase 6 decode path. Until answered, Phase 5's `timeline.av_offset`/`av_drift` ship with `priming: unknown` on the common case — covered by TIME-10 fixtures, not closed.
- **Phase 2 is large** (48 requirements). Expect it to decompose into several plans; it is one phase because doc 01 is one acceptance unit and no analyzer can be tested before it lands.
- BUILD-01/BUILD-05/BUILD-06 remain unproven: .github/workflows/ci.yml was authored and passes every locally-verifiable check (YAML validity, both tasks' automated verify scripts, all grep-based acceptance criteria), but no commit was pushed to origin during 01-05's execution, so the matrix actually reporting green, the two-run vcpkg cache restore proof, and fork-PR read/write behavior are all unverified pending a real CI run
- scripts/gen_corpus.sh is never invoked in .github/workflows/ci.yml for Linux/macOS -- only the Windows gen_corpus.ps1 check runs; every corpus-dependent integration test would fail on a real CI run on 4 of 5 matrix legs until a fixture-generation step is added (WINDOWS.md #8, predates Phase 3)

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260815-m5g | Pin Python to 3.11+ in CI so the Phase 2 registry generator can rely on stdlib tomllib | 2026-08-15 | 2a628fd | [260815-m5g-pin-python-to-3-11-in-ci-so-the-phase-2-](./quick/260815-m5g-pin-python-to-3-11-in-ci-so-the-phase-2-/) |
| 260902-it6 | Migrate CLI option binding from shared_ptr to CLI::Option* (03-CONTEXT.md D-05) | 2026-09-02 | 8258c83 | [260902-it6-migrate-cli-option-binding-from-shared-p](./quick/260902-it6-migrate-cli-option-binding-from-shared-p/) |

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| *(none)* | | | |

## Session Continuity

Last session: 2026-09-04T19:28:47.124Z
Stopped at: Completed 03-13-PLAN.md (mp4 fragment-duration CR-01/CR-02 gap closed, WR-02/WR-03 fixed)
Resume file: None
