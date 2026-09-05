---
phase: 03-probe-layer-container-size
verified: 2026-09-05T07:29:36Z
status: gaps_found
score: 4/5 roadmap success criteria fully verified; 1/5 (SC5) still failed under real CI evidence, for a different (partially new) root cause than the prior verification round
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "1/5 fully clean; 4/5 gap-bearing"
  gaps_closed:
    - "Gap 1 (SC2, CR-01/CR-02): container.mp4.fragment_duration's raw int64 DTS-delta subtraction and non-strict-weak-order sort comparator — reachable UB from crafted input. Closed by 03-13: detail::compute_median_fragment_duration routes every delta through detail::checked_sub and orders by raw same-timebase tick value (a total order that cannot overflow). Verified: 11 new unit tests exercising extreme-DTS inputs pass; independently confirmed via direct code read (grep -c 'checked_sub' + absence of 'compare_ticks_checked' + absence of the raw subtraction, all as claimed); fragment_duration's value/evidence on mp4_fragmented.mp4 confirmed byte-identical pre/post fix per 03-13-SUMMARY.md."
    - "Gap 2 (SC3/SC4/DIR-06, CR-04): unchecked megabytes-to-bytes and seconds-to-milliseconds multiplication on --probe-memory-budget-mb/--probe-timeout (and their [probe] TOML counterparts), overflowing to a negative budget that silently blanked every size.* check at exit 0, or produced a spurious immediate timeout at exit 65. Closed by 03-12: resolve_probe_memory_budget_bytes + a hardened resolve_probe_timeout_ms, both routed through detail::checked_mul with named-maximum usage errors (exit 64), applied at all FOUR command entry points including a 5th, previously-unlisted defect site in snapshot.cpp. Independently reproduced this verification round: both of 03-VERIFICATION.md's original repro commands now exit 64 with a bound-naming diagnostic (CLI11's own Range check fires first); the accepted-maximum budget still produces a real size.stream_bitrate finding (0 partial_scan hits)."
  gaps_remaining:
    - "Gap 3 (SC5, TRUST-06 CI wiring) is NOT closed, though its originally-diagnosed mechanism is fixed. 03-14 wired scripts/gen_corpus.sh + a new scripts/check_corpus.sh preflight into all 5 CI legs before Configure, and a real CI run (PR #3, run 33951407521, independently confirmed via `gh run view` to have concluded failure) proves the corpus now generates successfully on every leg. But that same real run exposed two blocking-leg failures unrelated to the corpus that leave SC5's 'comes back clean as a CI release blocker' still false: x64-windows-static-md fails to even BUILD (src/probe/ebml_scan.cpp:348, a NOMINMAX/std::max macro clash — confirmed via direct code read that NOMINMAX is defined nowhere in this project's own sources), and x64-linux fails 5/620 tests (unit.inspect_container, ts_scan_golden x3, integration.size_checks) because the committed byte-level goldens were captured against a different local ffmpeg build than the ffmpeg 9.0.1 CI actually installs. Both are tracked as WINDOWS.md #9/#10, both open, both outside 03-14's declared files_modified by the plan's own explicit scoping. TRUST-06 itself has still never been confirmed to execute and pass on any real CI leg — it was not among the 5 x64-linux failures, but this was not independently re-confirmed against the run log by 03-14's own SUMMARY, and the Windows leg never reaches the Test step at all."
  regressions: []
