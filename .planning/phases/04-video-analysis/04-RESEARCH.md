# Phase 4: Video Analysis - Research

**Researched:** 2026-09-09
**Domain:** libav parser-pass extraction (stream parameters, GOP/NAL classification, colorimetry, HDR metadata) with no decode pass
**Confidence:** HIGH for the three priority questions (all empirically tested against the actual pinned/linked FFmpeg binaries and source); MEDIUM-HIGH for standard-ground architecture (grounded in Phase 3 code read directly); MEDIUM for exact SPS/PPS Exp-Golomb byte layouts (semantically verified, byte-level construction is an execution-time task).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01: H.264/HEVC NAL sequences are hand-constructed, not encoded.** The corpus stays
  LGPL-only; minimal NAL sequences exercise the IDR/CRA classifier directly. The generator ffmpeg
  pin is **not** changed to a GPL build.
- **D-02: The generator writes those synthetic streams to disk as real fixture files**, so DOC-03
  needs no exemption — they enter `CORPUS_DIGEST.txt` like any other fixture.
- **D-03: The synthetic-stream writer is a Python helper under `tools/`**, invoked by
  `gen_corpus.sh`, following the `tools/gen_registry.py` precedent (Python 3.11, zero non-stdlib
  deps).
- **D-04: Real encoders are used wherever they can express the check.** `gop.length` (`-g`) and
  `frame_types` (`-bf`) stay genuine `mpeg4`/`mpeg2video` encodes. Synthetic streams are used
  **only** where NAL semantics are required.
- **D-05: Phase 4 builds the mode-interval/CFR-VFR derivation as a shared pure function** in the
  probe layer over `const StreamPacketScan&`; `video.frame_rate.measured` is its first consumer,
  Phase 5's timeline math its second. No `IntervalStats` struct is resurrected.
- **D-06: Cadence is measured on PTS, falling back to DTS when PTS is absent**, with the axis used
  recorded in evidence.
- **D-07: CFR/VFR is decided by exact rational equality against the mode interval within a fixed
  epsilon expressed in timebase ticks** — integer comparison throughout, named constant, not a
  tunable.
- **D-08: Build the full HDR precedence seam, wire one source.** `source:` emits `stream` today;
  absence of stream-level metadata on a codec that could carry frame-level yields
  `skipped:requires_decode`. Phase 7 fills the second arm.
- **D-09: MDCV/CLL are written as container-level boxes (mp4 `mdcv`/`clli`, or the Matroska
  equivalents) around an ordinary encode — not as in-bitstream SEI.** This narrows D-01/D-02's
  scope: only the NAL/IDR family needs hand-written bitstreams; the HDR family does not.
- **D-10: `VIDEO-10`'s incoherence guard gets its own check id** (e.g. `video.hdr.coherence`) at
  `info` severity, its own `--explain` doc, its own DOC-03 fixture pair.
- **D-11: Phase 4 measures parser overhead and records the number as evidence; it does not add a
  CI gate.** The blocking gate is Phase 5's `PERF-03`/`PERF-05`.
- **D-12: The overhead measurement runs against a multi-minute file generated on demand by a
  separate script**, never entering `tests/fixtures/`, `CORPUS_DIGEST.txt` or the CI corpus step.

### Claude's Discretion

- ParserScan's memory accounting under D-01 (Phase 3) — per-AU records on top of `PacketRecord`
  inside the same global probe-memory budget divided by thread count; behavior on exhaustion.
- How `ParserScan` fuses with `PacketScan` in the orchestrator — the invariant is one sweep,
  asserted by the existing exact-sweep-count field.
- `hdr.dovi` fixture construction — establish what the pinned FFmpeg 9.0.1 can emit before the
  planner commits to a method. **(Answered below — see Priority Finding 2.)**
- Rational expression of the HDR tolerances (±0.0002 absolute on chromaticities, ±5% on luminance
  and CLL).
- The exact epsilon value in D-07, and the exact NAL subset the D-03 writer must emit.

### Deferred Ideas (OUT OF SCOPE)

- `video.closed_captions` (`VIDEO-11`) — Phase 7 (decode pass). SEI T.35 scan in ParserScan is a
  post-v1 stretch goal, tracked not promised.
- Per-frame Dolby Vision RPU diffing — v1 is the configuration record only.
- The blocking CI perf gate, the 10-minute reference fixture, regression tracking — Phase 5.
- Re-pinning the generator ffmpeg to GPL builds — rejected for this phase by D-01.
- Closing `T-3-18`/`T-3-41` — out of scope, recorded in `03-SECURITY.md`.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PROBE-03 | `ParserScan` extends the sweep, per-AU `pict_type`/`key_frame`/`repeat_pict`/`field_order` + NAL-type sequences, <10% overhead | Priority Finding 3 (parser internals) + Architecture Patterns (fusion requirement) |
| VIDEO-01 | Stream-parameter checks | Code Examples (extraction sources), Common Pitfalls (mpeg4/mpeg2video colr gotcha) |
| VIDEO-02 | `frame_count` always from scan, never `nb_frames` | Confirmed no new research needed — PacketScan already counts packets (`packet_scan.h`) |
| VIDEO-03 | yuvj420p→yuv420p+full = exactly one finding | Priority Finding 4 (empirical, **negative result** — flagged as Open Question) + range-fold table (Standard Stack) |
| VIDEO-04 | Container/bitstream SAR conflict | Common Pitfalls; design doc §2 (container wins) |
| VIDEO-05 | GOP checks incl. open/closed via NAL types | Priority Finding 3 (H.264/HEVC NAL semantics, VPS-before-SPS gotcha) |
| VIDEO-06 | `video.interlace` field-order cross-check | Priority Finding 3 (`field_order`/`repeat_pict` populated by both parsers) |
| VIDEO-07 | Colorimetry checks | Priority Finding 4 (empirical colr-atom recipe, `+write_colr` requirement) |
| VIDEO-08 | Change to `unspecified` = regression | No new research needed — pure compare-semantics, `unspecified` is a normal enum value |
| VIDEO-09 | HDR checks, precedence, `source:` evidence | Priority Finding 1 (mdcv/clli empirically confirmed with `mpeg4`) + Priority Finding 2 (DOVI) |
| VIDEO-10 | MDCV/CLL incoherence guard | Design doc §4 + D-10 (own check id); no extraction research needed (pure cross-field logic) |
| VIDEO-12 | No-parser codec degrades to `skipped:no_parser` | `SkipReason::no_parser` already exists (`model.h:46`) |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

Directives extracted from `.claude/CLAUDE.md` that bear directly on this phase's plan/execution:

- **C++20, no modules, no `std::format`** (use `fmt`) — applies to all new `src/probe/parser_scan.*`
  and `src/analyzers/video/*.cpp` code.
- **Rational everywhere; floating ms only in rendered output** — `video.frame_rate.measured`'s
  CFR/VFR epsilon (D-07), the ±0.0002 chromaticity and ±5% luminance/CLL tolerances (Claude's
  Discretion item), and every GOP-interval/level comparison must stay integer/rational, reusing
  `src/core/rational.h`'s `checked_mul`/`checked_sub`/`compare_ticks_checked` and
  `src/compare/tol.cpp`'s existing generic `Tolerance` machinery — never a `double` comparison.
- **Byte-identical `--json` across identical runs; fixed-K/fixed-ε algorithms** — the D-07 epsilon
  must be a named constant, not derived from a runtime-computed mean.
- **No media binaries in git; fixtures synthesized with `-flags +bitexact -fflags +bitexact`** — the
  pinned generator ffmpeg (`scripts/ffmpeg_pin.json`) requires ffmpeg ≥ 6.1 at corpus-generation
  time; this phase's fixtures (real-encoder and hand-constructed) must both honor this.
- **Warnings-as-errors on GCC/Clang/MSVC** — applies to all new probe/analyzer code.
- **Decode-only LGPL subset; GPL code never silently linked** — this phase's shipped analyzer code
  never links `libx264`/`libx265`/etc.; the *generator*-only GPL availability (Priority Finding 1's
  Linux/macOS pinned builds) must never leak into a fixture-recipe that would be unreproducible on
  the Windows LGPL-only pinned build.
