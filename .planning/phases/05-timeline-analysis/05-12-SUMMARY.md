---
phase: 05-timeline-analysis
plan: 12
subsystem: performance
tags: [valgrind, cachegrind, instruction-count, ratchet, perf-gate, ci, wall-clock]

requires:
  - phase: 04-video-analysis
    provides: "tools/bench/parser_overhead.cpp and scripts/measure_parser_overhead.sh, the harness this plan extends (D-11/D-12)"
  - phase: 05-timeline-analysis
    provides: "the full, approved 16-id timeline check roster (05-11), consumed here as the 'full' leg's own analyzer set"
provides:
  - "tools/bench/timeline_overhead.cpp: opt-in dual-leg (plain PacketScan vs the full timeline analyzer set) benchmark, plus a --leg=plain|full single-configuration mode for cachegrind isolation"
  - "scripts/measure_timeline_perf.sh: default wall-clock mode (recorded, never asserted, D-13), --instructions (retired instruction counts under valgrind/cachegrind), --check-baseline (ratchet against a committed ledger, D-14)"
  - "tests/golden/PERF_BASELINE.txt: the committed, CI-read-only instruction-count ledger (D-15)"
  - "a designated-x64-linux-leg-only CI step in .github/workflows/ci.yml, installing valgrind explicitly"
  - "PERF-03's parser+timeline overhead clause and ROADMAP Phase 5 SC5's second clause, both amended in place with the evidence that forced the amendment"
affects: [ci-workflow, phase-05-verification, requirements-traceability]

actuals:
  tokens: 14012
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Two-configuration bench binary with a --leg= single-run mode: the default (both-legs, self-checked) mode serves wall-clock measurement; a single-leg mode serves per-process instrumentation (valgrind/cachegrind profiles one whole process, so two configurations can only be isolated from each other as two separate invocations)"
    - "Ratchet-against-committed-ledger with a printed pasteable replacement line: CI measures, compares, and prints; a human commits the replacement line after review (D-15, mirrors UPDATE_GOLDENS' local-only-refresh / CI-stays-read-only contract, Phase 2 D-12)"
    - "Ledger self-test control clause run unconditionally before the real comparison, against a synthetic baseline never touching the committed file (mirrors scripts/assert_corpus_digest.sh / lint_bash4_builtins.sh's own established shape)"

key-files:
  created:
    - tools/bench/timeline_overhead.cpp
    - scripts/measure_timeline_perf.sh
    - tests/golden/PERF_BASELINE.txt
  modified:
    - CMakeLists.txt
    - .github/workflows/ci.yml
    - .planning/REQUIREMENTS.md
    - .planning/ROADMAP.md

key-decisions:
  - "The 'full metadata-plus-timeline analyzer set' leg is implemented as a hand-curated list of the 7 registered timeline_*_analyzer() functions (mirroring src/probe/orchestrator.cpp's own timeline block, in the same order), run directly against a manually-populated ProbeResults rather than through detail::run_probe -- verified against src/analyzers/timeline/analyzers.h that every timeline analyzer declares required_passes of at most {Pass::demux_header, Pass::packet_scan} (none needs bmff/ebml/ts_scan or parser_scan), so this is a faithful, single-scan reproduction of production dispatch that ALSO exposes PacketScanResult's own read_frame_call_count/accounted_bytes fields for the tool's inline self-checks -- fields detail::run_probe's Fingerprint-only return type does not expose."
  - "Chose PERF_RATCHET_TOLERANCE_PERCENT=2 from real, measured local evidence (not a guessed round number): five repeated valgrind --tool=cachegrind runs of both legs against a fixed input, on this sandbox, produced the IDENTICAL retired-instruction count on every repetition (plain: 57,484,789 x5; full: 81,171,713 x5) -- zero measured same-binary run-to-run jitter, confirming D-13's determinism premise empirically. 2% absorbs cross-build codegen skew (a vcpkg/toolchain bump between the baseline's commit and a later CI run), not measurement noise; it stays far below the magnitude a genuine regression produces (Phase 4's own 43-53% parser-overhead evidence)."
  - "tests/golden/PERF_BASELINE.txt was seeded with a genuinely measured (not invented) LOCAL baseline: the real D-16 600s/1920x1080 reference file, run through valgrind --tool=cachegrind inside a fresh ubuntu:24.04 container (matching the hosted runner's own image and the exact valgrind package version 05-RESEARCH.md confirmed installable) since valgrind is not available in this sandbox by any non-destructive means (no passwordless sudo). Explicitly marked PROVISIONAL; 05-13-PLAN.md's own job is to replace both lines with the designated-leg CI run's real transcribed values."
  - "Local valgrind verification for this plan (Task 2's own stated precondition) was satisfied via a Docker container (ubuntu:24.04 + valgrind 1:3.22.0-0ubuntu3 + python3, built once and reused) rather than host sudo apt-get, which required an interactive password unavailable in this session. The host-built binaries run unmodified inside the container (bind-mounted repo, identical glibc 2.39 base) -- this is a local-verification-only technique; the actual CI recipe (.github/workflows/ci.yml) installs valgrind directly on the runner via sudo apt-get, exactly as the plan specifies, with no Docker involved."
  - "PERF-03's requirement text and ROADMAP SC5 are amended in place (original text kept visible, amendment appended with evidence and the new measurement basis), following Phase 4's own 04-19-PLAN.md convention for PROBE-03/SC4/SC5 -- the amendment states the NEW mechanism (instruction-count ratchet) and records optimising either pass to its original absolute target as deferred, unowned work rather than a target that quietly disappeared."

