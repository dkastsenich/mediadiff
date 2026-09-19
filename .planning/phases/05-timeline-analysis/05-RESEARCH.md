# Phase 5: Timeline Analysis - Research

**Researched:** 2026-09-16
**Domain:** Integer/rational timestamp math over FFmpeg-demuxed packet streams; least-squares A/V drift estimation; MPEG-TS PTS unwrap; SMPTE timecode extraction without decode; CI performance-regression gating via instruction counts.
**Confidence:** HIGH for empirically-verified libav behavior and existing-code integration points; MEDIUM for CI-environment specifics (valgrind availability must be provisioned, not assumed present); LOW/ASSUMED flagged explicitly for anything not verified this session (see Assumptions Log).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Finding ownership and the no-others clause**

- **D-01: DOC-04's no-others assertion counts every non-pass, non-skipped finding in the whole report.** The counter is the one `tests/integration/test_video_yuvj.cpp` already uses — all families, `info` included, never filtered by group or id. Unrelated noise (a `size.*` delta riding along on two independently encoded files) fails the fixture, and the fix is to narrow the fixture, never to filter the count.
- **D-02: Each timeline fixture declares its complete expected finding set, and the whole-report count must match that set exactly.** One cause legitimately moves several facts: a +42 ms audio offset moves `timeline.av_offset`, the audio stream's `timeline.start`, and `container.mp4.edit_list`. Every member of a declared set beyond the first carries a written causal reason in the test. Rejected: engine-level attribution that demotes an explained effect to `info`; engineering every fixture down to a single firing check.
- **D-03: `timeline.start` reports a global absolute origin plus per-stream values relative to it.** One global-scoped measurement holds the file's earliest presentation time; each per-stream measurement is that stream's first presentation PTS minus the origin. Extends a doc-defined per-stream check with a global scope; `Scope::Kind::global` already exists.
- **D-04: `timeline.av_drift` splits into a rate check and a pattern check, and nothing else.** `timeline.av_drift` compares the rate in ms/min; `timeline.av_drift.pattern` compares the class exactly (`constant-offset` / `linear-drift` / `step` / `irregular`). End delta, step time, residual max and the K=32 trajectory are evidence. A pure constant offset never moves `av_drift` — only `av_offset` gates.

**Cadence on coarse timebases**

A shipped false positive was found during discussion: a 29.97 fps MP4 stream-copied to Matroska compares as `warn video.frame_rate.measured`, 29.970 vs 30.303 fps, under `--profile remux`. Matroska's 1 ms timebase stores frame intervals as 33/33/34 ms; Phase 4's D-07 takes 33 ms as the mode interval.

- **D-05: CFR/VFR is decided by grid conformance, and the measured rate is derived from the span.** The ideal interval is an exact rational (span divided by interval count); a stream is CFR when at least the existing 99.5% proportion of PTS sit within one tick of `first_pts + round(n x ideal)`. The reported rate comes from the span, not the mode interval, so NTSC-in-Matroska measures 29.973 against MP4's 29.970 — inside the shipped 0.1% tolerance. All integer/rational math, no floating point, thresholds still fixed named constants. Amends Phase 4's D-07 in `src/probe/cadence.{h,cpp}` and moves `video.frame_rate.measured` values on coarse-timebase files; recorded against D-07 rather than silently replacing it. Reversibility: costly.
- **D-06: `timeline.vfr_profile` bins are keyed on deviation from the stream's own grid, not on raw ticks.** Fixed fractional buckets (on-grid, one tick, one percent, 2x, 3x, longer) make the histogram comparable across containers/timebases. Doc 04's literal "bins in ticks" is not comparable. Reversibility: costly.
- **D-07: `timeline.av_drift` gates on rate only when the accumulated end delta also clears the 2 ms epsilon.** In a 1 ms timebase, packet timestamps round by up to 0.5 ms, so on a short clip the least-squares slope wanders past the 0.2 ms/min fail threshold with no real drift. Requiring both conditions makes rounding structurally unable to fire, while real drift still gates: 0.2 ms/min over the 10-minute reference file is exactly 2 ms, so the two conditions coincide there. Both constants stay fixed; the epsilon is the same one the pattern classifier uses. Reversibility: costly.
- **D-08: Detection thresholds ship as fixed named constants in v1.** The 250 ms discontinuity threshold, the gap rule's 2x nominal, K=32 and the 2 ms epsilon are all constants. Making one configurable requires fingerprint recording and mismatch-skip machinery, deferred. Severity/tolerance stay tunable per check as usual. Reversibility: reversible.

**Priming, and what `priming: unknown` promises**

Verified during discussion: for MP4 and Matroska the first AAC packet carries pts -1024 with `AV_PKT_DATA_SKIP_SAMPLES` = 1024 and `initial_padding` = 1024 — priming is available with no decode. An MPEG-TS remux of that same file carries no priming signal at all (`initial_padding` = 0, no side data). Neither does doc 04's own `-itsoffset 0.042` recipe: the offset rewrites the edit list, the priming signal disappears, and the first audio packet sits at 20.67 ms, so the fixture's stated +42 ms is not recoverable from it.

- **D-09: Phase 5 reads priming from what the demuxer already exposes without decoding.** `codecpar->initial_padding` and the first packet's `AV_PKT_DATA_SKIP_SAMPLES`, with the source recorded in evidence (`initial_padding` / `skip_samples` / `unknown`). The first audible sample is the first packet's presentation time plus its skip-samples count, which composes correctly with libav's own edit-list application rather than double-subtracting it. MP4 and Matroska become exact; MPEG-TS and the offset fixture stay `priming: unknown`. `PacketRecord` carries no side data today, so `PacketScan` must retain the first packet's skip-samples value per stream. Partly satisfies v2's `EXT-05`; Phase 6's `AUDIO-04` extends the same resolver. Reversibility: costly.
- **D-10: The fingerprint stores the raw offset, the adjusted offset and the priming state, and comparison runs on the basis both sides share.** Raw-to-raw whenever either side's priming is unknown, adjusted when both sides know it, with `priming: {state, source, samples}` recorded per side. Rejected: storing only the adjusted value and skipping on mismatch; marking unknown-priming values `estimated` so the tolerance widens 3x. Reversibility: one-way.
- **D-11: Unknown priming does not soften severity; the uncertainty is structured evidence.** The finding gates normally and carries priming as a structured object rather than a free-text hint, with the accept/tune/silence triple naming it. Rejected: demoting fail to warn while priming is unknown; refusing to report at all. Reversibility: reversible.
- **D-12: TIME-10 covers both arms, and the recoverable-priming fixture applies the shift to the video input.** Shifting the video leaves the audio edit list — and therefore the priming signal — intact, so that pair proves the exact arm (`source: skip_samples`). The unknown arm comes from the TS remux and from doc 04's own audio-side `-itsoffset` file, whose expected value is restated as the measured unadjusted offset with `priming: unknown`. Reversibility: reversible.

**The performance gates**

- **D-13: The ratio gates measure retired instruction counts; wall-clock is recorded and never asserted.** Instruction counts under valgrind/cachegrind are near-deterministic for a fixed binary and input and need no elevated perf permissions on hosted runners. `PERF-01`'s 3 s budget is measured in wall-clock and reported, never gated. Reversibility: reversible.
- **D-14: The gate is a ratchet against a committed baseline; the absolute ratios are measured, printed and tracked.** A regression against the recorded instruction-count baseline blocks the merge; the absolute `PERF-03` ratios are reported every run. Phase 4 recorded 43-53% parser overhead against a requirement that says under 10%, with an absolute cost near 1.5 ms — the ratio is high because the baseline pass is very cheap, not because the parser is slow. `PERF-03`'s parser clause is amended on that evidence. Rejected: treating under 10% as binding; gating only the 3 s budget. Reversibility: costly.
- **D-15: Performance history lives in a committed baseline ledger; CI stays read-only.** CI measures, compares, fails on regression and prints the pasteable replacement line; a human updates the file in a reviewed commit, exactly as `UPDATE_GOLDENS` works (Phase 2 D-12). Rejected: github-action-benchmark on a gh-pages branch; artifacts plus job summary only. Reversibility: reversible.
- **D-16: The reference file is Phase 4's on-demand generator promoted to 10 minutes at 1080p, gated on the designated leg only.** Same script, same gitignored directory, cached by content hash, never entering `tests/fixtures/` or `CORPUS_DIGEST.txt`. The gate runs on the designated `x64-linux` leg. Phase 4's A2 caveat stays recorded: `mpeg4` is the cheaper parser branch, so the measured ratio describes that branch. Reversibility: reversible.

### Claude's Discretion

- **The timeline check-ID roster**, approved at a roster checkpoint in the phase's first plan. Defaults: the duration-triple incoherence note gets its own id with the `state` semantic at `info` (Phase 4 D-10's precedent); TS 33-bit wrap events follow the same reasoning; whether `jitter` reports sigma and max deviation as one check with evidence or as two ids is the planner's call, bearing in mind D-01 counts each firing separately.
- **Primary-stream selection** for `av_offset`/`av_drift`: default is the first video stream that is not an attached picture, with one measurement per audio stream. An input with no audio, or no video, produces an explicit `skipped:` rather than silence.
- **Timecode source scope (TIME-11).** `tmcd` is reachable from the header pass; MPEG-2 GOP timecode and S12M packet side data need establishing against the pinned FFmpeg before the planner commits. Default is Phase 4's D-08 shape: build the precedence seam, wire the reachable sources, let the rest report `skipped:requires_decode` for Phase 7 to fill.
- **Trajectory storage for TIME-08** — evidence versus a new `Value` alternative — given that the compared values are rate and pattern (D-04) and evidence already survives the snapshot round trip.
- **The fixed-point representation of the least-squares fit and of jitter sigma.** No floating point and no float square root; the result must be byte-identical across platforms, and the overflow discipline is `core/rational.h`'s checked helpers.
- **How TS `discontinuity_indicator` is joined to demuxed packets** for doc 04's flagged (`info`) versus unflagged (gating) split, given `ts_scan` and `PacketScan` are separate passes.
- **Which streams `gaps`/`discontinuities` run on** (default: every stream carrying timestamps) and the last-frame duration reconstruction doc 04 section 1.3 prescribes.

