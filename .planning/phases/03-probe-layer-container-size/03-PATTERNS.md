# Phase 3: Probe Layer, Container & Size - Pattern Map

**Mapped:** 2026-09-02
**Files analyzed:** ~20 new/modified files (probe layer, analyzers, seam, SkipReason extension, CLI/registry glue, fixtures, goldens, tests)
**Analogs found:** 13 / 20 (the rest are genuinely greenfield — see "No Analog Found")

## Overview

This phase is unusual: `src/probe/` and `src/analyzers/*` are `.gitkeep`-only, so most new
production code has **no direct analog** in this codebase — it is the first code to touch
real media via FFmpeg. Rather than force a weak match, this file names the nearest convention
to imitate for each new file and is explicit where nothing precedes it. The effort below is
concentrated on the eight areas RESEARCH.md and CONTEXT.md flagged as having real, extractable
precedent: the `expected<T,Error>` boundary shape, the generated-registry pattern for adding a
check family, the `SkipReason` extension site (a hard compile-time prerequisite), rational/tick
arithmetic, the worker-pool concurrency model, fixture generation, golden comparison, and
self-testing lint scripts.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/probe/demux_session.{h,cpp}` | service (probe boundary) | file-I/O, request-response | `src/config/toml_load.cpp` (`read_whole_file` + `expected` boundary) | role-match (fallible file-opening boundary), no libav precedent |
| `src/probe/packet_scan.{h,cpp}` | service (probe boundary) | streaming (bounded sweep) | `src/cli/worker_pool.cpp` (bounded, single-pass iteration) + `core/model.h`'s `Fingerprint.partial` | partial-match (concurrency/ownership only; no scanning precedent) |
| `src/probe/bmff_scan.{h,cpp}`, `ebml_scan.{h,cpp}`, `ts_scan.{h,cpp}` | service (raw byte scanner) | transform (binary parse) | none in `src/`; nearest *convention* is `core/tolerance.cpp`'s hand-rolled ASCII/byte-level parser | no analog — see below |
| `src/probe/pass.h` | model/seam declaration | event-driven (declare-then-union) | `src/core/registry.h` (`CheckDef`/`CheckRegistry` — declared-then-looked-up shape) | partial-match (nearest "declare a capability, look it up centrally" shape) |
| `src/analyzers/container/{topology,meta,mp4,mkv,ts}.cpp` | analyzer (check emitter) | transform (probe result → `Measurement`) | `src/compare/tol.cpp` (consumes `Value`/`Ticks`, emits typed results) + `checks.def` entries for `meta.*` | role-match for the emit shape; no existing analyzer file to copy wholesale |
| `src/analyzers/size/size.cpp` | analyzer (check emitter) | transform + windowed aggregation | `src/compare/tol.cpp` (rational arithmetic, checked helpers) | strong match for the arithmetic; no windowing precedent |
| `src/core/checks.def` (extend) | config | CRUD (declarative registration) | itself — `[[check]] id = "meta.tool_version"` entry (lines 45-59) | exact — this is literally the template |
| `tools/gen_registry.py` (no code change expected, only new `docs/checks/*.md` + `checks.def` entries feed it) | config generator | batch/codegen | itself | exact — unmodified, just fed more input |
| `docs/checks/container.*.md`, `docs/checks/size.*.md` | docs (build-gating) | — | `docs/checks/meta.tool_version.md` (or whichever `meta.*` doc exists) | exact — required heading shape enforced by `gen_registry.py` |
| `src/core/model.h` (extend `SkipReason`) | model | — | itself, in place — add `partial_scan`, `insufficient_data` (and a timing-absent value if SIZE-01 needs one) next to the 11 existing enumerators (lines 39-51) | exact — same enum, same file |
| `src/report/json.cpp` (`skip_reason_to_string`, lines 34-60) | transform (serializer) | request-response | itself — every existing `case SkipReason::x:` arm | exact — add two/three new arms, same pattern, **no `default:`** |
| `src/report/junit.cpp` (`skip_reason_text`, lines 17-43) | transform (serializer) | request-response | itself — mirrors `report/json.cpp`'s switch exactly | exact — same obligation |
| `src/core/rational.h` (extend, if `checked_div` is added) | utility | transform (pure math) | itself — `checked_mul`/`checked_sub`/`checked_add`/`checked_negate` (lines 34-101) | exact — same file, same two-branch-overflow-check style |
| `src/cli/commands/{snapshot,compare,dir,inspect}.cpp` (extend `read_snapshot` call sites) | controller (CLI command) | request-response | themselves — the four existing `read_snapshot(path, registry)` call sites | exact — additive fallback wrapper, same call shape |
| `src/cli/commands/inspect.cpp` (add container section render) | controller (CLI command) | request-response | itself, `inspect.cpp:140-170` (the `file_path`/`CliOptions`/`read_snapshot` shape) | exact for CLI wiring; container-section rendering itself has no precedent |
| any new CLI flags this phase adds (e.g. memory-budget knob, wall-clock timeout) | CLI option registration | request-response | `src/cli/options.{h,cpp}`'s `add_policy_flags`/`PolicyArgs` (D-05 borrowed-`Option*` pattern) | exact — this is the mandated current convention |
| `scripts/gen_corpus.sh` (add Phase-3 fixture recipes) | build script | batch | itself — the manifest-writing skeleton (lines 1-91) and its documented recipe convention (`-flags +bitexact -fflags +bitexact -y -f lavfi ...`) | exact — same script, additive recipes only |
| `tests/golden/ts_scan_*.txt` + capture jig | test fixture / golden | batch (compare-only) | `tests/support/golden.{h,cpp}` + `tests/unit/test_golden.cpp` | exact — reuse `check_golden`/`UPDATE_GOLDENS` verbatim |
| a new lint script gating TSDuck-golden or `SkipReason` completeness (if planner adds one) | lint (build gate) | batch, self-testing | `scripts/lint_dead_code_after_fail.sh`, `scripts/lint_fixture_case_collisions.sh` | exact — both share the same self-test-first, zero-file-guard, documented-limitation shape |
| `src/util/fs.h` (extend for probe UTF-8→wide path shim, PROBE-01) | utility | transform | itself — `utf8_to_wide`/`getenv_utf8`/`rename_replace_utf8` already there | exact — same file, additive function |

## Pattern Assignments

### `expected<T, Error>` at a fallible boundary

**Analogs:** `src/core/snapshot.cpp::read_whole_file` (lines 15-33), `src/config/toml_load.cpp::read_whole_file` (lines 31-46), `src/compare/tol.cpp` (checked-arithmetic error paths).

**Shape to copy** (from `src/core/snapshot.cpp:15-33`):
```cpp
mediadiff::expected<std::string, Error> read_whole_file(const std::string& utf8_path) {
  FILE* handle = fopen_utf8(utf8_path, "rb");
  if (handle == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "could not open snapshot file: " + utf8_path});
  }
  std::string content;
  char buf[8192];
  std::size_t read_bytes = 0;
  while ((read_bytes = std::fread(buf, 1, sizeof(buf), handle)) > 0) {
    content.append(buf, read_bytes);
  }
  const bool had_error = std::ferror(handle) != 0;
  std::fclose(handle);
  if (had_error) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "error reading snapshot file: " + utf8_path});
  }
  return content;
}
```
**Rules to carry into `DemuxSession`/`PacketScan`/the three scanners:**
- Return `mediadiff::expected<T, Error>` from every entry point, never throw across the lib boundary.
- `Error{ErrorKind::X, "message naming the offending path/field"}` — always name the concrete input in the message, matching `toml_load.cpp`'s `"config file '" + std::string(path) + "' could not be opened"` shape.
- `ErrorKind::input_open` for "could not open/read the bytes at all"; `ErrorKind::input_unsupported` for "opened fine but the content is not what this parser understands" (see `snapshot.cpp::parse_scope_kind`, line 43: `unexpected(Error{ErrorKind::input_unsupported, "unknown scope kind: " + kind_str})`). This is exactly the distinction RESEARCH.md's `fingerprint_input` fallback needs to make between "file didn't open" and "not a valid snapshot" before falling through to the probe path.
- `usage_error` local lambda idiom (`toml_load.cpp:23-25`, `tolerance.cpp`) — a small file-local helper that constructs `unexpected(Error{ErrorKind::usage, ...})` is the established way to keep call sites terse; probe code constructing `ErrorKind::input_unsupported`/`input_open` repeatedly should do the same.

### Adding a check family to `checks.def` + the generated registry

**Analog:** `src/core/checks.def` itself — the `meta.tool_version` entry (lines 45-59) and the file's own header comment (lines 1-36) documenting every key.

**Template to copy verbatim per new check id:**
```toml
[[check]]
id = "container.mp4.faststart"      # example — real ids per doc 02 §3
group = "container"
semantic = "presence"                # or exact/tol/set/hash/dist/span per doc 02's table
unit = "none"
value_kind = "string"                # per doc 02's declared value_kind for this check
severity = "warn"
# optional: tolerance = "..."; volatile = true; requires_pass = true
```
**Mandatory companion file, or the build fails:** `docs/checks/<id>.md` with the three required
level-2 headings (`## What it measures`, `## Why it matters`, `## Accept / Tune / Silence`) and,
within the third, the three level-3 sub-headings (`### Accept`, `### Tune`, `### Silence`) —
enforced by `tools/gen_registry.py` (`REQUIRED_DOC_HEADINGS`, `REQUIRED_ATS_SUBHEADINGS`, read
in full this session). `gen_registry.py` itself needs **no code change** — it is a generic
generator; new families just add rows and docs.

**Also enforced (fail-first coverage, D-14/D-15/DOC-03):** every new id needs at least one
passing and one triggering fixture, or the coverage gate fails the build — this is the existing
Phase-2 mechanism, not new machinery.

### Extending `SkipReason` — the confirmed compile-time trap

**Files that MUST change together** (verified this session, matches RESEARCH.md exactly):
1. `src/core/model.h:39-51` — add `partial_scan`, `insufficient_data` (D-02, SIZE-01's "no dts"
   case may need a third, e.g. `no_timing_data`) to the `enum class SkipReason { ... }` list.
2. `src/report/json.cpp:34-60` — `skip_reason_to_string(SkipReason reason)`: add matching
   `case SkipReason::partial_scan: return "partial_scan";` arms. **No `default:` arm exists** —
   the switch is exhaustive by design so a missing case is a compile error, not a silent gap.
3. `src/report/junit.cpp:17-43` — `skip_reason_text(SkipReason reason)`: identical obligation,
   identical shape, same missing-`default:` idiom.

**Exact current shape to extend (`report/json.cpp:34-60`):**
```cpp
std::string_view skip_reason_to_string(SkipReason reason) {
  switch (reason) {
    case SkipReason::none:
      return "none";
    // ... 9 more existing arms ...
    case SkipReason::no_prior_release:
      return "no_prior_release";
  }
  return "none";
}
```
Add new arms in the same style (snake_case string identical to the enumerator name), keep the
post-switch `return "none";` as the (belt-and-braces, MSVC-flow-analysis-friendly) fallback —
`src/core/snapshot.cpp:63-68`'s `scope_kind_to_string` documents exactly why this shape exists
("Unreachable for any valid Scope::Kind — see src/cli/exit_code.h's own no-default:-arm-plus-
trailing-return pattern for why this shape").

**This must be a Wave-0/early planning task** — every `size.*`/`container.ts.*` check that skips
under D-02/D-03 cannot compile until this lands.

### Rational/tick arithmetic (SIZE-01 windowing)

**Analog:** `src/compare/tol.cpp` (lines ~92-190), the only production caller of
`core/rational.h`'s checked helpers today.

**Patterns to copy:**
```cpp
// Ordering / delta sign — cosmetic only, tolerates overflow via sign-only fallback:
compare_ticks(Ticks{candidate_mag->num, candidate_mag->tb}, Ticks{baseline_mag->num, baseline_mag->tb});

// A real verdict-affecting comparison MUST use the checked variant instead
// (see rational.h's own WR-03 comment — compare_ticks_checked, not compare_ticks,
// for anything that decides pass/fail/merge/overlap):
TickOrder order = compare_ticks_checked(a, b);
if (order.overflowed) { /* treat as skipped/insufficient_data, never fabricate an answer */ }

