---
phase: 07-content-quality
plan: 07
subsystem: video
tags: [hdr, mastering-display, content-light, first-frame, side-data, h264-sei, video.hdr.mdcv, video.hdr.cll, d-08, video-09]

requires:
  - phase: 04-video-analysis
    provides: "the D-08 precedence seam in video_hdr_analyzer (resolve_hdr_source's requires_decode branch) and the stream-arm extraction in demux_session.cpp with the T-4-48 short-payload guard"
  - phase: 07-content-quality
    plan: 01
    provides: "the fused video decode sweep (VideoDecodeState) and the --no-content clearing of Pass::video_decode"
  - phase: 07-content-quality
    plan: 04
    provides: "D-08: a sink that does not own the --sample stride sees every decoded frame"
  - phase: 07-content-quality
    plan: 05
    provides: "the skip-reason convention for decode-fed checks (partial_scan / insufficient_data)"
  - phase: 07-content-quality
    plan: 06
    provides: "video_pcm_hdr.h264 (SEI 137 + 144 in the first access unit, BT.2020/PQ VUI, no stream-level entry) and video_pcm_plain.h264"
provides:
  - "probe/hdr_static.{h,cpp}: HdrStaticMetadata and read_mdcv_side_data / read_cll_side_data, the ONE converter from AVMasteringDisplayMetadata / AVContentLightMetadata, size-guarded (T-4-48)"
  - "StreamVideoDecode::first_frame_hdr: the first decoded frame's MDCV/CLL, read independent of --sample, engaged once a frame exists even when both entries are absent"
  - "video.hdr.mdcv/.luminance/.primaries and video.hdr.cll/.max/.avg report frame-level metadata with evidence source: \"frame\", stream-level keeping precedence"
  - "H.264 in detail::could_carry_frame_level_hdr; detail::could_carry_frame_level_dovi keeps the old hevc/av1 table"
affects: [07-14, 07-15]

actuals:
  tokens: 29950
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Two extraction arms converge on one guarded reader, so the second arm cannot drift from the first's rationals"
    - "A per-stream resolution (source kind plus the metadata it points at) replaces a bare presence flag: the same ladder yields value, real absence, or a named skip"
    - "Decision branches a real fixture cannot reach are driven with a hand-built decode result over a real DemuxSession"

key-files:
  created:
    - src/probe/hdr_static.h
    - src/probe/hdr_static.cpp
    - tests/integration/test_hdr_first_frame.cpp
    - .planning/phases/07-content-quality/deferred-items.md
  modified:
    - src/probe/demux_session.cpp
    - src/probe/video_decode.h
    - src/probe/video_decode.cpp
    - src/analyzers/video/hdr.cpp
    - src/analyzers/video/analyzers.h
    - CMakeLists.txt
    - tests/unit/test_video_hdr.cpp
    - tests/integration/CMakeLists.txt
    - docs/checks/video.hdr.mdcv.md
    - docs/checks/video.hdr.cll.md
    - docs/checks/video.hdr.mdcv.luminance.md
    - docs/checks/video.hdr.dovi.md
    - docs/checks/video.hdr.dovi.config.md
    - claude_docs/03-video-analysis.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "Stream-level metadata keeps precedence per family (mdcv and cll resolve independently), so every existing video_hdr_* fixture is byte-identical and keeps source: \"stream\""
  - "The first frame is read at decode index 0 ahead of every early return in consume_frame (budget latch, unhashable frame) and independent of the stride: the arm needs only that the decoder handed over frame 0"
  - "Skip ladder for a frame-capable codec with no stream entry: no decode result -> requires_decode; decode ran but no first frame -> partial_scan (truncated, undecodable, or a cut-short packet scan) or insufficient_data (complete, zero frames); first frame read with nothing on it -> a real Absent{}"
  - "Value checks with nothing to measure keep requires_decode for the legacy cases and use insufficient_data / partial_scan for the frame arm's own cases, because requires_decode would tell a user to decode something that was decoded"
  - "video.hdr.dovi gets its own codec table (hevc, av1) instead of following the widened HDR10 table, otherwise every H.264 stream's dovi absence becomes a permanent, unfixable requires_decode skip"
  - "video.hdr.coherence is left stream-level only and logged in deferred-items.md: making it frame-aware makes its value pass-dependent (D-12), a decision not a bug fix"

