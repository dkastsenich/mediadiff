---
phase: 05-timeline-analysis
plan: 23
subsystem: timeline
tags: [av-drift, drift-pattern, narrow-vocabulary, documentation, gap-closure, SC1, TIME-07, DOC-04, WINDOWS]

# Dependency graph
requires:
  - phase: 05-timeline-analysis
    provides: "05-22's code-level implementation of the narrow-vocabulary branch (DriftPattern narrowed to three spellings, step_time_ms removed) and 05-21's recorded human decision (05-STEP-DESIGN.md `## Decision`: narrow-vocabulary, span:declared) plus its Orchestrator note correcting the span:observed MP4-side finding"
provides:
  - "docs/checks/timeline.av_drift.pattern.md narrowed to three published values (constant-offset/linear-drift/irregular), with a new '### Limits of timestamp-only detection' subsection stating the seamless-retrim limitation, the dropout-vs-sync-step ambiguity (A1), and which reading the shipped mapping assumes -- proven compiled into `mediadiff explain timeline.av_drift.pattern`"
  - "05-CHECK-ROSTER.md's `## Amendments` section (dated, citing UD-1/05-21/05-STEP-DESIGN.md's Decision) recording the withdrawn `step` value, the unchanged check id/semantics, and doc 04's superseded sections"
  - "ROADMAP.md Phase 5 SC1 and REQUIREMENTS.md TIME-07 amended in place (scoped one-line edits) to state the outcome narrow-vocabulary actually ships"
  - "src/core/checks.def's timeline.av_drift.pattern comment narrowed to three spellings with a dated amendment note; the sibling timeline.av_drift comment's stale step_time_ms reference corrected -- diff verified comment-only"
  - "WINDOWS.md #32: the residual MP4-to-TS timeline.av_drift/timeline.av_drift.pattern false finding recorded and waived with the human's 05-21 recorded reason (span:declared kept, priming/padding-aware span filed as a follow-up)"
affects: [ROADMAP-SC1, TIME-07, DOC-04, docs/checks/timeline.av_drift.pattern.md, docs/checks/timeline.av_drift.md, 05-CHECK-ROSTER.md]

# Actuals (#2632)
actuals:
  tokens: 7787
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Documentation-only gap-closure plan: doc/roster/spec amendments are dated, cite the originating decision record (UD-1/05-STEP-DESIGN.md), and are verified end to end through the compiled `mediadiff explain` output rather than merely reading correct on disk -- gen_registry.py only compiles the three fixed level-2 sections ('## What it measures'/'## Why it matters'/'## Accept / Tune / Silence') into the binary, so a new level-2 heading outside those three is silently dropped from `explain`; new content must nest under an existing required section as a level-3 sub-heading to actually reach the compiled-in doc."

key-files:
  created: []
  modified:
    - docs/checks/timeline.av_drift.pattern.md
    - docs/checks/timeline.av_drift.md
    - .planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md
    - .planning/ROADMAP.md
    - .planning/REQUIREMENTS.md
    - src/core/checks.def
    - .planning/WINDOWS.md

key-decisions:
  - "Implemented exactly the branch 05-STEP-DESIGN.md's `## Decision` recorded: narrow-vocabulary (docs/roster/SC1/TIME-07 amended, `step` withdrawn) and span:declared (WINDOWS.md #32 waived, no code change)."
  - "The new 'Limits of timestamp-only detection' content had to be authored as a level-3 sub-heading inside the existing '## What it measures' section, not a new level-2 section -- tools/gen_registry.py compiles only the three fixed required level-2 sections into check_explain.cpp, and a fourth level-2 heading is silently dropped from `mediadiff explain`'s output. Discovered empirically: the first attempt (a standalone '## Limits of timestamp-only detection' section) compiled cleanly but the acceptance grep against the real binary output found 0 matches."
  - "The 'undetectable from timestamps alone' phrase had to be kept on a single un-wrapped markdown source line -- the doc compiler preserves the source's own line-wrapping as literal newlines in the compiled string, so a phrase wrapped across two markdown lines is split by an embedded newline in `explain`'s output and a single-line grep against it reports 0."
  - "Also fixed (Rule 1, beyond the plan's literal file scope but within files_modified and directed by the orchestrator's project_specifics): checks.def's timeline.av_drift comment (line ~1416, not the pattern comment the plan's Task 2 action literally scoped) and docs/checks/timeline.av_drift.md both still referenced the now-nonexistent `step_time_ms` evidence field after 05-22 removed it entirely -- corrected in both files since a stale reference to a removed field is exactly the kind of vocabulary-code mismatch this plan exists to close."
  - "Cross-references to the roster's own '## Amendments' heading were written as plain prose ('see the Amendments section below') rather than literal backtick-quoted heading text, after the first draft's literal '`## Amendments`' cross-references made `grep -c \"## Amendments\"` report 3 instead of the acceptance criterion's required 1."
  - "TIME-07 marked complete via the shared-ID readiness gate (both 05-22 and 05-23 declaring it now have SUMMARYs). DOC-04 stays open -- `requirements.ready-ids` reports it blocked because 05-24-PLAN.md and 05-25-PLAN.md also declare DOC-04 and have not yet produced SUMMARYs; not forced, per this plan's own instruction."

