# Phase 2: Core Engine - Research

**Researched:** 2026-08-15
**Domain:** Compare-engine architecture in C++20 — check registry codegen, comparison semantics, policy/config precedence, canonical JSON/snapshot serialization, multi-format reporting, `dir` orchestration, CLI11 parsing — validated end-to-end against stub measurements (no real media I/O in scope)
**Confidence:** HIGH for library-level facts (all verified this session by reading the pinned vcpkg submodule's actual port manifests and installed headers at the exact `builtin-baseline` commit already resolved locally); MEDIUM for design-pattern guidance (cross-checked against `.planning/research/ARCHITECTURE.md`/`PITFALLS.md`/`FEATURES.md`, themselves built from web research); LOW/ASSUMED only where explicitly marked (generator internals, glob-matcher algorithm shape — genuine Claude's-discretion areas per 02-CONTEXT.md, not verifiable facts).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Check registry**

- **D-01: Generate the registry from checks.def with a build-time script.** — Reversibility: costly. One generator reads `src/core/checks.def` and emits the ID enum, the registry table, the embedded `--explain` documents and the docs manifest. X-macro was rejected for IDE/debugger hostility across ~60 checks and six phases; the constexpr table for the enum-vs-array drift problem it reintroduces.
- **D-02: The generator writes the `--explain` documents into a C++ translation unit, and fails the build when a registered ID has no matching file.** — Reversibility: reversible. Runtime file lookup rejected (breaks single-static-binary contract). C++23 `#embed` rejected as out of C++20 target.
- **D-03: Analyzers refer to checks through the generated enum; the string form exists only at the edges.** — Reversibility: costly. Call sites use `CheckId::video_color_range`. Strings appear only where they must: config globs, JSON output, `--explain`.
- **D-04: Each check declares one baseline severity and tolerance, plus explicit overrides only for profiles that differ.** — Reversibility: reversible.
- **D-05: The generator is written in Python 3.** — Reversibility: costly (new build prerequisite on every dev machine + CI leg). CMake script mode rejected (awkward `string(REGEX)` parsing). A C++ host tool rejected (host-vs-target cross-build complexity, real here since `x64-osx` already cross-builds from `arm64-osx`). **This is a new entry in PROJECT.md's Constraints and must be added there.**

**Measurement values**

- **D-06: Represent a measurement's value with `std::variant`.** — Reversibility: costly. Nine alternatives: `int64, rational, double, string, string_set, histogram, span_list, hash_chain, absent`. Hand-rolled tagged union rejected (owning copy/move/destroy correctness for nine alternatives, three heap-holding, is a large surface for a byte-identical-serialization type).
- **D-07: Define the rational type in core and convert from the libav one at the edge.** — Reversibility: costly. Keeps `core/`/`compare/` free of any libav include — unit-testable without FFmpeg linked, which matters in a phase with no media and where FFmpeg is the 40-minute dependency.
- **D-08: One central serializer owns canonical output.** — Reversibility: reversible. Single visit-based writer owns field order, shortest-round-trip float formatting, one-value-per-line layout. Rejected: nine independent per-type serializers (one divergent float format silently breaks SNAP-03/TRUST-05).
- **D-09: The registry's declared value kind is authoritative; a mismatch is an error, never a coercion.** — Reversibility: reversible. `CheckDef.value_kind` declares what a check emits; engine asserts and produces `status=error` on mismatch.

**Proving the engine without media**

- **D-10: Hand-authored snapshot pairs are the primary integration harness.** — Reversibility: reversible. `compare baseline.snap.json candidate.snap.json` drives compare → policy → report with no analyzer and no media.
- **D-11: The stub analyzer is a test-only target and never enters the shipped binary.** — Reversibility: reversible. Linked into test executables only. Accepted cost: shipped `snapshot` has no exercised path until Phase 3. Hidden dev flag and build-flag-gated stub both rejected (BUILD-09 showed how easily a default-off build option drifts).
- **D-12: `UPDATE_GOLDENS` is a local developer affordance; CI runs read-only and fails on any diff.**
- **D-13: Determinism is proven by running compare twice and diffing.** — Cheap, runs on every CI leg, catches platform float/ordering drift for free.

**Fail-first discipline**

- **D-14: Every comparison semantic must declare a fixture that passes and one that must not pass, and the harness fails if either is missing.** — Reversibility: costly (every check added in Phases 3–7 inherits it). Direct counter to Phase 1's six structurally-blind checks. A registry field rejected (bakes test metadata into shipped product). A CI script auditing test sources rejected (it would itself be an unobservable check).
- **D-15: The unit is each semantic crossed with each status it can emit — not merely pass versus fail.** — Reversibility: costly. Seven semantics × `pass|info|warn|fail|skipped|error`; `skipped ≠ pass` is load-bearing.
- **D-16: A permanent canary fixture must always report failing.** — One deliberately-wrong fixture whose expected outcome is a specific failure; if the suite ever reports it clean, the harness is broken.
- **D-17: The discipline is both a Phase 2 acceptance criterion and a recorded project convention.**

### Claude's Discretion

Every area presented was decided. The planner retains normal latitude on file and namespace layout within `core/`, `config/`, `compare/` and `report/`; the internal shape of the generator; and how plans are sliced across the 48 requirements.

### Deferred Ideas (OUT OF SCOPE)

