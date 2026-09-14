---
phase: 04-video-analysis
plan: 18
subsystem: testing
tags: [documentation, registry, list-checks, inspect, gap-closure, mutation-testing]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-12's video.hdr.coherence state-semantic check and 04-16's full 133-fixture corpus"
provides:
  - "A video.hdr.coherence.md Accept section that correctly states pass means neither side is flagged, not that both files agree"
  - "A src/core/registry.h Semantic comment that correctly states the semantic field IS printed by plain list-checks"
  - "An integration assertion pinning that semantic= column so a future regression fails a test instead of shipping unnoticed"
  - "A test_video_inspect_section.cpp exclusion predicate derived from scripts/gen_corpus.sh recipes, immune to the output-under-test emptying silently"
affects: ["04-VERIFICATION", "any future phase touching video.hdr.coherence docs, list-checks output, or the inspect-section corpus test"]

# Actuals (#2632)
actuals:
  tokens: 3700
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Recipe-derived test predicates (scripts/check_corpus.sh's own convention) applied to a second test: judge scope from the generator's source, never from the output under test"
    - "Mutation-proof pattern: every changed/added assertion in this plan was observed failing against a deliberate, reverted mutation before being declared load-bearing"

key-files:
  created: []
  modified:
    - docs/checks/video.hdr.coherence.md
    - src/core/registry.h
    - tests/integration/test_list_checks.cpp
    - tests/integration/test_video_inspect_section.cpp

key-decisions:
  - "WR-02 fix wording follows 04-REVIEW.md's suggested replacement verbatim in spirit: pass means NEITHER side is flagged, explicitly denies the both-sides-agree reading, and names both flagged spellings."
  - "registry.h's corrected comment states the additive-safety property precisely: no EXISTING row's spelling changes, not 'the field appears in no serialized output' (which was false)."
  - "Task 2's mutation renamed the format string's semantic= label to kind= (keeping the value argument) rather than deleting the argument outright, to avoid tripping an unrelated -Wunused-function/-Werror build break on semantic_to_string and keep the demonstration a ctest failure, not a build failure."
  - "Task 3's mutation gated compute_stream_scopes() in all six per-file-copy video analyzers (color/frame_types/gop/hdr/interlace/stream_params) for a rare codec (huffyuv, 2 fixtures) rather than one emit_* function, because only fully removing the stream's Scope empties groups.video entirely — the exact blind spot the old has_video_stream() predicate had. Gating a single emit_* only removes some of the nine checks, which the OLD test's own per-check assertions already caught, and would not have demonstrated the predicate's actual failure mode."

patterns-established: []

requirements-completed: [VIDEO-01, VIDEO-02, VIDEO-10]

coverage:
  - id: D1
    description: "video.hdr.coherence.md's Accept section correctly describes what a state-semantic pass means (WR-02)"
    requirement: VIDEO-10
    verification:
      - kind: unit
        ref: "python3 doc-content assertion (Task 1 verify script): 'neither' and 'identical value' present, heading order unchanged"
        status: pass
      - kind: integration
        ref: "ctest -R integration.doc03_coverage"
        status: pass
    human_judgment: false
  - id: D2
    description: "src/core/registry.h's Semantic comment no longer denies the field is serialized; names list-checks as the output that prints it"
    requirement: VIDEO-10
    verification:
      - kind: unit
        ref: "python3 content assertion (Task 1 verify script): 'in no serialized output' absent, 'list-checks' present"
        status: pass
      - kind: integration
        ref: "ctest -R integration.list_checks (all 7 cases, including the new semantic= pin)"
        status: pass
    human_judgment: false
  - id: D3
    description: "New integration assertion pins plain list-checks' semantic= token per row, matching each check's registered semantic, count equal to registry size"
    requirement: VIDEO-01
    verification:
      - kind: integration
        ref: "tests/integration/test_list_checks.cpp: 'list_checks - ENG-01: plain output carries a semantic= token for every row, matching each check's semantic'"
        status: pass
    human_judgment: false
  - id: D4
    description: "Mutation proof: renaming the semantic= label in list_checks.cpp's plain-branch format string makes the new assertion fail (0 == 61); reverting restores green (7/7)"
    verification:
      - kind: integration
        ref: "ctest -R integration.list_checks, observed red then green (see Task Commits below)"
        status: pass
    human_judgment: false
  - id: D5
    description: "test_video_inspect_section.cpp's has_video_stream(report) predicate replaced by a committed, sorted, name-based exclusion list (kNoVideoStreamFixtures) derived from scripts/gen_corpus.sh"
    requirement: VIDEO-02
    verification:
      - kind: unit
        ref: "python3 content assertion (Task 3 verify script): no report-derived has_video_stream(...json...) call remains, tracer_empty.mp4 named in the list"
        status: pass
      - kind: integration
        ref: "ctest -R 'integration.video inspect section' (both TEST_CASEs)"
        status: pass
    human_judgment: false
  - id: D6
    description: "Both TEST_CASEs assert a listed fixture (tracer_empty.mp4) renders an EMPTY video group, assert every listed name exists among enumerated fixtures (staleness guard), and keep the non-zero asserted-fixture-count guard (132 of 133 fixtures)"
    verification:
      - kind: integration
        ref: "ctest -R 'integration.video inspect section', both TEST_CASEs passing with 132 fixtures asserted, 1 excluded"
        status: pass
    human_judgment: false
  - id: D7
    description: "Mutation proof: gating all six video analyzers' compute_stream_scopes() for huffyuv-coded streams fully empties groups.video for video_noparser.mkv/_copy.mkv, and the new test FAILS all nine checks for both fixtures (the exact regression the old report-derived predicate would have silently skipped); reverting restores green (2/2)"
    verification:
      - kind: integration
        ref: "ctest -R 'integration.video inspect section', observed red (18 failed assertions naming both fixtures) then green"
        status: pass
    human_judgment: false

duration: 55min
completed: 2026-09-13
status: complete
---

# Phase 4 Plan 18: Gap Closure — WR-02 Doc/Comment Correction and Inspect-Test Self-Reference Fix Summary

**Rewrote a shipped check doc's misleading pass-semantics claim, corrected a false registry.h comment and pinned it with a new assertion, and replaced a corpus test's self-referential "does the output under test say it has video" predicate with a corpus-recipe-derived exclusion list — every changed/added assertion proven load-bearing via a deliberate, reverted mutation.**

## Performance

- **Duration:** 55 min
- **Started:** 2026-09-13 (session start)
- **Completed:** 2026-09-13
- **Tasks:** 3
- **Files modified:** 4

## Accomplishments

- `docs/checks/video.hdr.coherence.md`'s Accept section now correctly states that a `state`-semantic `pass` means neither side's value is one of the two flagged spellings (`hdr_meta_sdr_transfer` / `pq_without_mdcv`) — not that both files report the identical value — matching `compare_state`'s actual implementation (`src/compare/state.cpp:67-91`). WR-02 closed.
- `src/core/registry.h`'s `Semantic` comment no longer denies that the `semantic` field is serialized anywhere; it now correctly states plain `mediadiff list-checks` prints it, and the additive-safety property is worded as "no existing row's spelling changes."
- A new integration assertion in `tests/integration/test_list_checks.cpp` pins that `semantic=` column: every row of plain `list-checks` carries a `semantic=` token matching the check's registered semantic, and the count of such rows equals the registry size — the exact test that was missing when the false registry.h claim shipped unnoticed.
- `tests/integration/test_video_inspect_section.cpp`'s `has_video_stream(const nlohmann::json&)` predicate — which judged a fixture's video-stream status by reading `groups.video` from the very report the test was asserting over — is replaced by a committed, sorted, file-local exclusion list (`kNoVideoStreamFixtures`, currently `{"tracer_empty.mp4"}`) derived from `scripts/gen_corpus.sh`'s recipes. Both TEST_CASEs now also assert a listed fixture renders an EMPTY video group and that every listed name exists among the enumerated fixtures (staleness guard).
- Every changed or added assertion in this plan was observed FAILING against a deliberate mutation of the behavior it names, then observed passing again after the mutation was reverted (see Task Commits below for both red/green outputs).

## Task Commits

Each task was committed atomically:

1. **Task 1: Correct what a `state`-semantic pass means (WR-02) and the registry comment behind it** - `17d3405` (docs)
2. **Task 2: Pin the `semantic=` output so the corrected comment cannot silently become false again** - `00ad358` (test)
3. **Task 3: Decide which fixtures must render a video section from the corpus, not from the output under test** - `e9e7b92` (test)

_No plan-metadata-only commit beyond this SUMMARY; this plan carried no TDD RED/GREEN split at the plan-frontmatter level (tasks 2 and 3 are individually `tdd="true"` but each produced a single commit containing the completed, verified assertion plus its recorded mutation proof, per the plan's own task structure)._

## Files Created/Modified

- `docs/checks/video.hdr.coherence.md` - Accept section rewritten to state the neither-side-flagged rule and explicitly deny the both-sides-agree reading
- `src/core/registry.h` - `Semantic` enum comment's false "field appears in no serialized output" claim replaced with the true additive-safety statement
- `tests/integration/test_list_checks.cpp` - new `TEST_CASE` pinning plain `list-checks`' `semantic=` column
- `tests/integration/test_video_inspect_section.cpp` - `has_video_stream` removed; `kNoVideoStreamFixtures` (recipe-derived exclusion list), staleness guard, and empty-video-group assertions added to both TEST_CASEs; file header rewritten to explain the predicate's disqualification and the replacement's authority

## Mutation Proofs (Recorded)

**Task 2 — `semantic=` token removal:**
Renamed the plain-branch format string's label from `semantic=` to `kind=` in `src/cli/commands/list_checks.cpp` (kept the value argument to avoid an unrelated `-Wunused-function`/`-Werror` build break on `semantic_to_string`). Rebuild succeeded; ctest FAILED:
```
CHECK( semantic_token_count == registry.size() )
with expansion:
  0 == 61
CHECK( found_state_check ) -> false
CHECK( found_exact_check ) -> false
```
Reverted the label; rebuild + ctest: `100% tests passed, 0 tests failed out of 7`.

**Task 3 — emptying `groups.video` for a non-listed fixture:**
Temporarily gated `compute_stream_scopes()` in all six per-file-copy video analyzers (`color.cpp`, `frame_types.cpp`, `gop.cpp`, `hdr.cpp`, `interlace.cpp`, `stream_params.cpp`) to drop any `huffyuv`-coded stream to no-scope. This fully emptied `groups.video` (confirmed via `inspect --json`: `[]`) for `video_noparser.mkv` and `video_noparser_copy.mkv`, while their container/audio/etc. groups stayed populated. Rebuild succeeded; ctest FAILED both TEST_CASEs — all nine SC2 checks missing for both fixtures (18 total assertion failures), each naming the failing fixture in `INFO`:
```
CHECK( present_ids.count(check_id) == 1 )
...fixture: .../video_noparser.mkv check: video.codec
...fixture: .../video_noparser_copy.mkv check: video.frame_count
test cases: 1 | 0 passed | 1 failed  (x2 TEST_CASEs)
0% tests passed, 2 tests failed out of 2
```
This is exactly the regression the OLD `has_video_stream()` predicate could not have caught: since it read `groups.video` off the very report under test, an emptied group would have been read as "no video stream — skip this fixture," silently removing both fixtures from the assertion loop instead of failing. Reverted the mutation (all six files restored byte-identical to their pre-mutation state, confirmed via `git status --porcelain src/analyzers/` being empty); rebuild + ctest: `100% tests passed, 0 tests failed out of 2`.

As of this plan the enumerated corpus is 133 media fixtures: 132 asserted against the nine SC2 checks, 1 (`tracer_empty.mp4`) on the exclusion list — matching the plan's `must_haves`.

## Decisions Made

- Task 2's mutation targeted the format string's *label* (`semantic=` → `kind=`) rather than literally deleting the `semantic={}` placeholder and its argument, because deleting the only call site of the anonymous-namespace `semantic_to_string` function trips `-Wunused-function` under this project's `-Werror` build config — that would have produced a *build* failure, not the intended *test* failure demonstrating the assertion's load-bearing-ness. The rename achieves the same demonstration (the `semantic=` token vanishes from output) without an unrelated compile break.
- Task 3's mutation point was chosen at `compute_stream_scopes()` (present as a verbatim per-file copy in all six video analyzer `.cpp` files, per this project's established convention) rather than at a single `emit_*` function, because only removing the stream's `Scope` entirely empties `groups.video` — the actual blind spot named in the gap. Gating a single `emit_*` (as the plan's own example phrasing suggested) would only drop one of the nine checks; the OLD test's per-check `CHECK` assertions already catch that case regardless of the predicate, so it would not have demonstrated the specific defect this task closes.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Task 2/3's own `<verify>` regex does not match this ctest version's summary line**
- **Found during:** Task 2 and Task 3 verification
- **Issue:** Both tasks' `<verify>` scripts pipe `ctest --output-on-failure` through `grep -qE '[0-9]+ tests passed, 0 tests failed'`. This project's ctest prints `100% tests passed, 0 tests failed out of N` — the `%` character between the digits and the word "tests" means the regex (which expects a digit directly followed by a space) never matches, even on a fully green run. The underlying test outcome is genuinely 100% passing (confirmed by direct inspection of the ctest summary line and by the `N tests failed` count being 0); only the grep pattern in the plan's own verify script is broken against this ctest version's exact wording.
- **Fix:** No source or test change — ran the exact verify commands as written and confirmed they fail solely on the grep pattern, then verified the underlying pass/fail state directly from the ctest summary text (`100% tests passed, 0 tests failed out of 7` for Task 2; `... out of 2` for Task 3). Did not modify the `<verify>` scripts themselves (they live in PLAN.md, out of this task's file scope) or any source file to route around this.
- **Files modified:** None
- **Verification:** Direct `ctest --output-on-failure` output inspection, both post-Task-2 and post-Task-3 (see Mutation Proofs above and Task Commits section)
- **Commit:** N/A (no code change; documented here for the record per Rule 3's own "auto-fix blocking issues" resolution requirement)

---

**Total deviations:** 1 auto-fixed (1 blocking — pre-existing verify-script/ctest-version mismatch, worked around by direct verification rather than a code or plan-text change)
**Impact on plan:** No impact on shipped correctness. All three tasks' acceptance criteria and the plan-level `<verification>` block's substantive intent (build clean, targeted ctest suites pass, full suite has no regression, goldens untouched) are satisfied and directly confirmed.

## Issues Encountered

None beyond the grep-pattern deviation documented above.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- `docs/checks/video.hdr.coherence.md` exists and contains the corrected Accept wording (`FOUND`)
- `src/core/registry.h` exists, no longer contains the false claim, names `list-checks` (`FOUND`)
- `tests/integration/test_list_checks.cpp` exists, contains the new `semantic=` pin TEST_CASE (`FOUND`)
- `tests/integration/test_video_inspect_section.cpp` exists, `has_video_stream` removed, `kNoVideoStreamFixtures` present (`FOUND`)
- Commit `17d3405` found in `git log --oneline --all`
- Commit `00ad358` found in `git log --oneline --all`
- Commit `e9e7b92` found in `git log --oneline --all`
- `git diff -- tests/golden/CORPUS_DIGEST.txt` empty, `git diff -- tests/golden/list_checks_effective.txt` empty
- Full suite: `100% tests passed, 0 tests failed out of 771` (6 pre-existing environment-conditional skips, unrelated to this plan)

## Next Phase Readiness

- 04-VERIFICATION.md's sixth gap (WR-02, the registry.h comment, and the inspect-section self-reference) is closed with mutation-proof evidence for every changed/added assertion, per Human Decision 3.
- Remaining open items from 04-VERIFICATION.md not in this plan's scope: SC4's HDR first-frame extraction (deferred to Phase 7 per user decision), WR-01's `resolve_sar` non-positive-denominator guard, and the CORPUS_DIGEST.txt CI-mismatch gap — none of those are touched here; this plan closes only the WR-02/registry.h/inspect-test-predicate trio Human Decision 3 assigned to it.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*