- **Check IDs are forever** — every new `video.*`/`gop.*`/`color.*`/`hdr.*` id registered this phase
  (including D-10's new coherence check id) is a one-way commitment; renames require an alias plus
  deprecation cycle.
- **bash-3.2 compatibility for shell scripts (macOS runners)** — any new shell-script glue this
  phase adds to `gen_corpus.sh` (invoking the new D-03 Python tool) must avoid `mapfile`/`readarray`
  and other bash-4-only builtins, per the existing `scripts/lint_bash4_builtins.sh` gate.

## Summary

All three priority questions have concrete, empirically-verified answers, and two of them are
**more favorable** than the phrasing of the priority questions implied. **D-09 (mdcv/clli) is
fully achievable with the plain `mpeg4` encoder — no HDR-capable/GPL encoder is needed at all** —
via FFmpeg's generic, codec-independent `-mastering_display`/`-content_light` CLI options, which
attach `AVMasteringDisplayMetadata`/`AVContentLightMetadata` as **stream-level** side data that
survives an mp4 mux/demux round trip through real `mdcv`/`clli` boxes, verified byte-for-byte in
the produced file and confirmed to populate `codecpar->coded_side_data` (`AV_PKT_DATA_*`) on
read-back — exactly the source `VIDEO-09` names first. **DOVI (`hdr.dovi`) is a harder story:**
genuine Dolby Vision RPU encoding requires externally-supplied per-frame RPU metadata this
project's toolchain has no way to produce (`-dolbyvision`/`dolby-vision-profile` alone, even on
GPL `libx265`, refuses to encode without it) — real DOVI fixtures are **not achievable**. The
fallback is hand-constructing the 24-byte `dvcC`/`dvvC` configuration-record box directly (D-03's
own Python-writer pattern, extended from NAL bytes to box bytes), whose exact layout and lenient
parser (accepts as few as 4 bytes) were read from the actual FFmpeg source this project links.
**The H.264/HEVC NAL subset (priority question 3) is genuinely a header-only, "parseable not
decodable" requirement** — both parsers return immediately after the slice header's leading
fields (no macroblock data needed) — but HEVC carries one sharp trap the design doc does not
mention: **SPS decode fails outright unless a VPS NAL was already successfully parsed and the
SPS's `vps_id` resolves to it** (H.264 has no such requirement — no VPS concept exists).

A fourth, unprompted but load-bearing finding surfaced during the D-09 investigation and directly
threatens **VIDEO-03's own signature test**: **neither `mpeg4` nor `mpeg2video` — the project's
only two allowed real encoders — support `AV_PIX_FMT_YUVJ420P` as an encoder input** (confirmed via
`-h encoder=mpeg4`/`mpeg2video`: "Supported pixel formats: yuv420p[/yuv422p]" only), and requesting
it anyway silently downgrades to plain `yuv420p` with the range hint dropped, not preserved. The
"yuvj420p spelling" fixture the design doc's §6 acceptance criteria calls for **cannot currently be
constructed with a real mpeg4/mpeg2video encode** — this is reported as an Open Question the
planner must resolve (see below), not silently worked around.

