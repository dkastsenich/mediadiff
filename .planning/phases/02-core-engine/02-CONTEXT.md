# Phase 2: Core Engine - Context

**Gathered:** 2026-08-15
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 2 delivers the complete compare engine — check registry, the seven comparison semantics, policy resolution, snapshot I/O, all four report formats and `dir` orchestration — working end to end against **stub measurements**, so every analyzer in Phases 3–7 plugs into finished machinery rather than co-evolving with it.

**In scope:** `src/core/` (registry, fingerprint, profiles, tolerance, finding), `src/config/` (TOML load and precedence merge), `src/compare/` (the semantics engine), `src/report/` (json, markdown, junit; tty lives in `cli/`), and the CLI surface beyond `--version` — subcommands, repeatable options, report flags, exit codes, colour policy.

**Out of scope:** all real media parsing. No `DemuxSession`, no `PacketScan`, no libav calls beyond what Phase 1 already links for `--version`. The probe layer begins in Phase 3. If a task in this phase needs to open a media file, it has escaped the boundary.

**Requirements (48):** CLI-01/02/03/04/06/07/08/10 · ENG-01…16 · SNAP-01…07 · REPORT-01…07 · DIR-01…05 · TRUST-03/05/08 · DOC-01/02

**Source doc:** `claude_docs/01-core-concepts.md` (ROADMAP Phase 2 = design-doc phase 1).

</domain>

<decisions>
## Implementation Decisions

### Check registry

- **D-01: Generate the registry from checks.def with a build-time script.** — **Reversibility:** costly — every check added in Phases 3–7 is declared through this mechanism, and changing it later means rewriting `checks.def`, the generator, and potentially every call site.

  One generator reads `src/core/checks.def` and emits the ID enum, the registry table, the embedded `--explain` documents and the docs manifest. The deciding argument: doc 00 §7 already requires `docs/checks/<id>.md` to be *compiled into the binary*, so a build-time generation step has to exist regardless. Reusing it for the registry means one generator rather than two parallel systems that can drift.

  X-macro and a `constexpr` table were both considered — doc 01 §2 explicitly left the choice open. X-macro was rejected for IDE/debugger hostility and degrading error messages across ~60 checks and six phases; the constexpr table for the enum-vs-array drift problem it reintroduces.

- **D-02: The generator writes the `--explain` documents into a C++ translation unit, and fails the build when a registered ID has no matching file.** — **Reversibility:** reversible.

  This is what makes ENG-01's "no undocumented checks" a genuine build failure rather than a lint. Runtime file lookup was rejected outright: it breaks the single-static-binary contract and turns a missing document into a user-facing surprise instead of a build error. C++23 `#embed` was rejected as out of the C++20 target.

- **D-03: Analyzers refer to checks through the generated enum; the string form exists only at the edges.** — **Reversibility:** costly — the call sites are spread across every analyzer in Phases 3–7.

  Call sites use `CheckId::video_color_range`. Strings appear only where they must: config globs, JSON output, `--explain`. A mistyped identifier becomes a compile error rather than a measurement attributed to a check that does not exist — which, under `skipped ≠ pass`, would be an invisible hole rather than a loud one.

- **D-04: Each check declares one baseline severity and tolerance, plus explicit overrides only for profiles that differ.** — **Reversibility:** reversible.

  Keeps `checks.def` readable and makes a deliberate exception look like an exception. `video.color.range` is fail in every profile with no exceptions; `content.video.frame_hash` is fail but info under `hw-encoder` — that asymmetry should be visible, not buried in one cell of a five-column row.

- **D-05: The generator is written in Python 3.** — **Reversibility:** costly — it becomes a build prerequisite on every developer machine and every CI leg.

  **This is a new entry in PROJECT.md's Constraints and must be added there.** Phase 1's constraint list names CMake, ninja, git and a system ffmpeg for corpus generation; Python 3 now joins it. GitHub's runner images all ship Python 3, so CI is unaffected, but a developer without it can no longer build.

  CMake script mode was the dependency-free alternative and was rejected for the awkwardness of non-trivial parsing in `string(REGEX)`. A C++ host tool was rejected for the host-vs-target build complexity it adds, which is real here since `x64-osx` already cross-builds from arm64.

