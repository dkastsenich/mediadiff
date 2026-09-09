---
phase: 03-probe-layer-container-size
plan: 11
subsystem: probe-layer-container-size
tags: [ffmpeg, catch2, cli11, nlohmann-json, ansi-escaping, ci]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: "container.mp4/mkv/ts.* analyzers, size.* analyzers, meta.tags/tags.language, container.chapters/track_*, the report render model, the registry-driven check list (03-01 through 03-10)"
provides:
  - "src/util/sanitize.{h,cpp}: the single control-byte/ANSI-escape choke point, closing T-2-33"
  - "scripts/lint_control_bytes.sh: self-testing lint enforcing the choke point, wired into CI's lint (ENG-16 boundary) job"
  - "inspect's complete container section for MP4/MKV/MPEG-TS (topology, per-format mechanisms, per-program TS entries, legible skip reasons)"
  - "CONT-03's -v half: ignored volatile metadata tag differences shown only under -v"
  - "tests/integration/test_trust06_idempotence.cpp: TRUST-06 encode-twice idempotence as an unconditional CI release blocker"
  - "tests/integration/test_doc03_coverage.cpp: DOC-03's registry-driven per-check fixture-pair coverage gate (30/30 verified)"
affects: ["04-*", "any phase adding a new check to checks.def (must satisfy the DOC-03 gate)", "any phase adding a new terminal/markdown render path (must route through sanitize_for_display)"]

# Actuals (#2632)
actuals:
  tokens: 31172
  tasks: 4
  commits: 5

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "sanitize_for_display single choke point: every file-derived string reaching terminal/markdown output is escaped (C0 except tab, DEL, UTF-8 C1 range) before width/elision accounting, enforced by a self-testing lint over an explicit file allowlist with an inline `// control-bytes-allow: <reason>` escape valve for non-render call sites"
    - "header-only extraction for CLI-adjacent render logic (src/cli/commands/inspect_render.h) so tests/unit can exercise it without linking CLI11 option-parsing machinery, matching src/util/fs.h's established inline-header convention"
    - "DOC-03 coverage-gate pattern: enumerate checks from builtin_registry() (never a hand-maintained id list), declare a {trigger, clean} fixture pair per check id, assert non-clean-then-all-pass through the real CLI, and hard-require the verified count equals the registry count"

key-files:
  created:
    - src/util/sanitize.h
    - src/util/sanitize.cpp
    - scripts/lint_control_bytes.sh
    - src/cli/commands/inspect_render.h
    - tests/golden/inspect_container.txt
    - tests/integration/test_trust06_idempotence.cpp
    - tests/integration/test_doc03_coverage.cpp
    - tests/unit/test_sanitize.cpp
    - tests/unit/test_inspect_container_section.cpp
  modified:
    - src/cli/tty_render.cpp
    - src/cli/tty_render.h
    - src/cli/provenance_render.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/commands/compare.cpp
    - src/report/markdown.cpp
    - src/report/json.cpp
    - src/report/junit.cpp
    - scripts/gen_corpus.sh
    - CMakeLists.txt
    - .github/workflows/ci.yml
    - .planning/phases/02-core-engine/02-SECURITY.md
    - tests/integration/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "sanitize_for_display sanitizes before elision (never after), so an invisible escape sequence in a truncated tag value cannot corrupt width accounting or reappear unescaped past the ellipsis."
  - "inspect's own text render (inspect_render.h) is a deliberate 4th sanitize_for_display call site beyond Task 1's original three -- Task 2's own action text ('route every value and message through sanitize_for_display') is more specific/authoritative than the plan's summary verification line ('only... the three display render paths'), followed literally; the phase-level grep now shows 4 render call sites plus 2 definition files plus 2 comment-only mentions (json.cpp/junit.cpp), which is correct and intentional."
  - "json.cpp/junit.cpp are NOT sanitized -- JSON/XML already escape at the wire level; double-escaping through sanitize_for_display would corrupt goldens and duplicate work the format's own encoder already does correctly. Documented with a top-of-file comment in each so a future reader doesn't 'fix' the omission."
  - "TRUST-06's corruption-catches-a-regression proof (Test 3's own acceptance criterion) was verified manually once against a scratch, non-committed padded copy of idem_b.mp4 (8KiB trailing padding, caught by size.file/size.overhead) rather than shipping a permanent corrupted fixture -- matching the plan's own explicit instruction not to commit the corruption. A single-byte payload-region flip (the first attempt) did NOT trigger any check, consistent with 03-10's own established finding that real scanners correctly tolerate most payload-region single-byte flips as valid data; a size-changing corruption was needed instead."
  - "DOC-03's coverage gate calls the real CLI binary (compare/dir --json) for every declared pair rather than the lower-level fingerprint_input/compare_fingerprints library seam, matching every sibling integration test file's convention and exercising the exact same seam a real user's CI run does. meta.missing_candidate/meta.extra_candidate are dir-mode-only synthetic checks (emitted by src/cli/commands/dir.cpp, which is CLI-boundary code per ENG-16 and unreachable from direct library calls) and are proven by a dedicated dir-mode TEST_CASE rather than forced into the generic compare-pair table."
  - "Every DOC-03 trigger/clean fixture pair was proven empirically against the real binary before being written into the coverage table -- never guessed from a fixture's descriptive name alone. Two of the thirty (psi_interval, pmt_version_churn) currently only have a same-topology-mismatch trigger, not a dedicated spacing/churn-drift trigger; recorded as WINDOWS.md entry #7 rather than silently accepted as 'good enough'."