- **TRUST-08's cross-release idempotence has a bootstrap problem.** Comparing current build against a *previous release's* snapshot — and there is no previous release yet. "Silently pass when there is no baseline" would itself be an unobservable check. **Open — see Open Questions.**
- **What `Error` carries and how it composes across layers.** `Error.kind` maps to exit codes; whether it carries context, a cause chain, or a source location is undecided. **Open — see Open Questions.**
- **Whether the four report formats share an intermediate representation** or each render findings independently. **Open — see Open Questions.**
- **How `list-checks --effective` shows policy provenance** — the resolved severity chain must be visible under `-v`, implying the merge retains where each value came from. **Open — see Open Questions.**

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CLI-01 | Two bare positionals → implicit compare | Code Examples §CLI11 skeleton; doc 00 §3.2's own sample code, verified against the installed CLI11 2.6.2 headers (`App::add_option`, positional binding) |
| CLI-02 | `compare/snapshot/dir/inspect/list-checks/explain` subcommands | Architecture Patterns §Project Structure; `CLI::App::add_subcommand` verified in installed headers |
| CLI-03 | Repeatable `--set`/`--tol`, later flags win in argv order | Common Pitfalls §CLI11 vector accumulation; Code Examples §Repeatable options |
| CLI-04 | `--json[=path]`, repeatable `--report kind=path` | Code Examples §Report flag parsing |
| CLI-06 | Exit code contract 0/1/2/64/65/66/70 | Common Pitfalls §CLI11's own exit codes are NOT the mediadiff contract (verified: `CLI::ExitCodes` enum ranges 100–127) |
| CLI-07 | Partial JSON emitted on exit 66 | Architecture Patterns §Report/compare interaction |
| CLI-08 | Color auto-disable on `NO_COLOR`/non-TTY/`CI=true`, stays on for `GITHUB_ACTIONS` | Code Examples §fmt color styling (verified: `fmt/color.h` present, `text_style`/`emphasis` API) |
| CLI-10 | Wrong-unit tolerance → exit 64 naming expected unit | Architecture Patterns §Tolerance grammar parser |
| ENG-01 | Registry generates ID enum + registry + docs manifest; build fails on missing doc | Architecture Patterns §Registry generator pattern; Don't Hand-Roll |
| ENG-02 | Segment-wise glob match (`*`, `**`), no regex | Architecture Patterns §Glob matcher (ASSUMED algorithm shape — Claude's discretion) |
| ENG-03 | `deprecated_alias` resolved at config-parse with warning | Common Pitfalls §ESLint alias-ambiguity lesson |
| ENG-04 | Seven comparison semantics | Architecture Patterns §compare/ engine; source doc 01 §3 table |
| ENG-05 | Time tolerances in ticks, never floats | Architecture Patterns §Rational type (D-07) |
| ENG-06 | Severity resolution chain, last-writer-wins, `-v` shows chain | Architecture Patterns §Policy merge; Open Questions §provenance |
| ENG-07 | `volatile` checks default `ignore`, still computed | Architecture Patterns §Policy merge |
| ENG-08 | Five profiles behave per matrix | Architecture Patterns §Profiles |
| ENG-09 | Default profile `sw-encoder` | Architecture Patterns §Profiles |
| ENG-10 | `transform` profile expectation blocks | Architecture Patterns §Profiles |
| ENG-11 | Config merge precedence | Architecture Patterns §Policy merge |
| ENG-12 | `list-checks --effective` | Open Questions §provenance display |
| ENG-13 | `explain <check.id>` | Architecture Patterns §Registry generator pattern |
| ENG-14 | `skipped` first-class, machine-readable reason | Architecture Patterns §Finding model |
| ENG-15 | Error kind → exit code mapping, no exceptions across lib boundary | Common Pitfalls §CLI11 exit codes; Open Questions §Error composition |
| ENG-16 | `libmediadiff` no stdout, no `exit()` | Code Context — enforced today by `scripts/lint_eng16.sh`; unchanged in Phase 2 |
| SNAP-01 | `snapshot` writes `*.snap.json` with envelope | Architecture Patterns §Snapshot format |
| SNAP-02 | `compare` accepts snapshot in place of live media | Architecture Patterns §Fingerprint pattern |
| SNAP-03 | Canonical, git-diffable output | Common Pitfalls §`std::to_chars` float formatting availability; Architecture Patterns §Central serializer |
| SNAP-04 | Times as `{num, den, tb}` rationals | Architecture Patterns §Rational type |
| SNAP-05 | Tool-version skew warning; `schema_version` major mismatch → exit 65 | Architecture Patterns §Snapshot format |
| SNAP-06 | `snapshot f && compare f f.snap.json` clean, permanent CI test | Locked decision D-10 |
| SNAP-07 | `snapshot` refuses to overwrite tracked baseline in CI without `--force` | Common Pitfalls / FEATURES gap 1 — Jest/`insta`/ApprovalTests precedent |
| REPORT-01 | JSON schema-validated in CI, byte-identical | Standard Stack §json-schema-validator (verified new vcpkg port); Common Pitfalls §to_chars |
| REPORT-02 | TTY fixed group order, non-pass by default, width-aware | Architecture Patterns §Report renderers |
| REPORT-03 | Accept/tune/silence triple per gating finding | Architecture Patterns §Finding model |
| REPORT-04 | Markdown summary + `<details>`, overflow fold | Architecture Patterns §Report renderers |
| REPORT-05 | Markdown cap as character budget under GitHub's 65,536 | Common Pitfalls §Pitfall: report-size cap units |
| REPORT-06 | JUnit one `<testcase>` per gating-capable finding | Architecture Patterns §Report renderers |
| REPORT-07 | `inspect <file>` renders full analysis | Out of this phase's stub-analyzer scope for real content, but the renderer path is built here |
| DIR-01 | Pair by relative path, unpaired → fail/warn | Architecture Patterns §dir orchestration |
| DIR-02 | `dir` default header+packet, `--content` opts into decode | Out of scope for real decode (Phase 3); the flag plumbing is Phase 2's job |
| DIR-03 | Per-file rollup, corpus totals, worst-N, `files[]` JSON layer | Architecture Patterns §dir orchestration |
| DIR-04 | Deterministic sorted file order | Architecture Patterns §dir orchestration |
| DIR-05 | `--threads N` bounded worker pool | Common Pitfalls §Parallelism/memory (ARCHITECTURE §6); Code Examples §Worker pool shape |
| TRUST-03 | Class-2 path signature includes libav* toolchain versions | Common Pitfalls §Pitfall 1 — path signature must include toolchain, not just device/driver |
| TRUST-05 | `compare` twice → byte-identical `--json` | Locked decision D-13 |
| TRUST-08 | Cross-release idempotence test | Open Questions §bootstrap problem |
| DOC-01 | Every registered check has `docs/checks/<id>.md`, build-enforced | Architecture Patterns §Registry generator pattern |
| DOC-02 | `--explain` states what/why/how to accept-tune-silence | Architecture Patterns §Registry generator pattern |

</phase_requirements>

## Summary

Phase 2 has almost no *new* third-party-library risk: every C++ dependency it needs (CLI11 2.6.2, fmt 12.2.0, nlohmann-json 3.12.0 with `ordered_json`, tomlplusplus 3.4.0, tl-expected 1.3.1, Catch2 3.15.3) is already pinned in `vcpkg.json` and was resolved and installed locally during Phase 1 — this session verified all of them directly against the pinned `builtin-baseline` commit's port manifests and the already-installed headers under `vcpkg/packages/`, not against training-data version numbers. The one genuinely new candidate dependency this research surfaced is `json-schema-validator` (pboettch, MIT, wraps nlohmann-json, **version 2.4.0 present at the exact pinned baseline**) for REPORT-01's CI schema validation — confirmed to exist in the vcpkg registry at this project's exact baseline commit, not merely "exists on vcpkg in general." Python 3 (D-05's generator language) is already present locally (3.12.3) and ships on every GitHub-hosted CI image.

The real work of this phase is architectural, not dependency-acquisition: a build-time Python generator that turns `src/core/checks.def` into an ID enum + registry table + compiled-in `--explain` text + a docs-manifest build failure; a `std::variant`-based nine-alternative `Value` type with one central visit-based canonical serializer; a policy-precedence merger (profile → config → CLI, last-writer-wins, in-order); seven comparison semantics; four independent report renderers sharing a `Finding[]` input; and a `dir`-mode worker pool. None of this touches FFmpeg or any media I/O — the entire phase is provable against hand-authored `.snap.json` fixtures and a test-only stub analyzer, which is exactly what D-10/D-11 lock in.

