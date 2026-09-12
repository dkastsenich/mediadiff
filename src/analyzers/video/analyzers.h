#pragma once

// The `video.*` check family's registration declarations (04-01-PLAN.md,
// PROBE-03/VIDEO-05) -- src/probe/orchestrator.cpp assembles
// all_analyzers() from these named accessors, matching
// src/analyzers/{container,size}/analyzers.h's own established convention
// exactly.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "probe/pass.h"

namespace mediadiff {

// video.gop.length (04-01-PLAN.md Task 2, this phase's tracer check --
// PROBE-03/VIDEO-05): the median keyframe-to-keyframe access-unit
// distance, derived from ParserScanResult::key_frame flags. Scoped
// ContainerFamily::other (a codec-scoped check, not a container-scoped
// one -- VIDEO-12's own `skipped:no_parser` path, not
// `skipped:not_applicable_container`, covers a codec with no registered
// parser). required_passes = {Pass::demux_header, Pass::packet_scan,
// Pass::parser_scan} -- Pass::packet_scan is declared explicitly here
// (not left to src/probe/orchestrator.cpp's own parser_scan-implies-
// packet_scan rule) so this AnalyzerSpec's own required_passes is
// self-describing.
const AnalyzerSpec& video_gop_analyzer();

// video.codec/profile/level/resolution/frame_count (04-06-PLAN.md, VIDEO-01/
// VIDEO-02): the five per-video-stream identity checks, extracted directly
// from AVStream.codecpar (via DemuxSession::stream_info) after the header
// pass alone -- no registered parser required. Scoped ContainerFamily::other
// (codec-scoped, not container-scoped: every codec has codecpar fields,
// regardless of whether it has a registered libav parser). required_passes
// = {Pass::demux_header, Pass::packet_scan} -- Pass::packet_scan is
// declared only because video.frame_count needs the packet count; the
// other four checks need nothing past demux_header.
const AnalyzerSpec& video_stream_params_analyzer();

namespace detail {

// video.profile's own compared-value rule (VIDEO-01-E2, 04-06-PLAN.md):
// the resolved profile name when avcodec_profile_name found one,
// otherwise the raw integer's own decimal spelling -- never a shared
// "unknown" word, so two files with DIFFERENT unresolved profile integers
// compare as different rather than silently agreeing. Exposed here so
// tests/unit/test_video_stream_params.cpp can drive it directly with two
// distinct unresolved profile integers, mirroring
// src/analyzers/size/analyzers.h's own detail::compute_peak_window
// precedent for the identical "not practically reachable from one real
// fixture pair" problem shape.
std::string render_profile_value(const std::optional<std::string>& profile_name, int profile);

// video.level's own hand-written codec-specific human-string table
// (04-06-PLAN.md flagged assumption A1): libav has no single API that
// renders a level this way, so this table is this project's own and is
// kept deliberately small -- only the codecs claude_docs/
// 03-video-analysis.md section 2 names explicitly (H.264, HEVC, AV1).
// Every other codec, and every level value this table's own arithmetic
// does not accept as valid, falls through to the raw integer's own
// decimal spelling rather than guessing at a spelling this project has
// never verified. HEVC tier is deliberately NOT folded into the rendered
// string -- see docs/checks/video.level.md for the full empirical
// finding (general_tier_flag is not exposed by any public libav surface
// reachable without a decode pass, which this phase does not have).
std::string render_level_value(const std::string& codec_name, int level);

// video.sar/video.dar/video.sar.conflict's own "0/1 means unset, treated as
// 1:1" rule (VIDEO-01-E1, 04-07-PLAN.md, doc 03 section 2's own wording).
// `num`/`den` are always a valid, positive-denominator rational usable
// directly for comparison; `unset` records whether the RAW value this was
// resolved from was actually `0/den` (any den), so a stream that declares
// nothing stays distinguishable in evidence from one explicitly declaring
// `1:1`, even though both resolve to the identical comparable ratio.
// Exposed here so tests/unit/test_video_stream_params.cpp can drive the
// unset/explicit-1:1 distinction directly.
struct EffectiveSar {
  std::int64_t num = 1;
  std::int64_t den = 1;
  bool unset = false;
};

EffectiveSar resolve_sar(std::int64_t raw_num, std::int64_t raw_den);

// video.dar's own rational derivation: width*sar_num over height*sar_den,
// reduced by the greatest common divisor, every step through the checked
// integer helpers -- nullopt on a zero width, height, or sar denominator
// (no real fixture can produce this, every real video has nonzero
// dimensions; exposed here so Test 7 can drive the refusal directly).
std::optional<std::pair<std::int64_t, std::int64_t>> compute_dar(std::int64_t width, std::int64_t height,
                                                                    std::int64_t sar_num, std::int64_t sar_den);

}  // namespace detail

}  // namespace mediadiff