### Measurement values

- **D-06: Represent a measurement's value with the standard library variant type.** — **Reversibility:** costly — every analyzer in Phases 3–7 constructs these, and the serializer visits them.

  `std::variant` over the nine alternatives (`int64`, rational, `double`, string, string set, histogram, span list, hash chain, absent). Available on all four toolchains, exhaustiveness-checkable via `std::visit`, and value semantics suit a type that is copied into fingerprints and serialized. A hand-rolled tagged union was rejected: owning copy/move/destroy correctness for nine alternatives, three of which hold heap memory, is a large surface for a type that must serialize byte-identically.

- **D-07: Define the rational type in core and convert from the libav one at the edge.** — **Reversibility:** costly — every analyzer's emit path converts.

  PROJECT.md pins time as `{int64 value, AVRational tb}`, but `core/` defining its own POD keeps `core/` and `compare/` free of any libav include. The payoff is concrete: the semantics engine is unit-testable **without FFmpeg linked at all**, which matters in a phase that has no media and where FFmpeg is the 40-minute dependency.

- **D-08: One central serializer owns canonical output.** — **Reversibility:** reversible.

  A single visit-based writer owns field order, shortest-round-trip float formatting and the one-value-per-line layout. Byte-identity then becomes a property of one exhaustively testable function, rather than an invariant nine independent implementations must each preserve — where one divergent float format would silently break SNAP-03 and TRUST-05 in a way only a cross-platform diff would surface.

- **D-09: The registry's declared value kind is authoritative; a mismatch is an error, never a coercion.** — **Reversibility:** reversible.

  `CheckDef.value_kind` declares what a check emits. The engine asserts the emitted value matches and produces `status=error` when it does not. An analyzer emitting the wrong kind is a bug that must surface loudly; silently coercing it is exactly how a check stops measuring what it claims to measure.

### Proving the engine without media

- **D-10: Hand-authored snapshot pairs are the primary integration harness.** — **Reversibility:** reversible.

  SNAP-02 already requires `compare` to accept a `.snap.json` in place of live media, so `compare baseline.snap.json candidate.snap.json` drives the entire compare → policy → report pipeline with no analyzer and no media whatsoever. The fixtures are readable text that can be crafted to hit any semantic or edge case exactly, and they carry forward as regression cases for later phases.

- **D-11: The stub analyzer is a test-only target and never enters the shipped binary.** — **Reversibility:** reversible.

  Linked into test executables and driven directly. Nothing synthetic can reach a user and `list-checks` stays honest. Accepted cost: the shipped `snapshot` command has no exercised path until Phase 3 brings real probes. A hidden dev flag and a build-flag-gated stub were both rejected — BUILD-09 in Phase 1 already demonstrated how easily a default-off build option drifts from what ships.

- **D-12: `UPDATE_GOLDENS` is a local developer affordance; CI runs read-only and fails on any diff.** — **Reversibility:** reversible.

  A changed golden then appears as a reviewable diff in the pull request, where a human sees that the output actually changed — rather than being silently rewritten by the very run that was supposed to catch it.

- **D-13: Determinism is proven by running compare twice and diffing.** — **Reversibility:** reversible.

  Cheap, runs on every CI leg, and catches nondeterminism where it is introduced. Because the same fixtures must produce the same bytes on Linux, macOS and Windows, it also catches platform float and ordering drift for free.

### Fail-first discipline

- **D-14: Every comparison semantic must declare a fixture that passes and one that must not pass, and the harness fails if either is missing.** — **Reversibility:** costly — every check added in Phases 3–7 inherits the requirement.

  This is the direct counter to Phase 1's most expensive lesson. That phase shipped **six checks structurally incapable of observing their own subject**: two ctest filters that matched zero tests while reporting green, a PowerShell negative-path check that aborted before its own assertion, a zero-test guard that died before its diagnostic, an ENG-16 lint that could not match the commonest violation, and a test-count guard that undercounted and would have failed the build spuriously below ten tests.

  A registry field was rejected for baking test metadata into the shipped product. A CI script auditing test sources was rejected because it would itself be a check that could fail to observe its subject — the exact pattern being defended against.