// Checked cross-multiplication chains, always checking the bool return before using *out:
std::int64_t delta_den = 0;
if (!detail::checked_mul(baseline_mag->den, candidate_mag->den, &delta_den)) {
  // overflow path — tol.cpp treats this as "cannot resolve numerically"
}
```
**For SIZE-01's window-boundary math specifically:** `rational.h` has `checked_mul`/`checked_add`/
`checked_sub`/`checked_negate` but **no `checked_div`** (confirmed by reading the whole file this
session). RESEARCH.md's Open Question 2 recommends adding `checked_div` to `rational.h` alongside
the other four, in the same two-branch-no-widening-trick style, rather than an inline one-off in
`size.cpp` — follow the existing four's doc-comment convention (each explains *which real caller*
needed it and *why the naive version is unsafe*) if adding it.

**Never**: convert to `double`/float for any comparison feeding a verdict — this is `rational.h`'s
own stated reason for existing (`"30000/1001 vs 29.97 is exactly the trap a double comparison
falls into"`).

### Concurrency (memory budget / thread-count integration)

**Analog:** `src/cli/worker_pool.cpp` (full file, 76 lines) — `WorkerPool::run_indexed`.

**Key properties to design D-01's per-file budget against:**
- One job = one whole file, run start-to-finish on one thread (`run_one(index)`, lines 22-34) —
  a single file's `PacketScan` result never needs cross-thread synchronization; produced and
  consumed within one worker's call stack (matches RESEARCH.md Q3's ownership recommendation).
