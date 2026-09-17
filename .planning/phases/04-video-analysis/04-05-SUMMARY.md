---
phase: 04-video-analysis
plan: 05
subsystem: probe
tags: [ffmpeg, h264, hevc, dolby-vision, isobmff, python, fixtures]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-01's Pass::parser_scan fusion and video.gop.length analyzer (the acceptance oracle every fixture in this plan is proven against); 04-02's REQUIRED_ENCODERS/gen_corpus.sh conventions and video_base.mp4/video_sar_4_3.mp4 carriers this plan's DOVI/SAR-conflict fixtures splice or patch"
provides:
  - "tools/gen_video_fixtures.py -- a Python 3.11 stdlib-only, deterministic writer that hand-constructs H.264/HEVC Annex-B elementary streams (IDR-vs-CRA open/closed GOP classification), a Dolby Vision dvcC configuration-record box, and a container-vs-bitstream SAR conflict, closing D-01/D-02/D-03"
  - "11 new corpus fixtures (video_h264_closed/idr48/open/refs1/refs4.h264, video_hevc_idr/cra.hevc, video_dovi_a/b/a_copy.mp4, video_sar_conflict.mp4), every one proven to parse through the real linked FFmpeg 8.1 via the shipped mediadiff binary before being wired into gen_corpus.sh"
  - "Research Open Question 3 answered in writing (module docstring): the exact minimal SPS/PPS/VPS/PPS Exp-Golomb field widths that make av_parser_parse2 accept a hand-built stream"
  - "A load-bearing empirical correction to 04-05-PLAN.md's own wording: the HEVC VPS-less known-bad control does not leave pict_type unset on this project's linked FFmpeg 8.1; the reliable observable is a strictly higher diagnostics.probe_warnings count"
  - "Empirical confirmation that codecpar->sample_aspect_ratio (bitstream, post-probe) and AVStream::sample_aspect_ratio (container, from the pasp box) are the two distinct libav fields VIDEO-04's conflict check must read"
affects: [04-09-video-gop-frame-types, 04-12-video-dovi-coherence]

actuals:
  tokens: 17265
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Spike-first, real-parser-as-oracle bitstream construction: one candidate SPS/PPS/VPS/slice is built, fed through the actual linked av_parser_parse2 via the shipped mediadiff binary (or a throwaway libavformat/libavcodec probe compiled directly against build/x64-linux/vcpkg_installed when a JSON-level observable does not exist yet, e.g. AV_PKT_DATA_DOVI_CONF or AVStream::sample_aspect_ratio), and only generalized into the writer once one access unit parses correctly -- never trusted from spec-reading alone."
    - "ISOBMFF box splicing as a validated, path-walking operation (locate_sample_entry -> splice_child_box / patch_pasp), not a raw byte-offset hack: every ancestor box's declared size is read, bounds-checked against its own enclosing region, and grown by exactly the inserted delta -- refusing rather than silently producing a size-inconsistent tree."
  patterns-established:
    - "Container-level pasp differs from bitstream-derived SAR after avformat_find_stream_info's own internal decode probe: AVStream::sample_aspect_ratio holds the raw pasp value; AVCodecParameters::sample_aspect_ratio (codecpar) gets overwritten by the decoder's own VOL-header value during probing. A future SAR-conflict check must read the two DIFFERENT libav fields, not two views of the same one."