patterns-established: []

requirements-completed: [TIME-07]  # DOC-04 also declared by sibling plans 05-24/05-25 (requirements.ready-ids shared-ID gate) -- stays open until those also finish, per instruction not to force it.

coverage:
  - id: D1
    description: "The compiled-in explain text for timeline.av_drift.pattern states the timestamp-only limits (seamless-trim undetectable until Phase 6, the discontinuity/dropout-vs-sync-step ambiguity, which reading the shipped mapping assumes) and the decided three-value vocabulary, proven through a real build of the real binary"
    requirement: "TIME-07"
    verification:
      - kind: other
        ref: "cmake --build --preset x64-linux && ./build/x64-linux/mediadiff explain timeline.av_drift.pattern | grep -c \"undetectable from timestamps alone\" (1) && grep -c \"Limits of timestamp-only detection\" (2, heading + cross-reference)"
        status: pass
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure (994/994, 6 pre-existing skips)"
        status: pass
    human_judgment: false
  - id: D2
    description: "SC1, TIME-07, the roster and the checks.def comment all match the decided vocabulary; checks.def's non-comment lines (check ids, attributes) are untouched"
    requirement: "TIME-07"
    verification:
      - kind: other
        ref: "grep -c \"## Amendments\" 05-CHECK-ROSTER.md == 1; ROADMAP SC1 and REQUIREMENTS TIME-07 lines each contain 'amended'/'UD-1'; git diff -U0 -- src/core/checks.def | grep -E '^[-+][^-+]' | grep -v '^[-+][[:space:]]*#' | wc -l == 0 (comment-only diff)"
        status: pass
      - kind: integration
        ref: "ctest --preset x64-linux -N -R '^unit\\.registry' (5 tests, id count unaffected); ctest --preset x64-linux --output-on-failure (994/994)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The residual MP4-to-TS timeline.av_drift/timeline.av_drift.pattern finding is recorded in WINDOWS.md and waived with the human's recorded span:declared reason from 05-STEP-DESIGN.md's Decision; #26/#27/#28/#30 confirmed fixed, #29 confirmed untouched; full local phase state verified"
    requirement: "DOC-04"
    verification:
      - kind: other
        ref: "gsd-tools windows status; grep '| fixed |' for #26/#27/#28/#30; WINDOWS.md #32 status=waived with the verbatim recorded reason"
        status: pass
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure (994/994); MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure (994/994) and -R golden (15/15); scripts/lint_corpus_digest_provenance.sh (all clauses passed); scripts/assert_corpus_digest.sh (162 lines compared, exit 0)"
        status: pass
    human_judgment: false

# Metrics
duration: ~12min
completed: 2026-09-18
status: complete
---

# Phase 5 Plan 23: Documentation Half of Gap 1 Closure (Narrow-Vocabulary) Summary

**Every published statement of `timeline.av_drift.pattern`'s vocabulary now matches what 05-22 shipped -- three values, not four -- with the seamless-trim and discontinuity-ambiguity limits compiled into `mediadiff explain`, and the residual MP4-to-TS drift finding recorded and waived in WINDOWS.md with the human's own recorded reason.**

## Performance

