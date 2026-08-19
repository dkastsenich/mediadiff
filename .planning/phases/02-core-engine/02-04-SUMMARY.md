---
phase: 02-core-engine
plan: 04
subsystem: core-engine
tags: [compare-engine, tolerance-grammar, semantics, catch2, cmake]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 01)
    provides: "compare_fingerprints/comparator_for dispatch spine, the frozen schema_version/seed-ID/time-value/ID-grammar contracts, core/value.h's 9-alternative Value variant, core/rational.h's compare_ticks"
  - phase: 02-core-engine (plan 02)
    provides: "CheckDef::tolerance_for/severity_for, core/registry.h's Unit/ValueKind enums"
  - phase: 02-core-engine (plan 03)
    provides: "The test-only registry (tests/support/test_checks.def, --symbol-prefix test_), tests/support/stub_analyzer.h's make_stub_fingerprint, the (semantic, status) fail-first coverage gate and its not_yet_implemented_semantics() allow list, the permanent canary, tests/support/fixture_paths.h"
provides:
  - "core/tolerance.{h,cpp}: parse_tolerance() -- doc 01 section 3's grammar (every unit suffix, both sign spellings, the two-threshold {warn,fail} form) parsed into an exact num/den rational, never a double; every rejection names the check's expected unit (CLI-10)"
  - "core/registry.h's unit_suffix(Unit) -- the canonical suffix text a tolerance-grammar diagnostic renders"
  - "All seven Semantic enumerators dispatching to a real comparator: src/compare/{tol,set,presence,hash,dist,span}.cpp alongside the existing exact.cpp, comparator_for's switch now exhaustive with no default: arm"
  - "compare/engine.cpp's D-09 value_kind guard: compare_fingerprints itself now asserts a Measurement's held Value alternative matches the registry's declared value_kind before any comparator runs, independent of whether the Measurement came from a snapshot file"
  - "core/serializer.cpp's value_from_json now round-trips Value::Absent (JSON null) for any value_kind, closing a pre-existing snapshot self-check gap"
  - "The fail-first coverage allow list is empty -- all seven semantics have a real fixture for every status they can emit, tests/support/test_checks.def extended with t.tol_info/t.dist_bins_fail/t.int64_count to reach every cell with one severity per check"
affects: ["02-05 (policy precedence merge -- resolve_severity's body still returns only default_severity; every escalate() helper in the six new comparator files calls it exactly once, so the merge only has one call site per file to touch)", "02-06 (config layer's CLI --tol flag is the first real caller of parse_tolerance outside a comparator)", "03+ (real analyzers construct Measurements directly, in-process, with no read_snapshot gate -- compare/engine.cpp's D-09 check is their only guard against a wrong-kind Value)"]

# Actuals (#2632)
actuals:
  tokens: 26000
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "escalate(Severity) helper duplicated per comparator file (set/presence/hash/dist/tol/span each define their own small anonymous-namespace copy) rather than a shared header function -- matches exact.cpp's own inline severity switch, keeps each comparator file self-contained and independently reviewable, and the duplication is a five-line switch, not logic worth centralizing yet (plan 02-05's policy merge is the natural point to reconsider this)"
    - "Digit-count-based exact rational parsing (core/tolerance.cpp): a magnitude's num/den is derived by counting fractional digits (den = 10^count), never through strtod/atof/stod and never reduced to lowest terms -- '0.2' is exactly num=2,den=10, not num=1,den=5"
    - "Two-threshold tolerance grammar (Claude's Discretion, 02-CONTEXT.md): \"<warn>[suffix],<fail><suffix>\" -- the fail side's suffix is mandatory and canonical, the warn side's is optional but must match if present, so both \"3,5ms\" and \"3ms,5ms\" parse to the identical Tolerance"
    - "D-09 value_kind guard lives in compare/engine.cpp (compare_fingerprints), not in core/serializer.cpp alone -- serializer.cpp's value_from_json already gated the read-from-snapshot path, but a live analyzer constructing a Measurement in-process (Phase 3+) has no such gate, so the check is enforced again at the pairing/dispatch layer, before any comparator ever runs"
    - "compare_tol's own documented contract: a time-unit (Unit::ms) RationalValue's num is the measured quantity already expressed in the check's declared unit, with tb used only to order the two sides via compare_ticks for rendering the delta's sign, not to rescale the magnitude -- a deliberate scoping decision since Phase 2 has no real analyzer yet (D-10); flagged as a follow-up for whichever later phase's analyzer first needs genuinely differing per-side timebases"

