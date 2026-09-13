---
phase: 04-video-analysis
plan: 13
subsystem: infra
tags: [ci, corpus-digest, provenance, bash, lint]

requires:
  - phase: 04-video-analysis (04-01 through 04-12)
    provides: The video.* analyzer suite and its fixture corpus, whose pre-existing CORPUS_DIGEST.txt lines this plan restores
provides:
  - "tests/golden/CORPUS_DIGEST.txt with main's (8caf1f1) 80 pre-existing designated-leg fixture hashes restored verbatim"
  - "tests/golden/CORPUS_DIGEST_PROVISIONAL.txt naming the 57 Phase-4-added, workstation-derived fixture hashes as provisional"
  - "scripts/lint_corpus_digest_provenance.sh, an executable no-rewrite guard wired into CI's lint job"
affects: ["04-20 (designated-leg digest transcription)", "any future plan that touches tests/fixtures/ or tests/golden/CORPUS_DIGEST.txt"]

actuals:
  tokens: 9500
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Digest-restoration via a name-keyed awk pass over two listings (main's historical blob vs the current file), never a hand-edited diff"
    - "Provenance ledger as names-only (no hashes) to avoid a second competing source of truth"
    - "Four-clause ordered lint (shape, self-consistency, ledger validity, no-rewrite guard) with named diagnostics and first-failure exit, matching the sibling scripts/assert_corpus_digest.sh / scripts/check_corpus.sh convention"

key-files:
  created:
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - scripts/lint_corpus_digest_provenance.sh
  modified:
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/README.md
    - .github/workflows/ci.yml

key-decisions:
  - "Restored exactly the 75 fixture lines that actually differed between main (8caf1f1) and HEAD, using an awk name-to-hash map keyed on field 2, preserving LC_ALL=C order — verified 5 of main's 80 lines already agreed with HEAD"
  - "Premise re-asserted before writing: scripts/ffmpeg_pin.json byte-identical since 8caf1f1, and none of the 61 added $OUT_DIR/ recipe-token lines in scripts/gen_corpus.sh's diff overlaps main's 80 fixture names (zero removed $OUT_DIR/ lines) — the restore was safe to perform"
  - "The mutation self-test (Task 3) required keeping CORPUS_DIGEST_SUMMARY= self-consistent after flipping one hex character, otherwise clause 2 (summary mismatch) fires before clause 4 (the no-rewrite guard) ever runs — the evidence needed is specifically clause 4 naming the offending line"
  - "Clause 4's shallow-clone SKIP branch exits non-zero (not just a stderr warning) — an unexamined guard reporting overall success would be exactly the 'guard that silently stops gating' this project treats as P0"
  - "Lint job's checkout gets fetch-depth: 0 (previously the actions/checkout default of fetch-depth: 1), for a different reason than the build job's own fetch-depth: 0 (vcpkg's historical tree object) — this job needs commit 8caf1f1 reachable for the no-rewrite guard"

requirements-completed: [BUILD-08]

coverage:
  - id: D1
    description: "Every CORPUS_DIGEST.txt line present at main (8caf1f1) is restored verbatim; the file stays 138 lines with no re-sort; CORPUS_DIGEST_SUMMARY= is re-derived and internally self-consistent"
    requirement: BUILD-08
    verification:
      - kind: other
        ref: "manual shell verification: comm -23 of main's 80 listing lines against the restored file's listing lines (0 lines) and CORPUS_DIGEST_SUMMARY= self-consistency check (both from 04-13-PLAN.md Task 1's <verify> block)"
        status: pass
    human_judgment: false
  - id: D2
    description: "57 provisional fixture names committed in tests/golden/CORPUS_DIGEST_PROVISIONAL.txt, explaining what provisional means"
    requirement: BUILD-08
    verification:
      - kind: other
        ref: "bash scripts/lint_corpus_digest_provenance.sh clause 3 (ledger sorted/unique/every-entry-real)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Rewriting a pre-existing CORPUS_DIGEST.txt line fails an executable lint that CI runs, demonstrated by a reverted single-hex-character mutation naming the offending fixture"
    requirement: BUILD-08
    verification:
      - kind: other
        ref: "manual mutation test: flipped one hex char of .topo_chapters.ffmeta's committed hash (keeping the summary self-consistent), ran scripts/lint_corpus_digest_provenance.sh, observed exit 1 with clause 4 naming the exact line, reverted via git checkout --, re-ran and observed exit 0"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-09-13
status: complete
---

# Phase 04 Plan 13: Corpus digest provenance restoration and no-rewrite lint Summary

**Restored main's 80 designated-CI-leg fixture hashes into `tests/golden/CORPUS_DIGEST.txt` verbatim, named Phase 4's 57 genuinely-new hashes as provisional in a committed ledger, and wired an executable no-rewrite guard into CI so this regression class fails a lint instead of a code review.**

## Performance

- **Duration:** ~35 min
- **Tasks:** 3
- **Files modified:** 4 (1 restored, 2 created, 1 modified beyond the digest itself: README.md + ci.yml)

## Accomplishments

- `tests/golden/CORPUS_DIGEST.txt`'s 80 pre-existing (main/`8caf1f1`) fixture hash lines are byte-identical to main again — 75 lines actually changed (5 already agreed), file stays 138 lines, `LC_ALL=C` order preserved, `CORPUS_DIGEST_SUMMARY=` re-derived and internally self-consistent.
- Premise verified before writing: `scripts/ffmpeg_pin.json` is byte-identical between `8caf1f1` and HEAD, and none of `scripts/gen_corpus.sh`'s 61 added `$OUT_DIR/` recipe-token lines (all additions, zero removals) overlaps any of main's 80 pre-existing fixture names — the restore did not paper over a genuinely changed recipe.
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` committed: 57 sorted, unique fixture names (the exact set Phase 4 added, confirmed via `comm -13` against main's name list) with a header explaining what "provisional" means and that 04-20 replaces them.
- `scripts/lint_corpus_digest_provenance.sh` committed and wired into `.github/workflows/ci.yml`'s lint job: four ordered clauses (summary-line shape, self-consistency, ledger validity, the no-rewrite guard against `8caf1f1`), each with a named diagnostic and first-failure exit. An unreachable historical object (shallow clone) exits non-zero rather than silently reporting the guard as passed.
- Lint job's checkout gained `fetch-depth: 0` so `8caf1f1` is reachable in CI — previously the job used `actions/checkout@v4`'s shallow default, which would have forced clause 4 into its SKIP-and-fail branch on every real CI run.
- Proved the guard is load-bearing: flipped one hex character of a pre-existing fixture's hash (`.topo_chapters.ffmeta`), kept `CORPUS_DIGEST_SUMMARY=` self-consistent so clauses 1–3 stayed green, ran the lint, observed exit 1 with clause 4 naming the mutated line verbatim, reverted with `git checkout --`, and confirmed exit 0 again.
- `bash scripts/assert_corpus_digest.sh` now fails on this workstation (exit 1, 135 of 137 non-excluded lines differ) — this is the intended, correct post-restore state and was not resolved, per the task's own prohibition.
- Full suite run once: `ctest --preset x64-linux --output-on-failure` → 758/758 passed, 6 skipped by design — unaffected by this plan, as expected (no ctest test reads `CORPUS_DIGEST.txt`).

## Task Commits

Each task was committed atomically:

1. **Task 1: Restore main's 80 designated-leg hash lines and re-derive the summary** - `aa87940` (fix)
2. **Task 2: A committed provisional ledger and an executable no-rewrite lint** - `a01e268` (feat)
3. **Task 3: Wire the provenance lint into CI and prove it can fail** - `775ef5a` (ci)

_Task 1 is `type="tracer"`; auto mode was active (`workflow.auto_advance: true`), so its `<verify>` was re-run before expansion (both automated checks passed: `RESTORE_OK`, `SUMMARY_SELF_CONSISTENT`) and expansion proceeded without a checkpoint._

## Files Created/Modified

- `tests/golden/CORPUS_DIGEST.txt` - 75 pre-existing fixture hash lines restored to main's (`8caf1f1`) values; summary re-derived
- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` - new: 57 sorted, unique provisional fixture names with provenance header
- `scripts/lint_corpus_digest_provenance.sh` - new: four-clause no-rewrite/self-consistency/ledger-validity lint
- `tests/golden/README.md` - new subsection stating the three-rule policy (pre-existing lines never rewritten locally, new lines provisional, local `assert_corpus_digest.sh` pass is not designated-leg evidence)
- `.github/workflows/ci.yml` - lint job gained `fetch-depth: 0` on its checkout and a new step invoking the provenance lint

## Decisions Made

- Restore performed via an awk name-to-hash map (bash-3.2-safe, no associative arrays), keyed on field 2 of main's 80-line listing, walking the current 137-line listing and substituting main's hash wherever the name matches — preserves order and touches nothing else.
- Kept the mutation self-test's `CORPUS_DIGEST_SUMMARY=` self-consistent during the Task 3 proof so the no-rewrite guard (clause 4) specifically fired, rather than clause 2's unrelated self-consistency check masking it.
- Clause 4's shallow-clone skip path exits non-zero rather than only printing a warning — an unexamined guard must not let the overall lint report success, matching the project's "gate that stops gating is P0" convention (`scripts/assert_corpus_digest.sh`'s own self-test discipline).
- `fetch-depth: 0` added to the lint job's checkout for a reason distinct from (but analogous to) the build job's own pre-existing `fetch-depth: 0` (vcpkg's historical tree object vs. this lint's need for `8caf1f1`).

## Deviations from Plan

None — plan executed exactly as written. All `must_haves.truths`, `prohibitions`, and per-task `acceptance_criteria` were verified directly (see Coverage above and the automated `<verify>` transcripts captured during execution).

## Issues Encountered

- The plan's Task 1 verify command 1 (`comm -23 ... | tee /dev/stderr | wc -l | grep -qx 0`) printed harmless `comm: file 1/2 is not in sorted order` warnings when run without `LC_ALL=C` exported for the `comm` invocation itself (only the inner `sort` calls had it). Exporting `LC_ALL=C` for the whole command (as every sibling script in this repo does via `export LC_ALL=C` at its own top) eliminated the warning; the underlying comparison was already correct (0 differing lines) either way. Not a plan defect — the verify command works as specified once run in the same locale discipline the repo's own scripts use.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- This was gap-closure plan 1 of 6 (04-13 through 04-21... actually 04-13 through 04-18 per the plan set) restoring the corpus digest to a CI-survivable state. The remaining gap-closure plans (SAR guard, interlace evidence, vacuous yuvj test, review-finding fixes, VIDEO-09/PROBE-03 scope amendments) proceed next.
- `bash scripts/assert_corpus_digest.sh` failing locally is expected and must stay that way until 04-20 transcribes the designated leg's real hashes for the 57 provisional lines — no future plan in this gap-closure sequence should "fix" that local failure directly.
- The no-rewrite lint is now a permanent CI gate; any future plan that touches `tests/golden/CORPUS_DIGEST.txt` will be caught immediately if it rewrites a pre-existing line instead of only adding new ones.

## Self-Check: PASSED

- `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` exists on disk (created, verified via `[ -f ]`).
- `scripts/lint_corpus_digest_provenance.sh` exists on disk, is executable, and exits 0 against the current repository state (re-verified).
- `git log --oneline --all | grep -E "aa87940|a01e268|775ef5a"` returns all three commit hashes.
- Working tree is clean for every file this plan touched (`git status --short` shows no pending changes to `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`, `scripts/lint_corpus_digest_provenance.sh`, `tests/golden/README.md`, or `.github/workflows/ci.yml`).

---
*Phase: 04-video-analysis*
*Completed: 2026-09-13*