### Deferred Ideas (OUT OF SCOPE)

- **Configurable detection thresholds** (the 250 ms discontinuity knob), together with the fingerprint recording and mismatch-skip machinery they would require — deferred past v1 by D-08.
- **The full `audio.priming` precedence chain** (`initial_padding` → MP4 `elst` / iTunSMPB / MKV `CodecDelay` → `unknown`) — `AUDIO-04`, Phase 6, extending D-09's resolver rather than replacing it.
- **`EXT-05`'s remaining scope** — probe-level priming for codecs/containers beyond what the demuxer surfaces directly. D-09 covers only the already-exposed sources.
- **Engine-level finding attribution** ("explained by") and any report-layer grouping of a mechanism finding with its effect finding — rejected for this phase by D-02.
- **`meta.tags` noise on cross-container remuxes** — a Phase 3 volatile-tag question, not a timeline one.
- **Optimising the parser pass to meet an absolute sub-10% ratio** — D-14 gates on regression instead.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TIME-01 | All timeline math runs on `{int64, AVRational}` with `AV_NOPTS_VALUE` as first-class `absent` | `core/rational.h` (Rational/Ticks types, checked helpers) already exists and is verified line-by-line below; `PacketRecord` already preserves `AV_NOPTS_VALUE` verbatim (`packet_scan.h:105-112`, verified) |
| TIME-02 | MPEG-TS 33-bit PTS wraparound, raw preserved, wrap vs backward discontinuity distinguished | doc 04 section 1.2's unwrap rule (quoted below); `ts_scan.h`'s PCR/`discontinuity_indicator` machinery verified below for the join point |
| TIME-03 | `timeline.start`/`timeline.duration`, duration triple cross-check | D-03 (global+per-stream scope), `Scope::Kind::global` verified in `model.h:69-80`; `container.mp4.edit_list`/`EditListEntry` verified in `bmff_scan.h:46-58` for mechanism evidence |
| TIME-04 | `dts_monotonic`, `pts_unique`, `gaps`, `discontinuities` | `SpanList`/`Span` value type verified in `value.h:56-67`; `compare/tol.cpp` cross-multiplication pattern verified for how magnitude comparisons stay integer-exact |
| TIME-05 | `jitter`/`vfr_profile`, CFR ≥99.5%, jitter `skipped:vfr` | D-05/D-06 amend Phase 4's `cadence.{h,cpp}` (read and quoted in full below); sqrt-avoidance technique for sigma proposed below |
| TIME-06 | `timeline.av_offset`, priming-adjusted | D-09/D-10/D-11; empirically re-verified this session under the LINKED FFmpeg 8.1 (see Priming Extraction below) |
| TIME-07 | 32-checkpoint least-squares `av_drift` | doc 04 section 3 (quoted in full below); overflow analysis with concrete magnitude bounds computed below |
| TIME-08 | Drift trajectory stored in fingerprint | `Measurement::evidence` round-trip verified in `snapshot.cpp:297-298,356-368` |
| TIME-09 | Unknown priming carries `priming: unknown`, offset unadjusted | D-09/D-10/D-11 |
| TIME-10 | Fixtures cover non-zero-priming path, not just unknown | D-12; LGPL-clean fixture recipes verified below |
| TIME-11 | `timeline.timecode`, `tmcd`/S12M/GOP, SMPTE string with drop-frame | Empirically verified this session under the LINKED FFmpeg 8.1: `tmcd` reachable via `AVStream::metadata["timecode"]`, no decode; S12M packet side data has no first-party demuxer producer in this FFmpeg's source tree; GOP timecode is exclusively `AV_FRAME_DATA_GOP_TIMECODE` (decode-only) — see Timecode Extraction below |
| DOC-04 | No-others clause | D-01/D-02; `count_non_pass` predicate verified verbatim in `test_video_yuvj.cpp:84-93` |
| PERF-01 | ≤3s metadata+timeline on 10-min 1080p reference, measured not gated | D-13; `tools/bench/parser_overhead.cpp` and `measure_parser_overhead.sh` read in full below, extension points identified |
| PERF-03 | Parser <10%, timeline <15%, amended to ratchet | D-14 |
| PERF-05 | CI-measured with regression tracking | D-15/D-16; valgrind/cachegrind availability investigated below (NOT preinstalled on GitHub's ubuntu-24.04 image — must be provisioned) |
</phase_requirements>

## Summary

This phase adds no new external dependency — every requirement is implemented purely on top of code Phases 2-4 already shipped (`core/rational.h`'s checked-arithmetic helpers, `probe/packet_scan.h`'s `PacketRecord`, `probe/cadence.h`'s pure `derive_cadence` function, `probe/ts_scan.h`'s PCR/continuity machinery, `probe/bmff_scan.h`/`ebml_scan.h`'s edit-list/codec-delay evidence, and the `Measurement`/`Finding` model's `evidence` field). The work is arithmetic and integration discipline, not new libraries.

Three findings from this session materially sharpen what CONTEXT.md's decisions already established, all obtained by compiling small standalone probes against the project's actual linked FFmpeg 8.1 static libraries (`build/x64-linux/vcpkg_installed/x64-linux/lib/libavformat.a`, libavformat 62.12.100) rather than trusting the pinned 9.0.1 `ffprobe` or training-data assumptions:

1. **MP4's `codecpar->initial_padding` field is `0` for AAC-in-MP4** — the padding is signaled *exclusively* through the first packet's `AV_PKT_DATA_SKIP_SAMPLES` side data (`start_skip=1024`). Matroska populates *both* `initial_padding=1024` on `codecpar` *and* the identical packet-level side data. D-09's resolver must therefore check packet-level `skip_samples` first (or take whichever is nonzero) — checking `initial_padding` alone will silently report MP4 as `priming: unknown` even though the signal is present and free.
2. **`AV_PKT_DATA_S12M_TIMECODE`** is set, in this exact FFmpeg 8.1 source tree, by exactly one file: `libavdevice/decklink_dec.cpp` (a live capture device, not a file demuxer). Grepping the full linked source tree confirms none of `mov.c`, `matroskadec.c`, or `mpegts.c` ever populate it. For this project's file-based MP4/MKV/TS fixtures, the S12M arm of TIME-11 is structurally unreachable and should report `skipped:requires_decode` (or simply never fire) rather than be built out as a real code path.
3. **MPEG-2 GOP timecode is decode-only.** `AV_FRAME_DATA_GOP_TIMECODE` exists in `libavutil/frame.h` but is populated on the decoded `AVFrame`, never as packet-level side data or stream metadata. A synthesized MPEG-2 file with `-timecode` embedded in its GOP header shows no timecode at any no-decode inspection point (format-level, per-stream metadata, or first-packet side data) under the linked 8.1 library.

The `av_drift` least-squares algorithm's overflow discipline needs more care than "use `core/rational.h`'s checked helpers" alone provides: for a realistic long-form input (a 2-hour file at a 90 kHz timebase), the `K·Σx²` term in the closed-form slope formula overflows `int64_t` by roughly two orders of magnitude even after zero-basing the video timeline at the first checkpoint. `rational.h` already establishes the pattern this needs (a 128-bit-safe multiply via `_mul128` on MSVC, `__builtin_mul_overflow`/`__int128` elsewhere) — Phase 5 must extend that pattern to full 128-bit *accumulation* (not just single-multiply overflow detection), not merely reuse `checked_mul`/`checked_add` naively.

`mpdecimate` — doc 04's own suggested VFR-fixture filter — is confirmed GPL-gated at the FFmpeg build-configuration level (`configure`: `mpdecimate_filter_deps="gpl"`), so it will not build under the Windows LGPL pin at all. The `decimate` filter (a different, LGPL-2.1+-licensed filter with no `gpl` dependency declared) and the `select`/`setpts` filter chain are both confirmed LGPL-clean alternatives, verified directly against this project's vendored FFmpeg 8.1 source tree.

`valgrind`/`cachegrind`, which D-13 requires for the instruction-count performance gate, is **not preinstalled** on GitHub's `ubuntu-24.04` runner image (confirmed against the image's own README) and is not installed in this development sandbox either — it is `apt-get install`-able (`valgrind 1:3.22.0-0ubuntu3` is a real candidate in this sandbox's apt cache) but the CI workflow must add an explicit install step; this cannot be assumed present.

**Primary recommendation:** build timeline analyzers as pure functions over the existing shared probe primitives (`PacketScan`, `cadence.h`, `ts_scan.h`, `bmff_scan.h`/`ebml_scan.h`) exactly as Phase 4's video analyzers did; extend `core/rational.h` with a small, MSVC-portable 128-bit-safe accumulator (mirroring its existing `_mul128`/`__builtin_mul_overflow` split) rather than trusting that 64-bit checked arithmetic alone is sufficient for the drift algorithm's sums of squares; store the K=32 trajectory in `Measurement::evidence` (no new `Value` variant needed — the round-trip already exists); and treat the S12M/GOP timecode arms of TIME-11 as `skipped:requires_decode` by design, not as gaps to close in this phase.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Timestamp unwrap/monotonicity/gap detection | Probe/Analyzer (libmediadiff core) | — | Pure integer math over already-demuxed packet records; no I/O, no decode |
| A/V drift least-squares fit | Probe/Analyzer (libmediadiff core) | — | Same tier as above; consumes video+audio packet timelines already in memory |
| Priming extraction (skip_samples/initial_padding) | Probe layer (`PacketScan` extension) | Analyzer (`timeline.av_offset`) | The raw signal must be captured during the one `av_read_frame` sweep (PROBE-08's single-sweep invariant); the analyzer only interprets already-captured data |
| Timecode extraction (`tmcd`) | Probe layer (`demux_header` pass, `AVStream::metadata`) | Analyzer (`timeline.timecode`) | `tmcd` resolution happens inside `avformat_find_stream_info` itself — no separate scan needed, unlike bmff_scan/ebml_scan's box-walk scanners |
| MPEG-TS 33-bit unwrap | Probe/Analyzer, joined with `ts_scan`'s `discontinuity_indicator` | — | Unwrap operates on `PacketScan`'s raw PTS/DTS; `ts_scan` is a separate pass over the same file whose `discontinuity_indicator` flag must be correlated by packet position, not re-derived |
| Performance measurement (instruction counts) | CI / build tooling (`tools/bench/`, `scripts/`) | — | Not part of `libmediadiff` at all; a measurement harness and a CI step, per D-13/D-15's explicit tier separation from the analyzer code itself |
| Fixture generation (LGPL-clean VFR/jitter/wrap recipes) | Build/CI tooling (`scripts/gen_corpus.sh`) | — | Never part of the shipped binary; must be LGPL-clean on the Windows pin specifically |

## Standard Stack

No new external dependency is introduced by this phase. Every requirement builds on libraries and primitives already vetted and linked in Phases 1-4:

### Core (already linked, reused)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| FFmpeg (libavformat/libavcodec) | 8.1, port-version 4 (linked; confirmed `libavformat 62.12.100` via `mediadiff --version` this session) | Demux/parse; source of all raw timestamps, side data, stream metadata this phase reads | Already the project's sole media I/O dependency (BUILD-03); Phase 5 adds no new FFmpeg feature flags |
| `core/rational.h` | in-tree | Overflow-checked int64 arithmetic, cross-multiplication tick comparison | Already the project's mandated "rational everywhere" primitive (PROJECT.md); this phase extends it, does not replace it |

### New capability, no new dependency
| Need | Implementation approach | Why not a library |
|------|-------------------------|--------------------|
| 128-bit-safe accumulation for least-squares sums | Extend `core/rational.h` with a small accumulator type using `__int128` (GCC/Clang) / manual 128-bit via `_mul128`+`_addcarry_u64` (MSVC), mirroring the file's own existing `checked_mul` split | A third-party bignum library (e.g. Boost.Multiprecision) is unjustified for a fixed, small (K=32-term) sum; the project already has the MSVC/`__int128` split pattern in-house |
| Integer/fixed-point square root for jitter sigma | Either avoid sqrt entirely by comparing variance (σ²) against squared thresholds, or implement a portable bit-doubling integer sqrt (see Code Examples) | No existing project dependency provides this; the algorithm is ~20 lines and the variance-comparison route needs no sqrt implementation at all |
| Valgrind/cachegrind for instruction-count perf gate | `apt-get install valgrind` as an explicit CI step (Linux designated leg only, per D-16) | A build/measurement tool, not a linked dependency — never enters the shipped binary |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `__int128`/manual-128-bit accumulator | Widen to `double` for the sums, round-trip to rational only at the end | Rejected outright — PROJECT.md's rational-everywhere/no-float determinism rule forbids float in the compared value path; a double accumulator over `Σx²` at 1e17-1e19 magnitudes loses integer precision exactly where correctness matters |
| Integer sqrt for sigma | `std::sqrt(double)` purely for **rendered text**, never for the compared/gated value | Legitimate and recommended for the human-readable "σ ≈ 0.3ms" string in `--explain`/TTY output — PROJECT.md explicitly allows floating milliseconds *in rendered output only*. The compared `Value` itself must still avoid float. |
| `apt-get install valgrind` in CI | Perf/`perf stat` (hardware performance counters) | Rejected — hosted CI runners frequently disable or restrict access to hardware perf counters (`perf_event_paranoid`), which is exactly why D-13 chose valgrind/cachegrind (simulated, not HW-counter-based) in the first place |

**Version verification:** `mediadiff --version` (this session) confirms the linked build is `libavcodec 62.28.100` / `libavformat 62.12.100`, matching `vcpkg.json`'s pinned `ffmpeg 8.1, port-version 4`. `apt-cache policy valgrind` (this sandbox) confirms `1:3.22.0-0ubuntu3` is a real, installable candidate — a proxy for Ubuntu-family runner installability, not a guarantee of the exact GitHub image's apt mirror state.

## Package Legitimacy Audit

Not applicable — this phase introduces no new external package, npm/pip/cargo dependency, or vcpkg manifest change. All work is additive C++ inside `src/probe/`, `src/analyzers/timeline/`, `src/core/`, plus CI/script changes (`.github/workflows/ci.yml`, `scripts/`). `apt-get install valgrind` is an Ubuntu-repository system package (not vcpkg-manifested, not shipped in the binary) and is out of scope for the vcpkg-based Package Legitimacy Gate.

## Architecture Patterns

### System Architecture Diagram

```
                     ┌─────────────────────────────────────────┐
                     │   DemuxSession::open (existing, Ph.3)    │
                     │   AVFMT_FLAG_GENPTS off, wall-clock cap  │
                     └───────────────────┬───────────────────────┘
                                          │
                     ┌────────────────────▼────────────────────┐
                     │   run_packet_scan (ONE av_read_frame     │
                     │   sweep, existing, extended THIS PHASE): │
                     │   - PacketRecord{pts,dts,duration,...}   │
                     │   - NEW: first-packet skip_samples per   │
                     │     stream (D-09), captured in-sweep     │
                     └──┬───────────┬──────────────┬────────────┘
                        │           │              │
              ┌─────────▼───┐ ┌────▼──────┐  ┌─────▼──────────┐
              │ ts_scan     │ │ bmff_scan │  │ ebml_scan       │
              │ (existing)  │ │ (existing)│  │ (existing)      │
              │ PCR, PID,   │ │ elst[]    │  │ CodecDelay,     │
              │ disc.       │ │ (mech.    │  │ SeekPreRoll     │
              │ indicator   │ │  evidence)│  │ (mech. evidence)│
              └─────────┬───┘ └────┬──────┘  └─────┬───────────┘
                        │          │               │
                        └──────────┼───────────────┘
                                   │
                     ┌─────────────▼──────────────────────────┐
                     │  NEW: src/analyzers/timeline/*.cpp       │
                     │  Pure functions over shared ProbeResults:│
                     │  - unwrap.{h,cpp}: 33-bit TS unwrap      │
                     │  - start_duration.cpp: TIME-01/02/03     │
                     │  - monotonic.cpp: dts_monotonic/         │
                     │    pts_unique/gaps/discontinuities       │
                     │  - jitter_vfr.cpp: consumes cadence.h's  │
                     │    derive_cadence (extended by D-05/D-06)│
                     │  - av_sync.cpp: av_offset + av_drift      │
                     │    (32-checkpoint least-squares, 128-bit │
                     │    -safe sums) + av_drift.pattern         │
                     │  - timecode.cpp: tmcd from stream         │
                     │    metadata; S12M/GOP → skipped           │
                     └─────────────┬──────────────────────────┘
                                   │
                     ┌─────────────▼──────────────────────────┐
                     │  Fingerprint.measurements[]              │
                     │  (evidence carries K=32 trajectory,       │
                     │   priming{state,source,samples}, etc.)    │
                     └─────────────┬──────────────────────────┘
                                   │
                     ┌─────────────▼──────────────────────────┐
                     │  compare/engine.cpp (existing, unchanged  │
                     │  dispatch) → Finding[] (tol/span/dist/    │
                     │  state semantics, all pre-existing)       │
                     └────────────────────────────────────────┘
```

### Recommended Project Structure
```
src/analyzers/timeline/
├── analyzers.h          # shared detail:: helpers (mirrors src/analyzers/video/analyzers.h)
├── unwrap.h / unwrap.cpp # TIME-02: 33-bit PTS/DTS unwrap, pure function over raw ticks
├── start_duration.cpp   # TIME-03: timeline.start (global+per-stream), timeline.duration triple
├── monotonic.cpp        # TIME-04: dts_monotonic, pts_unique, gaps, discontinuities
├── jitter_vfr.cpp        # TIME-05: jitter, vfr_profile (consumes probe/cadence.h)
├── av_sync.cpp           # TIME-06/07/08/09: av_offset, av_drift, av_drift.pattern, priming
└── timecode.cpp          # TIME-11: tmcd (reachable), S12M/GOP (skipped:requires_decode)

src/probe/
├── packet_scan.{h,cpp}   # EXTEND: PacketRecord or a sibling per-stream field for first-packet
│                         #   skip_samples (D-09) — captured inside the existing sweep, no 2nd read
└── cadence.{h,cpp}        # EXTEND: D-05 grid-conformance rule replaces D-07's exact-tick rule;
                           #   D-06 grid-relative histogram bins

src/core/
└── rational.h             # EXTEND: 128-bit-safe accumulator (Int128Accumulator or similar),
                            #   mirroring the existing checked_mul MSVC/__int128 split
```

### Pattern 1: 33-bit MPEG-TS PTS/DTS unwrap (doc 04 §1.2, normative)

**What:** Per elementary stream, before any other analysis, detect and correct 33-bit PCR/PTS wraparound.

**Verbatim from `claude_docs/04-timeline-analysis.md` §1.2** [CITED: claude_docs/04-timeline-analysis.md:16]:
> "Per elementary stream, before any other analysis: given consecutive raw PTS/DTS in 90 kHz, if `delta < -2^32` (half range), add `2^33` to the running unwrap offset; symmetric guard for backward jumps > half range (treated as genuine discontinuity, not wrap). Unwrapped values feed everything downstream; raw values are preserved in evidence. Wrap events themselves are recorded (`info`) — a candidate that wraps where baseline didn't usually means a start-offset change upstream."

**When to use:** Any `ContainerFamily::ts` stream, applied to `PacketScan`'s raw PTS/DTS before computing any other timeline statistic (gaps, monotonicity, cadence).

**Implementation sketch** (integer-only, matching `core/rational.h`'s checked-arithmetic discipline):
```cpp
// src/analyzers/timeline/unwrap.h — pure function, testable in isolation
struct UnwrapResult {
  std::vector<std::int64_t> unwrapped;  // same length as input
  std::int64_t wrap_events = 0;
};

// kPtsWrapModulus = 1LL << 33; kPtsWrapHalfRange = 1LL << 32 (named constants,
// never bare literals — matches this project's T-4-11 convention).
UnwrapResult unwrap_ts_timestamps(std::span<const std::int64_t> raw_values_in_read_order);
```
The `delta < -2^32` test must be done via `detail::checked_sub` (already in `rational.h`) between consecutive *sorted-by-original-position* raw values, exactly as `cadence.cpp`'s own interval computation already sorts an index view rather than mutating `packets` (see `cadence.cpp:85-105`, verified this session) — the identical "sort an index view, never the source array" discipline applies here.

### Pattern 2: The A/V drift algorithm (doc 04 §3, normative — the flagship check)

**Verbatim, `claude_docs/04-timeline-analysis.md` §3** [CITED: claude_docs/04-timeline-analysis.md:44-52]:
> "Inputs: presentation timelines of the primary video stream and each audio stream (audio priming-adjusted). For K = 32 checkpoints at video timeline fractions `k/K`:
> 1. `t_v(k)` = presentation time of the nearest video frame start; `t_a(k)` = presentation time of the nearest audio *sample boundary* (packet start + sample-accurate offset within the packet at the stream rate).
> 2. `offset(k) = t_a_aligned(k) − t_v(k)` where alignment picks the audio time covering the same media position (nearest-sample; audio granularity ≪ 1 ms makes interpolation unnecessary).
> 3. Least-squares line over `(t_v(k), offset(k))` → slope = **rate** (ms/min), intercept ≈ `timeline.av_offset`.
> 4. Pattern classification: residual max < ε (2 ms) → `constant-offset` if |slope| below tolerance else `linear-drift`; any single residual step > 3× ε with stable plateaus on both sides → `step` (report the step time). Otherwise `irregular` (rendered with the residual max).
> 5. Cross-file comparison gates on |rate_candidate − rate_baseline| and end-delta difference; the trajectory (K offsets) is stored in the fingerprint so `compare` against a snapshot retains full fidelity.
>
> Determinism: integer/rational inputs, fixed K, fixed ε ⇒ byte-identical results across platforms — this check must never itself jitter (idempotence guarantee)."

**Overflow analysis (computed this session, not in any source document):**

The closed-form least-squares slope is `slope = (K·Σxy − Σx·Σy) / (K·Σx² − (Σx)²)`, with `x = t_v(k)` and `y = offset(k)`, both as exact integer ticks of a common timebase.

- Zero-basing `x` at `t_v(0)` (i.e. `x'_k = t_v(k) − t_v(0)`) is **not optional** — it is mathematically free (linear regression slope is invariant to a constant shift in `x`; the intercept recovers via `c' = c − slope·x_0`) and is *required* to keep magnitudes bounded by the file's own *span* rather than by an arbitrary absolute PTS. This matters acutely for MPEG-TS, where raw (pre-unwrap or even post-unwrap-but-pre-zero-base) PTS values can be large (up to `2^33 − 1 ≈ 8.59×10^9` at 90 kHz before even considering span).
- Even *after* zero-basing, a realistic 2-hour file at a 90 kHz timebase has `x_max ≈ 2×3600×90000 = 6.48×10^8` ticks. Then `Σx²` over 32 terms is on the order of `32×(6.48×10^8)² ≈ 1.34×10^19`, and the full numerator term `K·Σx²` is `32×1.34×10^19 ≈ 4.3×10^20` — this **overflows `int64_t`** (max `≈9.22×10^18`) by roughly a factor of 46. This is true even for ordinary long-form content, not just adversarial input.
- **Conclusion:** the least-squares sums (`Σx`, `Σy`, `Σx²`, `Σxy`) must accumulate in genuine 128-bit integers, not merely pass through `rational.h`'s existing `checked_mul`/`checked_add` (which detect and *reject* 64-bit overflow — correct for defensive code, but would make the flagship check report "cannot determine" on ordinary 2-hour movies, which is unacceptable given false positives/negatives are both P0-class failures for this project).
- `core/rational.h`'s own `checked_mul` [VERIFIED: src/core/rational.h:39-56] already establishes the exact toolchain-portable pattern needed: `#if defined(_MSC_VER)` uses `_mul128(a, b, &high)` (a genuine 128-bit multiply intrinsic from `<intrin.h>`, already included at the top of this file) and checks `high` against the sign-extension of `low`; the `#else` branch uses `__builtin_mul_overflow`. **Recommendation:** extend this file with a small accumulator type that, on GCC/Clang, uses `__int128` directly for the running sums, and on MSVC, composes `_mul128` (for the multiply) with `_addcarry_u64` (for the 128-bit add) — the same conditional-compilation shape this file already uses, just carried one step further (accumulate, not just single-operation-overflow-detect). A final range check converts the 128-bit sums back to a rational slope via a single, checked, wide-to-int64 division only if the *result* fits (which it always will — slopes are ms/min-scale, tiny relative to the ticks that produced them).
- **Nearest-checkpoint search** (`t_v(k)` = nearest video frame start to a target fraction) must use `PacketScan`'s existing bound (`kMaxPacketsPerStream = 5,000,000`, [VERIFIED: src/probe/packet_scan.h:63]) — a binary search over the sorted-by-PTS index view (same "sort an index view" pattern as `cadence.cpp`) keeps this O(K log N) rather than O(K·N), consistent with PERF-01's 3-second budget.

**Trajectory storage (TIME-08):** store the K=32 `{t_v, t_a, offset}` triples (or just `offset` if `t_v` is reconstructible from `k/K` and the span) as a `Measurement::evidence` JSON array. This field already round-trips through snapshot read/write [VERIFIED: src/core/snapshot.cpp:297-298 (read: `measurement.evidence = m.at("evidence");`), 356-368 (write: `mj["evidence"] = m->evidence;`)] — no new `Value` alternative is needed, matching the Discretion item's own framing ("evidence already survives the snapshot round trip").

### Pattern 3: Priming extraction — resolver logic (D-09), corrected by this session's empirical finding

**Verified this session, under the LINKED FFmpeg 8.1** (compiled a standalone probe against `build/x64-linux/vcpkg_installed/x64-linux/lib/libavformat.a` — the exact static library `mediadiff` links — and ran it against AAC fixtures synthesized by the pinned 9.0.1 ffmpeg in MP4/MKV/TS):

```
=== MP4 ===  libavformat linked version: 62.12.100
stream 0: codec=aac initial_padding=0
audio pkt stream=0 pts=-1024 dts=-1024 skip_samples_side_data=present start_skip=1024 end_skip=0

=== MKV ===  libavformat linked version: 62.12.100
stream 0: codec=aac initial_padding=1024
audio pkt stream=0 pts=-23 dts=-23 skip_samples_side_data=present start_skip=1024 end_skip=0

=== TS ===  libavformat linked version: 62.12.100
stream 0: codec=aac initial_padding=0
audio pkt stream=0 pts=126000 dts=126000 skip_samples_side_data=absent
```

**This refines D-09:** CONTEXT.md's own wording ("for MP4 and Matroska the first AAC packet carries pts -1024 with `AV_PKT_DATA_SKIP_SAMPLES` = 1024 and `initial_padding` = 1024") is accurate for the *packet-level* side data on both containers, but **`codecpar->initial_padding` itself is `0` for MP4** — only Matroska populates it directly on `codecpar`. A resolver that checks `initial_padding` first and falls back to packet-level `skip_samples` only when `initial_padding == 0` will work correctly by accident (since MP4's packet-level value is still checked as the fallback), but a resolver that treats `initial_padding != 0` as the *only* signal to check, or that logs `source: initial_padding` only when this field is populated, will misreport MP4's source as `skip_samples`-only in evidence — which is in fact correct, since that is the *only* place MP4 signals it. **Recommendation: check first-packet `AV_PKT_DATA_SKIP_SAMPLES` first (it is the universally-present signal on both containers); fall back to `codecpar->initial_padding` only if no packet-level side data was captured.** Record `source: "skip_samples"` for both MP4 and MKV in the common case (both actually carry it), never assume `source: "initial_padding"` is the primary MP4 path.

```cpp
// src/analyzers/timeline/av_sync.cpp — sketch, not verbatim
struct PrimingResult {
  enum class Source { skip_samples, initial_padding, unknown } source;
  std::int64_t samples = 0;
};

PrimingResult resolve_priming(std::int64_t first_packet_skip_samples,  // 0 if absent (D-09 field, packet_scan.h extension)
                               std::int64_t codecpar_initial_padding) {
  if (first_packet_skip_samples > 0) {
    return {PrimingResult::Source::skip_samples, first_packet_skip_samples};
  }
  if (codecpar_initial_padding > 0) {
    return {PrimingResult::Source::initial_padding, codecpar_initial_padding};
  }
  return {PrimingResult::Source::unknown, 0};
}
```

**Composition with edit-list adjustment (no double-counting):** D-09's own text is precise — "the first audible sample is the first packet's presentation time plus its skip-samples count, which composes correctly with libav's own edit-list application rather than double-subtracting it." This session's data confirms why: MP4's first packet PTS is already `-1024` (libav's own edit-list-derived presentation-axis adjustment, *not* the raw demuxed value), so `first_audible = pts(-1024) + skip_samples(1024) = 0` — exactly the *first frame's* presentation time, which is correct. A resolver that instead tried to independently read the MP4 `elst` mechanism (via `bmff_scan`'s `EditListEntry`) and *also* apply skip_samples would double-count; `bmff_scan`'s `container.mp4.edit_list` measurement should be cited in evidence as the *mechanism*, never re-applied arithmetically by the timeline analyzer.