- **Duration:** ~12 min
- **Started:** 2026-09-18T19:44:12Z (approximate, per STATE.md's prior session timestamp)
- **Completed:** 2026-09-18T19:56:11Z
- **Tasks:** 3/3 completed
- **Files modified:** 7

## Accomplishments

- `docs/checks/timeline.av_drift.pattern.md`: the vocabulary list narrowed from four to three values (`constant-offset` / `linear-drift` / `irregular`), with a one-sentence note that a spliced trim now reports `irregular` with its own residual max. A new `### Limits of timestamp-only detection` subsection (nested inside the compiled `## What it measures` section -- see Key Decisions) states: (a) a seamlessly re-timestamped trim is undetectable from timestamps alone until Phase 6's audio decode path exists; (b) a timestamp gap is ambiguous between a dropout (content missing, sync preserved) and a sync step (content contiguous, timestamps jumped), citing 05-STEP-DESIGN.md's A1 proof that the two edits can produce byte-identical files; (c) the shipped mapping silently assumes the sync-step reading for both. The `## Why it matters` and `### Tune` sections were also corrected -- both previously made false claims about the check existing to name a `step` pattern that no longer exists.
- Proven end to end through a real build: `./build/x64-linux/mediadiff explain timeline.av_drift.pattern` contains both "undetectable from timestamps alone" (on one un-wrapped source line, required because the doc compiler preserves markdown line-wrapping as literal newlines in the compiled string) and "Limits of timestamp-only detection" (2 occurrences: the heading itself plus one cross-reference).
- `docs/checks/timeline.av_drift.md`'s stale `step_time_ms` evidence reference (a field 05-22 removed entirely) corrected to stop describing evidence the code can no longer produce.
- `05-CHECK-ROSTER.md`: the `timeline.av_drift.pattern` row and item 5 updated to the three-value vocabulary; a new `## Amendments` section (dated 2026-09-18, citing UD-1/05-21/05-STEP-DESIGN.md's Decision) records the removed value and why, confirms the check id and `exact`/fail semantics are unchanged, and records that `claude_docs/04-timeline-analysis.md` section 3 point 4 and section 5's Step recipe are superseded for Phase 5 until Phase 6 -- per this plan's own A1, doc 04 itself stays untouched.
- `ROADMAP.md`'s Phase 5 SC1 clause 1 and `REQUIREMENTS.md`'s `TIME-07` each received a scoped one-line edit stating the narrowed vocabulary and the amendment date/citation, matching the plan's own prohibition against wholesale rewrites (verified: exactly one removed line in the ROADMAP diff).
- `src/core/checks.def`'s `timeline.av_drift.pattern` comment narrowed from "four spellings exactly" to "three spellings exactly" with a dated amendment note; the sibling `timeline.av_drift` comment's own stale `step_time_ms` reference (flagged by the orchestrator's project_specifics, not literally in the plan's Task 2 scope but within `files_modified` and directly required by the objective) was also corrected. The full `checks.def` diff was verified comment-only (`git diff -U0` filtered to non-comment changed lines reports 0); no check id, attribute, or non-comment line changed.
- `WINDOWS.md` entry #32 appended: the lossless MP4-to-TS remux pairs (`timeline_start_base.mp4` vs `timeline_start_shift.ts`/`timeline_avoffset_unknown.ts`) report `timeline.av_drift` fail and `timeline.av_drift.pattern` fail (`irregular`, `residual_max_ms=42`) because the checkpoint span uses libavformat's *estimated* TS audio duration, and observed TS packet extents carry AAC priming/padding with no edit list that neither span source can exclude -- worded per 05-STEP-DESIGN.md's Orchestrator note (the corrected finding), not the earlier harness-variant MP4-side regression claim the plan's project_specifics explicitly warned against restating. Waived with the human's verbatim recorded `span:declared` reason from 05-STEP-DESIGN.md's `## Decision`.
- `gsd-tools windows status` confirmed #26/#27/#28/#30 all `fixed` and #29 untouched (`open`), per the plan's own verification step.
- Final local phase check, all green: `ctest --preset x64-linux --output-on-failure` (994/994, 6 pre-existing skips); `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure` (994/994) and `-R golden` (15/15); `scripts/lint_corpus_digest_provenance.sh` (all clauses passed); `scripts/assert_corpus_digest.sh` (162 lines compared, exit 0 -- no fixture recipe changed by this documentation-only plan, so no `timeline_drift_step.mp4` exclusion was needed).

## Task Commits

Each task was committed atomically:

1. **Task 1: The compiled-in explain text states the timestamp-only limits and the decided vocabulary, proven end to end through the build** - `94695b3` (docs)
2. **Task 2: SC1, TIME-07, the roster and the checks.def comment match the decided vocabulary** - `7d53d13` (docs)
3. **Task 3: Record the residual MP4-to-TS drift outcome per the span decision, and verify the whole phase state** - `403de58` (docs)

**Plan metadata:** this commit (docs: complete 05-23 plan)

## Files Created/Modified

- `docs/checks/timeline.av_drift.pattern.md` - three-value vocabulary, new Limits of timestamp-only detection subsection, corrected Why-it-matters/Tune prose
- `docs/checks/timeline.av_drift.md` - stale `step_time_ms` evidence reference removed
- `.planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md` - pattern row/item 5 vocabulary updated, new dated `## Amendments` section
- `.planning/ROADMAP.md` - Phase 5 SC1 clause 1 amended (scoped one-line edit)
- `.planning/REQUIREMENTS.md` - TIME-07 amended (scoped one-line edit); TIME-07 checkbox/traceability marked complete
- `src/core/checks.def` - timeline.av_drift.pattern comment narrowed to three spellings with a dated amendment; timeline.av_drift comment's stale step_time_ms reference corrected
- `.planning/WINDOWS.md` - entry #32 appended and waived (residual MP4-to-TS timeline.av_drift/pattern finding)

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- Implemented exactly the branch 05-STEP-DESIGN.md's `## Decision` recorded: narrow-vocabulary + span:declared.
- The new doc content had to nest as a level-3 sub-heading inside an existing required level-2 section, discovered empirically against the real compiled `explain` output (gen_registry.py silently drops any fourth level-2 section).
- The "undetectable from timestamps alone" phrase had to stay on one un-wrapped source line so the compiled string (which preserves markdown line-wrapping as literal newlines) doesn't split it across a grep boundary.
- Beyond the plan's literal Task 2 file scope, also fixed checks.def's `timeline.av_drift` comment's stale `step_time_ms` reference (directed by the orchestrator's project_specifics) and `docs/checks/timeline.av_drift.md`'s matching stale reference.
- TIME-07 marked complete; DOC-04 left open per the shared-ID readiness gate (05-24/05-25 also declare it, not yet summarized).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] The new "Limits of timestamp-only detection" content was silently dropped from the compiled explain output on first attempt**
- **Found during:** Task 1, after the first build+grep verification returned 0 matches for "undetectable from timestamps alone"
- **Issue:** `tools/gen_registry.py` only compiles the three fixed, required level-2 headings ("## What it measures", "## Why it matters", "## Accept / Tune / Silence") into `check_explain.cpp`; a standalone fourth level-2 section ("## Limits of timestamp-only detection") is extracted by `extract_sections` but never emitted, so it silently disappears from `mediadiff explain`'s real output while the source markdown looked complete.
- **Fix:** Moved the content to a `### Limits of timestamp-only detection` level-3 sub-heading nested inside the existing `## What it measures` section, which IS compiled.
- **Files modified:** `docs/checks/timeline.av_drift.pattern.md`
- **Verification:** `./build/x64-linux/mediadiff explain timeline.av_drift.pattern | grep -c "Limits of timestamp-only detection"` reports 2 (heading + cross-reference); the acceptance-criterion grep for "undetectable from timestamps alone" passes.
- **Committed in:** `94695b3` (Task 1 commit)

