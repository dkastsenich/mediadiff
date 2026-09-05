---
phase: 03-probe-layer-container-size
plan: 19
subsystem: testing
tags: [ci, golden-tests, ffmpeg, corpus, ctest, github-actions]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: "03-16's pinned-ffmpeg-per-runner CI install and per-leg CORPUS_DIGEST_SUMMARY= reporting step; 03-17's Windows MSVC build fix and 03-18's bash-3.2 portability fixes, both needed so a real CI run could reach the digest step on every leg"
provides:
  - "A measured, evidence-based answer (not an assumption) to whether the pinned-ffmpeg corpus is byte-identical across CI legs: it is NOT — x64-linux and x64-windows-static-md agree byte-for-byte on all 80 fixtures, arm64-osx diverges on 76 of them"
  - "tests/golden/CORPUS_DIGEST.txt — the committed, CI-asserted per-fixture SHA-256 listing for the designated leg's corpus"
  - "A standing CI gate ('Assert the corpus digest matches the committed pin (D-GAP-01)') that hard-fails the designated leg (x64-linux) on any corpus drift, and explicitly announces non-assertion on every other leg"
  - "A named, counted CTest exclusion (EXPECTED_EXCLUDED_COUNT=5) confining the 5 byte-exact fixture-derived golden tests to the designated leg only, guarded against silently widening"
affects: [ci-workflow, golden-test-policy, future-ffmpeg-pin-bumps]

# Actuals (#2632)
actuals:
  tokens: 5123
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns: ["designated-leg golden assertion", "named+counted CTest exclusion guard against silent widening"]

key-files:
  created:
    - tests/golden/CORPUS_DIGEST.txt
  modified:
    - .github/workflows/ci.yml

key-decisions:
  - "D-GAP-01 corpus-identity policy: `designated` (not `uniform`) — chosen by the developer at the Task 2 checkpoint on Task 1's real-run evidence (run 33983460934): the three blocking legs do NOT agree (x64-linux and x64-windows-static-md byte-identical on all 80 fixtures; arm64-osx diverges on 76/80), so byte-exact fixture-derived golden assertions are pinned to x64-linux only, never loosened or made tolerant"
  - "Exclusion narrows SCOPE (which leg runs the byte-exact goldens) never STRENGTH (the assertion itself stays byte-exact on the designated leg) — the excluded set is named, its size (5) is asserted against unfiltered-vs-filtered ctest -N totals, and every non-designated leg prints the 5 excluded test names and the reason in its own log"

requirements-completed: [TRUST-06, TRUST-09, DOC-03]

coverage:
  - id: D1
    description: "Per-leg corpus digest evidence recorded from a real CI run, answering whether cross-platform byte-identity holds under the pinned ffmpeg"
    verification:
      - kind: other
        ref: "gh run view 33983460934 --log | grep CORPUS_DIGEST_SUMMARY= (quoted verbatim in this SUMMARY's evidence table)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Developer confirmed the `designated` corpus-identity policy at the Task 2 blocking checkpoint"
    verification: []
    human_judgment: true
    rationale: "A policy choice narrowing a locked decision (D-GAP-01) requires explicit developer confirmation, not automated inference — this is a decision record, not a testable behavior"
  - id: D3
    description: "Designated-leg digest gate and named/counted CTest exclusion implemented in .github/workflows/ci.yml, with tests/golden/CORPUS_DIGEST.txt committed"
    verification:
      - kind: other
        ref: "bash scripts/corpus_digest.sh | diff -u tests/golden/CORPUS_DIGEST.txt - (verified locally, see Task 3 evidence below)"
        status: pass
      - kind: integration
        ref: "CI run 33989567384, job build (x64-linux): success (digest gate passed, full byte-exact golden suite ran and passed)"
        status: pass
    human_judgment: false

duration: 65min
completed: 2026-09-05
status: complete
---

# Phase 03 Plan 19: Cross-Platform Corpus Byte-Identity Summary