### Pattern 4: Timecode extraction (TIME-11) — what's reachable without decode

**Verified this session, under the LINKED FFmpeg 8.1**, using an MP4 with a QuickTime `tmcd` track (`ffmpeg -timecode 00:00:10:00`):

```
libavformat: 62.12.100
format-level timecode tag: (none)
stream 0: codec_type=0 codec_id=12 codec_name=mpeg4 timecode_tag=00:00:10:00
stream 1: codec_type=2 codec_id=0 codec_name=none timecode_tag=00:00:10:00
stream 1 first packet: s12m_timecode_side_data=absent size=0
```
(`codec_tag=0x64636d74` decoded to ASCII `tmcd`; `codecpar->codec_id` is `AV_CODEC_ID_NONE` — this FFmpeg version has no dedicated codec ID for the tmcd track, it is carried purely via the fourcc tag and 20-byte extradata.)

**Finding:** the MOV/MP4 demuxer resolves the tmcd track's starting timecode *during* `avformat_find_stream_info` itself (it decodes the trivial 4-byte frame-count sample internally, as part of header-parse bookkeeping — not via `avcodec_send_packet`/`avcodec_receive_frame`) and publishes it as a plain string under the key `"timecode"` in **both** the video stream's and the `tmcd` stream's own `AVStream::metadata` dictionary. This is genuinely reachable from the existing header pass (`Pass::demux_header`) with **zero** additional decode calls — `av_dict_get(stream->metadata, "timecode", NULL, 0)`.