**Primary recommendation:** implement `Pass::parser_scan` as an extension **inside**
`run_packet_scan`'s own `av_read_frame` loop (not a second orchestrator dispatch arm — those read
independently), use `-mastering_display`/`-content_light` + plain `mpeg4` for all D-09 MDCV/CLL
fixtures, hand-construct the DOVI config-record box per the verified 24-byte layout, hand-construct
H.264/HEVC NAL sequences per the verified parser requirements (remembering HEVC's VPS-before-SPS
ordering), and escalate the yuvj420p pix_fmt gap to the user/planner before committing to VIDEO-03's
fixture recipe.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Stream-parameter extraction (VIDEO-01/02/04) | Probe layer (`src/probe/parser_scan.*`, new) | Analyzer (`src/analyzers/video/*.cpp`, new) | `codecpar` fields are already available post-`demux_header`; no parser pass needed for these, but the analyzer still lives in the probe-consuming analyzer tier |
| Per-AU pict_type/key_frame/NAL classification (VIDEO-05/06/12) | Probe layer (new `ParserScan`, fused into `PacketScan`'s sweep) | Analyzer | `av_parser_parse2` is a libav decode-adjacent call that belongs in probe/, mirroring `ts_scan`/`bmff_scan`'s own scan-then-analyze split |
| Colorimetry (VIDEO-07/08) | Analyzer (reads `codecpar` directly, no new pass) | — | Same tier as VIDEO-01; `color_range`/`color_primaries`/etc. are demux-time fields |
| HDR metadata (VIDEO-09/10) | Analyzer (reads `codecpar->coded_side_data`, no new pass) | Probe layer (fixture construction only, not extraction) | `coded_side_data` is populated at `avformat_open_input`/`avformat_find_stream_info` time — already available after `Pass::demux_header`, no `parser_scan` dependency |
| Parser overhead measurement (D-11/D-12) | CLI/harness (new script, outside `CORPUS_DIGEST.txt`) | Probe layer (timing instrumentation) | Explicitly not a check or a gate this phase — a recorded number |

## Priority Finding 1: D-09 (mdcv/clli) — CONFIRMED achievable with plain `mpeg4`, no HDR encoder needed

**Tested with:** `.ffmpeg-pinned/linux-x86_64/ffmpeg` (this repo's own pinned binary,
`ffmpeg version 9.0.1-https://www.martin-riedl.de`, installed by
`scripts/install_pinned_ffmpeg.sh` per `scripts/ffmpeg_pin.json`) — **not** the system
`/usr/local/bin/ffmpeg` (an unpinned `N-126086-ge5ecfe8970-20260812` nightly build), per the
task's own authority rule. `[VERIFIED: empirical, .ffmpeg-pinned/linux-x86_64/ffmpeg]`

FFmpeg exposes two generic, **codec-independent** per-stream **input** options,
`-mastering_display` and `-content_light`, that attach `AVMasteringDisplayMetadata` /
`AVContentLightMetadata` directly to the demuxed/generated input stream, independent of what
encoder later consumes it:

```bash
# Actual command run, actual output observed (abbreviated):
.ffmpeg-pinned/linux-x86_64/ffmpeg \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=1" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact \
  -y hdr_mpeg4_test.mp4
```

Console output on the **input** stanza (proving these are input-side options, not output/encoder
options — attempting them after `-i` errors: `"Option mastering_display ... cannot be applied to
output ... you are trying to apply an input option to an output file"`):

```
Input #0, lavfi, from 'testsrc2=...':
    Side data:
      Mastering display metadata: has_primaries:1 has_luminance:1 r(0.6800,0.3200) g(0.2650,0.6900) b(0.1500 0.0600) wp(0.3127, 0.3290) min_luminance=0.000100, max_luminance=1000.000000
      Content light level metadata: MaxCLL=1000, MaxFALL=400
```

The parsed values exactly match x265's `--master-display G(x,y)B(x,y)R(x,y)WP(x,y)L(max,min)`
convention, in the SEI-standard 0.00002-chromaticity/0.0001-luminance raw units — confirmed by hand
cross-checking: `G(13250,34500)/50000 = (0.265, 0.69)` matches the printed `g(0.2650,0.6900)`
exactly, and every other coordinate matches identically. `-content_light "1000,400"` parses as
`MaxCLL,MaxFALL`. `[VERIFIED: empirical]`

**The output file contains real `mdcv`/`clli` ISOBMFF boxes**, confirmed by raw byte search of the
produced file (not console text):

```
$ python3 -c "data=open('hdr_mpeg4_test.mp4','rb').read(); print(data.count(b'mdcv'), data.count(b'clli'))"
1 1
```

**Reading the file back with the same pinned binary (no injection flags this time) shows the
demuxed stream automatically carries the side data**, printed in the `Input #0` stanza (i.e.
before any decode occurs — this is `dump_format()` walking `st->codecpar->coded_side_data`, not a
decoded-frame artifact):

```
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'hdr_mpeg4_test.mp4':
  Stream #0:0[0x1](und): Video: mpeg4 (Simple Profile) (mp4v / 0x7634706D), yuv420p, 320x240 ...
    Side data:
      Mastering display metadata: has_primaries:1 has_luminance:1 r(0.6800,0.3200) ...
      Content light level metadata: MaxCLL=1000, MaxFALL=400
```

**Confirmed against the actual FFmpeg 8.1 source this project links** (not the 9.0.1 generator —
`vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean`, matching `STATE.md`'s "FFmpeg pinned to 8.1
(port-version 4) via vcpkg.json overrides"): `libavformat/mov.c:11080-11095` writes
`AV_PKT_DATA_MASTERING_DISPLAY_METADATA` / `AV_PKT_DATA_CONTENT_LIGHT_LEVEL` onto
`st->codecpar->coded_side_data` via `av_packet_side_data_add` — the exact source `VIDEO-09` names
first. `mov.c:9601-9602` registers `mdcv`/`clli` box-tag handlers (`mov_read_mdcv`/`mov_read_clli`,
defined at `mov.c:6494`/`6575`). `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/mov.c:11080-11095,9601-9602]`

**Why this matters for D-01's cross-platform constraint:** `scripts/ffmpeg_pin.json` pins the
Windows generator to `BtbN/FFmpeg-Builds LGPL static build` (explicitly excludes `libx264`/`libx265`
by construction), while the Linux/macOS generator pins are full GPL builds
(`martin-riedl.de`, confirmed via `.ffmpeg-pinned/linux-x86_64/ffmpeg -version`:
`--enable-gpl --enable-libx264 --enable-libx265`). **Any HDR fixture recipe requiring
libx264/libx265/libsvtav1 would silently fail or be unavailable on the Windows leg** — this is
almost certainly *why* D-09 phrases the requirement as "around an ordinary encode": the
`-mastering_display`/`-content_light` + `mpeg4` recipe above is the **only** cross-platform-safe
path, since `mpeg4` is in `install_pinned_ffmpeg.sh`'s `REQUIRED_ENCODERS` assertion
(`scripts/install_pinned_ffmpeg.sh:306`) and is therefore guaranteed present on every pinned build.
`[VERIFIED: scripts/ffmpeg_pin.json, scripts/install_pinned_ffmpeg.sh:306]`

**Matroska equivalent:** not separately tested this session (time-boxed); Matroska's own tags for
mastering-display/CLL (`MasteringMetadata`/simple tags under `Colour`) are a distinct code path in
`libavformat/matroskaenc.c`/`matroskadec.c` — recommend the planner run the identical
`-mastering_display`/`-content_light` recipe with a `.mkv`/`.webm` output extension and verify the
same `codecpar->coded_side_data` round trip empirically before committing to it; the input-side
mechanism (attaching to the demuxed/generated stream before encode) is container-independent, only
the muxer-side box/element format differs. `[ASSUMED — MKV path not independently verified this session]`

## Priority Finding 2: D-09's DOVI companion (`hdr.dovi`) — genuine RPU NOT achievable; hand-construction IS

**Attempted (both failed to produce a real RPU) — tested with the same pinned 9.0.1 binary:**

1. `-c:v libsvtav1 -dolbyvision 1 -pix_fmt yuv420p10le -color_primaries bt2020 -color_trc smpte2084
   -colorspace bt2020nc` → **encoder open failure**: `"Dolby Vision enabled, but could not determine
   profile and compatibility mode... Error while opening encoder"`. `[VERIFIED: empirical]`
2. Same with `-c:v libx265 -dolbyvision 1` (GPL, present on this Linux pinned build only — tested
   purely to establish upstream feasibility, not as a proposed recipe) → **identical failure.**
   `[VERIFIED: empirical]`
3. `-c:v libx265 -x265-params "dolby-vision-profile=8.1:..."` (the working alternate spelling) →
   **encode succeeds**, but the produced file has **zero** `dvcC`/`dvvC`/`dvwC` bytes and read-back
   shows no DOVI side data at all — `dolby-vision-profile=8.1` alone signals *intent* but x265
   requires externally-supplied per-frame RPU data (its native `--dolby-vision-rpu <file>` option,
   not exposed as any ffmpeg AVOption) to actually emit RPU NAL units. Without real Dolby Vision
   mastering data (which this project's toolchain has no source for — no DV grading tool anywhere
   in the stack), no combination of ffmpeg CLI flags produces genuine DOVI output.
   `[VERIFIED: empirical]`

**Conclusion: real DOVI-encoded fixtures are not achievable in this project's environment**, on any
pinned or system binary, GPL or not. This is a firm negative finding, not a gap in this session's
testing effort.

**The fallback — hand-construct the `dvcC`/`dvvC` box, mirroring D-01's NAL-hand-construction
precedent — is concretely achievable and low-risk**, verified by reading the actual box
parser/writer this project links:

`libavformat/dovi_isom.h:29`: `#define ISOM_DVCC_DVVC_SIZE 24` — the box payload is a fixed 24
bytes. `libavformat/dovi_isom.c:32-87` (`ff_isom_parse_dvcc_dvvc`) is **lenient**: it requires only
`size >= 4` (not the full 24) to populate `dv_version_major`, `dv_version_minor`, `dv_profile`,
`dv_level`, `rpu_present_flag`, `el_present_flag`, `bl_present_flag`; a 5th byte additionally yields
`dv_bl_signal_compatibility_id`/`dv_md_compression` (source quoted below). Byte layout, quoted
verbatim from the parser:

```c
// libavformat/dovi_isom.c:46-68 (ff_isom_parse_dvcc_dvvc)
dovi->dv_version_major = *buf_ptr++;    // 8 bits
dovi->dv_version_minor = *buf_ptr++;    // 8 bits

buf = *buf_ptr++ << 8;
buf |= *buf_ptr++;

dovi->dv_profile        = (buf >> 9) & 0x7f;    // 7 bits
dovi->dv_level          = (buf >> 3) & 0x3f;    // 6 bits
dovi->rpu_present_flag  = (buf >> 2) & 0x01;    // 1 bit
dovi->el_present_flag   = (buf >> 1) & 0x01;    // 1 bit
dovi->bl_present_flag   =  buf       & 0x01;    // 1 bit

// Has enough remaining data
if (size >= 5) {
    uint8_t buf = *buf_ptr++;
    dovi->dv_bl_signal_compatibility_id = (buf >> 4) & 0x0f; // 4 bits
    dovi->dv_md_compression = (buf >> 2) & 0x03; // 2 bits
}
```

Result populates `st->codecpar->coded_side_data` with `AV_PKT_DATA_DOVI_CONF`
(`dovi_isom.c:70-71`, `av_packet_side_data_add(..., AV_PKT_DATA_DOVI_CONF, ...)`), which is the
exact field `VIDEO-09` names for the DOVI configuration record.
`[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/dovi_isom.h:29, dovi_isom.c:32-87]`

**The box is parsed generically, not codec-gated:** `mov.c:3100-3172` (`ff_mov_read_stsd_entries`)
calls `mov_read_default` on every sample entry's remaining bytes regardless of the sample entry's
own fourcc (`mp4v`, `hvc1`, etc.), and `mov.c:9603-9605` registers `dvcC`/`dvvC`/`dvwC` in the same
global tag-dispatch table `mdcv`/`clli` use. This means **the Python writer can attach a
hand-constructed minimal `dvcC` box as a child of an ordinary `mp4v` (mpeg4) sample entry** — no
real HEVC/AV1 bitstream or encoder is needed at all, extending the same trick D-09 already uses for
mdcv/clli. `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/mov.c:3100-3172,9603-9605]`

**Recommendation:** treat `hdr.dovi` the same way D-01 treats NAL sequences — hand-construct a
minimal (5+ byte, well within the 24-byte allocation) `dvcC` box in the D-03 Python writer, injected
as a child box of the video sample entry in an otherwise-ordinary mpeg4-encoded mp4. This keeps
`hdr.dovi` inside D-02's "real fixture file, no DOC-03 exemption" requirement without ever touching
a GPL encoder or requiring real RPU data.

## Priority Finding 3: minimum NAL subset for `av_parser_parse2` (H.264 and HEVC)

Read directly from `libavcodec/h264_parser.c` and `libavcodec/hevc/parser.c`
(FFmpeg 8.1, the linked version) — `[VERIFIED: path:line as cited]`.

**Confirmed: this is a "parseable, not decodable" requirement.** Both parsers' slice-header
handlers return immediately after extracting the fields they need — no macroblock/CTU/residual
data is read at all:
- H.264: `h264_parser.c:574-575`, `av_freep(&rbsp.rbsp_buffer); return 0; /* no need to evaluate the
  rest */` — inside the `H264_NAL_SLICE`/`H264_NAL_IDR_SLICE` case, immediately after parsing
  `first_mb_in_slice`, `slice_type`, `pps_id`, `frame_num`, and (conditionally) `idr_pic_id`/
  `poc_lsb`.
- HEVC: `hevc/parser.c:169`, `return 1; /* no need to evaluate the rest */` — inside
  `hevc_parse_slice_header`, immediately after `first_slice_in_pic_flag`, `pps_id`, `slice_type`,
  and (conditionally) `pic_order_cnt_lsb`.

### H.264 (`libavcodec/h264_parser.c`)

Minimum NAL set: **SPS (type 7) → PPS (type 8) → at least one SLICE (type 1) or IDR_SLICE (type
5)**. NAL type values confirmed at `libavcodec/h264.h:35-43`: `H264_NAL_SLICE=1`, `H264_NAL_DPA=2`,
`H264_NAL_IDR_SLICE=5`, `H264_NAL_SEI=6`, `H264_NAL_SPS=7`, `H264_NAL_PPS=8`, `H264_NAL_AUD=9`.

`s->key_frame` is set true under any of three conditions (`h264_parser.c:355,366-369,387-388`):
1. NAL type is `H264_NAL_IDR_SLICE` (unconditional).
2. `p->sei.recovery_point.recovery_frame_cnt >= 0` (a recovery-point SEI is present — open-GOP
   recovery signaling).
3. **Heuristic** (quoted verbatim): `if (p->ps.sps->ref_frame_count <= 1 && p->ps.pps->ref_count[0]
   <= 1 && s->pict_type == AV_PICTURE_TYPE_I) s->key_frame = 1;` — a non-IDR I-slice with
   `num_ref_frames<=1` and `num_ref_idx_l0_default_active_minus1` implying `ref_count[0]<=1` is
   heuristically treated as a keyframe too.

`s->pict_type` comes from the slice header's `slice_type` field: `ff_h264_golomb_to_pict_type[slice_type
% 5]` (`h264_parser.c:365`) — a well-formed Exp-Golomb `slice_type` value is required, but its
numeric value alone (not surrounding macroblock data) determines I/P/B.

**Hard requirement:** the slice's `pps_id` must resolve to an already-successfully-parsed PPS
(`h264_parser.c:376-379`, errors and aborts the whole AU parse otherwise: `"non-existing PPS %u
referenced"`), and that PPS's own `sps` pointer must be valid (set when the PPS NAL itself was
parsed, `ff_h264_decode_picture_parameter_set`). **NAL order must be SPS, then PPS, then slice.**

