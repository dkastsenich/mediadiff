---
phase: 01-foundation-toolchain
plan: 04
subsystem: infra
tags: [lint, boundary-enforcement, ffmpeg, provenance, licensing, gitignore]

# Dependency graph
requires:
  - phase: 01-foundation-toolchain (plan 01)
    provides: "libmediadiff/mediadiff CMake target split (add_library(libmediadiff STATIC src/util/version.cpp)), the src/ and tests/ tree this plan's directory scaffolding and lint attach to"
provides:
  - "The doc 00 section 7 directory tree under src/ and docs/checks/, held by 12 .gitkeep marker files, zero placeholder headers"
  - "scripts/lint_eng16.sh — ENG-16/D-07 boundary lint over src/core, src/config, src/probe, src/analyzers (all 6 subtrees), src/compare, src/report, src/util; excludes the CLI's own directory by design"
  - "scripts/gen_corpus.sh / .ps1 — system-ffmpeg version gate (>=6.1, MEDIADIFF_FFMPEG override) plus GENERATOR_MANIFEST.json provenance emission (D-08)"
  - "LICENSE (Apache-2.0), NOTICE (FFmpeg/LGPL 2.1+ attribution), .clang-format (LLVM base, 100 cols)"
  - ".gitignore extended: /compile_commands.json, tests/fixtures/* with GENERATOR_MANIFEST.json carved out and kept trackable"
affects: ["01-05"]

# Actuals (#2632)
actuals:
  tokens: 7400
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Boundary lint as a word-boundary grep over a named directory list matching the real add_library(libmediadiff ...) membership, not an assumed layout — fails loudly (naming the missing directory) rather than silently scanning a shorter list"
    - "gen_corpus version-floor gate treats a git-describe 'N-<count>-g<hash>' snapshot build as satisfying any release-based floor, since it is by construction newer than the tag it is offset from"
    - "Fixed-key-order JSON manifest (generator, configuration, generated_at) emitted identically by both the shell and PowerShell variants for cross-platform provenance comparability"

key-files:
  created:
    - scripts/lint_eng16.sh
    - scripts/gen_corpus.sh
    - scripts/gen_corpus.ps1
    - LICENSE
    - NOTICE
    - .clang-format
    - src/core/.gitkeep
    - src/config/.gitkeep
    - src/probe/.gitkeep
    - src/analyzers/container/.gitkeep
    - src/analyzers/video/.gitkeep
    - src/analyzers/timeline/.gitkeep
    - src/analyzers/audio/.gitkeep
    - src/analyzers/content/.gitkeep
    - src/analyzers/size/.gitkeep
    - src/compare/.gitkeep
    - src/report/.gitkeep
    - docs/checks/.gitkeep
  modified:
    - .gitignore

key-decisions:
  - "gen_corpus's version parser explicitly branches on the git-describe 'N-<commits>-g<hash>' snapshot form before falling back to a bare MAJOR.MINOR regex, so a git-master ffmpeg build (this machine's actual binary: 'N-126086-ge5ecfe8970-20260812') is accepted rather than rejected for lacking a parseable release number — proven by running the real gate against this machine's real ffmpeg, not asserted"
  - "The ENG-16 lint's SCAN_DIRS list is written to match add_library(libmediadiff ...)'s actual membership (src/util is included; src/cli is not), not an assumed directory convention — confirmed by reading CMakeLists.txt before writing the scanner"
  - "GENERATOR_MANIFEST.json is generated at script runtime and deliberately left untracked/uncommitted by this plan, per the plan's own artifacts_produced note; .gitignore carves it out of the tests/fixtures/* exclusion so a future developer CAN commit it as real fixture provenance, but this plan does not do so itself"

patterns-established:
  - "Pattern: any lint/gate script that can silently scan a shorter list than intended must assert directory existence up front and name the missing directory in its failure message, never fold a short scan into a clean result"
  - "Pattern: distinguish a matcher's 'found nothing' exit code from its 'the tool itself failed' exit code explicitly (grep exit 1 vs >1) rather than swallowing both into `|| true`"

requirements-completed: [BUILD-08]

