---
phase: 02-core-engine
fixed_at: 2026-08-15T21:29:46Z
review_path: .planning/phases/02-core-engine/02-REVIEW.md
iteration: 1
findings_in_scope: 6
fixed: 6
skipped: 1
status: partial
---

# Phase 02: Code Review Fix Report

**Fixed at:** 2026-08-15T21:29:46Z
**Source review:** .planning/phases/02-core-engine/02-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 6 (CR-01, CR-02, CR-03, WR-01, WR-02, WR-03 — per explicit dispatch instructions)
- Fixed: 6
- Skipped: 1 (IN-01, explicitly excluded per dispatch instructions — Info tier, not in `critical_warning` scope regardless)

**Verification environment:** all builds and test runs below happened inside the isolated worktree at
`.claude/worktrees/rf-02-492775-1786827358` (branch `gsd-reviewfix/02-492775`), configured against the
`x64-linux` vcpkg triplet using the machine's existing `~/.cache/vcpkg/archives` binary cache (submodule
`vcpkg/` was `git submodule update --init`'d + unshallowed inside the worktree since it did not exist there
by default). A full clean rebuild (`cmake --build build/x64-linux --clean-first`) succeeded with zero
warnings under `-Wall -Wextra -Werror`, and the full CTest suite (296/296 passing, 1 legitimate
`unit.console_vt` skip) ran green immediately before the cleanup tail below. These numbers are reproducible
from the fast-forwarded main branch after this run's cleanup tail completes.

## Fixed Issues

### CR-01: Untrusted-snapshot type mismatches throw uncaught exceptions — crash on `compare`/`snapshot`, silent false "pass" on `dir`

**Files modified:** `src/core/serializer.cpp`, `src/core/snapshot.cpp`, `src/cli/main.cpp`, `tests/integration/test_type_poisoned_snapshot.cpp`, `tests/integration/CMakeLists.txt`
**Commit:** `b4738be`
**Applied fix:** Added type checks (`is_number_integer()`/`is_string()`) symmetric with the already-correct branches, to every `.contains()`-without-type-check site the reviewer flagged: `value_from_json`'s `rational`, `histogram` and `hash_chain` branches (`core/serializer.cpp`), and `input_identity_from_json` (`core/snapshot.cpp`). Audited every remaining `.get<>()` call in both files (per the reviewer's "audit ALL of them" note) — the rest were already type-checked. Added a defense-in-depth `catch (const std::exception&)` backstop in `main.cpp`'s `run()`, returning `kExitInternal`, for any future call site that reintroduces the pattern.

I did **not** need to touch `src/cli/worker_pool.cpp`'s blanket `catch (...)` as the reviewer's Fix section suggested — `src/cli/commands/dir.cpp`'s `job` lambda already writes `outcomes[i].hard_error = ...error()` and returns *before* the throw would have happened, once `read_snapshot` returns an `unexpected` instead of throwing. `dir.cpp`'s existing exit-code logic (`first_hard_error` → `exit_code_for(err.kind)`) already threads that hard error through to a non-zero exit with a named diagnostic line. Fixing the throw at its source was sufficient to fix both halves; verified directly (see Verification below) rather than assumed.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- Manual reproduction against the built binary, both halves, before writing automated tests: `compare` on a `size_bytes`-poisoned `input_identity` now exits `65` with a message (not SIGABRT/134); `dir` mode on the same poisoned pair now exits `65` (not `0`) with the corrupted file named in `diagnostics` and `summary.pass == 0`.
- New integration test `tests/integration/test_type_poisoned_snapshot.cpp` (2 test cases) spawns the real binary and asserts the exact exit codes (`65`, explicitly `!= 134`) for both halves — added to `tests/integration/CMakeLists.txt`.
- Full suite: 296/296 passing.

### CR-02: `--json` report float formatting bypasses the canonical `std::to_chars` serializer, breaking the determinism contract