- `worker_count = min(thread_count_, job_count)` (line 51) — actual OS thread count created.
- `thread_count_ <= 1` runs everything on the calling thread, in submission order (lines 39-46) —
  any budget/log-attribution code must behave correctly in this no-`std::thread` branch too.
- Per-job exceptions are swallowed at the pool boundary (`run_one`'s `try/catch(...)`, lines
  25-33) with an explicit comment on why — a probe/analyzer error must reach the caller through
  the `Fingerprint`/per-file result, not by throwing, since nothing above this catch will see it.
- `dir.cpp:264-279`'s thread-count resolution ladder clamps only the hardware-concurrency
  default path (`kMaxDefaultThreads`) — RESEARCH.md confirms an explicit `--threads N`/`[dir]
  threads` value is unclamped (T-2-41 residual); if the planner closes this, the one-line clamp
  addition belongs in this same ladder in `dir.cpp`, reusing `kMaxDefaultThreads` as the ceiling.

### Fixture generation

**Analog:** `scripts/gen_corpus.sh` (full file read) — the manifest-writing skeleton and its own
documented recipe convention.

**Template for new recipes** (per the script's own comment, lines ~90-95):
```bash
"$FFMPEG_BIN" -flags +bitexact -fflags +bitexact -y \
  -f lavfi -i <source-filter> ... "$OUT_DIR/<fixture-name>.<ext>"
```
- The manifest (`generator`, `configuration`, `generated_at` — fixed key order, `generated_at`
  the only field allowed to differ run-to-run) is already wired; new recipes append fixture-
  producing commands, they do not touch the manifest logic.
- `tests/fixtures/GENERATOR_MANIFEST.json` (present, untracked per git status) is the artifact
  this script produces — Phase 3's new recipes (faststart on/off, fragmented MP4, elst variants,
  MKV Cues placement, Opus CodecDelay, single/multi-program TS, 204-byte TS per CONTEXT.md §8)
  are additive entries in the same script, same convention.