**S12M packet side data — verified unreachable for this project's containers.** Grepping the vendored FFmpeg 8.1 source tree (`vcpkg/buildtrees/ffmpeg/x64-linux-rel/src`) for every call site that *sets* `AV_PKT_DATA_S12M_TIMECODE` [VERIFIED: vcpkg/buildtrees/ffmpeg/x64-linux-rel/src, grep session]:
```
libavcodec/decode.c:1536      { AV_PKT_DATA_S12M_TIMECODE, AV_FRAME_DATA_S12M_TIMECODE },   // decode-time packet→frame side-data map, not a producer
libavformat/framecrcenc.c:99  case AV_PKT_DATA_S12M_TIMECODE:                                // dump/framecrc tooling only
libavformat/dump.c:523        case AV_PKT_DATA_S12M_TIMECODE:                                // dump/framecrc tooling only
libavdevice/decklink_dec.cpp:862   av_packet_new_side_data(&pkt, AV_PKT_DATA_S12M_TIMECODE, size);  // the ONLY real producer
```
None of `libavformat/mov.c`, `matroskadec.c`, or `mpegts.c` ever call `av_packet_new_side_data(..., AV_PKT_DATA_S12M_TIMECODE, ...)`. The only producer in the entire linked source tree is `libavdevice/decklink_dec.cpp` — a live capture device this project neither links (`libavdevice` is not in `vcpkg.json`'s feature list [VERIFIED: vcpkg.json:10-13 — features are `["avcodec","avformat","swscale","swresample","dav1d","zlib"]`, no `avdevice`]) nor could plausibly use for file-based comparison. **Recommendation: do not build a real S12M code path this phase; register the check id (if the roster includes one) with a permanent `skipped:requires_decode`-equivalent or simply omit an S12M-specific check, and note in its `docs/checks/<id>.md` explain text that this source requires a device demuxer not linked by this build.**

**MPEG-2 GOP timecode — verified decode-only.** A synthesized MPEG-2 file with `-timecode 00:00:10:00` (both as raw `.mpg` and muxed to `.ts`) shows: no format-level tag, no per-stream metadata tag, and no packet-level side data at any no-decode inspection point, under both the system ffprobe and the linked 8.1 library directly. `AV_FRAME_DATA_GOP_TIMECODE` exists in `libavutil/frame.h` [VERIFIED: build/x64-linux/vcpkg_installed/x64-linux/include/libavutil/frame.h:125] but is, by its own name and FFmpeg's established packet-vs-frame side-data convention, populated only on `AVFrame` after `avcodec_receive_frame` — i.e. decode-only, exactly matching CONTEXT.md's Discretion item's own expectation ("MPEG-2 GOP timecode ... need establishing"). **This confirms: MPEG-2 GOP timecode belongs to Phase 7 (decode pass), reported `skipped:requires_decode` in Phase 5**, matching Phase 4's D-08 precedent for `video.closed_captions`/VIDEO-11.

**Drop-frame rendering:** SMPTE drop-frame timecodes are conventionally rendered with a semicolon before the frame field (`HH:MM:SS;FF`) instead of a colon (`HH:MM:SS:FF`). FFmpeg's own `av_timecode_make_string`/the tmcd metadata string already follows this convention in the string it publishes (confirmed by this session's own fixture using a non-drop-frame rate, which rendered `00:00:10:00` with all colons — a drop-frame-rate fixture, e.g. 29.97 NTSC drop-frame, would need re-verification if the planner wants to assert the semicolon form specifically; not verified this session, flagged in Assumptions Log).