**2. [Rule 1 - Bug] The target phrase was split across a markdown line-wrap in the compiled output**
- **Found during:** Task 1, same verification pass, after fixing deviation #1
- **Issue:** The doc compiler embeds each section's source text with its own line-wrapping preserved as literal `\n` bytes. The first draft wrapped "undetectable from timestamps" and "alone." across two markdown lines, so the compiled `explain` output printed them with a newline in between -- a single-line `grep -c "undetectable from timestamps alone"` reported 0 even though the words were all present.
- **Fix:** Rewrote the sentence so the full phrase "A seamlessly re-timestamped trim is undetectable from timestamps alone." sits on one un-wrapped source line.
- **Files modified:** `docs/checks/timeline.av_drift.pattern.md`
- **Verification:** `grep -c "undetectable from timestamps alone"` against the real compiled binary output reports 1.
- **Committed in:** `94695b3` (Task 1 commit)

**3. [Rule 1 - Bug] `checks.def`'s `timeline.av_drift` comment and `docs/checks/timeline.av_drift.md` both still described a now-nonexistent `step_time_ms` evidence field**
- **Found during:** Task 1/2, directed explicitly by the orchestrator's project_specifics ("src/core/checks.def still mentions step_time_ms around line 1416; it is in your files_modified, so fix that text")
- **Issue:** 05-22 removed `DriftFit::step_time_ms` and its evidence-emission entirely, but the explanatory comment above `timeline.av_drift`'s own `[[check]]` block (not the pattern block Task 2's action literally scoped) and `docs/checks/timeline.av_drift.md`'s "Evidence also carries..." paragraph both still claimed the field exists ("present only when timeline.av_drift.pattern is step") -- a stale claim about evidence the code can no longer produce, exactly the class of mismatch this plan exists to close.
- **Fix:** Removed the `step_time_ms` clause from both locations, keeping the surrounding sentence structure intact.
- **Files modified:** `src/core/checks.def`, `docs/checks/timeline.av_drift.md`
- **Verification:** `grep -rn step_time_ms src/core/checks.def docs/checks/timeline.av_drift.md` finds no remaining reference; full suite green; checks.def diff confirmed comment-only.
- **Committed in:** `94695b3` (av_drift.md, Task 1 commit), `7d53d13` (checks.def, Task 2 commit)