- **D-15: The unit is each semantic crossed with each status it can emit — not merely pass versus fail.** — **Reversibility:** costly.

  The seven semantics yield `pass | info | warn | fail | skipped | error`, and `skipped ≠ pass` is load-bearing. Proving that `hash` can return `skipped:hash_incomparable` matters as much as proving it can fail: a semantic that can never emit `skipped` would quietly convert "we cannot tell" into a verdict, which is precisely what the decode-determinism class system exists to prevent.

- **D-16: A permanent canary fixture must always report failing.** — **Reversibility:** reversible.

  One deliberately-wrong fixture whose expected outcome is a specific failure. If the suite ever reports it clean, the harness is broken rather than the fixture. This cannot be turtles all the way down, but it makes the base case cheap, permanent and loud — and it is exactly what would have caught `ctest -R unit` selecting zero tests while reporting green.

- **D-17: The discipline is both a Phase 2 acceptance criterion and a recorded project convention.** — **Reversibility:** reversible.

  Gates this phase concretely and is written where Phases 3–7 inherit it. Those phases add roughly sixty real checks, and docs 02–06 already require "one triggering fixture pair and one clean pair" for each. Making the rule explicit now means the verifier can check it rather than hope for it.

### Claude's Discretion

Every area presented was decided. The planner retains normal latitude on file and namespace layout within `core/`, `config/`, `compare/` and `report/`; the internal shape of the generator; and how plans are sliced across the 48 requirements.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source of truth
- `claude_docs/01-core-concepts.md` — this phase's source doc in full (ROADMAP Phase 2 = design-doc phase 1). §1 object model, §2 registry, §3 comparison semantics table, §4 severity resolution, §5 profiles, §6 configuration precedence, §7 decode-determinism classes, §8 snapshot format, §9 report formats, §10 `dir` orchestration, §11 error taxonomy, §12 testing strategy, §13 phase acceptance.
- `claude_docs/00-design-and-requirements.md` — §3.1 CLI surface and exit codes, §3.2 CLI11 parsing design, §6 architecture, §7 repo layout, §9 conventions.

### Phase 1 inheritance — decisions and hard-won lessons
- `.planning/phases/01-foundation-toolchain/01-CONTEXT.md` — D-01…D-08. Still binding: the `expected<T, E>` alias, the library/CLI target split, the ENG-16 boundary.
- `.planning/phases/01-foundation-toolchain/01-VERIFICATION.md` — **read the Resolution section.** Documents the six-unobservable-checks pattern that D-14 through D-16 exist to counter.
- `.planning/phases/01-foundation-toolchain/01-REVIEW.md` — the code-review findings, including three deferred warnings in `tests/integration/cli_harness.h` that this phase's own integration tests will run through.

### Research
- `.planning/research/ARCHITECTURE.md` — §3 surveys registry design in clang-tidy, ESLint, OPA and Semgrep: ID stability, deprecation and aliasing, and which registry designs scale versus rot. Directly relevant to D-01 through D-04.
- `.planning/research/PITFALLS.md` — CI-gate product mistakes, including check-ID renames breaking user configs.
- `.planning/research/FEATURES.md` — the snapshot-testing UX conventions (Jest, `insta`, ApprovalTests) that SNAP-07's CI-safe write behaviour follows.

