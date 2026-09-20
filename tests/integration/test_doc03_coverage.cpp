// DOC-03 (03-11-PLAN.md Task 4): every check the built registry currently
// registers needs at least one fixture pair that TRIGGERS it (a real,
// observable, non-clean finding) and one that comes back CLEAN (a real
// `pass`, not merely "the check never ran"). Enumerated from
// core::builtin_registry() itself -- never from a hand-maintained id list
// -- so a newly registered check with no declared pair fails THIS gate by
// name, rather than silently shipping uncovered. The same discipline
// tests/unit/test_fail_first_coverage.cpp already applies per
// (semantic, status) cell, extended here to per-check-id across the real
// CLI, real fixtures and the real production registry.
//
// Twenty-seven checks were registered across plans 03-02, 03-04, 03-05,
// 03-06, 03-08 and 03-09, joining Phase 2's original three
// (meta.tool_version/missing_candidate/extra_candidate) -- thirty as of
// Phase 3. 04-01-PLAN.md registers Phase 4's tracer, `video.gop.length`,
// bringing the total to thirty-one; 04-06-PLAN.md registers the five
// per-video-stream identity checks (video.codec/profile/level/resolution/
// frame_count), bringing the total to thirty-six. 04-07-PLAN.md registers
// video.sar/video.dar/video.sar.conflict and the two video.frame_rate.*
// checks, bringing the total to forty-one. 04-08-PLAN.md registers
// video.pix_fmt and the five colour-identity checks
// (video.color.range/primaries/transfer/matrix/chroma_loc), bringing the
// total to forty-seven. 04-09-PLAN.md registers video.gop.idr_interval,
// video.gop.closed, video.gop.refs and video.frame_types, bringing the
// total to fifty-one. 04-10-PLAN.md registers video.interlace, bringing
// the total to fifty-two. 04-11-PLAN.md registers video.hdr.mdcv/.
// luminance/.primaries and video.hdr.cll/.max/.avg, bringing the total to
// fifty-eight. 04-12-PLAN.md registers video.hdr.dovi, video.hdr.dovi.config
// and video.hdr.coherence, bringing the total to sixty-one. 05-01-PLAN.md
// registers Phase 5's tracer, timeline.start, bringing the total to
// sixty-two. 05-04-PLAN.md registers timeline.duration and
// timeline.duration.coherence, bringing the total to sixty-four.
// 05-05-PLAN.md registers timeline.dts_monotonic and timeline.pts_unique,
// bringing the total to sixty-six. 05-06-PLAN.md registers timeline.gaps
// and timeline.wrap_events, bringing the total to sixty-eight.
// 05-07-PLAN.md registers timeline.discontinuities and timeline.
// discontinuities.flagged, bringing the total to seventy. 05-08-PLAN.md
// registers timeline.jitter and timeline.vfr_profile, bringing the total
// to seventy-two. 05-09-PLAN.md registers timeline.av_offset, bringing the
// total to seventy-three. 05-10-PLAN.md registers timeline.av_drift and
// timeline.av_drift.pattern, bringing the total to seventy-five.
// 05-11-PLAN.md registers timeline.timecode and timeline.timecode.value,
// bringing the total to seventy-seven -- the full 16-id Phase 5 timeline
// roster, closing out this phase's own DOC-03 obligation. 06-01-PLAN.md
// registers Phase 6's tracer, content.audio.sample_hash, bringing the
// total to seventy-eight. 06-03-PLAN.md registers the six per-audio-
// stream identity checks (audio.codec/sample_rate/sample_fmt/bit_depth/
// channels/layout), bringing the total to eighty-four. This file is
// where a gap becomes visible.
//
// Every declared pair below was proven empirically against the real
// binary before being committed here (never guessed from a fixture's
// name alone) -- see this task's own commit message for the exact
// verification transcript.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/registry.h"
#include "support/fixture_paths.h"

using mediadiff::CheckRegistry;
using mediadiff::builtin_registry;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }
std::string snapshot(const std::string& name) { return mediadiff::test::snapshot_dir() + "/" + name; }

// A declared fixture pair for one check id: `trigger_*` must make the
// check report a real, observable non-clean result; `clean_*` must make
// it report `pass` (every scope, when the check is scoped more than
// once -- e.g. per-track/per-program checks).
struct CoveragePair {
  std::string trigger_baseline;
  std::string trigger_candidate;
  std::string clean_baseline;
  std::string clean_candidate;
};

// meta.missing_candidate/meta.extra_candidate are dir-mode-only synthetic
// checks (emitted by src/cli/commands/dir.cpp's own pairing algorithm,
// which is CLI-boundary code per ENG-16 and structurally unreachable from
// this integration target's direct library calls) -- handled by their own
// dedicated dir-mode TEST_CASE below rather than forced into this table's
// compare-pair shape.
const std::set<std::string>& dir_mode_only_checks() {
  static const std::set<std::string> ids = {"meta.missing_candidate", "meta.extra_candidate"};
  return ids;
}