### HEVC (`libavcodec/hevc/parser.c`)

Minimum NAL set: **VPS (type 32) → SPS (type 33) → PPS (type 34) → at least one VCL NAL** in the
IRAP or TRAIL/TSA/STSA/RADL/RASL range. NAL type values confirmed at
`libavcodec/hevc/hevc.h:29-66`: `HEVC_NAL_TRAIL_N=0`, `HEVC_NAL_TRAIL_R=1`, `HEVC_NAL_BLA_W_LP=16`,
`HEVC_NAL_IDR_W_RADL=19`, `HEVC_NAL_IDR_N_LP=20`, `HEVC_NAL_CRA_NUT=21`, `HEVC_NAL_VPS=32`,
`HEVC_NAL_SPS=33`, `HEVC_NAL_PPS=34`, `HEVC_NAL_EOB_NUT=37`. `IS_IRAP_NAL` is `nal->type >= 16 &&
nal->type <= 23` (`parser.c:37`); `IS_IDR_NAL` is `type == HEVC_NAL_IDR_W_RADL ||
type == HEVC_NAL_IDR_N_LP` (`parser.c:38`).

**`s->key_frame = 1` is set for ANY IRAP NAL** (`parser.c:74-76`), including `CRA_NUT` — **the
`key_frame` boolean does not itself distinguish IDR from CRA for open/closed classification**;
`gop.idr_interval`'s classifier must inspect the NAL type directly (`IS_IDR_NAL` vs
`type==HEVC_NAL_CRA_NUT`/BLA family), matching the design doc's own §2 wording exactly.

**Sharp, undocumented trap: VPS must precede SPS, and SPS's `sps_video_parameter_set_id` must
reference an already-registered VPS, or SPS decode fails outright** (unlike H.264, which has no
VPS concept at all). Confirmed in `libavcodec/hevc/ps.c:1220-1225`:

```c
if (!vps_list[sps->vps_id]) {
    // ... error path ...
}
sps->vps = av_refstruct_ref_c(vps_list[sps->vps_id]);
```

`hevc_parse_slice_header` unconditionally dereferences `sps->vps->vps_timing_info_present_flag`
(`parser.c:97`) — if SPS decode had silently produced a null `vps` pointer this would be a crash,
but the confirmed behavior is that SPS decode itself **errors and refuses to register the SPS** when
its VPS is missing, so the practical failure mode is "the whole AU fails to parse" (pict_type/
key_frame never populated for that AU), not a crash. **The D-03 writer's HEVC path must emit VPS
before SPS, in every synthetic stream, even though real encoders always do this and a hand-writer
might be tempted to omit it as "not load-bearing."**

`s->pict_type` comes from `slice_type` (`parser.c:135-144`, only `HEVC_SLICE_I/P/B` accepted,
anything else is `AVERROR_INVALIDDATA` and aborts the AU).

### Framing

Both `h264_find_frame_end` (`h264_parser.c:82-171`) and `hevc_find_frame_end`
(`hevc/parser.c:258-305`) scan for Annex-B start codes (`0x000001`) when `p->is_avc` is false — the
priority question's own "Annex-B framing" phrasing matches this path. **Recommendation confirmed:**
the D-03 writer should emit raw Annex-B elementary streams (3- or 4-byte start-code-delimited NALs,
`is_avc=0` path), not AVCC/length-prefixed framing — this avoids the extradata-vs-inline SPS/PPS
question entirely (Annex-B carries SPS/PPS inline in the bitstream itself, no `avcC`/`hvcC`
extradata box to construct separately) and is the simpler, lower-risk construction.

**Exact Exp-Golomb bit layouts for a minimal valid SPS/PPS (field widths, valid ranges, RBSP
trailing bits) were not independently re-derived from spec text this session** — time-boxed in
favor of the higher-value questions above. `[ASSUMED — recommend a small round-trip spike:
construct one candidate SPS/PPS by hand, feed it through the actual linked `av_parser_parse2` in a
throwaway C++ program, and confirm `pict_type`/`key_frame` populate correctly, before writing the
full D-03 tool]`.

## Priority Finding 4 (unprompted but load-bearing): mpeg4/mpeg2video cannot express `yuvj420p`, and `colr`-box writing needs an explicit muxer flag

Discovered while establishing D-09's feasibility; directly threatens VIDEO-03 and VIDEO-07 fixture
construction, which D-04 currently assumes real `mpeg4`/`mpeg2video` encodes can express.

**Finding A — no `colr` box is written by default for `mpeg4` output, even when
`-color_primaries`/`-color_trc`/`-color_range`/`-colorspace` are all explicitly set:**

```
$ ffmpeg -f lavfi -i testsrc2... -c:v mpeg4 -color_primaries bt2020 -color_trc smpte2084 \
    -color_range tv -colorspace bt2020nc -y hdr_mpeg4_test.mp4
$ python3 -c "print(open('hdr_mpeg4_test.mp4','rb').read().count(b'colr'))"
0
```

`[VERIFIED: empirical]` `libavformat/mov.c`'s own `-h muxer=mov` help text names the fix:
`write_colr` movflag — `"Write colr atom even if the color info is unspecified (Experimental...)"`.
Adding `-movflags +write_colr` makes the box appear (confirmed: `colr` box present, byte-parsed as
an `nclx` atom).

**Finding B — even with `+write_colr`, the top-level `-color_primaries`/`-color_trc` CLI options
did NOT round-trip for `mpeg4`** (raw box bytes parsed directly, not just console text):

```
$ python3 -c "
box = ...  # colr box bytes, primaries at offset 12-14, transfer 14-16, matrix 16-18, full_range@18
"
primaries 2 transfer 2 matrix 1 full_range 128    # 2 == AVCOL_PRI_UNSPECIFIED / AVCOL_TRC_UNSPECIFIED
```

`-color_range` and `-colorspace` (matrix) DID round-trip correctly (`full_range=128`=full,
`matrix=1`=bt709), but `-color_primaries`/`-color_trc` silently became `unspecified` (2). Switching
to the `setparams` video filter for primaries/transfer/colorspace **and** the top-level
`-color_range` flag together produced a fully correct round trip, verified byte-for-byte
(`primaries=1, transfer=1, matrix=1, full_range=128`):

```bash
ffmpeg -f lavfi -i "testsrc2=..." \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range pc -movflags +write_colr \
  -y out.mp4
```

`[VERIFIED: empirical — this exact recipe was confirmed working via direct box-byte inspection]`.
**This recipe (`setparams` filter for primaries/transfer/colorspace + top-level `-color_range` +
`-movflags +write_colr`) is the one the planner should use for every VIDEO-07 colorimetry fixture
built on `mpeg4`/`mpeg2video`.**

**Finding C — `AV_PIX_FMT_YUVJ420P` cannot be produced by either allowed real encoder at all:**

