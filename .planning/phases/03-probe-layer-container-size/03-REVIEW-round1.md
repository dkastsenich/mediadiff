---
phase: 03-probe-layer-container-size
reviewed: 2026-09-05T07:23:09Z
depth: standard
files_reviewed: 34
files_reviewed_list:
  - src/config/toml_load.h
  - src/config/toml_load.cpp
  - src/cli/options.h
  - src/cli/options.cpp
  - src/cli/commands/compare.cpp
  - src/cli/commands/dir.cpp
  - src/cli/commands/inspect.cpp
  - src/cli/commands/snapshot.cpp
  - src/cli/commands/explain.cpp
  - src/cli/commands/list_checks.cpp
  - src/cli/commands/inspect_render.h
  - src/cli/diagnostics.h
  - src/cli/diagnostics.cpp
  - src/cli/main.cpp
  - src/analyzers/container/analyzers.h
  - src/analyzers/container/mp4.cpp
  - src/analyzers/container/ts.cpp
  - src/probe/pass.h
  - src/probe/orchestrator.cpp
  - src/report/junit.cpp
  - src/util/sanitize.h
  - scripts/check_corpus.sh
  - scripts/lint_control_bytes.sh
  - .github/workflows/ci.yml
  - CMakeLists.txt
  - tests/unit/test_toml_load.cpp
  - tests/unit/test_junit.cpp
  - tests/unit/test_mp4_fragment_duration.cpp
  - tests/unit/CMakeLists.txt
  - tests/integration/test_probe_budget_overflow.cpp
  - tests/integration/test_cli_diagnostics_escaping.cpp
  - tests/integration/CMakeLists.txt
  - tests/fixtures/config/probe_budget_over_ceiling.toml
  - tests/fixtures/config/probe_timeout_over_ceiling.toml
findings:
  critical: 0
  warning: 1
  info: 1
  total: 2
status: issues_found
---

# Phase 03: Code Review Report (gap-closure plans 03-12..03-15)

**Reviewed:** 2026-09-05T07:23:09Z
**Depth:** standard
**Files Reviewed:** 34
**Status:** issues_found

## Summary

This review covers only the gap-closure work landed by plans 03-12 through 03-15 (diff base `1204cfa`), which closed four classes of defect the prior 03-REVIEW.md/03-VERIFICATION.md found in the probe-layer/container-size phase:

1. **03-12** — bounded and checked-converted the `--probe-timeout`/`--probe-memory-budget-mb` (and their `[probe]` config-file counterparts) at a single resolver seam (`resolve_probe_memory_budget_bytes`, `bound_and_convert_timeout_seconds`), routing all four command entry points (`compare`, `inspect`, `snapshot`, `dir`) through it. This also caught a fifth defect site in `snapshot.cpp` that the original review/verification had missed.
2. **03-13** — replaced a raw signed subtraction and an overflow-unsound `compare_ticks_checked`-based sort in the MP4 fragment-duration median with a checked-subtraction, checked-order `detail::compute_median_fragment_duration` seam, and applied the same `checked_sub` treatment to a TS byte-offset delta.
3. **03-14** — added `scripts/check_corpus.sh`, a CI preflight that mechanically extracts the expected fixture list from `gen_corpus.sh` and fails fast (with a self-test control clause) before any test runs against a possibly-incomplete corpus, and wired system-ffmpeg installation + the check into `.github/workflows/ci.yml`.
4. **03-15** — closed out T-2-33 by (a) making `junit.cpp`'s `xml_escape` control-byte-safe (C0 controls other than tab/LF/CR, plus DEL, now escaped as visible `\xHH` rather than passed through raw) and (b) introducing a single CLI diagnostic sink (`report_cli_error`, `src/cli/diagnostics.cpp`) that all 44 previously-inline `fputs(..., stderr)` call sites now route through, with a second `lint_control_bytes.sh` rule enforcing that no other `src/cli/*` file writes to stderr directly.

I read every file in the required-reading list in full, diffed each against `1204cfa` to isolate exactly what this gap-closure work changed, built the project, and ran the full unit and integration suites plus both `scripts/lint_control_bytes.sh` and `scripts/lint_eng16.sh` locally — all green (453/453 unit test cases modulo one platform-conditional skip, 166/166 integration test cases, both lints clean). I also independently verified the two new overflow-boundary behaviors (`--probe-memory-budget-mb 8796093022208` and `--probe-timeout 9223372036854776`) against the built binary and confirmed both now exit 64 naming the correct ceiling, matching the new tests' claims.

