---
phase: quick-260914-ryu
plan: 01
subsystem: infra
tags: [ffmpeg, lgpl, gen_corpus, ci, video-fixtures]

# Dependency graph
requires:
  - phase: quick-260910-vvp
    provides: pinned-first ffmpeg resolution with release-identity gate in gen_corpus.sh
provides:
  - LGPL-safe interlace fixture recipes that no longer depend on the GPL-only temporal-interlacing filter
affects: [phase-04-ci-green, x64-windows-static-md]

actuals:
  tokens: 936
  tasks: 2
  commits: 1

tech-stack:
  added: []
  patterns: []

key-files:
  created: []
  modified: [scripts/gen_corpus.sh]

key-decisions:
  - "Switched all three interlace filterchains (video_ilace_tff.mp4, video_ilace_bff.mp4, the .video_ilace_seg_a.m2v segment feeding video_ilace_mixed.mp4) from the GPL-only t-prefixed temporal-interlacing filter to its LGPL twin interlace, with lowpass=off mandatory to preserve byte identity"
  - "scripts/gen_corpus.ps1 left untouched — confirmed empirically (grep for interlace/ilace across all 95 lines returns nothing); the PowerShell generator never grew the Phase-4 video recipes, and its parity work was already deferred for separate user approval in quick task 260910-vvp"
  - "claude_docs/03-video-analysis.md left untouched — it is a frozen design-input record (git log shows exactly one commit, 67e8d90, no edits since); the same sentence already diverges from the shipped implementation in two other unamended ways (x264/libx265 vs the corpus's actual GPL-free codec choices per D-04/D-09), and this project's established convention is to record such deviations in gen_corpus.sh's own recipe comments, never by rewriting the frozen doc"

requirements-completed: [BUILD-08]

coverage:
  - id: D1
    description: "The three interlace fixture recipes in scripts/gen_corpus.sh invoke only the LGPL interlace filter (never the GPL-only tinterlace filter), with byte-identical output to the previous recipe proven via three OLD-vs-NEW cmp comparisons on the pinned Linux (GPL) generator before the edit landed"
    requirement: "BUILD-08"
    verification:
      - kind: other
        ref: "scripts/gen_corpus.sh Task 1 <verify> automated blocks — byte-identity cmp (3 pairs), filterchain grep gate (0 tinterlace hits, 3 interlace=scan=...:lowpass=off hits, 0 lowpass=linear hits), comment + scope gate"
        status: pass
    human_judgment: false
  - id: D2
    description: "Repo-wide regression sweep confirms no other file was touched and the existing corpus/digest/test infrastructure stays green after the filterchain swap"
    requirement: "BUILD-08"
    verification:
      - kind: other
        ref: "bash -n scripts/gen_corpus.sh; bash scripts/lint_bash4_builtins.sh; bash scripts/check_corpus.sh; bash scripts/lint_corpus_digest_provenance.sh; git diff --stat -- tests/golden/ (empty)"
        status: pass
      - kind: integration
        ref: "ctest --preset x64-linux --output-on-failure"
        status: pass
    human_judgment: false

duration: 20min
completed: 2026-09-14
status: complete
---

# Quick Task 260914-ryu Summary

**Replaced the GPL-only `tinterlace` filter with its LGPL twin `interlace` (with `lowpass=off`) in the three `gen_corpus.sh` interlace fixture recipes, proving byte-for-byte identity with the previous output before committing.**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-09-14T18:09:29Z (approx, first tool call)
- **Completed:** 2026-09-14T18:16:36Z (commit timestamp)
- **Tasks:** 2
- **Files modified:** 1 (`scripts/gen_corpus.sh`)

## Accomplishments
- Proved byte-identity of the OLD (`tinterlace`) vs NEW (`interlace=scan=...:lowpass=off`) filterchains for all three affected recipes, using the pinned Linux (GPL) ffmpeg generator that can run both filters, before touching the file
- Switched the three `-vf` filterchains in `scripts/gen_corpus.sh` (the `video_ilace_tff.mp4`/`video_ilace_tff_copy.mp4` recipe, the `video_ilace_bff.mp4` recipe, and the `.video_ilace_seg_a.m2v` segment feeding `video_ilace_mixed.mp4`) from the GPL-only temporal-interlacing filter to the LGPL `interlace` filter
- Extended the existing "Interlace (VIDEO-06)" comment block explaining why the switch was necessary (the x64-windows-static-md corpus step's `--enable-gpl`-free pinned build cannot see the old filter at all), why `interlace` is a safe substitute (shares `libavfilter/vf_tinterlace.c`), and why `lowpass=off` is mandatory (default `lowpass=linear` would move all four fixtures' committed digest lines)
- Ran the full repo-wide regression sweep (bash -n, bash-3.2 lint, check_corpus, digest-provenance lint, 771-test ctest suite) — all green, no fixture/golden bytes moved
- Confirmed and documented the two deliberate no-op decisions: `scripts/gen_corpus.ps1` has no interlace recipe to fix, and `claude_docs/03-video-analysis.md` is a frozen design-input record left untouched by project convention

## Task Commits