key-files:
  created:
    - src/core/tolerance.h
    - src/core/tolerance.cpp
    - src/compare/tol.cpp
    - src/compare/set.cpp
    - src/compare/presence.cpp
    - src/compare/hash.cpp
    - src/compare/dist.cpp
    - src/compare/span.cpp
    - tests/unit/test_tolerance.cpp
    - tests/unit/test_compare_semantics.cpp
    - tests/support/docs/t.tol_info.md
    - tests/support/docs/t.dist_bins_fail.md
    - tests/support/docs/t.int64_count.md
    - "46 tests/fixtures/snapshots/*.snap.json fixtures (one pair per (semantic, status) cell)"
  modified:
    - CMakeLists.txt
    - src/core/registry.h
    - src/core/serializer.cpp
    - src/compare/engine.cpp
    - src/compare/exact.cpp
    - src/compare/semantics.h
    - tests/support/test_checks.def
    - tests/support/docs/t.tol_ms.md
    - tests/support/docs/t.dist_bins.md
    - tests/support/docs/t.span_runs.md
    - tests/unit/CMakeLists.txt
    - tests/unit/test_fail_first_coverage.cpp

key-decisions:
  - "Two-threshold tolerance grammar chosen as \"<warn>[suffix],<fail><suffix>\" -- doc 01 section 3 requires the {warn,fail} form to exist but does not spell out its literal text; accepting an optional matching suffix on the warn side (not only the bare form) makes \"3ms,5ms\" as valid as \"3,5ms\", the more readable spelling actually used in tests/support/test_checks.def"
  - "Unit::count's canonical tolerance suffix is \"bytes\"; the bare (no-suffix) form doc 01's \"±8\" example shows is ALSO accepted for that one unit, as an additional spelling rather than the only one -- core/registry.h's unit_suffix() reports only the canonical \"bytes\" spelling in diagnostics"
  - "tests/support/test_checks.def gained three new checks (t.tol_info, t.dist_bins_fail, t.int64_count) and three modified ones (t.tol_ms's tolerance -> two-threshold, t.dist_bins gained a tolerance string, t.span_runs's severity -> fail) -- not in the plan's own files_modified list, but required: the fail-first coverage gate needs every (semantic, status) cell backed by a fixture, and a CheckDef's severity/tolerance is fixed at registry-generation time, so reaching tol's five statuses and dist's four with only the plan's original checks was structurally impossible"
  - "compare_hash's precondition evidence keys (decode_path_class, sampling_state, normalization) are this plan's own design -- no analyzer exists yet to populate Measurement::evidence for hashing (that's Phase 6+); a future analyzer needing a different shape can extend the table without touching the pass/fail/skipped decision logic"
  - "compare_span's spans-adjacency merge gap is \"one tick\" (in the span's own timebase, or overlap-check-only when adjacent spans' tb differ) rather than doc 01's literal \"1 frame\" -- this engine layer has no frame-rate concept yet (Phase 2 proves the engine without media, D-10); a later analyzer supplying a genuine frame duration is expected to encode it into the span's own tb"
  - "core/serializer.cpp's value_from_json gained a null -> Value::Absent path for any value_kind (a Rule 1 bug fix, not scoped to this plan's files_modified list) -- value_to_json already wrote Absent as JSON null, so the round-trip was asymmetric before this fix, which would have broken the permanent `mediadiff snapshot f && mediadiff compare f f.snap.json` self-check for any Absent measurement"

patterns-established:
  - "Every non-exact comparator resolves severity once via resolve_severity(check, policy), stores it on the Finding immediately, and only escalates it into a Status on the non-pass path -- exact.cpp's own established shape, now followed uniformly across set/presence/hash/dist/tol/span"
  - "A comparator that needs a parsed Tolerance calls parse_tolerance(check.tolerance_for(policy.profile), check.unit) and propagates its Error via mediadiff::unexpected on failure, rather than asserting or ignoring a malformed tolerance string"

requirements-completed: [ENG-04, ENG-05, CLI-10]