coverage:
  - id: D1
    description: "The doc 00 section 7 directory tree exists under src/ and docs/checks/, tracked via 12 .gitkeep marker files, with zero placeholder headers containing invented APIs"
    requirement: "BUILD-08"
    verification:
      - kind: other
        ref: "find src/core src/config src/probe src/analyzers src/compare src/report -name '*.h' -o -name '*.hpp' -o -name '*.cpp' | wc -l -> 0; git ls-files src/ docs/checks/ | grep -c .gitkeep -> 12 total (11 under src/, 1 under docs/checks/)"
        status: pass
    human_judgment: false
  - id: D2
    description: "scripts/lint_eng16.sh passes on the clean tree, fails on a planted std::cout violation (and the failure names the file/line/text), and passes again once the violation is removed"
    requirement: "BUILD-08"
    verification:
      - kind: integration
        ref: "bash scripts/lint_eng16.sh (exit 0) -> plant src/core/_lint_probe.cpp with std::cout -> bash scripts/lint_eng16.sh (exit 1, names src/core/_lint_probe.cpp:1) -> remove -> bash scripts/lint_eng16.sh (exit 0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "scripts/lint_eng16.sh fails loudly, naming the missing directory, rather than reporting clean when a scan target is absent"
    requirement: "BUILD-08"
    verification:
      - kind: integration
        ref: "mv src/probe src/probe_renamed_for_test; bash scripts/lint_eng16.sh -> exit 1, \"scan target 'src/probe' does not exist\"; restored -> exit 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "scripts/gen_corpus.sh gates on ffmpeg >= 6.1, distinguishing an absent binary from a too-old one in its message, and accepts this machine's real 'N-'-prefixed git-master ffmpeg"
    requirement: "BUILD-08"
    verification:
      - kind: integration
        ref: "MEDIADIFF_FFMPEG=/nonexistent/ffmpeg bash scripts/gen_corpus.sh -> exit 1 'was not found'; MEDIADIFF_FFMPEG=<5.0 shim> -> exit 1 'found: ffmpeg version 5.0 ...'; real ffmpeg (N-126086-...) -> exit 0"
        status: pass
    human_judgment: false
  - id: D5
    description: "Two consecutive runs of gen_corpus.sh produce a GENERATOR_MANIFEST.json identical in key order and every value except generated_at; zero fixtures generated is the Phase 1 success case"
    requirement: "BUILD-08"
    verification:
      - kind: integration
        ref: "two-run node key/value comparison (plan's own <verify> block) — pass; manifest contains generator/configuration/generated_at in that fixed order"
        status: pass
    human_judgment: false
  - id: D6
    description: "No media binary can enter git — the ignore rule excludes generated fixtures while keeping GENERATOR_MANIFEST.json trackable"
    requirement: "BUILD-08"
    verification:
      - kind: other
        ref: "git check-ignore -q build (succeeds); git check-ignore -q tests/fixtures/GENERATOR_MANIFEST.json (fails, i.e. not ignored); git check-ignore -q tests/fixtures/<planted-media-file> (succeeds)"
        status: pass
    human_judgment: false
  - id: D7
    description: "Project carries Apache-2.0 LICENSE, an LGPL/FFmpeg NOTICE, and .clang-format at LLVM/100-column"
    requirement: "BUILD-08"
    verification:
      - kind: other
        ref: "grep -c 'Apache License' LICENSE -> 4; grep -ci ffmpeg NOTICE -> 9, grep -c LGPL NOTICE -> 6; grep -c ColumnLimit .clang-format -> 1 (value 100)"
        status: pass
    human_judgment: false

duration: ~30min
completed: 2026-08-13
status: complete
---

# Phase 1 Plan 4: Repository shape, ENG-16 boundary lint, and corpus generator provenance Summary

**The doc 00 directory tree now exists as 12 tracked `.gitkeep` markers with zero invented placeholder APIs; `scripts/lint_eng16.sh` mechanically enforces the library/CLI boundary (D-07) and was observed failing on a planted `std::cout` violation and on a missing scan directory before being observed passing clean; `scripts/gen_corpus.{sh,ps1}` gates on a system ffmpeg >= 6.1 — including this machine's actual git-master build — and records generator identity into a fixed-key-order provenance manifest (D-08).**

## Performance