patterns-established:
  - "Adding a codec to a capability table changes every stream of that codec: diff snapshots of the whole corpus before and after and explain each changed id"

requirements-completed: [VIDEO-09]

coverage:
  - id: D1
    description: "video_pcm_hdr.h264 (SEI 137 + 144 in frame 0, no stream-level entry) reports mdcv, luminance, primaries, cll, cll.max and cll.avg with source frame and the exact rationals the writer encoded (R, G, B reorder, white point, 1000/0.005 cd/m2, MaxCLL 1000, MaxFALL 400)"
    requirement: VIDEO-09
    verification:
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - h264 sei"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - the frame-sourced values survive a snapshot round trip"
        status: pass
    human_judgment: false
  - id: D2
    description: "Stream-level precedence: all nine existing video_hdr_* mp4 fixtures keep source stream and measurements identical with and without the decode pass; a whole-corpus before/after snapshot diff changed only video.hdr.mdcv/cll ids on the H.264 and HEVC fixtures"
    requirement: VIDEO-09
    verification:
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - stream precedence"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp (every pre-existing case, unchanged)"
        status: pass
    human_judgment: false
  - id: D3
    description: "With the decode pass and no entry on the first frame, a frame-capable codec reports a real Absent with no skip reason; under --no-content it reports skipped:requires_decode on both sides of a compare"
    requirement: VIDEO-09
    verification:
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - real absence with decode"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - no content"
        status: pass
    human_judgment: false
  - id: D4
    description: "The no-first-frame branches: undecodable -> partial_scan, decode or packet scan cut short before a frame -> partial_scan, complete decode with zero frames -> insufficient_data, a first frame that was read stays valid after a later truncation (hand-built and real frame-record truncation)"
    requirement: VIDEO-09
    verification:
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - undecodable"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - a decode cut short before the first frame is partial_scan"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - a complete decode with zero frames is insufficient_data"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - a first frame that was read stays valid when the decode later stopped"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - a frame-record truncation after the first frame keeps the value"
        status: pass
    human_judgment: false
  - id: D5
    description: "One conversion path with the T-4-48 guard: the shared reader fills every rational exactly and records a one-byte-short payload as short without reading it"
    requirement: VIDEO-09
    verification:
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp#video_hdr - shared reader mdcv"
        status: pass
      - kind: unit
        ref: "tests/unit/test_video_hdr.cpp#video_hdr - shared reader cll"
        status: pass
    human_judgment: false
  - id: D6
    description: "D-08: the first frame is read whatever --sample N is (strides 1, 2, 7, 1000), and a real difference (plain vs HDR stream) is one non-pass finding per presence check, exit non-zero, clean on identical bytes"
    requirement: VIDEO-09
    verification:
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - sampling independent"
        status: pass
      - kind: integration
        ref: "tests/integration/test_hdr_first_frame.cpp#hdr_first_frame - trigger pair"
        status: pass
    human_judgment: false
  - id: D7
    description: "The frame arm against a real HEVC or AV1 bitstream carrying its metadata only in SEI/OBU: no fixture exists (no LGPL encoder for either in REQUIRED_ENCODERS), so HEVC and AV1 rely on the same libavcodec frame side-data export the H.264 proof exercises"
    requirement: VIDEO-09
    verification: []
    human_judgment: true
    rationale: "Automation proves the frame arm on H.264 only; HEVC/AV1 are in the codec table but unexercised on a real HDR-carrying bitstream"

duration: 25min
completed: 2026-10-01
status: complete
---

# Phase 7 Plan 07: VIDEO-09 first-frame HDR arm Summary