### Pattern 5: TS `discontinuity_indicator` join (Claude's Discretion item)

`ts_scan` and `PacketScan` are genuinely separate passes over the same MPEG-TS file [VERIFIED: src/probe/pass.h:34-42 — `Pass::packet_scan` and `Pass::ts_scan` are distinct enumerators; ProbeResults holds `std::optional<PacketScanResult> packet_scan` and `std::optional<TsScanResult> ts` as two independently-populated fields]. `ts_scan.h`'s `PidStats` [VERIFIED: src/probe/ts_scan.h:69-76] tracks `cc_discontinuities` per PID but does not record *byte offset or PTS* for each discontinuity occurrence at the packet level needed to correlate with a specific PES packet's presentation time (only `first_cc_error_offset` is tracked, and only for CC *errors*, not `discontinuity_indicator` occurrences specifically). **This is a genuine integration gap the planner must close**: either (a) extend `TsScanResult`/`PidStats` with a `std::vector<std::int64_t> discontinuity_indicator_offsets` per PID (byte offsets where the flag was seen set), then correlate against `PacketScan`'s packet `pos` field [VERIFIED: src/probe/packet_scan.h:110 — `PacketRecord::pos`] which already carries the byte offset of each packet, or (b) have `timeline.discontinuities` re-derive the flag directly from raw TS bytes itself (duplicating `ts_scan`'s adaptation-field parsing, which the project's own established pattern — one scanner per binary grammar — argues against). **Recommendation: extend `ts_scan.h`, not duplicate its parsing** — this is the same "declare the seam ahead of the implementation" pattern the project already used for `Pass::parser_scan` a phase early [per STATE.md: "Declare the seam ahead of the implementation ... D-09's priming source field continues it"].

### Anti-Patterns to Avoid

- **Re-deriving the CFR/VFR classification independently in `timeline.jitter`/`timeline.vfr_profile`:** both must consume `probe/cadence.h`'s `derive_cadence` (already the shared primitive per PROBE-10, extended by D-05/D-06 this phase) — never a second, parallel cadence computation. `cadence.h`'s own header comment already names this project's second consumer explicitly [VERIFIED: src/probe/cadence.h:13-16].
- **Computing `float`/`double` anywhere in the compared `Value` path** for drift slope, jitter sigma, or any gating decision — PROJECT.md's rational-everywhere rule is absolute for compared values; floating point is permitted *only* in rendered text (TTY/Markdown/`--explain`), never in what a comparator reads.
- **Trusting `checked_mul`/`checked_add` alone to make the drift algorithm's sums "safe"** — as shown above, these functions correctly *detect* 64-bit overflow but returning `false` (⇒ `insufficient_data`) on an ordinary 2-hour file is itself a defect, not a safety net. Genuine 128-bit accumulation is required, not merely overflow detection.
- **Applying MP4 edit-list adjustment and skip_samples priming adjustment both, independently** — see Pattern 3's double-counting warning.
- **Assuming `mpdecimate` is available** for VFR fixture generation — confirmed GPL-gated; will silently be absent (or fail to configure) under the `win64-lgpl` Windows pin.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| CFR/VFR classification | A second interval-statistics pass inside `timeline.jitter`/`vfr_profile` | `probe/cadence.h::derive_cadence`, extended by D-05/D-06 | PROBE-10's shared-primitive rule; this file's header comment already names Phase 5 as its designed second consumer |
| Overflow-checked scalar arithmetic | Ad hoc `if (a > INT64_MAX / b)` checks scattered per call site | `core/rational.h::detail::checked_mul/add/sub/negate/div` | Already the project's single source of truth for this, with an MSVC-specific `_mul128` path already solved |
| 128-bit accumulation for least-squares sums | A bespoke bignum type per analyzer file | One shared extension to `core/rational.h`, following its existing MSVC/GCC-Clang split | Keeps the "no libav header outside src/probe/" and "no float in compared values" invariants centrally enforced, and avoids re-solving the MSVC `__int128`-absence problem per call site |
| Integer square root | Newton's-method-from-scratch per check | Either avoid sqrt entirely (compare variance, not sigma, against squared thresholds) or one shared `isqrt` helper in `core/rational.h` if a rendered/compared sigma value is truly required | Sqrt-avoidance is strictly simpler and more obviously correct; if genuinely needed, one shared implementation avoids drift between jitter's sigma and any future check needing the same primitive |
| TS PSI/adaptation-field re-parsing for `discontinuity_indicator` correlation | A second byte-level TS walker inside `timeline.discontinuities` | Extend `ts_scan.h`'s existing bounded walker with an offset-recording field, joined against `PacketScan::pos` | `ts_scan.h` is already the project's one hand-rolled, security-audited TS parser (PROBE-06/07); a second one duplicates the exact attacker-controlled-length parsing risk PROBE-07's carve-out logic exists to get right |

