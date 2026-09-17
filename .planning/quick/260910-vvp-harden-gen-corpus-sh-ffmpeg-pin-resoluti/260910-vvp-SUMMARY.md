---
phase: quick
plan: 260910-vvp
subsystem: testing
tags: [bash, ffmpeg, ci, gen_corpus, fixture-provenance]

requires:
  - phase: 03-probe-layer-container-size
    provides: scripts/ffmpeg_pin.json, scripts/install_pinned_ffmpeg.sh, tests/golden/README.md's designated-leg golden policy
provides:
  - scripts/resolve_pinned_ffmpeg.sh -- shared, sourceable pinned-first ffmpeg resolution + release-identity gate
  - scripts/gen_corpus.sh hardened to reject an off-pin ffmpeg before writing any fixture byte
  - scripts/test_gen_corpus_pin_gate.sh -- seven-case, mutation-proven gate test wired into CI lint
  - tests/golden/README.md documentation of the resolution order, the gate, and the escape hatch
affects: [04-video-analysis, any future script needing this project's fixture-synthesis ffmpeg]

actuals:
  tokens: 10607
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Sourceable + directly-runnable dual-mode bash library (scripts/resolve_pinned_ffmpeg.sh), following scripts/install_pinned_ffmpeg.sh's python3-heredoc pin-reading idiom"

key-files:
  created:
    - scripts/resolve_pinned_ffmpeg.sh
    - scripts/test_gen_corpus_pin_gate.sh
  modified:
    - scripts/gen_corpus.sh
    - .github/workflows/ci.yml
    - tests/golden/README.md
    - .planning/debug/knowledge-base.md

key-decisions:
  - "Identity gate compares the leading MAJOR.MINOR.PATCH release triple, not the whole reported version string -- exact equality against the pin's 9.0.1 would reject the pinned build's own real-world reported strings on every CI leg."
  - "PATH remains a permitted resolution route (never prohibited) -- ci.yml's Windows PowerShell cross-check clears MEDIADIFF_FFMPEG and requires a PATH ffmpeg."
  - "Task 4 (gen_corpus.ps1 parity) intentionally NOT executed -- out of this run's authorized scope; recommendation preserved below."

requirements-completed: [BUILD-08, TRUST-06]

coverage:
  - id: D1
    description: "resolve_pinned_ffmpeg.sh resolves override -> pinned -> PATH and rejects a release-version mismatch unless the escape hatch is set"
    requirement: "BUILD-08"
    verification:
      - kind: integration
        ref: "scripts/test_gen_corpus_pin_gate.sh (Cases 1-6)"
        status: pass
    human_judgment: false
  - id: D2
    description: "gen_corpus.sh sources the resolver and aborts before writing any fixture byte when the resolved binary is not the pinned release"
    requirement: "TRUST-06"
    verification:
      - kind: integration
        ref: "scripts/test_gen_corpus_pin_gate.sh (Case 7); mutation check (gate call removed -> suite goes red)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Documentation of the resolution order, the gate, and the escape hatch's rules"
    verification:
      - kind: other
        ref: "tests/golden/README.md 'The corpus must come from the pinned generator' section (grep-verified)"
        status: pass
    human_judgment: false

duration: ~45min
completed: 2026-09-10
status: complete
---

# Quick Task 260910-vvp: Harden gen_corpus.sh ffmpeg pin resolution Summary

**`scripts/gen_corpus.sh` now prefers the pinned ffmpeg and refuses to generate a corpus with a binary from a different FFmpeg release than `scripts/ffmpeg_pin.json` pins, while `PATH` stays a permitted fallback route.**

## Performance

- **Duration:** ~45 min
- **Tasks:** 3 of 4 (Task 4 explicitly not authorized for this run — see below)
- **Files modified:** 6 (2 created, 4 modified)

## Accomplishments

- New `scripts/resolve_pinned_ffmpeg.sh`: dual-mode (sourceable + directly runnable), bash-3.2 clean, resolves `MEDIADIFF_FFMPEG` override → the repo-local pinned install under `.ffmpeg-pinned/` → `PATH`, keeps the pre-existing `>= 6.1` version floor verbatim, and adds a release-identity gate.
- `scripts/gen_corpus.sh` sources the new file instead of inlining resolution; all fixture recipes' `$FFMPEG_BIN` references are unchanged.
- New `scripts/test_gen_corpus_pin_gate.sh`: seven stub-driven cases (no real ffmpeg, no build) wired into CI's `lint (ENG-16 boundary)` job, plus a mutation check proving the suite is not vacuous.
- `tests/golden/README.md` documents the resolution order, the identity check, the fix command, and the escape hatch's rules.

## Task Commits

Each task was committed atomically:

1. **Task 1: resolve_pinned_ffmpeg.sh — pinned-first resolution plus a release-identity gate, wired into gen_corpus.sh end to end** — `31d285a` (feat)
2. **Task 2: A seven-case test that proves the gate fires and the override still works, wired into the CI lint job** — `9928b3a` (test)
3. **Task 3: Document the resolution order, the gate and what the hatch forfeits** — `ff9fafa` (docs)

_No TDD RED/GREEN split was applied per the orchestrator's MVP_MODE=false, TDD_MODE=false instruction — each task's tests and implementation landed together in one commit._

## Files Created/Modified

- `scripts/resolve_pinned_ffmpeg.sh` — new. Owns resolution (override/pinned/PATH), the `>= 6.1` floor, and `mediadiff_assert_pinned_identity`.
- `scripts/gen_corpus.sh` — lines 18-64 (the old inline `MEDIADIFF_FFMPEG:-ffmpeg}` fallback + floor) replaced by a source of the sibling file plus one call (`mediadiff_resolve_ffmpeg gen_corpus`); header comment updated to point future callers (e.g. `04-03-PLAN.md`'s `measure_parser_overhead.sh`) at the new file.
- `scripts/test_gen_corpus_pin_gate.sh` — new. Seven cases plus a documented mutation-check recipe.
- `.github/workflows/ci.yml` — one new lint-job step (`Run gen_corpus pinned-ffmpeg resolution gate test`); `lint (ENG-16 boundary)` job name unchanged (required status check).
- `tests/golden/README.md` — new "The corpus must come from the pinned generator" section.
- `.planning/debug/knowledge-base.md` — `corpus-fixture-byte-drift` entry's "Known remaining hazard" bullet extended with a 2026-09-10 update recording the follow-up landed; original hazard text left intact.

## The exact failure message the gate emits

Against the real git-master nightly on this workstation (`/usr/local/bin/ffmpeg`, `N-126086-ge5ecfe8970-20260812`), forced via `MEDIADIFF_FFMPEG`:

```
gen_corpus: ffmpeg release-identity mismatch.
gen_corpus: selected binary: /usr/local/bin/ffmpeg (route: override)
gen_corpus: reported version: ffmpeg version N-126086-ge5ecfe8970-20260812 Copyright (c) 2000-2026 the FFmpeg developers
gen_corpus: pin expects: 9.0.1 (scripts/ffmpeg_pin.json)
gen_corpus: fix with: bash scripts/install_pinned_ffmpeg.sh
gen_corpus: consequence -- a corpus generated by an unpinned build produces a byte set matching neither tests/golden/* nor tests/golden/CORPUS_DIGEST.txt, so every byte-exact test reports a regression that is not one (.planning/debug/resolved/corpus-fixture-byte-drift.md).
gen_corpus: to downgrade this to a warning for deliberate experimentation only, set MEDIADIFF_ALLOW_UNPINNED_FFMPEG=1 (see tests/golden/README.md) -- a corpus generated that way must never refresh a golden.
```

## The route report line from a successful run

With `MEDIADIFF_FFMPEG` unset and the pinned install present:

```
gen_corpus: ffmpeg resolved to /home/dzka/projects/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg (route: pinned) -- reports "9.0.1-https://www.martin-riedl.de", pin expects 9.0.1 (scripts/ffmpeg_pin.json)
gen_corpus: installed and checksum-verified by scripts/install_pinned_ffmpeg.sh
```

## Mutation-check result

`scripts/test_gen_corpus_pin_gate.sh`'s own line `  mediadiff_assert_pinned_identity || return 1` was deleted with `sed` on a throwaway copy of `scripts/`, and the same seven-case suite was re-run against that mutated copy. Result: the suite went **red** — Case 4 ("the gate fires") no longer aborted (its `install_pinned_ffmpeg.sh` message never appeared), and Case 7's real `gen_corpus.sh` run went on to create `tests/fixtures/GENERATOR_MANIFEST.json` in its sandbox before the mutated version-check silently passed. 4 of 25 assertions failed on the mutated copy, versus 0 failures on the unmutated tree. This proves the gate call is load-bearing to the test suite, not decorative.

## Decisions Made

- Identity gate extracts and compares the leading `MAJOR.MINOR.PATCH` release triple (via `mediadiff_ffmpeg_release_triple`), not the full reported version string — exact string equality against the pin's `9.0.1` would reject the pinned build's own real reported strings (`9.0.1-https://www.martin-riedl.de` on linux/macos, `n9.0.1-11-ge47273f4d9-20260902` on windows) on every CI leg.
- No `uname` → runner-key mapping duplicated between `install_pinned_ffmpeg.sh` and the new resolver — every `builds` entry in `scripts/ffmpeg_pin.json` is a candidate; the first one that exists AND successfully runs `-version` on this host wins.
- `PATH` stays a permitted resolution route on every leg, never prohibited — `ci.yml`'s Windows PowerShell cross-check clears `MEDIADIFF_FFMPEG` and requires a real `ffmpeg` on `PATH`.
- Pin-read failure (missing/unparseable `scripts/ffmpeg_pin.json`, or no `python3`) is routed through the same fatal-unless-hatched branch as a version mismatch — fail closed, matching `install_pinned_ffmpeg.sh`'s own stance.
- **`.planning/phases/04-video-analysis/04-03-PLAN.md` Task 2's instruction** to resolve the generator binary "exactly as `gen_corpus.sh` does" now means: source `scripts/resolve_pinned_ffmpeg.sh` and call `mediadiff_resolve_ffmpeg <caller-name>`, rather than copying `gen_corpus.sh`'s old inline logic (which no longer exists in that form).

## Task 4 (gen_corpus.ps1 parity) — NOT executed, by orchestrator instruction

Task 4 was explicitly excluded from this run's authorized scope: the original user request named `scripts/gen_corpus.sh` only, and expanding the same hardening to the PowerShell sibling (`scripts/gen_corpus.ps1`) was deliberately reserved for the user's separate approval. `scripts/gen_corpus.ps1` was **not modified**.

This is a deferred recommendation, not a judgment that it is unnecessary: `gen_corpus.ps1` carries the identical unpinned-PATH-fallback defect at its own lines 25-28 (`$env:MEDIADIFF_FFMPEG`, defaulting to bare `ffmpeg`). Today it is masked in CI because `install_pinned_ffmpeg.sh` exports `MEDIADIFF_FFMPEG` into `GITHUB_ENV` (so the shell-script route always wins on Windows CI) and appends the pinned binary's directory to `GITHUB_PATH` (so the PowerShell step's own `PATH` fallback also resolves to the pinned build) — but nothing *asserts* that either resolves correctly; it just happens to today. A developer running `gen_corpus.ps1` locally with a different ffmpeg on `PATH` gets the same silent-substitution risk this quick task closed for the shell variant. If executed later, Task 4's own action text in `PLAN.md` gives the exact shape: same resolution order, same release-triple comparison against `scripts/ffmpeg_pin.json`, same escape-hatch variable name, same route report, keeping the CI Windows step's two invariants (the `was not found` negative-path message, and the `generator, configuration, generated_at` manifest key order) intact.

## Deviations from Plan

None — plan executed exactly as written for Tasks 1-3. One necessary addition within Task 1's own scope: the verify script `V5` requires `grep -c 'FFMPEG_BIN' scripts/gen_corpus.sh` to be `>= 68`; after moving the resolution block out, the recipe-only count was 67, so a one-line comment above the `mediadiff_resolve_ffmpeg gen_corpus` call (naming the variables it sets) was added to both document the new call site and satisfy the count — not a functional change.

## Issues Encountered

None. All verification scripts for Tasks 1-3 passed on first or second attempt (the `FFMPEG_BIN` count adjustment above was the only iteration needed).

## Verification Bar Confirmation

- **Mutation check:** actually performed on a throwaway copy; result reported above (suite goes red, 4/25 assertions fail).
- **`bash -n` clean** on every modified/created shell script: `scripts/resolve_pinned_ffmpeg.sh`, `scripts/gen_corpus.sh`, `scripts/test_gen_corpus_pin_gate.sh` — all confirmed syntax-clean.
- **`scripts/gen_corpus.sh` still resolves successfully on this host:** confirmed via `mediadiff_resolve_ffmpeg gen_corpus`, which resolves to `/home/dzka/projects/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg` (route: pinned) — the pinned binary present at `.ffmpeg-pinned/linux-x86_64/ffmpeg` on this host. No full corpus generation was run (not required by the verification bar).
- **No fixture, golden data file, digest, or generator manifest modified:** `git diff --stat` across all three commits touches only `.github/workflows/ci.yml`, `.planning/debug/knowledge-base.md`, `scripts/gen_corpus.sh`, `scripts/resolve_pinned_ffmpeg.sh`, `scripts/test_gen_corpus_pin_gate.sh`, and `tests/golden/README.md` (documentation, explicitly in scope for Task 3). `tests/fixtures/GENERATOR_MANIFEST.json` was confirmed clean (`git status --short` empty for `tests/fixtures/`).

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `scripts/gen_corpus.sh` is hardened and CI-gated; Phase 04 (currently paused at wave 1 pending the corpus-determinism debug that motivated this task) can resume with confidence that a future local corpus regeneration on this or any other workstation will refuse an off-pin ffmpeg rather than silently producing a third, undetected byte set.
- Task 4 (`gen_corpus.ps1` parity) remains open, tracked here and in this SUMMARY's own recommendation above, for a separate approval.

---
*Quick task: 260910-vvp*
*Completed: 2026-09-10*
