---
phase: 02-core-engine
plan: 07
subsystem: core-engine
tags: [nlohmann-json, xxhash, ffmpeg-version, snapshot, cli11, catch2]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 01)
    provides: "The D-08 canonical Value<->JSON serializer (value_to_json/value_from_json), the pre-envelope read_snapshot(), core/model.h's Envelope/Fingerprint/Measurement, and the checkpoint-frozen schema_version=\"1.0\"/ID grammar/time-value shape this plan builds directly on top of"
  - phase: 02-core-engine (plan 06)
    provides: "compare's fully-resolved four-layer Policy (profile/config/CLI), reused unchanged by this plan's snapshot/idempotence tests"
provides:
  - "src/core/serializer.h's serialize_document(): the one canonical git-diffable text renderer (one space per indent level, one scalar per line, std::to_chars-only float formatting) every snapshot write and this plan's own report/envelope text goes through"
  - "src/core/model.h's full Envelope: decode_path, sampling, input_identity (privacy-safe basename/size/XXH3-128), diagnostics, alongside schema_version/tool_version"
  - "src/core/snapshot.h's write_snapshot() (atomic temp-file+rename write) and compute_input_identity() (XXH3-128 file identity), plus read_snapshot() extended with the SNAP-05 schema_version-major refusal and tool_version-skew diagnostic"
  - "src/util/version.h's tool_version() and compose_decode_path_signature() (TRUST-03's libavcodec/libavformat/swscale version-triple signature)"
  - "src/util/fs.h's rename_replace_utf8(): the cross-platform atomic-replace primitive every snapshot writer needs"
  - "src/cli/commands/snapshot.{h,cpp}: the `snapshot` subcommand and write_snapshot_gated() (SNAP-07's CI-safe write gate)"
  - "tests/baseline/report-1.0.json: the frozen TRUST-08 cross-release baseline"
affects: ["02-08/02-09/02-10 (report formats can now assume a fully-populated Envelope, including diagnostics for skew warnings)", "02-11 (dir mode's per-file snapshot writes reuse write_snapshot/write_snapshot_gated unchanged)", "Phase 3 (the probe layer is what finally makes `mediadiff snapshot <real-media-file>` produce a real fingerprint instead of the honest refusal this plan ships)"]

# Actuals (#2632)
actuals:
  tokens: 23511
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: ["xxHash (find_package(xxHash CONFIG REQUIRED), xxHash::xxhash linked into libmediadiff) -- XXH3_128bits for input_identity's file digest"]
  patterns:
    - "serialize_document's brace-attaches-to-adjacent-line layout: a child gets a newline of its own only once an EARLIER sibling already placed a scalar on the current line, not merely 'not the first child' -- this is what keeps lines(document) == scalars(document) exactly true even when a leading sibling is an empty container ({}/[]), the real shape a freshly-built Envelope has before any analyzer populates decode_path"
    - "A double becomes text in exactly one place (serialize_document's write_scalar), never at value_to_json construction time -- double_to_json just stores the native double; this is what stops nlohmann's own dump() from ever reintroducing a second float formatter downstream"
    - "std::to_chars' shortest-round-trip omission of the decimal point on whole-number doubles (1.0 -> \"1\", -0.0 -> \"-0\") is indistinguishable from an int64 token to nlohmann's own JSON lexer (which would silently lose -0.0's sign by lexing it as an integer) -- write_scalar appends \".0\" whenever the to_chars token has no '.'/'e'/'E', forcing the float lexer path"
    - "Encoding-sensitive path manipulation (basename extraction, git dir/filename splitting) is done via plain byte-oriented std::string::find_last_of(\"/\\\\\") rather than std::filesystem::path, which -- constructed from a narrow std::string on Windows -- converts via the ambient ANSI code page instead of UTF-8"
    - "git-tracked-ness is checked via a direct posix_spawn/CreateProcessA invocation of `git ls-files --error-unmatch`, not a shell string -- avoids both shell-injection risk and the ANSI-vs-UTF-8 path-encoding pitfall a std::system() call would reintroduce"

