---
phase: 02-core-engine
reviewed: 2026-08-15T20:31:28Z
depth: standard
files_reviewed: 73
files_reviewed_list:
  - CMakeLists.txt
  - scripts/lint_check_id_strings.sh
  - src/cli/color_policy.cpp
  - src/cli/color_policy.h
  - src/cli/commands/compare.cpp
  - src/cli/commands/compare.h
  - src/cli/commands/dir.cpp
  - src/cli/commands/dir.h
  - src/cli/commands/explain.cpp
  - src/cli/commands/explain.h
  - src/cli/commands/inspect.cpp
  - src/cli/commands/inspect.h
  - src/cli/commands/list_checks.cpp
  - src/cli/commands/list_checks.h
  - src/cli/commands/snapshot.cpp
  - src/cli/commands/snapshot.h
  - src/cli/dir_pairing.cpp
  - src/cli/dir_pairing.h
  - src/cli/exit_code.cpp
  - src/cli/exit_code.h
  - src/cli/main.cpp
  - src/cli/options.cpp
  - src/cli/options.h
  - src/cli/provenance_render.cpp
  - src/cli/provenance_render.h
  - src/cli/tty_render.cpp
  - src/cli/tty_render.h
  - src/cli/worker_pool.cpp
  - src/cli/worker_pool.h
  - src/compare/dist.cpp
  - src/compare/engine.cpp
  - src/compare/engine.h
  - src/compare/exact.cpp
  - src/compare/hash.cpp
  - src/compare/presence.cpp
  - src/compare/semantics.h
  - src/compare/set.cpp
  - src/compare/span.cpp
  - src/compare/tol.cpp
  - src/config/toml_load.cpp
  - src/config/toml_load.h
  - src/core/check_explain.h
  - src/core/checks.def
  - src/core/error.h
  - src/core/glob.cpp
  - src/core/glob.h
  - src/core/model.h
  - src/core/policy.cpp
  - src/core/policy.h
  - src/core/profiles.cpp
  - src/core/profiles.h
  - src/core/rational.h
  - src/core/registry.h
  - src/core/serializer.cpp
  - src/core/serializer.h
  - src/core/snapshot.cpp
  - src/core/snapshot.h
  - src/core/tolerance.cpp
  - src/core/tolerance.h
  - src/core/value.h
  - src/report/json.cpp
  - src/report/json.h
  - src/report/junit.cpp
  - src/report/junit.h
  - src/report/markdown.cpp
  - src/report/markdown.h
  - src/report/model.cpp
  - src/report/model.h
  - src/util/fs.h
  - src/util/version.cpp
  - src/util/version.h
  - tools/gen_registry.py
  - vcpkg.json
findings:
  critical: 3
  warning: 3
  info: 1
  total: 7
status: issues_found
---

# Phase 02: Code Review Report

**Reviewed:** 2026-08-15T20:31:28Z
**Depth:** standard
**Files Reviewed:** 73
**Status:** issues_found

## Summary

The compare engine, registry, policy resolution, snapshot I/O, report renderers and CLI surface are structurally sound and mostly match the design decisions recorded in `02-CONTEXT.md` (no-default-arm switches, rational cross-multiplication instead of float, `expected<T,E>` threaded consistently through the public APIs, symlink-safe directory walk, ordered/sorted output for determinism). However, three defects break hard project constraints:

1. `core/serializer.cpp::value_from_json` and `core/snapshot.cpp::input_identity_from_json` accept a syntactically-valid but type-mismatched `.snap.json` (an untrusted input per the project's own threat model) and call nlohmann's `.get<T>()` without checking the node's type first. This throws `nlohmann::json::type_error`, which is **never caught** on the single-file `compare`/`snapshot`/`inspect` paths (crash via `std::terminate`) and is **silently swallowed** by `WorkerPool::run_indexed`'s `catch (...)` on the `dir` path — which converts a corrupt/malformed snapshot into an apparent zero-finding "clean" file in the corpus report. This is exactly the class of failure PROJECT.md calls the worst possible outcome for a CI gate, just in its inverted (silent-pass) form.
2. `report/json.cpp::render_json` (both overloads) calls `nlohmann::ordered_json::dump(2)` directly on the assembled report document instead of routing through `core/serializer.cpp::serialize_document`'s `std::to_chars`-based writer. Any `rational`- or `real`-valued check's baseline/candidate embeds a `double` (the `RationalValue`→JSON "ms" convenience field, or a raw `real` value), which is then formatted by nlohmann's own float-to-text algorithm — a second float formatter, which is precisely what D-08 and the codebase's own comment in `serializer.cpp` (lines 40-43) name as forbidden. `report/junit.cpp`, `src/cli/tty_render.cpp` and `src/cli/commands/inspect.cpp` have the same pattern for their own value-rendering.
3. Cross-multiplication in `compare/tol.cpp` and `compare/dist.cpp` operates on `std::int64_t` magnitudes read directly from an untrusted snapshot's `RationalValue`/`Histogram` fields with no range validation, so a crafted large `num`/`den`/bin-count triggers signed integer overflow (UB) in the tolerance/distribution decision path.

Two further Warnings (unchecked digit-accumulation overflow in tolerance/resolution-expectation parsing; `NO_COLOR` losing to `GITHUB_ACTIONS` in the color-policy precedence) and one Info item round out the findings below.

## Critical Issues

### CR-01: Untrusted-snapshot type mismatches throw uncaught exceptions — crash on `compare`/`snapshot`, silent false "pass" on `dir`

**File:** `src/core/serializer.cpp:257-261, 300, 325-333` and `src/core/snapshot.cpp:90-92`
**Issue:** `value_from_json`'s `rational`, `histogram` and `hash_chain` branches, and `input_identity_from_json`, only check `.contains(key)` before calling `.at(key).get<std::int64_t>()` / `.get<std::string>()`. They never check the node's *type* first (contrast with the `int64`/`real`/`string`/`string_set` branches, which correctly call `is_number_integer()`/`is_number()`/`is_string()` before `.get<>()`). A `.snap.json` such as:

```json
{"schema_version":"1.0","tool_version":"x",
 "measurements":[{"id":"t.tol_ms","scope":{"kind":"global","index":0},
                  "value":{"num":"not-a-number","den":10,"tb":{"num":1,"den":1}}}]}
```
causes `json.at("num").get<std::int64_t>()` (`serializer.cpp:258`) to throw `nlohmann::json::type_error` (code 302).

This is thrown from inside `read_snapshot`, called by:
- `src/cli/commands/compare.cpp:132/138` (`run_compare`) — no `try`/`catch` anywhere between here and `main()`. `src/cli/main.cpp:82-86` only catches `CLI::ParseError`; CLI11's `App::run_callback` (vcpkg `CLI/impl/App_inl.hpp:1111`) does not wrap the callback in `try`/`catch` either. Result: **uncaught exception, `std::terminate()`, process crash** on a media-processing CLI whose entire premise is "zero setup, safe to point at CI-supplied files."
- `src/cli/commands/snapshot.cpp:229` (`snapshot` subcommand) — same crash.
- `src/cli/commands/dir.cpp:306/311` (`dir`'s per-file job, run inside `WorkerPool::run_indexed`) — here the exception IS caught, but by `src/cli/worker_pool.cpp:26-35`'s blanket `catch (...) { /* deliberately empty */ }`. Because the throw happens *before* `outcomes[i].hard_error` or `results[i].findings` are ever written, that file's `JobOutcome` stays at its default (`hard_error = nullopt`, `partial = false`) and `results[i].findings` stays empty. `dir.cpp:372-381`'s exit-code decision only inspects `outcome.hard_error`/`outcome.partial`, both false/unset — so a directory containing one corrupted/malformed snapshot is reported as a **clean pass with zero findings for that file**, with no diagnostic line, no error, no crash. This is a false negative in the tool's primary CI-gate use case: a corrupt or adversarially-crafted candidate snapshot silently stops being compared at all instead of surfacing an error, defeating the "every real regression is caught" guarantee this project exists to provide.

**Fix:** Add type checks symmetric with the already-correct branches, e.g.:
```cpp
// serializer.cpp, ValueKind::rational
const auto& tb = json.at("tb");
if (!tb.is_object() || !tb.contains("num") || !tb.contains("den") ||
    !json.at("num").is_number_integer() || !json.at("den").is_number_integer() ||
    !tb.at("num").is_number_integer() || !tb.at("den").is_number_integer()) {
  return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "rational value has non-integer num/den/tb"});
}
```
Apply the same pattern to the `histogram` bin's `"bin"`/`"count"`, `hash_chain`'s `"algorithm"`/`"digest"`/`"element_count"`, and `input_identity_from_json`'s `"basename"`/`"size_bytes"`/`"xxh3_128"`. Separately, `main.cpp`/`wmain` should wrap `mediadiff::run(...)` in a `catch (const std::exception&)` that reports `ErrorKind::internal` and exits `kExitInternal` as defense in depth, and `WorkerPool::run_indexed`'s swallowed exception must record a hard error into the per-index result (not silently leave it as "nothing happened") so `dir` mode never reports a file as clean when it never actually compared.

### CR-02: `--json` report float formatting bypasses the canonical `std::to_chars` serializer, breaking the determinism contract

**File:** `src/report/json.cpp:210, 243`; secondary occurrences at `src/report/junit.cpp:74-75`, `src/cli/tty_render.cpp:246-247`, `src/cli/commands/inspect.cpp:69, 126`
**Issue:** `render_json` builds the whole `nlohmann::ordered_json report` document and returns `report.dump(2)` (lines 210 and 243) — nlohmann's own `dump()`, not `core/serializer.cpp::serialize_document`. `finding_to_json` embeds `value_to_json(finding.baseline)`/`value_to_json(finding.candidate)` into that document; for any `rational`-valued check (i.e. any `tol`-semantic check, which is the majority of timing/drift checks this project is built around) `value_to_json` emits a `RationalValue`'s derived `"ms"` field as a raw `double` (`core/serializer.cpp:29-30`), and for a `real`-valued check it emits a raw `double` directly. Both then get text-formatted by nlohmann's own float-to-string algorithm inside `dump(2)`, not by `serialize_document`'s single `std::to_chars` call site.

This is precisely the anti-pattern the codebase's own comment in `core/serializer.cpp:40-43` warns against by name: *"any later call to nlohmann's own `dump()` on the containing document would have reformatted it via nlohmann's own algorithm anyway — reintroducing exactly the 'second float formatter' D-08 forbids."* That warning was heeded inside `write_snapshot` (which correctly calls `serialize_document`), but not inside `render_json`, which is the function that actually produces the `--json` report — the artifact PROJECT.md's determinism contract ("`--json` output must be byte-identical across identical runs" AND "a check that jitters is a bug, not a tolerance problem") explicitly names. Two independent float formatters for the same `Value` type is itself a correctness risk (they are not guaranteed to agree bit-for-bit on every double, e.g. `-0.0`, subnormals, or values near an exponent boundary), and ties determinism of the flagship report format to nlohmann's internal formatter rather than the one function this project designated as canonical.

**Fix:** Route `render_json`'s final text production through `serialize_document(report)` instead of `report.dump(2)`:
```cpp
// src/report/json.cpp
return serialize_document(report);
```
(`serialize_document` already produces valid, readable JSON text with a trailing newline; verify/update `tests/baseline/report-1.0.json` and the JSON-report determinism/golden tests accordingly.) Apply the same fix to `junit.cpp`'s `baseline_candidate_detail` and `tty_render.cpp`'s finding-detail formatting, both of which also call `.dump()` on a `Value`-derived JSON node that can contain a `double`.

### CR-03: Unbounded integer cross-multiplication on untrusted snapshot magnitudes — signed overflow (UB) in the tolerance/distribution decision path

**File:** `src/compare/tol.cpp:97-98, 108-113, 115-118`; `src/compare/dist.cpp:96-98, 100, 117`
**Issue:** `RationalValue::num`/`den` (`core/value.h:41-42`) and `Histogram` bin counts (`core/value.h:51`) are plain `std::int64_t` read straight out of an untrusted `.snap.json` via `value_from_json` with no range check (see CR-01 — even once type-checked, magnitude is never bounded). `compare_tol` then computes, unchecked:
```cpp
const std::int64_t delta_den = baseline_mag->den * candidate_mag->den;
const std::int64_t delta_num = candidate_mag->num * baseline_mag->den - baseline_mag->num * candidate_mag->den;
...
within_fail = abs_delta_num * tolerance->den * 100 <= tolerance->num * abs_baseline_num * candidate_mag->den;
```
and `compare_dist` computes:
```cpp
const std::int64_t diff_num = a_count * b_total - b_count * a_total;
...
const bool within_tolerance = worst_num * tolerance->den * 100 <= tolerance->num * worst_denom;
```
None of these multiplications use `core/rational.h`'s own `detail::checked_mul` (which the codebase demonstrably has and uses for exactly this purpose in `compare_ticks`) — every one is a plain `*` on `std::int64_t`. A candidate/baseline snapshot with e.g. `"num": 9223372036854775000` (near `INT64_MAX`, legal per the type check, no magnitude check at all) triggers signed integer overflow, which is undefined behavior in C++ and — depending on compiler optimization — can produce an arbitrary pass/fail/warn verdict, not merely a wrong-but-defined one. This is a false-verdict risk directly reachable from an untrusted candidate snapshot on the exact class of check (`tol`/`dist`) the project's own priorities flag as the highest-consequence path for silent false positives/negatives.

**Fix:** Reuse `core/rational.h::detail::checked_mul` (or an equivalent overflow-checked multiply) for every cross-multiplication in `compare_tol`/`compare_dist`, and on overflow return a `Finding` with `Status::error` (not a fabricated pass/warn/fail) — mirroring `value_kind_mismatch`'s own "never a coercion" contract in `compare/engine.cpp`. At minimum, validate `num`/`den`/bin counts against a documented sane bound at snapshot-read time (`value_from_json`) so a malformed or adversarial snapshot is rejected with `ErrorKind::input_unsupported` before ever reaching a comparator.

## Warnings

### WR-01: Digit-by-digit magnitude parsing has no overflow guard

**File:** `src/core/tolerance.cpp:126-132, 137-141`; `src/core/profiles.cpp:51-58, 91-98, 102-110`
**Issue:** `parse_magnitude`'s `int_value = int_value * 10 + digit` loop (tolerance grammar) and `parse_leading_int`/`parse_resolution_expectation`'s equivalent loops (transform scale-factor grammar) accumulate into `std::int64_t` with no digit-count cap and no overflow check. A `mediadiff.toml` `[tolerance]` entry or `--tol` value such as `"99999999999999999999ms"`, or a `[transform] expect.resolution` string with an absurdly long digit run, silently wraps (signed overflow, UB) into an arbitrary tolerance/resolution rather than being rejected as malformed input.
**Fix:** Cap accepted digit count (e.g. 18 digits, safely under `int64` range) or use `core/rational.h::detail::checked_mul`/an add-overflow check in the accumulation loop, returning `usage_error(...)` / `ErrorKind::usage` on overflow instead of silently wrapping.

### WR-02: `NO_COLOR` is overridden by `GITHUB_ACTIONS=true`, contrary to its own spec and the project's stated precedence

**File:** `src/cli/color_policy.cpp:5-19`
**Issue:** `decide_color` checks `inputs.github_actions == "true"` (forcing color on) *before* checking `inputs.no_color.has_value()` (forcing color off):
```cpp
} else if (inputs.github_actions.has_value() && *inputs.github_actions == "true") {
  color_enabled = true;
} else if (inputs.no_color.has_value()) {
  color_enabled = false;
```
So a job that sets both `GITHUB_ACTIONS=true` (set automatically by every GitHub Actions runner) and `NO_COLOR=1` (an explicit, deliberate user/operator request per the NO_COLOR convention this project claims to honor) gets color output anyway. `claude_docs/00-design-and-requirements.md:75` frames the GitHub Actions carve-out as keeping color for the *default auto-disabled-when-not-a-tty* case, not as an override of an explicit `NO_COLOR` request — the current ordering does the latter.
**Fix:** Check `flag_no_color` and `no_color` before `github_actions`:
```cpp
if (inputs.flag_no_color || inputs.no_color.has_value()) {
  color_enabled = false;
} else if (inputs.github_actions.has_value() && *inputs.github_actions == "true") {
  color_enabled = true;
} else if (inputs.ci.has_value() && *inputs.ci == "true") {
  color_enabled = false;
} else {
  color_enabled = inputs.stdout_is_tty;
}
```

### WR-03: `compare_ticks`'s overflow fallback silently treats unequal same-sign values as equal in a real comparison path, not just display

**File:** `src/core/rational.h:77-93`; consumed for real decisions in `src/compare/span.cpp:31-35, 51-71`
**Issue:** `rational.h`'s own comment documents the overflow fallback ("falls back to comparing sign only... this is a documented, deliberate limitation") as acceptable because it believed the only consumer was `compare_tol`'s cosmetic `+`/`-` sign. But `compare/span.cpp` also calls `compare_ticks` (via `ticks_less`/`ticks_less_equal`) to *sort*, *merge*, and decide *overlap* between spans — i.e., to decide whether a candidate span is "introduced" (gates the check) or matches baseline. On overflow, two same-signed but genuinely different tick values compare as equal (function returns `0`), which can cause `merge_spans` to merge spans that should not merge, or `overlaps` to report/miss an overlap incorrectly — silently producing a wrong pass/fail verdict for a `span` check rather than the documented "acceptable for a +/- display glyph" limitation.
**Fix:** Either propagate an overflow signal out of `compare_ticks` so `compare_span` can degrade to `Status::error`/`skipped` (matching the project's "never fabricate a verdict" rule) instead of silently continuing, or bound the values `Span`/`RationalValue` may legally carry at snapshot-read time so the cross-multiplication in `compare_ticks` cannot overflow for any value that passed validation.

## Info

### IN-01: TOCTOU window between the existence/git-tracked check and the actual write in the snapshot safe-write gate

**File:** `src/cli/commands/snapshot.cpp:181-202`
**Issue:** `write_snapshot_gated` calls `file_exists_utf8(out_path)` and (conditionally) `is_git_tracked(out_path)` (which itself spawns `git` and waits — a window on the order of milliseconds) before calling `write_snapshot`, which performs its own `fopen_utf8`/rename. Between the check and the write, `out_path` could be replaced (e.g. by a symlink) by another process, so the "refuse to overwrite a tracked snapshot" gate can be bypassed by a concurrent actor. Low real-world impact for a single-user CLI tool typically run in CI, but worth noting since the project's own priorities call out TOCTOU in this exact function.
**Fix:** Not urgent given the tool's threat model (local/CI single-invocation use), but if hardening is desired, open `out_path` with `O_EXCL`-equivalent semantics (or `_wsopen_s`/appropriate exclusive-open flags on Windows) as part of the gate rather than a separate existence probe.

---

_Reviewed: 2026-08-15T20:31:28Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