I found no BLOCKER-level defects in this gap-closure work: the checked-arithmetic, control-byte-escaping, and diagnostic-sink-routing changes are correct, internally consistent, and covered by tests that actually exercise the previously-broken paths (not just the happy path). The one WARNING below is a real gap in the T-2-33 completion claim: the new JUnit `xml_escape` control-byte fix is not fully equivalent to `sanitize_for_display`'s own escaping despite the code's own comments asserting parity — it omits the backslash-doubling step that make the escape form unambiguous, silently reintroducing exactly the ambiguity `sanitize_for_display` was built to avoid, just in a different renderer.

## Warnings

### WR-01: `junit.cpp`'s `xml_escape` control-byte fix omits backslash-doubling, reintroducing the exact ambiguity `sanitize_for_display` was designed to prevent

**File:** `src/report/junit.cpp:104-121` (the `default:` arm of `xml_escape`'s switch, plus `append_control_byte_escape` at `src/report/junit.cpp:74-80`)

**Issue:** `src/util/sanitize.h`'s `sanitize_for_display` (the project's other T-2-33 escaping choke point) deliberately escapes a literal backslash byte (`0x5C`) as a doubled `"\\"`, and its own header comment explains exactly why: without that, raw text that happens to contain the four literal characters `\`, `x`, `1`, `b` would be visually indistinguishable from this function's own escape of a real ESC byte (`0x1B` → `"\x1b"`). `junit.cpp`'s parallel `xml_escape` fix (this plan's own CR-03/T-2-33 completion work) implements the identical `"\xHH"` escape form for illegal C0/DEL bytes but does **not** implement the matching backslash-doubling step — a literal backslash byte falls through `xml_escape`'s `default:` arm unchanged (`out += static_cast<char>(c);`).

Concretely: a `Finding::message`/`candidate`/`baseline` containing a real ESC byte (`0x1B`) and one containing the four literal ASCII bytes `\`, `x`, `1`, `b` both render to the identical XML text `\x1b` in the emitted JUnit report — a reader (or a script trying to detect "was this report tampered with / does this file have a raw control byte") cannot tell the two apart, exactly the failure mode `sanitize_for_display`'s own design comment calls out as the reason it exists. This is not an XML-injection risk (the four metacharacter substitutions still hold, so no markup escapes), but it is a real correctness gap relative to the stated design intent, and the file's own top-of-file comment claims stronger parity with `sanitize_for_display` than the implementation delivers ("matching the escape form src/util/sanitize.cpp's sanitize_for_display already uses for the same class of byte"). No test in `tests/unit/test_junit.cpp`'s new CR-03 suite exercises a literal backslash byte, so this gap is currently unguarded by any regression test.

**Fix:** Add a `case '\\':` arm to `xml_escape` that doubles the backslash, mirroring `sanitize_for_display`:
```cpp
case '\\':
  out += "\\\\";
  break;
```
and add a test case asserting that a `Finding::message` containing a literal backslash byte round-trips distinguishably from one containing an actual ESC byte (e.g. assert the two renders differ, or assert `count of "\x5c"`-style disambiguation), matching the existing CR-03 test suite's own thoroughness for the C0/DEL cases.

## Info

### IN-01: `resolve_probe_timeout_ms`/`resolve_probe_memory_budget_mb` re-parse CLI11-validated text with `std::stoll`, duplicating validation already performed by `->check()`

**File:** `src/cli/options.cpp:309-361`

**Issue:** Both resolvers re-parse `opt_string(args.timeout_seconds)` / `opt_string(args.memory_budget_mb)` via `std::stoll` even though CLI11's own `->check(CLI::NonNegativeNumber)`/`->check(CLI::PositiveNumber)`/`->check(CLI::Range(...))` chain (registered in `add_probe_flags`) has already validated and bounded the value by the time this code runs — both `catch` blocks are explicitly documented as "unreachable in practice." This is intentional defense-in-depth per the code's own comments (mirroring `resolve_profile_selection`'s "re-validate, don't trust blindly" convention) and is not a defect, but it is duplicated logic with no test able to exercise the catch branches, which is worth noting as a maintenance cost rather than acting on.

**Fix:** No action needed; documenting for completeness. If this pattern is revisited, consider whether CLI11's own typed `->transform()`/bound-variable form could eliminate the re-parse rather than re-validating text a second time.

---

_Reviewed: 2026-09-05T07:23:09Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