**Key insight:** every "don't hand-roll" item in this phase is really the same rule stated five ways — this project has already built the shared primitive (cadence, checked arithmetic, the one TS scanner, the one MP4/MKV box walker); Phase 5's job is to *extend* those primitives, never to grow a second implementation beside them.

## Common Pitfalls

### Pitfall 1: Trusting `codecpar->initial_padding` as the primary MP4 priming signal
**What goes wrong:** A resolver that only checks `initial_padding` reports `priming: unknown` for every MP4 file, even though the signal is present.
**Why it happens:** The container's actual behavior (MP4 signals padding only via packet-level `AV_PKT_DATA_SKIP_SAMPLES`, not via `codecpar`) contradicts the natural assumption that "the codec parameters field" is the primary source, and contradicts even this project's own CONTEXT.md wording, which states both fields are populated for MP4 without distinguishing which one actually carries the nonzero value.
**How to avoid:** Check packet-level `skip_samples` first (verified nonzero on both MP4 and MKV); fall back to `initial_padding` only if absent. See Pattern 3.
**Warning signs:** A TIME-10 "recoverable priming" MP4 fixture (D-12) reporting `priming: unknown` when it should report `source: skip_samples`.

### Pitfall 2: Computing the least-squares sums directly in `int64_t` without zero-basing or 128-bit accumulation
**What goes wrong:** `av_drift` silently reports `insufficient_data` (if using checked arithmetic that correctly detects overflow) or produces a wrapped/garbage slope (if using unchecked arithmetic) on ordinary multi-hour content at a fine timebase.
**Why it happens:** K=32 is small, so it's easy to assume the sums stay small; they don't — `Σx²` scales with the *square* of the file's span in ticks, and a 90 kHz timebase over hours of content produces very large tick counts.
**How to avoid:** Zero-base `x` at the first checkpoint's `t_v`; accumulate `Σx, Σy, Σx², Σxy` in a genuine 128-bit integer (not just overflow-checked 64-bit).
**Warning signs:** A drift unit test using a short (seconds-long) synthetic timeline passes, but a fixture built from the 10-minute PERF-01 reference file (or longer) reports `insufficient_data` or a nonsensical rate.

### Pitfall 3: Assuming `mpdecimate` builds everywhere
**What goes wrong:** A VFR fixture recipe using `mpdecimate` works on the Linux/macOS `--enable-gpl` pinned ffmpeg used for local fixture generation, then silently produces nothing (or the whole `gen_corpus.ps1` run fails) on the Windows `win64-lgpl` pin — exactly the class of defect Phase 4 already hit twice (`tinterlace`, then `interlace`; quick tasks 260914-ryu/t47).
**Why it happens:** `configure`'s `mpdecimate_filter_deps="gpl"` is not visible from the filter's mere existence in a locally-available ffmpeg binary; a developer testing only on Linux never observes the gate.
**How to avoid:** Use the `decimate` filter (verified LGPL-2.1+, no `gpl` dependency in `configure`) or a `select`-filter-based deterministic frame-drop pattern (see Fixture Recipes below) instead.
**Warning signs:** A fixture recipe review that only checks the filter's source-file license header, not `configure`'s `_deps="gpl"` declarations — the header can be LGPL while the filter is still gated (not the case for mpdecimate specifically, which is GPL-headed *and* GPL-gated, but a general trap for other filters).

### Pitfall 4: Assuming valgrind/cachegrind is present on the CI runner
**What goes wrong:** D-13's instruction-count gate silently no-ops or the CI step errors on a fresh runner.
**Why it happens:** Developer sandboxes and some CI images bundle valgrind by default; GitHub's `ubuntu-24.04` hosted runner image does not (confirmed against the image's own README this session).
**How to avoid:** Add an explicit `apt-get install -y valgrind` (or equivalent) step to the designated leg's CI job, gated the same way the existing `MEDIADIFF_DESIGNATED_LEG` machinery gates other x64-linux-only steps.
**Warning signs:** A CI run where the perf gate step is green only because it never actually ran cachegrind (silently skipped due to a missing binary, if the script doesn't fail loudly on `command not found`).

### Pitfall 5: Double-counting priming adjustment against MP4 edit-list adjustment
**What goes wrong:** `timeline.av_offset` reports an offset that's off by exactly the priming sample count, in the direction of over-correction.
**Why it happens:** `bmff_scan`'s `container.mp4.edit_list` measurement and libav's own automatic edit-list application (which already shifts `AVStream::start_time` and packet PTS onto the presentation axis) are two views of the *same* mechanism; applying skip_samples priming adjustment on top of an already-edit-list-adjusted PTS, when the edit list *itself* encodes the same priming trim, double-corrects.
**How to avoid:** Confirmed this session — MP4's first packet PTS is already `-1024` (the edit-list-adjusted presentation-axis value), and `first_audible = pts + skip_samples = -1024 + 1024 = 0`, which is correct. Do not additionally consult `bmff_scan`'s `EditListEntry::media_time` to compute a second, independent adjustment — cite it only as mechanism evidence.
**Warning signs:** A TIME-10 fixture's measured `av_offset` differing from the expected value by exactly the priming sample count converted to time.

### Pitfall 6: Building the S12M timecode arm as if a real code path exists
**What goes wrong:** Time spent building packet-side-data extraction logic for `AV_PKT_DATA_S12M_TIMECODE` that can never fire on any fixture this project can legitimately generate (MP4/MKV/TS only, no DeckLink capture device linked).
**Why it happens:** Doc 04's check table lists `AV_PKT_DATA_S12M_TIMECODE`/GOP timecode alongside `tmcd` as if all three are equally reachable; they are not.
**How to avoid:** Confirmed by source-tree grep this session — only `libavdevice/decklink_dec.cpp` ever sets this side data, and `libavdevice` isn't even linked (`vcpkg.json`'s ffmpeg feature list excludes it). Register (if desired) but never exercise this code path with a real trigger fixture; document its unreachability directly in `docs/checks/timeline.timecode.md`.
**Warning signs:** DOC-03's "every check needs a triggering fixture" gate ([VERIFIED: tests/integration/test_doc03_coverage.cpp:1-9] — mirrors DOC-04's own no-others discipline) failing because no real trigger exists for an S12M-specific check id — a sign the roster should not have split S12M into its own separately-triggerable id in the first place.

## Code Examples

### 33-bit TS unwrap (integer-only, matches `core/rational.h` discipline)
```cpp
// Source: derived from claude_docs/04-timeline-analysis.md §1.2 (quoted above)
// and core/rational.h's own checked_sub pattern (verified src/core/rational.h:66-75)
inline constexpr std::int64_t kTsPtsWrapModulus = std::int64_t{1} << 33;   // 2^33
inline constexpr std::int64_t kTsPtsWrapHalfRange = std::int64_t{1} << 32; // 2^32

UnwrapResult unwrap_ts_timestamps(std::span<const std::int64_t> raw_in_read_order) {
  UnwrapResult result;
  result.unwrapped.reserve(raw_in_read_order.size());
  std::int64_t offset = 0;
  std::optional<std::int64_t> prev_raw;
  for (std::int64_t raw : raw_in_read_order) {
    if (prev_raw) {
      std::int64_t delta = 0;
      if (detail::checked_sub(raw, *prev_raw, &delta)) {
        if (delta < -kTsPtsWrapHalfRange) {
          std::int64_t new_offset = 0;
          if (detail::checked_add(offset, kTsPtsWrapModulus, &new_offset)) {
            offset = new_offset;
            ++result.wrap_events;
          }
          // else: overflow on offset itself -- pathological input, report
          // insufficient_data upstream rather than silently wrapping.
        }
        // delta > +kTsPtsWrapHalfRange: symmetric backward-jump guard,
        // treated as a genuine discontinuity, NOT a wrap (doc 04 §1.2) --
        // offset is NOT adjusted in this branch.
      }
    }
    std::int64_t adjusted = 0;
    detail::checked_add(raw, offset, &adjusted);  // check real code: handle false
    result.unwrapped.push_back(adjusted);
    prev_raw = raw;
  }
  return result;
}
```

### Variance-only jitter comparison (avoids integer sqrt entirely)
```cpp
// If the check compares candidate vs baseline sigma via tolerance, but the
// STORED/COMPARED value can instead be variance (sigma^2), no sqrt is ever
// needed for the gating decision -- only for optional rendered text.
// variance = Σ(interval - mean)^2 / N, computed as an EXACT rational
// (num/den) via checked_mul/checked_sub/checked_add throughout -- never
// divided until the final comparison, matching compare_ticks' own
// cross-multiplication-not-division discipline (core/rational.h:131-171,
// verified this session).
//
// Rendered text ONLY (never the compared Value): 
//   double approx_sigma_ms = std::sqrt(variance_num_ms2 / double(variance_den));
//   fmt::format("sigma ~= {:.2f} ms", approx_sigma_ms);
// This is explicitly sanctioned by PROJECT.md: "floating milliseconds
// appear only in rendered output."
```