- **Duration:** ~30 min
- **Completed:** 2026-08-13
- **Tasks:** 2
- **Files modified:** 20 (17 created + 1 modified in Task 1; 2 created in Task 2)

## Accomplishments

- Full doc 00 section 7 tree created under `src/` (core, config, probe, the six `analyzers/` subtrees, compare, report) and `docs/checks/`, each held by an empty `.gitkeep` — no headers, no invented interfaces, confirmed by `find ... -name '*.h' -o -name '*.hpp' -o -name '*.cpp' | wc -l` returning 0 across all six scanned engine subtrees
- `.gitignore` extended (not overwritten) with `/compile_commands.json` and a `tests/fixtures/*` exclusion that carves out `GENERATOR_MANIFEST.json`, proven with `git check-ignore` against `build`, the manifest, and a planted `.mp4` inside `tests/fixtures/`
- `.clang-format` (LLVM base, `ColumnLimit: 100`), `LICENSE` (full Apache-2.0 text), `NOTICE` (names FFmpeg, asserts LGPL 2.1+, records that no GPL component is enabled, points to FFmpeg's and dav1d's upstream source for the LGPL source-availability obligation)
- `scripts/lint_eng16.sh`: word-boundary grep (`(^|[^A-Za-z0-9_])(printf|std::cout|std::cerr|exit\()`) over `src/core`, `src/config`, `src/probe`, `src/analyzers`, `src/compare`, `src/report`, `src/util` — matching `CMakeLists.txt`'s actual `add_library(libmediadiff ...)` membership rather than an assumed layout. The CLI's own directory is never named in the script (`grep -c 'src/cli' scripts/lint_eng16.sh` returns 0). Distinguishes a matcher tool failure (grep exit > 1) from a clean scan (grep exit 1) instead of collapsing both into `|| true`. Fails loudly, naming the missing directory, when a scan target doesn't exist.
- `scripts/gen_corpus.sh` / `scripts/gen_corpus.ps1`: `MIN_MAJOR=6`/`MIN_MINOR=1` named constants, `MEDIADIFF_FFMPEG` override (default `ffmpeg`), and an explicit branch for git-describe `N-<commits>-g<hash>` snapshot builds so this machine's real `N-126086-ge5ecfe8970-20260812` ffmpeg is accepted rather than rejected by a naive numeric parse. On success writes `tests/fixtures/GENERATOR_MANIFEST.json` with fixed key order `generator`, `configuration`, `generated_at` and exits 0 having generated zero fixtures — the Phase 1 success case.

## Task Commits

1. **Task 01-04-T1: Repository tree, project files, and the library-boundary lint** — `7bcc5a3` (feat)
2. **Task 01-04-T2: Deterministic corpus generator with recorded generator identity** — `77d2c48` (feat)

**Plan metadata:** this SUMMARY + STATE.md update (pending final commit)

## Files Created/Modified

- `.gitignore` — added `/compile_commands.json` and the `tests/fixtures/*` + `!tests/fixtures/GENERATOR_MANIFEST.json` pair
- `.clang-format`, `LICENSE`, `NOTICE` — project licensing/format files
- `scripts/lint_eng16.sh` — ENG-16/D-07 boundary lint
- `scripts/gen_corpus.sh`, `scripts/gen_corpus.ps1` — BUILD-08/D-08 corpus generator skeleton, shell and PowerShell
- 12 `.gitkeep` markers: `src/core`, `src/config`, `src/probe`, `src/analyzers/{container,video,timeline,audio,content,size}`, `src/compare`, `src/report`, `docs/checks`

## Decisions Made

- **Git-master version handling (D-08 adjacency):** rather than reject this machine's `N-126086-ge5ecfe8970-20260812` ffmpeg for lacking a bare `MAJOR.MINOR`, the parser recognizes the `[nN]-<digits>-g<hex>` git-describe snapshot form as a distinct, always-newer-than-any-tagged-release case and accepts it. Verified by actually running the gate against this machine's real ffmpeg (see Verification Evidence), not merely asserted.
- **Lint scan scope drawn from CMakeLists.txt, not doc 00's prose:** confirmed `add_library(libmediadiff STATIC src/util/version.cpp)` before writing `SCAN_DIRS`, so `src/util` is included (it currently holds no violations) and no other implicit assumption about target membership is baked in.
- **GENERATOR_MANIFEST.json left uncommitted:** the plan's own `artifacts_produced` block lists it under "Generated at runtime (not committed)". It exists in the working tree after running `scripts/gen_corpus.sh` (required for the acceptance-criteria proof) but was not `git add`ed — the `.gitignore` rule merely makes it *possible* to track, for a future plan that generates real fixtures.

