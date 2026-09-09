---
phase: 03-probe-layer-container-size
verified: 2026-09-06T12:00:00Z
status: passed
score: 5/5 roadmap success criteria verified
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "4/5 fully verified; SC5 failed under real CI evidence"
  gaps_closed:
    - "SC5 (\"Encoding a fixture twice with identical settings and comparing under sw-encoder comes back clean as a CI release blocker\") — closed on independently re-queried real CI run 34023871831 (head 64bc168). All three blocking legs (x64-linux, arm64-osx, x64-windows-static-md) plus the required lint job conclude Build=success/Test=success at both job and step level — independently confirmed via `gh run view --json jobs` and per-job `--log` greps, not by re-quoting 03-22-SUMMARY.md's narration."
    - "WINDOWS.md #13 (AppleClang unused-const-variable, arm64-osx/x64-osx) — closed: `kClusterId` is now referenced by a real, behavior-asserting test case (unknown-size Cluster-inside-Segment edge, PROBE-05); confirmed present in the working tree and confirmed compiling under arm64-osx in the closing run."
    - "WINDOWS.md #16 (unqualified report_cli_error, x64-windows-static-md) — closed: all three call sites in main.cpp now read `mediadiff::report_cli_error(...)`; confirmed present, confirmed compiling under MSVC in the closing run."
    - "WINDOWS.md #18/#19/#20/#21 — four further blocking-leg defects discovered en route (missing provenance_render.cpp link on the unit-test target; a `far` identifier colliding with a Windows SDK legacy macro; an arm64-osx bitrate-margin doc03_coverage failure; a GITHUB_PATH MSYS-vs-native-path bug breaking the Windows job's PowerShell cross-check step) — all four fixed and closed with cited observed evidence and a resolved_at timestamp, confirmed via `windows status` (total 22, 11 fixed/11 open, markdown table and JSON array agree)."
  gaps_remaining: []
  regressions: []
deferred: []
---

# Phase 3: Probe Layer, Container & Size Verification Report

**Phase Goal:** Real media enters the engine — one header pass and one packet sweep feed every
container, metadata and size check, plus the shared primitives that later phases consume instead
of recomputing.
**Verified:** 2026-09-06T12:00:00Z
**Status:** passed
**Re-verification:** Yes — after gap-closure round 3 (plans 03-21, 03-22)

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---|---|---|
| 1 | `mediadiff inspect` on MP4/MOV, MKV/WebM, MPEG-TS renders a complete container section | ✓ VERIFIED (unchanged, no regression from round 3) | No round-3 plan touched the rendering path. Local `ctest -N` still reports the same 623-test tree (622 + 1 new PROBE-05 test); container-family tests unaffected. |
| 2 | Cross-container migration demotes cleanly; truncated/garbage input degrades to `skipped:unparsed_mechanism` with byte offset or exits 65; never crashes or silently passes | ✓ VERIFIED (closed round 1, unaffected by round 3) | Round-3's diff touches `tests/unit/test_ebml_scan.cpp`, `src/cli/main.cpp`, two scripts, and ledger/digest files only — none of which is on this code path. |
| 3 | `size.*` checks report rate economics from the packet scan alone, DTS-in-ticks windowing, cross-platform-identical | ✓ VERIFIED (closed round 1; the one round-3-era wrinkle is a fixture-margin fix, not a computation defect) | WINDOWS.md #20 (arm64-osx's `doc03_coverage` clean-pair for `size.stream_bitrate` tipping over its 3% warn bound under cross-architecture SIMD variance) was a **fixture calibration** issue, not a defect in `size.*`'s own DTS-tick windowing math — fixed by widening the fixture pair's margin (700k/715k) and regenerating the designated-leg digest from real CI output. Confirmed fixed: `build (arm64-osx)` reports `100% tests passed out of 618` in the closing run. |
| 4 | Each file read exactly once; PROBE-10 shared primitive; peak memory per in-flight file bounded | ✓ VERIFIED (closed round 1, unaffected by round 3) | `pass_union`/`packet_budget` tests unaffected by round 3's file set. |
| 5 | Encode-twice-and-compare comes back clean as a CI release blocker (TRUST-06); every check has a triggering+clean fixture pair (DOC-03); `ts_scan` cross-checked against TSDuck (TRUST-09) | ✓ VERIFIED — independently re-confirmed on real CI run 34023871831 | See "Independent Re-Verification of SC5" below. All three blocking legs (x64-linux, arm64-osx, x64-windows-static-md) plus the required lint job conclude `success` at both job and step level; four `trust06_idempotence` `Passed` lines observed directly in two named legs' own logs (**and, additionally, on the Windows leg itself** — stronger evidence than either plan's own must-haves required); `x64-windows-static-md`'s `Test` step is observed concluding `success` for the first time in this phase's CI history; DOC-03's coverage gate and TRUST-09's three TSDuck-derived goldens observed `Passed` on the designated leg. |