1. **Task 1: Prove byte identity, then switch the three interlace filterchains** - `bf9a42f` (fix)
2. **Task 2: Repo-wide consistency sweep and regression gates** - no code commit (verification-only task; no files changed beyond Task 1's)

**Plan metadata:** committed separately as `a72be80` (plan authoring, prior to this execution)

## Files Created/Modified
- `scripts/gen_corpus.sh` - Three `-vf` filterchains switched from `tinterlace=interleave_top/bottom,setparams=...` to `interlace=scan=tff/bff:lowpass=off,setparams=...`; comment block extended with the GPL/LGPL rationale and the `lowpass=off` byte-identity explanation

## Decisions Made
See `key-decisions` in frontmatter above (filterchain switch, and the two confirmed no-op files).

## Deviations from Plan

None - plan executed exactly as written. The precondition (pinned Linux GPL ffmpeg present, reporting both `interlace` and `tinterlace` filters) was met; the byte-identity proof passed on the first attempt for all three recipe pairs.

## Issues Encountered

None.

## Verification Evidence

**Byte-identity proof (three `cmp` pairs, pinned Linux GPL generator, scratch dir only):**

| Pair | OLD sha256 | NEW sha256 | Result |
|------|-----------|-----------|--------|
| `video_ilace_tff.mp4` recipe (2s, tff) | `bc3740d9b7911dad97e1744d707ac4d41476a3589f4806762ee6a1d1283a898e` | `bc3740d9b7911dad97e1744d707ac4d41476a3589f4806762ee6a1d1283a898e` | identical |
| `video_ilace_bff.mp4` recipe (2s, bff) | `e538b59b18209e21dcc3f9f473a87a461e23b9dc7045c2744bc088babcac4d35` | `e538b59b18209e21dcc3f9f473a87a461e23b9dc7045c2744bc088babcac4d35` | identical |
| `.video_ilace_seg_a.m2v` (1s, tff, m2v) | `23d782f1a6e30b034e11c99a71d462760e6335f51ca8bb43e714460e3343052f` | `23d782f1a6e30b034e11c99a71d462760e6335f51ca8bb43e714460e3343052f` | identical |

All three `cmp` invocations reported no bytes differ. This was run BEFORE the edit landed, blocking the edit on success.

**Filterchain gate (post-edit):**
- Comment-stripped `tinterlace` count on executed lines: `0`
- `interlace=scan=(tff|bff):lowpass=off,setparams=field_mode=(tff|bff)` line count: `3`
- `lowpass=linear` on executed lines: `0`

**Task 2 regression sweep:**
- `bash -n scripts/gen_corpus.sh`: OK
- `bash scripts/lint_bash4_builtins.sh`: clean (18 files scanned, self-test OK)
- `bash scripts/check_corpus.sh`: clean (138 fixtures present and non-empty, self-test OK)
- `bash scripts/lint_corpus_digest_provenance.sh`: all 4 clauses passed (80 pre-existing `CORPUS_DIGEST.txt` lines verbatim, `CORPUS_DIGEST_PROVISIONAL.txt` correctly empty with its transcription marker)
- `git diff --stat -- tests/golden/`: empty (no golden touched)
- `ctest --preset x64-linux --output-on-failure`: **100% tests passed, 0 tests failed out of 771** (6 pre-existing designated-leg-only skips, unrelated to this change)

## No-Op Decisions (documented per plan requirement)

1. **`scripts/gen_corpus.ps1`** — inspected and deliberately left unchanged. `grep -in 'interlace\|ilace'` across all 95 lines returns zero matches; the PowerShell generator never grew the Phase-4 video recipes at all, and its parity work with `gen_corpus.sh` was already deferred for separate user approval in quick task 260910-vvp. There is nothing in this file for the GPL-only-filter fix to touch.
2. **`claude_docs/03-video-analysis.md`** (line 70) — inspected and deliberately left unchanged. `git log -- claude_docs/03-video-analysis.md` shows exactly one commit (`67e8d90`, "docs: add design document set") with no edits since — it is a frozen design-input record. The same sentence already diverges from the shipped implementation in two other unamended ways (it names x264 for the GOP pair and libx265 for HDR10, while the corpus deliberately uses neither, per decisions D-04/D-09). This project's established convention (visible in `gen_corpus.sh`'s own "Deviation from the plan's literal recipe text" comments) is to record implementation deviations from `claude_docs/` inside `gen_corpus.sh`'s recipe comments, never by rewriting the frozen design doc. The untracked scratch-worktree copy at `.claude/worktrees/stoic-shaw-7ec357/claude_docs/03-video-analysis.md` was out of scope entirely and not touched (`git ls-files .claude/worktrees` returns empty).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `scripts/gen_corpus.sh`'s interlace recipes now use only LGPL-available filters, so the x64-windows-static-md CI leg's corpus generation step should get past `video_ilace_tff.mp4` without any golden needing to move — this can only be finally confirmed by a real CI run on that leg (out of scope for this local-only quick task).
- No blockers for Phase 4 continuation; `ctest --preset x64-linux` remains fully green (771/771).

---
*Phase: quick-260914-ryu*
*Completed: 2026-09-14*

## Self-Check: PASSED
- FOUND: scripts/gen_corpus.sh
- FOUND: bf9a42f (commit exists in git log)
- FOUND: .planning/quick/260914-ryu-replace-gpl-only-tinterlace-with-the-lgp/260914-ryu-SUMMARY.md