## Deviations from Plan

### Auto-fixed Issues

None — no code required a fix beyond what the plan's own action/verify text specified.

### Noted Plan-Text Discrepancy (not a code change)

**1. [Plan-spec inconsistency, not a Rule 1-4 deviation] Acceptance criterion miscounts `.gitkeep` scope**

- **Found during:** Task 1 verification
- **Issue:** The plan's acceptance criteria state `git ls-files src/ | grep -c '.gitkeep'` returns 12, but the plan's own `files_modified` list places 11 `.gitkeep` markers under `src/` and 1 (`docs/checks/.gitkeep`) outside it. `git ls-files src/ | grep -c '.gitkeep'` therefore returns 11, not 12; the total across both directories is 12.
- **Resolution:** Did not create a 12th spurious marker under `src/` to force the literal count — that would contradict doc 00 section 7's actual tree (which the plan's own `read_first` names as authoritative). Verified the intended invariant instead: `git ls-files src/ docs/checks/ | grep -c '.gitkeep'` returns 12, and every directory doc 00 section 7 names is present and tracked. No code or structure was changed to chase the literal grep scope in the acceptance criterion.
- **Files affected:** none (documentation-only observation)
- **Verification:** `git ls-files src/ | grep -c '.gitkeep'` -> 11; `git ls-files docs/checks/ | grep -c '.gitkeep'` -> 1; sum -> 12, matching the plan's `files_modified` list exactly.

---

**Total deviations:** 0 auto-fixed; 1 noted plan-text discrepancy with no code impact.
**Impact on plan:** None on delivered functionality — every `must_haves` truth, artifact, and prohibition in the plan is satisfied by the code as written; only one acceptance-criterion *command's scope* (`git ls-files src/` vs. the two directories the plan's own file list actually touches) doesn't match the number stated next to it.

## Issues Encountered

None. The build tree from Plans 01/02 was warm and untouched by this plan (no `CMakeLists.txt` changes); `ctest --test-dir build/x64-linux` reports 9/9 passing both before and after this plan's commits.

## User Setup Required