**Score:** 5/5 roadmap success criteria verified.

### Independent Re-Verification of SC5

This verifier re-ran the queries against GitHub's API directly rather than trusting
03-21-SUMMARY.md / 03-22-SUMMARY.md's quoted output, per this round's explicit instruction (two
prior rounds overstated CI cleanliness in exactly this spot).

**Run identity** (`gh run view 34023871831 --json status,conclusion,headSha,headBranch`):
`status=completed`, `conclusion=success`, `headSha=64bc168684fac34e649f16a18f6a4c736ec192e4`,
`headBranch=gsd/phase-03-probe-layer-container-size`. Matches the orchestrator's cited run exactly.

**All six jobs, independently queried** (`gh run view --json jobs`):

| Job | Conclusion | Build step | Test step |
|---|---|---|---|
| lint (ENG-16 boundary) | success | — | — |
| build (x64-linux) | success | success | success |
| build (arm64-osx) | success | success | success |
| build (x64-windows-static-md) | success | success | success |
| build (x64-osx) — non-blocking | failure | failure | skipped |
| build (arm64-linux) — non-blocking | failure | skipped (fails earlier) | skipped |

Matches the orchestrator's table verbatim. All three designated-blocking legs conclude `Build`
and `Test` = `success` at the step level, not merely at the job level.

