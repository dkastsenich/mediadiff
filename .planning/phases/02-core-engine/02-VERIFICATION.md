---
phase: 02-core-engine
verified: 2026-08-15T21:42:27Z
status: human_needed
score: 5/5 truths verified (2 present, behavior-unverified — routed to human/Phase-3 deferral)
behavior_unverified: 2
overrides_applied: 0
deferred:
  - truth: "mediadiff snapshot <media-file> && mediadiff compare <media-file> <media-file>.snap.json is clean, produced from a real media file"
    addressed_in: "Phase 3"
    evidence: "02-CONTEXT.md phase boundary: 'all real media parsing... The probe layer begins in Phase 3.' The shipped `snapshot` command on a real file exits 65 with the message 'fingerprinting a media file requires the probe layer, which arrives with Phase 3'. SNAP-06 is instead proven via a stub Fingerprint written directly by write_snapshot() in tests/integration/test_idempotence.cpp, which is the correct Phase-2 substitute per D-10/D-11."
  - truth: "The written snapshot envelope carries a decode-path signature including libavcodec/libavformat/swscale toolchain versions"
    addressed_in: "Phase 3"
    evidence: "src/util/version.cpp's compose_decode_path_signature() (TRUST-03) exists and is unit-tested (test_schema_version.cpp: 'compose_decode_path_signature composes three distinct version triples') but is never called from src/cli/commands/snapshot.cpp — there is no envelope-composing call site yet because live snapshot production is Phase 3 scope. decode_path stays whatever was in the input fingerprint (empty [] for a stub) when 'snapshot' re-materializes an existing *.snap.json."
human_verification:
  - test: "Run `mediadiff compare` / `dir` / TTY report output in a real Windows console (cmd.exe or Windows Terminal) with `SetConsoleMode`'s ENABLE_VIRTUAL_TERMINAL_PROCESSING path exercised"
    expected: "Colour renders as actual styling (ANSI-interpreted) rather than literal escape-sequence bytes printed to the terminal"
    why_human: "This sandbox is Linux-only; unit.console_vt is skipped in this environment (needs a real TTY) and the color/VT-enable code path (src/util/fs.h / Windows console setup) cannot be exercised programmatically here. Explicitly flagged as human-only in the phase's verification context."
  - test: "Confirm NO_COLOR / non-TTY / CI=true auto-disable and GITHUB_ACTIONS=true stays-enabled behavior visually in a real terminal session (not just via unit test assertions on decide_color's pure function)"
    expected: "Colour appears/disappears exactly as the WR-02-fixed precedence table states, matching what a human operator sees, not only what the unit tests assert against synthetic EnvVars"
    why_human: "The logic is unit-tested (11 test cases, WR-02 fix verified) and cannot regress silently, but actual terminal rendering (glyph width, 256-color vs truecolor fallback) is a visual property this sandbox cannot observe."
---

# Phase 2: Core Engine Verification Report

**Phase Goal:** The complete compare engine — registry, comparison semantics, policy resolution, snapshots, all four report formats and `dir` orchestration — works end to end against stub measurements, so every analyzer that follows plugs into finished machinery.