**video.hdr.mdcv/cll now report the first decoded frame's mastering-display and content-light side data (source: "frame") for H.264, HEVC and AV1 streams with no stream-level entry, through one size-guarded reader shared with the stream arm, with stream-level precedence and a real-absence / named-skip ladder.**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-10-01T01:05Z (approx.)
- **Completed:** 2026-10-01T01:30Z (approx.)
- **Tasks:** 3
- **Files modified:** 19 (4 created)

## Accomplishments

- `probe/hdr_static.{h,cpp}` is the only place `AVMasteringDisplayMetadata` / `AVContentLightMetadata` are read; `demux_session.cpp` (stream arm) and `video_decode.cpp` (first-frame arm) both go through it, so the T-4-48 guard and every rational are identical between arms.
- The decode sink reads frame 0's side data into `StreamVideoDecode::first_frame_hdr`, ahead of every early return and independent of `--sample`.
- `video_pcm_hdr.h264` reports all six checks present, `source: "frame"`, with the exact values the writer encoded, surviving a snapshot round trip.
- A real `Absent{}` (no skip reason) once decode ran and the first frame carried nothing; `skipped:requires_decode` under `--no-content`; `partial_scan` / `insufficient_data` when no first frame was read.
- Existing output is otherwise untouched, proven by a whole-corpus before/after snapshot diff (see below).

## Task Commits

1. **Task 1: One HDR static-metadata conversion path for both arms, and first-frame capture in the decode sink** - `6eb2e58` (feat)
2. **Task 2: The frame arm in video_hdr_analyzer, with precedence and honest skips** - `64aa4ae` (feat)
3. **Task 3: Record VIDEO-09's completion in the open, full suite and lints** - `d38d886` (docs)

**Plan metadata:** recorded in the `docs(07-07)` close-out commit.

## Files Created/Modified

- `src/probe/hdr_static.{h,cpp}` - shared HDR static-metadata reader
- `src/probe/demux_session.cpp` - stream arm routed through it
- `src/probe/video_decode.{h,cpp}` - `first_frame_hdr`, `tap_first_frame_hdr`
- `src/analyzers/video/hdr.cpp`, `analyzers.h` - `FrameArm`, `HdrResolution`, the widened `HdrSourceKind`, h264 in the table, dovi's own table
- `tests/integration/test_hdr_first_frame.cpp` - 16 tests (prefix `hdr_first_frame - `)
- `tests/unit/test_video_hdr.cpp` - shared-reader tests, dovi table test, the one changed assertion
- `docs/checks/video.hdr.{mdcv,cll,mdcv.luminance,dovi,dovi.config}.md`, `claude_docs/03-video-analysis.md`, `.planning/REQUIREMENTS.md` - the arm described in the open

## Decisions Made

See `key-decisions` in the frontmatter. The two worth repeating: the dovi check keeps its own `hevc`/`av1` table, and `video.hdr.coherence` was deliberately left stream-level (see Issues Encountered).

## Existing outputs changed by adding `h264` to `could_carry_frame_level_hdr`

Proof method: `mediadiff snapshot` of every file in `tests/fixtures` (237 files) and `compare --no-content --json f f` of every file, before (HEAD 36c11c5) and after, diffed per measurement. 224 files are byte-identical in both. Every changed file differs only in the six HDR ids below, and every other measurement and every envelope field is identical in the full snapshots.

Changed in a default (decoding) run, old -> new (`video.hdr.mdcv`/`video.hdr.cll` presence; the four value checks move the same way):