patterns-established:
  - "Self-testing lint with a comment-aware, per-file allowlist and an inline escape-valve marker (scripts/lint_control_bytes.sh) -- the 6th lint in this project's family, following lint_dead_code_after_fail.sh/lint_fixture_case_collisions.sh's established shape (known-bad/known-good synthetic self-test before the real scan)."
  - "Registry-driven coverage gate (test_doc03_coverage.cpp) as a template for any future 'every X needs a Y' completeness requirement: enumerate from the authoritative source, declare per-item expectations in one table, hard-fail naming exactly what's missing, and assert the verified count against the source's own count rather than trusting a green run."

requirements-completed: [TRUST-06, DOC-03, CONT-03]

coverage:
  - id: D1
    description: "T-2-33 closed: a single sanitize_for_display choke point escapes control bytes/ANSI sequences before any file-derived string reaches terminal or markdown output, enforced by a self-testing lint wired into the required lint (ENG-16 boundary) CI job."
    requirement: ""
    verification:
      - kind: unit
        ref: "tests/unit/test_sanitize.cpp (10 cases: C0/DEL/C1 escaping, tab/ASCII/UTF-8 passthrough, backslash doubling, invalid-UTF-8 replacement)"
        status: pass
      - kind: other
        ref: "bash scripts/lint_control_bytes.sh"
        status: pass
    human_judgment: false
  - id: D2
    description: "inspect renders a complete container section for MP4, Matroska and MPEG-TS (generic topology, per-format mechanisms, per-program TS entries, legible skip reasons); CONT-03's -v half shows ignored volatile tags only under -v."
    requirement: CONT-03
    verification:
      - kind: unit
        ref: "tests/unit/test_inspect_container_section.cpp (8 cases including the CONT-03 -v evidence test and the control-byte-in-tag-value test)"
        status: pass
      - kind: integration
        ref: "tests/golden/inspect_container.txt (mp4/mkv/ts golden, read-only unless UPDATE_GOLDENS)"
        status: pass
    human_judgment: false
  - id: D3
    description: "TRUST-06: encoding a fixture twice with identical settings compares clean under --profile sw-encoder (and strict-bitexact, byte-identical), wired into CI as an unconditional, non-skippable part of the existing Test step."
    requirement: TRUST-06
    verification:
      - kind: integration
        ref: "tests/integration/test_trust06_idempotence.cpp -- 'trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder' and '...strict-bitexact...'"
        status: pass
    human_judgment: false
  - id: D4
    description: "DOC-03: every one of the 30 registered checks has a declared fixture pair that triggers it and one that comes back clean, enumerated from the built registry; verified count (30) equals registry count (30)."
    requirement: DOC-03
    verification:
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- 'doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one' (REQUIRE verified_count == registry.size(), 30 == 30)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_doc03_coverage.cpp -- 'doc03_coverage - dir-mode-only checks: meta.missing_candidate/meta.extra_candidate...'"
        status: pass
    human_judgment: false

duration: 45min
completed: 2026-09-03
status: complete
---

# Phase 03 Plan 11: Close the phase — sanitization, container completeness, TRUST-06, DOC-03 Summary

