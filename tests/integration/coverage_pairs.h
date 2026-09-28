#pragma once

// DOC-03's declared fixture-pair table (test_doc03_coverage.cpp's own
// per-check-id trigger/clean pairs), extracted here so a SECOND consumer
// (tests/integration/test_audio_corpus_sweep.cpp, 06-11-PLAN.md Task 2)
// can draw on the SAME already-proven-clean pairs rather than re-deriving
// a possibly-divergent notion of "clean" for a check test_doc03_coverage.cpp
// already pins -- mirrors tests/integration/timeline_findings.h's own
// extraction precedent (that header's own top comment: "moved here
// VERBATIM from test_video_yuvj.cpp ... so the repository holds exactly
// one definition"). Header-only, `inline` throughout, the same convention.
//
// test_doc03_coverage.cpp itself now includes this header instead of
// carrying its own copy of CoveragePair/dir_mode_only_checks/declared_pairs
// -- a single source of truth for every declared pair, so DOC-03's own
// per-check gate and this corpus-wide clean sweep can never silently drift
// apart on what "clean" means for a given check id. Every entry below is
// moved VERBATIM (including every citation comment) from
// test_doc03_coverage.cpp's own anonymous namespace -- no pair's baseline,
// candidate or reasoning was re-derived or re-verified for this move.

#include <map>
#include <set>
#include <string>

#include "support/fixture_paths.h"

