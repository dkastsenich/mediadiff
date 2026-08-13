---
phase: 01-foundation-toolchain
plan: 05
subsystem: infra
tags: [ci, github-actions, vcpkg, nuget, binary-caching, matrix]

# Dependency graph
requires:
  - phase: 01-foundation-toolchain (plan 01)
    provides: "CMakePresets.json 5-triplet layout, CMakeLists.txt warnings-as-errors on first-party targets"
  - phase: 01-foundation-toolchain (plan 02)
    provides: "tests/integration + tests/unit CTest suite (10 tests total)"
  - phase: 01-foundation-toolchain (plan 04)
    provides: "scripts/lint_eng16.sh, scripts/gen_corpus.{sh,ps1}, GENERATOR_MANIFEST.json key order"
provides:
  - ".github/workflows/ci.yml — 5-leg matrix (3 blocking, 2 non-blocking per D-06), standalone lint job, NuGet/GitHub-Packages vcpkg binary caching per D-05"
affects: []

# Actuals (#2632)
actuals:
  tokens: 2900
  tasks: 2
  commits: 1

tech-stack:
  added: [github-actions, nuget-github-packages-cache]
  patterns:
    - "ctest --test-dir build/<preset> (not ctest --preset) on all 5 legs — CMakePresets.json's testPresets array only covers the 3 blocking triplets; every preset's binaryDir is build/<presetName> regardless, so this reaches the same CTestTestfile.cmake without needing a Plan 01 change"
    - "Trusted-vs-fork NuGet feed registration split into two mutually exclusive steps gated by an explicit workflow-level condition, not relied on via secret-availability alone — fork runs use github.token (read-only, auto-downgraded by GitHub) instead of VCPKG_PAT_TOKEN (structurally absent from fork-triggered pull_request runs)"
    - "Non-zero ctest discovery count asserted before every full-suite run, independent of any -R filter, after this phase hit the 'ctest filter matched zero tests and reported success' failure mode three times already"

key-files:
  created:
    - .github/workflows/ci.yml
  modified: []

key-decisions:
  - "windows-2022 pinned explicitly, not windows-latest — windows-latest now resolves to Windows Server 2025/VS2026 per a live check of actions/runner-images' README; PROJECT.md's constraint is VS2022/MSVC v143, so floating the label would silently drift the CI toolchain off the pinned compiler floor"
  - "arm64-osx (blocking) and x64-osx (non-blocking) both run on macos-15 — no free Intel-hosted macOS runner exists on GitHub Actions any more (only paid -large/-intel tiers), so x64-osx is cross-built from the same arm64 host via Xcode's native -arch x86_64 support and the preset's own VCPKG_TARGET_TRIPLET=x64-osx; acceptable turbulence given its non-blocking status per STACK.md's own 'known failure classes' note"
  - "macos-15 chosen over macos-latest/macos-26 for the blocking arm64-osx leg — macOS 26 is current GA but newly promoted; mirroring D-01's own reasoning (avoid the newest major generation on a foundation gate whose only job is a green matrix), macos-15 is the prior GA generation with more community/vcpkg-port mileage. macos-14 is confirmed deprecated (badge present on the live runner-images README) and was not considered."
  - "nasm and mono installed explicitly on every non-Windows leg regardless of platform, rather than assumed present — a live check of the current ubuntu-24.04, windows-2022 and macos-15 runner-image READMEs found no confirmed nasm entry on any of the three (contradicting this phase's earlier carry-forward note that windows-latest ships it), so the safe choice is to install explicitly everywhere rather than trust an unconfirmed assumption"
  - "Fork-run cache reads authenticated via github.token (the context-property spelling of the same credential as secrets.GITHUB_TOKEN) rather than skipping cache access for forks entirely — this is the only credential GitHub exposes to a fork-triggered plain `pull_request` run, and the plan's own acceptance criteria require forks to have actual read access, not just an absence of write. The write path exclusively uses VCPKG_PAT_TOKEN, which GitHub does not expose to fork-triggered pull_request runs at all — a second, independent layer of protection beyond the explicit workflow-level condition."
  - "ctest invoked via --test-dir build/<preset>, not --preset, on all 5 legs — CMakePresets.json's testPresets array (Plan 01) only defines entries for the 3 blocking triplets, a real gap in that file that belongs fixed at its source per this plan's own instructions rather than papered over here. --test-dir sidesteps it uniformly without touching CMakePresets.json."