### Golden comparison (TRUST-09 / D-04)

**Analog:** `tests/support/golden.{h,cpp}` (both read in full) + `tests/unit/test_golden.cpp`.

**Reuse verbatim, do not reinvent:**
```cpp
// tests/support/golden.h's public surface:
std::optional<std::string> golden_check_result(std::string_view case_name, std::string_view actual);
void check_golden(std::string_view case_name, std::string_view actual);
```
- Case-name convention to extend: `ts_scan_<fixture>`, comparing against
  `tests/golden/ts_scan_<fixture>.txt` (mirrors the existing `golden_path` helper,
  `golden.cpp:29`).
- `UPDATE_GOLDENS` gate (`golden.cpp:20-24`): unset or empty are BOTH "not set" — deliberately
  strict (`"UPDATE_GOLDENS=" on a CI runner that merely exports the variable name... must not
  silently re-enable the refresh path"`). A missing golden under `UPDATE_GOLDENS` unset is a
  hard failure, never an implicit create.
- Diff reporting: `FirstDiff`/`first_diff` (`golden.cpp:39-66`) — a line-based first-divergence
  report; new TSDuck-vs-`ts_scan` comparisons should reuse this exact diagnostic shape rather
  than inventing a byte-diff.
- Per RESEARCH.md Q5: diff the small **extraction adapter's canonical JSON** (per-PID counts, CC
  error counts, PAT/PMT presence+version, PCR presence), never the full `tsanalyze --normalized`
  dump — write that extracted JSON as the golden text, same mechanism, narrower content.

### Lint scripts with self-test control clauses

**Analogs:** `scripts/lint_dead_code_after_fail.sh`, `scripts/lint_fixture_case_collisions.sh`
(both read in full).