**Measured (not assumed) that the pinned-ffmpeg corpus is NOT byte-identical across CI legs — arm64-osx diverges from x64-linux/x64-windows-static-md on 76 of 80 fixtures — and implemented the developer-confirmed `designated`-leg policy as a standing CI gate: a committed digest asserted only on x64-linux, with the byte-exact fixture-derived goldens named-and-counted-excluded elsewhere.**

## Performance

- **Duration:** ~65 min across two work sessions (Task 1 measurement + checkpoint wait, then Task 3 implementation)
- **Tasks:** 3 (2 code/evidence tasks + 1 blocking decision checkpoint)
- **Files modified:** 2 (`.github/workflows/ci.yml`, `tests/golden/CORPUS_DIGEST.txt` created)

## Accomplishments

- Answered, from real per-leg CI evidence rather than assumption, the question D-GAP-01 could not answer by itself: pinning one nominal ffmpeg version does not produce byte-identical fixtures across platforms.
- Got an explicit, recorded developer decision (`designated`) before narrowing any part of a locked golden-test guarantee.
- Implemented a standing CI gate that keeps the byte-exact assertions genuinely byte-exact (never loosened, never made tolerant, never duplicated per platform) while confining them to the one leg whose corpus they are actually valid against.
- Built the exclusion so it cannot silently widen: the excluded-test count is asserted against a named constant, and every non-designated leg's log names each excluded test and states why.

## Task Commits

1. **Task 1: Read per-leg corpus digests, state whether identity holds** - `8dff505` (docs)
2. **Task 2: Checkpoint — confirm the corpus-identity policy** - no commit (decision checkpoint; developer selected `designated`)
3. **Task 3: Implement the confirmed policy as a standing gate** - `351750c` (feat)

**Plan metadata:** this commit (docs: complete plan)

## Files Created/Modified

- `tests/golden/CORPUS_DIGEST.txt` - Committed per-fixture SHA-256 listing (80 lines) plus `CORPUS_DIGEST_SUMMARY=` line, extracted verbatim from the x64-linux leg's "Report corpus digest" step output in run 33983460934; regenerating locally via `bash scripts/corpus_digest.sh` reproduces it byte-for-byte.
- `.github/workflows/ci.yml` - Added the `Assert the corpus digest matches the committed pin (D-GAP-01)` step (hard fail on x64-linux on any drift; explicit non-assertion announcement on every other leg) immediately after `Report corpus digest`; and, in the `Test` step, a `designated`-branch conditional that runs the full suite unmodified on x64-linux and, on every other leg, excludes the 5 byte-exact fixture-derived golden tests by name via a CTest `-E` regex, guarded by `EXPECTED_EXCLUDED_COUNT=5` asserted against the unfiltered-vs-filtered `ctest -N` totals, with each excluded test named in the log.

## Task 1: Per-leg digest evidence