### Project-level
- `.planning/PROJECT.md` — constraints and Key Decisions. **D-05 requires adding Python 3 to the Constraints section.**
- `.planning/REQUIREMENTS.md` — the 48 requirement texts verbatim.
- `.planning/ROADMAP.md` — Phase 2's five success criteria. **Note criterion 3 was amended on 2026-08-15** to absorb the colour-rendering clause moved out of Phase 1, since this is the first phase that emits styled output.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- `src/util/expected.h` — `mediadiff::expected<T, E>` aliased over `tl-expected`. This is the error-handling vehicle for all of `core/`; the header is the only file permitted to name the backing library type.
- `src/util/fs.h` — `fopen_utf8` and the UTF-8/UTF-16 conversion helpers. Every file-opening site in this phase (TOML config load, snapshot read/write, report output) goes through it. Verified on real Windows hardware in Phase 1.
- `src/util/version.{h,cpp}` — the `--version` composition, and the working example of how the CLI renders information.
- `tests/integration/cli_harness.h` — spawns the built binary and captures stdout/stderr on both platforms. This phase's CLI integration tests will use it. **It carries three known deferred warnings** (pipe-FD leak on non-`EINTR` `select()` errors, `EINTR` treated as EOF which can silently truncate captured output, unchecked `waitpid`). The truncation one is worth fixing before leaning on this harness heavily — a truncated capture would let an assertion pass against incomplete data.

### Established Patterns

- **Target boundary:** `libmediadiff` writes to no standard stream and never exits the process; `mediadiff` owns rendering and process control. Enforced by `scripts/lint_eng16.sh`, which now catches `fprintf(stderr, …)` and matches on stream names rather than a function list. It still false-positives on banned tokens inside string literals — a known, tracked issue.
- **Test suites are prefixed** (`unit.`, `integration.`) so `ctest -R <suite>` genuinely selects them. Any new suite must follow this and **verify its filter selects a non-zero count** — three filters in Phase 1 silently matched nothing.
- **`skipped ≠ pass`** is applied to the project's own test suite, not just its output: `test_console_vt` skips rather than passes when stdout is not a console.

### Integration Points

- `src/cli/main.cpp` — the only entry point and the only place exit codes are produced. This phase extends it from `--version` to the full subcommand surface via CLI11.
- `src/core/`, `src/config/`, `src/compare/`, `src/report/` — currently empty scaffolds holding only a `.gitkeep`. This phase fills all four.
- `src/probe/` and `src/analyzers/*` — remain empty until Phase 3. Nothing in this phase should write into them.

</code_context>

<specifics>
## Specific Ideas

- **PROJECT.md's Constraints section needs a Python 3 entry** (D-05). It currently names CMake ≥ 3.25, ninja, git and a system ffmpeg ≥ 6.1; the registry generator adds Python 3 as a build prerequisite on every developer machine.
- **The `cli_harness.h` `EINTR`-as-EOF warning is worth closing early in this phase.** It was deferred in Phase 1 as test-harness-only, which was right when the harness ran three tests. This phase's CLI integration tests will lean on it much harder, and silent output truncation would let assertions pass against incomplete data — the same family as the pattern D-14 defends against.
- **The two advisory CI legs (`arm64-linux`, `x64-osx`) have been red for the whole of Phase 1.** Non-blocking by decision D-06 of that phase, but standing red erodes signal, which is the thing this project exists to prevent. Worth deciding during this phase whether to fix them or formally drop those triplets.

</specifics>

<deferred>
## Deferred Ideas

Gray areas identified but not explored in this discussion. Research and planning should treat these as open, and surface them if they turn out to block:

- **TRUST-08's cross-release idempotence has a bootstrap problem.** The requirement is to compare the current build against a snapshot taken by the *previous release* — and there is no previous release. How that check behaves on the first run needs an answer, and "silently pass when there is no baseline" would be another check that cannot observe its subject.
- **What `Error` carries and how it composes across layers.** `Error.kind` maps to exit codes per doc 01 §11, but whether it carries context, a cause chain or a source location is undecided.
- **Whether the four report formats share an intermediate representation** or each render findings independently.
- **How `list-checks --effective` shows policy provenance** — doc 01 §4 requires the resolved severity chain to be visible under `-v`, which implies the merge retains where each value came from.

</deferred>

---

*Phase: 2-Core Engine*
*Context gathered: 2026-08-15*