key-files:
  created:
    - tools/gen_video_fixtures.py
  modified:
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "H.264 SPS uses pic_order_cnt_type=2 (profile_idc=66/Baseline), eliminating every picture-order-count field from both the SPS and every slice header -- av_parser_parse2 is proven to return immediately after frame_num (and, for IDR NALs, idr_pic_id), so the slice bitstream terminates there with rbsp_trailing_bits and is never spec-complete beyond it. This is the single biggest simplification the Task 1 spike found."
  - "video_h264_open.h264 and video_h264_closed.h264 both declare max_num_ref_frames=1 / num_ref_idx_l0_default_active_minus1=0 (ref_count[0]=1) so the H.264 parser's own key_frame heuristic (ref_frame_count<=1 && ref_count[0]<=1 && pict_type==I) fires on the open stream's non-IDR I slices exactly like real IDRs do on the closed stream -- proven empirically, not assumed."
  - "video_h264_refs1.h264 and video_h264_refs4.h264 both reuse the closed-GOP pattern (every keyframe position is a REAL IDR); they differ ONLY in their SPS's declared max_num_ref_frames (and, for refs4, the PPS ref_count[0]). Real IDRs set key_frame unconditionally regardless of ref count, so both fixtures report the SAME video.gop.length as video_h264_closed.h264 -- this plan's own writer does not itself exercise the 'additional keyframes' heuristic; that is left for 04-09 to discover with its own fixtures, per the plan's own instruction to record the caveat rather than force it to manifest here."
  - "The HEVC VPS-less known-bad control is asserted via diagnostics.probe_warnings (strictly higher for the VPS-less variant than an otherwise-identical VPS-present one), not via pict_type staying unset -- see Deviations."
  - "The dvcC box is spliced as the LAST child of the mp4v sample entry inside video_base.mp4's moov (which entirely follows mdat in this corpus's fixtures), so every ancestor box's size field is grown in place and no stco/co64 absolute offset into mdat is ever touched."
  - "The pasp patch overwrites the EXISTING pasp box's 8-byte hSpacing/vSpacing content in place (video_sar_4_3.mp4 already carries one, from its own setsar=4/3 encode) -- no enclosing box's size field changes at all for this fixture. patch_pasp also supports inserting a fresh pasp box via splice_child_box for a carrier that lacks one, though no shipped fixture currently exercises that path."

patterns-established:
  - "Spike-first, real-parser-as-oracle bitstream construction (see tech-stack.patterns)."
  - "Validated, path-walking ISOBMFF box splicing (see tech-stack.patterns)."

requirements-completed: []

coverage:
  - id: D1
    description: "tools/gen_video_fixtures.py: Python 3.11 stdlib-only writer producing deterministic H.264/HEVC Annex-B streams, a DOVI dvcC box, and a pasp SAR-conflict patch, with a --selftest exercising every known-bad/known-good control"
    verification:
      - kind: other
        ref: "python3 tools/gen_video_fixtures.py --selftest (OK); python3 -c \"import ast; ast.parse(open('tools/gen_video_fixtures.py').read())\"; grep -c '^import ' reports only argparse/os/shutil/subprocess/sys/tempfile"
        status: pass
    human_judgment: false
  - id: D2
    description: "11 hand-constructed fixtures wired into gen_corpus.sh behind a python3>=3.11 gate, visible to check_corpus.sh's mechanical extraction, hashed into the regenerated CORPUS_DIGEST.txt"
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh (135 verified, was 124) && bash scripts/lint_bash4_builtins.sh && bash scripts/assert_corpus_digest.sh (3 excluded lines)"
        status: pass
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure: 640/640 passed, 0 failed, 6 designated skips"
        status: pass
    human_judgment: false

duration: 55min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 05: Hand-Constructed Video Fixtures (H.264/HEVC/DOVI/SAR) Summary

**A Python 3.11 stdlib-only writer hand-constructs minimal H.264/HEVC Annex-B streams, a Dolby Vision `dvcC` configuration record, and a container-vs-bitstream SAR conflict -- every one proven to parse through the real linked FFmpeg 8.1 via the shipped `mediadiff` binary before being wired into `gen_corpus.sh`.**

## Performance

- **Duration:** 55 min
- **Started:** 2026-09-12T19:05:00Z (approx.)
- **Completed:** 2026-09-12T19:59:22Z
- **Tasks:** 3/3 completed
- **Files modified:** 4 (1 created: `tools/gen_video_fixtures.py`; 3 modified: `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST.txt`, `tests/fixtures/GENERATOR_MANIFEST.json`)

## Accomplishments