**Shared shape any new gate (e.g. a `SkipReason`-completeness lint, or a TSDuck-golden-presence
lint) should follow:**
- `set -euo pipefail` at the top.
- A documented **known limitation** section stated deliberately up front (both scripts do this —
  "this is a line-based scan, not a C++ tokenizer" / "not a tokenizer or dataflow analysis") —
  the honesty convention is itself the pattern, not just the mechanism.
- A **zero-file guard**: `lint_dead_code_after_fail.sh:37-42` refuses to report clean if its scan
  target directory doesn't exist, with an explicit message: *"Refusing to scan a shorter list and
  report clean — a gate that scans zero files is not the same as a gate that scanned everything
  and found nothing."* Any new lint must include the equivalent guard.
- An explicit, named escape-valve marker convention for genuine false positives (`// dead-code-
  after-fail-allow`, `fixture-case-allow:`) rather than loosening the scan itself.
- `LC_ALL=C` pinning when the script's correctness depends on ASCII-only behavior
  (`lint_fixture_case_collisions.sh:29-34`) — relevant if any new lint does case-folding or
  byte-level pattern matching on filenames/identifiers.

### CLI option binding (D-05 — current convention, not legacy)

**Analog:** `src/cli/options.{h,cpp}` (`PolicyArgs`/`add_policy_flags`), `src/cli/commands/
compare.cpp:97-113`, `src/cli/commands/inspect.cpp:140-152`.

**Exact shape for any new CLI surface this phase adds** (e.g. a memory-budget flag, a wall-clock
timeout flag):
```cpp
// Registration (compare.cpp:97-99 style):
CLI::Option* baseline_path =
    cmp->add_option("baseline", "Baseline artifact or *.snap.json")->type_name("TEXT")->required();

// Typed numerics keep a bound variable or ->check() (D-05's stated exception —
// untargeted add_option is not templated and drops parse-time type validation):
// e.g. for a --probe-budget-mb value, bind a variable or add ->check(CLI::PositiveNumber).

// Callback captures the raw Option* by value — the App owns it for the whole
// program lifetime (compare.cpp:112-116):
cmp->callback([baseline_path, /* ... */]() {
  run_compare(opt_string(baseline_path), /* ... */);
});
```
- Read accessors are always `opt_string(o)` / `opt_flag(o)` / `opt_strings(o)`
  (`options.cpp:26-33`) — never `o->as<T>()` directly at a call site, and never a
  `shared_ptr<T>`-per-option.
- `opt_strings`'s `count() == 0` guard is **mandatory**, not defensive padding — an unguarded
  `as<std::vector<std::string>>()` on an unset repeatable option returns `{""}`, not `{}`
  (documented landmine, `options.h`'s own comment, cross-checked against CLI11 2.6.2's
  `Option.hpp:735-741`).
- `->type_name("TEXT")` must be restored explicitly for any untargeted string option (the
  untargeted `add_option(name, desc)` overload runs no type inference — `options.cpp`'s own
  comment block documents the exact behavior regression this guards against).
- This binds Phase 3's CLI additions to the **post-D-06-migration** codebase — CONTEXT.md states
  the 38-site `shared_ptr`→`Option*` migration is a prerequisite completed on `main` before this
  phase starts; treat every existing CLI file as already in the new shape.

## Shared Patterns

### Error/fallible-boundary shape
**Source:** `src/util/expected.h` (the alias itself) + `src/core/snapshot.cpp`, `src/config/
toml_load.cpp` (concrete usage).
**Apply to:** every function in `src/probe/*` and `src/analyzers/*` that can fail — all return
`mediadiff::expected<T, Error>`, never throw.

### Checked rational/tick arithmetic
**Source:** `src/core/rational.h`, consumed by `src/compare/tol.cpp`.
**Apply to:** `size.cpp`'s windowing, any DTS/PTS/duration math in `packet_scan.cpp`,
`container.ts.pcr_interval`/`psi_interval`'s mux-rate-estimate math (D-03).

### Registry-driven check declaration
**Source:** `src/core/checks.def` + `tools/gen_registry.py` (unmodified).
**Apply to:** every new `container.*`/`size.*` check id — declare in `checks.def`, write the
matching `docs/checks/<id>.md` with all required headings, or the build fails.