```
$ ffmpeg -h encoder=mpeg4      2>&1 | grep "Supported pixel formats"
    Supported pixel formats: yuv420p
$ ffmpeg -h encoder=mpeg2video 2>&1 | grep "Supported pixel formats"
    Supported pixel formats: yuv420p yuv422p
```

`[VERIFIED: empirical]` Neither lists `yuvj420p`. Requesting it anyway (`-pix_fmt yuvj420p`, or via
`-vf format=yuvj420p`) does not error — ffmpeg silently inserts an implicit conversion to
`yuv420p`, and **the range hint is not even preserved as a side effect**: read-back showed
`yuv420p(tv)` (limited range, plain pix_fmt) in every variant tried, confirmed empirically across
three independent attempts (`format=yuvj420p` filter, `-pix_fmt yuvj420p` direct, both combined
with `-movflags +write_colr`). A `rawvideo`-codec attempt correctly showed `yuvj420p(pc,
progressive)` in the **pre-mux** console output but read back as pix_fmt `none` (an unresolved raw
tag in the `mov` container) — promising but not a working recipe within this session's time budget.

**This is reported as an Open Question, not silently worked around** — see below. VIDEO-03 is
explicitly "the phase's signature test" per `04-CONTEXT.md`'s Specific Ideas section, and D-04's
current text ("Real encoders are used wherever they can express the check... Synthetic streams are
used only where NAL semantics are required") does not anticipate this gap. The planner needs either
a revised D-04 scope (pix_fmt-family hand-construction, mirroring D-01's NAL approach) or a resolved
recipe (e.g. a correctly-tagged `rawvideo` fixture, or an `mjpeg`-encoded fixture if `mjpeg` is
confirmed present on every pinned build — not yet verified against `install_pinned_ffmpeg.sh`'s
`REQUIRED_ENCODERS` list, which does **not** currently include `mjpeg`).

## Standard Stack

This phase adds **no new external dependencies**. It extends the existing probe layer
(`src/probe/`) with a new pass and adds analyzers under the already-scaffolded
`src/analyzers/video/` (`[VERIFIED: empirical — `ls src/analyzers/video/` shows only `.gitkeep`]`),
using the FFmpeg APIs already linked via vcpkg (8.1, per `STATE.md`'s BUILD-10 record) and the
already-established Python tooling pattern (`tools/gen_registry.py`).

### Core (reused, not newly introduced)
| Component | Version | Purpose | Why Standard |
|-----------|---------|---------|--------------|
| `av_parser_parse2` / `AVCodecParserContext` | FFmpeg 8.1 (linked) | per-AU `pict_type`/`key_frame`/`repeat_pict`/`field_order` | Only libav API that extracts bitstream-header-level frame properties without a full decode; signature verified at `libavcodec/avcodec.h:2832-2837` |
| `AVCodecParserContext.field_order` / `.repeat_pict` | FFmpeg 8.1 | VIDEO-06 interlace, VIDEO-01 frame-rate evidence | `avcodec.h:690,2611,2709` confirm the fields exist and are populated by both H.264 and HEVC parsers from SEI picture-timing |
| `codecpar->coded_side_data` (`AV_PKT_DATA_MASTERING_DISPLAY_METADATA`, `AV_PKT_DATA_CONTENT_LIGHT_LEVEL`, `AV_PKT_DATA_DOVI_CONF`) | FFmpeg 8.1 | VIDEO-09 stream-level HDR source | Confirmed populated by `mov.c` at demux time, no decode required |

### pix_fmt range-fold table (VIDEO-03)

Read directly from the linked FFmpeg's own pixel-format enum comments
(`libavutil/pixfmt.h:85-283`) — this is the **complete, exhaustive** list, five entries, no more:

| Deprecated (yuvj) | Folds to | range |
|---|---|---|
| `AV_PIX_FMT_YUVJ420P` | `AV_PIX_FMT_YUV420P` | full |
| `AV_PIX_FMT_YUVJ422P` | `AV_PIX_FMT_YUV422P` | full |
| `AV_PIX_FMT_YUVJ444P` | `AV_PIX_FMT_YUV444P` | full |
| `AV_PIX_FMT_YUVJ440P` | `AV_PIX_FMT_YUV440P` | full |
| `AV_PIX_FMT_YUVJ411P` | `AV_PIX_FMT_YUV411P` | full |

`[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavutil/pixfmt.h:85-283]` — comment
text quoted verbatim: `"planar YUV 4:2:0, 12bpp, full scale (JPEG), deprecated in favor of
AV_PIX_FMT_YUV420P and setting color_range"` (line 85), same phrasing pattern for the other four.

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `-mastering_display`/`-content_light` CLI options (D-09) | Real HDR encoder (`libx265`/`libsvtav1` `-x265-params master-display=...`) | Requires GPL encoder unavailable on the Windows LGPL pinned build (D-01 constraint); no benefit over the CLI-option approach, which is codec-independent and already verified working |
| Hand-constructed `dvcC` box (DOVI) | Real Dolby Vision encode | Not achievable at all in this project's toolchain — no RPU source exists |
| Annex-B raw elementary stream for D-03's NAL writer | AVCC-framed MP4 with `avcC`/`hvcC` extradata | AVCC requires the parser to pull SPS/PPS from `AVCodecContext.extradata` on first call (a second construction surface); Annex-B keeps SPS/PPS inline, simpler, matches priority question 3's own phrasing |

**Installation:** none — no new packages.

## Package Legitimacy Audit

No new external packages are introduced by this phase. The D-03 Python writer should follow
`tools/gen_registry.py`'s own "zero non-stdlib dependencies" convention (Python 3.11's `struct`
module is sufficient for both NAL-byte and ISOBMFF-box-byte construction) —
`[VERIFIED: tools/gen_registry.py`'s own docstring, "this generator has zero non-stdlib
dependencies"]`. No `package-legitimacy check` run was needed; there is nothing to check.

## Architecture Patterns

### System Architecture Diagram

```
DemuxSession (already open, Phase 3)
        |
        v
run_packet_scan()  <-- one av_read_frame loop -------------------------+
        |                                                              |
        | for each packet already read:                                |
        |   PacketRecord{pts,dts,duration,size,flags,pos} appended      |
        |   (existing, Phase 3)                                        |
        |                                                              |
        +-- NEW (Phase 4, if any AnalyzerSpec declared Pass::parser_scan):
        |     av_parser_parse2(per-stream AVCodecParserContext, packet.data)
        |     -> AccessUnitRecord{pict_type,key_frame,repeat_pict,field_order,
        |                          nal_types[]}  appended to ParserScanResult
        |                                                              |
        v                                                              v
PacketScanResult (existing)                          ParserScanResult (NEW, optional)
        |                                                              |
        +---------------------------+--------------------------------+
                                     v
                          ProbeResults{packet_scan, parser_scan, ...}
                                     |
              +----------------------+----------------------+
              v                      v                      v
   video.* stream-param      video.gop.*/frame_types   video.color.*/hdr.*
   analyzers (codecpar-      analyzers (ParserScanResult   analyzers (codecpar-
   only, no parser_scan)     consumers)                    only, no parser_scan)
```

**Why parser_scan must extend the SAME loop, not add a second `if (pass == Pass::parser_scan)` arm
in the orchestrator:** the existing `bmff_scan`/`ebml_scan`/`ts_scan` arms in
`src/probe/orchestrator.cpp:180-206` each **independently** open/read the file (bmff_scan explicitly
so — `"run_bmff_scan opens utf8_path itself... rather than reading through the already-open
session"`, `pass.h:120-125`). `Pass::packet_scan`'s own arm (`orchestrator.cpp:190-200`) calls
`run_packet_scan(session, PacketScanLimits{})`, which is itself one complete `av_read_frame` loop.
**If `parser_scan` were added as its own sibling arm calling a separate `run_parser_scan()`, the
file would be read twice** when both passes are requested — violating the design doc's explicit
"same `av_read_frame` sweep... the file is still read once" and the `packet_scan.h:151`
`read_frame_call_count` field's own stated purpose ("exposed so a test can assert an EXACT sweep
count"). The correct fusion point is **inside** `run_packet_scan`'s existing per-packet loop: when
`parser_scan` was requested for a given stream's codec, additionally feed that packet's data through
`av_parser_parse2` in the same iteration, before/after appending the `PacketRecord`.
`[VERIFIED: src/probe/orchestrator.cpp:180-206, src/probe/pass.h:120-125, src/probe/packet_scan.h:144-151]`

### Recommended Project Structure
```
src/probe/
├── packet_scan.{h,cpp}      # EXTENDED: optional parser-scan fusion inside run_packet_scan
├── parser_scan.h            # NEW: AccessUnitRecord, ParserScanResult, NAL-type enums (H.264/HEVC)
└── orchestrator.cpp         # EXTENDED: Pass::parser_scan added to union_passes resolution only
                              #   (no new independent read arm)
src/analyzers/video/
├── stream_params.cpp        # VIDEO-01/02/04, codecpar-only, no parser_scan dependency
├── gop.cpp                  # VIDEO-05/12, parser_scan-dependent
├── frame_types.cpp          # part of VIDEO-01/05, parser_scan-dependent (keyframe-flag fallback for no_parser)
├── interlace.cpp            # VIDEO-06, parser_scan-dependent (field_order cross-check)
├── color.cpp                # VIDEO-07/08, codecpar-only
└── hdr.cpp                  # VIDEO-09/10, codecpar->coded_side_data-only, no parser_scan
tools/
└── gen_video_fixtures.py    # NEW (D-03): NAL + dvcC-box hand construction, invoked by gen_corpus.sh
```

### Pattern 1: Two `AnalyzerSpec`s per family (Phase 3 precedent, continues here)
**What:** one real-data spec scoped to the applicable codecs, one family-agnostic
not-applicable sibling that reports `skipped:no_parser`/`skipped:not_applicable_container`.
**When to use:** every `video.gop.*`/`video.frame_types` analyzer — codecs without a registered
libav parser (ProRes is the design doc's own example) must degrade explicitly per VIDEO-12, never
silently omit the check.
**Example (pattern only, not code from this phase):**
```cpp
// Mirrors src/analyzers/container/*.cpp's existing family-scoped + family-agnostic pair —
// Source: src/probe/pass.h:149-153 (AnalyzerSpec shape, verified this session)
```

### Anti-Patterns to Avoid
- **Second independent `av_read_frame` sweep for parser_scan:** breaks the design doc's explicit
  single-sweep requirement and the existing `read_frame_call_count` exact-count test's spirit.
- **Reading raw SEI 0.00002/0.0001-unit chromaticity/luminance values and re-scaling them:** not
  needed. `AVMasteringDisplayMetadata.display_primaries`/`white_point`/`min_luminance`/
  `max_luminance` are already `AVRational` in **real** CIE-xy [0,1] / cd/m² units by the time
  `codecpar->coded_side_data` exposes them (confirmed via the empirical printout: `r(0.6800,0.3200)`,
  `max_luminance=1000.000000`, not raw SEI-encoded integers) —
  `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavutil/mastering_display_metadata.h:38-69,
  cross-checked against the empirical printout in Priority Finding 1]`. `AVContentLightMetadata.MaxCLL`/
  `MaxFALL` are plain `unsigned` (not `AVRational`) in cd/m² directly (`mastering_display_metadata.h:107-117`).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| pix_fmt range-folding | A custom yuvj-name lookup table maintained by hand | The 5-entry table above, sourced directly from `libavutil/pixfmt.h`'s own enum comments | Exhaustive and unlikely to drift — libav itself documents each mapping inline; a hand-guessed table risks missing `yuvj440p`/`yuvj411p` (less common, easy to omit from memory) |
| DOVI/HDR box construction | A guessed byte layout for `dvcC`/`mdcv`/`clli` | `ff_isom_put_dvcc_dvvc`'s exact bit-packing (`dovi_isom.c:89-126`, quoted above) as the ground truth for the DOVI box; for mdcv/clli, the empirically-verified `-mastering_display`/`-content_light` CLI recipe (no hand-byte-construction needed at all for those two) | A hand-guessed HDR box layout that merely "looks right" is exactly the failure mode this project's provenance rules exist to prevent — a wrong bit offset produces a box the demuxer silently ignores (returns an error the mov box-walk swallows) rather than a build failure |
| HEVC NAL-sequence ordering | Skipping VPS "since it's not load-bearing for classification" | Always emit VPS before SPS | Confirmed via source read: SPS decode outright fails without a registered VPS; skipping it doesn't just lose optional metadata, it breaks the entire AU parse |

**Key insight:** every byte-level claim in this domain (box layouts, NAL type values, pix_fmt
mappings) has a single, extractable ground truth already vendored into this repository's own
`vcpkg/buildtrees/ffmpeg/src/n8.1-.../` tree — there is no need to trust training-data recall for
any of it, and this research pass deliberately did not.

## Common Pitfalls

### Pitfall 1: Assuming `-color_primaries`/`-color_trc` "just work" for `mpeg4`/`mpeg2video` → mp4
**What goes wrong:** primaries/transfer silently read back as `unspecified` even though the CLI
flags were passed and no error was printed.
**Why it happens:** the `mov` muxer needs `-movflags +write_colr` to write a `colr` box at all for
these two codecs, and even then the top-level `-color_primaries`/`-color_trc` CLI options did not
propagate through the encoder→muxer path in testing — the `setparams` video filter did.
**How to avoid:** use the exact verified recipe in Priority Finding 4 (`-vf
setparams=color_primaries=...:color_trc=...:colorspace=... -color_range ... -movflags
+write_colr`) for every VIDEO-07 fixture.
**Warning signs:** a fixture's colorimetry check passes as "clean" (no finding) when it should have
triggered — silently-unspecified primaries/transfer on BOTH sides of a pair looks identical to a
genuinely-matching pair.

### Pitfall 2: Treating `key_frame==true` as sufficient for HEVC open/closed GOP classification
**What goes wrong:** both `IDR_W_RADL`/`IDR_N_LP` (closed) and `CRA_NUT`/`BLA_*` (open) set
`s->key_frame=1` — a classifier reading only the boolean cannot distinguish them.
**Why it happens:** `IS_IRAP_NAL` (`nal->type >= 16 && nal->type <= 23`) covers the whole IRAP
family; `key_frame` is a coarser signal than the design doc's own open/closed requirement.
**How to avoid:** `ParserScan`'s per-AU record must carry the raw NAL type (or at minimum an
IDR-vs-other-IRAP boolean derived from it), not just `key_frame`.
**Warning signs:** an open-GOP HEVC fixture (CRA-led) misclassifies as closed in testing.

### Pitfall 3: Omitting VPS in a hand-written HEVC test stream
**What goes wrong:** SPS decode fails, the whole AU fails to parse, `pict_type`/`key_frame` never
populate — looks like "the parser doesn't work" rather than "the fixture is malformed."
**Why it happens:** H.264 has no VPS concept, so a writer generalizing from H.264 experience easily
forgets HEVC needs one.
**How to avoid:** always emit VPS before SPS in the D-03 writer's HEVC path; verified requirement,
see Priority Finding 3.
**Warning signs:** HEVC NAL fixtures parse fine in isolation via manual byte inspection but
`av_parser_parse2` returns without ever setting `pict_type`.

### Pitfall 4: Assuming `yuvj420p` fixtures can reuse D-04's real-encoder path unchanged
**What goes wrong:** the encode "succeeds" with no error, but the produced fixture does not
actually exercise the range-folding logic VIDEO-03 needs — it silently degrades to plain
`yuv420p`/limited-range on both counts.
**Why it happens:** neither `mpeg4` nor `mpeg2video` lists `yuvj420p` in `Supported pixel formats`;
ffmpeg auto-inserts a lossy conversion rather than erroring.
**How to avoid:** flag to the user/planner before fixture-writing begins — see Open Questions.
**Warning signs:** a "yuvj420p vs yuv420p+full" DOC-03 pair passes because both sides are
byte-identical `yuv420p`/limited, defeating the test's entire purpose.

## Code Examples

### `-mastering_display`/`-content_light` fixture recipe (D-09, VIDEO-09)
```bash
# Source: this session's own empirical verification, .ffmpeg-pinned/linux-x86_64/ffmpeg 9.0.1
"$FFMPEG_BIN" \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/hdr_mdcv_cll_a.mp4"
```

### Colorimetry fixture recipe (VIDEO-07)
```bash
# Source: this session's own empirical verification
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range pc -flags +bitexact -fflags +bitexact \
  -movflags +write_colr -y \
  "$OUT_DIR/color_709_pc.mp4"
```

### DOVI configuration-record box bit-packing (reference for the D-03 Python writer)
```c
// Source: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/dovi_isom.c:89-112
// (ff_isom_put_dvcc_dvvc — quoted verbatim, this is what the box MUST decode as)
put_bits(&pb, 8, dovi->dv_version_major);
put_bits(&pb, 8, dovi->dv_version_minor);
put_bits(&pb, 7, dovi->dv_profile & 0x7f);
put_bits(&pb, 6, dovi->dv_level & 0x3f);
put_bits(&pb, 1, !!dovi->rpu_present_flag);
put_bits(&pb, 1, !!dovi->el_present_flag);
put_bits(&pb, 1, !!dovi->bl_present_flag);
put_bits(&pb, 4, dovi->dv_bl_signal_compatibility_id & 0x0f);
put_bits(&pb, 2, dovi->dv_md_compression & 0x03);
put_bits(&pb, 26, 0); /* reserved */
put_bits32(&pb, 0); /* reserved */  // total padded to 24 bytes; parser accepts as few as 4-5
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| Legacy `AV_CH_LAYOUT_*`/raw `uint64_t` channel masks | N/A to this phase (audio, Phase 6) | — | Not applicable here — noted only because CLAUDE.md flags it project-wide |
| Reading HDR metadata only from `AVFrame` side data (decode-required) | `AVCodecParameters.coded_side_data` (stream-level, no decode) available since libavcodec 60.30.100 (FFmpeg 6.1) | Oct 2023 | This project's target (FFmpeg 8.1/9.0) is well past this floor — `VIDEO-09`'s stream-level precedence arm is unconditionally available, confirmed via source read this session, not merely "should be available per version floor" |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Matroska/WebM muxing round-trips `-mastering_display`/`-content_light` the same way mp4 does | Priority Finding 1 | If wrong, the Matroska-equivalent half of D-09 needs its own recipe investigation before fixture-writing; low risk since mp4 is likely the primary container for these fixtures anyway |
| A2 | Exact Exp-Golomb bit layouts for a minimal valid H.264/HEVC SPS/PPS (field widths beyond what was directly read from the parser's own consumption order) | Priority Finding 3 | A malformed SPS/PPS causes `av_parser_parse2` to silently fail to populate `pict_type`/`key_frame` (not crash) — recommend a small round-trip spike (construct one candidate, run it through the real linked parser) before committing the full D-03 writer |
| A3 | `mjpeg` encoder is present on every one of the four `scripts/ffmpeg_pin.json` pinned builds (only confirmed on the Linux pinned build this session) | Priority Finding 4 / Open Questions | If absent on the Windows LGPL pinned build, an `mjpeg`-based yuvj420p fixture recipe would not be cross-platform-safe, mirroring exactly the risk D-09 was designed to avoid for HDR |
| A4 | A correctly-tagged `rawvideo` fixture can be made to round-trip `yuvj420p` through the mov demuxer with the right fourcc/tag option (not found within this session's time budget, but the pre-mux side showed the correct pix_fmt+range, suggesting it's a tagging problem, not a fundamental block) | Priority Finding 4 | If this path doesn't pan out either, VIDEO-03's fixture may require D-03-style hand-construction (writing the sample-entry pix_fmt tag directly), expanding D-01/D-02's scope beyond NAL/IDR |

**If this table is empty:** N/A — see entries above; everything else in this document that carries
a `[VERIFIED: ...]` tag was independently confirmed via direct source read or an executed command
this session, not recalled from training data.

## Open Questions

1. **Can VIDEO-03's "yuvj420p spelling" fixture be constructed with a real encoder at all, and if
   not, does D-04's scope need to expand?**
   - What we know: neither `mpeg4` nor `mpeg2video` (the project's two allowed real encoders)
     supports `AV_PIX_FMT_YUVJ420P` as an encoder input pix_fmt; requesting it anyway silently
     degrades to plain `yuv420p`/limited-range, defeating the fixture's purpose. `rawvideo` shows a
     correct pre-mux `yuvj420p(pc,...)` stanza but an unresolved `none` pix_fmt on read-back within
     this session's testing.
   - What's unclear: whether a correct `rawvideo` fourcc/tag combination resolves the read-back
     issue, whether `mjpeg` is cross-platform-pin-safe, or whether this needs D-01-style hand
     construction (writing the sample-entry `pix_fmt` tag directly via the Python writer).
   - Recommendation: run a short, focused spike (30 minutes, not a full research pass) on the
     `rawvideo` fourcc-tagging angle before defaulting to hand-construction; if hand-construction is
     chosen, it is a small, low-risk extension of D-03's already-established pattern (this document's
     DOVI section demonstrates the same technique for a different box).

2. **Does the Matroska-equivalent HDR box path (D-09's "or the Matroska equivalents") round-trip
   through `-mastering_display`/`-content_light` the same way mp4 does?**
   - What we know: the input-side attachment mechanism is container-independent (it attaches to the
     demuxed/generated AVStream before any muxer sees it).
   - What's unclear: whether `matroskaenc.c`/`matroskadec.c` write/read the equivalent Matroska
     tags from the same side-data fields.
   - Recommendation: a five-minute empirical check (same recipe, `.mkv` extension) before the
     planner commits fixture file extensions for VIDEO-09.

3. **Exact minimal SPS/PPS Exp-Golomb encoding for the D-03 writer** — see Assumption A2.
   Recommend a small isolated spike using the real linked `av_parser_parse2` (not a fresh reading of
   the H.264/HEVC spec from memory) as the acceptance oracle.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Pinned generator FFmpeg (Linux x64) | D-09/DOVI/pix_fmt empirical fixture recipes | Yes | 9.0.1 (martin-riedl.de) | — |
| vcpkg-built FFmpeg source tree (8.1) | source-level box/parser verification | Yes | n8.1-d2e2c4494d | — |
| Pinned generator FFmpeg (Windows/macOS/arm64-linux) | Cross-platform recipe validation | Not tested this session (Linux sandbox only) | 9.0.1 per `ffmpeg_pin.json` | Recipes chosen specifically to avoid needing GPL encoders, minimizing cross-platform risk even without direct testing |
| Python 3.11 | D-03 writer | Already pinned in CI (quick task `260815-m5g`) | — | — |

**Missing dependencies with no fallback:** none — this phase adds no new dependencies.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 3.15.3 (project-standard, CTest-integrated) |
| Config file | `CMakeLists.txt` (`Catch2::Catch2WithMain`, `catch_discover_tests()`) |
| Quick run command | `ctest --preset x64-linux -R <test_name_substring> --output-on-failure` |
| Full suite command | `ctest --preset x64-linux --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PROBE-03 | ParserScan populates pict_type/key_frame/repeat_pict/field_order/NAL types, <10% overhead | unit + integration | `ctest --preset x64-linux -R parser_scan` | ❌ Wave 0 |
| VIDEO-01/02/04 | Stream-param extraction, frame_count from scan | unit | `ctest --preset x64-linux -R video_stream_params` | ❌ Wave 0 |
| VIDEO-03 | yuvj420p range-fold = exactly one finding | integration (DOC-03 fixture pair) | `ctest --preset x64-linux -R doc03_coverage` (existing gate, extended) | ⚠️ blocked on Open Question 1 |
| VIDEO-05/12 | GOP open/closed via NAL type, no_parser degrade | unit (hand-verified NAL table, mirrors `test_ts_continuity.cpp`'s pattern) | `ctest --preset x64-linux -R gop_classification` | ❌ Wave 0 |
| VIDEO-06 | interlace field_order cross-check, `mixed` proportions | unit | `ctest --preset x64-linux -R interlace` | ❌ Wave 0 |
| VIDEO-07/08 | Colorimetry exact, unspecified-is-a-regression | integration (fixture pair per verified recipe) | `ctest --preset x64-linux -R doc03_coverage` | ❌ Wave 0 |
| VIDEO-09/10 | HDR precedence, coherence guard | integration (fixture pair, verified recipe) | `ctest --preset x64-linux -R doc03_coverage` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** targeted `ctest -R` filter for the touched analyzer.
- **Per wave merge:** full `ctest --preset x64-linux`.
- **Phase gate:** full suite green before `/gsd-verify-work`, plus `DOC-03`'s registry-enumerated
  fixture-pair count-equality assertion (`tests/integration/test_doc03_coverage.cpp`) covering every
  new `video.*`/`gop.*` check id.

### Wave 0 Gaps
- [ ] `tests/unit/test_parser_scan.cpp` — H.264/HEVC hand-constructed NAL sequences fed directly to
  `av_parser_parse2` (mirrors `tests/unit/test_ts_continuity.cpp`'s "hand-verified table, never
  through a whole run" discipline), proving pict_type/key_frame/NAL classification independent of
  any fixture file.
- [ ] `tests/unit/test_gop_classification.cpp` — IDR-vs-CRA / IDR-vs-non-IDR-I open/closed logic,
  same hand-verified-table pattern.
- [ ] `tools/gen_video_fixtures.py` — the D-03 writer itself (NAL bytes + `dvcC` box bytes).
- [ ] A short spike script resolving Open Question 1 (yuvj420p) before the fixture-recipe list is
  finalized.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A — no auth surface in this phase |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Yes | Every parser call operates on untrusted, potentially-crafted media bytes; `av_parser_parse2` itself is libav-hardened, but this project's OWN code around it (per-AU record accounting, NAL-type-sequence walk for GOP classification) must bounds-check exactly like Phase 3's `ts_scan.cpp`/`meta.cpp` precedent |
| V6 Cryptography | No | N/A — no crypto in this phase |

### Known Threat Patterns for this phase's stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Unbounded per-AU record growth from a hostile stream with millions of tiny AUs (parser called once per packet, same cardinality as `PacketRecord`) | Denial of Service | Same global probe-memory budget PacketScan already enforces (D-01, Phase 3) — ParserScan's records must be accounted against the same budget, not a separate unbounded one (per CONTEXT.md's own "Claude's Discretion" item on this) |
| A crafted SPS/PPS/VPS with an out-of-range field (e.g. `sps_id`/`pps_id` far outside libav's own `MAX_PPS_COUNT`) | Tampering | Handled internally by libav's own parser (`h264_parser.c:371-374` explicitly rejects `pps_id >= MAX_PPS_COUNT`); this project's wrapper must treat a parser-reported error as `skipped`/`no_parser`-equivalent for that AU, never propagate a raw libav error as a crash |
| A NAL-type-sequence walk (open/closed GOP classification) reading past the end of a truncated packet's own start-code search | Tampering / DoS | Mirrors T-3-18/T-3-41's class of below-threshold bounds issues Phase 3 left open beside this phase's own code — new NAL-walk code must bounds-check every start-code search against the packet's own declared size, never trust an assumed trailing NAL boundary |
| Resource exhaustion from the D-12 multi-minute overhead-measurement file generator (runs outside the normal corpus, on demand) | Denial of Service | D-12 already scopes this to "never enters `tests/fixtures/`... no per-leg generation cost" — the generator script itself should bound its own output size/duration with a named constant, mirroring every other gate's "self-tests, refuses to pass vacuously" pattern this project requires |

## Sources

### Primary (HIGH confidence — direct source read or executed command, this session)
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/mov.c` — mdcv/clli/dvcC parsing and side-data attachment (the actual linked FFmpeg 8.1 source)
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/dovi_isom.{c,h}` — DOVI config-record byte layout
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavutil/{dovi_meta.h,mastering_display_metadata.h,pixfmt.h}` — struct/enum ground truth
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/{h264_parser.c,h264.h,avcodec.h}` — H.264 parser internals, `av_parser_parse2` signature
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/hevc/{parser.c,hevc.h,ps.c}` — HEVC parser internals, VPS-before-SPS requirement
- `.ffmpeg-pinned/linux-x86_64/ffmpeg` (this repo's own pinned 9.0.1 binary) — every empirical CLI test in Priority Findings 1, 2, 4
- `src/probe/{packet_scan.h,pass.h,orchestrator.cpp}`, `src/core/model.h` — existing Phase 3 probe-layer architecture this phase extends
- `src/compare/tol.cpp`, `src/core/registry.h`, `src/core/tolerance.cpp` — existing generic tolerance-comparison machinery HDR checks should reuse
- `scripts/{install_pinned_ffmpeg.sh,gen_corpus.sh,ffmpeg_pin.json}` — cross-platform generator constraints

### Secondary (MEDIUM confidence)
- `claude_docs/03-video-analysis.md` — the phase's own design doc (project-authoritative, not externally verified this session for every clause)

### Tertiary (LOW confidence / flagged for validation)
- Matroska-equivalent HDR box round-trip (Assumption A1) — not independently tested
- Exact minimal SPS/PPS Exp-Golomb bit layouts (Assumption A2) — semantically verified (which fields, what order) but not byte-derived

## Metadata

**Confidence breakdown:**
- Priority Findings 1/2/4 (D-09, DOVI, pix_fmt gap): HIGH — executed commands with byte-level file inspection, cross-checked against the actual linked FFmpeg source
- Priority Finding 3 (NAL subset): HIGH for structural requirements (NAL types, ordering, key_frame logic), MEDIUM for exact bit-level SPS/PPS construction (flagged, not derived)
- Architecture (parser_scan fusion): HIGH — read directly from the existing orchestrator/packet_scan code this phase must extend
- Standard stack / pix_fmt table: HIGH — read directly from vendored FFmpeg headers

**Research date:** 2026-09-09
**Valid until:** tied to the pinned FFmpeg versions (8.1 linked / 9.0.1 generator) — re-verify empirical recipes if either pin changes

---

## Orchestrator Addendum — Open Question 1 RESOLVED (2026-09-09)

**Status: CLOSED. Do not plan around this as an open question.**

Open Question 1 asked whether VIDEO-03's `yuvj420p` fixture can be built at all, since neither
`mpeg4` nor `mpeg2video` accepts `yuvj420p` as encoder input. **It can.** The answer is `mjpeg`,
which the research pass did not test.

**Verified by the orchestrator against the pinned build**
(`.ffmpeg-pinned/linux-x86_64/ffmpeg`, `ffmpeg version 9.0.1-https://www.martin-riedl.de`):

```
$ ffmpeg -hide_banner -h encoder=mjpeg | grep -i "pixel formats"
    Supported pixel formats: yuvj420p yuvj422p yuvj444p yuv420p yuv422p yuv444p

$ ffmpeg -f lavfi -i "testsrc2=size=64x64:rate=25:duration=1" \
    -c:v mjpeg -pix_fmt yuvj420p -flags +bitexact -fflags +bitexact -y yuvj.mp4
$ ffprobe -show_entries stream=codec_name,pix_fmt,color_range -of csv=p=0 yuvj.mp4
mjpeg,yuvj420p,pc
```

`mjpeg` is a native FFmpeg codec rather than an external library, so it carries no GPL
dependency and satisfies `gen_corpus.sh`'s never-libx264/GPL convention exactly as `mpeg4` and
`mpeg2video` do. It requires **no** change to `ffmpeg_pin.json` and **no** hand construction
under D-01.

**Scope of this verification: the Linux pinned build only.** This does not discharge the
research pass's own assumption **A3** — that `mjpeg` is present on all four pinned builds,
notably BtbN's Windows `-lgpl` artifact. A3 remains open and is exactly the cross-platform
risk D-09 was designed to avoid for HDR. **The plan must verify `mjpeg` availability on every
pinned build before depending on it**, and the natural place is the existing preflight in
`scripts/check_corpus.sh`, which already runs unconditionally before Configure on all five
legs.

**Consequence for planning:** VIDEO-03's signature pair — the same intent spelled two ways,
which must produce exactly ONE finding on `video.color.range` — is constructible as:

- baseline: `-c:v mjpeg -pix_fmt yuvj420p`
- candidate: `-c:v mjpeg -pix_fmt yuv420p -color_range pc`

**D-04 does not need revisiting.** The researcher's caveat that Open Question 1 "may require
revisiting D-04's scope" is superseded — real encoders still cover every check they were scoped
to cover, and `mjpeg` simply joins `mpeg4`/`mpeg2video` as a third allowed built-in encoder.

### Also independently re-verified by the orchestrator

- **Priority finding 1 (D-09) — CONFIRMED.** With plain `-c:v mpeg4` and the codec-independent
  `-mastering_display` / `-content_light` options placed **before `-i`** (they are input-side
  options; placing them after the input errors with "you are trying to apply an input option to
  an output file"), the pinned 9.0.1 wrote real `clli` and `mdcv` boxes — observed in the raw
  bytes at file offsets `0x9800` and `0x9810` — and they round-tripped on demux as stream-level
  side data with rationals intact (`red_x=34000/50000`, `max_luminance=10000000/10000`,
  `max_content=1000`, `max_average=400`). Values coming back as rationals rather than floats
  matters for the project's rational-everywhere rule.
- **Priority finding 4 — CONFIRMED against the pinned build.** `mpeg4` reports
  `Supported pixel formats: yuv420p` only; `mpeg2video` reports `yuv420p yuv422p`. Neither
  accepts `yuvj420p`. The finding was correct; only its conclusion needed the `mjpeg` addition.

### Still open (unchanged)

- **Open Question 2** — whether the Matroska HDR path round-trips as mp4 does. Not tested.
- **Open Question 3** — exact minimal SPS/PPS Exp-Golomb bit layout for D-03's writer. The
  research pass recommends a small execution-time spike against the real linked parser; that
  recommendation stands.