**Closed T-2-33 with one enforced sanitize_for_display choke point, completed inspect's container section and CONT-03's -v half, and shipped two registry/manifest-driven CI gates (TRUST-06 encode-twice idempotence, DOC-03 per-check fixture-pair coverage at 30/30) that neither a hand-maintained list nor a loosened tolerance can silently satisfy.**

## Performance

- **Duration:** 45 min
- **Started:** 2026-09-03T21:53:45Z
- **Completed:** 2026-09-03T22:38:22Z
- **Tasks:** 4
- **Files modified:** 23 (excluding tests/fixtures/GENERATOR_MANIFEST.json, which every task regenerates)

## Accomplishments
- T-2-33 (carried in from Phase 2, made materially more reachable by this phase putting file CONTENT — not just filenames — into rendered output) is closed at a single choke point (`sanitize_for_display`), enforced by a new self-testing lint (`scripts/lint_control_bytes.sh`) wired into the required `lint (ENG-16 boundary)` CI job, with `02-SECURITY.md` updated in the same commit (88/88 threats closed, 0 open below threshold).
- `inspect` now renders a complete container section for MP4, Matroska and MPEG-TS — generic topology plus subtitle/tmcd/caption presence, per-format mechanisms, per-program TS entries, legible skip reasons — and CONT-03's remaining `-v` half (ignored volatile metadata tags shown only under `-v`) is done, with a committed golden (`tests/golden/inspect_container.txt`).
- TRUST-06 is proven: an identical-settings double encode compares clean under `--profile sw-encoder` (35 findings, zero non-pass/skipped) and under `--profile strict-bitexact` where the pair is confirmed genuinely byte-identical — living inside the existing unconditional `Test` step, never a separate or conditional job.
- DOC-03's per-check fixture-pair coverage gate ships: every one of the 30 checks the built registry currently declares has a proven-empirical trigger pair and a proven-empirical clean pair; the gate's own verified count (30) is hard-asserted equal to the registry's own count (30), never trusted from a green run alone.

## Task Commits

Each task was committed atomically:

1. **Task 1: Close T-2-33 with a single control-byte escaping choke point** - `13f5e71` (feat)
2. **Task 2: inspect container section completeness + CONT-03 -v evidence** - `cf235f4` (feat)
3. **Task 3: TRUST-06 encode-twice idempotence as a CI release blocker** - `02cd3e7` (feat)
4. **Task 4: DOC-03 per-check fixture-pair coverage gate** (checkpoint:human-verify, auto-approved under `workflow.auto_advance: true` after self-verifying the 30==30 count and the no-silent-exemption acceptance criterion) - `cecc928` (test)

**Plan metadata:** _pending_ (docs: complete plan)