key-files:
  created:
    - tests/unit/test_serializer.cpp
    - tests/unit/test_snapshot_roundtrip.cpp
    - tests/support/docs/t.real_ratio.md
    - tests/integration/test_schema_version.cpp
    - tests/integration/test_snapshot_safe_write.cpp
    - tests/integration/test_idempotence.cpp
    - tests/fixtures/snapshots/schema_major_mismatch.snap.json
    - tests/fixtures/snapshots/schema_minor_diff.snap.json
    - tests/fixtures/snapshots/schema_tool_version_skew.snap.json
    - tests/fixtures/snapshots/schema_version_missing.snap.json
    - tests/baseline/report-1.0.json
    - src/cli/commands/snapshot.h
    - src/cli/commands/snapshot.cpp
  modified:
    - src/core/serializer.h
    - src/core/serializer.cpp
    - src/core/model.h
    - src/core/snapshot.h
    - src/core/snapshot.cpp
    - src/util/version.h
    - src/util/version.cpp
    - src/util/fs.h
    - src/cli/main.cpp
    - tests/support/test_checks.def
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - CMakeLists.txt

key-decisions:
  - "test_snapshot_roundtrip.cpp (Task 1's own file) drives value_to_json/value_from_json/serialize_document directly rather than write_snapshot/read_snapshot -- Task 1 touches only core/serializer.{h,cpp}; write_snapshot/read_snapshot's full envelope is Task 2's own deliverable, so testing Task 1's slice against Task 2's not-yet-built API would have made Task 1's own <verify> block unbuildable in isolation. tests/integration/test_schema_version.cpp (Task 2) re-proves the same write-read-write property through the real file-I/O path once write_snapshot exists"
  - "decode_path and sampling stay raw nlohmann::ordered_json on Envelope rather than typed structs -- no analyzer populates either until Phase 3's probe layer exists, and no acceptance criterion in this plan tests their internal shape; a typed struct now would be an unexercised guess Phase 3 might have to migrate away from. compose_decode_path_signature() is proven standalone instead"
  - "`mediadiff snapshot <file>` dispatches on whether <file> reads as a valid *.snap.json (via read_snapshot itself) rather than a filename-extension check: an input that IS already a snapshot is re-materialized through the real write_snapshot_gated path (no stub analyzer in the shipped binary, D-11); anything else, including any real media file, gets the honest 'the probe layer arrives with Phase 3' refusal. This is what makes SNAP-07's gate testable end-to-end through the real CLI binary in this phase, without inventing a second, test-only entry point"
  - "SNAP-07's git-tracked check and CI=true check both live in src/cli/commands/snapshot.cpp, not core/snapshot.cpp's write_snapshot -- spawning git and reading getenv(\"CI\") are squarely cli/-layer concerns under ENG-16 (libmediadiff never touches the environment or spawns processes)"
  - "TRUST-08's cross-release bootstrap logic (the skipped:no_prior_release resolution) is test-only glue inside test_idempotence.cpp, not a shipped check -- 02-07-PLAN.md's own Flagged Assumptions entry records this as the deliberate resolution to the bootstrap problem 02-CONTEXT.md flagged as unresolved"

patterns-established:
  - "Every new Envelope/InputIdentity field defaults to a valid empty value (nlohmann::ordered_json::array()/::object(), std::optional::nullopt) via in-class default member initializers, so every pre-existing Envelope-constructing call site (stub_analyzer.cpp) keeps compiling and behaving identically with zero changes outside this plan's own files"

requirements-completed: [SNAP-01, SNAP-03, SNAP-04, SNAP-05, SNAP-06, SNAP-07, TRUST-03, TRUST-08]