patterns-established:
  - "A --leg=<name> single-configuration CLI mode on an opt-in bench binary, specifically so a shell script can isolate each configuration under a per-process profiler (valgrind/cachegrind) that cannot itself attribute cost to a sub-region of one process's run."

requirements-completed: [PERF-01, PERF-03, PERF-05]
# PERF-05 is also declared by 05-13-PLAN.md's own frontmatter
# (requirements: [DOC-04, PERF-05]) -- per the shared-ID gate
# (requirements.ready-ids), it stays blocked from Complete until 05-13
# closes out too, regardless of what this plan declares. PERF-01 and
# PERF-03 are declared by this plan alone and are expected to flip to
# Complete via the standard update_requirements step.

coverage:
  - id: D1
    description: "The measurement harness: a ten-minute 1080p reference generated on demand outside the corpus (never entering tests/fixtures/ or CORPUS_DIGEST.txt), wall-clock recorded without assertion, instruction counts available behind --instructions, every failure mode (missing valgrind, missing reference file, unparseable cachegrind output, zero instruction count) failing loudly by name"
    requirement: PERF-01
    verification:
      - kind: other
        ref: "bash scripts/measure_timeline_perf.sh (default mode, manually run against both a reduced local override and the real 600s/1920x1080 reference: plain=38317us full=50818us, both under PERF-01's 3s budget)"
        status: pass
      - kind: other
        ref: "bash scripts/measure_timeline_perf.sh --instructions run without valgrind on PATH (temporarily emptied PATH): exits 1, message names valgrind"
        status: pass
    human_judgment: false
  - id: D2
    description: "The committed baseline ledger and the ratchet: tests/golden/PERF_BASELINE.txt records leg/metric/value/commit per line, CI is read-only against it, a regression prints the pasteable replacement line, and the ratchet's own self-test demonstrably fails on a synthetic 100% regression and a metric absent from the ledger"
    requirement: PERF-03
    verification:
      - kind: other
        ref: "bash scripts/measure_timeline_perf.sh --instructions --check-baseline, run inside a valgrind-equipped container against the real reference file: self-test OK, both metrics within tolerance at an exact match (change=0%), exit 0"
        status: pass
      - kind: other
        ref: "manually forced a synthetic 72% regression by editing the ledger's full_instructions value: exits 1, prints ::error:: and a pasteable replacement line; restoring the original value and re-running returns a clean exit"
        status: pass
    human_judgment: false
  - id: D3
    description: "The designated-leg CI step installs valgrind explicitly, runs the ratchet only on x64-linux, fails the job if the step produced no measurement output at all, and never writes back to the repository (no commit/push/PR); PERF-03 and ROADMAP SC5 both carry their amendments visibly with evidence"
    requirement: PERF-05
    verification:
      - kind: other
        ref: "python3 -c \"import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))\" -- parses; grep -c 'apt-get install -y valgrind' reports 1; grep -n 'git push|git commit|peter-evans/create-pull-request' shows no matches"
        status: pass
    human_judgment: true
    rationale: "PERF-05's own text requires the gate to actually run and report on the real designated-leg CI runner -- that confirmation cannot happen from this sandbox and is explicitly Plan 05-13's job (per this plan's own <human-check> and the flagged provisional-baseline state). A human (or 05-13's own execution) must confirm the first real designated-leg run replaces the provisional ledger lines."

duration: this-session
completed: 2026-09-17
status: complete
---

# Phase 05 Plan 12: Timeline Performance Gates Summary