- `tools/gen_video_fixtures.py`: a single Python 3.11, stdlib-only writer (`BitWriter`/Exp-Golomb primitives, Annex-B NAL framing with emulation prevention, minimal H.264 and HEVC parameter-set/slice construction, an ISOBMFF box walker/splicer, a `dvcC` payload packer, and a `pasp` in-place patcher), every output written through a sibling temp file plus `os.replace`.
- Five H.264 Annex-B fixtures (`video_h264_closed/idr48/open/refs1/refs4.h264`) and two HEVC ones (`video_hevc_idr/cra.hevc`), each proven -- via the shipped `mediadiff inspect --json`, this plan's own designated acceptance oracle -- to report a real `video.gop.length`, never a skip.
- Three DOVI fixtures (`video_dovi_a/b/a_copy.mp4`), each verified via a throwaway `libavformat` probe compiled against `build/x64-linux/vcpkg_installed` to report `AV_PKT_DATA_DOVI_CONF` with the exact requested profile/level (8/6 and 5/4).
- One SAR-conflict fixture (`video_sar_conflict.mp4`), verified via a throwaway decode probe to produce a genuine divergence: `AVStream::sample_aspect_ratio` (container, patched to 1:1) versus `AVCodecParameters::sample_aspect_ratio` (bitstream-derived during `avformat_find_stream_info`'s own internal decode probe, still 4:3) -- both values captured below.
- All 11 fixtures wired into `scripts/gen_corpus.sh` behind a `python3 >= 3.11` interpreter gate, in one invocation with every output path passed as a literal `$OUT_DIR/<name>` token; `tests/golden/CORPUS_DIGEST.txt` regenerated once for the enlarged 135-fixture corpus.

## Task Commits

Tasks 1 and 2 were committed together (see Issues Encountered for why); Task 3 separately:

1. **Tasks 1+2: H.264/HEVC writer, DOVI dvcC box, SAR-conflict pasp patch** - `92490a3` (feat)
2. **Task 3: Wire into gen_corpus.sh, regenerate digest** - `94713ad` (feat)

## Files Created/Modified

- `tools/gen_video_fixtures.py` - the full writer: bit-level primitives, H.264/HEVC parameter-set/slice construction, ISOBMFF box splicing, `dvcC`/`pasp` helpers, named fixture builders, `--selftest`, and the CLI
- `scripts/gen_corpus.sh` - one new block (python3 version gate + a single writer invocation covering all 11 fixtures), appended after the 04-04 HDR block and after `video_base.mp4`/`video_sar_4_3.mp4` already exist
- `tests/golden/CORPUS_DIGEST.txt` - regenerated once for the enlarged 135-fixture corpus (still 3 excluded lines)
- `tests/fixtures/GENERATOR_MANIFEST.json` - `generated_at` timestamp only

## Decisions Made

See `key-decisions` in the frontmatter above for the full list. In short: `pic_order_cnt_type=2` eliminates all H.264 POC syntax; open vs. closed GOP for H.264 leans on the parser's own `ref_frame_count<=1` heuristic (both fixtures share `max_num_ref_frames=1`); `refs1`/`refs4` reuse the closed-GOP pattern and differ only in their SPS's declared ref count (real IDRs make the heuristic irrelevant to their own `gop.length`, so the "additional keyframes" behavior the heuristic can produce is documented for 04-09 rather than forced to appear here); the HEVC VPS-less control is asserted via `probe_warnings`, not `pict_type` (see Deviations); the `dvcC`/`pasp` box operations reuse a single validated, size-checked ISOBMFF walker.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug/empirical correction] The HEVC VPS-less known-bad control does not leave `pict_type` unset on this project's linked FFmpeg 8.1**
- **Found during:** Task 2's spike, building the VPS-less known-bad control the plan's own Test 3 calls for.
- **Issue:** 04-05-PLAN.md's Task 2 `<behavior>` describes the control as one that "fails to yield a parsed picture type." A throwaway `av_parser_parse2` probe (compiled directly against `build/x64-linux/vcpkg_installed`, mirroring 04-01/04-02's own established pattern) showed the opposite: a VPS-less access unit still reports `pict_type=1`/`key_frame=1` even though libav logs explicit `VPS 0 does not exist` / `SPS 0 does not exist` / `PPS id out of range` errors -- the lightweight parser appears to derive these fields from the raw `slice_type`/NAL-type bytes before the PPS/SPS/VPS lookup chain that actually fails.
- **Fix:** Verified the REAL, reliably observable signal is the shipped `mediadiff` binary's own `diagnostics.probe_warnings` count: a VPS-less stream reports strictly MORE probe warnings than an otherwise-identical VPS-present stream (empirically: 44 vs. 25 warnings for a 12-access-unit stream; 11 vs. 3 for a single access unit), because mediadiff's probe layer counts libav's own logged errors. `--selftest`'s known-bad control asserts this comparison instead of asserting on `pict_type`.
- **Files modified:** `tools/gen_video_fixtures.py` (`selftest`, module docstring documents the correction in full)
- **Verification:** `python3 tools/gen_video_fixtures.py --selftest` passes; the assertion fails loudly (and was observed to fail during development before the correction) if the VPS-less stream's warning count is not strictly higher.
- **Committed in:** `92490a3`

