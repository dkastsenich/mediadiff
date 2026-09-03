---
phase: 03-probe-layer-container-size
verified: 2026-09-03T23:01:47Z
status: gaps_found
score: 4/5 roadmap success criteria fully verified (1 gap-bearing SC each on SC2/SC3/SC4 via a shared root cause, plus a confirmed SC5 CI-wiring gap)
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "A cross-container migration demotes cleanly and truncated/garbage input degrades to skipped:unparsed_mechanism or exits 65 cleanly, and never crashes or silently passes (ROADMAP SC2)."
    status: partial
    reason: >
      Cross-container demotion, garbage-input exit-65, and truncated-input unparsed_mechanism-with-byte-offset
      were all empirically confirmed working (see Data-Flow/Behavioral Spot-Checks below). However
      03-REVIEW.md (committed ba4d881, written after this phase's own SUMMARYs, findings not yet fixed)
      documents two unresolved Critical findings that are reachable UB from crafted input, directly
      threatening the "never crashes" half of this truth:
      CR-01 — src/analyzers/container/mp4.cpp:286's container.mp4.fragment_duration computes
      `keyframe_dts[i] - keyframe_dts[i - 1]` as a RAW (unchecked) int64 subtraction on
      attacker/file-controlled AVPacket::dts values — signed integer overflow (UB) on a crafted MP4,
      the only DTS-delta site in the codebase that does not use detail::checked_sub, breaking this
      project's own documented "every arithmetic site touching file-controlled magnitudes is checked"
      invariant.
      CR-02 — the same function's std::stable_sort comparator (mp4.cpp:296-306) can fail to establish
      a strict weak order when compare_ticks_checked overflows inconsistently across the "durations"
      vector, which is itself UB per the C++ standard, occurring *before* the code's own
      "detect-then-skip" overflow guard ever runs.
      Neither finding has a regression test yet (no sanitizer preset exists in this repo per
      WINDOWS.md #3, so ASan/UBSan coverage that would catch this class of bug was never run against
      bmff_scan/mp4.cpp for this phase).
    artifacts:
      - path: src/analyzers/container/mp4.cpp
        issue: "Lines 286 and 296-306 perform unchecked signed arithmetic / build a non-strict-weak-order comparator on file-controlled AVPacket::dts values (CR-01, CR-02 in 03-REVIEW.md)."
    missing:
      - "Route the keyframe_dts delta subtraction through detail::checked_sub, skipping the check on overflow (matching every sibling site in ts_scan.cpp/size.cpp)."
      - "Detect compare_ticks_checked overflow risk before calling std::stable_sort, or replace the fold-into-equal comparator with a well-defined total order."
      - "Add a crafted-fixture regression test (or a sanitizer build) that actually exercises the extreme-DTS path so this class of defect cannot regress silently."
  - truth: "size.file, size.stream_bitrate, size.peak_bitrate and size.overhead report rate economics from the packet scan alone, and peak memory per in-flight file is bounded and asserted so --threads N is an honest memory knob (ROADMAP SC3 + SC4/DIR-06)."
    status: failed
    reason: >
      The rational bitrate math itself is correct and cross-platform-identical (independently
      recomputed by hand against tests/fixtures/mp4_faststart.mp4's real evidence: byte_total=120903,
      dts_span_ticks=25088, tb=1/12800 -> exact numerator/denominator match). However, 03-REVIEW.md's
      CR-04 finding was independently REPRODUCED during this verification, not merely read: running
      `mediadiff compare --probe-memory-budget-mb 8796093022208 tests/fixtures/mp4_faststart.mp4
      tests/fixtures/mp4_faststart.mp4 --json` (a value CLI11's PositiveNumber validator accepts as an
      ordinary positive integer) causes the unchecked `probe_budget_mb_result * 1024 * 1024`
      multiplication in src/cli/commands/compare.cpp:178 (mirrored in dir.cpp:325, inspect.cpp:75,
      and options.cpp:271 for --probe-timeout) to signed-overflow to a NEGATIVE int64. The command
      exits 0 (success) with every one of size.stream_bitrate/size.peak_bitrate/size.overhead silently
      reduced to `skipped:partial_scan`, with no error or warning surfaced to the user explaining why.
      This is exactly the "every size regression check goes blind" failure mode 03-REVIEW.md predicted,
      confirmed reproducible with an ordinary CLI flag value, not a contrived edge case. It directly
      falsifies "size.* report rate economics" (the checks silently stop reporting anything) and
      DIR-06/SC4's "peak memory per in-flight file is bounded and asserted... --threads N is an honest
      memory knob" (the budget arithmetic that DIR-06's own asserted-bound guarantee rests on is not
      itself overflow-safe).
    artifacts:
      - path: src/cli/commands/compare.cpp
        issue: "Line 178: unchecked `*probe_budget_mb_result * 1024 * 1024` — confirmed to overflow to a negative budget and silently disable size.* checks (CR-04)."
      - path: src/cli/commands/dir.cpp
        issue: "Line 325: identical unchecked multiplication pattern (CR-04)."
      - path: src/cli/commands/inspect.cpp
        issue: "Line 75: identical unchecked multiplication pattern (CR-04)."
      - path: src/cli/options.cpp
        issue: "Line 271: `resolve_probe_timeout_ms` unchecked seconds*1000 multiplication; confirmed reproducible with --probe-timeout 9223372036854776, producing an immediate spurious timeout (exit 65) on a trivially small local fixture."
      - path: src/config/toml_load.cpp
        issue: "Lines 280/292: static_cast<int> narrowing of an unbounded int64 TOML value with no upper-bound check (CR-04)."
    missing:
      - "Route both multiplications through detail::checked_mul, returning ErrorKind::usage on overflow instead of silently entering the checked-arithmetic discipline's blind spot."
      - "Add an explicit upper bound on memory_budget_mb/timeout_seconds in config/toml_load.cpp before the static_cast<int> narrowing, mirroring the existing kMaxDirThreads pattern."
      - "A CLI-level regression test asserting an extreme (but CLI11-legal) --probe-memory-budget-mb value either produces a clean usage error or does NOT silently blank every size.* check."
  - truth: "Encoding a fixture twice with identical settings and comparing under sw-encoder comes back clean as a CI release blocker (ROADMAP SC5)."
    status: failed
    reason: >
      The TRUST-06 test itself (tests/integration/test_trust06_idempotence.cpp) is real, well-written,
      and passes locally (confirmed via `ctest --preset x64-linux -R trust06`). It is correctly
      discovered by catch_discover_tests inside the unconditional Test step, matching its own header
      comment's "no separate job, no if:, no continue-on-error" claim. However, confirmed by directly
      reading .github/workflows/ci.yml end to end: scripts/gen_corpus.sh (the Linux/macOS/Windows-sh
      fixture generator that produces idem_a.mp4/idem_b.mp4 and every other media fixture this phase's
      tests depend on) is invoked NOWHERE in the workflow. Only a Windows-specific gen_corpus.ps1
      version-gate smoke check runs, AFTER the Test step, and only on the windows-2022 leg. All media
      fixtures (tests/fixtures/*.mp4, *.mkv, *.ts, etc.) are git-ignored per .gitignore's
      `tests/fixtures/*` rule and confirmed NOT tracked in git (only GENERATOR_MANIFEST.json and the
      hand-authored config/registry/probe fixtures are). On the two blocking CI legs that run a fresh
      checkout without a locally-primed fixture cache — x64-linux (ubuntu-24.04) and arm64-osx
      (macos-15), both `blocking: true` in the matrix — the Test step's `ctest --output-on-failure`
      would find idem_a.mp4/idem_b.mp4 (and virtually every other Phase 3 corpus-dependent test fixture)
      missing, and test_trust06_idempotence.cpp's own assertion (exit 0, every finding pass/skipped)
      would hard-FAIL on a "could not open input" error rather than a real regression signal. This
      matches WINDOWS.md #8 exactly (predates this phase but the phase's own TRUST-06 test is the
      concrete mechanism this gap defeats): a release blocker that cannot run correctly on 2 of 3
      blocking CI legs is not actually functioning as "wired into CI as a release blocker" — it would
      either be permanently red (blocking every merge for the wrong reason) or the whole Test step's
      exit code would be masked in some other way not evidenced anywhere in this workflow.
    artifacts:
      - path: .github/workflows/ci.yml
        issue: "No step invokes scripts/gen_corpus.sh before the Test step on any of the 5 matrix legs; only gen_corpus.ps1's version-gate smoke check runs, after Test, on Windows only."
    missing:
      - "A fixture-generation step (calling scripts/gen_corpus.sh, or restoring a committed/cached corpus) before the Test step on every leg whose tests depend on generated media fixtures."
deferred: []
---

# Phase 3: Probe Layer, Container & Size Verification Report

**Phase Goal:** Real media enters the engine — one header pass and one packet sweep feed every
container, metadata and size check, plus the shared primitives that later phases consume instead
of recomputing.
**Verified:** 2026-09-03T23:01:47Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---|---|---|
| 1 | `mediadiff inspect` on MP4/MOV, MKV/WebM, MPEG-TS renders a complete container section (topology incl. subtitle/caption presence, per-format mechanisms, per-program TS measurements, metadata tags as a set with volatile keys shown under `-v`) | ✓ VERIFIED | Ran `mediadiff inspect -v` directly against `tests/fixtures/mp4_faststart.mp4`, `mkv_cues_front.mkv`, `ts_multiprogram.ts` — all render `container.format`, `track_count` (incl. `subtitle:0` explicitly), `track_types`, `track_order`, `chapters`, plus faststart/brands/fragmentation/edit_list/timescale (MP4), cues_placement/codec_delay/timestamp_scale/duration_element (MKV), and per-`program[N]`-scoped cc_errors/pcr_interval/psi_interval/pmt_version_churn/null_ratio (TS). `tests/unit/test_meta_tags.cpp` confirms volatile keys (`creation_time`/`encoder`/`handler_name`/`encoding_tool`) are excluded from the comparison set but present in evidence; `src/cli/inspect_render.h:277-358`'s `append_ignored_evidence` renders them under `-v`. |
| 2 | Cross-container migration demotes `container.<fmt>.*` to `skipped:cross_container` on both sides while topology proceeds; truncated/garbage input degrades to `skipped:unparsed_mechanism` with byte offset or exits 65; never crashes or silently passes | ⚠️ PARTIAL | Ran `mediadiff compare mp4_faststart.mp4 mkv_cues_front.mkv --json`: every `container.mp4.*`/`container.mkv.*` finding is `skipped/cross_container` while `container.format`/`track_count`/`track_types`/`track_order`/`chapters` compare normally at the semantic layer. Ran a 50-byte `/dev/urandom` file through `compare`: exits 65 cleanly ("could not probe input"). Ran a 4096-byte-truncated MP4 through `compare`: `container.mp4.faststart`/`brands`/`fragmentation`/`fragment_duration`/`timescale` all degrade to `skipped:unparsed_mechanism` with `{"stop_offset":2684}` evidence, exit 1 (a real size.file regression, not a crash). BUT: 03-REVIEW.md's CR-01/CR-02 (unresolved, committed `ba4d881`, no fix yet) document reachable signed-overflow UB and a non-strict-weak-order `std::stable_sort` comparator in `container.mp4.fragment_duration`'s median computation, directly threatening "never crashes" for crafted input this verification did not personally construct a triggering fixture for (no sanitizer preset exists in this repo to catch it either — WINDOWS.md #3). |
| 3 | `size.file`/`size.stream_bitrate`/`size.peak_bitrate`/`size.overhead` report rate economics from the packet scan alone, DTS-in-ticks windowing, cross-platform-identical | ✗ FAILED (partial) | Hand-verified the raw rational math is exact: `mp4_faststart.mp4`'s `size.stream_bitrate video[0]` evidence (`byte_total=120903`, `dts_span_ticks=25088`, `tb=1/12800`) reproduces the emitted `num=12380467200`/`den=25088` bit-for-bit via `byte_total*8*tb.den = 120903*8*12800 = 12380467200`. `detail::compute_peak_window` (src/analyzers/size/size.cpp:345+) sorts DTS-in-ticks with checked comparisons as designed. BUT: independently reproduced CR-04 — `mediadiff compare --probe-memory-budget-mb 8796093022208 ... --json` overflows `probe_budget_mb_result * 1024 * 1024` to a negative int64, silently forcing every `size.stream_bitrate`/`size.peak_bitrate`/`size.overhead` finding to `skipped:partial_scan` with exit 0 and no diagnostic. |
| 4 | Each file read exactly once (union of declared passes), PROBE-10 packet-interval statistics shared as one probe-level primitive, peak memory per in-flight file bounded and asserted so `--threads N` is an honest memory knob | ✗ FAILED (partial) | `ctest -R pass_union` (8/8 pass) behaviorally proves single-sweep + pointer-identical shared `PacketScanResult` across two analyzers. `ctest -R packet_budget` (3/3 pass) proves the accounted-bytes budget divides correctly by resolved thread count. All four CLI entry points (`compare.cpp`, `dir.cpp`, `inspect.cpp`, `snapshot.cpp`) route through the single `probe/orchestrator.h::run_probe`. BUT: the same CR-04 overflow undermines "asserted... honest memory knob" — the budget arithmetic the D-01 bound is built on is not itself overflow-checked, so an ordinary-looking `--probe-memory-budget-mb`/`--probe-timeout` value silently corrupts the very bound this truth claims is asserted. |
| 5 | Encode-twice-and-compare comes back clean as a CI release blocker (TRUST-06); every check has a triggering + clean fixture pair (DOC-03); `ts_scan` cross-checked against TSDuck (TRUST-09) | ✗ FAILED (partial) | `ctest -R trust06_idempotence` (2/2 pass) and `ctest -R doc03` (2/2 relevant, enumerating the real production registry at 30/30 checks incl. all 27 Phase-3 ids) and `ctest -R ts_scan_golden` (3/3 pass, comparing against committed TSDuck-captured goldens, D-04 compliant — no TSDuck linked) all pass locally. BUT: read `.github/workflows/ci.yml` end to end — `scripts/gen_corpus.sh` is invoked on NO leg, only the unrelated `gen_corpus.ps1` version-gate runs, after Test, Windows-only. Media fixtures are confirmed git-ignored and untracked. On a fresh checkout, the two blocking Linux/macOS legs would hard-fail nearly every corpus-dependent test (including TRUST-06 itself) from missing fixtures, not from a real regression — the release blocker cannot function as wired (WINDOWS.md #8, confirmed). Additionally WINDOWS.md #7 (already open) notes `container.ts.psi_interval`/`pmt_version_churn` satisfy DOC-03's literal trigger gate via an unintended path (CONT-08 topology mismatch) rather than a genuine same-topology PSI-interval perturbation. |

**Score:** 1/5 roadmap success criteria fully clean; 4/5 have a genuine, reproducible or documented-and-unresolved gap traced to a small number of root causes (CR-01/CR-02 UB in `mp4.cpp`, CR-04 unchecked probe-budget arithmetic, and the pre-existing `gen_corpus.sh`/CI wiring gap). All core positive-path behavior (container rendering, cross-container demotion, degrade paths, rate-economics math, single-sweep sharing) is real and independently confirmed working — the gaps are edge-case/robustness and CI-wiring defects layered on top of genuinely working machinery, not stubs or missing functionality.

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/probe/{bmff,ebml,ts}_scan.{cpp,h}` | Bounded box/element/packet walks (PROBE-04/05/06/07) | ✓ VERIFIED | Reviewed in 03-REVIEW.md as "carefully and consistently bounds-checked... no exploitable unbounded read"; behaviorally confirmed via real `inspect` output on all 3 formats. |
| `src/probe/packet_scan.{cpp,h}` | One-sweep PacketScan, PROBE-10 shared raw array | ✓ VERIFIED | `ctest -R pass_union` proves single sweep + shared pointer identity. |
| `src/probe/demux_session.{cpp,h}`, `src/probe/orchestrator.{cpp,h}` | Single-open DemuxSession, pass-union orchestration (PROBE-01/08) | ✓ VERIFIED | All 4 CLI commands route through `orchestrator.h::run_probe`; 8/8 `pass_union` tests pass. |
| `src/analyzers/container/{mp4,mkv,ts,topology,meta}.cpp` | CONT-01…09 check families | ✓ VERIFIED (with CR-01/CR-02 caveat) | 27 check ids registered and rendered correctly on real fixtures; `mp4.cpp`'s `fragment_duration` has unresolved UB per 03-REVIEW.md. |
| `src/analyzers/size/size.cpp` | SIZE-01 rate-economics checks | ⚠️ HOLLOW under overflow | Correct math confirmed by hand; silently disabled by CR-04 overflow in the CLI/config layer feeding it its budget. |
| `docs/checks/*.md` (27 new files) | DOC-01 build-enforced docs for every Phase-3 check | ✓ VERIFIED | All 27 files present, matching 03-CHECK-ROSTER.md exactly. |
| `.github/workflows/ci.yml` | TRUST-06 wired as CI release blocker | ✗ NOT ACTUALLY FUNCTIONAL on 2/3 blocking legs | `gen_corpus.sh` never invoked; media fixtures git-ignored/untracked; confirmed by direct read of the workflow file. |
| `tests/golden/*` + `scripts/{capture,extract,lint}_tsduck*` | TRUST-09 TSDuck cross-check, no linking | ✓ VERIFIED | 3/3 golden comparison tests pass; goldens are committed text, not requiring TSDuck at CI/build time. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `src/cli/commands/{compare,dir,inspect,snapshot}.cpp` | `src/probe/orchestrator.h::run_probe` | direct call | ✓ WIRED | Confirmed by grep; all 4 command files include and call it. |
| `SkipReason` enum | `src/report/json.cpp` / `junit.cpp` / `docs/schema/report-1.0.json` | closed enum kept in sync | ✓ WIRED | `skipped:cross_container`, `skipped:unparsed_mechanism`, `skipped:partial_scan`, `skipped:no_timing_data` all observed rendering correctly in real `--json` output. |
| `--probe-memory-budget-mb` CLI flag | D-01 packet-scan byte ceiling → `size.*` skip decisions | `compare.cpp`/`dir.cpp`/`inspect.cpp` unchecked multiplication | ⚠️ WIRED BUT UNSAFE | Confirmed the link is real and functional for ordinary values, but overflow-prone at the boundary (CR-04, empirically reproduced). |
| `container.mp4.*`/`container.mkv.*` findings | `container.format` mismatch → `cross_container` | `src/compare/*` demotion logic | ✓ WIRED | Confirmed via real `compare --json` output. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|---|---|---|---|---|
| `size.stream_bitrate` measurement | `RationalValue{num,den,tb}` | `StreamPacketScan::packets` (real demuxed AVPacket stream) | Yes — hand-verified exact against real fixture evidence | ✓ FLOWING |
| `container.ts.pcr_interval`/`psi_interval` | per-program `program[N]` scoped rational | `ts_scan.cpp`'s real PCR/PSI parse of `ts_multiprogram.ts` | Yes — distinct values per program (89ms vs 88ms, 106ms vs 111ms) | ✓ FLOWING |
| `size.*` under an overflowed probe-memory-budget | `Absent{}` w/ `skip_reason=partial_scan` | orchestrator's `default_packet_scan_max_bytes()` corrupted to negative | No — silently blanked, not a fabricated wrong value, but with no diagnostic | ⚠️ STATIC (degrades silently) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| `inspect` renders complete MP4 container section | `mediadiff inspect tests/fixtures/mp4_faststart.mp4 -v` | faststart/brands/fragmentation/edit_list/timescale all present with evidence | ✓ PASS |
| `inspect` renders complete MKV container section | `mediadiff inspect tests/fixtures/mkv_cues_front.mkv -v` | cues_placement/codec_delay/timestamp_scale/duration_element all present with evidence | ✓ PASS |
| `inspect` renders complete TS container section, per-program | `mediadiff inspect tests/fixtures/ts_multiprogram.ts -v` | 2-program cc_errors/pcr_interval/psi_interval/pmt_version_churn/null_ratio all present, program-scoped | ✓ PASS |
| Cross-container demotes cleanly | `mediadiff compare mp4_faststart.mp4 mkv_cues_front.mkv --json` | all `container.mp4.*`/`container.mkv.*` → `skipped/cross_container`; generic topology compares normally | ✓ PASS |
| Garbage input exits cleanly | `mediadiff compare /dev/urandom(50B) mp4_faststart.mp4 --json` | exit 65, clean error message, no crash | ✓ PASS |
| Truncated input degrades with byte offset | `mediadiff compare truncated(4KB).mp4 mp4_faststart.mp4 --json -v` | `skipped:unparsed_mechanism`, `{"stop_offset":2684}` evidence, exit 1 (real diff), no crash | ✓ PASS |
| Single sweep + shared primitive | `ctest -R pass_union` | 8/8 pass | ✓ PASS |
| Accounted memory budget divides correctly (nominal input) | `ctest -R packet_budget` | 3/3 pass | ✓ PASS |
| **Probe-memory-budget overflow silently blanks size.\* checks (CR-04 repro)** | `mediadiff compare --probe-memory-budget-mb 8796093022208 mp4_faststart.mp4 mp4_faststart.mp4 --json` | exit 0; `size.stream_bitrate`/`size.peak_bitrate`/`size.overhead` all `skipped:partial_scan`, no diagnostic | ✗ FAIL (confirms CR-04) |
| **Probe-timeout overflow produces a spurious immediate timeout (CR-04 repro, second site)** | `mediadiff compare --probe-timeout 9223372036854776 mp4_faststart.mp4 mp4_faststart.mp4` | exit 65, "exceeded its wall-clock budget" on a trivially small local file | ✗ FAIL (confirms CR-04, but exits cleanly rather than crashing) |
| DOC-03 fixture-pair coverage gate | `ctest -R doc03` | 2/2 relevant tests pass, 30/30 registered checks covered | ✓ PASS |
| TRUST-09 TSDuck golden cross-check | `ctest -R ts_scan_golden` | 3/3 pass | ✓ PASS |
| Full workspace suite (run once) | `ctest --preset x64-linux --output-on-failure` | 588/588 pass, 1 platform-skip (Windows-only VT test) | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh`-style probes declared by this phase; N/A — skipped.

### Requirements Coverage

All 23 requirement IDs declared across the 11 plans (`DIR-06, PROBE-01/02/04/05/06/07/08/09/10, CONT-01…09, SIZE-01, TRUST-06/09, DOC-03`) are present in REQUIREMENTS.md's Phase-3 mapping and marked `[x]`/Complete. Cross-referenced against `.planning/ROADMAP.md`'s own Phase-3 requirement list (identical set) — no orphans, no omissions.

| Requirement | Source Plan | Status | Evidence |
|---|---|---|---|
| PROBE-01 | 03-02 | ✓ SATISFIED | `DemuxSession` single-open, verified via CLI routing. |
| PROBE-02 | 03-03 | ✓ SATISFIED | `PacketScan` sweep, `ctest -R pass_union`/`packet_scan`. |
| PROBE-04 | 03-05 | ✓ SATISFIED | `bmff_scan`, verified via real `inspect` output on MP4. |
| PROBE-05 | 03-06 | ✓ SATISFIED | `ebml_scan`, verified via real `inspect` output on MKV. |
| PROBE-06/07 | 03-07 | ✓ SATISFIED | `ts_scan`, verified via `inspect` and TSDuck goldens. |
| PROBE-08 | 03-02 | ✓ SATISFIED | Pass-union orchestration, `ctest -R pass_union`. |
| PROBE-09 | 03-01, 03-10 | ✓ SATISFIED | Garbage/truncated-input behavior directly confirmed. |
| PROBE-10 | 03-03 | ✓ SATISFIED (design note) | Shared raw-array primitive, documented deliberate scope choice (not a precomputed stats struct); real consumers (video/timeline) don't exist until Phase 4/5, so full "consumed by both families" claim is not yet checkable but the sharing mechanism itself is proven. |
| CONT-01…09 | 03-02, 03-04, 03-05, 03-06, 03-08 | ✓ SATISFIED (CR-01/CR-02 caveat on CONT-05's fragment_duration) | Real `inspect`/`compare` output confirms all families; unresolved UB noted as a gap above. |
| SIZE-01 | 03-01, 03-09 | ⚠️ SATISFIED BUT FRAGILE | Math correct; CR-04 overflow silently disables the whole family under a plausible CLI input. |
| DIR-06 | 03-03 | ⚠️ SATISFIED BUT FRAGILE | Accounted-budget division correct for nominal input; same CR-04 overflow corrupts the bound itself. |
| TRUST-06 | 03-11 | ⚠️ TEST REAL, CI WIRING BROKEN | Test passes locally; cannot function as a CI release blocker per the `gen_corpus.sh` gap. |
| TRUST-09 | 03-10 | ✓ SATISFIED | TSDuck goldens pass, D-04 compliant. |
| DOC-03 | 03-11 | ✓ SATISFIED (with a known semantic-precision caveat, WINDOWS.md #7) | 30/30 registered checks covered by a real, registry-driven gate. |

No ORPHANED requirements found: every ID REQUIREMENTS.md maps to Phase 3 appears in some plan's `requirements:` frontmatter, and vice versa.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---|---|---|---|
| `src/analyzers/container/mp4.cpp` | 286, 296-306 | Unchecked signed arithmetic + non-strict-weak-order sort comparator on file-controlled values (CR-01/CR-02) | 🛑 Blocker | Reachable UB from crafted input; violates the phase's own "never crashes" success criterion and this codebase's own checked-arithmetic discipline. |
| `src/cli/commands/{compare,dir,inspect}.cpp`, `src/cli/options.cpp`, `src/config/toml_load.cpp` | 178/325/75/271/280,292 | Unchecked int64 multiplication + unbounded narrowing on user/config-supplied magnitudes (CR-04) | 🛑 Blocker | Empirically reproduced silent corruption of the D-01 memory/timeout budget; disables every `size.*` check with no diagnostic. |
| `src/report/junit.cpp` | 65-87 | `xml_escape` does not strip/escape raw C0 control bytes despite the file's own header comment claiming full coverage (CR-03) | ⚠️ Warning | Not a Phase-3-declared requirement id, but directly contradicts 03-11-SUMMARY.md's claim that T-2-33 closed "88/88 threats" — a SUMMARY completeness claim falsified by this same phase's own review. |
| `src/cli/commands/compare.cpp` (+ `dir.cpp`, `inspect.cpp`) | multiple | CLI stderr diagnostics bypass the `sanitize_for_display` choke point (WR-01) | ⚠️ Warning | Same T-2-33-completeness concern as CR-03 above. |
| `src/core/serializer.cpp` | 24-30 | The generic `RationalValue` → JSON `ms` convenience field assumes `num/den` = seconds; for `size.stream_bitrate`/`peak_bitrate` (bits/sec, not time) this produces a nonsensical `ms` figure (e.g. `493481632.65`) even though the underlying `num`/`den` (the actually-compared value) is exact and correct | ℹ️ Info | Pre-existing Phase-2-approved schema design, explicitly excluded from comparison — cosmetic mislabeling only, does not affect correctness of any check's actual result. Worth a follow-up to special-case non-time units rather than a Phase-3 regression. |

**Debt-marker gate:** No `TBD`/`FIXME`/`XXX` found in any Phase-3-modified file — clean.

### Human Verification Required

None. All findings above were resolved programmatically (direct CLI execution against real fixtures, direct code reading, and direct reproduction of the reported defects) rather than requiring subjective/visual/real-time judgment.

### Gaps Summary

Phase 3's core positive-path machinery is real and works: three independent hand-rolled scanners
correctly render complete, per-format container sections on real MP4/MKV/TS fixtures; cross-container
demotion, garbage-input refusal, and truncated-input degradation all behave exactly as specified;
the size.* rate-economics math is byte-exact and cross-platform-rational by construction; the
single-sweep/shared-primitive architecture is behaviorally proven by 8 passing pass-union tests;
and the DOC-03/TRUST-09 CI gates are real, registry-driven, and pass. This is a substantial, working
implementation, not a stub.

However, three concrete, either-reproduced-or-documented-and-unresolved defects prevent a clean
`passed` verdict:

1. **CR-01/CR-02** (`src/analyzers/container/mp4.cpp`): reachable UB from crafted input in
   `container.mp4.fragment_duration`, unresolved in 03-REVIEW.md, directly threatens SC2's
   "never crashes" guarantee.
2. **CR-04** (probe-budget/timeout CLI and config plumbing): empirically reproduced during this
   verification — an ordinary-looking `--probe-memory-budget-mb` value silently disables every
   `size.*` check with exit 0 and no diagnostic, undermining SC3 and DIR-06/SC4's "asserted... honest
   memory knob" claim.
3. **CI wiring** (`.github/workflows/ci.yml`): `scripts/gen_corpus.sh` is invoked on no leg; every
   corpus-dependent test (including TRUST-06 itself) would hard-fail from a missing fixture rather
   than a real regression on 2 of 3 blocking legs on a fresh checkout, directly undermining SC5's
   "wired into CI as a release blocker" claim. This gap predates Phase 3 (WINDOWS.md #8) but Phase 3
   is the first phase whose success criterion actually depends on it functioning.

All three are structured as gaps above with concrete fix guidance. None require a design reversal —
each is a checked-arithmetic/CI-plumbing fix consistent with patterns this codebase already uses
correctly everywhere else.

**This looks intentional in one respect only** — PROBE-10's raw-array-not-precomputed-stats design
choice is a deliberate, well-documented architectural decision, not a gap; it is reported as
✓ SATISFIED above, not as a gap needing an override.

---

_Verified: 2026-09-03T23:01:47Z_
_Verifier: Claude (gsd-verifier)_