namespace mediadiff::test {

inline std::string fixture(const std::string& name) { return fixture_dir() + "/" + name; }
inline std::string snapshot(const std::string& name) { return snapshot_dir() + "/" + name; }

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
// dedicated dir-mode TEST_CASE in test_doc03_coverage.cpp rather than
// forced into this table's compare-pair shape.
inline const std::set<std::string>& dir_mode_only_checks() {
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
inline const std::map<std::string, CoveragePair>& declared_pairs() {
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
      // clock-error recipe, doc 04 section 5, reached DSP-free as
      // `sample_rate=47952,asetrate=48000` with `-c:a pcm_s16le` audio -- see
      // scripts/gen_corpus.sh's own DETERMINISM notes: the original
      // `asetrate=48048,aresample=48000` form put libswresample's
      // per-architecture SIMD in the fixture's own generation path, and the
      // native AAC encoder that used to follow it was independently
      // arch-divergent too, which together made audio.loudness.true_peak
      // diverge by up to 4 dB between CI legs. Both are now gone: no
      // resampler, and no lossy encoder) verified empirically under
      // --profile sw-encoder:
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
      // -- verified `pass`, "s16" both sides. (Restored 06-11-PLAN.md Task 2
      // human-review correction: a same-file self-compare only proves
      // determinism, which test_trust06_idempotence.cpp already covers
      // corpus-wide; this pair proves the stronger, load-bearing property --
      // audio.sample_fmt stays clean when a DIFFERENT recording's other
      // dimensions change -- cross-dimension independence, not mere
      // determinism. The corpus-wide sweep
      // (tests/integration/test_audio_corpus_sweep.cpp) now declares this
      // pair's OTHER non-pass findings by name instead of forcing this pair
      // toward a self-compare.)
      {"audio.sample_fmt",
       {fixture("audio_stereo_s16.wav"), fixture("audio_stereo_s24.wav"), fixture("audio_stereo_s16.wav"),
        fixture("audio_pcm_base.wav")}},
      // audio.bit_depth: trigger is audio_51.flac (16-bit) vs
      // audio_dropout.flac (24-bit), both genuinely declaring
      // bits_per_raw_sample so the finding reports a real `fail` rather
      // than an Absent-driven `skipped` -- verified `fail`, 16 vs 24.
      // Clean pair is audio_51.flac vs audio_51_side.flac, both 16-bit --
      // verified `pass`, 16 both sides. (Restored 06-11-PLAN.md Task 2
      // human-review correction: this pair is ALSO audio.layout's own
      // declared trigger pair ("5.1" vs "5.1(side)") -- by design, per
      // D-02, one pair may be check X's clean pair and check Y's trigger
      // pair simultaneously. A same-file self-compare would prove only
      // determinism; this pair proves audio.bit_depth stays clean across a
      // genuine layout change, the cross-dimension independence a
      // self-compare cannot demonstrate. The corpus-wide sweep
      // (tests/integration/test_audio_corpus_sweep.cpp) declares
      // audio.layout's expected non-pass finding on this pair by name.)
      {"audio.bit_depth",
       {fixture("audio_51.flac"), fixture("audio_dropout.flac"), fixture("audio_51.flac"),
        fixture("audio_51_side.flac")}},
      // audio.channels: trigger is audio_stereo_s16.wav (2ch) vs
      // audio_mono_s16.wav (1ch) -- verified `fail`, 2 vs 1. Clean pair is
      // audio_stereo_s16.wav vs audio_stereo_s24.wav, both 2ch -- verified
      // `pass`, 2 both sides (this pair simultaneously triggers
      // audio.sample_fmt above, which is fine -- DOC-03 only requires THIS
      // check to be clean on the declared clean pair, not that the pair be
      // clean everywhere). (Restored 06-11-PLAN.md Task 2 human-review
      // correction: a same-file self-compare only proves determinism; this
      // pair proves audio.channels stays clean across a genuine sample-
      // format change -- cross-dimension independence. The corpus-wide
      // sweep (tests/integration/test_audio_corpus_sweep.cpp) declares this
      // pair's other expected non-pass findings by name.)
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
      // audio.profile (06-04-PLAN.md, AUDIO-03): trigger is
      // audio_sbr_explicit.mp4 (explicit AOT_SBR ASC) vs
      // audio_sbr_implicit.mp4 (bare AAC-LC ASC, SBR only discoverable via
      // the bounded decode) -- verified `fail`, "HE-AAC (sbr: explicit)"
      // vs "HE-AAC (sbr: implicit)"; this is also
      // test_audio_profile_sbr.cpp's own Test 2 pair. Clean pair is
      // audio_sbr_explicit.mp4 vs audio_sbr_explicit_copy.mp4, both
      // explicit -- verified `pass`, "HE-AAC (sbr: explicit)" both sides.
      {"audio.profile",
       {fixture("audio_sbr_explicit.mp4"), fixture("audio_sbr_implicit.mp4"), fixture("audio_sbr_explicit.mp4"),
        fixture("audio_sbr_explicit_copy.mp4")}},
      // audio.priming (06-06-PLAN.md, AUDIO-04, D-14): trigger is
      // audio_prime_base.mp4 (known priming, source skip_samples, "1024")
      // vs audio_prime_copy.ts (a `-c copy` MPEG-TS remux carrying no
      // AV_PKT_DATA_SKIP_SAMPLES side data at all) -- verified `fail`,
      // "1024" vs the literal "unknown" (D-14: unknown compares as its own
      // value, never a skip).
      //
      // Clean pair (06-11-PLAN.md Task 2, Rule 1 fix): audio_prime_base.mp4
      // AGAINST ITSELF, not the originally declared audio_prime_base.mp4
      // vs audio_prime_roundtrip.mkv (a single MP4->MKV hop). That pair
      // WAS `pass` for audio.priming itself ("1024" both sides), but it is
      // a genuine cross-CONTAINER remux -- container.format necessarily
      // differs (mov vs matroska) and meta.tags/size.overhead move too
      // (surfaced by the corpus-wide sweep,
      // tests/integration/test_audio_corpus_sweep.cpp, asserting the WHOLE
      // report rather than only this one id). A cross-container pair was
      // never a valid "nothing changed" case in the sense this sweep
      // checks, and the cross-container priming-stability claim this pair
      // existed to prove is independently covered by
      // tests/integration/test_audio_priming.cpp's own dedicated Test 5
      // ("audio_prime_base.mp4 vs audio_prime_roundtrip.mkv reports pass
      // with the SAME resolved [priming value]"), so narrowing THIS
      // declaration to a same-file self-compare loses no coverage.
      //
      // NOTE, preserved from the original entry: audio_prime_roundtrip2.mp4
      // (the MP4->MKV->MP4 round trip 06-06-PLAN.md's own must_haves/
      // acceptance-criteria text names as the clean pair) is deliberately
      // NOT used here -- measured directly against this project's own
      // linked FFmpeg 8.1 (never the system ffprobe, which is a materially
      // different, newer build), it reports "1014" against the base's
      // "1024", a genuine ~10-sample rounding artifact of the MKV
      // `CodecDelay` intermediate's own nanosecond-granularity round trip
      // -- exactly the risk 06-02-SUMMARY.md's own "Next Phase Readiness"
      // note flagged in advance ("06-06 should treat this as data to
      // measure a tolerance against, not assume away"). Since
      // `audio.priming` is registered `semantic=exact` over a `string`
      // value (D-14 forces this shape; no numeric tolerance is
      // expressible), there is no way to make that specific pair report
      // `pass` without either regenerating the fixture (forbidden:
      // `tests/golden/CORPUS_DIGEST.txt` never rewrites an existing line)
      // or adding a tolerance mechanism D-14 explicitly rules out. See
      // 06-06-SUMMARY.md's own Deviations section for the full measurement
      // and reasoning.
      {"audio.priming",
       {fixture("audio_prime_base.mp4"), fixture("audio_prime_copy.ts"), fixture("audio_prime_base.mp4"),
        fixture("audio_prime_base.mp4")}},
      // audio.loudness.integrated (06-08-PLAN.md, AUDIO-05): trigger is
      // audio_loud_ref.flac vs audio_loud_plus3.flac (the same tone at
      // +3dB) -- verified `fail`, -21.8 vs -18.8 LUFS, 3.0LU beyond the
      // 0.5/1.0LU two-threshold tolerance. Clean pair is audio_loud_ref.flac
      // vs its DOC-03 clean pair audio_loud_ref_copy.flac (a second,
      // independent encode of the same source) -- verified `pass`.
      {"audio.loudness.integrated",
       {fixture("audio_loud_ref.flac"), fixture("audio_loud_plus3.flac"), fixture("audio_loud_ref.flac"),
        fixture("audio_loud_ref_copy.flac")}},
      // audio.loudness.true_peak (06-08-PLAN.md, AUDIO-06): trigger is
      // audio_peak_under.flac (baseline, -2.1 dBTP, under the -1.0 dBTP
      // ceiling) vs audio_peak_over.flac (candidate, -0.6 dBTP, above it) --
      // verified `fail` via the asymmetric ceiling escalation (an upward
      // crossing), not merely the ordinary 0.3dB tolerance. Clean pair
      // reuses audio_loud_ref.flac vs audio_loud_ref_copy.flac (same
      // rationale as audio.loudness.integrated above) -- verified `pass`.
      {"audio.loudness.true_peak",
       {fixture("audio_peak_under.flac"), fixture("audio_peak_over.flac"), fixture("audio_loud_ref.flac"),
        fixture("audio_loud_ref_copy.flac")}},
      // audio.silence.edges (06-09-PLAN.md, AUDIO-07): trigger is
      // audio_silence_none.flac (baseline, no silence) vs
      // audio_silence_lead.flac (candidate, 250ms leading silence) --
      // verified `fail`, one introduced leading span. Clean pair is
      // audio_silence_none.flac vs itself -- verified `pass`.
      {"audio.silence.edges",
       {fixture("audio_silence_none.flac"), fixture("audio_silence_lead.flac"), fixture("audio_silence_none.flac"),
        fixture("audio_silence_none.flac")}},
      // audio.silence.dropouts (06-09-PLAN.md, AUDIO-07): trigger is
      // audio_dropout_clean.flac (baseline, no interior dropout) vs
      // audio_dropout.flac (candidate, a 400ms interior mute) -- verified
      // `fail`, one introduced interior span. Clean pair is
      // audio_dropout_clean.flac vs itself -- verified `pass`. (Rule 1 fix,
      // 06-09-SUMMARY.md: the plan's own literal recipe made these two
      // fixtures byte-identical; scripts/gen_corpus.sh was corrected so
      // audio_dropout_clean.flac genuinely carries no mute.)
      {"audio.silence.dropouts",
       {fixture("audio_dropout_clean.flac"), fixture("audio_dropout.flac"), fixture("audio_dropout_clean.flac"),
        fixture("audio_dropout_clean.flac")}},
      // meta.decode_errors (06-10-PLAN.md, AUDIO-08, D-09): trigger is
      // audio_corrupt_clean.mp4 (baseline, 0 decode errors) vs
      // audio_corrupt_frames.mp4 (candidate, a handful of scattered
      // byte-corrupted AAC access units) -- verified `fail`, 0 vs 7. Clean
      // pair is audio_corrupt_frames.mp4 vs itself -- verified `pass`
      // (Test 5: a baseline with a stable non-zero error count against a
      // candidate with the SAME count stays comparable, never gating
      // forever).
      {"meta.decode_errors",
       {fixture("audio_corrupt_clean.mp4"), fixture("audio_corrupt_frames.mp4"), fixture("audio_corrupt_frames.mp4"),
        fixture("audio_corrupt_frames.mp4")}},
  };
  return pairs;
}

}  // namespace mediadiff::test