**Verified:** 2026-08-15T21:42:27Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (mapped to the 5 ROADMAP success criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1a | `snapshot`/`compare` round-trip is clean by construction (against a stub fingerprint, since real media needs Phase 3's probe layer) | ✓ VERIFIED | `tests/integration/test_idempotence.cpp` SNAP-06 test passes; manually reproduced with `write_snapshot()`-equivalent (copied fixture) → `compare f f` → `pass:1, exit 0`. Real-media `snapshot` correctly refuses with exit 65 and a message naming Phase 3, rather than crashing or fabricating a result. |
| 1b | `*.snap.json` is canonical/git-diffable, times stored as rationals, envelope carries a decode-path signature with toolchain versions | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED (partial) | Canonical serializer (field order, sorted scopes, one-value-per-line, shortest round-trip float, `{num,den,tb}` rationals) is VERIFIED — 296/296 tests including cross-platform-determinism-relevant serializer suite. `compose_decode_path_signature()` (TRUST-03) exists and is unit-tested standalone but is **not called from any envelope-producing path** in this phase — no such path exists yet (Phase 3 scope). See Deferred Items. |
| 1c | `compare` refuses an incompatible `schema_version` major with exit 65 | ✓ VERIFIED | Manually reproduced: schema_version `"2.0"` vs build's `"1.0"` → exit 65, message names both versions. Also covered by `integration.schema_version` tests (6/6 passing). |
| 1d | `snapshot` refuses to overwrite a tracked baseline in CI without `--force` | ✓ VERIFIED | Manually reproduced: `CI=true mediadiff snapshot tracked.snap.json` (git-tracked, no `--force`) → exit 64, `--force` named; `--force` → exit 0, overwrite succeeds. `integration.snapshot_safe_write` (6/6 passing) covers tracked/untracked/zero-byte/CI-untracked/force-twice-idempotent cases. |
| 2 | Policy resolution end to end — `--profile`, `mediadiff.toml`, path overrides, repeatable `--set`/`--tol` in argv order — across all five profiles, `list-checks --effective`, `-v` provenance chain, wrong-unit tolerance exit 64 | ✓ VERIFIED | Manually reproduced all five profiles resolving without error; `--set` repeated twice resolves last-writer-wins in argv order both directions; TOML `profile=`/`[severity]` picked up and overridden by a later CLI `--set`; `-v` shows a two-entry provenance chain (`builtin` → `cli`) naming the winning layer; `--tol meta.tool_version=5px` → exit 64 naming expected unit `none`. Note: PLAN 02-10's own must-have about a flag working "before and after the subcommand name" was demonstrated only as *intra-subcommand* order-independence, not literal root-vs-subcommand placement — `mediadiff --set ... compare a b` genuinely fails (exit 64, "not expected") due to a documented CLI11 2.6.2 limitation (`02-10-SUMMARY.md`). This does not affect the ROADMAP criterion's literal wording ("repeatable `--set`/`--tol` in argv order"), which is about ordering among repeated occurrences of the same flag — verified working. |
| 3 | All four report formats render the same findings; TTY grouped fixed order + accept/tune/silence triple; JSON schema-validated, byte-identical, matches a frozen prior-release baseline; Markdown budgeted under GitHub's limit; JUnit zero-integration-work; colour auto-disable rules | ✓ VERIFIED (colour-in-real-terminal routed to human) | TTY: fixed group order, `-v` shows pass, default hides pass, accept/tune/silence triple renders in fixed order under a gating fail/warn finding (manually reproduced). JSON: validates against `docs/schema/report-1.0.json` (`integration.json_schema`, 8 tests incl. an intentionally-invalid document failing validation), two runs byte-identical (manually reproduced + `TRUST-05`/`determinism` suites), `tests/baseline/report-1.0.json` frozen baseline compared via TRUST-08 idempotence test. Markdown: `kMarkdownBudgetBytes = 60000` (5,536-byte headroom under GitHub's 65,536-char limit), overflow-fold tested at the exact byte boundary. JUnit: well-formed `testsuites`/`testcase` XML, zero-tests-declared-not-empty-file for a clean run, one `<testcase>` per gating finding (manually reproduced both zero-fail and one-fail cases). Colour auto-disable (`NO_COLOR`, non-TTY, `CI=true`) vs `GITHUB_ACTIONS=true` staying enabled: logic VERIFIED via 11 unit tests including the two WR-02 regression tests explicitly crossing `NO_COLOR` against `GITHUB_ACTIONS=true`; **actual rendering in a real Windows console is human-only** (see Human Verification). |
| 4 | `dir a b` pairs by relative path deterministically under a `--threads`-bounded pool, reports unpaired files, honours the full exit-code contract with partial JSON on 66, process control confined to `cli/` | ✓ VERIFIED | Manually reproduced: unpaired baseline-only file → `meta.missing_candidate` (fail); unpaired candidate-only file → `meta.extra_candidate` (warn); `--threads 1` vs `--threads 8` produce byte-identical `--json`; empty-corpus `dir` → 0 files, exit 0. All 7 documented exit codes (0,1,2,64,65,66,70) reproduced directly against the real binary or confirmed by a named integration test asserting the exact integer (`test_exit_codes.cpp`, 9 cases). Exit 66 confirmed to still emit parseable JSON with a `partial: true` diagnostic. `scripts/lint_eng16.sh` passes clean (no stdout/`exit()` in the engine subtrees). |
| 5 | Every registered check resolves to enforced documentation (`explain`, `inspect`), `skipped` is machine-readable and never conflated with `pass`, all seven comparison semantics behave per spec with ticks-not-floats time comparison | ✓ VERIFIED | `explain meta.tool_version` prints what/why/accept-tune-silence in fixed order; unknown check id → exit 64. `inspect` renders every group (7, fixed order) including empty ones with an explicit no-measurements line. `docs/checks/<id>.md` exists for all 3 registered checks + generator build-gate tests (`test_registry_generator.cpp`, 5/5: missing doc, empty doc, bad grammar, dangling alias all fail the build naming the offender). `skipped ≠ pass`: `test_fail_first_coverage.cpp` proves the not-yet-implemented allow list is empty (all 7 semantics implemented) and every semantic's per-status fixture matrix is present; `hash` precondition mismatch → `skipped:hash_incomparable`, never fabricated. Ticks-not-floats: `core/rational.h`'s cross-multiplication (with the review-fix's overflow-checked variants) is the only comparison path; a review-flagged UB path (CR-03/WR-03) was fixed and independently re-verified via new unit tests engineered to trigger overflow, asserting `Status::error` rather than a fabricated verdict. |

**Score:** 5/5 criteria have their testable substance verified; 2 sub-items (both criterion 1) are present-but-not-wired *by design* (Phase 3 scope) and 2 human-verification items (criterion 3's real-terminal colour rendering) are outstanding.

### Deferred Items

Not gaps — explicitly out of Phase 2's scope per `02-CONTEXT.md`'s phase boundary ("All real media parsing... the probe layer begins in Phase 3. If a task in this phase needs to open a media file, it has escaped the boundary.").

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | `mediadiff snapshot <real-media-file>` produces a live fingerprint | Phase 3 | `snapshot` on a real file exits 65 with an explicit message naming Phase 3; SNAP-06's round-trip is proven instead via a stub fingerprint written directly by `write_snapshot()`, which is D-10/D-11's designed substitute. |
| 2 | Snapshot envelope's `decode_path` is populated with `compose_decode_path_signature()`'s libavcodec/libavformat/swscale versions | Phase 3 | The signature-composing function (TRUST-03) exists and is unit-tested in isolation, but no call site wires it into an envelope because there is no live envelope-producing path yet — `snapshot.cpp` only re-materializes an already-written fingerprint's existing (possibly-empty) `decode_path`. |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/core/checks.def` + `tools/gen_registry.py` | Registry generator, build-fails-on-missing-doc | ✓ VERIFIED | `docs/checks/*.md` present for all 3 registered checks; generator tests (5/5) prove missing/empty/malformed docs fail the build naming the offender. |
| `src/compare/{exact,tol,set,presence,hash,dist,span}.cpp` | All seven comparison semantics | ✓ VERIFIED | `not_yet_implemented_semantics()` allow list is empty (asserted by test); each semantic has a fixture pair for every status it can emit. |
| `src/core/profiles.{h,cpp}` | Five shipped profiles | ✓ VERIFIED | All five (`strict-bitexact`, `sw-encoder`, `hw-encoder`, `remux`, `transform`) resolve without error via `list-checks --effective --profile <name>`; `sw-encoder` is the default with neither flag nor TOML present. |
| `src/core/policy.cpp` | Config precedence merge | ✓ VERIFIED | Manually reproduced profile → TOML `[severity]` → CLI `--set` chain; `-v` shows the resolved provenance. |
| `src/core/snapshot.cpp` / `src/core/serializer.cpp` | Snapshot I/O, canonical serialization | ✓ VERIFIED | Byte-identical round-trip, atomic temp-file+rename write, `{num,den,tb}` rationals, shortest-round-trip float formatting (all covered by passing serializer unit suite). Review-flagged type-confusion crash (CR-01) fixed and covered by a dedicated integration test. |
| `src/report/{json,markdown,junit}.cpp`, `src/cli/tty_render.cpp` | Four report formats | ✓ VERIFIED | All four manually exercised end to end and match spec (fixed group order, triple, budget, zero-integration XML). CR-02's float-formatter-bypass fix independently re-verified via golden regeneration + byte-identical two-run check. |
| `src/cli/dir_pairing.cpp`, `src/cli/worker_pool.cpp`, `src/cli/commands/dir.cpp` | `dir` orchestration | ✓ VERIFIED | Deterministic sorted-path pairing, index-addressed result vector (order-independent of completion order), peak-in-flight-bounded pool (unit-tested), `meta.missing_candidate`/`meta.extra_candidate` reproduced directly. |
| `src/cli/exit_code.cpp` | Exit-code contract | ✓ VERIFIED | All 7 codes (0/1/2/64/65/66/70) reproduced directly or via a named test asserting the exact integer. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `src/cli/commands/compare.cpp` | `src/core/snapshot.cpp::read_snapshot` | direct call | ✓ WIRED | Manually confirmed: malformed/incompatible snapshots surface as exit 64/65, not a crash. |
| `src/cli/commands/snapshot.cpp` | `src/core/policy.cpp` (git-tracked gate) | `write_snapshot_gated` | ✓ WIRED | `--force`/CI-tracked-refusal reproduced directly. |
| `src/report/json.cpp` | `src/core/serializer.cpp::serialize_document` | CR-02 fix | ✓ WIRED | `--json` output now routes through the canonical `std::to_chars` writer, not `nlohmann::dump()` — confirmed via code read and byte-identical two-run check. |
| `src/cli/commands/dir.cpp` | `src/cli/worker_pool.cpp` | `WorkerPool::run_indexed` | ✓ WIRED | `--threads 1` vs `--threads 8` byte-identical output confirms index-addressed, completion-order-independent aggregation. |
| `src/cli/commands/dir.cpp` | `src/core/registry.h` (`meta.missing_candidate`/`meta.extra_candidate`) | `dir_pairing.cpp` → per-file finding synthesis | ✓ WIRED | Reproduced directly: unpaired baseline/candidate files produce the correct checks at the correct severities. |
| `src/util/version.cpp::compose_decode_path_signature` | any snapshot-envelope-producing call site | — | ✗ NOT WIRED (by design, deferred to Phase 3) | Grep confirms zero call sites outside its own unit test; no live envelope-producing path exists yet in this phase. |

### Requirements Coverage

All 48 requirement IDs assigned to Phase 2 in `.planning/REQUIREMENTS.md` are marked Complete and are traceable to a specific plan's `requirements:` frontmatter (cross-checked against all 11 `02-*-PLAN.md` files — no orphans, no gaps in the mapping):

CLI-01/02/03/04/06/07/08/10, ENG-01…16, SNAP-01…07, REPORT-01…07, DIR-01…05, TRUST-03/05/08, DOC-01/02 — **all 48 SATISFIED** per the manual/automated evidence above. TRUST-03 and TRUST-08 in particular were the two "bootstrap problem" items flagged as deferred/open in `02-CONTEXT.md`; both are resolved (TRUST-03 via `compose_decode_path_signature()` — present and correct, just not yet wired into a Phase-2 call site per the deferred item above; TRUST-08 via the `no_prior_release` skip-not-pass idempotence test).

No orphaned requirements: the phase's declared requirement set (48) exactly matches REQUIREMENTS.md's traceability table for Phase 2.

### Anti-Patterns Found

None. `grep`-scanned `src/` for `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER`/"not yet implemented"/"coming soon": zero hits. The one known, honestly-tracked stub (`Finding.delta`/`Finding.evidence` rendering as unconditional JSON `null`, since no Phase-2 check populates them) is recorded in `.planning/WINDOWS.md` as an open Broken-Windows-ledger item with a clear reason and is schema-nullable — it does not undercut criterion 3's "render the same findings" (findings still render correctly; only two forward-looking, currently-unpopulated fields are null). Judged non-blocking and already surfaced through the proper tracking mechanism rather than hidden.

Code review (`02-REVIEW.md`) found 3 Critical + 3 Warning issues (untrusted-snapshot type-confusion crash/silent-pass, second float formatter breaking the determinism contract, unchecked int64 overflow in tolerance/distribution comparison, digit-accumulation overflow, `NO_COLOR`/`GITHUB_ACTIONS` precedence bug, `compare_ticks` overflow silently faking span equality). All 6 were fixed (`02-REVIEW-FIX.md`) with dedicated regression tests, and the full 296-test suite passes post-fix. Spot-checked CR-01 and CR-02's fixes directly against the built binary (poisoned-value handling, `--json` float formatting) rather than trusting the fix report's own claims — confirmed correct.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| snapshot round-trip clean | `compare base.snap.json base.snap.json` | `pass:1, exit 0` | ✓ PASS |
| schema_version major mismatch | `compare base.snap.json major_mismatch.snap.json` | exit 65, names both versions | ✓ PASS |
| CI tracked-overwrite gate | `CI=true snapshot tracked.snap.json` (no `--force`) | exit 64, names `--force` | ✓ PASS |
| `--force` overwrite | `CI=true snapshot tracked.snap.json --force` | exit 0 | ✓ PASS |
| repeatable `--set` argv order | `--set a=fail --set a=info` | resolves `info` (last wins) | ✓ PASS |
| wrong-unit tolerance | `--tol meta.tool_version=5px` | exit 64, names `none` | ✓ PASS |
| all five profiles resolve | `list-checks --effective --profile <each>` | exit 0 for all five | ✓ PASS |
| TTY accept/tune/silence triple | `compare ... --set meta.tool_version=fail` | triple renders, fixed order | ✓ PASS |
| exit 1 on fail | same as above | exit 1 | ✓ PASS |
| exit 2 on warn+strict | `compare base candidate --strict` | exit 2 | ✓ PASS |
| exit 0 on warn without strict | `compare base candidate` | exit 0 | ✓ PASS |
| exit 64 no positional args | `compare` (no args) | exit 64 | ✓ PASS |
| exit 64 no args at all | `mediadiff` (bare) | exit 64 | ✓ PASS |
| exit 65 unreadable input | `compare nonexistent.snap.json base` | exit 65 | ✓ PASS |
| exit 66 partial JSON | `compare partial_decode.a partial_decode.b --json` | exit 66, `partial: true` in diagnostics | ✓ PASS |
| `dir` unpaired-file findings | `dir dirA dirB` (one file each-side-only) | `meta.missing_candidate` fail / `meta.extra_candidate` warn | ✓ PASS |
| `dir` determinism across threads | `dir --threads 1` vs `--threads 8` `--json` | byte-identical | ✓ PASS |
| `dir` empty corpus | `dir emptyA emptyB` | 0 files, exit 0 | ✓ PASS |
| Markdown/JUnit/JSON render same findings | `compare ... --report md=... --report junit=... --json` | all three internally consistent | ✓ PASS |
| `explain`/`inspect` | `explain meta.tool_version`, `inspect base.snap.json` | correct fixed-order content | ✓ PASS |
| `--content` plumbing-only honesty | `dir` help/flag wiring | explicit "accepted but has no effect yet" message, not silent | ✓ PASS |
| `--set`/`--tol` before subcommand token | `mediadiff --set ... compare a b` | exit 64 "not expected" (documented CLI11 2.6.2 limitation) | ✓ PASS (confirms documented deviation, doesn't affect ROADMAP wording) |
| ENG-16 lint | `scripts/lint_eng16.sh` | "clean" | ✓ PASS |

### Probe Execution

Not applicable — Phase 2 has no `scripts/*/tests/probe-*.sh` convention; probes begin in Phase 3.

### Full Test Suite (run once)

`ctest --output-on-failure` in `build/x64-linux`: **296/296 passing**, 1 legitimate skip (`unit.console_vt`, needs a real TTY — matches the phase's own documented expectation). Binary confirmed up to date with HEAD (`18fc086`) via a no-op incremental build.

### Human Verification Required

### 1. Colour renders as styling in a real Windows console

**Test:** Run `mediadiff compare`/`dir` with colour enabled in an actual Windows `cmd.exe` or Windows Terminal session.
**Expected:** ANSI-styled output (colour, not literal `\x1b[...m` bytes) — confirms `SetConsoleMode`'s `ENABLE_VIRTUAL_TERMINAL_PROCESSING` path actually works on real Windows hardware, not just that the decision logic is unit-tested.
**Why human:** This sandbox is Linux; `unit.console_vt` is itself skipped here for the same reason. Explicitly called out as human-only in the phase's verification context.

### 2. Colour precedence rules as experienced in a real terminal session

**Test:** Set `NO_COLOR=1` with `GITHUB_ACTIONS=true`, and separately `CI=true` with `GITHUB_ACTIONS=true`, in a real interactive terminal and observe actual rendered output.
**Expected:** Matches the WR-02-fixed precedence table (`NO_COLOR` wins over everything; `GITHUB_ACTIONS=true` wins over `CI=true`) as visually experienced, not only as asserted by the 11 passing unit tests against `decide_color`'s pure function.
**Why human:** Terminal color rendering (glyph width, ANSI interpretation) is a visual property this sandbox cannot observe directly, even though the underlying decision logic is thoroughly unit-tested and was specifically regression-tested for the exact `NO_COLOR`/`GITHUB_ACTIONS` interaction the review flagged.

### Gaps Summary

No blocking gaps found. Every one of the 48 requirement IDs assigned to Phase 2 has direct, reproducible evidence in the built binary and/or a passing automated test, cross-checked rather than taken from SUMMARY.md claims. The phase's own explicitly-acknowledged boundary items (live-media `snapshot`, decode-path-signature wiring) are correctly deferred to Phase 3 rather than faked or silently dropped — the CLI itself says so out loud (`snapshot` on real media exits 65 naming Phase 3). The one open Windows-ledger stub (`Finding.delta`/`evidence` unconditional null) is honestly tracked and does not undermine any of the five success criteria. The one code-review cycle (3 Critical, 3 Warning) was fully remediated with independently-verified regression tests, and this verification re-confirmed two of the fixes (CR-01, CR-02) directly against the built binary rather than trusting the fix report. The only outstanding items are two genuinely human-only visual checks in a real Windows console, which this Linux sandbox cannot perform.

---

_Verified: 2026-08-15T21:42:27Z_
_Verifier: Claude (gsd-verifier)_