gaps:
  - truth: "Encoding a fixture twice with identical settings and comparing under sw-encoder comes back clean as a CI release blocker (ROADMAP SC5)."
    status: failed
    reason: >
      tests/integration/test_trust06_idempotence.cpp is real and passes locally (2/2, confirmed this
      round via `ctest -R trust06_idempotence`). The specific defect the prior VERIFICATION.md
      diagnosed — scripts/gen_corpus.sh invoked on no CI leg — is genuinely fixed: 03-14 added an
      unconditional install-ffmpeg / generate-corpus / verify-corpus step group before Configure on
      all five matrix legs, and this was proven on a real, non-simulated CI run (PR #3, run
      33951407521; independently re-confirmed this round via `gh run view 33951407521` returning
      conclusion=failure, i.e. the run is real and its failure is real, not fabricated). That run
      showed the corpus itself generated and verified cleanly on every leg it reached.
      However, the same real run shows the CI matrix is NOT green on 2 of its 3 blocking legs, for
      reasons that directly undermine "comes back clean as a CI release blocker":
        (a) x64-windows-static-md fails at the Build step, before Test ever runs: `error C2059:
            syntax error: ')'` on `std::max(1.0, std::abs(value))` at src/probe/ebml_scan.cpp:348,
            because windows.h's `max` macro clobbers `std::max` (NOMINMAX is undefined anywhere in
            this project — confirmed this round via `grep -rn NOMINMAX` over every project source and
            CMake file, finding only third-party vcpkg port definitions that do not apply to this
            project's own compilation units). This means TRUST-06 — and every other Phase-3 check —
            has literally never been proven to build on MSVC, the toolchain PROJECT.md names as a
            hard parity requirement.
        (b) x64-linux fails 5 of 620 tests — unit.inspect_container, ts_scan_golden (ts_204,
            ts_multiprogram, ts_single), integration.size_checks — because the committed byte-level
            goldens were generated against a local ffmpeg master snapshot
            (N-126086-ge5ecfe8970-20260812) while CI's own install step provisions ffmpeg 9.0.1;
            different muxer output invalidates byte-identical goldens across builds. This is the
            exact CI leg CI treats as blocking, and it is currently red.
      Neither defect is a corpus-generation problem — both are new-to-CI evidence the corpus fix
      itself surfaced, tracked as WINDOWS.md #9 (open) and #10 (open) — but their net effect is that
      SC5's own wording ("comes back clean as a CI release blocker") remains unmet: a release blocker
      that cannot build on one required leg and fails 5 tests on another required leg is not, in
      fact, functioning as a clean release blocker yet. 03-14-SUMMARY.md itself states this
      explicitly and does not claim otherwise ("Full five-leg green was NOT achieved").
    artifacts:
      - path: src/probe/ebml_scan.cpp
        issue: "Line 348: std::max(1.0, std::abs(value)) fails to compile under MSVC because NOMINMAX is undefined anywhere in the project, so windows.h's max macro clobbers std::max (WINDOWS.md #9, open). A Phase-3 file (03-06), unrelated to the gap-closure plans' own declared files_modified."
      - path: tests/fixtures/GENERATOR_MANIFEST.json
        issue: "Committed byte-level goldens (unit.inspect_container, ts_scan_golden, integration.size_checks) were captured against a different ffmpeg build than CI's installed ffmpeg 9.0.1, and fail on the real x64-linux CI leg (WINDOWS.md #10, open, an explicitly-unresolved design question: pin CI's ffmpeg vs. regenerate/version-tolerant goldens)."
    missing:
      - "Define NOMINMAX (project-wide compile definition, or scoped to ebml_scan.cpp / any TU including <io.h> or <windows.h>) so the MSVC leg's Build step succeeds — a small, well-understood fix."
      - "Decide and implement a golden-drift policy: pin CI's installed ffmpeg to the exact build the committed goldens were captured with, OR regenerate the goldens against ffmpeg 9.0.1 (the version CI actually installs and will keep installing), OR make the byte-level assertions tolerant of muxer-level differences across ffmpeg builds. This is a real open design question, not a one-line fix, and belongs to whichever gap-closure plan owns it next."
      - "Re-run CI after both fixes land and confirm all 3 blocking legs (x64-linux, arm64-osx, x64-windows-static-md) are green end-to-end, with test_trust06_idempotence explicitly observed to execute and pass in the run log on at least x64-linux and arm64-osx — this has still never been independently confirmed against a real run log, only inferred from '5 unrelated golden tests failed, trust06 was not named among them'."
deferred: []
---