coverage:
  - id: D1
    description: "serialize_document renders any document with exactly one scalar per line (braces attach to adjacent content), std::to_chars is the sole float formatter, and all nine Value alternatives round-trip write-read-write to byte-identical text including 0.0/-0.0 and ULP-adjacent doubles"
    requirement: "SNAP-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_serializer.cpp (20/20 pass, tag [serializer])"
        status: pass
      - kind: unit
        ref: "tests/unit/test_snapshot_roundtrip.cpp (2/2 pass, tag [snapshot])"
        status: pass
      - kind: other
        ref: "grep -c \"std::to_chars\" src/core/serializer.cpp -> 7; grep -rn \"snprintf|ostringstream|std::to_string(.*double\" src/core/serializer.cpp -> no matches"
        status: pass
    human_judgment: false
  - id: D2
    description: "The full envelope (decode_path, sampling, input_identity, diagnostics) round-trips through write_snapshot/read_snapshot; a schema_version MAJOR mismatch is refused with exit 65; a tool_version skew at equal major is accepted and recorded as a diagnostic; input_identity never leaks an absolute path"
    requirement: "SNAP-01, SNAP-04, SNAP-05, TRUST-03"
    verification:
      - kind: integration
        ref: "tests/integration/test_schema_version.cpp (7/7 pass, tag [integration])"
        status: pass
      - kind: other
        ref: "./build/x64-linux/mediadiff compare tests/fixtures/snapshots/schema_major_mismatch.snap.json tests/fixtures/snapshots/tracer_b_clean.snap.json -> exit 65, stderr names both '2.0' and '1.0'"
        status: pass
    human_judgment: false
  - id: D3
    description: "`mediadiff snapshot` refuses to overwrite a git-tracked or CI-protected target without --force, leaves a refused target byte-identical, and --force is idempotent (byte-identical output, clean git status on the second run)"
    requirement: "SNAP-07"
    verification:
      - kind: integration
        ref: "tests/integration/test_snapshot_safe_write.cpp (6/6 pass, tag [integration])"
        status: pass
      - kind: other
        ref: "manual scratch-git-repo smoke test: untracked write exit 0; tracked write exit 64 (message names --force, bytes unchanged); --force exit 0; CI=true untracked refusal exit 64"
        status: pass
    human_judgment: false
  - id: D4
    description: "`snapshot f && compare f f.snap.json` is clean (SNAP-06); TRUST-08's cross-release check reports skipped:no_prior_release (never pass) when the baseline is absent, and byte-equality when tests/baseline/report-1.0.json (committed by this plan) is present"
    requirement: "SNAP-06, TRUST-08"
    verification:
      - kind: integration
        ref: "tests/integration/test_idempotence.cpp (3/3 pass, tag [integration])"
        status: pass
    human_judgment: false

duration: ~65min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 7: Canonical Serializer, Full Envelope, and Snapshot Write Summary

**`serialize_document` renders any snapshot as one-scalar-per-line, `std::to_chars`-only canonical text; the Envelope grows to its full doc-01 shape with privacy-safe `input_identity`; and `mediadiff snapshot` ships with SNAP-07's CI-safe git-tracked write gate, proven to actually refuse.**

## Performance

- **Duration:** ~65 min
- **Started:** 2026-08-15T17:16:46Z (continuing directly from 02-06)
- **Completed:** 2026-08-15T18:20:00Z
- **Tasks:** 3
- **Files modified:** 26 (13 created, 13 modified)

## Accomplishments

