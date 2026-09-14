---
phase: "04"
slug: "video-analysis"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: validated
nyquist_compliant: true
wave_0_complete: true
validated: "2026-09-14"
created: "2026-09-09"
---

# Phase 04 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 3.15.3 (project-standard, CTest-integrated via `catch_discover_tests()`) |
| **Config file** | `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` |
| **Quick run command** | `ctest --preset x64-linux -R <substring> --output-on-failure` |
| **Full suite command** | `ctest --preset x64-linux --output-on-failure` |
| **Estimated runtime** | ~90 s full suite (623 tests as of Phase 3 close); targeted `-R` filters run in seconds |

**Corpus prerequisite.** Integration tests that read fixtures require the corpus. `bash
scripts/gen_corpus.sh` must run before any `-R doc03_coverage` or fixture-reading test, and
`scripts/check_corpus.sh` is the preflight that asserts it produced what the recipes declare.

---

## Sampling Rate

- **After every task commit:** targeted `ctest --preset x64-linux -R <touched-area>`
- **After every plan wave:** full `ctest --preset x64-linux --output-on-failure`
- **Before `/gsd-verify-work`:** full suite green, plus DOC-03's registry-enumerated
  fixture-pair count-equality assertion covering every new `video.*` check id
- **Max feedback latency:** ~90 s (full suite); < 10 s for targeted filters

---

## Per-Task Verification Map

Task IDs are assigned by the planner; this map records the requirement→test binding the plans
must satisfy. Every row's command is runnable today except where a Wave 0 file is named.

| Requirement | Behavior | Test Type | Automated Command | File Exists | Status |
|---|---|---|---|---|---|
| PROBE-03 | `ParserScan` populates `pict_type`/`key_frame`/`repeat_pict`/`field_order` plus NAL-type sequences, inside the single existing `av_read_frame` sweep | unit | `ctest --preset x64-linux -R parser_scan --output-on-failure` | ✅ | ✅ green (14 tests, 2026-09-14) |
| PROBE-03 | Overhead measured against plain `PacketScan` and **recorded, not gated** (D-11) | measurement | recorded in SUMMARY; no CI gate this phase | n/a | ✅ recorded (manual-only, D-11) |
| VIDEO-01 | `codec`, `profile`, `level`, `resolution`, `sar`/`dar`, `pix_fmt`, `frame_rate.declared`, `frame_rate.measured`, `frame_count` | unit | `ctest --preset x64-linux -R video_stream_params --output-on-failure` | ✅ | ✅ green (27 tests, 2026-09-14) |
| VIDEO-02 | `frame_count` always from the scan, never `nb_frames`; container value kept as evidence | unit | `ctest --preset x64-linux -R video_stream_params --output-on-failure` | ✅ | ✅ green (27 tests, 2026-09-14) |
| VIDEO-03 | yuvj420p → yuv420p+full produces **exactly one** finding, on `video.color.range` | integration | `ctest --preset x64-linux -R doc03_coverage --output-on-failure` | ✅ | ✅ green (2 tests, 2026-09-14) |
| VIDEO-04 | Container-vs-VUI SAR conflict records both, compares the effective one, flags the conflict `info` | unit | `ctest --preset x64-linux -R video_stream_params --output-on-failure` | ✅ | ✅ green (27 tests, 2026-09-14) |
| VIDEO-05 | `gop.length`, `gop.idr_interval` with open/closed via NAL type, `gop.refs`, `frame_types` | unit | `ctest --preset x64-linux -R gop_classification --output-on-failure` | ✅ | ✅ green (23 tests, 2026-09-14) |
| VIDEO-06 | `interlace` cross-checks declared `field_order` against parser flags; `mixed` with proportions | unit | `ctest --preset x64-linux -R interlace --output-on-failure` | ✅ | ✅ green (22 tests, 2026-09-14) |
| VIDEO-07 | `color.range` (fail in every profile), `primaries`, `transfer`, `matrix`, `chroma_loc` | integration | `ctest --preset x64-linux -R doc03_coverage --output-on-failure` | ✅ | ✅ green (2 tests, 2026-09-14) |
| VIDEO-08 | A change **to** `unspecified` is a regression, not a wildcard match | unit | `ctest --preset x64-linux -R video_color --output-on-failure` | ✅ | ✅ green (12 tests, 2026-09-14) |
| VIDEO-09 | `hdr.mdcv`/`cll`/`dovi` with stream-level precedence and recorded `source:` | integration | `ctest --preset x64-linux -R doc03_coverage --output-on-failure` | ✅ | ✅ green (2 tests, 2026-09-14) |
| VIDEO-10 | Incoherence guard fires as non-gating `info` even when both files share it (D-10: own check id) | unit + integration | `ctest --preset x64-linux -R video_hdr --output-on-failure` | ✅ | ✅ green (8 tests, 2026-09-14) |
| VIDEO-12 | A codec with no parser degrades to `skipped:no_parser`; frame types fall back to keyframe-flag granularity | unit | `ctest --preset x64-linux -R gop_classification --output-on-failure` | ✅ | ✅ green (23 tests, 2026-09-14) |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