### 128-bit-safe least-squares accumulation (extends `core/rational.h`)
```cpp
// Source: extends the pattern already in core/rational.h's checked_mul
// (verified src/core/rational.h:39-56) one step further: accumulation,
// not just single-multiply overflow detection.
#if defined(_MSC_VER)
struct Int128Accum {
  std::int64_t hi = 0;
  std::uint64_t lo = 0;
  unsigned char carry_ = 0;
  void add_product(std::int64_t a, std::int64_t b) {
    std::int64_t high = 0;
    std::uint64_t low = static_cast<std::uint64_t>(_mul128(a, b, &high));
    carry_ = _addcarry_u64(0, lo, low, &lo);
    hi += high + carry_;
  }
};
#else
struct Int128Accum {
  __int128 value = 0;
  void add_product(std::int64_t a, std::int64_t b) {
    value += static_cast<__int128>(a) * static_cast<__int128>(b);
  }
};
#endif
// A final, single checked narrowing (range-checked, not silently
// truncating) converts back to int64_t/Rational once the slope's actual
// magnitude (always tiny -- ms/min scale) is known to fit.
```

## State of the Art

| Old Approach (Phase 4, D-07) | Current Approach (Phase 5, D-05) | When Changed | Impact |
|--------------|------------------|--------------|--------|
| CFR/VFR by exact-tick equality (epsilon=0) against the *mode interval* | CFR/VFR by grid conformance: exact rational ideal interval (span/count), 99.5% within 1 tick of `first_pts + round(n·ideal)`, rate derived from span | This phase, D-05, amending `src/probe/cadence.{h,cpp}` | Fixes a real shipped false positive (29.97 fps MP4→MKV remux comparing `warn`); moves `video.frame_rate.measured` values on every coarse-timebase (e.g. 1ms Matroska) fixture — committed goldens carrying such values must be re-baselined |
| `timeline.vfr_profile` bins in raw ticks | Bins keyed on deviation-from-grid (on-grid, 1 tick, 1%, 2x, 3x, longer) | This phase, D-06 | Makes the histogram comparable across containers with different timebases (same content bins identically in MP4 and Matroska) |

