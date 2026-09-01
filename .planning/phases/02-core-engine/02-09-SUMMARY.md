---
phase: 02-core-engine
plan: 09
subsystem: cli
tags: [tty-renderer, colour-policy, fmt-color, terminal-width, check-registry-generator, accept-tune-silence]

# Dependency graph
requires:
  - phase: 02-core-engine (plan 08)
    provides: "src/report/model.h's ReportModel/GroupBlock/Summary/RenderOptions/build_report_model/is_gating/scope_to_text -- the shared intermediate this plan's TTY renderer consumes rather than re-deriving grouping/ordering/filtering; RenderOptions.show_pass, built and unit-tested in 02-08 but never set false by any of its three renderers, is this plan's own hook for REPORT-02's 'only non-pass by default'"
provides:
  - "src/cli/color_policy.h/.cpp: ColorInputs, ColorDecision, decide_color -- the single pure-function answer to CLI-08's NO_COLOR/CI/GITHUB_ACTIONS/--no-color/--ascii precedence question, table-tested with no process spawn or environment mutation"
  - "src/cli/options.h/.cpp: ColorArgs, add_color_flags, read_color_inputs -- the one place getenv/isatty are called for colour purposes"
  - "src/cli/tty_render.h/.cpp: render_tty(ReportModel, CheckRegistry, ColorDecision, terminal_width) -- fixed group order, only non-pass/non-ignored by default, the accept/tune/silence triple under every gating finding, elided (never wrapped) value columns, word-wrapped hint prose"
  - "tools/gen_registry.py: REQUIRED_ATS_SUBHEADINGS, extract_subsections, load_ats_sections -- splits '## Accept / Tune / Silence' into '### Accept'/'### Tune'/'### Silence', enforced as a build failure and embedded onto CheckDef"
  - "src/core/registry.h: CheckDef::explain_accept/explain_tune/explain_silence -- the three compiled-in explain sub-sections a renderer reaches via registry.find(id) alone, with no dependency on either generated CheckId enum"
  - "src/cli/commands/compare.cpp: a second, verbose-aware ReportModel plus the terminal-width query and stdout TTY print, gated on --json not being requested"
affects: ["02-10 (add_common_options(CLI::App&, CliOptions&) will fold --no-color/--ascii into the shared flag set this plan first registers per-subcommand via add_color_flags)", "02-10's explain command shares the same doc-derived compiled-in text this plan's generator extension produces"]

# Actuals (#2632)
actuals:
  tokens: 18743
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: ["fmt/color.h (fmt::fg, fmt::color, text_style) -- header-only, no new vcpkg dependency, already transitively available via fmt::fmt"]
  patterns:
    - "decide_color is a pure function over an explicit ColorInputs value; read_color_inputs (src/cli/options.cpp) is the ONLY call site for getenv/isatty related to colour -- the same 'push I/O to the edge, keep the decision pure and testable' shape D-08's serializer and 02-08's build_report_model already established"
    - "A renderer that needs per-check compiled-in text but must work identically against BOTH the production registry and a --symbol-prefix test_ one reaches it through CheckDef fields via registry.find(id)+registry.at(index), never through the generated, enum-typed explain_doc(CheckId) accessor -- CheckDef::explain_accept/explain_tune/explain_silence is the second data path gen_registry.py now emits alongside check_explain.cpp's raw-string documents"
    - "Width-awareness is byte-length elision for a value COLUMN (finding rows) and byte-length word-wrap for PROSE (the summary line and the accept/tune/silence hint text) -- two different treatments of the same terminal_width budget, chosen because wrapping a value column mid-diff is unreadable but wrapping a sentence is normal terminal behavior"
    - "compare.cpp builds two independent ReportModel instances from the same findings/policy/registry -- one with RenderOptions defaulted to 'hide nothing' for JSON/Markdown/JUnit (02-08's own convention), a second with show_pass/show_ignored driven by -v for TTY alone, since ReportModel itself carries no per-renderer filtering flag"