**Real CI run used:** [`33983460934`](https://github.com/dkastsenich/mediadiff/actions/runs/33983460934) (branch `gsd/phase-03-probe-layer-container-size`, head SHA `a5cc9a999c684f8d3f48316241d3f7a8ea969760` — includes plans 03-16, 03-17, 03-18). Overall run conclusion `failure` (driven by legs unrelated to this evidence — see per-leg table); every one of the five build legs reached and completed the `Report corpus digest (cross-platform byte-identity evidence)` step before any later failure, so all five digests below are real, not "unknown."

### Five-leg digest table

| Build leg | Runner | Pinned ffmpeg version (from `install_pinned_ffmpeg.sh` output) | `CORPUS_DIGEST_SUMMARY=` |
|---|---|---|---|
| `build (x64-linux)` | `ubuntu-latest` (x64) | `9.0.1-https://www.martin-riedl.de` | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `build (arm64-osx)` | macOS arm64 | `9.0.1-https://www.martin-riedl.de` | `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` |
| `build (x64-windows-static-md)` | Windows x64 | `n9.0.1-11-ge47273f4d9-20260902` (BtbN build) | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `build (x64-osx)` | macOS x64 | `9.0.1-https://www.martin-riedl.de` | `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` |
| `build (arm64-linux)` (non-blocking) | Linux arm64 | `9.0.1-https://www.martin-riedl.de` | `a1148c1bc3457152a25a032afd03e370c281398521639d60f3ec50a69d446aac` |

**Per-leg job conclusion and failing step (for context — none of these failures happened before the digest step ran):**

| Job | Conclusion | Failing step |
|---|---|---|
| `build (x64-linux)` | `success` | — |
| `build (arm64-osx)` | `failure` | `Build` (WINDOWS.md #13, pre-existing AppleClang `-Werror` unused-const-variable, out of scope) |
| `build (x64-windows-static-md)` | `failure` | `Build` (pre-existing, out of scope this round) |
| `build (x64-osx)` | `failure` | `Build` (WINDOWS.md #14, pre-existing cross-arch linker mismatch, out of scope) |
| `build (arm64-linux)` | `failure` | `Register vcpkg NuGet feed` (WINDOWS.md #11, pre-existing, non-blocking) |
| `lint (ENG-16 boundary)` | `success` | — |

### Question 1 — Do the three BLOCKING legs (`x64-linux`, `arm64-osx`, `x64-windows-static-md`) agree?

**No.** Two of the three agree; one does not:

| Leg | Digest |
|---|---|
| `x64-linux` | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `x64-windows-static-md` | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `arm64-osx` | `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` |

`x64-linux` and `x64-windows-static-md` are byte-identical across **all 80** fixture lines (`diff` between the two full per-fixture listings pulled from the run log reports zero differing lines). `arm64-osx` diverges from both. All three legs reported the same nominal ffmpeg version family (`9.0.1`); the two Linux/Windows builds happen to both come from martin-riedl.de/BtbN builds of the same upstream release and agree byte-for-byte, while the macOS arm64 build of the same nominal release does not. This is consistent with 03-16-SUMMARY.md's finding (WINDOWS.md #12): `+bitexact` does not force a fixed SIMD/floating-point codepath, so a different microarchitecture (here, a genuinely different OS/CPU family: Apple Silicon vs x86_64) diverges even under a pinned nominal version.

### Question 2 — Do the two macOS legs (`arm64-osx`, `x64-osx`) agree with each other?

**Yes.** Both report `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` — identical, as expected since (per this plan's A3) they resolve to the same `macos-arm64` pin entry and run on the same host architecture. This rules out "the pin manifest resolved to two different binaries" as an explanation for the blocking-leg disagreement; the divergence is a genuine `arm64-osx` vs `x64-linux`/`x64-windows-static-md` platform difference, not a pin-resolution bug.

### Question 3 — Which fixtures differ, and how many?

Pulling the full 80-line per-fixture listing for `x64-linux` and `arm64-osx` out of the run log and comparing by fixture name (not by line position, since `sort` under `LC_ALL=C` is stable but this comparison joins on name defensively):

**76 of 80 fixtures differ.** Only 4 fixtures are byte-identical between `x64-linux` and `arm64-osx`:

- `.topo_chapters.ffmeta` — a hand-authored ffmetadata text file, not ffmpeg-encoded media
- `.topo_subs.srt` — a hand-authored subtitle text file, not ffmpeg-encoded media
- `size_partial.mp4` — the 25,000-tiny-frame fixture added in 03-09 (trivial/degenerate encode)
- `tracer_empty.mp4` — an empty/near-empty fixture

Every fixture that involves a real encoded audio or video payload of non-trivial size differs between the two platforms. Representative examples (full list of 76 differing names available in this plan's working evidence; a sample): `idem_a.mp4`, `idem_b.mp4`, `lang_eng.mp4`, `mkv_opus_a.webm`, `mp4_faststart.mp4`, `size_crf20.mp4`, `ts_204.ts`, `ts_multiprogram.ts`, `topo_chapters.mkv`, `tracer_a.mp4` — spanning every fixture family (mp4, mkv, webm, ts) and every fixture use case (topology, tags, size, timeline, TS-specific). This is not a narrow, isolated codec-path difference; it is essentially the entire corpus.

**Answer to the governing question:** cross-platform byte-identity under the pin does **not** hold. Two platforms sharing a similar toolchain lineage (Linux x86_64, Windows x86_64 via BtbN — itself built for the same architecture family) happen to agree; the genuinely different CPU/OS platform (macOS arm64) does not, on 95% of the corpus.

## Task 2: Checkpoint decision

Presented Task 1's evidence table verbatim, with two options (`uniform` — enforce one committed digest on every leg; `designated` — byte-exact fixture-derived goldens run on x64-linux only). **The developer chose `designated`**, on the strength of Task 1's evidence: the three blocking legs do not agree (x64-linux and x64-windows-static-md byte-identical on all 80 fixtures, arm64-osx diverging on 76/80), so enforcing a single committed digest on every leg (`uniform`) would either be a permanent red build on arm64-osx or would require silently accepting a digest that doesn't reflect that leg's real corpus. `designated` was chosen specifically because it keeps the byte-exact assertions genuinely byte-exact (never loosened, never made tolerant — the exact thing D-GAP-01 protects) while confining them to the leg whose corpus the committed digest actually describes.

**Flagged assumption (what is no longer covered on non-designated legs):** on `arm64-osx`, `x64-osx`, `x64-windows-static-md`, and `arm64-linux`, the following 5 byte-exact fixture-derived golden tests do not run:
- `unit.inspect_container - golden:` (the container+meta section for one representative fixture per family)
- `unit.ts_scan_golden - ts_204.ts matches` the committed TSDuck-derived golden
- `unit.ts_scan_golden - ts_multiprogram.ts matches` the committed TSDuck-derived golden
- `unit.ts_scan_golden - ts_single.ts matches` the committed TSDuck-derived golden
- `integration.size_checks - the size.* findings are pinned` by a committed, read-only golden

`TRUST-06` remains covered on every leg since it compares two encodes produced by that leg's own binary within the same run and is therefore unaffected by cross-platform corpus variance.

## Task 3: Implementation

Implemented exactly the `designated` option confirmed at the checkpoint (`351750c`):

- **Committed `tests/golden/CORPUS_DIGEST.txt`** — the exact 80-line per-fixture SHA-256 listing plus `CORPUS_DIGEST_SUMMARY=` line, taken from the designated leg's (`x64-linux`) real CI run output (run 33983460934), matching Task 1's evidence table (`d351f426...`). Verified locally that `bash scripts/corpus_digest.sh | diff -u tests/golden/CORPUS_DIGEST.txt -` reports no differences against a freshly regenerated corpus.
- **Added the `Assert the corpus digest matches the committed pin (D-GAP-01)` CI step**, placed immediately after `Report corpus digest (cross-platform byte-identity evidence)`. On `x64-linux` it diffs the live corpus digest against the committed file and hard-fails (printing the diff) on any mismatch; CI never writes the file. On every other leg it prints an explicit line naming the designated leg and stating that this leg's corpus is deliberately not asserted against the committed digest — never a silent skip.
- **Added the named, counted CTest exclusion** in the `Test` step. On `x64-linux`, `ctest` runs unmodified (full suite, including the 5 byte-exact fixture-derived goldens). On every other leg, the 5 tests are excluded by an explicit CTest name regex (`EXCLUDED_TEST_REGEX`), and `EXPECTED_EXCLUDED_COUNT=5` is asserted against the difference between the unfiltered and filtered `ctest -N` totals — mirroring the existing `Total Tests:` guard's own reasoning that a check whose count can drift silently is the same defect class as a check that silently stops gating. Each excluded test is named individually in the log alongside the reason (pinned ffmpeg builds for this runner and for the designated leg do not produce byte-identical fixtures).
- Confirmed no test source under `tests/support/`, `tests/unit/`, or `tests/integration/` was touched (`git diff --stat` for those paths is empty in `351750c`), and `.gitignore` was not touched — no tolerance, normalization, or per-platform golden set was introduced anywhere.

**CI outcome — run `33989567384`, head `351750c` (COMPLETED), verified by the orchestrator (not re-run by this finalization pass):**

| Leg | Result |
|---|---|
| `build (x64-linux)` — designated, blocking | **success** — digest gate passed, full byte-exact golden suite ran and passed |
| `lint (ENG-16 boundary)` — required | **success** |
| `build (x64-windows-static-md)` — blocking | failure at Build (see Issues Encountered — pre-existing, out of scope) |
| `build (arm64-osx)` — blocking | failure at Build — pre-existing WINDOWS.md #13 |
| `build (x64-osx)` — non-blocking | failure at Build — pre-existing WINDOWS.md #13/#14 |
| `build (arm64-linux)` — non-blocking | failure at "Register vcpkg NuGet feed" — infrastructure/permissions, unrelated |

All three blocking-leg verification requirements this plan owns are satisfied: the designated leg (`x64-linux`) is `success` with the digest gate and full byte-exact golden suite passing; the other two blocking legs' failures are pre-existing, out-of-scope build defects unrelated to this plan's changes (see Issues Encountered).

## Decisions Made

- **D-GAP-01 policy = `designated`**, chosen by the developer at the Task 2 checkpoint directly on Task 1's real-run divergence evidence (x64-linux/x64-windows-static-md byte-identical on all 80 fixtures; arm64-osx diverging on 76 of 80). See "Task 2: Checkpoint decision" above for the full rationale and the flagged-assumption list of what is no longer covered on non-designated legs.
- Byte-exact assertions were narrowed in **scope only** (which leg runs them), never in **strength** (no assertion was made tolerant, normalized, or duplicated per platform) — this was the explicit design constraint carried from D-GAP-01 through to the checkpoint choice and into the Task 3 implementation.

## Deviations from Plan

None — plan executed exactly as written, including the blocking checkpoint gate before any implementation began.

## Issues Encountered

- The `build (x64-windows-static-md)` leg failed at `Build` on run `33989567384`, a pre-existing, newly-reachable MSVC-only defect (not introduced by 03-19, not in this plan's `files_modified`): `src/cli/main.cpp:297` calls `report_cli_error(...)` unqualified from inside `wmain`, which sits outside `namespace mediadiff` (closed at line 209) — it needs `mediadiff::report_cli_error`. It only became reachable because 03-17 fixed the earlier C2059 that used to abort the Windows build first. Recorded here per the orchestrator's instruction; not fixed in this plan. Plan 03-20 owns tracking/fixing it.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The corpus-identity question that D-GAP-01 left open is now closed with real evidence and a standing, non-silent CI gate: `tests/golden/CORPUS_DIGEST.txt` is committed and asserted on the designated leg, and the byte-exact fixture-derived golden exclusion elsewhere is named, counted, and cannot silently widen.
- `TRUST-06`, `TRUST-09`, and the in-scope `DOC-03` rows (`ordering`, `empty`) are satisfied by this plan's `scripts/corpus_digest.sh` sort-order guarantee and the zero-fixture guard respectively; the `adjacency` row remains out of this round's scope per this plan's `<flagged_assumptions>` A4.
- Open, out-of-scope item for a future plan: the `x64-windows-static-md` `report_cli_error` namespace-qualification build failure described above under Issues Encountered (owned by plan 03-20).
- A future ffmpeg pin bump under the `designated` policy must regenerate `tests/golden/CORPUS_DIGEST.txt` from the designated leg's (`x64-linux`) real CI output in the same commit as the pin change, per this plan's Task 3 action text — the digest and the pin move together.

## Self-Check: PASSED

- FOUND: `.github/workflows/ci.yml` (contains `Assert the corpus digest matches the committed pin (D-GAP-01)`)
- FOUND: `tests/golden/CORPUS_DIGEST.txt` (81 lines, last line `CORPUS_DIGEST_SUMMARY=d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712`)
- FOUND commit: `8dff505` (docs(03-19): record per-leg corpus digest evidence from real CI run)
- FOUND commit: `351750c` (feat(03-19): implement designated-leg corpus-digest gate (D-GAP-01))

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-05*
