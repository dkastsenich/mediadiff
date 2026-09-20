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
// insufficient_data for a stream that decoded to zero samples. Writes the
// three evidence keys src/compare/hash.cpp already reads
// (decode_path_class, sampling_state, normalization) plus the D-03
// divergence-locator fields a non-pass finding needs.
const AnalyzerSpec& content_audio_sample_hash_analyzer();

}  // namespace mediadiff