key-files:
  created:
    - src/cli/color_policy.h
    - src/cli/color_policy.cpp
    - src/cli/tty_render.h
    - src/cli/tty_render.cpp
    - tests/unit/test_color_policy.cpp
    - tests/unit/test_tty_render.cpp
    - tests/golden/tty_basic.txt
  modified:
    - src/cli/options.h
    - src/cli/options.cpp
    - src/cli/commands/compare.cpp
    - src/cli/commands/compare.h
    - tools/gen_registry.py
    - src/core/registry.h
    - docs/checks/meta.tool_version.md
    - docs/checks/meta.missing_candidate.md
    - docs/checks/meta.extra_candidate.md
    - tests/support/docs/*.md (15 files)
    - tests/fixtures/registry/good/docs/good.sample.md
    - tests/unit/CMakeLists.txt
    - CMakeLists.txt

key-decisions:
  - "The accept/tune/silence triple is carried as three CheckDef string_view fields (explain_accept/explain_tune/explain_silence), populated at generation time from docs/checks/<id>.md's own '### Accept'/'### Tune'/'### Silence' sub-headings, rather than looked up through check_explain.cpp's generated, per-registry-typed explain_doc(CheckId) accessor -- this is what lets render_tty work unchanged against both builtin_registry() and test_registry(), neither of which it names by its own generated enum"
  - "CheckDef::explain_accept/explain_tune/explain_silence carry default member initializers (= \"\", mirroring transform_affected's own = false) so tests/unit/test_glob.cpp's pre-existing hand-built CheckDef aggregate keeps compiling under -Wmissing-field-initializers without listing the three new fields"
  - "cpp_string_literal now escapes embedded newlines to the literal two-character sequence \\n (and drops any \\r) -- the first generator callers whose source text is genuine multi-paragraph prose, versus every prior caller's single-line id/group/tolerance text"
  - "TTY prints to stdout only when --json was not requested in any form (bare or =path) -- a caller asking for machine-readable output on stdout does not also want human-readable text interleaved into the same stream; --report md=/junit= write independently either way"
  - "The one-line summary is word-wrapped (never elided) to terminal_width, matching the accept/tune/silence hint text's own treatment -- an early version left it unwrapped and it exceeded a 40-column terminal on its own, which is not a 'value column' and should never have been elided in the first place"

patterns-established:
  - "A CLI-layer renderer (src/cli/*.cpp) that needs unit-test coverage, despite CLI-layer code never previously being linked into mediadiff_unit_tests, is added as a direct source in tests/unit/CMakeLists.txt rather than moved into libmediadiff -- ENG-16's library/CLI boundary is preserved (the renderer still writes to no stream itself), only the test target's own source list grows"

requirements-completed: [REPORT-02, REPORT-03, CLI-08]

coverage:
  - id: D1
    description: "decide_color resolves CLI-08's full precedence matrix (--no-color unconditional; GITHUB_ACTIONS=true beats CI=true; NO_COLOR present-at-any-value; CI=true; else isatty) as a pure function over explicit ColorInputs, with --ascii independent of every colour rule"
    requirement: "CLI-08"
    verification:
      - kind: unit
        ref: "tests/unit/test_color_policy.cpp (9/9 pass, tag [color])"
        status: pass
      - kind: other
        ref: "manual CLI smoke test: piped stdout with CI=true GITHUB_ACTIONS=true emits ANSI escapes (cat -v shows ^[[38;2;255;255;000m); NO_COLOR=1 and --no-color both suppress escapes even with GITHUB_ACTIONS=true"
        status: pass
    human_judgment: false
  - id: D2
    description: "render_tty groups findings in the fixed container/video/timeline/audio/content/size/meta order, hides pass/ignored findings unless -v, and never wraps a finding row's value column (elides with a trailing marker instead)"
    requirement: "REPORT-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_tty_render.cpp (11/11 pass, tag [tty]); golden tests/golden/tty_basic.txt"
        status: pass
    human_judgment: false
  - id: D3
    description: "Every gating finding (severity warn/fail) prints its own accept/tune/silence triple, in that fixed order, individually rather than once per group; a non-gating finding prints none; a check with no tune knob still prints the tune label with explicit 'not applicable' text, sourced from documentation whose three level-3 sub-headings the generator now enforces as a build failure"
    requirement: "REPORT-03"
    verification:
      - kind: unit
        ref: "tests/unit/test_tty_render.cpp (triple-presence, triple-order, per-finding-repetition and tune-label tests, 4/11 of the suite's cases)"
        status: pass
      - kind: other
        ref: "manual verification: removed '### Tune' from docs/checks/meta.tool_version.md, ran gen_registry.py directly -> exit 1 naming 'meta.tool_version: missing required sub-heading(s) ... ### Tune'; restored the file and confirmed clean regeneration"
        status: pass
    human_judgment: false
  - id: D4
    description: "compare's stdout TTY output actually renders styled ANSI text end-to-end through the real binary against real fixtures (not merely proven in isolation by unit tests)"
    verification:
      - kind: manual_procedural
        ref: "./build/x64-linux/mediadiff compare tests/fixtures/snapshots/tracer_a.snap.json tests/fixtures/snapshots/tracer_b_skew.snap.json (plain, --ascii, --json-suppressed, piped-no-color, CI+GITHUB_ACTIONS, NO_COLOR, --no-color-beats-GITHUB_ACTIONS) all produced the documented output shape"
    human_judgment: true
    rationale: "The plan's own <verify> names a human-check step (real Windows console/conhost.exe/Windows Terminal rendering) that this Linux sandbox cannot perform -- CI capture redirects stdout, which disables colour by design. The Linux-side manual runs above prove the logic end-to-end; the cross-platform terminal rendering claim itself is deferred to the plan's documented human checkpoint."

duration: ~30min
completed: 2026-08-15
status: complete
---

# Phase 2 Plan 9: Colour Policy and the TTY Renderer Summary

**`decide_color` answers CLI-08's colour question as one pure, table-tested function; `render_tty` renders the fourth report format — fixed group order, only non-pass by default, and the accept/tune/silence triple under every gating finding, sourced from documentation the build now enforces the structure of.**

## Performance

- **Duration:** ~30 min
- **Started:** 2026-08-15 (continuing directly from 02-08)
- **Completed:** 2026-08-15
- **Tasks:** 2
- **Files modified:** 34 (7 created, 27 modified)

## Accomplishments

- `src/cli/color_policy.{h,cpp}`: `ColorInputs`/`ColorDecision`/`decide_color` — the one place CLI-08's colour question is answered, as a pure function over explicit data so the full `NO_COLOR`/`CI`/`GITHUB_ACTIONS`/`--no-color`/`--ascii` precedence matrix is table-testable with no process spawn or environment mutation. `--no-color` beats every environment signal; `GITHUB_ACTIONS=true` keeps colour enabled even under `CI=true` (GitHub's log viewer renders ANSI); `NO_COLOR`'s presence, not its value, disables colour; `--ascii` is independent of colour state entirely.
- `src/cli/options.{h,cpp}` gains `ColorArgs`/`add_color_flags`/`read_color_inputs` — `read_color_inputs` is the sole call site for `getenv`/`isatty` on the colour question, keeping `decide_color` itself free of either.
- `src/cli/tty_render.{h,cpp}`: `render_tty(ReportModel, CheckRegistry, ColorDecision, terminal_width)` renders the same shared `ReportModel` the JSON/Markdown/JUnit renderers consume — groups in the fixed `container→video→timeline→audio→content→size→meta` order, only non-pass/non-ignored findings by default (`-v` reveals both), and the accept/tune/silence triple under **every** gating finding individually. A finding row's value column is elided with a trailing marker rather than wrapped; the summary line and the triple's own hint prose are word-wrapped instead, since prose (not a tabular value) reads fine wrapped. Status glyphs (`✓⚠✗ℹ○‼`) swap to ASCII words (`OK`/`WARN`/`FAIL`/`INFO`/`SKIP`/`ERROR`) under `--ascii`, independent of colour; every styled `fmt::fg` call is gated on `ColorDecision::color_enabled` so a disabled decision emits no escape byte at all.
- `tools/gen_registry.py` extended: every registered check's `## Accept / Tune / Silence` section must now carry three level-3 sub-headings (`### Accept`, `### Tune`, `### Silence`) — a missing one fails the build, naming the check and the missing heading(s). Their bodies are embedded onto three new `CheckDef` fields (`explain_accept`/`explain_tune`/`explain_silence`) rather than routed through the generated, per-registry-typed `explain_doc(CheckId)` accessor, which is what lets `render_tty` fetch them via `registry.find(id)` alone and work unchanged against both `builtin_registry()` and `--symbol-prefix test_`'s `test_registry()`.
- All 18 pre-existing check docs (3 shipped `meta.*` checks, 15 `t.*` test-only checks, plus the registry generator's own `good` fixture) converted to the new three-sub-heading structure — verified live by temporarily deleting one `### Tune` heading and confirming the generator fails the build naming it, then restoring the file.
- `src/cli/commands/compare.cpp`: a second, `-v`-aware `ReportModel` (distinct from the JSON/Markdown/JUnit one, which never hides pass/ignored findings) is built and printed to stdout via `render_tty` whenever `--json` was not requested in any form; terminal width is queried once (`ioctl(TIOCGWINSZ)` / `GetConsoleScreenBufferInfo`), defaulting to 100 columns when it cannot be interrogated.

## Task Commits

1. **Task 1: The one colour decision** — `f0a2747` (feat)
2. **Task 2: TTY renderer with the accept/tune/silence triple** — `9154e4f` (feat)

Each task's commit independently built (`cmake --build`) and ran fully green (`ctest`) before landing — verified by temporarily reverting `compare.cpp`/`compare.h` and the `tty_render.*` CMake entries, confirming the color-only state built and passed its 9 `[color]` tests plus the full pre-existing 208-test baseline (217 total), then restoring the Task 2 content.

**Plan metadata:** _pending — see final commit below_

## Files Created/Modified

- `src/cli/color_policy.{h,cpp}` — `ColorInputs`, `ColorDecision`, `decide_color`
- `src/cli/tty_render.{h,cpp}` — `render_tty` and its internal glyph/elision/word-wrap helpers
- `src/cli/options.{h,cpp}` — `ColorArgs`, `add_color_flags`, `read_color_inputs`
- `src/cli/commands/compare.{h,cpp}` — `--no-color`/`--ascii` wiring, terminal-width query, stdout TTY print
- `tools/gen_registry.py` — `REQUIRED_ATS_SUBHEADINGS`, `extract_subsections`, `load_ats_sections`, `cpp_string_literal` newline escaping, `CheckDef::explain_accept/explain_tune/explain_silence` generation
- `src/core/registry.h` — the three new `CheckDef` fields (default-initialized to `""`)
- `docs/checks/meta.tool_version.md`, `meta.missing_candidate.md`, `meta.extra_candidate.md` — converted to `### Accept`/`### Tune`/`### Silence`
- `tests/support/docs/t.*.md` (15 files), `tests/fixtures/registry/good/docs/good.sample.md` — same conversion
- `tests/unit/test_color_policy.cpp` — 9 table-driven tests, tag `[color]`
- `tests/unit/test_tty_render.cpp` — 11 tests plus a golden, tag `[tty]`
- `tests/golden/tty_basic.txt` — new golden fixture
- `tests/unit/CMakeLists.txt`, `CMakeLists.txt` — new sources, header-set entries

## Decisions Made

See frontmatter `key-decisions` for full rationale on: sourcing the triple from `CheckDef` fields rather than the enum-typed `explain_doc` accessor; default-initializing the three new fields so a pre-existing hand-built `CheckDef` keeps compiling; `cpp_string_literal`'s newline-escaping extension; TTY-suppressed-under-`--json` behavior; and word-wrapping (not eliding) the summary line.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `tests/unit/test_glob.cpp`'s hand-built `CheckDef` needed default member initializers, not an edit**
- **Found during:** Task 1/2 combined build, first full compile after adding the three `explain_*` fields to `CheckDef`
- **Issue:** `-Wmissing-field-initializers` (`-Werror` project-wide) failed on `test_glob.cpp`'s pre-existing `make_def()` helper, which designated-initializes a synthetic `CheckDef` and does not (and should not) know about doc-derived explain text.
- **Fix:** Gave `explain_accept`/`explain_tune`/`explain_silence` default member initializers (`= ""`), mirroring `transform_affected`'s own `= false` precedent, rather than editing the untouched test file.
- **Files modified:** `src/core/registry.h`.
- **Commit:** `f0a2747` (Task 1's own commit).

**2. [Rule 1 - Bug] The generator's own `good` fixture doc needed the new sub-heading structure**
- **Found during:** Task 2, full-suite verification
- **Issue:** `tests/fixtures/registry/good/docs/good.sample.md` (the registry generator's own known-good fixture, `tests/unit/test_registry_generator.cpp`) predates this plan and still used the old bullet-list `## Accept / Tune / Silence` body — now correctly rejected by the generator's own stricter structure check, failing `unit.registry generator: good fixture exits 0 and produces a non-empty check_id.h`.
- **Fix:** Converted the fixture to `### Accept`/`### Tune`/`### Silence`, matching every other doc this plan converts.
- **Files modified:** `tests/fixtures/registry/good/docs/good.sample.md`.
- **Commit:** `9154e4f` (Task 2's own commit).

**3. [Rule 1 - Bug] The one-line summary was not width-bounded**
- **Found during:** Task 2, writing `test_tty_render.cpp`'s own width-elision test
- **Issue:** `render_summary_line` originally emitted a single unwrapped line; at a 40-column terminal width the summary line itself (60 bytes) violated this renderer's own "no rendered line exceeds `terminal_width`" contract — a bug in the renderer, not a gap the plan asked me to close, but directly caused by this plan's own new code.
- **Fix:** The summary line is now word-wrapped (never elided, since it is prose, not a value column) to `terminal_width`, reusing the same `wrap_text` helper the accept/tune/silence hint lines use.
- **Files modified:** `src/cli/tty_render.cpp`.
- **Commit:** `9154e4f` (Task 2's own commit).

---

**Total deviations:** 3 auto-fixed (1 Rule 3, 2 Rule 1)
**Impact on plan:** All three were necessary to keep the build green and this plan's own acceptance criteria checkable; none changes scope, architecture, or any frozen contract from prior waves.

## Known Stubs

None — TTY output renders real, resolved finding data end to end; no placeholder or empty-by-construction rendering path exists.

## Issues Encountered

- **`--ascii`'s status-word set extends beyond the design doc's own four-word list.** Doc 01 section 9 names only `OK WARN FAIL INFO` (for pass/warn/fail/info); this renderer also needs a `Status::skipped` and `Status::error` glyph. `SKIP`/`ERROR` extend the same convention in `--ascii` mode (`○`/`‼` otherwise) rather than reusing an existing word, so a skip and an error stay visually distinguishable. Not a deviation from a stated requirement — the doc sentence simply didn't enumerate all six `Status` values.
- **Manual Windows-console verification (CLI-08's own human-check) could not be performed in this Linux sandbox.** Recorded as `coverage: D4`'s `human_judgment: true` entry per this plan's own `<verify>` block, which states CI/automated environments cannot observe this: capturing stdout redirects it, disabling colour by design. Linux-side manual runs (piped, `--ascii`, `NO_COLOR`, `CI`+`GITHUB_ACTIONS`, `--no-color`) all confirmed the underlying escape-sequence logic is correct; only the real-console rendering claim itself is deferred.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `render_tty`/`decide_color`/`add_color_flags`/`read_color_inputs` are stable, tested surfaces; plan 02-10's own action text already anticipates folding `--no-color`/`--ascii` (registered per-subcommand here) into a shared `add_common_options(CLI::App&, CliOptions&)` alongside `--profile`/`--config`/`--set`/`--tol`/`--json`/`--report`/`--strict`/`-q`/`-v` — this plan's `ColorArgs`/`add_color_flags` shape is a direct, drop-in fit for that consolidation.
- `CheckDef::explain_accept/explain_tune/explain_silence` are compiled-in and registry-indexed; plan 02-10's `explain <check.id>` command can read the SAME three fields directly, or continue to use `check_explain.cpp`'s existing full-text `explain_doc` accessor for its own different purpose (the whole three-section document vs. this plan's split triple) — both paths now coexist without conflict.
- All four report formats (JSON, Markdown, JUnit, TTY) are complete; doc 01 section 9's full report-format surface is implemented.
- No blockers.

---
*Phase: 02-core-engine*
*Completed: 2026-08-15*

## Self-Check: PASSED

All 7 created files verified present on disk; both task commit hashes (`f0a2747`, `9154e4f`) verified present in `git log --oneline --all`.
