# Phase 2: Core Engine - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-15
**Phase:** 2-Core Engine
**Areas discussed:** Registry mechanism, Value union shape, Stub strategy, Fail-first checks
**Mode:** interactive (default) — all four offered areas selected

---

## Registry mechanism

| Option | Description | Selected |
|--------|-------------|----------|
| Codegen script at build time | One generator emits enum, registry, embedded docs, manifest | ✓ |
| X-macro | Pure C++, no build step; IDE/debugger hostile | |
| constexpr table | Debuggable; enum-vs-array drift risk | |

**Choice:** codegen at build time.
**Notes:** the deciding argument was that doc 00 §7 already requires `docs/checks/<id>.md` to be compiled into the binary, so a build-time generation step must exist regardless — one generator beats two systems that can drift.

| Option | Description | Selected |
|--------|-------------|----------|
| Generated into a C++ TU at build time | Build fails when a registered ID has no doc | ✓ |
| C++23 `#embed` / linker blobs | Out of the C++20 target; per-platform divergence | |
| Read from disk at runtime | Breaks single-binary contract | |

**Choice:** generated TU. Makes ENG-01 a build failure rather than a lint.

| Option | Description | Selected |
|--------|-------------|----------|
| Generated enum, strings at edges | Typo becomes a compile error | ✓ |
| string_view constants | Simpler generator; typo compiles and the check silently never runs | |
| You decide | | |

**Choice:** generated enum.

| Option | Description | Selected |
|--------|-------------|----------|
| Base default + per-profile overrides | Exceptions look like exceptions | ✓ |
| Full 5-value matrix | Explicit but repetitive; wrong cells easy to miss | |
| Profiles as separate files | Splits one check across six files | |

**Choice:** base + overrides.

### Follow-up: generator language

| Option | Description | Selected |
|--------|-------------|----------|
| CMake script mode | Zero new dependencies; clunky parsing | |
| Python 3 | Comfortable parsing/templating; **new build dependency** | ✓ |
| Small C++ host tool | No new language; host-vs-target complexity when cross-compiling | |

**Choice:** Python 3.
**Notes:** raised specifically because it adds an entry to PROJECT.md's Constraints, which currently names only CMake, ninja, git and a system ffmpeg. GitHub runners ship Python 3 so CI is unaffected; developers now need it.

---

## Value union shape

| Option | Description | Selected |
|--------|-------------|----------|
| `std::variant` | Value semantics, visit exhaustiveness, all four toolchains | ✓ |
| Hand-rolled tagged union | Full layout control; owns copy/move/destroy for 9 alternatives | |
| Polymorphic base + derived | Extensible; loses value semantics, forces heap | |

**Choice:** `std::variant`.

| Option | Description | Selected |
|--------|-------------|----------|
| Own POD in core, convert at edge | Keeps `core/`/`compare/` free of libav; testable without FFmpeg | ✓ |
| Use `AVRational` directly | Zero conversion; pulls libav into the pure-logic layer | |
| You decide | | |

**Choice:** own POD.
**Notes:** the payoff is that the semantics engine is unit-testable without FFmpeg linked — material in a phase with no media and a 40-minute dependency.

| Option | Description | Selected |
|--------|-------------|----------|
| One central serializer | Byte-identity is one testable function | ✓ |
| Each alternative serializes itself | Byte-identity becomes a convention across nine sites | |

**Choice:** central serializer.

| Option | Description | Selected |
|--------|-------------|----------|
| Registry pins kind; mismatch is an error | Wrong kind surfaces loudly | ✓ |
| Dispatch on runtime kind | Permissive; silent behaviour change | |
| You decide | | |

**Choice:** registry authoritative.

---

## Stub strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Snapshot pairs as the main harness | Drives compare→policy→report with no analyzer, no media | ✓ |
| Stub analyzer as the main harness | Closer to real analyzers; test code in the product registry | |
| Both, equally | More harness to maintain | |

**Choice:** snapshot pairs.
**Notes:** SNAP-02 already requires `compare` to accept a `.snap.json` in place of live media, so this capability exists for product reasons and can be reused as the test vehicle at no extra cost.

| Option | Description | Selected |
|--------|-------------|----------|
| Test-only target | Nothing synthetic can ship; `list-checks` stays honest | ✓ |
| Hidden dev flag | End-to-end coverage; synthetic data reachable in a shipped artifact | |
| Build-flag gated | Second build configuration differing from what ships | |

**Choice:** test-only.
**Notes:** BUILD-09 in Phase 1 already showed how easily a default-off build option drifts from what actually ships.

| Option | Description | Selected |
|--------|-------------|----------|
| Goldens reviewed; CI never regenerates | Changed golden appears as a reviewable PR diff | ✓ |
| Regenerate freely, rely on PR review | Decouples "test passed" from "output correct" | |
| You decide | | |

**Choice:** read-only in CI.

| Option | Description | Selected |
|--------|-------------|----------|
| Run compare twice and diff | Cheap; catches platform drift for free | ✓ |
| Compare against a committed golden JSON | Conflates jitter with legitimate format change | |
| Both | Most coverage; two harnesses | |

**Choice:** run-twice.

---

## Fail-first checks

| Option | Description | Selected |
|--------|-------------|----------|
| Paired fixtures enforced by the harness | Machine-checkable, no registry coupling | ✓ |
| CheckDef registry field | Strongest coupling; bakes test metadata into the shipped registry | |
| CI script auditing test sources | Would itself be a check that could fail to observe its subject | |

**Choice:** paired fixtures.
**Notes:** the third option was rejected specifically because it reproduces the pattern being defended against.

| Option | Description | Selected |
|--------|-------------|----------|
| Each semantic × each status it can emit | Proves `skipped` is reachable, not just `fail` | ✓ |
| Each semantic, one non-pass case | Leaves skipped/error paths unproven | |
| Each requirement | Requirements and semantics do not map one-to-one | |

**Choice:** semantic × status.
**Notes:** a semantic that can never emit `skipped` would convert "we cannot tell" into a verdict — the exact failure the determinism-class system exists to prevent.

| Option | Description | Selected |
|--------|-------------|----------|
| Permanent canary fixture | Cheap, permanent, loud base case | ✓ |
| Assert non-zero counts at every gate | Targets the observed failure mode; per-site rather than one tripwire | |
| Both | Catches a dead harness and a mis-scoped filter | |

**Choice:** canary.

| Option | Description | Selected |
|--------|-------------|----------|
| Phase 2 criterion AND recorded convention | Gates now, inherited by Phases 3–7 | ✓ |
| Phase 2 criterion only | Tighter scope | |
| Convention only, no gate | Same shape as the discipline that produced six unobservable checks | |

**Choice:** criterion and convention.

---

## Claude's Discretion

Nothing was explicitly delegated — every area presented was decided. The planner retains normal latitude on file and namespace layout within the four directories, the generator's internal shape, and how the 48 requirements are sliced into plans.

## Deferred Ideas

Offered as candidates for further discussion; the user chose to proceed to context instead. Recorded in CONTEXT.md `<deferred>`:

- TRUST-08's cross-release idempotence bootstrap problem — there is no previous release to compare against on the first run
- What `Error` carries and how it composes across layers
- Whether the four report formats share an intermediate representation
- How `list-checks --effective` surfaces policy provenance
