---
phase: quick
plan: 01
subsystem: testing
tags: [ci, bash, ffmpeg, opus, golden-tests, ci.yml]

requires: []
provides:
  - scripts/assert_corpus_digest.sh -- self-testing, bash-3.2-compatible gate narrowing D-GAP-01's byte-exact comparison
  - WINDOWS.md #22 waived with an honest reason; #24 records the residual byte-drift gap
affects: [ci-corpus-digest, windows-ledger]

actuals:
  tokens: 8323
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Self-testing bash gate: known-good, non-vacuity, and count-guard controls run unconditionally before the real comparison, mirroring scripts/check_corpus.sh and scripts/lint_bash4_builtins.sh's established shape."

key-files:
  created:
    - scripts/assert_corpus_digest.sh
  modified:
    - .github/workflows/ci.yml
    - tests/golden/README.md
    - .planning/WINDOWS.md

key-decisions:
  - "Excluded exactly 3 lines (not 2): mkv_opus_a.webm, mkv_opus_b.webm, and CORPUS_DIGEST_SUMMARY= (a hash OF those two lines, so leaving it in would re-introduce the jitter through the back door)."
  - "New script (scripts/assert_corpus_digest.sh) invoked by the existing ci.yml step, rather than filtering inside scripts/corpus_digest.sh or inlining the exclusion in ci.yml -- keeps corpus_digest.sh's cross-leg evidence output byte-unmodified and makes the gate locally runnable and self-testing."
  - "WINDOWS.md #22 resolved via `windows waive`, not `windows fixed` -- the underlying libopus cross-host nondeterminism is genuinely not fixed. A new open entry (#24) records the resulting coverage gap so it isn't hidden inside #22's closure."

patterns-established:
  - "A gate script that could accidentally become vacuous (an over-broad exclusion) must ship with an unconditional self-test proving a perturbation OUTSIDE the exclusion still fails, run before every real comparison."

requirements-completed: [BUILD-08, TRUST-06]

coverage:
  - id: D1
    description: "scripts/assert_corpus_digest.sh excludes only the two libopus fixture lines plus the derived summary line from byte-exact comparison, with a self-test proving the gate is not vacuous and an exact excluded-count guard."
    requirement: "TRUST-06"
    verification:
      - kind: other
        ref: "bash scripts/assert_corpus_digest.sh <fresh> <fresh> (V1) -- exits 0, reports 78 compared lines"
        status: pass
      - kind: other
        ref: "non-vacuity control: perturb mkv_noopus.mkv digest, assert failure (V2)"
        status: pass
      - kind: other
        ref: "Opus-only perturbation: assert pass (V3)"
        status: pass
      - kind: other
        ref: "count-guard control: drop one Opus line, assert failure with 'quietly stops gating' message (V4)"
        status: pass
    human_judgment: false
  - id: D2
    description: "ci.yml's designated-leg D-GAP-01 step invokes scripts/assert_corpus_digest.sh; EXCLUDED_TEST_REGEX / EXPECTED_EXCLUDED_COUNT=5 (WINDOWS.md #17's unrelated test exclusion) left byte-unchanged; scripts/lint_bash4_builtins.sh and YAML parsing both still pass (V5)."
    requirement: "BUILD-08"
    verification:
      - kind: other
        ref: "scripts/lint_bash4_builtins.sh exits 0; python3 yaml.safe_load(ci.yml) succeeds; EXPECTED_EXCLUDED_COUNT=5 occurs exactly once; assert_corpus_digest.sh referenced (V5)"
        status: pass
    human_judgment: false
  - id: D3
    description: "tests/golden/README.md documents the three skipped lines, the reason, the lost byte-level coverage, and the tests that still cover structure/findings."
    verification:
      - kind: other
        ref: "grep checks for mkv_opus_a.webm, mkv_opus_b.webm, 'no byte-level drift detection', assert_corpus_digest.sh, CORPUS_DIGEST_SUMMARY, UPDATE_GOLDENS (PASS_DOC)"
        status: pass
    human_judgment: false
  - id: D4
    description: "WINDOWS.md #22 waived (not fixed) with a reason naming assert_corpus_digest.sh; new open entry #24 records the residual gap; ledger parses cleanly with open_count=11, waived_count=1."
    requirement: "TRUST-06"
    verification:
      - kind: other
        ref: "gsd-tools windows status --raw asserted via python3: open_count==11, waived_count==1, entry 22 waived with reason naming assert_corpus_digest.sh, new open entry naming mkv_opus_a.webm (PASS_LEDGER, PASS_LEDGER_PARSE)"
        status: pass
    human_judgment: false

duration: 20min
completed: 2026-09-08
status: complete
---

# Quick Task 260908-oax: Narrow Corpus Digest Assertion Summary

**Self-testing `scripts/assert_corpus_digest.sh` excludes exactly the two libopus fixture lines and their derived summary line from D-GAP-01's byte-exact comparison, closing WINDOWS.md #22 as waived (not fixed) with the residual gap tracked openly at #24.**

## Performance

- **Duration:** ~20 min
- **Tasks:** 3
- **Files modified:** 4 (1 created: `scripts/assert_corpus_digest.sh`; 3 modified: `.github/workflows/ci.yml`, `tests/golden/README.md`, `.planning/WINDOWS.md`)