- `src/core/serializer.{h,cpp}`'s `serialize_document`: the one canonical, git-diffable text renderer — indent one space per level, every scalar on its own line, `std::to_chars` is the sole place a `double` becomes text (never nlohmann's own `dump()`). Fixed a genuine latent bug along the way: a naive "not the first child" newline rule produces a phantom blank line when a leading sibling is an empty container (exactly the shape a freshly-built `Envelope` has before Phase 3's analyzers populate `decode_path`) — the final algorithm tracks "has ANY earlier sibling already placed a scalar" instead, which keeps `lines(document) == scalars(document)` exactly true in every case tested, including that one.
- `src/core/model.h`'s `Envelope` grows to its full doc 01 shape (`decode_path`, `sampling`, `input_identity`, `diagnostics`); `src/core/snapshot.{h,cpp}`'s `write_snapshot` (atomic temp-file-then-rename, registry-order + scope-sorted measurements) and `compute_input_identity` (privacy-safe basename/size/XXH3-128 via the newly-linked xxHash); `read_snapshot` extended with SNAP-05's schema_version-MAJOR refusal (exit 65) and tool_version-skew diagnostic.
- `src/util/version.{h,cpp}`'s `compose_decode_path_signature` (TRUST-03: libavcodec/libavformat/swscale version triples, parsed as integers, never string substrings) and `tool_version`; `src/util/fs.h`'s `rename_replace_utf8` (the MSVC-CRT-`rename()`-doesn't-replace fix SNAP-07's `--force` idempotency needs on Windows).
- `src/cli/commands/snapshot.{h,cpp}`: `mediadiff snapshot <file> [--out] [--force]`, dispatching on whether `<file>` already reads as a valid `*.snap.json` (re-materialize through the real write path) versus anything else (an honest "the probe layer arrives with Phase 3" refusal, never a fabricated fingerprint). `write_snapshot_gated` implements SNAP-07 via a direct `git ls-files --error-unmatch` spawn (no shell) plus a `CI=true` check — proven to actually refuse via both the automated suite and a manual scratch-git-repo smoke test.
- `tests/baseline/report-1.0.json`: the frozen TRUST-08 cross-release baseline this plan commits, so the second release onward exercises the real byte-equality comparison instead of the `skipped:no_prior_release` bootstrap path.

## Task Commits

Each task was committed atomically:

1. **Task 1: The canonical serializer** — `af3a66b` (feat)
2. **Task 2: The fingerprint envelope, snapshot write, and the schema-version refusal** — `f5e3f2a` (feat)
3. **Task 3: The snapshot subcommand, the CI-safe write gate, and the idempotence harnesses** — `02d0ad3` (feat)

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified

- `src/core/serializer.{h,cpp}` — `serialize_document`; `double_to_json` simplified to store a native double (float-to-text happens exactly once, at `serialize_document`'s own pass)
- `src/core/model.h` — `Envelope` grows `decode_path`/`sampling`/`input_identity`/`diagnostics`; new `InputIdentity` struct
- `src/core/snapshot.{h,cpp}` — `write_snapshot`, `compute_input_identity`, `kSchemaVersion`; `read_snapshot` gains the major-mismatch refusal and skew diagnostic
- `src/util/version.{h,cpp}` — `tool_version`, `compose_decode_path_signature` (now includes `<libswscale/swscale.h>`)
- `src/util/fs.h` — `rename_replace_utf8`
- `src/cli/commands/snapshot.{h,cpp}`, `src/cli/main.cpp` — the `snapshot` subcommand
- `tests/support/test_checks.def`, `tests/support/docs/t.real_ratio.md` — `t.real_ratio` (`value_kind=real`), the one alternative no prior check exercised
- `tests/unit/test_serializer.cpp`, `tests/unit/test_snapshot_roundtrip.cpp` — Task 1 coverage
- `tests/integration/test_schema_version.cpp` — Task 2 coverage
- `tests/integration/test_snapshot_safe_write.cpp`, `tests/integration/test_idempotence.cpp` — Task 3 coverage
- `tests/fixtures/snapshots/schema_*.snap.json` (4 files) — hand-authored schema-version fixtures
- `tests/baseline/report-1.0.json` — the frozen TRUST-08 baseline
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` — xxHash wiring, new sources, `MEDIADIFF_BASELINE_DIR`

## Decisions Made

See frontmatter `key-decisions` for full rationale on: Task 1's test scope staying within `core/serializer.{h,cpp}`'s own API surface (not forward-referencing Task 2's `write_snapshot`); `decode_path`/`sampling` staying raw JSON rather than typed structs; `snapshot`'s read-as-snapshot-first dispatch (what makes SNAP-07 end-to-end CLI-testable this phase without a probe layer); the gate's git/CI logic living in `cli/` under ENG-16; and TRUST-08's bootstrap logic being test-only glue, not a shipped check.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing test coverage the plan's own must_haves require] Added `t.real_ratio` (value_kind=real) to the test-only registry**
- **Found during:** Task 1, while designing `test_snapshot_roundtrip.cpp`'s "all nine Value alternatives" fixture
- **Issue:** No check, production or test-only, had ever declared `value_kind = "real"` before this plan. `read_snapshot`/`write_snapshot` both dispatch a measurement's on-disk shape off its `CheckDef`'s declared `value_kind`, so the `double` alternative was structurally untestable through the registry-dispatched read/write path — even though this plan's own `must_haves` explicitly require exactly that coverage ("A double measurement at 0.0, -0.0, the smallest normal and the largest finite value round-trips...").
- **Fix:** Added `t.real_ratio` (`semantic=tol`, `unit=percent`, `value_kind=real`, `tolerance=5%`) to `tests/support/test_checks.def` plus its explain doc. Verified it does not perturb the D-14 fail-first coverage gate (which scans by `(semantic, status)` cell across ANY check of the right semantic — `t.tol_ms`/`t.tol_info` already satisfy the `tol` cells).
- **Files modified:** `tests/support/test_checks.def`, `tests/support/docs/t.real_ratio.md`.
- **Commit:** `af3a66b` (Task 1's own commit).

**2. [Rule 1 - Latent bug] `serialize_document`'s newline rule produced a phantom blank line when a leading sibling is empty**
- **Found during:** Task 1, while deriving the exact `lines(document) == scalars(document)` property by induction before writing the acceptance test for it
- **Issue:** A naive "insert a newline before every child except the first" rule breaks the invariant precisely when the FIRST child of a container is itself an empty container (`{}`/`[]`) and a LATER sibling has scalars — exactly the shape a freshly-constructed `Envelope` has (`decode_path: []` legitimately comes first in field order, before `schema_version`... actually the reverse order was chosen specifically to avoid this in practice, but the general serializer must not depend on field ordering to stay correct).
- **Fix:** Changed the rule to track whether ANY earlier sibling already placed a scalar on the current line (`started_content`), inserting a newline before a child only when both that child has scalars AND `started_content` is already true. Verified by induction and locked in with a dedicated test (`"line count still equals scalar count when a leading sibling is an empty container"`).
- **Files modified:** `src/core/serializer.cpp`.
- **Commit:** `af3a66b` (Task 1's own commit, found and fixed before it landed).

**3. [Rule 3 - Blocking cross-platform correctness] Added `util/fs.h::rename_replace_utf8`**
- **Found during:** Task 2, while implementing `write_snapshot`'s atomic temp-file-then-rename
- **Issue:** `std::rename` (the obvious choice, already used by the pre-existing snapshot-read helper's error paths) atomically replaces an existing destination on POSIX, but the MSVC CRT's `rename()` does NOT replace an existing destination file — it fails outright. Without a fix, `snapshot --force`'s overwrite semantics (SNAP-07's own idempotency requirement: "Running `snapshot --force` twice ... produces byte-identical files") would silently fail on Windows the very first time the target already existed.
- **Fix:** Added `rename_replace_utf8` to `util/fs.h` (not in this plan's original `files_modified` list), implemented via `MoveFileExW` + `MOVEFILE_REPLACE_EXISTING` on Windows and a plain `std::rename` on POSIX — reusing `fs.h`'s own `utf8_to_wide` so no wide-character type is named outside `fs.h`/`cli/main.cpp`, matching that header's own stated boundary.
- **Files modified:** `src/util/fs.h`.
- **Commit:** `f5e3f2a` (Task 2's own commit).

---

**Total deviations:** 3 auto-fixed (1 Rule 1, 1 Rule 2, 1 Rule 3)
**Impact on plan:** All three were necessary for correctness (the serializer bug and the Windows rename gap are genuine bugs a later phase or platform would have hit; the `t.real_ratio` check is missing test infrastructure this plan's own must_haves depend on). None changes scope, architecture, or any frozen contract from prior waves.

## Issues Encountered

- **`std::to_chars`'s shortest-round-trip text omits the decimal point for whole-number doubles** (`1.0` -> `"1"`, `-0.0` -> `"-0"`), which nlohmann's own JSON lexer reads as an INTEGER token (no `.`/`e`/`E`) — and integers have no signed zero, so `-0.0` would silently round-trip to `0.0` if left as-is. Resolved by appending `.0` to any float token lacking a decimal/exponent marker, forcing the float lexer path while keeping the text otherwise identical to `std::to_chars`' own output — deterministic and idempotent across repeated write-read-write cycles.
- **Windows' `CreateProcessA`/narrow-string APIs interpret command-line text via the process's active code page**, which is normally NOT UTF-8 — but `app.manifest` (Phase 1) already sets `ActiveCodePage=UTF-8` for the whole `mediadiff.exe` process, so `git`'s spawn (via `CreateProcessA`) inherits that same UTF-8 interpretation, matching the precedent `tests/process_spawn.h`'s own Windows branch already established for spawning `mediadiff.exe` itself.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The full envelope shape (`decode_path`, `sampling`, `input_identity`, `diagnostics`) is frozen and byte-diffable; Phase 3's probe layer populates `decode_path`/`sampling`/`input_identity` for real, and `compose_decode_path_signature()` is ready for a Phase-3 analyzer to embed into a class-2 stream's `decode_path` entry.
- `mediadiff snapshot` ships honestly: it re-materializes an existing `*.snap.json` today, and Phase 3 only needs to replace the "no probe layer" refusal branch with a real fingerprint build — `write_snapshot_gated`'s gate and `write_snapshot`'s atomic write need no changes.
- `tests/baseline/report-1.0.json` is committed; the next release's own baseline generation (when it happens) replaces this file and the `skipped:no_prior_release` bootstrap branch becomes provably-reachable dead code, exactly as designed.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 13 created files verified present on disk; all 3 task commit hashes (`af3a66b`, `f5e3f2a`, `02d0ad3`) verified present in `git log --oneline --all`.