**Deprecated/outdated:** Phase 4's exact-tick CFR rule (D-07 in `04-CONTEXT.md`) is amended, not replaced outright — its zero-epsilon exact-match logic remains correct for same-timebase comparisons; D-05 only changes what "matching" means when the axis is a coarse (e.g. 1ms) timebase incapable of representing the true interval exactly.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Drop-frame timecode renders with a semicolon (`HH:MM:SS;FF`) via FFmpeg's own tmcd metadata string, matching SMPTE convention | Pattern 4 (Timecode Extraction) | Not verified this session (only a non-drop-frame fixture was tested); if wrong, TIME-11's drop-frame flag extraction needs a different parse of the metadata string, or must derive drop-frame status from the timecode rate itself (29.97/59.94) rather than string punctuation |
| A2 | `apt-cache policy valgrind`'s candidate (`1:3.22.0-0ubuntu3`) in this sandbox is representative of what GitHub's `ubuntu-24.04` runner's apt mirror will resolve at CI-run time | Common Pitfalls #4, Environment Availability | Low risk — both are Ubuntu-family systems using the standard archive; a pin-specific version mismatch would only affect instruction-count exact-reproducibility across valgrind versions over time, which D-15's ledger-based ratchet already tolerates by design (a version bump is a deliberate, reviewed baseline update) |
| A3 | The `select`/`setpts` (or `decimate`) filter chain can reproduce a VFR profile with the specific on-grid/2x/3x bin distribution D-06's histogram wants, deterministically | Fixture recipe guidance (Common Pitfalls #3, Don't Hand-Roll) | Medium — not fixture-tested this session (only the license/build-gate status was verified via `configure` and source license headers, not an actual generated file's `vfr_profile` measurement); the planner should generate and inspect an actual `select`-filtered fixture with the built `mediadiff` binary (once timeline analyzers exist) before finalizing the recipe |
| A4 | TS `ts_scan.h` needs an added byte-offset-per-discontinuity-indicator field to correlate with `PacketScan::pos`, as opposed to some existing-but-unread field already sufficing | Pattern 5 (TS discontinuity join) | Low — directly read `ts_scan.h`'s full struct definitions this session; `PidStats` genuinely has no per-occurrence offset list for `discontinuity_indicator` specifically (only `first_cc_error_offset` for CC *errors*), so this is a structural gap, not a research oversight — but the planner should re-confirm scope (maybe only `info`-severity aggregate counts are needed per doc 04, not full offset lists) before committing to the exact struct shape |
| A5 | Valgrind/cachegrind produce near-deterministic instruction counts run-to-run on a shared CI runner (the premise D-13 itself rests on) | Validation Architecture, Environment Availability | This is a CONTEXT.md-locked premise (D-13), not re-verified empirically this session (no CI access) — genuinely no way to falsify this from a local sandbox; treat as the locked decision's own basis, not a fresh research claim |

**If this table is empty:** N/A — see rows above.

## Open Questions

1. **Exact drop-frame timecode string format from this FFmpeg version's tmcd metadata resolution.**
   - What we know: non-drop-frame renders as `HH:MM:SS:FF` (verified).
   - What's unclear: whether a genuinely drop-frame-rate (29.97/59.94 NTSC) `tmcd` fixture renders `HH:MM:SS;FF` from this same no-decode metadata path, or whether the semicolon only appears from a different (decode-involving) code path.
   - Recommendation: the planner's first timeline plan should generate a drop-frame `tmcd` fixture and inspect the raw metadata string with the same standalone-probe technique used this session, before committing to a parse implementation.

2. **Exact roster scope for TS wrap-event and duration-triple-incoherence check ids** (Claude's Discretion item, left to the roster checkpoint).
   - What we know: doc 04 and D-01/D-10 (Phase 4 precedent) establish the *pattern* (`state` semantic, `info` severity, own check id).
   - What's unclear: the literal id strings, left to the phase's first-plan roster checkpoint per CONTEXT.md's own framing.
   - Recommendation: follow Phase 4's `video.hdr.coherence` naming precedent (`timeline.<family>.coherence` or similar) at the roster checkpoint.

3. **Whether `timeline.gaps`/`discontinuities` should run on subtitle/data streams that carry timestamps but no meaningful "frame" semantics.**
   - What we know: CONTEXT.md's Discretion item defaults to "every stream carrying timestamps."
   - What's unclear: whether a subtitle stream's typically-sparse, intentionally-irregular PTS pattern would trigger false-positive `gaps` findings under the same 2×-nominal rule tuned for audio/video.
   - Recommendation: verify with an actual subtitle-track fixture before locking `gaps`' stream-scope default; consider excluding `Scope::Kind::subtitle` from the default scope if early fixture testing shows false positives, recording the exclusion as a roster-checkpoint decision.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| FFmpeg (linked, vcpkg 8.1) | All timeline analyzers | ✓ (this sandbox) | libavformat 62.12.100 / libavcodec 62.28.100 | — |
| FFmpeg (pinned, corpus generation) | `gen_corpus.sh` fixture recipes | ✓ (this sandbox) | 9.0.1 (`.ffmpeg-pinned/linux-x86_64/ffmpeg`) | — |
| `mediadiff` build (x64-linux) | Empirical verification against the real binary | ✓ (this sandbox) | 0.1.0 | — |
| valgrind/cachegrind | D-13's instruction-count perf gate | ✗ (this sandbox); ✗ confirmed on GitHub `ubuntu-24.04` runner image (checked against the image's own README) | `apt` candidate `1:3.22.0-0ubuntu3` (this sandbox's Ubuntu/Pop!_OS repo) | `apt-get install valgrind` as an explicit CI step on the designated leg only |
| GitHub `ubuntu-24.04` runner internet/apt access for the perf CI step | D-16 (designated-leg-only perf gate) | Not directly testable from this sandbox | — | Standard GitHub-hosted-runner apt access; no fallback needed if this assumption holds (industry-standard for GH Actions Ubuntu images) |

**Missing dependencies with no fallback:**
- None — valgrind's absence has a clean, low-risk fallback (an explicit `apt-get install` CI step).

**Missing dependencies with fallback:**
- valgrind/cachegrind: install explicitly in CI (see above); this is not optional to plan for, since D-13's entire perf-gate design depends on it being present at the exact designated-leg step.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 3.15.3 (vcpkg-pinned, confirmed linked: `libCatch2.a` present in `build/x64-linux/vcpkg_installed/x64-linux/lib/`) |
| Config file | CTest via CMake presets — no separate Catch2 config file; `tests/unit/CMakeLists.txt` / `tests/integration/CMakeLists.txt` register targets |
| Quick run command | `ctest --preset x64-linux -R "unit\\.timeline" --output-on-failure` (unit tests only, fast iteration) |
| Full suite command | `ctest --preset x64-linux --output-on-failure` (matches `.planning/config.json`'s own `test_command`) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TIME-01 | `AV_NOPTS_VALUE` stays `absent`, never coerced, through every timeline check | unit | `ctest --preset x64-linux -R unit.timeline_absent -x` | ❌ Wave 0 |
| TIME-02 | 33-bit TS unwrap correctness (wrap vs backward-discontinuity distinction) | unit | `ctest --preset x64-linux -R unit.ts_unwrap -x` | ❌ Wave 0 |
| TIME-03 | `timeline.start` global+per-stream scope; duration triple incoherence | integration | `ctest --preset x64-linux -R integration.timeline_start_duration -x` | ❌ Wave 0 |
| TIME-04 | `dts_monotonic`/`pts_unique`/`gaps`/`discontinuities` trigger+clean pairs | integration | `ctest --preset x64-linux -R integration.timeline_monotonic -x` | ❌ Wave 0 |
| TIME-05 | CFR/VFR grid conformance (D-05); jitter sigma comparison | unit + integration | `ctest --preset x64-linux -R "unit.cadence\|integration.timeline_jitter" -x` | Partially — `cadence.h` unit tests exist from Phase 4, need D-05 amendment coverage; jitter integration tests ❌ Wave 0 |
| TIME-06/09 | `av_offset` priming-adjusted / `priming: unknown` | integration | `ctest --preset x64-linux -R integration.timeline_av_offset -x` | ❌ Wave 0 |
| TIME-07/08 | 32-checkpoint least-squares drift, rate/pattern/trajectory | unit (synthetic timelines) + integration | `ctest --preset x64-linux -R "unit.av_drift\|integration.timeline_av_drift" -x` | ❌ Wave 0 — **highest-priority Wave 0 gap**: overflow-safety (128-bit accumulator) needs a dedicated unit test with a synthetic multi-hour-scale timeline, not just short fixtures |
| TIME-10 | Recoverable vs unknown priming fixture pair | integration | `ctest --preset x64-linux -R integration.timeline_priming -x` | ❌ Wave 0 |
| TIME-11 | `tmcd` presence+value; S12M/GOP `skipped:requires_decode` | integration | `ctest --preset x64-linux -R integration.timeline_timecode -x` | ❌ Wave 0 |
| DOC-04 | No-others clause | integration | Extend `count_non_pass` pattern from `tests/integration/test_video_yuvj.cpp` into new timeline fixture tests | Pattern exists [VERIFIED: tests/integration/test_video_yuvj.cpp:84-93], no timeline-specific file yet |
| PERF-01/03/05 | Wall-clock recorded (not gated); instruction-count ratchet gated on designated leg | manual-only (D-13) + CI script | `scripts/measure_parser_overhead.sh` (existing) extended with a valgrind/cachegrind leg; new `scripts/measure_timeline_instructions.sh` or similar | ❌ Wave 0 — needs new script, new baseline ledger file, and `tools/bench/` extension for a cachegrind-driven leg alongside the existing wall-clock leg |

### Sampling Rate
- **Per task commit:** `ctest --preset x64-linux -R "unit.timeline\|unit.cadence\|unit.rational" --output-on-failure` (fast subset covering the arithmetic primitives every timeline check depends on)
- **Per wave merge:** full `ctest --preset x64-linux --output-on-failure`
- **Phase gate:** full suite green, plus `scripts/measure_parser_overhead.sh`-style manual-only perf recording (D-13's wall-clock leg) before `/gsd-verify-work`; the instruction-count ratchet (D-16) only runs on the designated CI leg, never locally as a blocking gate

### Wave 0 Gaps
- [ ] `tests/unit/test_rational_int128.cpp` (or extend `test_rational.cpp`) — covers the new 128-bit accumulator extension to `core/rational.h`, with a synthetic long-duration (multi-hour-equivalent tick magnitude) case proving no overflow where naive 64-bit accumulation would fail
- [ ] `tests/unit/test_ts_unwrap.cpp` — covers TIME-02's wrap-vs-discontinuity distinction with hand-constructed tick sequences straddling `2^32`/`2^33`
- [ ] `tests/unit/test_av_drift.cpp` — covers doc 04 §3's rate/pattern classification exactly on synthetic timelines (constant-offset, linear-drift, step, irregular), per the doc's own acceptance criterion ("drift algorithm unit tests on synthetic timelines hit rate/pattern classification exactly")
- [ ] `tests/integration/test_timeline_*.cpp` (one or several, mirroring `test_video_*.cpp`'s per-family split) — trigger+clean pairs for every registered `timeline.*` check id, plus the DOC-04 no-others whole-report count
- [ ] `scripts/measure_timeline_instructions.sh` or an extension of `measure_parser_overhead.sh` — the D-13/D-16 valgrind/cachegrind-driven leg, plus a committed baseline ledger file (D-15) and a documented `apt-get install valgrind` CI step
- [ ] Framework install: none — Catch2/CTest already fully wired; only new test *files* and the perf script are new

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | This project has no auth surface |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Yes | Integer overflow/UB avoidance via `core/rational.h`'s checked helpers (extended with 128-bit accumulation this phase); the 33-bit TS unwrap must never let a crafted stream produce an out-of-range offset (see `unwrap_ts_timestamps` sketch's own overflow-checked `offset` update) |
| V6 Cryptography | No | N/A (XXH3-128 hashing, used elsewhere in the project, is not in this phase's scope) |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Crafted MPEG-TS with pathological PTS sequences designed to trigger repeated spurious "wraps" (denial of service via unbounded `offset` growth, or a crash via unchecked arithmetic) | Denial of Service | `detail::checked_add` on every `offset` update (see unwrap sketch); a failed check degrades the stream to `insufficient_data`/skip rather than crashing or looping unboundedly — matches this project's existing "never crash, degrade cleanly" pattern for `ts_scan`/`bmff_scan`/`ebml_scan` |
| Crafted long-duration or high-timebase-resolution file designed to overflow the least-squares sums, producing either a crash (unchecked path) or a silently-wrong slope (naive wraparound) | Tampering (a crafted input causing a fabricated pass/fail verdict) | Genuine 128-bit accumulation (this session's overflow analysis) plus a final range-checked narrowing back to `int64_t`/`Rational` — never a silent wraparound; on failure, report `insufficient_data` rather than a garbage rate, consistent with `cadence.cpp`'s own "an overflow anywhere in this arithmetic yields insufficient_data, never a wrapped value" precedent [VERIFIED: src/probe/cadence.cpp:107-109 comment text] |
| A crafted `PacketScan` with an enormous number of packets designed to make the K=32 nearest-checkpoint search O(K·N) rather than O(K log N), causing PERF-01's 3s budget to be blown on an adversarial (not necessarily malicious, could just be a very long/high-framerate) input | Denial of Service | Binary search over a sorted index view (same discipline `cadence.cpp` already uses), bounded in any case by `kMaxPacketsPerStream = 5,000,000` [VERIFIED: src/probe/packet_scan.h:63] |
| TS `discontinuity_indicator`/PSI section length fields (attacker-controlled, per `ts_scan.h`'s own documented threat model) misinterpreted when joined against `PacketScan::pos` | Tampering | Reuse, not duplicate, `ts_scan.h`'s existing bounds-checked parsing (Pattern 5); never re-parse raw TS bytes a second time in the timeline analyzer itself |

## Sources

### Primary (HIGH confidence — verified this session via tool/direct file read)
- `claude_docs/04-timeline-analysis.md` — read in full, quoted verbatim where cited
- `.planning/phases/05-timeline-analysis/05-CONTEXT.md` — read in full, D-01 through D-16 and Discretion/Deferred sections quoted
- `.planning/REQUIREMENTS.md` — read in full (TIME-01..11, DOC-04, PERF-01/03/05, traceability table)
- `.planning/STATE.md` — read in full (Phase 3/4 decision history relevant to cadence/priming/perf precedent)
- `src/core/rational.h`, `src/probe/cadence.{h,cpp}`, `src/probe/packet_scan.h`, `src/probe/ts_scan.h`, `src/probe/bmff_scan.h`, `src/probe/ebml_scan.h`, `src/probe/pass.h`, `src/core/model.h`, `src/core/value.h`, `src/core/registry.h`, `src/core/checks.def` (relevant excerpts), `src/compare/tol.cpp` (excerpt), `src/core/snapshot.cpp` (excerpt) — read directly this session, line numbers cited throughout
- `tools/bench/parser_overhead.cpp`, `scripts/measure_parser_overhead.sh` — read in full
- `tests/integration/test_video_yuvj.cpp`, `tests/integration/test_doc03_coverage.cpp` (excerpts) — read directly
- Standalone probe programs compiled and run this session against `build/x64-linux/vcpkg_installed/x64-linux/lib/libavformat.a` (the project's actual linked FFmpeg 8.1, `libavformat 62.12.100`) — priming (`AV_PKT_DATA_SKIP_SAMPLES`/`initial_padding`) and timecode (`tmcd` metadata, `AV_PKT_DATA_S12M_TIMECODE` absence) findings
- `grep` of the vendored FFmpeg 8.1 source tree (`vcpkg/buildtrees/ffmpeg/x64-linux-rel/src`) — `configure`'s `mpdecimate_filter_deps="gpl"`, filter license headers (`vf_mpdecimate.c` GPL-2, `vf_decimate.c`/`setpts.c`/`f_select.c`/`bsf/setts.c` LGPL-2.1+), and every call site setting `AV_PKT_DATA_S12M_TIMECODE`
- `WebFetch` of `https://raw.githubusercontent.com/actions/runner-images/main/images/ubuntu/Ubuntu2404-Readme.md` — confirmed valgrind/cachegrind absent from GitHub's `ubuntu-24.04` hosted runner image's documented software list
- `apt-cache policy valgrind` (this sandbox) — confirmed installable

### Secondary (MEDIUM confidence)
- WebSearch for GitHub Actions runner-image valgrind availability (corroborating, not primary — the WebFetch of the actual README is primary)

### Tertiary (LOW confidence / flagged for validation)
- None beyond what's captured in the Assumptions Log

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependency; every primitive re-used was read directly this session
- Architecture (probe integration, priming, timecode): HIGH — empirically verified against the actual linked FFmpeg 8.1, not merely the pinned corpus-generation ffmpeg or training-data assumptions
- Overflow analysis (128-bit accumulation need): HIGH — derived from first-principles arithmetic on concrete, stated magnitudes (10-min reference file's own duration per D-16, and a realistic 2-hour case), not assumed
- Fixture recipes (LGPL-clean VFR/jitter): MEDIUM — license/build-gate status verified against source; actual generated-fixture vfr_profile behavior not yet tested end-to-end (A3 in Assumptions Log)
- Performance/CI environment (valgrind): MEDIUM — absence confirmed against documented runner image contents; exact apt-mirror-resolvable version at CI-run time not independently confirmed (A2)
- Pitfalls: HIGH — each pitfall in this document traces to either a CONTEXT.md-recorded real shipped false positive, a direct source-grep finding, or a first-principles arithmetic bound computed this session

**Research date:** 2026-09-16
**Valid until:** 30 days (stable domain — FFmpeg 8.1 is pinned and will not silently change; re-verify if the FFmpeg baseline is bumped per BUILD-10's "deliberate, recorded decision" convention before this phase executes)