## Accomplishments

- Created `scripts/assert_corpus_digest.sh`: a self-testing, bash-3.2-compatible gate that compares 78 of 81 corpus-digest lines, excluding `mkv_opus_a.webm`, `mkv_opus_b.webm`, and the derived `CORPUS_DIGEST_SUMMARY=` line, guarded by an exact excluded-line-count assertion (3 on both sides) so the exclusion cannot silently widen.
- Wired ci.yml's designated-leg (`x64-linux`) `Assert the corpus digest matches the committed pin (D-GAP-01)` step to the new script, replacing the prior inline `diff` that asserted byte-exactness the two Opus fixtures do not have. `EXCLUDED_TEST_REGEX`/`EXPECTED_EXCLUDED_COUNT=5` (WINDOWS.md #17's unrelated test-exclusion gate) left untouched.
- Documented the lost byte-level coverage in `tests/golden/README.md`: names the three skipped lines, the libopus cross-host CPU-feature-dispatch cause, the cost (no byte-level drift detection for the two Opus fixtures on any leg), what still covers them (structure/findings tests), and the expected refresh churn.
- Resolved WINDOWS.md #22 via `windows waive` (not `windows fixed`) with a reason naming `assert_corpus_digest.sh` as the enforcing artifact, and appended a new open entry (#24) recording the residual byte-drift gap with the EBML-structure-hash alternative named as the un-taken fix.

## Task Commits

1. **Task 1: Self-testing assert_corpus_digest.sh, wired into the CI step end-to-end** - `5b458ed` (feat)
2. **Task 2: Document the byte-level coverage this change gives up** - `a9c229b` (docs)
3. **Task 3: Resolve WINDOWS.md #22 honestly and record the residual gap** - `ae0efaf` (docs)

_No plan-metadata commit issued separately — this summary/state-update commit follows below._

## Files Created/Modified

- `scripts/assert_corpus_digest.sh` - new self-testing gate script; `assert_digest_pair` function plus an unconditional 3-control self-test (known-good, non-vacuity, count-guard) run before every real comparison
- `.github/workflows/ci.yml` - designated-leg branch of the D-GAP-01 step now calls the new script; comment block extended to record WINDOWS.md #22's resolution
- `tests/golden/README.md` - new `CORPUS_DIGEST.txt (D-GAP-01, WINDOWS.md #22)` section
- `.planning/WINDOWS.md` - #22 waived; #24 appended (open)

## Decisions Made

- Excluded exactly 3 lines, not 2: `CORPUS_DIGEST_SUMMARY=` is a SHA-256 of the full 80-line listing including both Opus lines, so it cannot be byte-stable while they are not — leaving it in the comparison would have made the whole change inert.
- Chose a new invoked script over filtering inside `scripts/corpus_digest.sh` (would delete cross-leg evidence on 5 legs to fix an assertion that runs on 1) or inlining the exclusion in `ci.yml` (not locally runnable or self-testable, and this change's core risk is a filter that silently disables the whole comparison).
- `windows waive 22` (not `fixed`) because the defect as recorded — the designated leg's fixture generation is not run-to-run reproducible — is still true; only the assertion's scope changed, not the underlying nondeterminism.

## Deviations from Plan

None - plan executed exactly as written. One minor cosmetic note: the WINDOWS.md #24 description contains a small redundant phrase ("assert structure and findings rather than bytes, not bytes") introduced when composing the `--description` argument for `gsd-tools windows append`; the ledger has no `edit` subcommand and hand-editing is explicitly prohibited by the plan, so the wording was left as recorded rather than risk a ledger desync. It does not affect any acceptance criterion (`PASS_LEDGER`/`PASS_LEDGER_PARSE` both pass) and the meaning is unambiguous.

## Issues Encountered

- The README's "no byte-level drift detection" phrase was initially split across two lines with markdown bold markers (`**no\nbyte-level drift detection**`), which the `grep -q` verification command (line-based) could not match. Fixed by rewrapping so the full phrase sits on one line before the phrase was moved inside the bold span; verified with `grep -q 'no byte-level drift detection'` afterward.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The designated-leg D-GAP-01 gate now asserts a property the fixtures actually have (78 byte-exact lines), closing the flapping-gate risk that prompted this task (real CI runs 34021508083 vs 34022461121).
- Residual gap tracked openly at WINDOWS.md #24: `mkv_opus_a.webm`/`mkv_opus_b.webm` have no byte-level drift detection on any leg. A future phase could close this by hashing parsed EBML structure (element IDs, sizes, ordering, `CodecDelay`/`SeekPreRoll`) instead of file bytes — not undertaken here, named as the deferred alternative.
- No blockers for subsequent CI runs; the next real push to the designated leg is the first live proof this gate no longer flaps on docs-only commits.

---
*Phase: quick*
*Completed: 2026-09-08*

## Self-Check: PASSED

All claimed files found on disk (`scripts/assert_corpus_digest.sh`, `.github/workflows/ci.yml`, `tests/golden/README.md`, `.planning/WINDOWS.md`, this SUMMARY.md) and all claimed commit hashes (`5b458ed`, `a9c229b`, `ae0efaf`) found in `git log --oneline --all`.
