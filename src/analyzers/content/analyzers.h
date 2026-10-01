#pragma once

// 06-01-PLAN.md (Phase 6's tracer): the `content` analyzer family's
// registration header -- mirrors src/analyzers/video/analyzers.h's own
// AnalyzerSpec-accessor shape. `src/analyzers/content/` held only a
// .gitkeep before this plan; this is its first real content.

#include "probe/pass.h"

namespace mediadiff {

// content.audio.sample_hash (06-CHECK-ROSTER.md): required_passes =
// {Pass::demux_header, Pass::packet_scan, Pass::audio_decode},
// scope = ContainerFamily::other (every container). Emits one
// measurement per audio stream at Scope::Kind::audio. Skip-reason
// priority: partial_scan first (a truncated packet scan), then
// requires_decode when ProbeResults::audio_decode is std::nullopt
// (content decode was not requested for this invocation), then
// partial_scan AGAIN when StreamAudioDecode::undecodable is true
// (06-10-PLAN.md, D-09: the narrow "genuinely could not run" case -- zero
// decoded frames across the whole sweep), then insufficient_data for a
// stream that decoded to zero samples with zero decode errors. Writes the
// three evidence keys src/compare/hash.cpp already reads
// (decode_path_class, sampling_state, normalization) plus the D-03
// divergence-locator fields a non-pass finding needs.
const AnalyzerSpec& content_audio_sample_hash_analyzer();

// 07-01-PLAN.md (CONTENT-01, D-05/D-06/D-09; 07-CHECK-ROSTER.md):
// content.video.frame_hash -- required_passes = {Pass::demux_header,
// Pass::packet_scan, Pass::video_decode}, scope = ContainerFamily::other.
// Emits one measurement per video stream at Scope::Kind::video (the stream's
// rank among video streams, the convention every video.* check uses), and
// nothing for an attached picture. Skip-reason priority: partial_scan (a
// truncated packet scan), then requires_decode (ProbeResults::video_decode
// absent, or the stream never attempted), then -- after pushing the
// envelope's decode_path record -- partial_scan AGAIN when the stream is
// undecodable, then insufficient_data for a stream that decoded zero frames.
// The value is a HashChain carrying one digest and one tick per frame; the
// three evidence keys src/compare/hash.cpp already reads (decode_path_class,
// sampling_state, normalization) ride with it, plus decoder_flags,
// decode_error_count, corrupt_frame_count, geometry_change_count, timestamps
// and frame_interval for the divergence locator (07-03).
const AnalyzerSpec& content_video_frame_hash_analyzer();

// 07-05-PLAN.md (CONTENT-06; 07-CHECK-ROSTER.md): content.video.frozen_runs
// and content.video.black_runs -- required_passes = {Pass::demux_header,
// Pass::packet_scan, Pass::video_decode}, scope = ContainerFamily::other.
// Both are `span`/`ms` checks emitted together, one pair per non-attached-
// picture video stream at Scope::Kind::video. Skip-reason priority:
// partial_scan (a truncated packet scan), requires_decode, partial_scan again
// for an undecodable or truncated decode (evidence carries the truncation
// reason), insufficient_data for a stream that decoded zero frames or whose
// thumbnail could not be scored, no_timing_data when a run exists but no frame
// timing does. Otherwise a REAL SpanList (empty when nothing qualifies, never
// Absent{} for a decoded stream). Spans are measured from the stream's own
// first frame, end-exclusive at the last run frame's time plus one frame
// interval, as integer-millisecond RationalValues.
const AnalyzerSpec& content_video_runs_analyzer();

// 07-08-PLAN.md (CONTENT-04; D-01, 07-CHECK-ROSTER.md two-file scope rule):
// content.video.perceptual's ONE-SIDED emission -- required_passes =
// {Pass::demux_header, Pass::packet_scan, Pass::video_decode}, scope =
// ContainerFamily::other. For a file with a primary video stream (the first one
// that is not an attached picture) it emits exactly one measurement at
// Scope{video, 0}: skipped:requires_decode when content decode is off, else
// skipped:requires_media. A perceptual score needs BOTH pictures, so it exists
// only in a live media-vs-media compare, where probe/lockstep.cpp replaces this
// placeholder on both fingerprints (never duplicates it); `snapshot`, `inspect`
// and the snapshot side of a compare keep it, so `inspect` renders an explicit
// skip row and SNAP-06 stays clean. A file with no video stream emits nothing,
// matching the video.* family.
const AnalyzerSpec& content_video_perceptual_analyzer();

// True when the probed file has a PRIMARY video stream -- the first video stream
// that is not an attached picture (the two-file scope rule shared by
// content.video.perceptual and every quality.* id, 07-CHECK-ROSTER.md). Defined
// in video_perceptual.cpp; the one definition both analyzers call.
bool content_has_primary_video(const ProbeResults& results);

// 07-10-PLAN.md (CONTENT-08, CONTENT-10; D-01): quality.psnr and quality.ssim's
// ONE-SIDED emission, the same shape as content_video_perceptual_analyzer()
// above -- one explicit skip per id at Scope{video, 0} for a file with a primary
// video stream (skipped:requires_decode under --no-content, else
// skipped:requires_media), replaced on both fingerprints by probe/lockstep.cpp in
// a live media-vs-media compare. So a snapshot records the two ids only as
// requires_media skips (never a score), and a compare against a snapshot reports
// requires_media on both sides. 07-11 adds quality.vmaf to this same analyzer.
const AnalyzerSpec& content_quality_analyzer();

}  // namespace mediadiff