**system-ffmpeg** (per this plan's `user_setup` entry): a system `ffmpeg` >= 6.1 CLI on PATH is required for `scripts/gen_corpus.sh`. This machine already had one satisfying the precondition — `ffmpeg version N-126086-ge5ecfe8970-20260812` (a git-master snapshot build, confirmed via `ffmpeg -version | head -1` before Task 2 began) — so no installation step was needed in this environment. Recorded here for a developer machine that lacks it: `apt-get install ffmpeg` (Debian/Ubuntu), `brew install ffmpeg` (macOS), `winget install Gyan.FFmpeg` (Windows).

## Captured Verification Evidence

**Fail-first proof for the ENG-16 lint (planted violation):**
```
$ bash scripts/lint_eng16.sh
ENG-16: clean. No standard-stream writes or process-exit calls found in the scanned engine subtrees.
exit: 0

$ printf 'int f(){ std::cout << 1; return 0; }\n' > src/core/_lint_probe.cpp
$ bash scripts/lint_eng16.sh
ENG-16 violation: a library-side source writes to a standard stream or calls the process-exit function:
src/core/_lint_probe.cpp:1:int f(){ std::cout << 1; return 0; }
exit: 1

$ rm -f src/core/_lint_probe.cpp
$ bash scripts/lint_eng16.sh
ENG-16: clean. No standard-stream writes or process-exit calls found in the scanned engine subtrees.
exit: 0
```

**Fail-first proof for the missing-directory case:**
```
$ mv src/probe src/probe_renamed_for_test
$ bash scripts/lint_eng16.sh
ENG-16 lint error: scan target 'src/probe' does not exist.
Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing.
exit: 1
$ mv src/probe_renamed_for_test src/probe
$ bash scripts/lint_eng16.sh
ENG-16: clean. ...
exit: 0
```

**gen_corpus version gate — real machine ffmpeg accepted:**
```
$ ffmpeg -version | head -1
ffmpeg version N-126086-ge5ecfe8970-20260812 Copyright (c) 2000-2026 the FFmpeg developers
$ bash scripts/gen_corpus.sh
gen_corpus: manifest written to tests/fixtures/GENERATOR_MANIFEST.json. No fixtures generated in Phase 1 (skeleton only).
```

**gen_corpus version gate — absent vs. too-old, both messages verbatim:**
```
$ MEDIADIFF_FFMPEG=/nonexistent/ffmpeg bash scripts/gen_corpus.sh
gen_corpus requires a system ffmpeg >= 6.1 on PATH (or MEDIADIFF_FFMPEG pointing at one); '/nonexistent/ffmpeg' was not found.
exit: 1

$ cat /tmp/shim_bin/fake_ffmpeg   # shim used for the below-floor test
#!/usr/bin/env bash
if [ "$1" = "-version" ]; then
  echo "ffmpeg version 5.0 Copyright (c) 2000-2022 the FFmpeg developers"
  echo "built with gcc 11"
  echo "configuration: --disable-gpl"
  exit 0
fi
exit 1

$ MEDIADIFF_FFMPEG=/tmp/shim_bin/fake_ffmpeg bash scripts/gen_corpus.sh
gen_corpus requires a system ffmpeg >= 6.1; found: ffmpeg version 5.0 Copyright (c) 2000-2022 the FFmpeg developers
exit: 1
```
(The shim was created only in the session scratchpad/`/tmp`, never committed to the repo.)

**Two-run manifest determinism (key order and non-timestamp values identical):**
```
$ bash scripts/gen_corpus.sh && cp tests/fixtures/GENERATOR_MANIFEST.json /tmp/gm1.json
$ sleep 1 && bash scripts/gen_corpus.sh
$ diff /tmp/gm1.json tests/fixtures/GENERATOR_MANIFEST.json
4c4
<   "generated_at": "2026-08-12T22:36:48Z"
---
>   "generated_at": "2026-08-12T22:36:49Z"
```

**Manifest key order — shared by both variants (for Plan 05's Windows leg to assert against):**
```
generator, configuration, generated_at
```
Confirmed identical order in `scripts/gen_corpus.sh` (JSON heredoc field order) and `scripts/gen_corpus.ps1` (`[ordered]@{ generator = ...; configuration = ...; generated_at = ... }`, preserved through `ConvertTo-Json`).

**Ignore-rule proof:**
```
$ git check-ignore -q build && echo OK           # build output ignored
OK
$ git check-ignore -q tests/fixtures/GENERATOR_MANIFEST.json || echo "not ignored (tracked)"
not ignored (tracked)
$ touch tests/fixtures/somejunk.mp4 && git check-ignore -q tests/fixtures/somejunk.mp4 && echo "media ignored"
media ignored
```

## Next Phase Readiness

- `scripts/lint_eng16.sh` is ready to be wired as a CI/CTest step in Plan 05 — it is standalone-runnable (`bash scripts/lint_eng16.sh`) and does not depend on this plan's CMake being untouched.
- `scripts/gen_corpus.ps1`'s execution (not just its shape) is exercised on Plan 05's Windows CI leg; its manifest key order (`generator`, `configuration`, `generated_at`) is recorded above for that leg to assert against.
- Plan 05 (CI matrix) can build directly on this plan's `.gitignore`, `LICENSE`/`NOTICE`, and the two scripts without further scaffolding.
- No blockers for Plan 03 (Windows UTF-8 / `util/fs.h`) — this plan touched none of `CMakeLists.txt`, `src/cli/main.cpp`, `src/util/fs.h`, or `tests/**`, honoring the ownership boundaries stated in this plan's critical reminders.

---
*Phase: 01-foundation-toolchain*
*Completed: 2026-08-13*

## Self-Check: PASSED

All 19 created/modified files verified present on disk (17 from Task 1 + 2 from Task 2); both task commit hashes (`7bcc5a3`, `77d2c48`) verified present in `git log`.