**Files modified:** `src/core/serializer.h`, `src/core/serializer.cpp`, `src/report/json.cpp`, `src/report/junit.cpp`, `src/cli/tty_render.cpp`, `src/cli/commands/inspect.cpp`, `tests/golden/json_schema_basic.txt`, `tests/baseline/report-1.0.json`
**Commit:** `f72c138`
**Applied fix:** `report/json.cpp`'s both `render_json` overloads and `inspect.cpp`'s `render_inspect_json` (all top-level `--json` documents) now call `serialize_document(...)` instead of nlohmann's `report.dump(2)`/`doc.dump(2)`.

For the three call sites that embed a `Value`'s JSON representation **inline** in already-linear text — `junit.cpp`'s `baseline_candidate_detail` (an XML failure/error element body), `tty_render.cpp`'s finding row (subject to `wrap_text`/`elide_value`'s single-line-text contract), and `inspect.cpp`'s `value_to_text` (one `"  {id} {scope}: {value}\n"` line per measurement) — I did not blindly swap in `serialize_document`, because its own one-scalar-per-line layout would splice a literal newline into each of those single-line contexts and corrupt them. Instead I added `serialize_value_compact` to `core/serializer.h`/`.cpp`: a second recursive tree-walker that reuses `write_scalar` (the exact, single `std::to_chars` call site) for every leaf, producing single-line output. This keeps the "one canonical float formatter" property D-08 requires (there remains exactly one place a `double` becomes text) while not breaking the three inline layouts. I judged this necessary adaptation given the reviewer's own hard constraint ("do not introduce a third formatter") — a second *tree walk* reusing the same formatter is not a second *formatter*.

Regenerated `tests/golden/json_schema_basic.txt` via the project's own `UPDATE_GOLDENS=1 ctest -R json_schema` mechanism, and `tests/baseline/report-1.0.json` (TRUST-08's frozen cross-release baseline) by re-running `mediadiff compare tests/fixtures/snapshots/tracer_a.snap.json tests/fixtures/snapshots/tracer_b_clean.snap.json --json` against the fixed binary — both fixtures' content is byte-identical to each other post-fix, confirming determinism. Verified the new format is stable by running the underlying `compare` twice and diffing (byte-identical).

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- `UPDATE_GOLDENS=1` unit + integration suites both reported "All tests passed" — only `json_schema_basic.txt` and `report-1.0.json` changed (`git status` confirmed no other golden drifted, since no other fixture's underlying check emits a `rational`/`real` value in this phase's stub-only seed registry).
- Full suite: 296/296 passing (this includes `integration.json_schema`'s own byte-identical-across-two-runs assertion and `integration.idempotence`'s TRUST-08 frozen-baseline comparison, both of which exercise the new format directly).

### CR-03: Unbounded integer cross-multiplication on untrusted snapshot magnitudes — signed overflow (UB) in the tolerance/distribution decision path