## Files Created/Modified
- `src/util/sanitize.h` / `src/util/sanitize.cpp` - The single control-byte/ANSI-escape choke point (`sanitize_for_display`)
- `scripts/lint_control_bytes.sh` - Self-testing lint enforcing the choke point across exactly 3 permitted render-path files
- `src/cli/tty_render.cpp` / `.h` - Sanitizes finding id/scope/message/baseline/candidate/file-summary/worst-N paths before elision; new `append_ignored_evidence` + `show_evidence` param for CONT-03's `-v` half
- `src/cli/provenance_render.cpp` - Sanitizes provenance chain `entry.value`/`entry.detail`
- `src/report/markdown.cpp` - Sanitizes finding id/message and per-file relative_path before cell-escaping
- `src/report/json.cpp` / `src/report/junit.cpp` - Documented as deliberately NOT sanitized (wire-level escaping already applies)
- `src/cli/commands/inspect_render.h` (new) - Header-only extraction of inspect's render logic (text + json), now the 4th `sanitize_for_display` call site, testable without linking CLI11
- `src/cli/commands/inspect.cpp` - Slimmed to `register_inspect_command` only, calling the extracted header
- `src/cli/commands/compare.cpp` - Passes `verbose` through to `render_tty`'s new `show_evidence` parameter
- `.planning/phases/02-core-engine/02-SECURITY.md` - T-2-33 closure note, audit trail row, 88/88 threats closed
- `scripts/gen_corpus.sh` - `tags_esc_a/b.mp4` (control-byte tag fixtures), `idem_a/b.mp4` (TRUST-06's identical-encode pair)
- `tests/golden/inspect_container.txt` - Container+meta section golden for mp4/mkv/ts
- `tests/integration/test_trust06_idempotence.cpp` - TRUST-06's CI-blocking idempotence test
- `tests/integration/test_doc03_coverage.cpp` - DOC-03's registry-driven per-check coverage gate
- `tests/unit/test_sanitize.cpp` / `tests/unit/test_inspect_container_section.cpp` - Unit coverage for the choke point and the container section
- `.github/workflows/ci.yml` - One new lint step (control-byte lint) in the existing `lint (ENG-16 boundary)` job; TRUST-06/DOC-03 need no new CI wiring (they ride the existing unconditional `Test` step)

## Decisions Made
See `key-decisions` in frontmatter for the full rationale on each. In short: sanitize before elide; inspect_render.h is an intentional 4th sanitize call site per Task 2's own literal instruction; json/junit stay unsanitized by design; TRUST-06's corruption proof was manual/uncommitted per the plan's own acceptance criterion; DOC-03's gate calls the real CLI for every pair and every pair was verified empirically, never guessed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Extracted inspect's render logic into a new header-only file for testability**
- **Found during:** Task 2
- **Issue:** The plan's Task 2 `<files>` list names only `inspect.cpp`, but `render_inspect_text`/`render_inspect_json` lived in an anonymous namespace inside `inspect.cpp`, which also pulls in `cli/options.h`/`cli/exit_code.h` — not linked into `mediadiff_unit_tests`. The plan's own Task 2 explicitly requires unit-test coverage of the container section, which was structurally impossible without either widening the unit target's link surface (risky) or extracting the render functions.
- **Fix:** Extracted `render_inspect_text`/`render_inspect_json`/supporting helpers into `src/cli/commands/inspect_render.h` (header-only, `inline`, matching `src/util/fs.h`'s established convention). `inspect.cpp` now only contains `register_inspect_command`.
- **Files modified:** `src/cli/commands/inspect_render.h` (new), `src/cli/commands/inspect.cpp`, `CMakeLists.txt` (added to `FILE_SET HEADERS`), `tests/unit/CMakeLists.txt`
- **Verification:** `tests/unit/test_inspect_container_section.cpp` (8 cases) links and passes without pulling CLI11 option-parsing machinery into the unit target.
- **Committed in:** `cf235f4` (Task 2 commit)

**2. [Rule 1 - Bug] Removed TRUST-06's committed corrupted-fixture test case**
- **Found during:** Task 3
- **Issue:** An initial draft of `test_trust06_idempotence.cpp` included a permanent `TEST_CASE` that byte-flipped a copy of `idem_b.mp4` and asserted the comparison came back non-clean. This directly contradicts the plan's own acceptance criterion: "temporarily corrupting one of the two encodes makes the test FAIL naming the firing check (verify once during development; do not commit the corruption)." A single-byte payload-region flip also did NOT trigger any check (consistent with 03-10's established finding that real scanners tolerate most payload-region single-byte flips as valid data), so the test as drafted was both non-compliant with the plan and empirically wrong.
- **Fix:** Removed the committed `TEST_CASE`. Performed the corruption proof manually once against a scratch, non-committed copy of `idem_b.mp4` with 8KiB of trailing padding appended (a size-changing corruption, caught by `size.file`/`size.overhead`), confirmed the assertion helper's FAIL path fires and names the check, then discarded the scratch file.
- **Files modified:** `tests/integration/test_trust06_idempotence.cpp`
- **Verification:** `ctest -R "integration.*trust06"` — 2/2 pass; manual corruption run confirmed the FAIL path works before the test case was removed.
- **Committed in:** `02cd3e7` (Task 3 commit)

**3. [Rule 2 - Missing Critical] Built `tests/integration/test_doc03_coverage.cpp` from scratch (not merely wired an existing file)**
- **Found during:** Task 4
- **Issue:** Task 4 is typed `checkpoint:human-verify`, but its own `<what-built>` text describes `test_doc03_coverage.cpp` as if already built. It did not exist; no prior plan had created a registry-enumerating, per-check-id coverage gate (each plan 03-04 through 03-09 proved its own checks individually, but nothing cross-checked the full registry against a fixture-pair manifest). Per `<checkpoint_protocol>`'s "Automation before verification" rule, the missing automation was built before the checkpoint's own verification could be meaningfully performed.
- **Fix:** Built the gate: enumerates `builtin_registry()`, declares a `{trigger, clean}` fixture pair per check id (every pair proven empirically against the real binary before being written into the table — see the commit message for the verification transcript), asserts non-clean-then-all-pass through the real CLI, and hard-`REQUIRE`s the verified count equals the registry count.
- **Files modified:** `tests/integration/test_doc03_coverage.cpp` (new), `tests/integration/CMakeLists.txt`
- **Verification:** `ctest -R "integration.*doc03"` — 2/2 pass; direct binary run with `-s` confirms `registry check count: 30` / `verified check count: 30`.
- **Committed in:** `cecc928` (Task 4 commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 2 missing-critical, 1 Rule 1 bug-fix)
**Impact on plan:** All three were necessary to satisfy the plan's own explicit requirements (unit-testability, the "do not commit the corruption" acceptance criterion, and Task 4's own described-as-built gate). No scope creep beyond what each task's action text already demanded.