**4. [Rule 1 - Bug] Literal `## Amendments` cross-references inflated the roster's own heading-count acceptance check**
- **Found during:** Task 2, verification of the acceptance criterion `grep -c "## Amendments" .planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md` reporting 1
- **Issue:** The first draft's two cross-references (the pattern row's note and item 5's note) both quoted the literal heading text `` `## Amendments` ``, which a plain `grep -c` on that string matches identically to the real heading -- inflating the count to 3.
- **Fix:** Reworded both cross-references to plain prose ("see the Amendments section below") that doesn't contain the literal heading string.
- **Files modified:** `.planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md`
- **Verification:** `grep -c "## Amendments"` reports 1.
- **Committed in:** `7d53d13` (Task 2 commit)

---

**Total deviations:** 4 auto-fixed (all Rule 1 -- documentation correctness/consistency bugs found while proving the compiled output, none architectural).
**Impact on plan:** All four were necessary to make the plan's own must_haves truth ("the compiled explain text states the limits") actually hold against the real binary, and to make the checks.def/roster fixes complete rather than partial. No scope creep beyond documentation and comment text; no product code, test, or fixture touched.

## Issues Encountered

None beyond the deviations above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Gap 1 (SC1's step-vocabulary question) is now closed at both the code/test level (05-22) and the documentation/contract level (this plan): every published statement of `timeline.av_drift.pattern`'s vocabulary matches what the code can produce, verified through the real compiled binary.
- `TIME-07` is marked complete by this plan (both plans declaring it, 05-22 and 05-23, have SUMMARYs now).
- `DOC-04` stays open -- also declared by `05-24-PLAN.md` and `05-25-PLAN.md`, per the shared-ID readiness gate. Not forced, per instruction.
- The residual MP4-to-TS `timeline.av_drift`/`timeline.av_drift.pattern` false finding is recorded and waived in `WINDOWS.md` (#32), not closed -- a priming/padding-aware span is the follow-up.
- **Separate observation, not fixed here, not a new WINDOWS entry (per instruction):** on the same MP4-to-TS pairs, the declared `size.stream_bitrate` audio warn's causal comment in `tests/integration/test_timeline_start_duration.cpp` attributes it to PES/PSI overhead; the orchestrator's own measurement found the actual cause is the 7-byte ADTS header MPEG-TS's muxer adds to each of ~173 AAC frames (both sides' DTS spans equal, ~4.017s; TS audio carries 1218 more bytes, +3.5% against the 3% warn threshold). Flagged here for the orchestrator to raise with the human together with the priming-aware-span follow-up; `test_timeline_start_duration.cpp` was not edited.
- claude_docs/04-timeline-analysis.md remains untouched (per A1) -- the divergence is recorded in `05-CHECK-ROSTER.md`'s `## Amendments` instead, as the plan requires.
- No blockers for `05-24`/`05-25`.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-18*

## Self-Check: PASSED

- `docs/checks/timeline.av_drift.pattern.md` - FOUND
- `docs/checks/timeline.av_drift.md` - FOUND
- `.planning/phases/05-timeline-analysis/05-CHECK-ROSTER.md` - FOUND
- `.planning/ROADMAP.md` - FOUND
- `.planning/REQUIREMENTS.md` - FOUND
- `src/core/checks.def` - FOUND
- `.planning/WINDOWS.md` - FOUND
- Commit `94695b3` - FOUND (git log)
- Commit `7d53d13` - FOUND (git log)
- Commit `403de58` - FOUND (git log)
- Full `ctest` suite: 994/994 passed, 0 failed (6 pre-existing skips, unrelated to this plan)
- `MEDIADIFF_DESIGNATED_LEG=1 ctest`: 994/994 passed, 0 failed; `-R golden`: 15/15 passed
- `scripts/lint_corpus_digest_provenance.sh`: all clauses passed
- `scripts/assert_corpus_digest.sh`: 162 lines compared, exit 0