### `SkipReason`-must-not-fabricate-a-verdict
**Source:** `src/core/model.h`'s enum + both report-format exhaustive switches.
**Apply to:** every `size.*` check under D-02 (partial scan), `container.ts.*` under D-03/doc 02
§5 (insufficient data), any check consuming a degraded probe result.

### Borrowed `CLI::Option*` binding
**Source:** `src/cli/options.{h,cpp}` (D-05).
**Apply to:** any new flag this phase's CLI surface adds.

### Golden-file regression gating with deliberate refresh
**Source:** `tests/support/golden.{h,cpp}` (D-12/D-04).
**Apply to:** `ts_scan` vs. TSDuck comparison (TRUST-09).

## No Analog Found

Files/areas with no close match in the codebase — the planner should follow doc 02's own
prescriptive spec (element IDs, walk order, CC carve-outs, per-check semantics — all "Claude's
Discretion" items CONTEXT.md defers to doc 02) rather than an in-repo precedent:

| File | Role | Data Flow | Reason |
|---|---|---|---|
| `src/probe/demux_session.{h,cpp}` | service | request-response (libav session) | First code in the repo to call `avformat_open_input`/`avformat_find_stream_info`; no libav call exists anywhere in `src/` today. |
| `src/probe/packet_scan.{h,cpp}` | service | streaming | First no-decode packet sweep; no analog for a bounded, budget-aware `av_read_frame` loop. |
| `src/probe/bmff_scan.{h,cpp}` | service | transform (binary parse) | First hand-rolled ISO-BMFF box walker; no byte-level container parser exists in `src/` (nearest sibling convention is `core/tolerance.cpp`'s ASCII grammar parser, useful only for "how this codebase writes a hand-rolled parser with clear error messages," not for box-walking itself). |
| `src/probe/ebml_scan.{h,cpp}` | service | transform (binary parse) | First hand-rolled EBML/vint walker; same reasoning as bmff_scan. |
| `src/probe/ts_scan.{h,cpp}` | service | transform (binary parse) | First hand-rolled MPEG-TS/PSI walker (188/204-byte packets, CC tracking); same reasoning. |
| `src/probe/pass.h` + orchestrator union logic (PROBE-08) | seam/model | event-driven | Genuinely new seam — `AnalyzerSpec`/`Pass` enum/self-registering vector has no precedent; `core/registry.h`'s `CheckRegistry` is the nearest "declare, look up centrally" shape but is a different kind of registry (compile-time generated vs. self-registering at static-init time). |
| `src/analyzers/{container,size}/*.cpp` bodies (the actual check logic) | analyzer | transform | No existing analyzer exists to copy the emit-`Measurement` loop from; `compare/tol.cpp` shows how a `Measurement`'s `Value` is *consumed*, not how one is *produced*. |
| Wall-clock/watchdog timeout for `DemuxSession` (open discretion item) | concurrency primitive | event-driven | No existing timeout/watchdog pattern in the codebase; `WorkerPool` provides the job-ownership model this must integrate with, but the watchdog mechanism itself is new. |
| Control-byte filtering for `tty_render.cpp` (T-2-33, carried in) | utility | transform | No sanitization function exists yet anywhere in `src/`; this is new code, not an extension of an existing filter. |

## Metadata

**Analog search scope:** `src/core/`, `src/config/`, `src/compare/`, `src/cli/`, `src/report/`,
`tools/gen_registry.py`, `scripts/*.sh`, `tests/support/`.
**Files read in full or targeted-range this session:** `03-CONTEXT.md`, `03-RESEARCH.md`,
`src/core/snapshot.cpp` (head), `src/config/toml_load.cpp` (head), `src/core/tolerance.cpp`
(head), `src/core/rational.h` (full), `src/core/model.h` (SkipReason region), `src/report/
json.cpp` / `src/report/junit.cpp` (SkipReason switches), `tools/gen_registry.py` (head/docstring
+ constants), `src/core/checks.def` (head), `src/cli/worker_pool.cpp` (full), `src/cli/
options.{h,cpp}` (full header, head of .cpp), `src/cli/commands/compare.cpp` (head + command
registration), `src/cli/commands/inspect.cpp` (snippet), `tests/support/golden.{h,cpp}`,
`scripts/lint_dead_code_after_fail.sh`, `scripts/lint_fixture_case_collisions.sh` (heads),
`scripts/gen_corpus.sh` (full), `src/compare/tol.cpp` (grep for checked-arithmetic call sites).
**Pattern extraction date:** 2026-09-02