coverage:
  - id: D1
    description: "Tolerance grammar parser: every unit suffix, both sign spellings, an integer and fractional magnitude, the two-threshold form, and every rejection case (empty, no-digits, wrong-suffix, negative, non-ASCII) naming the expected unit"
    requirement: "ENG-05, CLI-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_tolerance.cpp (17 test cases)"
        status: pass
      - kind: other
        ref: "grep -rn 'strtod|atof|stod' src/core/tolerance.cpp -> no matches"
        status: pass
    human_judgment: false
  - id: D2
    description: "All seven comparison semantics dispatch to a real comparator; time tolerances compare via compare_ticks/integer cross-multiplication only, never a double; a hash precondition mismatch is skipped:hash_incomparable, never a fabricated verdict; a value_kind mismatch is Status::error"
    requirement: "ENG-04"
    verification:
      - kind: unit
        ref: "tests/unit/test_compare_semantics.cpp (10 test cases: tol/dist boundaries, empty inputs, StringSet/SpanList ordering, hash skip, D-09 error)"
        status: pass
      - kind: other
        ref: "grep -cE 'static_cast<double>|static_cast<float>|\\(double\\)' src/compare/tol.cpp -> 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "The fail-first coverage allow list is empty: every (semantic, status) cell the coverage table declares has a real fixture pair, proven to actually gate via a negative control"
    requirement: "ENG-04"
    verification:
      - kind: unit
        ref: "ctest -R 'unit.(fail_first|semantics|canary)' (14 test cases)"
        status: pass
      - kind: other
        ref: "manual negative control: removing tests/fixtures/snapshots/t.dist_bins_fail__fail.a.snap.json makes unit.fail_first fail naming '(dist, fail)'; restored and green again"
        status: pass
    human_judgment: false

duration: 1h 30min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 4: Tolerance Grammar and the Six Remaining Comparison Semantics Summary

**Every one of the seven `compare/` semantics (`tol`, `set`, `presence`, `hash`, `dist`, `span`, plus the already-implemented `exact`) now dispatches to a real comparator behind an exact-rational tolerance grammar parser, with time compared through integer cross-multiplication only, a wrong-unit tolerance rejected as a usage error naming the expected unit, and `hash` able to say "we cannot tell" via `skipped:hash_incomparable` instead of guessing.**

## Performance

- **Duration:** ~1h 30min
- **Started:** 2026-08-15T14:41:00Z (approx, session start)
- **Completed:** 2026-08-15T16:12:31Z
- **Tasks:** 3
- **Files modified:** 71 (25 created, 46 fixture files among them; 12 modified)

## Accomplishments
- `core/tolerance.{h,cpp}`: a hand-rolled, regex-free, `strtod`/`atof`/`stod`-free grammar parser producing an exact `num/den` rational — `parse_tolerance("0.2ms/min", Unit::ms_per_min)` yields exactly `num=2, den=10`, never a rounded double; the plus-minus sign in both spellings (`±`/`+-`); the two-threshold `{warn, fail}` form; and every rejection (empty, no digits, wrong suffix, negative, non-ASCII outside the leading sign) naming the check's expected unit via `core/registry.h`'s new `unit_suffix(Unit)`
- All six remaining comparators implemented — `tol` (severity-escalating single threshold or independent-of-severity two-threshold zones, integer cross-multiplication only, `compare_ticks` for the sign), `set` (symmetric difference over `StringSet`, order-insensitive by construction), `presence` (never compares values, only presence), `hash` (evidence-carried precondition check before any digest comparison — a mismatch is `skipped:hash_incomparable`, never a fabricated pass or fail), `dist` (max absolute bin-proportion difference via single-pass merge and cross-multiplication), `span` (interval algebra with tick-adjacent merging; an introduced span gates, a removed-only span is always `info`)
- `comparator_for`'s switch is now exhaustive over all seven `Semantic` values with no `default:` arm
- `compare/engine.cpp` gained the D-09 value_kind guard `compare_fingerprints` itself enforces before dispatching to any comparator — a live analyzer's in-process Measurement (no `read_snapshot` gate) is covered too
- Closed a pre-existing snapshot round-trip gap: `value_from_json` never had a `null -> Absent` path even though `value_to_json` already wrote `Absent` as `null`
- The fail-first coverage allow list (`tests/unit/test_fail_first_coverage.cpp`) is empty — 46 hand-authored fixture files back every `(semantic, status)` cell, with three new/modified test-only checks to reach every cell despite one severity per check