| Fixture | Old | New | Cause |
|---|---|---|---|
| `video_h264_closed/_closed_copy/_idr48/_open/_refs1/_refs4.h264`, `video_pcm_plain/_cc/_crop.h264` | `Absent{}` no skip, `could_carry_frame_level: false`; value checks `requires_decode` | `Absent{}` no skip, `could_carry_frame_level: true`, `decode_available: true`; value checks `insufficient_data` | h264 now frame-capable; decoded, first frame carried nothing |
| `video_pcm_hdr.h264` | `Absent{}`; value checks `requires_decode` | `present`, `source: "frame"`, all six values | the frame arm |
| `video_huge_dims.h264` | `Absent{}` no skip | `skipped:requires_decode` | h264 frame-capable, stream not attempted (max_pixels) |
| `video_hevc_idr.hevc`, `video_hevc_cra.hevc` | `skipped:requires_decode` | `skipped:insufficient_data`, `reason: no_decoded_frames` | decode ran to a clean end with zero frames (07-05/07-06 convention) |

Changed under `--no-content`: the same H.264 fixtures' `video.hdr.mdcv` and `video.hdr.cll` move from `pass` ("both absent") to `skipped:requires_decode`, with `could_carry_frame_level: true`; the envelope `pass`/`skipped` counts move with them. HEVC fixtures under `--no-content` are unchanged.

The only pre-existing test assertion that moved is `video_hdr - could_carry_frame_level_hdr ...`, which asserted `h264` was `false`; it now asserts `true`, with the comment saying so. No other test and no golden changed. All existing `video_hdr_*` fixtures keep `source: "stream"` and byte-identical measurements (the `stream precedence` test asserts this against the no-decode run, and the diff above confirms it against HEAD).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `video.hdr.dovi` would have regressed for every H.264 stream**
- **Found during:** Task 2
- **Issue:** `resolve_dovi_source` reused `could_carry_frame_level_hdr`. Adding `h264` to that table would turn every H.264 stream's dovi absence from a real `Absent{}` into a permanent `skipped:requires_decode` that no decode pass can ever resolve (Dolby Vision has no frame arm).
- **Fix:** `detail::could_carry_frame_level_dovi` keeps the old `hevc`/`av1` table and `resolve_dovi_source` uses it; dovi docs say so. Dovi output is byte-identical for every fixture (confirmed by the snapshot diff, which changed no dovi id).
- **Files modified:** `src/analyzers/video/hdr.cpp`, `src/analyzers/video/analyzers.h`, `docs/checks/video.hdr.dovi.md`, `docs/checks/video.hdr.dovi.config.md`, `tests/unit/test_video_hdr.cpp`
- **Verification:** `video_hdr - could_carry_frame_level_dovi ...`, `hdr_first_frame - real absence with decode` (dovi Absent with `could_carry_frame_level: false`)
- **Committed in:** `64aa4ae`

### Plan wording replaced by the settled convention

**2. Skip reasons for "decode ran but no first frame"** (orchestrator direction, citing `07-05-SUMMARY.md` and `07-06-SUMMARY.md`). The plan's truth says zero frames -> `skipped:partial_scan`. Implemented as the convention instead: undecodable, a truncated decode, or a cut-short packet scan before any frame -> `partial_scan` (evidence `reason`, plus `decode_truncation_reason` when truncated); a complete decode with zero frames -> `insufficient_data`. A first frame that was read stays valid however the decode ended later. Both branches are tested.

**3. Plan Test 6's fixture premise was wrong.** It expected `video_h264_closed.h264` to be undecodable and report `partial_scan`. That fixture decodes (ten-odd frames) and correctly reports a real `Absent{}`; only `video_huge_dims.h264` cannot decode, and it is not attempted (max_pixels), so it reports `requires_decode`. The undecodable and cut-short branches are driven by a hand-built decode result over a real `DemuxSession`, plus one real frame-record truncation. Recorded rather than forcing the test to lie.

### Additions beyond the plan text

**4.** The analyzer declares `Pass::packet_scan` alongside `Pass::video_decode` (the plan says add `Pass::video_decode`): it reads `ProbeResults::packet_scan` to tell a cut-short scan from an empty stream. The orchestrator already implies packet_scan from video_decode, so the pass union is unchanged.

**5.** Value checks use `insufficient_data` / `partial_scan` for the frame arm's own no-value cases and keep `requires_decode` for the pre-existing ones (stream entry without the field, non-carrying codec, no decode result), so existing outputs do not move.