**Files modified:** `src/core/rational.h`, `src/compare/tol.cpp`, `src/compare/dist.cpp`, `tests/unit/test_compare_semantics.cpp`
**Commit:** `00bcd69`
**Applied fix:** Added `detail::checked_sub`/`detail::checked_add`/`detail::checked_negate` to `core/rational.h` alongside the pre-existing `detail::checked_mul` (already used by `compare_ticks`). Routed **every** cross-multiplication, subtraction and absolute-value computation in `compare_tol` (`delta_den`, `delta_num`, `abs_delta_num`, `abs_baseline_num`, both the relative- and absolute-tolerance comparison products) and `compare_dist` (`baseline_total`/`candidate_total`/combined-bin accumulation, `diff_num`, `abs_diff_num`, `denom`, the worst-bin cross-comparison, and the final tolerance comparison) through these primitives. Went slightly beyond the reviewer's explicitly-cited line numbers to also guard `compare_dist`'s bin-total *accumulation* loops (`+=`) — an equally-reachable overflow path from the same untrusted `Histogram` the reviewer's own finding names, not called out by line number but the same root cause. On overflow, each comparator returns a real `Finding` carrying `Status::error` and a diagnostic message, mirroring `compare/engine.cpp`'s `value_kind_mismatch` "never a coercion, never a fabricated verdict" contract (D-09) — never a UB-tainted pass/warn/fail.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- Two new unit tests in `tests/unit/test_compare_semantics.cpp` construct `RationalValue`/`Histogram` values engineered to overflow at a specific step (`kMax * 2` for tol; `(kMax-1) + 1000` for dist's total accumulation) against the test registry's real `t.tol_ms`/`t.dist_bins` checks, asserting `Status::error`.
- Full suite: 296/296 passing; `tests/unit/test_fail_first_coverage.cpp`'s allow list confirmed still empty (`not_yet_implemented_semantics()` is `{}`) — this fix adds a new `Status::error` *path* to `tol`/`dist` but doesn't change the coverage table's existing satisfaction (that cell is already satisfied via the pre-existing value_kind-mismatch-at-read-time fixtures per D-09; the new overflow path is additional coverage, not required or claimed by that gate).

### WR-01: Digit-by-digit magnitude parsing has no overflow guard

**Files modified:** `src/core/tolerance.cpp`, `src/core/profiles.cpp`, `tests/unit/test_tolerance.cpp`, `tests/unit/test_transform_profile.cpp`
**Commit:** `bcb8559`
**Applied fix:** Routed every digit-accumulation loop (`core/tolerance.cpp`'s `parse_magnitude`'s integer and fractional halves, plus its `den *= 10` loop and the final `num` composition; `core/profiles.cpp`'s `parse_leading_int` and `parse_resolution_expectation`'s scale-factor grammar, including its own `den`/`scale_num` composition) through `core/rational.h::detail::checked_mul`/`checked_add`, returning a usage error the instant a magnitude cannot be represented exactly. Also guarded `parse_tolerance`'s two-threshold common-denominator rescale (`fail_scaled`/`warn_scaled`), which the reviewer's cited line range didn't name explicitly but which multiplies two already-parsed magnitudes by a ratio that can itself be large. `parse_leading_int`'s `ParsedInt` gained an `overflowed` field, consistent with `parse_dimensions`'s existing "return `nullopt`, treat as a non-match" contract for malformed input — no new error type needed there since that call site was already a `std::optional`-returning fallback path.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- 4 new unit tests (2 in `test_tolerance.cpp` for the tolerance grammar's integer and fractional halves, 2 in `test_transform_profile.cpp` for the scale-factor grammar and `parse_dimensions`) assert a ~30-digit magnitude is rejected as `ErrorKind::usage` (or `nullopt` for `parse_dimensions`), never silently wrapped.
- Manual CLI reproduction: `mediadiff compare ... --tol meta.tool_version=99999999999999999999999999999ms` exits `64` with `"tolerance magnitude is too large to represent exactly"`, not a wrapped/garbage tolerance.
- Full suite: 296/296 passing.

### WR-02: `NO_COLOR` is overridden by `GITHUB_ACTIONS=true`, contrary to its own spec and the project's stated precedence

**Files modified:** `src/cli/color_policy.cpp`, `src/cli/color_policy.h`, `tests/unit/test_color_policy.cpp`
**Commit:** `a4beae0`
**Applied fix:** Applied exactly the reviewer's suggested reordering: `flag_no_color`/`no_color.has_value()` are now checked together, first, before `github_actions`. Updated `color_policy.h`'s own precedence doc comment (which had been documenting the *buggy* ordering as if it were intentional) to match the corrected behavior and explain why. Added two regression tests specifically crossing `NO_COLOR`/`NO_COLOR=""` against `GITHUB_ACTIONS=true` (the exact combination the finding names) — the existing test suite had no case exercising that specific combination before this fix.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- `[color]`-tagged unit tests: 16 assertions in 11 test cases, all passing (9 pre-existing + 2 new).
- Full suite: 296/296 passing.