**Threat refs** are assigned per plan in each PLAN.md's `<threat_model>` block (ASVS L1,
`block_on: high`), following the Phase 3 register convention; they are not pre-allocated here.

---

## Wave 0 Requirements

- [x] `tests/unit/test_parser_scan.cpp` — hand-constructed H.264/HEVC NAL sequences fed directly
      to `av_parser_parse2`, proving `pict_type`/`key_frame`/NAL classification independently of
      any fixture file. Mirrors `tests/unit/test_ts_continuity.cpp`'s hand-verified-table
      discipline (that file never reads a fixture).
- [x] `tests/unit/test_gop_classification.cpp` — IDR-vs-CRA and IDR-vs-non-IDR-I open/closed
      logic, same hand-verified-table pattern.
- [x] `tools/gen_video_fixtures.py` — the D-03 writer: Annex-B NAL bytes, plus the 24-byte
      `dvcC`/`dvvC` box for `hdr.dovi`.
- [x] New `video.*` fixture recipes in `scripts/gen_corpus.sh`, including the VIDEO-03 signature
      pair, the colorimetry pairs, and the HDR pair.
- [x] (superseded) `scripts/check_corpus.sh` extension asserting `mjpeg` availability on the pinned build
      (research assumption **A3** — confirmed on Linux only; the Windows leg uses BtbN's `-lgpl`
      artifact and is unverified).

**Open Question 1 is CLOSED** — see `04-RESEARCH.md`'s Orchestrator Addendum. `mjpeg` natively
supports `yuvj420p`, is a built-in LGPL codec, and produced a real `mjpeg,yuvj420p,pc` mp4 on the
pinned build. No spike is needed for the yuvj fixture; A3 above is the residual risk.

**Open Question 3 remains** — the exact minimal SPS/PPS Exp-Golomb bit layout for the D-03 writer.
The research pass recommends a short spike against the real linked parser before the writer task
is considered done; treat it as part of the `tools/gen_video_fixtures.py` Wave 0 item rather than
a separate deliverable.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|---|---|---|---|
| Parser-pass overhead under 10% | PROBE-03 / SC5 | D-11 makes this measured-and-recorded, not gated. The blocking gate, the 10-minute reference fixture and regression tracking are Phase 5 (`PERF-03`, `PERF-05`). A wall-clock assertion on shared CI runners is a false-positive source, and this project treats false positives as P0. | Generate the long file on demand per D-12 (outside `tests/fixtures/` and `CORPUS_DIGEST.txt`), run the probe with and without `Pass::parser_scan`, record both timings and the ratio in the plan's SUMMARY. |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or a named Wave 0 dependency
- [x] Sampling continuity: no 3 consecutive tasks without an automated verify
- [x] Wave 0 covers all MISSING references above
- [x] No watch-mode flags
- [x] Feedback latency < 90 s

---

## Validation Audit 2026-09-14

| Metric | Count |
|--------|-------|
| Gaps found | 0 |
| Resolved | 0 |
| Escalated | 0 |

Audit performed at Phase 4 close (execute-phase verify:post hook, nyquist step) against branch head
c668cc1 with the corpus generated: every automated command in the map was run live and is green
(`parser_scan` 14, `video_stream_params` 27, `doc03_coverage` 2, `gop_classification` 23,
`interlace` 22, `video_color` 12, `video_hdr` 8 tests). One stale command was corrected: VIDEO-10's
`-R hdr_coherence` matched no test because the eight `video.hdr.coherence` cases live in
`tests/unit/test_video_hdr.cpp` under the `unit.video_hdr` prefix; the map now uses `-R video_hdr`.
The Wave 0 `check_corpus.sh` mjpeg-availability extension was superseded by the pinned-generator
release-identity gate (`scripts/gen_corpus.sh` + `scripts/ffmpeg_pin.json`, quick tasks 260910-vvp
and 260914-qlk): the Windows LGPL build's mjpeg support is now proven by CI run 34891069554, whose
x64-windows-static-md leg generated all 138 fixtures and produced the designated leg's digest
byte for byte. PROBE-03's overhead figure stays recorded-not-gated (D-11, Phase 5 `PERF-03`).
Full suite: 771/771 on x64-linux (CI run 34891069554 and locally), 766/766 on Windows.