**6.** Task 3 touched none of `tests/integration/test_video_inspect_section.cpp` or `tests/integration/test_timeline_structure.cpp`: the full suite needed no declared-set change, so there was nothing to add and (per the acceptance criterion) nothing loosened. `git diff main -- tests/integration/*.cpp | grep -E '^-\s+"[a-z_.]+",' | wc -l` prints 0.

---

**Total deviations:** 1 auto-fixed (Rule 1), 1 fixture-premise correction, 1 convention-driven wording change, 3 additions.
**Impact on plan:** None changes the plan's goal. Item 1 prevents a real regression; items 2-3 follow the settled convention and the real corpus.

## Issues Encountered

- **`video.hdr.coherence` is still stream-level only (not fixed, needs a decision).** For `video_pcm_hdr.h264` the mdcv check now says `present` (frame) while coherence says `pq_without_mdcv`. 07-06's writer comment calls that fixture a coherent HDR10 set. Making coherence frame-aware makes its value depend on which passes ran (D-12) unless the approved four-value vocabulary changes, so it was left alone and written up in `.planning/phases/07-content-quality/deferred-items.md`. Severity is `info` in every profile; it never gates.
- **HEVC and AV1 are unproven on a real HDR-carrying bitstream.** No LGPL encoder for either is in `REQUIRED_ENCODERS`, so there is no fixture. They are in the codec table and use the same libavcodec frame side-data export the H.264 test exercises; the stream/frame logic is codec-agnostic. The existing `video_hevc_*` fixtures decode zero frames and report `insufficient_data`.
- **Designated-leg goldens:** none contains an H.264 stream or an HDR id (inspect_container, the three ts_scan goldens, size_checks). `MEDIADIFF_DESIGNATED_LEG=1 ctest -R "inspect_container|ts_scan_golden|size_checks"` passes 16/16 on the current corpus without regenerating it. No golden, corpus digest line or fixture changed, so nothing was appended to `.planning/WINDOWS.md`.

## Known Stubs

None.

## Threat Flags

None. The two planned mitigations are in place: T-07-21 (the shared reader's size guard, tested one byte short for both payloads and through the analyzer with a short frame payload) and T-07-22 (the frame arm feeds the unchanged `quantize_chromaticity` / luminance-denominator paths).

## Verification

- `cmake --build --preset x64-linux`: no work left; `ctest --preset x64-linux`: 1404/1404 pass (was 1385 at HEAD 36c11c5; the six local skips are the designated-leg goldens and `unit.console_vt`).
- All ten ci.yml lint scripts pass; `scripts/test_gen_corpus_pin_gate.sh` passes (40 assertions).
- Mutation check: disabling the first-frame capture fails six `hdr_first_frame` tests.
- Acceptance greps: `reinterpret_cast<const AVMasteringDisplayMetadata` appears 0 times in `demux_session.cpp` and once in `hdr_static.cpp`; `read_mdcv_side_data` appears in both `demux_session.cpp` and `video_decode.cpp`; `"h264"`, `Pass::video_decode` and `"frame"` are in `hdr.cpp`; `amended, 07-07-PLAN.md` is on the VIDEO-09 row.
- The 07-06 fixtures and `tests/golden/CORPUS_DIGEST*.txt` are untouched; no fixture was regenerated.

## Requirement VIDEO-09

The requirement text asks for `hdr.mdcv`, `hdr.cll` and `hdr.dovi` with extraction from the stream-level `coded_side_data` (Phase 4, proven by the unchanged `video_hdr_*`/`video_dovi_*` tests) and the first-frame side-data source deferred to Phase 7 (proven here for H.264). Marked complete. The one clause not proven on a real bitstream is the HEVC/AV1 first-frame path (D7 above).

## Next Phase Readiness

- The decode sweep now carries a per-stream `first_frame_hdr`; 07-14/07-15 are unaffected.
- Open item for a human: the `video.hdr.coherence` decision in `deferred-items.md`.

## Self-Check: PASSED

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