**Instruction-count ratchet under valgrind/cachegrind (D-13/D-14) replaces PERF-03's absolute <10%/<15% targets, backed by a committed baseline ledger CI never writes (D-15) and a designated-x64-linux-leg CI step that installs valgrind explicitly -- seeded with a genuinely measured, explicitly-provisional local baseline (33% overhead, 32-50ms absolute cost, comfortably inside PERF-01's 3s budget) pending 05-13's real CI transcription.**

## Performance

- **Duration:** this-session
- **Tasks:** 3 completed
- **Files created:** 3 (tools/bench/timeline_overhead.cpp, scripts/measure_timeline_perf.sh, tests/golden/PERF_BASELINE.txt)
- **Files modified:** 4 (CMakeLists.txt, .github/workflows/ci.yml, .planning/REQUIREMENTS.md, .planning/ROADMAP.md)

## Accomplishments

- `tools/bench/timeline_overhead.cpp` (Task 1): an opt-in, self-checked dual-leg benchmark comparing plain `PacketScan` against the full 7-analyzer timeline check roster, mirroring `parser_overhead.cpp`'s discipline (two inline self-checks: identical `read_frame_call_count` across legs, non-zero `measurement_count` on the full leg). A `--leg=plain|full` mode lets a caller isolate exactly one configuration per process invocation, which is what makes per-configuration `valgrind --tool=cachegrind` instrumentation possible at all.
- `scripts/measure_timeline_perf.sh` (Task 1/2): three modes. Default records wall-clock and never asserts it (D-13). `--instructions` runs both legs separately under cachegrind, parses each leg's own retired-instruction total, and fails loudly by name on a missing valgrind, missing reference file, unparseable cachegrind output, or a zero instruction count. `--check-baseline` (implies `--instructions`) compares both legs' counts against the committed ledger with a self-tested ratchet function, printing a pasteable replacement line on every outcome.
- `tests/golden/PERF_BASELINE.txt` (Task 2): the committed, CI-read-only ledger, seeded with a real (not invented) local measurement of the actual 600s/1920x1080 D-16 reference file, explicitly marked provisional pending 05-13's designated-leg transcription.
- `.github/workflows/ci.yml` + `.planning/REQUIREMENTS.md` + `.planning/ROADMAP.md` (Task 3): the designated-leg CI step (valgrind installed explicitly, no write-back to the repo, a no-output guard), and PERF-03/SC5's amendments recording the new measurement basis with Phase 4's and this plan's own evidence, in the same original-plus-amendment form Phase 4's 04-19-PLAN.md established for PROBE-03/SC4/SC5.

## Task Commits

1. **Task 1: measurement harness** - `b68085d` (feat)
2. **Task 2: committed baseline ledger and ratchet** - `07f6d67` (feat)
3. **Task 3: designated-leg CI step and PERF-03/SC5 amendments** - `7f97807` (feat)

_Note: Task 2's `--check-baseline` ratchet implementation (including its own self-test path) was written into `scripts/measure_timeline_perf.sh` as part of Task 1's commit, since both tasks share one file and splitting the implementation would have left an intermediate, non-functional state (the file would reference `check_metric_against_baseline` before Task 2's own commit defined it). Task 2's commit adds the ledger file itself, matching the plan's own file-scoped task boundaries as closely as the shared-file dependency allows._

## Files Created/Modified

- `tools/bench/timeline_overhead.cpp` - the dual-leg / `--leg=` benchmark binary
- `scripts/measure_timeline_perf.sh` - the three-mode measurement/ratchet harness
- `tests/golden/PERF_BASELINE.txt` - the committed instruction-count baseline ledger
- `CMakeLists.txt` - `mediadiff_timeline_overhead` target under the existing `MEDIADIFF_BUILD_BENCH` option
- `.github/workflows/ci.yml` - the designated-leg perf step (valgrind install, bench-target build, the ratchet itself)
- `.planning/REQUIREMENTS.md` - `PERF-03`'s amended text
- `.planning/ROADMAP.md` - Phase 5 SC5's amended second clause

## Decisions Made

See `key-decisions` in the frontmatter above for the full reasoning on: the "full" leg's implementation shape, the ratchet tolerance's empirical basis, the provisional ledger seeding, the Docker-based local valgrind verification technique, and the PERF-03/SC5 amendment convention.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `.gitignore` needed no change (already correct); `.mediadiff-bench/` was already gitignored from Phase 4**
- **Found during:** Task 1, while working through the files_modified list
- **Issue:** The plan's `files_modified` lists `.gitignore`, but `/.mediadiff-bench/` was already present (Phase 4's own D-12 addition) and already covers every file this plan's harness generates there.
- **Fix:** No edit made; verified via `git status --porcelain | grep -c 'mediadiff-bench'` reporting `0` after generating both the small local test file and the full 600s/1920x1080 reference file.
- **Files modified:** none
- **Verification:** `git status --porcelain | grep -c 'mediadiff-bench'` prints `0` (see "Known Harness Quirk" below for the grep exit-code caveat).
- **Committed in:** n/a (no change needed)