**2. [Rule 1 - Bug in my own selftest, not the fixture] `video_h264_refs4.h264`'s self-test assertion was based on a wrong construction model**
- **Found during:** Task 1's own selftest run, before the first commit.
- **Issue:** My first selftest draft asserted `video_h264_refs4.h264` would report `video.gop.length` as `skipped: insufficient_data` (reasoning from an open-GOP-style construction that never shipped). The actual fixture, per the plan's own text ("identical to `video_h264_closed.h264` except the SPS declares four"), reuses the closed-GOP pattern of REAL IDRs at every keyframe position -- and a real IDR sets `key_frame` unconditionally regardless of `ref_frame_count`, so `video.gop.length` correctly reports the same `24` as the closed stream.
- **Fix:** Corrected the selftest assertion to check `video.gop.length.num == 24` (matching `video_h264_closed.h264`) instead of expecting a skip. No change to the fixture-generation logic itself was needed -- only the test's own expectation was wrong.
- **Files modified:** `tools/gen_video_fixtures.py` (`selftest`)
- **Verification:** `python3 tools/gen_video_fixtures.py --selftest` passes.
- **Committed in:** `92490a3` (caught before the first commit; no separate fix-up commit needed)

---

**Total deviations:** 2 (1 empirical correction to the plan's own literal wording, recorded prominently per the plan's own instruction to report rather than silently work around; 1 self-caught error in my own draft self-test, corrected before committing). **Impact on plan:** Both are necessary for correctness. No scope creep -- every shipped fixture matches the plan's own recipe exactly; only the ASSERTION used to prove the VPS-less control's failure mode changed, because the plan's literal wording about the failure symptom did not hold empirically on this project's actually-linked FFmpeg 8.1.

## Issues Encountered

**Tasks 1 and 2 committed together.** `tools/gen_video_fixtures.py`'s H.264 half (Task 1) and HEVC/DOVI/pasp half (Task 2) share a `BitWriter`, `write_atomic`, and a single combined `--selftest` function and CLI -- considerably more interdependent than 04-04's own precedent for this exact situation (two independent, appendable shell-script blocks). Splitting the diff into two commits after the fact would have meant temporarily hacking apart already-validated, working code purely to satisfy commit granularity, a materially higher-risk move for a plan whose own `must_haves.prohibitions` list is this strict about correctness. Documented here rather than attempted; both tasks' own acceptance criteria are independently verified below regardless of commit boundary.