## Issues Encountered

**DOC-03 checkpoint auto-approval under `workflow.auto_advance: true`.** Task 4 is `type="checkpoint:human-verify" gate="blocking"` — not `gate="blocking-human"` and not a package-legitimacy checkpoint — so per the auto-mode checkpoint protocol it auto-approves once its own verification passes. Self-verified: registry/verified count match at 30/30, and the acceptance criterion's exemption-mechanism grep (`exempt|skip|allow`) shows only legitimate `Status::skipped`-value comparisons and explanatory comments, no actual exemption.

**Two of the thirty DOC-03 trigger pairs exercise their check via an indirect path.** `container.ts.psi_interval`/`container.ts.pmt_version_churn` have no fixture pair in the current corpus that perturbs same-topology PAT/PMT spacing or PMT version directly; their declared trigger pair (`ts_single.ts` vs `ts_multiprogram.ts`) fires both via the CONT-08 unpaired-program topology-mismatch path instead. This satisfies "a fixture pair that TRIGGERS it" as the gate defines it, but not the intended semantic trigger. Recorded as WINDOWS.md entry #7 (open) rather than silently accepted or worked around with a hand-authored recipe outside this task's scope.

**`scripts/gen_corpus.sh` is never invoked in `.github/workflows/ci.yml` for Linux/macOS.** Discovered while verifying TRUST-06's CI-wiring requirement (Task 3's own action text: "confirm [a system ffmpeg is installed] is true for the leg this runs on"). Only the Windows-specific `gen_corpus.ps1` positive/negative-path check runs anywhere in the workflow; the shell variant that every Linux/macOS-run integration test's fixtures depend on has no invocation at all. This predates this plan (present since Phase 1's `01-05-PLAN.md` authored the CI matrix) and is materially larger than this task's scope (Task 3 explicitly limits itself to "add the [ffmpeg] install step... rather than making the test conditional" — it does not ask for full corpus-generation wiring). Not fixed here; recorded as WINDOWS.md entry #8 (open). A real CI run today would fail every corpus-dependent test on the Linux/macOS/x64-windows(sh) legs until a fixture-generation step is added — this compounds STATE.md's existing BUILD-01/BUILD-05/BUILD-06 blocker ("no commit was pushed to origin during 01-05's execution... unverified pending a real CI run").

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 3 (probe layer, container & size) is complete: all 11 plans executed, TRUST-06/DOC-03/CONT-03 requirements closed, T-2-33 closed. Two structural gaps carry forward as open WINDOWS.md entries rather than blocking this plan's completion:
- **WINDOWS.md #7:** psi_interval/pmt_version_churn need a dedicated same-topology trigger fixture (not urgent — the gate is satisfied, the semantic gap is cosmetic to the gate's own proof, not a missing check capability).
- **WINDOWS.md #8:** `scripts/gen_corpus.sh` needs a CI invocation on the Linux/macOS legs before a real CI run can pass at all — this is a prerequisite for BUILD-01/BUILD-05/BUILD-06 ever resolving, not specific to Phase 3, and should be addressed early in whichever phase next touches `.github/workflows/ci.yml` (or as a dedicated quick task before then, given its blast radius: every corpus-dependent integration test on 4 of 5 matrix legs).
Phase 4 can proceed on the analyzer/check registry this phase established; the DOC-03 gate (30/30) will need updating (a new declared pair) for every check Phase 4 registers, or it will fail by name — this is the intended behavior, not a maintenance burden to route around.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-03*

## Self-Check: PASSED

All 9 created files confirmed present on disk; all 4 task commit hashes (`13f5e71`, `cf235f4`, `02cd3e7`, `cecc928`) confirmed present in `git log --oneline --all`.