## Task Commits

Each task was committed atomically:

1. **Task 1: Tolerance grammar parser with unit checking** - `cae737f` (feat)
2. **Task 2: The six remaining comparison semantics** - `68e2376` (feat)
3. **Task 3: Close the fail-first coverage allow list** - `21f1745` (test)

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified
- `src/core/tolerance.{h,cpp}` - `Tolerance` struct + `parse_tolerance()`
- `src/core/registry.h` - `unit_suffix(Unit)`
- `src/compare/{tol,set,presence,hash,dist,span}.cpp` - the six new comparators
- `src/compare/engine.cpp` - D-09 value_kind guard (`value_kind_mismatch`/`value_kind_of`)
- `src/compare/exact.cpp` - `comparator_for`'s now-exhaustive switch
- `src/compare/semantics.h` - forward declarations for all seven comparators
- `src/core/serializer.cpp` - `value_from_json`'s `null -> Absent` fix
- `tests/support/test_checks.def` + 3 new/3 modified `tests/support/docs/t.*.md` - the three new checks needed for full coverage
- `tests/unit/test_tolerance.cpp`, `tests/unit/test_compare_semantics.cpp` - new unit suites
- `tests/unit/test_fail_first_coverage.cpp` - allow list emptied
- 46 `tests/fixtures/snapshots/t.*.snap.json` - one pair per `(semantic, status)` cell
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt` - new sources wired in

## Decisions Made
See frontmatter `key-decisions` for full rationale on: the two-threshold grammar's literal text shape, `Unit::count`'s "bytes"-plus-bare-form suffix handling, the three test-registry additions needed to reach full coverage, `compare_hash`'s evidence-key shape, `compare_span`'s "one tick" merge-gap simplification, and the `value_from_json` Absent round-trip fix.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] Extended `tests/support/test_checks.def` with three new checks and modified three existing ones**
- **Found during:** Task 3
- **Issue:** The fail-first coverage table requires `tol` to reach `pass`/`info`/`warn`/`fail`/`error` and `dist` to reach `pass`/`warn`/`fail`/`error`, but a `CheckDef`'s severity and tolerance string are fixed at registry-generation time — one severity per check. The plan's own pre-existing `t.tol_ms` (severity=fail) and `t.dist_bins` (severity=warn, no declared tolerance at all) could not reach every cell on their own.
- **Fix:** Added `t.tol_info` (severity=info) and `t.dist_bins_fail` (severity=fail) as sibling checks under the same semantics; added `t.int64_count` (value_kind=int64) for the D-09 acceptance criterion's exact pairing; changed `t.tol_ms`'s tolerance to the two-threshold `"3ms,5ms"` form (covers pass/warn/fail on its own), gave `t.dist_bins` a `"5%"` tolerance it previously lacked, and changed `t.span_runs`'s severity from info to fail (one check now backs span's pass/info/fail since a removed-only span is always info regardless of severity).
- **Files modified:** `tests/support/test_checks.def`, three new + three modified `tests/support/docs/t.*.md`.
- **Commit:** `21f1745`

**2. [Rule 1 - Bug] Fixed `value_from_json`'s missing `Absent` round-trip**
- **Found during:** Task 3, while designing the `presence` semantic's fixtures (a `presence` fixture needs to encode "this side is absent" in JSON, which requires reading `null` back as `Value::Absent` for any `value_kind`)
- **Issue:** `value_to_json` already serializes `Absent` as JSON `null`, but `value_from_json` had no case that ever produced `Value::Absent` — reading a snapshot containing an `Absent` measurement back in would fail with a value_kind-mismatch error, breaking the permanent `mediadiff snapshot f && mediadiff compare f f.snap.json` self-check (doc 01 section 8) for any check whose value can legitimately be absent.
- **Fix:** Added `if (json.is_null()) return Value{Absent{}};` at the top of `value_from_json`, before the `expected_kind` switch, so `null` round-trips regardless of the check's declared kind (matching D-09's own Absent exemption).
- **Files modified:** `src/core/serializer.cpp`.
- **Commit:** `68e2376`

**3. [Rule 3 - Blocking] Added the D-09 value_kind guard to `compare/engine.cpp` itself, not only relying on `read_snapshot`'s existing gate**
- **Found during:** Task 2
- **Issue:** Task 2's action text explicitly requires `compare_fingerprints` to assert value_kind conformance "before dispatching... This runs for every semantic, so no comparator needs to re-check it" — `read_snapshot`'s existing `value_from_json` gate only covers measurements read from a `.snap.json` file, not ones constructed in-process (the only path available today via `tests/support/stub_analyzer.h`, and the path every real analyzer from Phase 3 onward will use).
- **Fix:** Added `value_kind_mismatch`/`value_kind_of`/`value_kind_name` helpers and the guard itself to `compare/engine.cpp`'s pairing loop, constructing a `Status::error` `Finding` directly (bypassing the comparator) on a mismatch, exempting `Absent` per D-09.
- **Files modified:** `src/compare/engine.cpp`.
- **Commit:** `68e2376`

---

**Total deviations:** 3 auto-fixed (1 Rule 1, 1 Rule 2, 1 Rule 3)
**Impact on plan:** All three were necessary to satisfy the plan's own must_haves and acceptance criteria (full (semantic, status) coverage, the D-09 in-process guard, and a working `presence` round-trip); none change scope, architecture, or any frozen contract from prior waves.

## Issues Encountered

- **First implementation of the two-threshold tolerance grammar rejected any suffix on the warn side** (matching a header-comment design that hadn't yet accounted for `test_checks.def`'s own `"3ms,5ms"` spelling), which broke both `test_tolerance.cpp`'s own two-threshold test cases and — more importantly — caused `t.tol_ms`'s tolerance string itself to fail to parse at compare time, which surfaced as a spurious `unit.fail_first_coverage` failure on first `ctest` run. Fixed by relaxing the grammar to accept an optional, matching suffix on the warn side (`"3,5ms"` and `"3ms,5ms"` both valid), re-verified via a full rebuild and `ctest` pass.
- **A C++ hex-escape-sequence-out-of-range compile error** in two `test_tolerance.cpp` string literals (`"\xC2\xB18"` — the trailing ASCII `8` was consumed as part of the hex escape, overflowing `char`'s range) — fixed by splitting into adjacent string literals (`"\xC2\xB1" "8"`), which the compiler concatenates but parses each hex escape independently.
- **The plan's own acceptance-criteria grep (`grep -rn "strtod\|atof\|stod" src/core/tolerance.cpp`) initially matched a comment** that named those functions explicitly to explain what the parser does NOT use — reworded to describe the property ("never a locale-sensitive C-library string-to-floating-point conversion") without naming the literal grep targets.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 02-05's policy precedence merge replaces `core/policy.h`'s `resolve_severity` body; every comparator's `escalate()` helper calls `resolve_severity` exactly once, so the merge only has one call site per file to touch, mirroring `exact.cpp`'s already-established shape.
- Plan 02-06's config layer is the first real caller of `parse_tolerance` outside a comparator (the CLI `--tol` flag) — its signature (`std::string_view raw, Unit expected_unit`) needs no changes for that call site.
- Real analyzers from Phase 3 onward construct `Measurement`s in-process with no `read_snapshot` gate of their own — `compare/engine.cpp`'s D-09 check is their only guard against a wrong-`value_kind` `Value`, already proven working via `tests/unit/test_compare_semantics.cpp`'s `t.int64_count` case.
- `compare_tol`'s and `compare_span`'s documented simplifications (RationalValue's `num` already in-unit for time comparisons; "one tick" merge gap absent a real frame rate) are both flagged, contained scoping decisions for a phase with no real analyzer yet (D-10) — whichever later phase's analyzer first needs genuinely differing per-side timebases, or a real frame-rate-aware merge gap, should revisit these two files specifically.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 13 created files verified present on disk (`src/core/tolerance.{h,cpp}`, `src/compare/{tol,set,presence,hash,dist,span}.cpp`, `tests/unit/test_{tolerance,compare_semantics}.cpp`, `tests/support/docs/t.{tol_info,dist_bins_fail,int64_count}.md`); all 3 task commit hashes (`cae737f`, `68e2376`, `21f1745`) verified present in `git log --oneline --all`.