**2. [Rule 3 - Blocking] Local `valgrind` unavailable via the plan's own stated precondition path (`apt-get install valgrind`) -- no passwordless sudo in this sandbox**
- **Found during:** Task 1, before any code was written, while checking Task 2's stated precondition
- **Issue:** `sudo apt-get install -y valgrind` requires an interactive password not available to this session (`sudo: a password is required`).
- **Fix:** Built a reusable Docker image (`ubuntu:24.04` + `valgrind 1:3.22.0-0ubuntu3` + `python3`, matching the exact runner image and valgrind version `05-RESEARCH.md` already confirmed installable) and ran every `--instructions`/`--check-baseline` verification with the repo bind-mounted into that container. The host-built binaries execute unmodified there (identical glibc 2.39 base). This is a **local-verification-only** technique -- the shipped CI recipe in `.github/workflows/ci.yml` installs valgrind directly on the runner via `sudo apt-get`, with no Docker involved, exactly as the plan specifies.
- **Files modified:** none (verification technique only, no script changes)
- **Verification:** all of Task 1's and Task 2's `--instructions`/`--check-baseline` acceptance criteria reproduced successfully inside the container, including the missing-valgrind negative path (run on the host directly, no container, PATH emptied) and both the regression and clean-pass paths of `--check-baseline`.
- **Committed in:** n/a (no source change; a verification-environment workaround only)

---

**Total deviations:** 2 (both Rule 3, blocking-environment workarounds; no source code or shipped-behavior changes).
**Impact on plan:** Neither deviation altered any shipped file's behavior. Both were necessary to actually exercise the `--instructions`/`--check-baseline` code paths locally given this sandbox's lack of interactive sudo access.

## Known Harness Quirk

Task 1's own `<verify>` command chains `... && git status --porcelain | grep -c 'mediadiff-bench'` with `&&`. When zero lines match (the correct, passing state), `grep -c` prints `0` to stdout but itself exits `1` (POSIX `grep` convention: a non-zero exit means "no lines matched", independent of `-c`'s own count), which breaks the `&&` chain at that final step even though the printed output (`0`) is exactly what the acceptance criterion asks for ("`git status --porcelain | grep -c 'mediadiff-bench'` reports `0`"). Verified directly: the full chain up through `measure_timeline_perf.sh` exits 0 and prints `0` from the final `grep -c`; the overall shell chain's own exit code is 1 purely from this grep semantics artifact, not from any real untracked file. This is the same class of dynamic-probe quirk recorded elsewhere in this project's session memory (`GSD verify-probe cd prefix`) -- noted here rather than worked around, since altering the harness output to force `grep` to exit 0 on a zero count would be a stranger workaround than documenting the quirk.

## Issues Encountered

None beyond the two deviations above (both environment-access workarounds, not code issues).

## User Setup Required

None - no external service configuration required. (Real CI will need no additional setup either -- `sudo apt-get install -y valgrind` runs unattended in GitHub Actions, unlike this interactive sandbox.)

## Threat Flags

None -- this plan introduces one CI-only system package (`valgrind`, installed from the runner's own Ubuntu archive, never vcpkg-manifested, never linked into `libmediadiff`, never shipped in the binary), already accepted by this plan's own `T-05-SC` threat-register row on that basis.

## Next Phase Readiness

The measurement harness, the committed ledger, the designated-leg CI step, and both requirement/roadmap amendments are all in place and locally verified end-to-end (including the negative paths: missing valgrind, a forced regression, an exact-match pass). What remains, explicitly out of this plan's scope and assigned to **05-13-PLAN.md**: a real designated-leg (`x64-linux`) CI run to (a) confirm the perf step actually executes and passes there, (b) transcribe its real instruction-count values over the two provisional lines in `tests/golden/PERF_BASELINE.txt`, and (c) capture the designated leg's corpus digest listing per that plan's own DOC-04/PERF-05 scope. The wall-clock number for the human-check (`<=3s` PERF-01 budget) was recorded on this workstation: plain=38317us (38.3ms), full=50818us (50.8ms) against the real 600s/1920x1080 reference file -- both several orders of magnitude under the 3s budget, and D-13's non-assertion contract (no test, script, or CI step gates on this number) is upheld throughout.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-17*