**`avformat_find_stream_info`'s own internal decode probe overwrites `codecpar->sample_aspect_ratio` with the bitstream value for mpeg4-in-mp4**, discovered while proving the SAR-conflict fixture actually conflicts (Task 2's Flagged Assumption A3). A naive read of `codecpar->sample_aspect_ratio` alone shows NO divergence after the `pasp` patch (`4/3` before and after) -- the deprecated but still-populated `AVStream::sample_aspect_ratio` field (set directly from `pasp` at demux time, never touched by the internal decode probe) is what actually carries the container value. Verified via a throwaway decode probe; recorded in `key-decisions`/`tech-stack.patterns` above for plan 04-07's `video.sar`/`video.sar.conflict` implementation to consume rather than rediscover.

## Read-Back Verification Tables

### H.264 / HEVC (via the shipped `mediadiff inspect --json`, the acceptance oracle)

| File | container.format | video.gop.length | keyframe_count |
|---|---|---|---|
| video_h264_closed.h264 | h264 | 24 | 4 |
| video_h264_idr48.h264 | h264 | 48 | 2 |
| video_h264_open.h264 | h264 | 24 | 4 (non-IDR I slices heuristically flagged) |
| video_h264_refs1.h264 | h264 | 24 | 4 |
| video_h264_refs4.h264 | h264 | 24 | 4 (real IDRs only; ref count irrelevant here) |
| video_hevc_idr.hevc | hevc | 24 | 4 |
| video_hevc_cra.hevc | hevc | 24 | 4 (CRA_NUT sets key_frame unconditionally, like IDR) |

### DOVI (via a throwaway `libavformat` probe against the linked FFmpeg 8.1)

| File | AV_PKT_DATA_DOVI_CONF | profile | level | rpu/el/bl | compat_id |
|---|---|---|---|---|---|
| video_dovi_a.mp4 | present | 8 | 6 | 1/0/1 | 4 |
| video_dovi_b.mp4 | present | 5 | 4 | 1/0/1 | 4 |
| video_dovi_a_copy.mp4 | byte-identical to video_dovi_a.mp4 (`cmp` confirms) | -- | -- | -- | -- |

### SAR conflict (via a throwaway decode probe against the linked FFmpeg 8.1)

| File | AVStream::sample_aspect_ratio (container) | AVCodecParameters::sample_aspect_ratio (bitstream, post-probe) |
|---|---|---|
| video_sar_4_3.mp4 (unpatched) | 4/3 | 4/3 |
| video_sar_conflict.mp4 (pasp patched to 1:1) | **1/1** | **4/3** |

## Requirement Gate

`gsd_run query requirements.ready-ids` was run against this plan's four declared requirements (`PROBE-03`, `VIDEO-04`, `VIDEO-05`, `VIDEO-09`) before any completion marking was attempted, per this execution's mandatory `<requirement_marking_guard>`:

```json
{"ready": [], "blocked": ["PROBE-03", "VIDEO-04", "VIDEO-05", "VIDEO-09"], "total": 4}
```

All four are **blocked** (later plans also declare them). **Nothing was marked complete.** No call to `requirements.mark-complete` was made.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Plan 04-09 (GOP/frame-types) now has `video_h264_closed/idr48/open/refs1/refs4.h264` and `video_hevc_idr/cra.hevc` -- open/closed classification proven byte-provably distinguishable by NAL type alone, and the `refs1`/`refs4` ref-count heuristic caveat recorded for its own fixture design. Plan 04-12 (DOVI/coherence) now has `video_dovi_a/b/a_copy.mp4` with confirmed `AV_PKT_DATA_DOVI_CONF` side data. Plan 04-07 (SAR/frame-rate) now has `video_sar_conflict.mp4` with a confirmed, field-precise divergence (`AVStream::sample_aspect_ratio` vs. `AVCodecParameters::sample_aspect_ratio`) to read from -- this SUMMARY's own finding on which two libav fields actually diverge is the durable record that plan should consume rather than rediscover. No blockers.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED

All 1 created file (`tools/gen_video_fixtures.py`) and all 3 modified files confirmed present on disk with the expected content; both commit hashes (`92490a3`, `94713ad`) confirmed present in `git log --oneline --all`.