// Every OTHER registered check's declared trigger/clean fixture pair,
// reusing the exact pairs already proven (empirically, against the real
// binary, not merely by fixture-name inference) by this phase's own
// per-plan test files -- test_probe_tracer.cpp, test_container_topology.cpp,
// test_container_mp4.cpp, test_container_mkv.cpp, test_container_ts.cpp,
// test_size_checks.cpp -- so this gate never re-derives a second,
// possibly-divergent notion of "clean" for a check another file already
// pins.
const std::map<std::string, CoveragePair>& declared_pairs() {
  static const std::map<std::string, CoveragePair> pairs = {
      // --- Phase 2 (snapshot-based; meta.tool_version is the only
      // compare-visible one -- missing/extra_candidate are dir-mode-only,
      // see dir_mode_only_checks() above) ---
      {"meta.tool_version",
       {snapshot("tracer_a.snap.json"), snapshot("tracer_b_skew.snap.json"), snapshot("tracer_a.snap.json"),
        snapshot("tracer_b_clean.snap.json")}},

      // --- container.format / container.track_*/chapters / meta.tags* ---
      {"container.format",
       {fixture("tracer_a.mp4"), fixture("tracer_a.mkv"), fixture("tracer_a.mp4"), fixture("tracer_a_copy.mp4")}},
      {"container.track_count",
       {fixture("topo_subs.mp4"), fixture("topo_nosubs.mp4"), fixture("topo_subs.mp4"),
        fixture("topo_subs_copy.mp4")}},
      {"container.track_types",
       {fixture("topo_subs.mp4"), fixture("topo_nosubs.mp4"), fixture("topo_subs.mp4"),
        fixture("topo_subs_copy.mp4")}},
      {"container.track_order",
       {fixture("topo_order_a.mp4"), fixture("topo_order_b.mp4"), fixture("topo_order_a.mp4"),
        fixture("topo_order_a.mp4")}},
      {"container.chapters",
       {fixture("topo_chapters.mkv"), fixture("topo_nochapters.mkv"), fixture("topo_nochapters.mkv"),
        fixture("topo_nochapters.mkv")}},
      {"meta.tags",
       {fixture("tags_title_a.mp4"), fixture("tags_title_b.mp4"), fixture("tags_volatile_a.mp4"),
        fixture("tags_volatile_b.mp4")}},
      {"meta.tags.language",
       {fixture("lang_eng.mp4"), fixture("lang_fra.mp4"), fixture("lang_und.mp4"), fixture("lang_absent.mp4")}},

      // --- container.mp4.* (03-05-PLAN.md) ---
      {"container.mp4.faststart",
       {fixture("mp4_faststart.mp4"), fixture("mp4_nofaststart.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.brands",
       {fixture("mp4_fragmented.mp4"), fixture("mp4_faststart.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.fragmentation",
       {fixture("mp4_fragmented.mp4"), fixture("mp4_faststart.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.fragment_duration",
       {fixture("mp4_fragmented_close.mp4"), fixture("mp4_fragmented_far.mp4"), fixture("mp4_fragmented.mp4"),
        fixture("mp4_fragmented_close.mp4")}},
      {"container.mp4.edit_list",
       {fixture("mp4_editdelay.mp4"), fixture("mp4_edittrim.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.timescale",
       {fixture("mp4_ts_a.mp4"), fixture("mp4_ts_b.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},

      // --- container.mkv.* (03-06-PLAN.md) ---
      {"container.mkv.cues_placement",
       {fixture("mkv_cues_front.mkv"), fixture("mkv_cues_end.mkv"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},
      {"container.mkv.codec_delay",
       {fixture("mkv_opus_a.webm"), fixture("mkv_opus_b.webm"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},
      {"container.mkv.timestamp_scale",
       {fixture("mkv_tscale_a.mkv"), fixture("mkv_tscale_b.mkv"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},
      {"container.mkv.duration_element",
       {fixture("mkv_noduration.mkv"), fixture("mkv_cues_front.mkv"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},

      // --- container.ts.* (03-08-PLAN.md) ---
      {"container.ts.cc_errors",
       {fixture("ts_single.ts"), fixture("ts_ccgap.ts"), fixture("ts_single.ts"), fixture("ts_single_copy.ts")}},
      {"container.ts.cc_discontinuities",
       {fixture("ts_single.ts"), fixture("ts_discontinuity.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      {"container.ts.pcr_interval",
       {fixture("ts_pcr_far_a.ts"), fixture("ts_pcr_far_b.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      // psi_interval/pmt_version_churn: no fixture pair in the current
      // corpus perturbs same-topology PAT/PMT spacing or PMT version
      // directly (see this task's own commit message and 03-11-SUMMARY.md
      // "Known Coverage Gaps" for the empirical sweep that established
      // this). The single-vs-multiprogram topology mismatch below DOES
      // make both checks report a real `fail` -- via the unpaired-program
      // path (CONT-08), not a same-topology measurement drift -- which is
      // sufficient to satisfy "a fixture pair that TRIGGERS it" as this
      // gate defines it, but is flagged as a real, recorded gap rather
      // than presented as the intended semantic trigger.
      {"container.ts.psi_interval",
       {fixture("ts_single.ts"), fixture("ts_multiprogram.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      {"container.ts.pmt_version_churn",
       {fixture("ts_single.ts"), fixture("ts_multiprogram.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      {"container.ts.null_ratio",
       {fixture("ts_nullratio_a.ts"), fixture("ts_nullratio_b.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},

      // --- size.* (03-09-PLAN.md) ---
      {"size.file",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
      {"size.stream_bitrate",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
      {"size.peak_bitrate",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
      {"size.overhead",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},

      // --- video.gop.length (04-01-PLAN.md, Phase 4's tracer) ---
      {"video.gop.length",
       {fixture("video_gop_g48.mp4"), fixture("video_gop_g96.mp4"), fixture("video_gop_g48.mp4"),
        fixture("video_gop_g48_copy.mp4")}},

      // --- video.codec/profile/level/resolution/frame_count
      // (04-06-PLAN.md, VIDEO-01/VIDEO-02) --- video.profile and
      // video.level deliberately SHARE their triggering pair: a single
      // mpeg2video profile change (Main/level 8 -> Simple/level 10, see
      // 04-02-SUMMARY.md's own read-back table) moves both at once -- a
      // real property of the codec, not a shortcut, so this is not
      // "fixed" into two separate pairs.
      {"video.codec",
       {fixture("video_base.mp4"), fixture("video_codec_mpeg2.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.profile",
       {fixture("video_prof_a.mp4"), fixture("video_prof_b.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.level",
       {fixture("video_prof_a.mp4"), fixture("video_prof_b.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.resolution",
       {fixture("video_base.mp4"), fixture("video_res_640.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.frame_count",
       {fixture("video_base.mp4"), fixture("video_frames_50.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},

      // --- video.sar/video.dar/video.sar.conflict/video.frame_rate.*
      // (04-07-PLAN.md, VIDEO-01/VIDEO-04) ---
      {"video.sar",
       {fixture("video_base.mp4"), fixture("video_sar_4_3.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.dar",
       {fixture("video_base.mp4"), fixture("video_sar_4_3.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.sar.conflict",
       {fixture("video_sar_4_3.mp4"), fixture("video_sar_conflict.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.frame_rate.declared",
       {fixture("video_base.mp4"), fixture("video_fps_30.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},
      {"video.frame_rate.measured",
       {fixture("video_base.mp4"), fixture("video_fps_30.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},

      // --- video.pix_fmt/video.color.range/video.color.primaries/
      // video.color.transfer/video.color.matrix/video.color.chroma_loc
      // (04-08-PLAN.md, VIDEO-03/VIDEO-07/VIDEO-08) ---
      //
      // video.pix_fmt's CLEAN pair is deliberately the two-spellings pair
      // rather than a byte-identical copy: a copy would prove only that
      // equal files compare equal, while this pair proves the fold made
      // two genuinely different declarations (yuvj420p vs yuv420p+pc,
      // which read back as the SAME raw pix_fmt post-fold, see
      // 04-02-SUMMARY.md's own read-back table) compare equal -- the
      // property that matters. The originally chosen candidate
      // (video_yuv420p_pc.mp4) turned out BYTE-IDENTICAL to
      // video_yuvj420p.mp4 under the pinned mjpeg encoder (it normalises
      // a direct `-pix_fmt yuv420p -color_range pc` request back to a
      // yuvj* name before muxing), so that pair proved nothing (see
      // deferred-items.md's 04-08 entry and 04-16-PLAN.md). Replaced with
      // video_yuv420p_pc_tagged.mp4 (04-16-PLAN.md Task 1): a stream-copy
      // remux of video_yuv420p_tv.mp4 carrying a full-range colour box,
      // whose bytes genuinely differ from video_yuvj420p.mp4's.
      // Its TRIGGER pair could not reuse the plan's own literal suggestion
      // (video_base.mp4/video_yuvj420p.mp4): both fold to the identical
      // "yuv420p" name (video_base.mp4 was never yuvj* to begin with), so
      // that pair compares `pass`, not a trigger at all -- proven
      // empirically against the real binary before being rejected.
      // video_noparser.mkv (huffyuv, 04-02's own no-parser VIDEO-12
      // fixture) is yuv422p, a genuinely different declared format from
      // video_base.mp4's yuv420p, and was substituted instead.
      {"video.pix_fmt",
       {fixture("video_base.mp4"), fixture("video_noparser.mkv"), fixture("video_yuvj420p.mp4"),
        fixture("video_yuv420p_pc_tagged.mp4")}},
      {"video.color.range",
       {fixture("video_range_pc.mp4"), fixture("video_color_bt709.mp4"), fixture("video_color_bt709.mp4"),
        fixture("video_color_bt709_copy.mp4")}},
      {"video.color.primaries",
       {fixture("video_color_bt709.mp4"), fixture("video_color_bt601.mp4"), fixture("video_color_bt709.mp4"),
        fixture("video_color_bt709_copy.mp4")}},
      {"video.color.transfer",
       {fixture("video_color_bt709.mp4"), fixture("video_color_bt601.mp4"), fixture("video_color_bt709.mp4"),
        fixture("video_color_bt709_copy.mp4")}},
      {"video.color.matrix",
       {fixture("video_color_bt709.mp4"), fixture("video_color_bt601.mp4"), fixture("video_color_bt709.mp4"),
        fixture("video_color_bt709_copy.mp4")}},
      {"video.color.chroma_loc",
       {fixture("video_chroma_left.mkv"), fixture("video_chroma_center.mkv"), fixture("video_color_bt709.mp4"),
        fixture("video_color_bt709_copy.mp4")}},

      // --- video.gop.idr_interval/video.gop.closed/video.gop.refs/
      // video.frame_types (04-09-PLAN.md, PROBE-03/VIDEO-05/VIDEO-12) ---
      // The GOP-family clean pair is a byte-identical copy of
      // video_h264_closed.h264 (added to scripts/gen_corpus.sh by this
      // task) rather than an unrelated codec/container's own clean pair --
      // a clean pair drawn from a different codec family would prove
      // something other than "this check passes when nothing changed"
      // (this plan's own action text).
      {"video.gop.idr_interval",
       {fixture("video_h264_closed.h264"), fixture("video_h264_idr48.h264"), fixture("video_h264_closed.h264"),
        fixture("video_h264_closed_copy.h264")}},
      {"video.gop.closed",
       {fixture("video_h264_closed.h264"), fixture("video_h264_open.h264"), fixture("video_h264_closed.h264"),
        fixture("video_h264_closed_copy.h264")}},
      {"video.gop.refs",
       {fixture("video_h264_refs1.h264"), fixture("video_h264_refs4.h264"), fixture("video_h264_closed.h264"),
        fixture("video_h264_closed_copy.h264")}},
      {"video.frame_types",
       {fixture("video_base.mp4"), fixture("video_bf3.mp4"), fixture("video_base.mp4"),
        fixture("video_base_copy.mp4")}},

      // --- video.interlace (04-10-PLAN.md, VIDEO-06) --- trigger: a real
      // top-field-first vs bottom-field-first flip; clean: a byte-identical
      // copy of the TFF fixture (proven distinct/identical by SHA-256 in
      // this plan's own dispatched test_evidence_guard, and re-verified
      // against the real binary before being written here).
      {"video.interlace",
       {fixture("video_ilace_tff.mp4"), fixture("video_ilace_bff.mp4"), fixture("video_ilace_tff.mp4"),
        fixture("video_ilace_tff_copy.mp4")}},

      // --- video.hdr.mdcv/.luminance/.primaries and video.hdr.cll/.max/
      // .avg (04-11-PLAN.md, VIDEO-09) --- every trigger pair below was
      // proven empirically against the real binary before being written
      // here (this task's own commit message carries the transcript).
      // Known coverage gap, recorded rather than papered over (this
      // plan's own Task 3 instruction, following the precedent
      // container.ts.psi_interval already set in this file): the CLEAN
      // pair for the three VALUE checks (.luminance/.primaries/.max/.avg)
      // is a byte-identical copy of video_hdr_a.mp4 rather than a
      // differing-but-within-tolerance pair. A within-tolerance pair (a
      // luminance/MaxCLL/MaxFALL value shifted by less than the five
      // percent tolerance, or a chromaticity shifted by less than one
      // 0.0002 grid step) would be the STRONGER clean case -- it would
      // prove the tolerance/quantisation math itself accepts a genuine
      // small difference, not merely that identical values compare
      // identical -- but no such fixture exists in this phase's corpus
      // (plan 04-04 built each `_b` variant to isolate exactly one
      // dimension at a value CLEARLY outside tolerance, never a
      // within-tolerance nudge). A future phase that extends the HDR
      // fixture corpus could add one.
      {"video.hdr.mdcv",
       {fixture("video_hdr_a.mp4"), fixture("video_hdr_none.mp4"), fixture("video_hdr_a.mp4"),
        fixture("video_hdr_a_copy.mp4")}},
      {"video.hdr.mdcv.luminance",
       {fixture("video_hdr_a.mp4"), fixture("video_hdr_lum_b.mp4"), fixture("video_hdr_a.mp4"),
        fixture("video_hdr_a_copy.mp4")}},
      {"video.hdr.mdcv.primaries",
       {fixture("video_hdr_a.mp4"), fixture("video_hdr_prim_b.mp4"), fixture("video_hdr_a.mp4"),
        fixture("video_hdr_a_copy.mp4")}},
      {"video.hdr.cll",
       {fixture("video_hdr_a.mp4"), fixture("video_hdr_none.mp4"), fixture("video_hdr_a.mp4"),
        fixture("video_hdr_a_copy.mp4")}},
      {"video.hdr.cll.max",
       {fixture("video_hdr_a.mp4"), fixture("video_hdr_cll_b.mp4"), fixture("video_hdr_a.mp4"),
        fixture("video_hdr_a_copy.mp4")}},
      {"video.hdr.cll.avg",
       {fixture("video_hdr_a.mp4"), fixture("video_hdr_cll_b.mp4"), fixture("video_hdr_a.mp4"),
        fixture("video_hdr_a_copy.mp4")}},

      // --- video.hdr.dovi/video.hdr.dovi.config/video.hdr.coherence
      // (04-12-PLAN.md, VIDEO-09's third family / VIDEO-10, D-10) ---
      {"video.hdr.dovi",
       {fixture("video_dovi_a.mp4"), fixture("video_base.mp4"), fixture("video_dovi_a.mp4"),
        fixture("video_dovi_a_copy.mp4")}},
      {"video.hdr.dovi.config",
       {fixture("video_dovi_a.mp4"), fixture("video_dovi_b.mp4"), fixture("video_dovi_a.mp4"),
        fixture("video_dovi_a_copy.mp4")}},
      // video.hdr.coherence's trigger is a DIFFERENCE in coherence state
      // between two files (video_hdr_coherent.mp4's "coherent" vs
      // video_hdr_pq_nomdcv.mp4's "pq_without_mdcv") -- which is what
      // `compare` compares. The both-sides-share-it behaviour VIDEO-10
      // also requires (Decision 1: a SHARED incoherence still fires) is
      // covered by Test 5 in tests/unit/test_video_hdr.cpp rather than by
      // this gate -- the two requirements are different and neither
      // substitutes for the other. This check is `info` severity and
      // never gates the exit code in any profile.
      {"video.hdr.coherence",
       {fixture("video_hdr_coherent.mp4"), fixture("video_hdr_pq_nomdcv.mp4"), fixture("video_hdr_coherent.mp4"),
        fixture("video_hdr_coherent_copy.mp4")}},

      // --- timeline.start (05-01-PLAN.md, TIME-01/TIME-03, D-03) --- the
      // trigger pair is the whole-file MPEG-TS remux, which reports a
      // non-pass timeline.start finding at GLOBAL scope (D-03: one cause,
      // one finding) -- `any_non_clean` only needs ONE non-pass finding
      // among this check's own statuses, which the global measurement
      // alone satisfies even though every per-stream timeline.start
      // finding on this same pair stays `pass`. The clean pair is a
      // byte-identical copy, matching every other tracer's own clean-pair
      // shape.
      {"timeline.start",
       {fixture("timeline_start_base.mp4"), fixture("timeline_start_shift.ts"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},

      // --- timeline.duration / timeline.duration.coherence (05-04-PLAN.md,
      // TIME-01/TIME-03, D-08, Phase 4 D-10's precedent) --- timeline.duration's
      // trigger is a real 4s-vs-2s content-length change (timeline_duration_short.mp4,
      // this plan's own new fixture); the clean pair is the same byte-identical
      // copy every other tracer in this phase uses.
      //
      // timeline.duration.coherence's trigger is the SAME MPEG-TS remux pair
      // timeline.start already declares above -- per this plan's own A1
      // flagged_assumption, proven empirically against the real binary
      // (not assumed) before being declared here: the TS demuxer's own
      // AVStream::duration for the AUDIO stream genuinely disagrees with its
      // own AVFormatContext::duration by more than the fixed 40ms threshold,
      // firing `container_vs_stream` at `info` severity. Its clean pair is
      // the identical byte-identical copy.
      {"timeline.duration",
       {fixture("timeline_start_base.mp4"), fixture("timeline_duration_short.mp4"),
        fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4")}},
      {"timeline.duration.coherence",
       {fixture("timeline_start_base.mp4"), fixture("timeline_start_shift.ts"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},

      // --- timeline.dts_monotonic / timeline.pts_unique (05-05-PLAN.md,
      // TIME-01/TIME-04) --- timeline.dts_monotonic's trigger is
      // timeline_dts_backward.ts, a two-segment MPEG-TS splice producing a
      // genuine dts[i] <= dts[i-1] violation on each stream (05-05-SUMMARY.md
      // documents why a `setts`-crafted single encode cannot produce this:
      // a genuinely backward DTS is structurally impossible to write via
      // ffmpeg's own CLI/muxer, MP4's `stts` box included). timeline.
      // pts_unique's trigger is timeline_pts_dupe.mp4, a `setts`-crafted
      // single encode with exactly one duplicate PTS pair. Both clean pairs
      // are the same byte-identical copy every other tracer in this phase
      // uses.
      {"timeline.dts_monotonic",
       {fixture("timeline_start_base.mp4"), fixture("timeline_dts_backward.ts"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},
      {"timeline.pts_unique",
       {fixture("timeline_start_base.mp4"), fixture("timeline_pts_dupe.mp4"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},

      // --- timeline.gaps / timeline.wrap_events (05-06-PLAN.md, TIME-02/
      // TIME-04) --- timeline.gaps' trigger is timeline_gap.mp4, a
      // `setts`-crafted PTS-only shift (DTS untouched) producing a real
      // presentation-order hole verified empirically against the real
      // binary: video reports `fail` with a single {1960ms,2120ms} span,
      // audio stays `pass` (untouched by the shift). timeline.wrap_events'
      // trigger pair is timeline_ts_nowrap.ts (unflagged `no_wrap`) against
      // timeline_ts_wrap.ts (flagged `ts_33bit_wrap`, a genuine mid-file
      // 33-bit PTS/DTS wrap from a direct -output_ts_offset encode) --
      // the state-semantic asymmetry itself is the trigger. Both clean
      // pairs are byte-identical copies: timeline.gaps reuses the phase's
      // own timeline_start_base.mp4/_copy.mp4 pair; timeline.wrap_events
      // uses timeline_ts_nowrap.ts/timeline_ts_nowrap_copy.ts so BOTH
      // sides of the clean pair are unflagged (the state semantic's own
      // `pass` requirement -- neither side flagged, never "both agree").
      {"timeline.gaps",
       {fixture("timeline_start_base.mp4"), fixture("timeline_gap.mp4"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},
      {"timeline.wrap_events",
       {fixture("timeline_ts_nowrap.ts"), fixture("timeline_ts_wrap.ts"), fixture("timeline_ts_nowrap.ts"),
        fixture("timeline_ts_nowrap_copy.ts")}},

      // --- timeline.discontinuities / timeline.discontinuities.flagged
      // (05-07-PLAN.md, TIME-02/TIME-04) --- timeline.discontinuities'
      // trigger pair is timeline_ts_nowrap.ts against timeline_ts_jump.ts
      // (a genuine, UNFLAGGED ~3.02s forward presentation jump from two
      // independently-muxed, spliced TS segments) -- verified empirically
      // against the real binary: both video and audio report `fail`, each
      // with jump_count 0 -> 1. Its clean pair reuses timeline_ts_nowrap.ts/
      // timeline_ts_nowrap_copy.ts, the SAME byte-identical, both-
      // unflagged pair timeline.wrap_events above already uses (all-`pass`
      // confirmed empirically for both new check ids on this exact pair).
      // timeline.discontinuities.flagged's trigger pair is
      // timeline_ts_jump.ts against timeline_ts_jump_flagged.ts -- the
      // SAME jump, now with tools/gen_ts_discontinuity.py's one-bit edit
      // making it container-EXPLAINED on the video stream (verified via
      // evidence: jump_count 0 -> 1, `info`, "+1 introduced span(s)");
      // its clean pair reuses the same timeline_ts_nowrap.ts/_copy.ts pair
      // (all-`pass` confirmed for `.flagged` there too, since neither side
      // of that pair carries any discontinuity_indicator flag at all).
      {"timeline.discontinuities",
       {fixture("timeline_ts_nowrap.ts"), fixture("timeline_ts_jump.ts"), fixture("timeline_ts_nowrap.ts"),
        fixture("timeline_ts_nowrap_copy.ts")}},
      {"timeline.discontinuities.flagged",
       {fixture("timeline_ts_jump.ts"), fixture("timeline_ts_jump_flagged.ts"), fixture("timeline_ts_nowrap.ts"),
        fixture("timeline_ts_nowrap_copy.ts")}},

      // --- timeline.jitter / timeline.vfr_profile (05-08-PLAN.md, TIME-05)
      // --- timeline.jitter's trigger pair is timeline_start_base.mp4
      // against timeline_jitter.mp4 (a single interior video frame shifted
      // forward by exactly one whole frame, staying inside the original
      // PTS range so the stream remains unambiguously CFR under D-05) --
      // verified empirically: video reports `fail` with a real, non-zero
      // sigma (~4.01ms), audio (untouched) stays `pass`. Its clean pair
      // reuses timeline_start_base.mp4/_copy.mp4 (all-`pass` for both ids
      // on this exact byte-identical pair, confirmed empirically).
      // timeline.vfr_profile's trigger pair is timeline_start_base.mp4
      // against timeline_vfr.mp4 (the SAME LGPL-clean `select`+`-fps_mode
      // vfr` thinning chain video_vfr.mp4 already proves classifies VFR) --
      // verified empirically: video reports `warn` (worst bin 'on_grid'
      // exceeds tolerance) while `timeline.jitter` itself is
      // `skipped:vfr` on that same stream (ROADMAP SC3); its clean pair
      // reuses the same timeline_start_base.mp4/_copy.mp4 pair.
      {"timeline.jitter",
       {fixture("timeline_start_base.mp4"), fixture("timeline_jitter.mp4"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},
      {"timeline.vfr_profile",
       {fixture("timeline_start_base.mp4"), fixture("timeline_vfr.mp4"), fixture("timeline_start_base.mp4"),
        fixture("timeline_start_base_copy.mp4")}},

      // --- timeline.av_offset (05-09-PLAN.md, TIME-06/TIME-09/TIME-10) ---
      // trigger pair is timeline_start_base.mp4 against
      // timeline_avoffset_video_shift.mp4 (the SAME testsrc2/sine lavfi
      // sources with `-itsoffset` applied to the VIDEO input only, per
      // D-12, so the audio edit list and its skip_samples priming signal
      // survive) -- verified empirically under --profile sw-encoder: both
      // sides resolve `priming.source == skip_samples`, the comparison
      // basis is `adjusted`, and the measured 40ms delta reports `fail`
      // (beyond the registered 20ms fail threshold). Its clean pair reuses
      // the same timeline_start_base.mp4/_copy.mp4 pair every other
      // timeline check above reuses (all-`pass` for this exact
      // byte-identical pair, confirmed empirically).
      {"timeline.av_offset",
       {fixture("timeline_start_base.mp4"), fixture("timeline_avoffset_video_shift.mp4"),
        fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4")}},
      // 05-10-PLAN.md Task 3 (TIME-07/TIME-08, D-04): timeline.av_drift and
      // timeline.av_drift.pattern -- ONE trigger pair covers BOTH ids.
      // timeline_drift_base.mp4/timeline_drift_linear.mp4 (the classic 0.1%
      // clock-error recipe, doc 04 section 5, `asetrate=48048,
      // aresample=48000`) verified empirically under --profile sw-encoder:
      // timeline.av_drift reports `fail` (measured rate ~-60.28ms/min,
      // beyond the registered 0.2ms/min fail threshold) and
      // timeline.av_drift.pattern reports `fail` with candidate value
      // `linear-drift`. Its clean pair reuses the same
      // timeline_start_base.mp4/_copy.mp4 byte-identical pair every other
      // timeline check above reuses (both ids `pass`, confirmed
      // empirically: rate/end_delta exactly zero, pattern `constant-
      // offset`).
      {"timeline.av_drift",
       {fixture("timeline_drift_base.mp4"), fixture("timeline_drift_linear.mp4"),
        fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4")}},
      {"timeline.av_drift.pattern",
       {fixture("timeline_drift_base.mp4"), fixture("timeline_drift_linear.mp4"),
        fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4")}},
      // 05-11-PLAN.md Task 3 (TIME-11): timeline.timecode's own trigger
      // pair -- timeline_tc_ndf.mp4 (a tmcd track present) vs
      // timeline_tc_absent.mp4 (the identical encode with no `-timecode`
      // option at all, so no tmcd track) -- verified empirically under
      // --profile sw-encoder: reports non-pass (`present` -> `Absent`).
      // Clean pair: the byte-identical `cp` copy, verified `pass`.
      {"timeline.timecode",
       {fixture("timeline_tc_ndf.mp4"), fixture("timeline_tc_absent.mp4"), fixture("timeline_tc_ndf.mp4"),
        fixture("timeline_tc_ndf_copy.mp4")}},
      // 05-11-PLAN.md Task 3 (TIME-11): timeline.timecode.value's own
      // trigger pair -- timeline_tc_ndf.mp4 (`00:00:10:00`) vs
      // timeline_tc_ndf_shifted.mp4 (`00:00:20:00`, the SAME encode,
      // differing only in the tmcd track's own start timecode) --
      // verified empirically under --profile sw-encoder: reports
      // non-pass (`info`, D-04-style exact-string-mismatch). Clean pair:
      // the same byte-identical copy as timeline.timecode above.
      {"timeline.timecode.value",
       {fixture("timeline_tc_ndf.mp4"), fixture("timeline_tc_ndf_shifted.mp4"), fixture("timeline_tc_ndf.mp4"),
        fixture("timeline_tc_ndf_copy.mp4")}},
      // 06-01-PLAN.md Task 2 (AUDIO-10, D-01/D-02): content.audio.sample_hash's
      // own trigger pair -- audio_hash_base.mp4 (440 Hz sine) vs
      // audio_hash_alt.mp4 (the identical recipe at 880 Hz, a genuinely
      // different sample stream) -- verified empirically: reports non-pass
      // with a populated D-03 divergence report. Clean pair:
      // audio_hash_base.mp4 vs audio_hash_base_copy.mp4, two INDEPENDENT
      // bitexact encodes of the identical 440 Hz signal -- verified
      // empirically to hash byte-identical (`89d280015b77f4702bd20d52638f4753`),
      // proving D-01's encoder-determinism claim, not merely a `cp` copy.
      {"content.audio.sample_hash",
       {fixture("audio_hash_base.mp4"), fixture("audio_hash_alt.mp4"), fixture("audio_hash_base.mp4"),
        fixture("audio_hash_base_copy.mp4")}},

      // --- 06-03-PLAN.md (AUDIO-01, AUDIO-02): the six per-audio-stream
      // header-pass identity checks -- codec/sample_rate/sample_fmt/
      // bit_depth/channels/layout. Every pair below was run through the
      // real `mediadiff compare --profile sw-encoder --json` binary and
      // its finding's `status`/`baseline`/`candidate` fields inspected
      // directly before being committed here. ---
      // audio.codec: trigger is aac (audio_hash_base.mp4) vs mp2
      // (audio_mp2_base.mpg) -- verified `fail`, baseline "aac" vs
      // candidate "mp2". Clean pair reuses content.audio.sample_hash's own
      // clean pair (two independent bitexact aac encodes of the same
      // signal) -- verified `pass`, "aac" both sides.
      {"audio.codec",
       {fixture("audio_hash_base.mp4"), fixture("audio_mp2_base.mpg"), fixture("audio_hash_base.mp4"),
        fixture("audio_hash_base_copy.mp4")}},
      // audio.sample_rate: trigger is audio_hash_base.mp4 (44100 Hz) vs
      // audio_sbr_implicit.mp4 (88200 Hz, SBR-implicit doubling) --
      // verified `fail`, 44100 vs 88200. Clean pair reuses the same
      // independent-bitexact-encode pair as audio.codec above -- verified
      // `pass`, 44100 both sides.
      {"audio.sample_rate",
       {fixture("audio_hash_base.mp4"), fixture("audio_sbr_implicit.mp4"), fixture("audio_hash_base.mp4"),
        fixture("audio_hash_base_copy.mp4")}},
      // audio.sample_fmt: trigger is audio_stereo_s16.wav (s16) vs
      // audio_stereo_s24.wav (s32, since s24 canonicalizes to its packed
      // 32-bit container per D-02) -- verified `fail`, "s16" vs "s32".
      // Clean pair is audio_stereo_s16.wav vs audio_pcm_base.wav, both s16
      // -- verified `pass`, "s16" both sides.
      {"audio.sample_fmt",
       {fixture("audio_stereo_s16.wav"), fixture("audio_stereo_s24.wav"), fixture("audio_stereo_s16.wav"),
        fixture("audio_pcm_base.wav")}},
      // audio.bit_depth: trigger is audio_51.flac (16-bit) vs
      // audio_dropout.flac (24-bit), both genuinely declaring
      // bits_per_raw_sample so the finding reports a real `fail` rather
      // than an Absent-driven `skipped` -- verified `fail`, 16 vs 24.
      // Clean pair is audio_51.flac vs audio_51_side.flac, both 16-bit --
      // verified `pass`, 16 both sides.
      {"audio.bit_depth",
       {fixture("audio_51.flac"), fixture("audio_dropout.flac"), fixture("audio_51.flac"),
        fixture("audio_51_side.flac")}},
      // audio.channels: trigger is audio_stereo_s16.wav (2ch) vs
      // audio_mono_s16.wav (1ch) -- verified `fail`, 2 vs 1. Clean pair is
      // audio_stereo_s16.wav vs audio_stereo_s24.wav, both 2ch -- verified
      // `pass`, 2 both sides (this pair simultaneously triggers
      // audio.sample_fmt above, which is fine -- DOC-03 only requires THIS
      // check to be clean on the declared clean pair, not that the pair be
      // clean everywhere).
      {"audio.channels",
       {fixture("audio_stereo_s16.wav"), fixture("audio_mono_s16.wav"), fixture("audio_stereo_s16.wav"),
        fixture("audio_stereo_s24.wav")}},
      // audio.layout: trigger is audio_51.flac (5.1) vs
      // audio_51_side.flac (5.1(side)) -- the headline "same channel
      // count, different layout" story -- verified `fail`, "5.1" vs
      // "5.1(side)"; this is also test_audio_stream_params.cpp's own Test
      // 1/2 pair. Clean pair is audio_51.flac against itself -- verified
      // `pass`, "5.1" both sides.
      {"audio.layout",
       {fixture("audio_51.flac"), fixture("audio_51_side.flac"), fixture("audio_51.flac"), fixture("audio_51.flac")}},
  };
  return pairs;
}

// Runs `mediadiff compare baseline candidate --profile sw-encoder --json`
// and returns the status string of every finding whose id == check_id
// (more than one when the check is scoped per-track/per-program).
std::vector<std::string> statuses_for(const std::string& baseline, const std::string& candidate,
                                       const std::string& check_id) {
  INFO("baseline: " << baseline);
  INFO("candidate: " << candidate);
  REQUIRE(fs::exists(baseline));
  REQUIRE(fs::exists(candidate));

  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", "sw-encoder", "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));

  std::vector<std::string> statuses;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == check_id) {
      statuses.push_back(finding.at("status").get<std::string>());
    }
  }
  return statuses;
}

bool any_non_clean(const std::vector<std::string>& statuses) {
  for (const std::string& status : statuses) {
    if (status != "pass" && status != "skipped") {
      return true;
    }
  }
  return false;
}

bool all_pass(const std::vector<std::string>& statuses) {
  if (statuses.empty()) {
    return false;
  }
  for (const std::string& status : statuses) {
    if (status != "pass") {
      return false;
    }
  }
  return true;
}

}  // namespace

// --- The gate itself: enumerate the REAL registry, never a hand list ------

TEST_CASE("doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one",
          "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  REQUIRE(registry.size() > 0);

  std::vector<std::string> uncovered_no_pair;
  std::vector<std::string> uncovered_trigger_did_not_fire;
  std::vector<std::string> uncovered_clean_was_not_clean;
  std::size_t verified_count = 0;

  const auto& pairs = declared_pairs();
  const auto& dir_only = dir_mode_only_checks();

  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    const std::string id(registry.at(i).id);

    if (dir_only.count(id) != 0) {
      // Covered by "doc03_coverage - dir-mode-only checks..." below --
      // still counted toward the verified total, per this task's own
      // requirement that the reported count equal the registry's count.
      ++verified_count;
      continue;
    }

    const auto found = pairs.find(id);
    if (found == pairs.end()) {
      uncovered_no_pair.push_back(id);
      continue;
    }

    const CoveragePair& pair = found->second;
    const std::vector<std::string> trigger_statuses = statuses_for(pair.trigger_baseline, pair.trigger_candidate, id);
    const std::vector<std::string> clean_statuses = statuses_for(pair.clean_baseline, pair.clean_candidate, id);

    bool ok = true;
    if (!any_non_clean(trigger_statuses)) {
      uncovered_trigger_did_not_fire.push_back(id);
      ok = false;
    }
    if (!all_pass(clean_statuses)) {
      uncovered_clean_was_not_clean.push_back(id);
      ok = false;
    }
    if (ok) {
      ++verified_count;
    }
  }

  INFO("registry check count: " << registry.size());
  INFO("verified check count: " << verified_count);

  if (!uncovered_no_pair.empty()) {
    std::string names;
    for (const auto& name : uncovered_no_pair) names += name + " ";
    FAIL("DOC-03 gap -- no declared fixture pair for: " << names);
  }
  if (!uncovered_trigger_did_not_fire.empty()) {
    std::string names;
    for (const auto& name : uncovered_trigger_did_not_fire) names += name + " ";
    FAIL("DOC-03 gap -- declared TRIGGER pair produced no non-clean finding for: " << names);
  }
  if (!uncovered_clean_was_not_clean.empty()) {
    std::string names;
    for (const auto& name : uncovered_clean_was_not_clean) names += name + " ";
    FAIL("DOC-03 gap -- declared CLEAN pair did not compare all-pass for: " << names);
  }

  // The count-must-equal-the-registry assertion this task's own
  // checkpoint requires a human confirm: printed via INFO above (visible
  // with --output-on-failure or -s), and enforced here as a hard
  // REQUIRE so a silently-smaller verified count is a test failure, not
  // just a number a human has to notice.
  REQUIRE(verified_count == registry.size());
}

// --- meta.missing_candidate / meta.extra_candidate (dir-mode-only) --------

TEST_CASE(
    "doc03_coverage - dir-mode-only checks: meta.missing_candidate/meta.extra_candidate trigger on an unpaired "
    "file each way and are absent (clean) on a fully-paired directory",
    "[integration]") {
  const fs::path scratch_root = fs::temp_directory_path() / "mediadiff_doc03_coverage_scratch";
  std::error_code ec;
  fs::remove_all(scratch_root, ec);
  fs::create_directories(scratch_root, ec);

  const fs::path unpaired_baseline = scratch_root / "unpaired_baseline";
  const fs::path unpaired_candidate = scratch_root / "unpaired_candidate";
  fs::create_directories(unpaired_baseline, ec);
  fs::create_directories(unpaired_candidate, ec);
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_baseline / "shared.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_candidate / "shared.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_baseline / "only_baseline.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_candidate / "only_candidate.mp4", std::ios::binary) << src.rdbuf();
  }

  const CliResult triggering =
      run_cli({"dir", unpaired_baseline.string(), unpaired_candidate.string(), "--json"});
  const nlohmann::ordered_json triggering_report = nlohmann::ordered_json::parse(triggering.out, nullptr, false);
  REQUIRE_FALSE(triggering_report.is_discarded());

  bool found_missing = false;
  bool found_extra = false;
  for (const auto& file_block : triggering_report.at("files")) {
    for (const auto& finding : file_block.at("findings")) {
      const std::string id = finding.at("id").get<std::string>();
      if (id == "meta.missing_candidate") found_missing = true;
      if (id == "meta.extra_candidate") found_extra = true;
    }
  }
  CHECK(found_missing);
  CHECK(found_extra);

  // Clean: a fully-paired directory never emits either check at all --
  // "the check comes back clean" for a presence-only, unpaired-file
  // synthetic check means "correctly silent when nothing is actually
  // missing", not "reports pass" (there is no per-file Fingerprint for it
  // to compare against).
  const fs::path paired_baseline = scratch_root / "paired_baseline";
  const fs::path paired_candidate = scratch_root / "paired_candidate";
  fs::create_directories(paired_baseline, ec);
  fs::create_directories(paired_candidate, ec);
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(paired_baseline / "shared.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(paired_candidate / "shared.mp4", std::ios::binary) << src.rdbuf();
  }

  const CliResult clean = run_cli({"dir", paired_baseline.string(), paired_candidate.string(), "--json"});
  const nlohmann::ordered_json clean_report = nlohmann::ordered_json::parse(clean.out, nullptr, false);
  REQUIRE_FALSE(clean_report.is_discarded());

  bool clean_found_missing = false;
  bool clean_found_extra = false;
  for (const auto& file_block : clean_report.at("files")) {
    for (const auto& finding : file_block.at("findings")) {
      const std::string id = finding.at("id").get<std::string>();
      if (id == "meta.missing_candidate") clean_found_missing = true;
      if (id == "meta.extra_candidate") clean_found_extra = true;
    }
  }
  CHECK_FALSE(clean_found_missing);
  CHECK_FALSE(clean_found_extra);

  fs::remove_all(scratch_root, ec);
}

// Confirms no exemption mechanism silently drops a check from the count
// above -- this task's own acceptance criterion 4
// (`grep -c 'exempt|skip|allow' tests/integration/test_doc03_coverage.cpp`
// must show any exemption use carries a written reason, or that none
// exists at all). None exists: dir_mode_only_checks() is a routing table
// to a DIFFERENT proof of coverage, not an exemption from being covered --
// both its members are asserted against in the TEST_CASE immediately
// above, and both are still counted in verified_count.