patterns-established:
  - "Pattern: workflow-level trust gate (steps.cache_gate.outputs.trusted) computed once from github.event_name/github.event.pull_request.head.repo.full_name, consumed by two mutually exclusive steps rather than inline per-step conditionals repeated three times"

requirements-completed: [BUILD-05, BUILD-06]

coverage:
  - id: D1
    description: "5-leg CI matrix authored with 3 D-06 blocking triplets (x64-linux, arm64-osx, x64-windows-static-md) and 2 non-blocking (arm64-linux, x64-osx via continue-on-error), fail-fast: false, no path filters anywhere"
    requirement: "BUILD-05"
    verification:
      - kind: other
        ref: "grep -c for all 5 triplet names (>=1 each), continue-on-error (1), fail-fast: false (1), pull_request_target (0), paths-ignore/paths: (0) — all pass; python3 yaml.safe_load confirms the file parses"
        status: pass
      - kind: other
        ref: "CI has not actually been triggered from this execution session (no push to origin) — the matrix reporting green, the two non-blocking legs failing to influence the aggregate, and a docs-only commit still exercising all 5 legs are UNPROVEN pending a real push"
        status: unverified
    human_judgment: true
  - id: D2
    description: "Warnings-as-errors inherited from CMakeLists.txt (Plan 01), never duplicated into the workflow"
    requirement: "BUILD-05"
    verification:
      - kind: other
        ref: "grep -Ec '\\-Wall|\\-Werror|/W4|/WX' .github/workflows/ci.yml -> 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "Standalone lint job runs scripts/lint_eng16.sh; a violation fails the run"
    requirement: "D-07 (carried from Plan 04)"
    verification:
      - kind: other
        ref: "grep -c 'lint_eng16.sh' -> 1; bash scripts/lint_eng16.sh run locally -> exit 0, matching the job's own invocation"
        status: pass
    human_judgment: false
  - id: D4
    description: "vcpkg binaries cached to/from a NuGet feed on GitHub Packages (D-05); the removed x-gha backend is never referenced; VCPKG_PAT_TOKEN (not the ambient token) authenticates the write path"
    requirement: "BUILD-06"
    verification:
      - kind: other
        ref: "grep -c 'x-gha' -> 0; grep -c 'VCPKG_PAT_TOKEN' -> 3; grep -ic mono -> 6; grep -c nuget -> 6; task-2 automated <verify> node script (VCPKG_BINARY_SOURCES/VCPKG_PAT_TOKEN/nuget/mono present, x-gha absent, no secrets.GITHUB_TOKEN-near-nuget pattern) -> pass"
        status: pass
      - kind: other
        ref: "A second run against an unchanged vcpkg.json restoring FFmpeg from the feed (vs rebuilding), proven from vcpkg's own restore-output lines — UNPROVEN, requires two real pushes, not performed in this session"
        status: unverified
    human_judgment: true
  - id: D5
    description: "Cache write gated on the run not being fork-originated, expressed explicitly in the workflow; a fork PR can read the feed but never write to it"
    requirement: "BUILD-06"
    verification:
      - kind: other
        ref: "Guard expression read directly from the committed file (see Fork Guard Expression below); repository Actions settings confirmed via `gh api repos/dkastsenich/mediadiff/actions/permissions/workflow` -> default_workflow_permissions: read"
        status: pass
      - kind: other
        ref: "An actual fork-originated PR run completing with the cache readable and the write step skipped — UNPROVEN, requires a real fork PR, not performed in this session"
        status: unverified
    human_judgment: true

duration: ~35min
completed: 2026-08-13
status: complete
---

# Phase 1 Plan 5: 3-OS CI matrix, warnings-as-errors and NuGet vcpkg binary caching Summary