### WR-03: `compare_ticks`'s overflow fallback silently treats unequal same-sign values as equal in a real comparison path, not just display

**Files modified:** `src/compare/span.cpp`, `tests/unit/test_compare_semantics.cpp` (`src/core/rational.h`'s `TickOrder`/`compare_ticks_checked` addition and `compare_ticks`'s doc-comment update ended up committed under CR-03's commit `00bcd69` instead — see note below)
**Commit:** `31f53f3`
**Applied fix:** Took the reviewer's first suggested option ("propagate an overflow signal out of `compare_ticks`") rather than the second (bounding values at read time), for consistency with CR-03's "return `Status::error`, never fabricate" approach in the same file family. Added `TickOrder`/`compare_ticks_checked` to `core/rational.h` — the real-decision counterpart to the display-only `compare_ticks`, reporting `{0, true}` on overflow instead of a fabricated tie. Threaded a `bool& overflowed` out-parameter through every real-decision comparison in `compare/span.cpp` (`ticks_less`, `ticks_less_equal`, `merge_spans`'s sort comparator and same-timebase `+1` merge-adjacency check — which had its own unguarded `+1` add, now also guarded via `detail::checked_add` — `overlaps`, `overlaps_any`). `compare_span` checks the flag once, after `introduced`/`removed` have been computed, and returns `Status::error` rather than trusting either list if overflow occurred anywhere.

**Note on commit boundaries:** `core/rational.h`'s `TickOrder`/`compare_ticks_checked` addition is logically WR-03's, but ended up inside CR-03's commit (`00bcd69`) instead of this one. I deliberately `git add -p`'d `rational.h` to stage only the CR-03-relevant hunk (`checked_sub`/`checked_add`/`checked_negate`) before committing CR-03, but `git commit -m ... <pathspec>...` (given an explicit file path) stages **all** current unstaged changes to that path, not only what had been `git add -p`-staged — so the two WR-03 hunks (the doc-comment update and the new `TickOrder`/`compare_ticks_checked` block) were swept into CR-03's commit along with it. I caught this only after the fact (verified via `git show --stat`). Rather than rewrite history with `--amend` (against this workflow's own "always create NEW commits" rule) I am documenting it here transparently: CR-03's commit message covers only its own intent and does not mention `TickOrder`/`compare_ticks_checked`, but its diff to `rational.h` includes them. This commit's own diff to `rational.h` is empty as a direct consequence.

**Verified:**
- Full build clean under `-Wall -Wextra -Werror`.
- New unit test in `test_compare_semantics.cpp`: a candidate span engineered so its start ticks overflow (`kMax * 2`) against a baseline span, asserting `Status::error` rather than a fabricated overlap/no-overlap verdict.
- Full suite: 296/296 passing, including the pre-existing `"semantics: shuffling a SpanList's element order produces the same merged output"` test (confirms the overflow-signal plumbing didn't change ordinary, non-overflowing span behavior).

## Skipped Issues

### IN-01: TOCTOU window between the existence/git-tracked check and the actual write in the snapshot safe-write gate

**File:** `src/cli/commands/snapshot.cpp:181-202`
**Reason:** Explicitly excluded from this fix run's scope per the dispatching agent's own instructions ("Leave IN-01 (TOCTOU) unfixed — record it as skipped with the reason"). Independently, it is an Info-tier finding and this run's `fix_scope` is `critical_warning`, so it would have been out of scope regardless. The reviewer's own finding notes this is low real-world impact for a single-user CLI typically run in CI, and explicitly says "not urgent given the tool's threat model" — no code change was attempted.
**Original issue:** `write_snapshot_gated` calls `file_exists_utf8`/`is_git_tracked` (which spawns `git` and waits) before `write_snapshot`'s own `fopen_utf8`/rename — a millisecond-scale window in which `out_path` could be replaced by a concurrent actor, bypassing the "refuse to overwrite a tracked snapshot" gate.

---

_Fixed: 2026-08-15T21:29:46Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