**Four `trust06_idempotence` result lines, pulled directly from each job's own `--log` output** (not
inferred from absence in a failure list):
- `build (x64-linux)`: `617/623 ... Passed 0.01 sec` and `618/623 ... Passed 0.01 sec`
- `build (arm64-osx)`: `612/618 ... Passed 0.03 sec` and `613/618 ... Passed 0.03 sec`
- `build (x64-windows-static-md)`: `612/618 ... Passed 0.03 sec` and `613/618 ... Passed 0.03 sec` (this exceeds what either plan's must-haves required — the Windows leg was only required to *conclude* its Test step, and it additionally ran and passed the same idempotence pair)

**DOC-03 / TRUST-09 gates on the designated leg** (`build (x64-linux)`, pulled from its own log):
`unit.ts_scan_golden` — `ts_204.ts`, `ts_multiprogram.ts`, `ts_single.ts` all `Passed`;
`integration.doc03_coverage` — both cases (`dir-mode-only checks...` and `every registered check
has a declared triggering fixture pair and a declared clean one`) `Passed`.

**Per-blocking-leg corpus verification**: all three blocking legs print
`check_corpus.sh: clean. Verified 80 fixture(s) present and non-empty` in their own logs.

**Overall test summary per blocking leg**: `x64-linux`: `100% tests passed, 0 tests failed out of
623`. `arm64-osx`: `100% tests passed out of 618`. `x64-windows-static-md`: `100% tests passed, 0
tests failed out of 618`.

**Non-blocking-leg failure classification, independently confirmed as legitimate (not a convenient
exclusion):**
- `build (arm64-linux)` fails at the `Register vcpkg NuGet feed (read-write, trusted runs only)`
  step — an infra/credentials step that runs before `Configure`/`Build`/`Test`, all of which are
  `skipped`. Matches WINDOWS.md #11 exactly.
- `build (x64-osx)` fails at `Build` with `ld: symbol(s) not found for architecture arm64` against
  `libmediadiff_core.a`'s FFmpeg symbols — the documented cross-architecture-build failure class
  (x64 cross-built from an arm64 host). Matches WINDOWS.md #14 exactly.
- Both legs are declared `blocking: false` in `.github/workflows/ci.yml` with
  `continue-on-error: ${{ !matrix.blocking }}` at the job level — this is a structural property of
  the workflow file itself (not a branch-protection-rule assumption), so neither leg's failure can
  influence the required-check aggregate in either direction. `git diff --stat 1b684de..HEAD --
  .github/workflows/ci.yml CMakeLists.txt` is empty across the entire round-3 span — no leg was
  quietly made non-blocking and no exclusion/warning-flag was widened to manufacture this result.

**Conclusion: SC5 is met.** The encode-twice comparison runs and comes back clean on all three
blocking legs (plus additionally on the Windows leg, beyond what was strictly required), DOC-03's
coverage gate and TRUST-09's TSDuck-derived goldens pass on the designated leg, and the release
blocker is genuinely wired into a CI matrix whose three required legs are green on real,
independently-queried evidence — not inference, not a partial matrix, not absence-from-a-failure-list.

### Required Artifacts (round-3 files, verified against the current working tree)

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `tests/unit/test_ebml_scan.cpp` | `kClusterId` referenced by a real test, not a silencing no-op | ✓ VERIFIED | Line 89 declares the constant; line 248 uses it in a new Cluster-inside-Segment test asserting `first_cluster_offset` by value, not merely presence. `grep -c kClusterId` = 2. |
| `src/cli/main.cpp` | Every `report_cli_error` call site namespace-qualified | ✓ VERIFIED | All three call sites (lines 157, 176, 297) read `mediadiff::report_cli_error(...)`. |
| `scripts/lint_bash4_builtins.sh` | Widened matchers close the split-flag/multi-option bypasses; self-test covers all 6 checks | ✓ VERIFIED (code review confirms by direct execution) | 03-REVIEW.md independently re-ran the extracted AWK matcher against `declare -r -A arr` and `shopt -s dotglob globstar` — both now flagged, with no new false positives. |
| `scripts/install_pinned_ffmpeg.sh` | tar.xz member-path traversal guard | ⚠️ PARTIALLY VERIFIED — see CR-01 below | The direct `../escape.txt` traversal case is refused (confirmed). A **relative-target symlink** bypass remains unaddressed and the header comment's "refused outright" claim is inaccurate for that case (03-REVIEW.md CR-01, reproduced by the reviewer). Dead code today — every `ffmpeg_pin.json` entry uses `"archive": "zip"` — so not currently reachable. |
| `.planning/WINDOWS.md` | Entries #13/#16 closed on cited observed evidence; every newly-exposed defect recorded | ✓ VERIFIED | `windows status`: total 22, 11 fixed / 11 open, markdown table and JSON array agree exactly (independently re-run). #13, #16, #18, #19, #20, #21 all read `fixed` with a `resolved_at`; #11, #14, #17, #22 read `open` with a recorded reason. |
| `.planning/REQUIREMENTS.md` | TRUST-06 status agrees at both carrying sites, matching observed evidence | ✓ VERIFIED | Line 173 (`- [x] **TRUST-06**...`) and line 372 (`| TRUST-06 | Phase 3 | Complete |`) agree; this verifier's own independent CI re-query confirms the underlying claim is now true, not merely asserted. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| Two one-line source fixes (main.cpp:297, test_ebml_scan.cpp:89) | arm64-osx / x64-windows-static-md Build step conclusions | one push, per-leg step conclusions read individually | ✓ WIRED, PROVEN AT RUNTIME | Confirmed via independent `gh run view --json jobs` query against the closing run. |
| `tests/golden/CORPUS_DIGEST.txt` (regenerated `size_near_b.mp4` entry) | `build (arm64-osx)`'s `doc03_coverage`/digest-assert steps | fixture-margin widening + designated-leg digest regeneration from real x64-linux CI output | ✓ WIRED, PROVEN AT RUNTIME | `build (arm64-osx)`: `100% tests passed out of 618` in the closing run; `build (x64-linux)`'s digest-assert step also passed in the same run. |
| `.planning/WINDOWS.md` (dual representation) | `windows status` / `/gsd-ship`'s enforcement gate | markdown table + fenced JSON array kept in sync via the `windows` subcommands | ✓ WIRED | Independently re-verified: `windows status` total (22) matches both representations exactly; per-entry disposition (fixed vs open) matches between the two representations for every one of #11/#13/#14/#16/#17/#20/#21/#22 checked. |
| `.planning/REQUIREMENTS.md` TRUST-06 | Observed CI evidence | evidence-not-edit reconciliation (03-22 Task 2) | ✓ WIRED | Confirmed both sites already read `Complete`/checked and the underlying claim is now independently verified true, closing the discrepancy the prior verification round flagged. |

### Behavioral Spot-Checks (independently re-run against real CI, not re-quoting any SUMMARY)

| Behavior | Command | Result | Status |
|---|---|---|---|
| Closing run's per-job conclusions | `gh run view 34023871831 --json jobs` | 4 required jobs `success`, 2 non-blocking `failure` | ✓ CONFIRMS orchestrator's table exactly |
| Per-step Build/Test conclusions for all 5 build legs | `gh run view --json jobs --jq '.jobs[]|{name,steps:[...]}'` | 3 blocking legs Build=success/Test=success; x64-osx Build=failure/Test=skipped; arm64-linux Build=skipped/Test=skipped | ✓ CONFIRMS step-level (not just job-level) success on all 3 blocking legs |
| `trust06_idempotence` Passed lines, x64-linux | `gh run view --log --job <id> \| grep -i trust06_idempotence` | 2 `Passed` lines (#617, #618) | ✓ CONFIRMS |
| `trust06_idempotence` Passed lines, arm64-osx | same, different job id | 2 `Passed` lines (#612, #613) | ✓ CONFIRMS |
| `trust06_idempotence` Passed lines, x64-windows-static-md | same, different job id | 2 `Passed` lines (#612, #613) — exceeds requirement | ✓ CONFIRMS (bonus evidence) |
| DOC-03/TRUST-09 gate lines, x64-linux | `grep -iE 'doc03_coverage|ts_scan_golden'` on that job's log | 3 `ts_scan_golden` + 2 `doc03_coverage` Passed lines | ✓ CONFIRMS |
| Per-leg corpus verification | `grep -i 'check_corpus.sh: clean'` on each blocking leg's log | 3/3 present | ✓ CONFIRMS |
| Non-blocking-leg failure root causes | `--log-failed` on `x64-osx`/`arm64-linux` jobs | link error (arch mismatch) / NuGet feed registration | ✓ CONFIRMS legitimate pre-existing classification, not a convenient exclusion |
| Workflow/CMake gate integrity across the whole round-3 span | `git diff --stat 1b684de..HEAD -- .github/workflows/ci.yml CMakeLists.txt` | empty | ✓ CONFIRMS no leg was quietly narrowed to manufacture green |
| Local test count matches CI's built tree | `ctest --test-dir build/x64-linux -N` | `Total Tests: 623` | ✓ CONFIRMS working tree matches what CI built |
| `windows status` ledger integrity | `node gsd-tools.cjs windows status` | total 22; 11 fixed / 11 open; table and JSON agree | ✓ CONFIRMS |
| Code-review CR-01 (relative-symlink tar.xz bypass) still unfixed | `sed -n '217,236p' scripts/install_pinned_ffmpeg.sh` | only `os.path.isabs(linkname)` checked; no relative-target resolution | ✓ CONFIRMS review's finding is still present verbatim; no fix commit exists after `be355f1` |
| WINDOWS.md #22 (x64-linux corpus non-reproducibility) still open, unledgered CR-01 | `grep -n 'CR-01\|symlink' .planning/WINDOWS.md` | no match | ✓ CONFIRMS CR-01 has not yet been added to the ledger |

### Probe Execution

No `scripts/*/tests/probe-*.sh`-style probes declared by this phase; N/A — skipped.

### Requirements Coverage

All 23 requirement IDs (`PROBE-01/02/04/05/06/07/08/09/10, CONT-01…09, SIZE-01, DIR-06,
TRUST-06/09, DOC-03`) are present in `.planning/REQUIREMENTS.md`'s Phase-3 mapping, all marked
`Complete`, and cross-referenced against `.planning/ROADMAP.md`'s Phase-3 requirement list —
identical 23-id set, no orphans, no omissions. Checklist-site checkboxes (`- [x] **ID**: ...`)
spot-checked for PROBE-05, TRUST-06, TRUST-09, DOC-03 all agree with their table-row status.

| Requirement | Status | Evidence |
|---|---|---|
| PROBE-01, 02, 04, 06, 07, 08 | ✓ SATISFIED (unchanged) | Untouched by round 3. |
| PROBE-05 | ✓ SATISFIED, freshly strengthened | This round adds a real behavioral test for the unknown-size-Cluster-inside-Segment edge (previously documented in `ebml_scan.h`'s header comment but never tested), asserting `first_cluster_offset` by value. |
| PROBE-09, PROBE-10 | ✓ SATISFIED (unchanged) | Untouched by round 3. |
| CONT-01…09 | ✓ SATISFIED (unchanged) | Untouched by round 3. |
| SIZE-01, DIR-06 | ✓ SATISFIED (unchanged) | The round-3 fixture-margin fix (WINDOWS.md #20) was a calibration correction, not a change to `size.*`'s computation. |
| TRUST-06 | ✓ SATISFIED, now genuinely supported by observed evidence | Independently re-confirmed this round via direct `gh` queries against run 34023871831: the release blocker is wired into CI and comes back green on all three required legs. Previously `Complete` was asserted but unsupported by CI evidence (round 2's own finding); this round's evidence closes that gap. |
| TRUST-09 | ✓ SATISFIED | `ts_scan_golden`'s three TSDuck-derived cases independently confirmed `Passed` on the designated leg in the closing run. |
| DOC-03 | ✓ SATISFIED | `doc03_coverage`'s two cases independently confirmed `Passed` on the designated leg in the closing run. |

No ORPHANED requirements found.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| `scripts/install_pinned_ffmpeg.sh` | 217-236 | tar.xz extraction guard checks only absolute symlink targets; a relative-target symlink that resolves outside the destination directory is not checked, and the header comment's "refused outright" claim is inaccurate for that case (03-REVIEW.md CR-01, reproduced by the reviewer with a working exploit) | ⚠️ Warning (not a phase blocker — see rationale below) | Dead code today: every entry in `scripts/ffmpeg_pin.json` uses `"archive": "zip"` (confirmed: `grep -c '"archive": "zip"' scripts/ffmpeg_pin.json` = 4), and reaching the tar.xz branch also requires a URL+SHA-256 pin entry an attacker already controls. Not yet recorded in `.planning/WINDOWS.md` as of this verification — this is a process gap: a critical-severity code review finding that should be ledgered even while deferred. **Recommend adding a WINDOWS.md entry before `/gsd-ship`**, since the ledger's own stated purpose is to make exactly this kind of accepted-but-unresolved finding visible to the next round. |
| `scripts/gen_corpus.sh` | 483-491 | x64-linux's own libopus-encoded fixture generation (`mkv_opus_a.webm`/`mkv_opus_b.webm`) observed non-reproducible run-to-run on the identical commit and pinned ffmpeg binary (WINDOWS.md #22, open) | ⚠️ Warning (does not currently fail any SC — see rationale below) | This threatens the evidentiary basis of the designated-leg byte-exact golden policy (WINDOWS.md #17) in principle, but did not recur in either of this round's two subsequent CI runs, and none of the 5 byte-exact golden tests failed in the closing run 34023871831 (`x64-linux`: `100% tests passed, 0 tests failed out of 623`). Honestly recorded as open with quoted hash evidence from both the passing and failing prior runs; correctly not folded into a claimed-closed SC5. |
| `src/cli/options.cpp` | 309-361 | `resolve_probe_timeout_ms`/`resolve_probe_memory_budget_mb` re-parse CLI11-already-validated text with `std::stoll` | ℹ️ Info | Pre-existing, carried forward verbatim from round-1 review (IN-01); untouched by round 3. |
| `scripts/lint_bash4_builtins.sh` | various | Line-based comment-stripping still false-positives on a quoted string literal containing a flagged construct on a live code line (IN-02, reconfirmed open by 03-REVIEW.md) | ℹ️ Info | No file under `scripts/` currently triggers it; the `# bash4-allow` escape valve is the sanctioned remedy if one ever does. |
| `scripts/install_pinned_ffmpeg.sh`, `scripts/corpus_digest.sh` | various | `compute_sha256` duplicated verbatim between the two scripts (IN-04) | ℹ️ Info | Deliberate, documented in both scripts' own header comments; a future-drift risk, not fixed this round by design (the ffmpeg-install path is executed by all 5 CI legs, so a shared-helper refactor was judged too risky mid-round). |
| `tests/unit/CMakeLists.txt` | 191-194 | Comment justifying the new `provenance_render.cpp` link overstates which test exercises the `verbose=true` rendering path — no unit test currently calls `render_inspect_text(..., true)` (03-REVIEW.md IN-05) | ℹ️ Info | Doesn't invalidate the link fix itself (the symbol needs linking regardless of runtime branch-folding); purely a documentation-accuracy note. |
| `src/cli/main.cpp` | 157, 176 | Two of the three `mediadiff::` qualifications added this round are functionally inert (already inside `namespace mediadiff`, never actually broken) (03-REVIEW.md IN-06) | ℹ️ Info | Harmless; the round's own SUMMARY already correctly attributes only the `wmain` call site (line 297) as the actual MSVC fix. |

**Debt-marker gate:** No unreferenced `TBD`/`FIXME`/`XXX` found in any file touched by round 3's
two gap-closure plans (`git diff --stat 40db636..HEAD` file set checked directly) — clean.

### Human Verification Required

None. All findings above — including the two open, non-blocking WINDOWS.md entries (#17, #22)
and the unledgered code-review finding (CR-01) — were resolved programmatically: by independently
re-querying the real CI run and its job/step-level conclusions via `gh run view`, by reading the
exact source lines the review and the ledger cite in the current working tree, and by confirming
no workflow/CMake gate was altered across the entire round-3 span.

### Gaps Summary

**SC5 is now met, independently re-confirmed.** Gap-closure round 3 (plans 03-21, 03-22) closed
the two one-line source defects (`tests/unit/test_ebml_scan.cpp:89`, `src/cli/main.cpp:297`) that
blocked two of the three required CI legs, discovered and fixed four further defects along the
way (a missing test-target link, a Windows-macro identifier collision, a fixture-margin
calibration issue, and a GITHUB_PATH format bug), and produced one real CI run
(34023871831, head `64bc168`) in which all three blocking legs and the required lint job conclude
`Build`=`success`/`Test`=`success` at the step level — independently re-verified against GitHub's
API directly by this verifier, not accepted from either plan's SUMMARY narration. This is the
first time in this phase's entire CI history (across at least 9 real runs referenced across
rounds 1-3) that this has been observed. All 23 Phase-3 requirement IDs are satisfied, and
REQUIREMENTS.md's TRUST-06 status — previously flagged by the prior verification round as
asserting more than its evidence supported — is now genuinely backed by observed evidence.

**Two items are worth carrying forward as recorded, non-blocking follow-ups, not as gaps against
this phase's goal:**

1. **03-REVIEW.md's CR-01** (a critical-severity finding: the tar.xz path-traversal guard added
   this round only checks absolute symlink targets, leaving a relative-target symlink escape
   unaddressed, and its header comment overstates the guarantee as "refused outright"). This does
   not block the phase goal: the branch is dead code today (every `ffmpeg_pin.json` entry is
   `"archive": "zip"`, confirmed), reaching it requires an attacker who already controls a pinned
   URL+SHA-256 entry, and no Phase-3 requirement or ROADMAP success criterion depends on this
   script's extraction hardening being complete — it was itself a code-review-driven hardening
   task (WR-02) layered onto D-GAP-01's supply-chain-integrity work, not a named acceptance
   criterion. It IS a process gap worth naming plainly: a critical review finding that has not yet
   been recorded in `.planning/WINDOWS.md`, unlike every other defect this phase has surfaced.
   **Recommend a WINDOWS.md entry and a follow-up fix (the reviewer's own drafted patch) before
   this branch is ever made live** (i.e., before any `ffmpeg_pin.json` entry uses `"archive":
   "tar.xz"`).
2. **WINDOWS.md #22** (x64-linux's own libopus-fixture generation observed non-reproducible once,
   run-to-run, on the identical commit and pinned ffmpeg binary). This does not currently fail any
   ROADMAP success criterion or requirement — it did not recur in either of this round's two
   later CI runs, and the closing run's designated-leg digest-assert step and all 5 byte-exact
   golden tests passed cleanly. It IS a legitimate, honestly-recorded reliability risk to the
   designated-leg byte-exact policy's own assumption (WINDOWS.md #17) that x64-linux is internally
   deterministic — a single non-recurrence is evidence against that assumption, not proof the
   policy is safe long-term. Correctly left open rather than silently assumed resolved; no action
   required before this phase can be considered complete, but a future ffmpeg-pin bump or CI
   flakiness investigation should treat this as open, not closed.

Neither item changes the phase's goal-achievement verdict: SC5's own precise, narrow claim —
"comes back clean as a CI release blocker" — is true on the evidence this verifier independently
gathered, and the surrounding infrastructure (ffmpeg pinning, corpus generation/digest, bash-3.2
portability, MSVC/AppleClang build parity) that three consecutive gap-closure rounds progressively
hardened is now proven working end-to-end on a real, current CI run.

---

_Verified: 2026-09-06T12:00:00Z_
_Verifier: Claude (gsd-verifier)_