Two concrete, previously-undocumented risks surfaced by this session's direct verification, both worth flagging to the planner before task-writing: (1) **CLI11's own `App::exit()` return value is in the 100–127 range** (`CLI::ExitCodes`), which does **not** match mediadiff's required exit-code contract (64/65/66/70) — `cli/main.cpp` must catch `CLI::ParseError` and translate it to `64` explicitly rather than trusting `app.exit(e)`'s return value, or CLI-06/CLI-10 will silently ship the wrong exit codes. (2) **Floating-point `std::to_chars` (needed for D-08's shortest-round-trip float formatting) carries a strict Apple-platform availability floor of macOS 13.3**, gated by libc++'s own availability annotations independent of Xcode/AppleClang compiler version — the project currently sets no `CMAKE_OSX_DEPLOYMENT_TARGET`, which is the safe default (defaults to the SDK's own version, well above 13.3), but this becomes a real build break the moment anyone adds an explicit lower deployment target for backward-compat reasons without re-checking this constraint.

**Primary recommendation:** Build the registry generator and the `Value`/serializer core first (nothing else in the phase can be tested without them), prove the whole compare→policy→report pipeline against Catch2 fixtures and the stub analyzer per D-10, and treat the fail-first discipline (D-14–D-17) as infrastructure to build in Wave 1, not a testing afterthought bolted on at the end — Phase 1's verification report is explicit that retrofitting "does this check actually observe its subject" cost a dedicated remediation pass after the fact.

## Architectural Responsibility Map

mediadiff is a single-process CLI/library pair, not a multi-tier web application — the standard Browser/SSR/API/CDN/Database tiers don't apply. The project's own architecture doc (`claude_docs/00-design-and-requirements.md` §6) already defines the correct tier boundaries for this shape; this map applies Phase 2's capabilities against those tiers so the planner can sanity-check task placement.

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| CLI parsing, subcommand dispatch, exit-code translation | `cli/` (process control) | — | ENG-16: only `cli/` may call `exit()` / write to stdout |
| TTY rendering, ANSI/color policy | `cli/` (process control) | `report/` (data shaping) | doc 01 §9: "TTY: rendered in `cli/`"; JSON/MD/JUnit render in `report/` |
| Check registry, ID enum, `--explain` text | `core/` (library) | build-time generator (Python, out-of-binary) | ENG-01/DOC-01/DOC-02: compiled-in, no runtime file lookup |
| `Value`/`Measurement`/`Fingerprint` types, rational math | `core/` (library) | — | D-06/D-07: libav-free, unit-testable without FFmpeg |
| Comparison semantics (`exact/±tol/set/presence/hash/dist/span`) | `compare/` (library) | `core/` (consumes Fingerprint+Policy only) | ARCHITECTURE §5: `compare/` never touches `probe/` or live files |
| Policy resolution (profiles, config precedence, `--set`/`--tol`) | `core/` + `config/` (library) | `cli/` (CLI11 option capture only) | ENG-06/ENG-11: merge logic is library-testable; CLI11 just captures argv |
| Snapshot I/O (`*.snap.json` read/write, canonical serialize) | `core/` (library) | `util/fs.h` (file I/O shim) | SNAP-01…07: no stdout involvement, pure library |
| JSON/Markdown/JUnit report rendering | `report/` (library) | — | doc 00 §7: `report/` = json, markdown, junit; tty lives in `cli/` |
| `dir` orchestration, worker pool, file pairing | `cli/` (process control, per doc 01 §10's own text) | `core/`/`compare/` (per-file logic reused unchanged) | doc 01 §10 explicitly places "all process control" in `cli/`; per-file analysis stays single-threaded/synchronous per ARCHITECTURE §6 |
| Stub analyzer (test harness only) | test executables only | — | D-11: never links into the shipped binary |

## Project Constraints (from CLAUDE.md)

- Language: C++20, no modules, `std::format` avoided in favor of fmt — GCC ≥ 12, Clang ≥ 15, AppleClang (Xcode 15+), MSVC v143. **New this phase per D-05: Python 3 joins the build-prerequisite list** (must be recorded in PROJECT.md's Constraints section as part of Phase 2 work).
- Build: CMake ≥ 3.25 with presets, ninja, git.
- Error handling: `mediadiff::expected<T, Error>`-style in `core/`, no exceptions across the lib boundary; the CLI maps `Error.kind` to exit codes. `src/util/expected.h` is the sole permitted site naming `tl::expected` directly (already built in Phase 1, `tl-expected` 1.3.1 pinned).
- Time representation: rational everywhere (`{int64 value, AVRational tb}` at the libav edge; a libav-free POD rational type inside `core/`/`compare/` per D-07). Floating milliseconds only in rendered output.
- Determinism: byte-identical `--json` across identical runs; fixed-K/fixed-ε algorithms; integer/rational inputs.
- Check IDs are forever: additions fine, renames only via alias + deprecation (ENG-03).
- `libmediadiff` writes to no standard stream and never calls `exit()` (ENG-16) — enforced today by `scripts/lint_eng16.sh`, scanning `src/core`, `src/config`, `src/probe`, `src/analyzers`, `src/compare`, `src/report`, `src/util`. This phase's new `src/core`, `src/config`, `src/compare`, `src/report` sources fall directly under this existing lint; no lint change needed unless a new top-level `src/` subtree is introduced.
- GSD workflow enforcement: file-changing work must go through a GSD command (`/gsd-execute-phase` etc.), not direct edits outside the workflow.

## Standard Stack

### Core (already pinned — no new manifest entries required for these)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| CLI11 | 2.6.2 `[VERIFIED: vcpkg/ports/cli11/vcpkg.json:3]` — `"version": "2.6.2"` | CLI parsing, subcommands, repeatable options | Already resolved and linked in Phase 1's `mediadiff` target; header-only via `find_package(CLI11 CONFIG REQUIRED)`, already wired in `CMakeLists.txt` |
| fmt | 12.2.0 `[VERIFIED: vcpkg/packages/fmt_x64-linux/include/fmt/base.h:24]` — `#define FMT_VERSION 120200` | TTY rendering, ANSI/color styling, all text formatting | Already linked (`target_link_libraries(libmediadiff PUBLIC ... fmt::fmt)`); `fmt/color.h` present with `text_style`/`emphasis`/`ansi_color_escape` for CLI-08 |
| nlohmann-json | 3.12.0, port-version 2 `[VERIFIED: vcpkg/ports/nlohmann-json/vcpkg.json:3-4]` — `"version-semver": "3.12.0", "port-version": 2` | JSON report + snapshot serialization | `ordered_json` confirmed present: `[VERIFIED: vcpkg/packages/nlohmann-json_x64-linux/include/nlohmann/json_fwd.hpp:71]` — `using ordered_json = basic_json<nlohmann::ordered_map>;` — this is the type SNAP-03/D-08's canonical field-order requirement depends on |
| tomlplusplus | 3.4.0, port-version 1 `[VERIFIED: vcpkg/ports/tomlplusplus/vcpkg.json:3-4]` — `"version": "3.4.0", "port-version": 1` | `mediadiff.toml` parsing (`config/`) | Header-only, TOML 1.0.0 |
| tl-expected | 1.3.1 `[VERIFIED: vcpkg/ports/tl-expected/vcpkg.json:3]` — `"version": "1.3.1"` | Backing type for `mediadiff::expected<T,E>` | Already the sole error-handling vehicle across `core/`, aliased in `src/util/expected.h` since Phase 1 |
| Catch2 | 3.15.3 `[VERIFIED: vcpkg/ports/catch2/vcpkg.json:3]` — `"version-semver": "3.15.3"` | Unit/integration tests, CTest | `Catch2::Catch2WithMain` target already wired; `TEST_PREFIX "unit."` / `"integration."` convention already established in `tests/{unit,integration}/CMakeLists.txt` |

### Supporting (new for this phase)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| json-schema-validator (pboettch) | 2.4.0 `[VERIFIED: vcpkg/ports/json-schema-validator/vcpkg.json:3]` — `"version": "2.4.0"` | REPORT-01's CI JSON-schema validation of `docs/schema/report-1.0.json` | Depends on `nlohmann-json` (already present); confirmed present at the exact pinned `builtin-baseline` commit `105fdc246ba9a28b0789284217b0d1120446d43f` this project already resolves against — not merely "exists on vcpkg somewhere." Package name discovered via direct portfile read this session, not web search — see Package Legitimacy Audit. |
| Python 3 | 3.12.3 locally `[VERIFIED: local `python3 --version`]`; GitHub-hosted runners ship Python 3 by default | D-05's registry generator language | Build prerequisite, not a linked library — invoked from CMake via `find_package(Python3 COMPONENTS Interpreter REQUIRED)` + `add_custom_command` |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `json-schema-validator` (C++, build-time) | A Python `jsonschema`-based CI script | Since D-05 already makes Python 3 a build prerequisite, a Python CI validation script avoids adding a new vcpkg dependency at all — genuinely simpler if REPORT-01's schema validation only needs to run in CI, not inside the shipped binary or a fast local dev loop. **Not currently installed** (`pip3`/`python3 -c "import jsonschema"` both failed this session on the local dev machine) — if chosen, CI must `pip install jsonschema` itself; nothing in the repo currently provisions it. The C++ `json-schema-validator` route keeps validation entirely inside the CTest suite (no separate CI step, works identically for local devs), which better matches this project's "no separate parallel test system" convention (D-14's rejection of a CI script that audits test sources for the same reason). **Recommendation: prefer the C++ route** (`json-schema-validator` added to `vcpkg.json`) for consistency with the rest of the test suite, unless the planner judges the extra ~15-40s of FFmpeg-adjacent-free build time for one more small header-only-ish lib not worth it — it is not part of the FFmpeg build critical path either way. |
| Hand-rolled JSON schema validation (manual key/type checks) | `json-schema-validator` | Don't Hand-Roll: JSON Schema draft validation (`$ref`, `oneOf`, format keywords) is exactly the kind of "looks simple, has 40 edge cases" surface the project's own Don't-Hand-Roll philosophy (libebur128 for BS.1770, xxHash for hashing) already avoids elsewhere |

**Installation (if the C++ schema-validator route is chosen):**
```bash
# vcpkg.json: add "json-schema-validator" to the top-level dependencies array
# (test-only use is fine as a regular dependency — vcpkg has no dev-dependency
# concept; it will build for the mediadiff_unit_tests / mediadiff_integration_tests
# targets only, via target_link_libraries, same pattern as Catch2 today)
```

**Version verification:** All versions above were verified by reading the actual `vcpkg.json` port manifest files inside the pinned `vcpkg/` submodule (`git rev-parse HEAD` in that submodule = `105fdc246ba9a28b0789284217b0d1120446d43f`, matching `vcpkg.json`'s `builtin-baseline`), not by `npm view`-equivalent registry queries (vcpkg has no such live registry query tool; the pinned commit *is* the registry state for this project). This is the authoritative source for a vcpkg-manifest project — stronger than a live registry lookup, since it reflects exactly what this repo's next `cmake --preset` will resolve.

## Package Legitimacy Audit

> The standard npm/PyPI/crates legitimacy gate (`gsd-tools query package-legitimacy check --ecosystem <npm|pypi|crates>`) does not cover the vcpkg/C++ ecosystem this project uses — it returned a usage error when attempted against `--ecosystem cpp` this session. For vcpkg dependencies, the authoritative verification is direct inspection of the port manifest at the pinned `builtin-baseline` commit, which was done for every dependency below.

| Package | Registry | Age/Provenance | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| json-schema-validator | vcpkg (port present at pinned baseline `105fdc24...`) | Upstream `pboettch/json-schema-validator` on GitHub, MIT-licensed, wraps the already-trusted `nlohmann-json` | N/A (vcpkg has no download-count concept) | `github.com/pboettch/json-schema-validator` `[VERIFIED: vcpkg/ports/json-schema-validator/vcpkg.json:5]` — `"homepage": "https://github.com/pboettch/json-schema-validator"` | OK | Approved — recommended in Standard Stack §Alternatives, `[ASSUMED]` pending the planner's build-vs-Python-CI-script decision |

**Packages removed due to SLOP verdict:** none.
**Packages flagged as suspicious [SUS]:** none.

*`json-schema-validator`'s package name was discovered via direct `Read` of the pinned vcpkg submodule's port directory listing this session (not web search, not training-data recall), which is the strongest provenance available for a vcpkg dependency — equivalent to reading `npm view <pkg>` against a registry whose exact resolved state this project has already locked in. It is still tagged `[ASSUMED]` for *adoption* (not existence) because the choice to add it — versus the Python-`jsonschema` CI-script alternative — is a design decision for the planner/discuss-phase to confirm, not a fact this research can settle unilaterally.*

## Architecture Patterns

### System Architecture Diagram

```
                         ┌─────────────── cli/ (process control) ───────────────┐
                         │ CLI11 parse → Config capture → dispatch → render     │
argv ───────────────────▶│  (compare|snapshot|dir|inspect|list-checks|explain)  │───▶ stdout/exit code
                         │  TTY renderer lives HERE (fmt styling, ANSI)         │
                         └───────────────────────┬───────────────┬─────────────┘
                                                  │               │
                          (stub analyzer, test-only, D-11)        │ file paths / .snap.json
                                                  ▼               ▼
   ┌───────────────────────────── libmediadiff (no stdout, no exit — ENG-16) ─────────────────────────┐
   │                                                                                                    │
   │  config/        mediadiff.toml load ──▶ raw TOML sections                                          │
   │                     │                                                                               │
   │                     ▼                                                                               │
   │  core/          CheckRegistry (generated) ──▶ Policy merge: profile → [severity]/[tolerance]         │
   │                     │                          → [override.*] (file order) → CLI --set/--tol (argv)  │
   │                     │                                                                               │
   │                     ▼                                                                               │
   │  core/          Measurement{check_id, Value(variant<9>), evidence, scope} ──▶ Fingerprint            │
   │                  (from stub analyzer in Phase 2; real analyzers arrive Phase 3+)                     │
   │                     │                                                                               │
   │                     ├──▶ central serializer (D-08) ──▶ *.snap.json  (canonical, git-diffable)        │
   │                     │         ▲                                                                      │
   │                     │         └── snapshot read path (SNAP-02: candidate/baseline may BE a .snap.json)│
   │                     ▼                                                                               │
   │  compare/       Fingerprint × Fingerprint × Policy ──▶ semantics engine                              │
   │                  (exact | ±tol | set | presence | hash | dist | span)                                │
   │                     │                                                                               │
   │                     ▼                                                                               │
   │  core/          Finding[]{id, scope, status, severity, delta, evidence, hints[]}                     │
   │                     │                                                                               │
   │                     ▼                                                                               │
   │  report/        json · markdown · junit renderers (pure functions of Finding[])                     │
   │                                                                                                       │
   └───────────────────────────────────────────────────────────────────────────────────────────────────┘
                                                  │
                                                  ▼
                                          cli/ renders TTY, writes
                                          --json/--report files,
                                          maps Error.kind → exit code
```

### Recommended Project Structure

```
src/
├─ cli/
│  ├─ main.cpp                # existing entry point; extend with subcommands
│  ├─ commands/                # one file per subcommand (compare, snapshot, dir, inspect, list-checks, explain)
│  ├─ tty_render.{h,cpp}       # TTY renderer — fmt styling lives here, not report/
│  └─ exit_code.h              # Error::Kind → exit code mapping (the ONE place this translation happens)
├─ core/
│  ├─ checks.def               # X-macro-free data file the generator reads (D-01)
│  ├─ check_id.h               # GENERATED — do not hand-edit; enum + registry table
│  ├─ check_explain.cpp        # GENERATED — embedded --explain text per D-02
│  ├─ value.h                  # std::variant<9 alternatives> (D-06)
│  ├─ rational.h               # libav-free {int64, int64} POD (D-07)
│  ├─ measurement.h            # Measurement{check_id, Value, evidence, scope}
│  ├─ fingerprint.h            # Fingerprint = measurements[] + envelope
│  ├─ finding.h                # Finding{id, scope, status, severity, delta, ...}
│  ├─ policy.h / policy.cpp    # profile defaults + merge (ENG-06/07/11)
│  ├─ profiles.h               # 5 shipped profiles, normative matrix
│  ├─ tolerance.h / .cpp       # grammar parser: "5ms"|"3%"|"±8"|... (CLI-10)
│  ├─ glob.h / .cpp            # segment-wise `*`/`**` matcher, no regex (ENG-02)
│  ├─ serializer.h / .cpp      # D-08's ONE canonical writer
│  └─ error.h                  # Error{kind, ...} — kind ∈ usage|input_open|input_unsupported|decode|internal
├─ config/
│  └─ toml_load.{h,cpp}        # mediadiff.toml discovery + precedence merge input
├─ compare/
│  ├─ semantics.h               # dispatch table: semantic → comparator fn
│  ├─ exact.cpp / tol.cpp / set.cpp / presence.cpp / hash.cpp / dist.cpp / span.cpp
│  └─ engine.{h,cpp}            # Fingerprint × Fingerprint × Policy → Finding[]
├─ report/
│  ├─ json.{h,cpp}
│  ├─ markdown.{h,cpp}
│  └─ junit.{h,cpp}
└─ util/                        # unchanged from Phase 1 (expected.h, fs.h, version.h)
tools/
└─ gen_registry.py               # D-05's generator, invoked from CMake at configure/build time
docs/
├─ checks/<check.id>.md          # explain-doc sources (already scaffolded, empty)
└─ schema/report-1.0.json        # JSON schema for REPORT-01
tests/
├─ unit/                         # semantics table-driven, glob matcher, precedence merger,
│                                  snapshot round-trip, tolerance grammar
├─ integration/                  # spawns the real binary via cli_harness.h
├─ golden/                       # canned fingerprints → expected TTY/MD/JSON bytes (UPDATE_GOLDENS=1)
└─ fixtures/snapshots/           # hand-authored *.snap.json pairs per semantic × status (D-10, D-14)
```

### Pattern 1: Build-time registry generator (D-01/D-02/D-05)

**What:** A Python 3 script invoked as a CMake custom command reads `src/core/checks.def` (a data file — exact syntax is the planner's/generator-implementer's discretion per 02-CONTEXT.md, e.g. line-oriented DSL or embedded TOML/JSON), cross-references `docs/checks/<id>.md` for every declared ID, and emits: (1) a generated C++ header with the `CheckId` enum and the constexpr registry table, (2) a generated `.cpp` embedding each doc's contents as string literals for `--explain`, (3) fails the CMake configure/build step with a non-zero exit and a clear message if any registered ID is missing its `docs/checks/<id>.md`.

**When to use:** This is the single source of truth for ENG-01, DOC-01, DOC-02, and (transitively) ENG-13's `explain` command and the compile-time check-ID safety ENG-03/D-03 depend on.

**Example (CMake wiring shape — the generator script itself is `[ASSUMED]`, this pattern is standard CMake custom-command usage, not project-specific):**
```cmake
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/core")
set(CHECKS_DEF "${CMAKE_SOURCE_DIR}/src/core/checks.def")

add_custom_command(
  OUTPUT "${GENERATED_DIR}/check_id.h" "${GENERATED_DIR}/check_explain.cpp"
  COMMAND Python3::Interpreter
          "${CMAKE_SOURCE_DIR}/tools/gen_registry.py"
          --checks "${CHECKS_DEF}"
          --docs-dir "${CMAKE_SOURCE_DIR}/docs/checks"
          --out-dir "${GENERATED_DIR}"
  DEPENDS "${CHECKS_DEF}" "${CMAKE_SOURCE_DIR}/tools/gen_registry.py"
          # Note: docs/checks/*.md files should also be a DEPENDS glob or
          # equivalent so an added/removed doc re-triggers generation —
          # CMake globs don't auto-reconfigure on new files by default,
          # a known CMake footgun worth flagging for the plan.
  COMMENT "Generating check registry from checks.def"
  VERBATIM
)
add_custom_target(generate_registry DEPENDS "${GENERATED_DIR}/check_id.h")
add_dependencies(libmediadiff generate_registry)
```

### Pattern 2: CLI11 subcommand + implicit-compare + repeatable-option skeleton (ENG-16-compliant boundary)

**What:** `cli/main.cpp` already has `app.require_subcommand(0, 1)` set from Phase 1, anticipating this exact extension.

**Example (verified against the installed CLI11 2.6.2 headers this session — `App::add_subcommand`, `Option` binding to `std::vector<std::string>`, `CLI::ExistingFile` validator all confirmed present):**
```cpp
// Source: pattern per doc 00 §3.2, cross-checked against installed CLI11 2.6.2 headers
CLI::App app{"media-aware regression diff", "mediadiff"};
app.require_subcommand(0, 1);

auto* cmp = app.add_subcommand("compare", "Compare two artifacts");
// ... snapshot, dir, inspect, list-checks, explain similarly ...

// Implicit-compare positionals on the ROOT app (not a subcommand):
std::string baseline_path, candidate_path;
app.add_option("baseline", baseline_path);   // may be a media file OR a *.snap.json (SNAP-02)
app.add_option("candidate", candidate_path);

// Repeatable --set / --tol: bind to std::vector<std::string>, one entry per
// occurrence, IN ENCOUNTER ORDER — CLI11's default container-option behavior
// accumulates automatically; no explicit ->take_all() needed for a
// std::vector<T> target (take_all()/MultiOptionPolicy exists for a different
// case: binding one option to more than N expected tokens on a SCALAR).
std::vector<std::string> set_overrides, tol_overrides;
cmp->add_option("--set", set_overrides, "check.glob=severity (repeatable)");
cmp->add_option("--tol", tol_overrides, "check=value (repeatable)");
// The config-precedence merger (ENG-06/ENG-11) must then walk
// set_overrides/tol_overrides IN THE ORDER CLI11 POPULATED THEM (argv order)
// — this is where "later flags override earlier ones in argv order" (CLI-03)
// actually gets implemented; CLI11 only guarantees the accumulation order,
// not the override semantics, which are core/'s job.
```

### Pattern 3: Exit-code translation — the ONE place CLI11's own error codes must NOT be trusted

**What:** `CLI11_PARSE`'s macro body (verified this session, `CLI/App.hpp`) is:
```cpp
// Source: vcpkg/packages/cli11_x64-linux/include/CLI/App.hpp:40-45 (verified this session)
#define CLI11_PARSE(app, ...)                                                                                          \
    try {                                                                                                              \
        (app).parse(__VA_ARGS__);                                                                                      \
    } catch(const CLI::ParseError &e) {                                                                                \
        return (app).exit(e);                                                                                          \
    }
```
`App::exit(e)` returns a value from `CLI::ExitCodes` (`vcpkg/packages/cli11_x64-linux/include/CLI/Error.hpp:44-62`, verified this session): `Success=0`, then `IncorrectConstruction=100` through `ArgumentMismatch=115`, `BaseClass=127`. **None of these match mediadiff's required contract (usage→64).** The current Phase-1 `cli/main.cpp` uses `CLI11_PARSE` directly and returns `app.exit(e)` unmodified — this is correct for Phase 1 (no exit-code contract existed yet), but Phase 2 tasks implementing CLI-06/CLI-10 must NOT keep this as-is.

**How to avoid the pitfall:** Replace the bare `CLI11_PARSE` macro usage with an explicit `try { app.parse(...); } catch (const CLI::ParseError& e) { return 64; }` (or a thin wrapper that always returns 64 for any `CLI::ParseError`, since every CLI11-level parse failure — bad option, missing required arg, bad conversion — is definitionally a usage error, not one of mediadiff's other four `Error::Kind`s). CLI-10's "wrong-unit tolerance... exit 64 naming the expected unit" additionally needs the tolerance-grammar parser (in `core/`, not CLI11's validator layer) to raise its own diagnostic message, since CLI11's built-in validators won't know the check-specific expected unit.

### Pattern 4: `fmt`'s color/styling API for CLI-08

**What:** `fmt/color.h` is present (`vcpkg/packages/fmt_x64-linux/include/fmt/color.h`, verified this session — `class text_style`, `enum class emphasis`, `class ansi_color_escape` all confirmed) and is a separate include from `fmt/core.h`/`fmt/format.h`.

**Example:**
```cpp
// Source: fmt 12.2.0 fmt/color.h (headers verified present this session)
#include <fmt/color.h>
if (color_enabled) {
  fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "FAIL");
} else {
  fmt::print("FAIL");
}
```
The `--ascii` swap (✓⚠✗ℹ → `OK WARN FAIL INFO`) and the `NO_COLOR`/`isatty`/`CI=true`/`GITHUB_ACTIONS=true` decision tree (doc 00 §3.2) are both plain conditionals gating whether `fmt::emphasis`/`fmt::fg` calls happen at all — no special library support needed beyond what's already linked.

### Pattern 5: nlohmann `ordered_json` for canonical field order

**What:** `nlohmann::ordered_json` (confirmed: `using ordered_json = basic_json<nlohmann::ordered_map>;`) preserves object-key insertion order rather than the default `basic_json`'s alphabetical/`std::map`-backed order. This is the type D-08's central serializer builds on for both `*.snap.json` and `--json` output, since "registry field order" (SNAP-03) requires the serializer to control key order explicitly rather than have it re-sorted underneath it.

**Example:**
```cpp
// Source: nlohmann-json 3.12.0, verified installed at vcpkg/packages/nlohmann-json_x64-linux
nlohmann::ordered_json j;
j["schema_version"] = "1.0";   // insertion order preserved on dump()
j["tool_version"] = tool_version;
// ... central serializer controls exactly this insertion sequence per
// registry order, per D-08 — do NOT rely on nlohmann's own dump() for
// FLOAT formatting though (see Common Pitfalls — to_chars availability)
```

### Anti-Patterns to Avoid

- **Trusting `App::exit()`'s return value as mediadiff's exit code.** See Pattern 3 — it is in the wrong numeric range entirely.
- **Letting `core/`/`compare/`/`report/` write to `stdout`/`stderr` "just for debugging."** `scripts/lint_eng16.sh` already scans these directories and will fail CI on `fprintf(stderr, ...)`, `std::cout`, etc. — verified present in the repo this session, not a future addition.
- **Reimplementing float formatting via `snprintf("%g", ...)` or similar instead of `std::to_chars`.** Breaks SNAP-03's "shortest round-trip" requirement and TRUST-05's byte-identity guarantee (rounding/precision differences across libc implementations on different platforms).
- **A registry field encoding test-fixture requirements (D-14's rejected alternative).** Bakes test metadata into the shipped product; the discipline must live in the test suite / CI harness, not `checks.def`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing/serialization | Custom JSON writer | `nlohmann::ordered_json` (already linked) | Insertion-order preservation is exactly what canonical output needs; a hand-rolled writer reintroduces the "nine independent serializers" risk D-08 explicitly rejects |
| JSON Schema validation (REPORT-01) | Manual key/type/required-field checks | `json-schema-validator` (pboettch, verified present at pinned baseline) or a Python `jsonschema` CI script | Schema validation has deep edge cases (`$ref`, `oneOf`, format keywords); a hand-rolled subset checker will drift from the actual schema silently |
| TOML parsing | Custom `.toml` reader | tomlplusplus (already linked) | TOML 1.0 has real edge cases (multi-line strings, dotted keys, array-of-tables) that a "just enough" parser will get wrong on some future user's config file |
| CLI argument parsing, subcommands, repeatable options | Hand-rolled argv loop | CLI11 (already linked) | doc 00 §3.2's own stated reason: "subcommand + repeatable-flag matrix is exactly where hand-rolling rots" |
| `expected<T,E>`-style error propagation | Hand-rolled tagged union / `std::optional<T>` + separate error out-param | `mediadiff::expected<T,E>` (already aliased over tl-expected) | Already the established Phase 1 pattern; core/ call sites should not invent a second error-propagation idiom |
| Float shortest-round-trip formatting | `snprintf`/`ostringstream` with a fixed precision | `std::to_chars` (stdlib, C++17 `<charconv>`) | Guarantees shortest string that round-trips exactly — a fixed-precision format either loses precision or produces non-minimal output that diffs noisily in git |
| Segment-wise glob matching | `std::regex` | A small hand-written segment matcher (ENG-02 explicitly forbids regex) | Locked decision: "no regex" — likely because check IDs are dotted-segment and a regex implementation invites `.`-escaping bugs and ReDoS-adjacent complexity for a problem that's naturally a simple split-and-compare |

**Key insight:** Every "don't hand-roll" item above already has its library linked and built as of Phase 1's CMakeLists.txt — this phase is assembly and design, not dependency acquisition. The one place hand-rolling is explicitly *required* (glob matcher, no regex) is a locked decision, not an oversight.

## Common Pitfalls

### Pitfall 1: CLI11's own exit codes are not mediadiff's exit codes

**What goes wrong:** `CLI11_PARSE`'s catch block returns `app.exit(e)`, which yields a value in `CLI::ExitCodes` (100–127 range, verified this session). If a task implements CLI-06/CLI-10 by leaving the Phase-1 `CLI11_PARSE(app, argc, argv); return 0;` pattern unchanged, every CLI11-level parse failure will exit with e.g. `109` (`RequiredError`) instead of the required `64`.

**Why it happens:** `CLI11_PARSE` is the "obvious" macro to reach for and it compiles and "works" (returns non-zero on failure) — the bug is silent because nothing asserts the *specific* exit code value in a quick smoke test; it only surfaces when CI-06/CLI-10's own acceptance test asserts `== 64` exactly.

**How to avoid:** Replace `CLI11_PARSE` with an explicit try/catch that maps every `CLI::ParseError` to `64`, or write a thin `mediadiff_cli_parse()` wrapper used everywhere instead of the macro.

**Warning signs:** An integration test asserting `exit_code == 64` for a bad flag passes with `exit_code == 109` unless the test is actually checking the literal value (a loose `!= 0` assertion would hide this).

### Pitfall 2: Floating-point `std::to_chars` has a real Apple-platform availability floor, separate from Xcode version

**What goes wrong:** D-08 requires `std::to_chars` for shortest-round-trip float formatting in the canonical serializer (SNAP-03, TRUST-05). libc++'s floating-point `to_chars`/`from_chars` overloads carry Apple's own "strict" availability annotation gated on a **macOS 13.3 deployment target**, independent of which Xcode/AppleClang version is compiling — this is a real, documented constraint (LLVM review `D74626`, Apple developer forums), not a hypothetical. If a future CMake change sets `CMAKE_OSX_DEPLOYMENT_TARGET` below 13.3 (e.g. for broader backward-compat), calling the float overload of `std::to_chars` becomes a **hard compile error** on macOS specifically, while compiling fine on Linux/Windows — a toolchain-specific failure this project's "toolchain parity" goal is designed to avoid.

**Why it happens:** Deployment-target minimums are usually chosen for runtime-OS-support reasons unrelated to standard-library internals; nobody connects "we want to support macOS 12" to "this breaks our canonical float serializer" without already knowing this specific availability gate exists.

**How to avoid:** Currently `CMakeLists.txt`/`CMakePresets.json` set **no** `CMAKE_OSX_DEPLOYMENT_TARGET` `[VERIFIED: grep across CMakeLists.txt, CMakePresets.json, .github/workflows/ci.yml this session returned no matches]` — this is the safe state (defaults to the SDK's own version, well above 13.3 on any Xcode-15-or-newer runner). The action item is preventative, not corrective: **do not add an explicit `CMAKE_OSX_DEPLOYMENT_TARGET` below 13.3** without first confirming `std::to_chars`'s floating-point overloads still compile, and document this constraint next to the serializer's `std::to_chars` call site so a future contributor doesn't hit it blind.

**Warning signs:** A macOS-only build failure mentioning `to_chars`/`from_chars` availability after an unrelated "lower our minimum macOS support" change.

### Pitfall 3: The Markdown report cap must be a *byte* budget under real margin, not a "60 KB" hand-wave

**What goes wrong (research: PITFALLS.md Pitfall 15, corrects doc 01 §9's own "60 KB" text):** GitHub's PR/issue comment API hard-limits body length to 65,536 **characters**. "60 KB" is ambiguous (1000 vs 1024, bytes vs characters) and, more importantly, non-ASCII content (Unicode symbols like ✓⚠✗ℹ if accidentally used in Markdown output instead of being TTY-only, or non-ASCII filenames surfaced in evidence) inflates UTF-8 *byte* count relative to *character* count — a naive byte-counted 60,000-byte cap is actually safe (well under 65,536 characters even with multi-byte inflation), but only if it's implemented as bytes counted correctly, not as "characters of a UTF-8 buffer" conflated with bytes.

**Why it happens:** "60 KB reads as safely under 65,536" at a glance, but the exact counting method matters and this doc's own text already has the ambiguity baked in (REPORT-05 in REQUIREMENTS.md exists specifically to correct this).

**How to avoid:** Implement the cap as an explicit byte-counted budget (e.g. 60,000 bytes, matching the pattern mature PR-comment GitHub Actions use — headroom of ~5,500 bytes below the real 65,536-character limit), and add a golden test with a synthetically oversized finding set asserting the renderer truncates before the real GitHub limit.

**Warning signs:** A Markdown report that passes mediadiff's internal cap but is rejected by a CI platform's comment-posting step with "body too long."

### Pitfall 4: `deprecated_alias` needs family/namespace context, not a bare `old→new` string pair (ENG-03)

**What goes wrong (research: ARCHITECTURE.md §3, citing a real ESLint bug):** ESLint's original deprecation metadata format was a bare string replacement, ambiguous about whether the replacement rule lived in the same plugin, a different plugin, or ESLint core — a real, documented pain point that forced a breaking schema revision. mediadiff's `deprecated_alias("container.faststart", "container.mp4.faststart")` risks the identical ambiguity if a future rename crosses namespaces (e.g. a check moving from `container.*` to `meta.*`) and the alias mechanism only stores two bare dotted strings.

**Why it happens:** A simple `old→new` map looks sufficient until the first cross-family rename, at which point "which registry owns `new`" becomes genuinely ambiguous if the alias table doesn't record it explicitly.

**How to avoid:** Design the alias record to carry enough structure to resolve unambiguously even across a namespace change — at minimum, validate at generator time that `new` exists as a currently-registered ID (catching a dangling/renamed-again alias at build time, not at a user's runtime `--set` parse).

**Warning signs:** A `deprecated_alias` entry pointing to an ID that no longer exists in the registry — should be a build failure, not a silent runtime no-op.

### Pitfall 5: The fail-first discipline (D-14–D-16) is infrastructure, not a testing afterthought

**What goes wrong:** Phase 1's verification report (`01-VERIFICATION.md`, Resolution section, read this session) documents **six** checks found structurally incapable of observing their own subject — two ctest filters matching zero tests while reporting green, a PowerShell negative-path check that aborted before its own assertion, a zero-test guard that died before its diagnostic, an ENG-16 lint that couldn't match the commonest violation, and a test-count guard that undercounted (and would have failed the build spuriously below ten tests). This is the single most expensive lesson from Phase 1, explicitly cited in 02-CONTEXT.md as the reason D-14/D-15/D-16 exist.

**Why it happens:** A "does this test suite actually exercise what it claims to" property is invisible from a green CI run alone — it requires either a canary that must fail, or an explicit non-zero-count assertion, neither of which is the default behavior of any test runner.

**How to avoid:** Build D-16's permanent canary fixture and D-15's "every semantic × every status" fixture matrix as Wave-1 infrastructure, before analyzer-adjacent work begins — not as a checklist item verified once at the end. Concretely: a CI step (or CTest test) that fails if the canary fixture reports anything other than its one expected `fail`; and a coverage assertion (could be a small script, itself audited per D-14's own reasoning against being an unobservable check) that every one of the seven semantics has at least one fixture reaching each of `pass|info|warn|fail|skipped|error` it can legitimately emit.

**Warning signs:** A new semantic or check ships with only a "happy path" fixture; a CI green run with a test count that looks suspiciously round or suspiciously low for the number of features shipped.

## Code Examples

### Registry ID usage at analyzer call sites (D-03) — illustrative shape, not the generator's exact output

```cpp
// Source: derived from doc 01 §2/§3 + D-03 (call-site convention); the exact
// generated symbol names are the generator's own design (Claude's discretion)
switch (measurement.check_id) {
  case CheckId::video_color_range:   // compile error if this ID doesn't exist —
                                       // exactly the "loud, not invisible" property
                                       // D-03 exists to guarantee
    // ...
}
```

### Tolerance grammar parsing (ENG-05, CLI-10)

```cpp
// Source: derived from doc 01 §3's tolerance grammar table
// "5ms" | "3%" | "±8" | "2frames" | "0.2ms/min" | "0.5LU" | "1.0dB" | "128samples" | "1tick"
// Parsed ONCE, unit-checked against CheckDef.unit. A mismatch is exit 64,
// naming the expected unit (CLI-10) — this parser lives in core/, is
// unit-testable without CLI11 or any file I/O, and is exactly the kind of
// pure function D-14's fixture-pair discipline applies to per-branch.
mediadiff::expected<Tolerance, Error> parse_tolerance(std::string_view raw, Unit expected_unit);
```

### Worker pool shape for `dir --threads N` (DIR-05)

```cpp
// Source: pattern per ARCHITECTURE.md §6 — "fixed-size pool pulling from a
// shared deterministically-sorted queue" is the standard shape for bounded
// batch parallelism; per-file analysis itself stays single-threaded/synchronous.
// Peak memory ≈ threads × per-file-peak (DIR-06's memory-bounding follow-up
// lands in Phase 3 once PacketScan's packet-array size is real — Phase 2
// only needs to deliver the POOL BOUND itself, per doc 01 §10's DIR-05 text).
std::vector<std::string> sorted_paths = /* deterministic sort, DIR-04 */;
ThreadPool pool(thread_count);
for (auto& path : sorted_paths) {
  pool.submit([path] { return analyze_one_file(path); });  // single-file-synchronous
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `CLI11_PARSE` macro trusted for exit codes | Explicit `CLI::ParseError` catch mapping to project-specific exit codes | N/A — always been necessary for any CLI11 project with a custom exit-code contract; not a version-driven change | See Pitfall 1 |
| `%g`/fixed-precision float formatting | `std::to_chars` shortest-round-trip (C++17 `<charconv>`) | Available since C++17, but AppleClang/libc++'s *floating-point* overloads only became fully available relatively recently (deployment-target-gated to macOS 13.3) | See Pitfall 2 |
| GitHub annotation-based CI reporting | Markdown + JUnit as primary CI surface | N/A — GitHub's 10/50 annotation caps are a long-standing platform limit, not a recent change | Already correctly avoided per PROJECT.md's Out of Scope list |

**Deprecated/outdated:** None specific to this phase's dependency set — all pinned library versions (CLI11 2.6.2, fmt 12.2.0, nlohmann-json 3.12.0, tomlplusplus 3.4.0, tl-expected 1.3.1, Catch2 3.15.3) are current as of Phase 1's own STACK.md research and unchanged since.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `checks.def`'s exact syntax (X-macro-free data file vs. some other DSL) is Claude's discretion per 02-CONTEXT.md, not a specified format | Architecture Patterns §Registry generator, §Project Structure | Low — explicitly delegated to the planner/implementer by the locked decisions themselves |
| A2 | The glob matcher's exact algorithm (split-on-`.`, compare segments, `**` matches ≥1 trailing segments) is a reasonable implementation of ENG-02's spec, not verified against an existing mediadiff implementation (none exists yet) | Don't Hand-Roll; Project Structure | Low — the spec (doc 01 §2) is precise enough that any correct implementation converges; risk is implementation bugs, not design ambiguity |
| A3 | `json-schema-validator` (vs. a Python `jsonschema` CI script) is the *recommended* choice for REPORT-01, but this is a design tradeoff, not a settled fact | Standard Stack §Alternatives Considered | Medium — if the planner picks the Python-script route instead, the `vcpkg.json` addition doesn't happen; low risk either way since both are described |
| A4 | `CMAKE_OSX_DEPLOYMENT_TARGET`'s current absence is safe (defaults to SDK version, above 13.3) — inferred from general CMake/Clang deployment-target defaulting behavior, not verified against this project's actual macOS CI build log for the specific SDK version resolved | Common Pitfalls §Pitfall 2 | Medium — if wrong, `std::to_chars` float overloads could already be broken on the `arm64-osx`/`x64-osx` CI legs; recommend the planner add a quick compile-smoke-test of `std::to_chars(buf, buf+N, 3.14)` on macOS CI early in Wave 1 to settle this definitively before D-08's serializer is built on top of it |
| A5 | The generator (`tools/gen_registry.py`) is invoked via `add_custom_command`/`add_custom_target` at CMake configure-or-build time; the exact CMake wiring shown is a standard pattern, not verified against any mediadiff-specific constraint beyond "Python 3 must be found" | Architecture Patterns §Pattern 1 | Low — this is boilerplate CMake, well-precedented; the DEPENDS-glob footgun noted inline is a real, general CMake gotcha worth flagging regardless |

**If this table is empty:** N/A — see entries above; all are genuinely open implementation-detail assumptions, not compliance/security/retention-policy claims.

## Open Questions

1. **TRUST-08's cross-release idempotence bootstrap problem.**
   - What we know: The requirement is to compare the current build against a snapshot taken by the *previous release*, and Phase 2 is the first phase where any release exists.
   - What's unclear: How the check behaves on the very first release (no prior snapshot exists yet) without becoming a check that silently passes when it has nothing to compare against — which D-14's own philosophy treats as exactly the unobservable-check pattern to avoid.
   - Recommendation: The planner should design this as `skipped:no_prior_release` (an explicit, machine-readable, non-`pass` status) on the bootstrap run, converting to a real comparison starting with the second release. This reuses the existing `skipped ≠ pass` machinery (ENG-14) rather than inventing new bootstrap-specific logic — flag as a discuss-phase-worthy design choice, not settled by this research alone.

2. **What `Error` carries beyond `kind`.**
   - What we know: `Error.kind ∈ usage | input_open | input_unsupported | decode | internal` maps to exit codes 64/65/65/66/70 (doc 01 §11).
   - What's unclear: Whether `Error` carries a human-readable message, a cause chain (for `?`-style propagation through several `expected` layers), or a source location — none of doc 00/01 specifies this.
   - Recommendation: At minimum a `std::string message` field is needed for CLI-10's "names the expected unit" requirement and for any usage-error diagnostic text — this should be a small, early Wave-1 design decision (the `Error` type's shape), since `core/`'s entire public API surface depends on it and every later call site constructs one.

3. **Whether the four report formats share an intermediate representation.**
   - What we know: All four (`tty`, `json`, `markdown`, `junit`) are documented as pure functions of `Finding[]` (ARCHITECTURE.md §5, confirmed correct pattern).
   - What's unclear: Whether there's an additional shared "grouped/sorted/summarized" intermediate step (e.g. a `ReportModel` that groups findings by container→video→timeline→...→meta order and computes summary counts once) that all four renderers consume, versus each renderer independently re-deriving the same grouping/sorting logic.
   - Recommendation: A shared intermediate (computed once from `Finding[]`) avoids four independent implementations of "group in fixed order, non-pass-by-default filtering, accept/tune/silence lookup" silently drifting — directly analogous to D-08's "one central serializer" rationale, applied to the read side. Recommend the planner adopt this shape explicitly rather than leave it implicit.

4. **How `list-checks --effective` shows policy provenance (ENG-06/ENG-12).**
   - What we know: The resolved severity chain must be visible under `-v` (ENG-06), and `--effective` must "show exactly the policy that was applied" (ROADMAP criterion 2).
   - What's unclear: Whether the merge function itself needs to retain per-value provenance (which layer — built-in/profile/config/CLI — set the *final* value, and optionally what each intermediate layer's value was) as structured data, or whether `-v` can reconstruct this by re-running the merge with tracing enabled.
   - Recommendation: Retaining provenance as structured data alongside the merged value (e.g. `{value, source_layer, source_detail}` per resolved field) is simpler to test and more honest than a separate "replay with tracing" code path that could drift from the real merge logic — recommend this shape, but this is a genuine design decision for the planner, not a fact this research settles.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Python 3 | D-05's registry generator | ✓ `[VERIFIED: local python3 --version]` | 3.12.3 locally; GitHub-hosted CI images ship Python 3 by default | — |
| CMake ≥ 3.25 | Build system (unchanged from Phase 1) | ✓ `[VERIFIED: local cmake --version]` | 3.28.3 locally | — |
| json-schema-validator (vcpkg) | REPORT-01 (if C++ route chosen) | ✓ present in pinned vcpkg baseline `[VERIFIED: vcpkg/ports/json-schema-validator/vcpkg.json]`, not yet added to `vcpkg.json`'s dependency list | 2.4.0 | Python `jsonschema` CI-script route (not installed locally; would need `pip install jsonschema` in CI) — see Standard Stack §Alternatives |
| No FFmpeg / media dependency needed | This phase's explicit boundary excludes `DemuxSession`/`PacketScan`/any libav call beyond what Phase 1 already links for `--version` | N/A | — | — |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** `json-schema-validator` is not yet in `vcpkg.json` — a one-line manifest addition if the C++ route is chosen; the Python `jsonschema` fallback needs `pip install jsonschema` provisioned in CI if chosen instead.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Catch2 3.15.3 `[VERIFIED: vcpkg/ports/catch2/vcpkg.json:3]`, driven via CTest (`include(CTest)` + `include(Catch)` + `catch_discover_tests`) |
| Config file | `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` (both exist, verified read this session) |
| Quick run command | `ctest --test-dir build/x64-linux -R unit` (unit suite only — `TEST_PREFIX "unit."` already established `[VERIFIED: tests/unit/CMakeLists.txt:19]`) |
| Full suite command | `ctest --test-dir build/x64-linux --output-on-failure` (mirrors `.github/workflows/ci.yml`'s existing invocation pattern, verified this session) |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| ENG-04 | All seven comparison semantics, table-driven | unit | `ctest -R unit.compare_semantics -x` (name illustrative — planner assigns) | ❌ Wave 0 |
| ENG-02 | Glob matcher `*`/`**`, segment-wise | unit | `ctest -R unit.glob_matcher` | ❌ Wave 0 |
| ENG-06/ENG-11 | Precedence merger, last-writer-wins under permutation | unit (property-style per doc 01 §12) | `ctest -R unit.policy_merge` | ❌ Wave 0 |
| SNAP-03/TRUST-05 | Snapshot round-trip byte-identity, `--json` determinism | unit + integration | `ctest -R unit.snapshot_roundtrip` / `ctest -R integration.compare_twice` | ❌ Wave 0 |
| REPORT-01 | JSON schema validation | integration (or CI-level Python script per the alternative route) | `ctest -R integration.json_schema` | ❌ Wave 0 |
| REPORT-02/04/06 | Golden TTY/MD/JUnit bytes | golden | `UPDATE_GOLDENS=1 ctest -R golden` to refresh, plain `ctest -R golden` to check | ❌ Wave 0 |
| DIR-01…05 | `dir` orchestration, pairing, ordering, thread bound | integration | `ctest -R integration.dir_mode` | ❌ Wave 0 |
| CLI-06/CLI-10 | Exit code contract, wrong-unit tolerance | integration (via `cli_harness.h`, already exists) | `ctest -R integration.exit_codes` | ❌ Wave 0 |
| D-14/D-15/D-16 | Fail-first discipline itself | integration/CI-level | A coverage-assertion step (semantic × status matrix) + the permanent canary fixture | ❌ Wave 0 — this is Phase 2's own meta-infrastructure, build it first |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build/x64-linux -R unit`
- **Per wave merge:** `ctest --test-dir build/x64-linux --output-on-failure` (full suite)
- **Phase gate:** Full suite green before `/gsd-verify-work`, plus the D-16 canary fixture explicitly confirmed still reporting its one expected failure (a green suite where the canary silently started passing would be the exact Phase-1-repeat this phase's own discipline exists to prevent)

### Wave 0 Gaps

- [ ] `tests/unit/test_registry_generator.cpp` (or equivalent) — covers ENG-01/DOC-01's build-failure-on-missing-doc behavior; likely needs to be a CMake-configure-time test or a separate small CTest invoking the generator script directly against a fixture `checks.def`, since the *real* `checks.def` growing over Phases 3–7 shouldn't itself gate Phase 2's own tests
- [ ] `tests/unit/test_value_variant.cpp` — exhaustiveness via `std::visit`, covering all nine `Value` alternatives (D-06)
- [ ] `tests/unit/test_serializer.cpp` — the D-08 central serializer's float-formatting and field-order guarantees, including a specific `std::to_chars` smoke assertion (see Assumption A4)
- [ ] `tests/fixtures/snapshots/` — hand-authored `.snap.json` pairs, one triggering + one clean per semantic (D-10/D-14); none exist yet beyond the empty `tests/fixtures/` directory and its `GENERATOR_MANIFEST.json`
- [ ] `tests/golden/` directory and `UPDATE_GOLDENS` env-var wiring — does not exist yet
- [ ] Framework install: none needed — Catch2 already installed and wired from Phase 1

## Security Domain

### Applicable ASVS Categories

mediadiff is a fully local, offline CLI tool with no network listener, no authentication, no session/account concept, and no server-side attack surface — most ASVS categories (V2 Authentication, V3 Session Management, V4 Access Control) do not apply. The categories that do apply are input-validation and file-handling adjacent, given this phase parses untrusted CLI arguments and an untrusted `mediadiff.toml`.

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A — no accounts, no network surface |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A — single local user, operates on files the invoking user already has OS-level permission to read |
| V5 Input Validation | Yes | Tolerance-grammar parser (fail loud on malformed input → exit 64, never silently coerce, per D-09's own "mismatch is an error, never a coercion" spirit applied to config too); TOML parsing via tomlplusplus (a vetted parser, not hand-rolled — see Don't Hand-Roll); glob matcher explicitly forbids regex (ENG-02), which also sidesteps ReDoS as a class entirely for this input surface |
| V6 Cryptography | No | Not applicable to this phase — xxHash (non-cryptographic, threat model is accidental collision not adversarial per PROJECT.md's own stated rationale) is a Phase 3+ concern (content hashing), out of Phase 2's stub-measurement scope |
| V12 File and Resources | Yes | `util/fs.h`'s `fopen_utf8` shim (already built, Phase 1) is the only file-opening path for config/snapshot/report I/O; `SNAP-07`'s CI-safe-write gate (refuse overwrite without `--force`) is itself a resource-integrity control, not just a UX nicety — it prevents an automated/CI-triggered accidental clobber of a tracked baseline file |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed/oversized `mediadiff.toml` causing a parser crash or resource exhaustion | Denial of Service | Rely on tomlplusplus's own parser robustness (a maintained, widely-used library) rather than a hand-rolled parser; the config-precedence merger should still validate the *shape* it expects after TOML parsing succeeds (e.g. reject a `[severity]` table entry with a non-string value gracefully, as a `usage` `Error`, not a crash) |
| A crafted `--set`/`--tol` argv value designed to break the glob matcher or tolerance parser | Tampering (of the tool's own policy resolution) | No regex (ENG-02, already locked) removes ReDoS as a class; the tolerance grammar parser (ENG-05/CLI-10) must reject unrecognized input with a clean `usage` error rather than undefined behavior — this is exactly what D-14's fixture-pair discipline should cover per parser branch |
| Path traversal via `--config`, snapshot paths, or `--report kind=path` | Information Disclosure / Tampering | Not a meaningful threat for a local CLI tool operating with the invoking user's own OS permissions on paths the user explicitly supplies — this is expected functionality (the user is trusted to name their own files), not a boundary crossing; no special mitigation needed beyond `fopen_utf8`'s existing fail-loud-on-invalid-UTF-8 behavior (already built, Phase 1) |
| A snapshot file crafted by a third party (e.g. downloaded from an untrusted source) with a hostile `schema_version` or malformed envelope, fed to `compare` | Tampering | SNAP-05's own requirement (refuse incompatible `schema_version` major with exit 65) already covers the structural case; the JSON parser (nlohmann-json) itself is a maintained library not expected to have exploitable parsing bugs for well-formed-but-hostile-content JSON — no additional mitigation needed beyond what SNAP-05 already specifies |

## Sources

### Primary (HIGH confidence — verified this session via direct file reads against the pinned repository/submodule state)

- `vcpkg/ports/{cli11,tomlplusplus,nlohmann-json,tl-expected,catch2,json-schema-validator}/vcpkg.json` — exact pinned versions at `builtin-baseline` commit `105fdc246ba9a28b0789284217b0d1120446d43f`
- `vcpkg/packages/{cli11,fmt,nlohmann-json,catch2,libebur128}_x64-linux/include/...` — installed headers confirming API surface (`CLI11_PARSE` macro body, `CLI::ExitCodes`, `MultiOptionPolicy`, `fmt::text_style`/`emphasis`, `nlohmann::ordered_json`)
- `CMakeLists.txt`, `CMakePresets.json`, `.github/workflows/ci.yml` — confirmed no `CMAKE_OSX_DEPLOYMENT_TARGET` is set; confirmed existing test-suite `TEST_PREFIX` convention and `ctest -N`/`Total Tests:` parsing pattern
- `src/util/expected.h`, `src/util/fs.h`, `src/util/version.h`, `src/cli/main.cpp`, `scripts/lint_eng16.sh` — existing Phase 1 code this phase extends
- `.planning/phases/01-foundation-toolchain/01-VERIFICATION.md` (Resolution section) — six-unobservable-checks pattern, directly informing Pitfall 5
- Local environment: `python3 --version` (3.12.3), `cmake --version` (3.28.3)

### Secondary (MEDIUM confidence — project research docs, themselves web-research-derived and cross-checked)

- `.planning/research/ARCHITECTURE.md` §3 (registry precedent survey), §4 (lib/CLI split precedent), §5 (component boundaries), §6 (parallelism/memory)
- `.planning/research/PITFALLS.md` Pitfalls 1, 2, 15, 16, 17
- `.planning/research/FEATURES.md` (snapshot-testing CI-safe-write gap, feature dependency graph)
- `claude_docs/00-design-and-requirements.md`, `claude_docs/01-core-concepts.md` — the phase's own normative source docs

### Tertiary (LOW confidence — single web search, cross-referenced against LLVM's own review system but not independently re-verified against Apple's current documentation)

- WebSearch: "libc++ std::to_chars floating point support Xcode 15 AppleClang" — LLVM review `D74626` and Apple developer forum threads on `to_chars`/`from_chars` availability gating; treated as MEDIUM given LLVM's own code review is a primary source, but flagged LOW-adjacent since Apple's exact current-SDK behavior was not independently re-confirmed against a live macOS toolchain this session (no macOS host available in this environment)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — every version/API claim verified against the actual pinned vcpkg submodule and installed headers this session, not recalled from training data
- Architecture: MEDIUM — the compare/policy/report/dir design is fully specified in the project's own source docs (doc 00/01) and cross-checked against `.planning/research/ARCHITECTURE.md`'s external precedent survey; the generator's internal implementation shape is explicitly Claude's-discretion, not a verified fact
- Pitfalls: HIGH for the two newly-surfaced findings (CLI11 exit codes, `std::to_chars` availability — both verified via direct header inspection / cross-checked web research), MEDIUM for the pitfalls carried forward from `.planning/research/PITFALLS.md` (already MEDIUM-confidence per that document's own classification)

**Research date:** 2026-08-15
**Valid until:** 30 days (stable C++ library ecosystem; no fast-moving dependency in this phase's scope) — re-verify sooner if the `builtin-baseline` commit in `vcpkg.json` is bumped before planning begins