# Phase 3: Probe Layer, Container & Size Verification Report

**Phase Goal:** Real media enters the engine — one header pass and one packet sweep feed every
container, metadata and size check, plus the shared primitives that later phases consume instead
of recomputing.
**Verified:** 2026-09-05T07:29:36Z
**Status:** gaps_found
**Re-verification:** Yes — after gap-closure plans 03-12 through 03-15

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---|---|---|
| 1 | `mediadiff inspect` on MP4/MOV, MKV/WebM, MPEG-TS renders a complete container section | ✓ VERIFIED (unchanged, no regression from gap-closure plans) | Re-confirmed cross-container `compare --json` output this round: `container.mp4.*`/`container.mkv.*` render/demote correctly; no gap-closure plan touched the rendering path. Local test suite (620/620) includes the container-family unit/integration tests, all green. |
| 2 | Cross-container migration demotes cleanly; truncated/garbage input degrades to `skipped:unparsed_mechanism` with byte offset or exits 65; never crashes or silently passes | ✓ VERIFIED — **gap 1 CLOSED this round** | Independently re-ran all three of the prior verification's reproductions this round: cross-container `compare` still shows every `container.mp4.*`/`container.mkv.*` finding `skipped` with `stop_offset`/candidate evidence intact; a 50-byte `/dev/urandom` file exits 65 cleanly; a 4096-byte-truncated MP4 degrades with `{"stop_offset":2684}` evidence at exit 1. CR-01/CR-02 (the reachable UB in `container.mp4.fragment_duration`'s median, previously unresolved) are fixed by 03-13: `detail::compute_median_fragment_duration` routes every inter-keyframe delta through `detail::checked_sub` and orders by a same-timebase raw tick value (a total order that cannot overflow), removing the non-strict-weak-order `compare_ticks_checked`-based comparator entirely. Confirmed via direct code read (`grep -n checked_sub\|compare_ticks_checked src/analyzers/container/mp4.cpp` shows the checked call present, the old raw subtraction and the overflow-folding comparator both absent) and via `ctest -R mp4_fragment_duration` (11/11 pass, including the two named CR-01/CR-02 reproduction cases). |
| 3 | `size.file`/`size.stream_bitrate`/`size.peak_bitrate`/`size.overhead` report rate economics from the packet scan alone, DTS-in-ticks windowing, cross-platform-identical | ✓ VERIFIED — **gap 2 CLOSED this round** | Independently re-ran both of the prior verification's CR-04 reproductions this round: `mediadiff compare --probe-memory-budget-mb 8796093022208 ...` now exits **64** ("Value 8796093022208 not in range [1 - 1048576]"), never 0, and produces zero `partial_scan` hits; `mediadiff compare --probe-timeout 9223372036854776 ...` now exits **64** ("Value ... not in range [0 - 86400]"), never the previous spurious wall-clock 65. `grep -rn '1024 \* 1024' src/cli/` reports 0; `grep -c checked_mul src/cli/options.cpp` reports 5; all four command entry points (`compare`, `dir`, `inspect`, `snapshot`) route through `resolve_probe_memory_budget_bytes` (confirmed via grep, 2 call sites each). `ctest -R probe_budget_overflow` (7/7 pass, including the accepted-maximum-still-reports-real-findings case). |
| 4 | Each file read exactly once; PROBE-10 packet-interval statistics shared as one probe-level primitive; peak memory per in-flight file bounded and asserted so `--threads N` is an honest memory knob | ✓ VERIFIED — **downstream of gap 2's closure** | `ctest -R pass_union` (8/8) and `ctest -R packet_budget` (3/3) unaffected by the gap-closure plans and still pass. The CR-04 overflow that previously corrupted the D-01 budget the "asserted... honest memory knob" claim rests on is fixed identically to SC3 above — same resolver, same fix. |
| 5 | Encode-twice-and-compare comes back clean as a CI release blocker (TRUST-06); every check has both a triggering and a clean fixture pair (DOC-03); `ts_scan` cross-checked against TSDuck (TRUST-09) | ✗ FAILED (partial — root cause changed, SC still unmet) | `ctest -R trust06_idempotence` (2/2), `ctest -R doc03` (2/2, 30/30 checks incl. all 27 Phase-3 ids), and `ctest -R ts_scan_golden` (3/3) all pass **locally**. The specific CI-wiring defect the prior round found (`scripts/gen_corpus.sh` invoked on no leg) is genuinely fixed by 03-14 and proven on a real, non-simulated CI run (PR #3, run 33951407521 — independently re-confirmed this round via `gh run view 33951407521`, which returns `conclusion: failure`, corroborating rather than contradicting the run-context evidence). But that same real run shows the CI matrix is **not green**: x64-windows-static-md fails to build at all (`ebml_scan.cpp:348`'s NOMINMAX/`std::max` clash, independently confirmed this round — `grep -rn NOMINMAX` over every project source/CMake file finds the symbol only inside third-party vcpkg ports, never defined for this project's own targets); x64-linux fails 5/620 tests from ffmpeg-version-drifted committed goldens. A release blocker that cannot build on one required leg and fails on another is not yet "wired into CI as a release blocker" in the sense SC5 asserts, regardless of how solid the underlying test and the corpus-generation fix both are. |

**Score:** 4/5 roadmap success criteria fully verified this round (SC1–SC4, up from 1/5 last round); SC5 remains unmet, though the specific defect the prior round attributed to it (corpus never generated in CI) is now fixed and two new, real, currently-open CI defects (WINDOWS.md #9, #10) — both outside the declared scope of the plan that surfaced them — are what keep SC5 false.

### Gap-Closure Verification (Prior Round's 3 Gaps)

| Gap | Prior Status | This Round | Evidence |
|---|---|---|---|
| Gap 1 — CR-01/CR-02 (mp4.cpp reachable UB, SC2) | `failed` | ✓ **CLOSED** | 03-13; `checked_sub`+total-order tick comparator; 11 new unit tests; value/evidence byte-identical on existing fixtures (per 03-13-SUMMARY.md, independently spot-checked this round via source read). |
| Gap 2 — CR-04 (probe-budget/timeout overflow, SC3/SC4/DIR-06) | `failed` | ✓ **CLOSED** | 03-12; bounded `CLI::Range` + `detail::checked_mul` resolvers at all 4 (in fact 5, incl. `snapshot.cpp`) entry points; both original reproductions independently re-run this round, now exit 64 with a named bound. |
| Gap 3 — CI wiring (`gen_corpus.sh` never invoked, SC5) | `failed` | ⚠️ **PARTIALLY CLOSED** — mechanism fixed, success criterion still unmet | 03-14; corpus generation/verification now unconditional on all 5 legs, proven on a real CI run. But that run exposed 2 new blocking-leg failures (WINDOWS.md #9 Windows build breakage, #10 Linux golden drift) that leave "comes back clean as a CI release blocker" false. Treated as a continuing gap under the same SC5, not a new independent gap, since the observable truth ("CI is green with TRUST-06 as a release blocker") is unchanged in its falsity even though its proximate cause has moved. |

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/analyzers/container/analyzers.h`, `mp4.cpp` | Checked, totally-ordered `container.mp4.fragment_duration` median (CONT-05) | ✓ VERIFIED | `detail::compute_median_fragment_duration` present, wired into `emit_fragment_duration`; 11/11 new tests pass; no `compare_ticks_checked` or raw DTS subtraction remains in the file. |
| `src/cli/options.{h,cpp}`, `src/config/toml_load.{h,cpp}`, all 4 command entry points | Bounded, checked probe budget/timeout resolvers (SIZE-01, DIR-06) | ✓ VERIFIED | `resolve_probe_memory_budget_bytes` new; `kMaxProbeMemoryBudgetMb`/`kMaxProbeTimeoutSeconds` bound both the CLI (`CLI::Range`) and the TOML loader before narrowing; 0 raw `1024 * 1024` products remain in `src/cli/`. |
| `scripts/check_corpus.sh`, `.github/workflows/ci.yml` | Corpus generated + verified, unconditionally, on all 5 CI legs before Configure (TRUST-06) | ⚠️ WIRED BUT MATRIX NOT GREEN | `grep -c gen_corpus.sh .github/workflows/ci.yml` ≥ 1 (confirmed); real CI run proves the corpus steps execute correctly in order on all 5 legs. But 2 of 3 blocking legs fail downstream for unrelated reasons (WINDOWS.md #9, #10), so the artifact this gap needed exists and works, while the end-to-end guarantee it exists to provide (a green release-blocking CI) does not yet hold. |
| `src/report/junit.cpp`'s `xml_escape`, `src/cli/diagnostics.{h,cpp}` | T-2-33 completion (control-byte escaping in JUnit + one CLI diagnostic sink) | ⚠️ SUBSTANTIALLY COMPLETE, ONE WARNING OPEN | 03-15 closed CR-03 (C0/DEL escaped as `\xHH`) and WR-01/IN-02 (one sink, 44 sites migrated, lint enforces it). This round's own 03-REVIEW.md (0 blockers, 1 warning) found `xml_escape`'s new `\xHH` escaping omits the backslash-doubling `sanitize_for_display` uses for the identical byte class — confirmed present in code this round (`src/report/junit.cpp`'s `default:` arm has no `case '\\':`). A real ESC byte and the literal 4 ASCII characters `\x1b` render identically in a JUnit report. Not an XML-injection risk (the 4 metacharacters are still escaped) and not gating any roadmap SC — CONT-03/CONT-04's actual `meta.tags` comparison behavior is unaffected — but it does mean plan 03-15's own claim that "T-2-33 is genuinely closed across every output format" is not, in fact, fully true yet. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `--probe-memory-budget-mb`/`--probe-timeout` (CLI + `[probe]` TOML) | `derive_per_file_cap_bytes` → every `size.*` skip decision | `resolve_probe_memory_budget_bytes` (new single resolver) | ✓ WIRED, CHECKED | Confirmed: CLI11's own `Range` check fires first for the reproduced extreme values (exit 64 at parse time, before the resolver even runs); the resolver's own bound is genuine defense-in-depth for the `--config` path, which has no CLI11 validator in front of it. |
| `AVPacket::dts` (file-controlled) → `keyframe_dts` | `container.mp4.fragment_duration`'s median | `detail::checked_sub` + same-timebase raw-tick ordering | ✓ WIRED, CHECKED | No raw subtraction or overflow-folding comparator remains; extreme-DTS behavior is now specified and tested rather than merely "not yet observed to crash." |
| `scripts/gen_corpus.sh` → `tests/fixtures/*` → `ctest` | The `Test` step's exit code → the required status check | 3-step unconditional CI group before `Configure` | ⚠️ WIRED, BUT DOWNSTREAM RED | The link itself (corpus generation feeding the test suite) is proven functional on real CI. The chain still ends red on 2 of 3 blocking legs for reasons the link itself does not cause. |

### Behavioral Spot-Checks (this round, independently re-run — not merely re-quoting SUMMARYs)

| Behavior | Command | Result | Status |
|---|---|---|---|
| CR-04 repro #1 (memory budget overflow) now refused | `mediadiff compare --probe-memory-budget-mb 8796093022208 mp4_faststart.mp4 mp4_faststart.mp4 --json` | exit 64, `--probe-memory-budget-mb: Value 8796093022208 not in range [1 - 1048576]`; 0 `partial_scan` hits in output | ✓ PASS (was ✗ FAIL last round) |
| CR-04 repro #2 (timeout overflow) now refused | `mediadiff compare --probe-timeout 9223372036854776 mp4_faststart.mp4 mp4_faststart.mp4` | exit 64, `--probe-timeout: Value 9223372036854776 not in range [0 - 86400]` (previously exit 65 with a spurious wall-clock message) | ✓ PASS (was ✗ FAIL last round) |
| CR-01/CR-02 regression suite | `ctest --test-dir build/x64-linux -R mp4_fragment_duration` | 11/11 pass, incl. both named CR-01/CR-02 reproduction cases | ✓ PASS |
| CR-04 regression suite | `ctest --test-dir build/x64-linux -R probe_budget_overflow` | 7/7 pass | ✓ PASS |
| Cross-container demotion still clean | `mediadiff compare mp4_faststart.mp4 mkv_cues_front.mkv --json` | every `container.mp4.*`/`container.mkv.*` finding `skipped`, generic topology fields compare normally | ✓ PASS (no regression) |
| Garbage input still exits cleanly | `mediadiff compare <50B /dev/urandom> mp4_faststart.mp4 --json` | exit 65, clean message, no crash | ✓ PASS (no regression) |
| Truncated input still degrades with byte offset | `mediadiff compare <4096B-truncated mp4> mp4_faststart.mp4 --json` | `container.mp4.*` all `skipped` with `{"stop_offset":2684}` evidence, exit 1 | ✓ PASS (no regression) |
| No NOMINMAX defined anywhere in this project's own sources | `grep -rn NOMINMAX` over every `.cpp`/`.h`/`CMakeLists.txt`/`.cmake` outside `vcpkg/` | 0 matches in project code; only third-party vcpkg port definitions found (do not apply to this project's compilation units) | ✗ CONFIRMS WINDOWS.md #9 is real and unfixed |
| Full local suite (run once) | `ctest --preset x64-linux` | 620/620 pass, 1 platform-skip (`unit.console_vt`, needs a real Windows console) | ✓ PASS (matches orchestrator's claim) |
| Corpus completeness preflight | `bash scripts/check_corpus.sh` | "clean. Verified 80 fixture(s)" | ✓ PASS (matches orchestrator's claim) |
| Real CI run is genuinely real and genuinely failed | `gh run view 33951407521 --json status,conclusion` | `{"status":"completed","conclusion":"failure"}`; `gh pr view 3 --json state` → `OPEN` | ✓ CONFIRMS run-context evidence, independently, via a different tool (`gh` CLI, not a re-read of the SUMMARY) |

### Probe Execution

No `scripts/*/tests/probe-*.sh`-style probes declared by this phase; N/A — skipped.

### Requirements Coverage

All 23 requirement IDs declared across the 15 plans (`PROBE-01/02/04/05/06/07/08/09/10, CONT-01…09,
SIZE-01, DIR-06, TRUST-06/09, DOC-03`) are present in REQUIREMENTS.md's Phase-3 mapping and marked
`[x]`/Complete. Cross-referenced against `.planning/ROADMAP.md`'s own Phase-3 requirement list
(identical 23-id set) — no orphans, no omissions. The 4 gap-closure plans (03-12 SIZE-01/DIR-06,
03-13 CONT-05/PROBE-09, 03-14 TRUST-06/DOC-03, 03-15 CONT-03/CONT-04) all declare requirement ids
already covered by the phase's original 11 plans — no new requirement id is introduced or reduced.

| Requirement | Status | Evidence |
|---|---|---|
| PROBE-01, 02, 04, 05, 06, 07, 08 | ✓ SATISFIED (unchanged) | No gap-closure plan touched these subsystems; unaffected. |
| PROBE-09 | ✓ SATISFIED (strengthened) | 03-13 closed the CR-01/CR-02 UB that threatened "unparseable structure degrades cleanly, never a crash." |
| PROBE-10 | ✓ SATISFIED (unchanged, design-scope note stands) | `pass_union` tests unaffected by gap-closure plans. |
| CONT-01, 02, 06, 07, 08, 09 | ✓ SATISFIED (unchanged) | Unaffected by gap-closure plans. |
| CONT-03, CONT-04 | ✓ SATISFIED, ⚠️ one non-gating warning | `meta.tags` set-comparison behavior itself is unaffected by 03-15; 03-15's own T-2-33 completion work (a hardening layer on top, not part of CONT-03/04's core behavior) has one open Warning (junit.cpp backslash-doubling, see above). |
| CONT-05 | ✓ SATISFIED — CR-01/CR-02 caveat now RESOLVED | 03-13 closes the previously-open Critical findings. |
| SIZE-01, DIR-06 | ✓ SATISFIED — CR-04 caveat now RESOLVED | 03-12 closes the previously-open Critical finding. |
| TRUST-06 | ⚠️ TEST REAL, CI STILL NOT GREEN | Test passes locally and the corpus-generation defect that previously blocked it in CI is fixed; but the CI matrix is not green on 2 of 3 blocking legs for other reasons, so "wired into CI as a release blocker" remains unmet. |
| TRUST-09 | ✓ SATISFIED (unchanged) | TSDuck goldens pass locally; unaffected by gap-closure plans (the x64-linux golden-drift issue affects `ts_scan_golden` specifically in CI, but the mechanism — comparing against committed goldens without linking TSDuck — is intact). |
| DOC-03 | ✓ SATISFIED (unchanged locally), same CI caveat as TRUST-06/09 | 30/30 checks covered locally; the coverage gate itself is real and registry-driven. |

No ORPHANED requirements found.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| `src/probe/ebml_scan.cpp` | 348 | `std::max` call breaks under MSVC because `NOMINMAX` is undefined anywhere in the project | 🛑 Blocker (for SC5 / cross-platform build parity) | Confirmed via real CI evidence and independently re-confirmed this round via source grep. Phase-3 code has never been proven to compile on MSVC. |
| `tests/fixtures/GENERATOR_MANIFEST.json` (committed goldens) | — | Byte-level goldens captured against a non-CI ffmpeg build; drift across ffmpeg versions | 🛑 Blocker (for SC5's "clean CI release blocker") | 5/620 tests fail on the real, blocking x64-linux CI leg. An open design question, not yet a fix. |
| `src/report/junit.cpp` | 104-121 | `xml_escape`'s new C0/DEL `\xHH` escaping omits the backslash-doubling step `sanitize_for_display` uses for the identical ambiguity class | ⚠️ Warning | Confirmed present in code this round. Not an XML-injection risk; does not gate any roadmap SC; does mean 03-15's "T-2-33 genuinely closed" claim is not fully accurate yet. |
| `src/cli/options.cpp` | 309-361 | `resolve_probe_timeout_ms`/`resolve_probe_memory_budget_mb` re-parse CLI11-already-validated text with `std::stoll`, both `catch` branches documented as unreachable | ℹ️ Info | Pre-existing defense-in-depth duplication, not a defect; carried from 03-REVIEW.md's own IN-01. |

**Debt-marker gate:** No `TBD`/`FIXME`/`XXX` found in any file touched by the 4 gap-closure plans (checked this round via direct grep, not merely re-quoting the review) — clean.

### Human Verification Required

None. Every finding above — including the two CI-blocking defects — was resolved programmatically:
by direct code reads, by re-running the local test suite and the two exact prior-round reproduction
commands, and by independently querying the real CI run and PR state via `gh` (a different tool than
the one that originally reported them), rather than by re-reading SUMMARY.md's narration.

### Gaps Summary

Phase 3's gap-closure work substantially delivered on its mandate: **2 of the prior round's 3 gaps
are genuinely, verifiably closed.** CR-01/CR-02 (reachable UB in `container.mp4.fragment_duration`)
and CR-04 (unchecked probe-budget/timeout arithmetic silently blanking `size.*`) are both fixed with
real checked-arithmetic, real regression tests that fail against the pre-fix code, and both
independently re-reproduced clean by this verification round using the exact commands the prior round
used to prove they were broken. SC2, SC3 and SC4 are now fully clean roadmap success criteria — a
genuine, material improvement from the prior round's 1/5.

**The third gap (SC5, TRUST-06's CI wiring) is not closed, and the reason has shifted rather than
disappeared.** 03-14 did exactly what it set out to do — `scripts/gen_corpus.sh` now runs
unconditionally on every CI leg, proven on a real (not simulated, not local-only) CI run this
verification independently re-confirmed via `gh run view` returned `conclusion: failure`. But that
same real run is the first time this project's CI has ever actually executed, and it surfaced two
pre-existing defects the corpus fix was never going to touch:

1. **`src/probe/ebml_scan.cpp:348`** fails to compile under MSVC (`NOMINMAX` undefined anywhere in
   the project) — the x64-windows-static-md blocking leg cannot even reach the Test step. This is a
   Phase-3 (03-06) file, not a gap-closure-plan file, but it is Phase-3's own code, and its
   consequence — "mediadiff has never been proven to build on MSVC" — is squarely inside this
   phase's goal of getting real media through the engine on every platform PROJECT.md names.
2. **Committed byte-level goldens** were captured against a local ffmpeg build that is not the
   ffmpeg 9.0.1 CI actually installs, so 5 tests fail on the real, blocking x64-linux leg — the exact
   leg SC5's "clean CI release blocker" promise depends on.

Both are tracked in `.planning/WINDOWS.md` (#9, #10), both open, both explicitly out of scope for the
plan that surfaced them (03-14's own SUMMARY says so, and this verification agrees that plan should
not have absorbed an unrelated MSVC macro bug or an ffmpeg-version design question into its own
scope). But their net effect on THIS verification's job — "does the phase goal hold in the codebase,
not in a SUMMARY" — is unambiguous: **SC5 is still false.** A release blocker that cannot build on
one required leg and fails 5 tests on another required leg is not yet wired into CI as a clean
release blocker, no matter how solid `test_trust06_idempotence.cpp` itself is.

**This is not a regression introduced by the gap-closure plans** — WINDOWS.md #9 and #10 both predate
this verification round (03-06 for #9, whenever the goldens were committed for #10) and were simply
invisible until 03-14's CI-wiring fix made a real CI run possible for the first time. Surfacing them
is 03-14's success, not its failure, exactly as 03-14-SUMMARY.md itself argues. But surfacing a defect
does not close the success criterion it blocks, and this verification's job is to report the SC's
truth value, not to credit the discovery.

One additional, non-gating finding from this run's own code review (03-REVIEW.md, 0 blockers,
1 warning) is carried forward for developer awareness: `junit.cpp`'s new control-byte escaping omits
backslash-doubling, so a real ESC byte and the literal text `\x1b` render identically in a JUnit
report. This does not gate any roadmap success criterion and is not an XML-injection risk, but it
does mean plan 03-15's "T-2-33 genuinely closed across every output format" claim is not, in fact,
fully accurate — worth a small follow-up fix (`case '\\': out += "\\\\"; break;` in `xml_escape`,
per 03-REVIEW.md's own suggested fix) before that claim is repeated in a future SUMMARY.

**What must happen before Phase 3 can pass:**
1. Define `NOMINMAX` (project-wide or scoped) so `ebml_scan.cpp` — and by extension every Phase-3 TU —
   compiles under MSVC.
2. Decide and implement a golden-drift policy (pin CI's ffmpeg, or regenerate/version-tolerant
   goldens) so the x64-linux blocking leg's 5 failing tests pass against the ffmpeg CI actually
   installs.
3. Re-run CI and confirm all 3 blocking legs are green end-to-end, with `test_trust06_idempotence`
   explicitly observed executing and passing in the run log — not merely absent from the list of
   failures.

Neither fix requires reopening CR-01/CR-02 or CR-04's closed work, and neither is a design reversal —
(1) is a one-line/CMake-level fix, (2) is a real but bounded design decision this project has not yet
made. No override is suggested for SC5: the gap is real, current, and independently reproducible via
`gh run view`, not a stale or superseded finding.

---

_Verified: 2026-09-05T07:29:36Z_
_Verifier: Claude (gsd-verifier)_