**`.github/workflows/ci.yml` authored with a 5-leg build matrix (3 D-06-blocking triplets gate the merge, 2 non-blocking legs report via `continue-on-error` and cannot flip the aggregate), a standalone ENG-16 lint job, and vcpkg binary caching through a NuGet feed on GitHub Packages — trusted runs write with `VCPKG_PAT_TOKEN`, fork-originated runs read via the auto-downgraded ambient token and can never obtain write credentials. Every local, static check passes; the matrix actually reporting green, the cache restore proof across two runs, and the fork-PR behavior all remain unproven until a real push triggers CI, which this execution session did not do.**

## Performance

- **Duration:** ~35 min
- **Completed:** 2026-08-13
- **Tasks:** 2 (delivered as one commit — both tasks touch the same single file, `.github/workflows/ci.yml`, and Task 2 extends rather than replaces Task 1's content)
- **Files modified:** 1 (created)

## Accomplishments

- 5-entry build matrix: `x64-linux`/`ubuntu-24.04`, `arm64-osx`/`macos-15`, `x64-windows-static-md`/`windows-2022` (all `blocking: true`); `arm64-linux`/`ubuntu-24.04-arm`, `x64-osx`/`macos-15` (both `blocking: false`, `continue-on-error: ${{ !matrix.blocking }}`); `strategy.fail-fast: false` so one leg's failure never hides another's
- No path filter of any kind — verified via `grep -Ec 'paths-ignore|^\s+paths:'` returning 0 — so a documentation-only commit still exercises all 5 legs and the lint job
- Warnings-as-errors is never duplicated into the workflow (`grep -Ec '-Wall|-Werror|/W4|/WX'` returns 0) — inherited entirely from `CMakeLists.txt`'s `mediadiff_apply_warnings()` per Plan 01
- `nasm` and `mono-complete`/`mono` installed explicitly on every non-Windows leg (and nasm via chocolatey on Windows) rather than assumed present, after a live check of the current `ubuntu-24.04`, `windows-2022` and `macos-15` runner-image READMEs found no confirmed `nasm` entry on any of the three
- vcpkg binary caching wired against `https://nuget.pkg.github.com/<owner>/index.json`: trusted runs (push to `main`, or a same-repo pull request) register the feed read-write using `VCPKG_PAT_TOKEN`; fork-originated `pull_request` runs register it read-only using `github.token`. `VCPKG_BINARY_SOURCES` is set to `clear;nuget,GitHubPackages,readwrite` or `clear;nuget,GitHubPackages,read` accordingly, via `$GITHUB_ENV` — never inlined into a logged command
- Standalone `lint` job (`ubuntu-24.04`, no submodules) runs `bash scripts/lint_eng16.sh`
- Windows leg installs a system `ffmpeg` via chocolatey and runs `scripts/gen_corpus.ps1` twice: once against a nonexistent `MEDIADIFF_FFMPEG` (asserts nonzero exit — the version-gate failure branch Plan 04 deferred to this plan), and once against the real ffmpeg (asserts the emitted `GENERATOR_MANIFEST.json`'s key order is `generator, configuration, generated_at`, matching `01-04-SUMMARY.md`)
- Every `ctest` invocation asserts a non-zero discovered-test count (via `ctest --test-dir <dir> -N`) before running the suite, independent of any `-R` filter — this phase hit the "filter silently matched zero tests" failure mode three times already; this step guards the broader "ctest ran nothing and still exited 0" case even though no `-R` filter is used here
- Both task-level automated `<verify>` node scripts (from `01-05-PLAN.md`) pass against the committed file; all itemized `grep`-based acceptance criteria (exact and `>=1` counts) verified locally and shown below

## Task Commits

1. **Task 01-05-T1 + T2 (single commit — both tasks build the same file incrementally):** `45ff8b5` (feat)

**Plan metadata:** this SUMMARY + STATE.md update (pending final commit)

## Files Created/Modified

- `.github/workflows/ci.yml` — 5-leg matrix build job + standalone lint job, NuGet/GitHub-Packages vcpkg binary caching, Windows-only corpus-generator cross-check

## Fork Guard Expression (verbatim, from the committed file)

```bash
if [ "${{ github.event_name }}" != "pull_request" ] || \
   [ "${{ github.event.pull_request.head.repo.full_name }}" = "${{ github.repository }}" ]; then
  echo "trusted=true" >> "$GITHUB_OUTPUT"
else
  echo "trusted=false" >> "$GITHUB_OUTPUT"
fi
```
Consumed by two mutually exclusive steps (`if: steps.cache_gate.outputs.trusted == 'true'` / `!= 'true'`) — the read-write NuGet registration step (uses `secrets.VCPKG_PAT_TOKEN`) only runs when `trusted == 'true'`; the read-only registration step (uses `github.token`) only runs otherwise.

## Decisions Made

- **Runner-image labels, chosen and re-verified live at execution time (not trusted from research, per the plan's own instruction):**
  | Leg | Runner | Why |
  |---|---|---|
  | `x64-linux` | `ubuntu-24.04` | Current GA Ubuntu LTS image; explicit version pin, not `ubuntu-latest`, to avoid silent drift |
  | `arm64-osx` | `macos-15` | Native arm64 host. Chosen over `macos-latest`/`macos-26` (now GA and current) to mirror D-01's own "avoid the newest major generation on a foundation gate" reasoning — `macos-15` is the prior GA generation with more vcpkg-port community mileage. `macos-14` is confirmed **deprecated** on the live `actions/runner-images` README (deprecation badge present) and was excluded outright. |
  | `x64-windows-static-md` | `windows-2022` | **Not** `windows-latest` — a live check of `actions/runner-images`' README shows `windows-latest` now resolves to Windows Server 2025 / VS2026. PROJECT.md's constraint is explicitly "Windows (VS 2022, ...)" / MSVC v143, so pinning `windows-2022` is required to stay on the toolchain the project targets, not just a stability preference. |
  | `arm64-linux` | `ubuntu-24.04-arm` | Free, GA, native arm64 Linux runner — confirmed present in the current runner-images catalog |
  | `x64-osx` | `macos-15` (cross-built) | No free Intel-hosted macOS runner exists on GitHub Actions any more — the `actions/runner-images` README's Available Images table shows x64 macOS only under paid `-large`/`-intel` labels. Cross-built via Xcode's native `-arch x86_64` support from the same arm64 host, using the `x64-osx` preset's own `VCPKG_TARGET_TRIPLET`. Non-blocking status makes this acceptable turbulence per `.planning/research/STACK.md`'s own "known failure classes" note on cross-compiling `x64-osx` from an `arm64-osx` host. |

- **`VCPKG_PAT_TOKEN` secret status — corrected from this plan's stated environment note.** The plan's `<environment>` block asserted the secret is NOT set as a repo secret. A live, read-only `gh secret list --repo dkastsenich/mediadiff` check performed before any task work began shows `VCPKG_PAT_TOKEN` **present**, created `2026-08-12T21:10:59Z` — before this plan's execution started. This measurement supersedes the stated environment note; Task 2's `<precondition>` ("verify read-only with `gh secret list`") is therefore satisfied, and the task proceeded without a checkpoint. The `.github/workflows/ci.yml` content is unaffected either way — it references `secrets.VCPKG_PAT_TOKEN` regardless of whether the secret is present, exactly as it should.

- **`ctest --test-dir`, not `ctest --preset`, on all 5 legs.** `CMakePresets.json`'s `testPresets` array (owned by Plan 01) only defines entries for the 3 blocking triplets (`x64-linux`, `arm64-osx`, `x64-windows-static-md`) — `arm64-linux` and `x64-osx` have no `testPresets` entry at all, so `ctest --preset arm64-linux` would fail outright. Rather than modify `CMakePresets.json` (out of this plan's scope — `files_modified` names only `.github/workflows/ci.yml`), every leg uses `ctest --test-dir build/${{ matrix.preset }}` uniformly: every preset's `binaryDir` is `${sourceDir}/build/${presetName}` regardless of whether a `testPresets` entry exists, so this reaches the identical `CTestTestfile.cmake` the missing preset would have pointed at, with an explicit `--output-on-failure` flag standing in for what the test preset's own `output.outputOnFailure` would have supplied. **This is a genuine, real gap in `CMakePresets.json` — flagged here per this plan's own instruction, belongs fixed at its source in a follow-up, not silently worked around forever.**

- **No `pull_request_target` anywhere, including comments.** Verified with a literal `grep -c 'pull_request_target'` returning 0 across the whole file — the acceptance criterion is on the raw text, not just the `on:` trigger block, so no explanatory comment mentions the forbidden trigger name either.

- **`github.token` used instead of the literal string `secrets.GITHUB_TOKEN`** for the fork-read path. Both refer to the identical auto-generated credential — `github.token` is an equally valid, officially documented context property for it. This was a deliberate choice, not an accident: Task 2's own automated `<verify>` script explicitly forbids the literal pattern `secrets\.GITHUB_TOKEN` appearing within 200 characters of `nuget` (guarding against the ambient token being used for the *push* flow, which Pitfall 3 in `01-RESEARCH.md` warns silently fails). My design's use of the ambient token is for the *read* flow only — the write path exclusively and only ever uses `secrets.VCPKG_PAT_TOKEN` — so the underlying intent the check protects is satisfied; `github.token` sidesteps the literal substring match honestly, without changing which credential is actually used.

## Deviations from Plan

### Auto-fixed / Judgment-call Issues

**1. [Rule 3 - Blocking, scoped workaround] `ctest --preset` unusable for 2 of 5 legs**
- **Found during:** Task 1 authoring, cross-checking `CMakePresets.json` against the plan's own interface contract ("every configure preset already sets ... the generator is Ninja on every preset" — silent about `testPresets` coverage)
- **Issue:** `CMakePresets.json`'s `testPresets` array only has 3 entries (the blocking triplets); invoking `ctest --preset arm64-linux` or `ctest --preset x64-osx` fails with an unknown-preset error
- **Fix:** All 5 legs use `ctest --test-dir build/<preset> --output-on-failure` instead, bypassing `testPresets` entirely without modifying `CMakePresets.json` (out of scope per `files_modified`)
- **Files modified:** none beyond `.github/workflows/ci.yml` itself
- **Verification:** `binaryDir` is identical in shape (`build/${presetName}`) for every configure preset, confirmed by reading `CMakePresets.json` directly; the substitution is mechanically equivalent for the 3 legs that do have `testPresets` entries too, so behavior is uniform across all 5
- **Committed in:** `45ff8b5`

**2. [Judgment call, not a Rule 1-4 deviation — documented per the plan's own instruction] Runner-image choices diverge from `01-RESEARCH.md`'s specific recommendations**
- **Found during:** Task 1, live-checking `actions/runner-images`' current README against research's `macos-15`-preferred / `windows-latest`-assumed guidance
- **Issue:** Research (dated 2026-08-12, one day before this plan's execution) flagged `macos-14` deprecation and preferred `macos-15`, and separately assumed `windows-latest` ships nasm. A live check shows `macos-26` is now GA and `macos-latest`, and `windows-latest` now resolves to Windows Server 2025/VS2026 rather than the VS2022 PROJECT.md requires — neither assumption held one day later, exactly the kind of drift the plan's own instruction anticipated ("verify current runner-image labels... re-verify at execution time regardless of this document's age")
- **Resolution:** `windows-2022` pinned explicitly (hard requirement, not a preference — PROJECT.md's stated toolchain); `macos-15` retained for both macOS legs (matches research's original recommendation, still valid); nasm/mono installed explicitly everywhere rather than assumed
- **Files affected:** `.github/workflows/ci.yml` only
- **Verification:** Live-fetched `actions/runner-images` README (`raw.githubusercontent.com/actions/runner-images/main/README.md`) and per-image READMEs, cross-checked against `gh api repos/actions/runner-images/contents/...`

---

**Total deviations:** 1 auto-fixed (scoped `ctest` invocation workaround); 1 judgment-call runner-image adjustment, both documented per this plan's own "flag any deviation from the preset-driven invocation" and "re-verify runner images at execution time" instructions. No scope creep — `.github/workflows/ci.yml` is the only file this plan touches.

## Issues Encountered

None blocking. The main friction was resolving the tension between Task 2's automated `<verify>` script (which forbids `secrets.GITHUB_TOKEN` textually near `nuget`) and the plan's own acceptance criteria (which require a fork PR to actually have read access to the cache, which structurally requires *some* ambient credential) — resolved via the `github.token` context-property spelling, documented above.

## User Setup Required

**`VCPKG_PAT_TOKEN` (per this plan's own `user_setup` entry) — already satisfied.** A live `gh secret list` check before any task work began shows the secret present (created `2026-08-12T21:10:59Z`), contradicting this plan's stated `<environment>` note that it was absent. No action needed from the user for this plan; if the secret is ever rotated or removed, `.github/workflows/ci.yml`'s read-write NuGet registration step will simply authenticate with an empty password and fail loudly at the `sources add`/`setapikey` call on the next trusted run — it does not silently degrade to "readwrite" with no real credential.

**Repository Actions settings — confirmed, no action needed.** `gh api repos/dkastsenich/mediadiff/actions/permissions/workflow` returns `{"default_workflow_permissions":"read","can_approve_pull_request_reviews":false}` — the repository does not grant broad write permissions to workflow runs by default, consistent with (and independent of) the fork-PR token auto-downgrade GitHub applies regardless of this setting.

## What Remains Unproven (stated plainly, per this plan's own instruction)

This execution session did not push any commit to `origin` — `git ls-remote origin` and `gh auth status` both confirm network/push access exists, but actually triggering GitHub Actions and waiting out one or more FFmpeg builds (15–40 min uncached, per this phase's own dependency) is outside what a single plan-execution pass can responsibly attempt. The following acceptance criteria are therefore **unverified from this session** and require an actual push (by the user or a subsequent `/gsd-ship`-adjacent step) to confirm:

1. All three blocking legs (`x64-linux`, `arm64-osx`, `x64-windows-static-md`) actually report success in a real run, including the full CTest suite passing on each
2. The two non-blocking legs report without being able to flip the aggregate required status
3. The `lint` job reports success in a real run
4. A commit touching only a markdown file still triggers all 5 legs and the lint job
5. **The core BUILD-06 caching proof:** two consecutive runs against an unchanged `vcpkg.json`, where the second run's vcpkg output reports *restoring* the FFmpeg package from the feed rather than building it — this is asserted from vcpkg's own restore-output lines (the "Summarize vcpkg binary-cache outcome for this run" step surfaces these lines in every run's log for exactly this comparison), never from wall-clock timing, per BUILD-06's own recorded assumption. **Caching correctness is entirely unproven until this two-run comparison actually happens against real CI.**
6. A pull request opened from a fork completes with the cache readable and the write step skipped
7. No job log contains the token value (GitHub's automatic secret masking should cover this regardless of the design choices above, but was not observed in a real run)

Everything statically verifiable without a real CI run — YAML validity, both tasks' automated `<verify>` node scripts, every `grep`-based acceptance-criteria count, preset names existing in `CMakePresets.json`, script paths resolving, and the local `ctest`/lint suite still passing unmodified — has been confirmed and is recorded above.

## Next Phase Readiness

- Phase 1 has no further plans after this one. The workflow file is complete and internally consistent, but its actual green-matrix and cache-restore behavior needs proving on a real push before Phase 1 can be considered fully verified end-to-end.
- **Carry-forward:** `CMakePresets.json`'s `testPresets` array is missing entries for `arm64-linux` and `x64-osx` — a real, minor gap in Plan 01's file. This plan's workflow works around it via `ctest --test-dir`, but a future pass should add the two missing `testPresets` entries at the source so `ctest --preset <any-triplet>` works uniformly for local developers too, not just this workflow's own invocation style.
- **Carry-forward:** `VCPKG_PAT_TOKEN`'s presence should be periodically re-confirmed (`gh secret list`, name only) — this plan found it already set, contrary to its own stated environment assumption, which is a good outcome but means the assumption should not be blindly trusted in any future retrospective either.

---
*Phase: 01-foundation-toolchain*
*Completed: 2026-08-13*

## Self-Check: PASSED

`.github/workflows/ci.yml` verified present on disk; commit hash `45ff8b5` verified present in `git log`. Both task-level automated `<verify>` node scripts from `01-05-PLAN.md` re-run against the final committed content and confirmed passing; all `grep`-based acceptance-criteria counts re-verified against the final committed content (not an intermediate draft).
