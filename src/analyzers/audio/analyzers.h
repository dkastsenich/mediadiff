#pragma once

// The `audio.*` check family's registration declarations (06-03-PLAN.md,
// AUDIO-01/AUDIO-02) -- src/probe/orchestrator.cpp assembles all_analyzers()
// from these named accessors, matching src/analyzers/{video,timeline}/
// analyzers.h's own established convention exactly.

#include <cstdint>

#include "probe/pass.h"

namespace mediadiff {

// audio.codec/sample_rate/sample_fmt/bit_depth/channels/layout (06-03-PLAN.md,
// AUDIO-01/AUDIO-02): six per-audio-stream identity checks, all extracted
// directly from AVStream.codecpar (via DemuxSession::stream_info) after the
// header pass alone -- no decode is required for any of the six, so they
// report real values under `--no-content` (AUDIO-01). Scoped
// ContainerFamily::other (codec-scoped, not container-scoped: every codec
// has codecpar fields, regardless of container family). required_passes =
// {Pass::demux_header, Pass::packet_scan} -- Pass::packet_scan is declared
// only so this analyzer can gate every one of the six behind
// `skipped:partial_scan` when the shared sweep truncated (D-02, Phase 3
// CONTEXT.md: a truncated scan makes dependent checks skip rather than
// report a number) -- mirrors video_stream_params_analyzer()'s own
// `partial_scan`-then-`insufficient_data` skip-reason priority exactly.
// `audio.layout` (Task 2) is the sixth measurement this same analyzer
// emits, derived through `av_channel_layout_describe` on the modern
// per-stream channel-layout struct only -- the legacy integer mask field
// and every one of its named bitmask constants are absent from the linked
// headers this build compiles against (this project's own prohibition;
// see 06-03-PLAN.md). An unspecified layout records its own canonical
// spelling as a real, comparable value -- never `Absent{}` and never a
// skip -- so a candidate that LOSES its layout reports a regression,
// following src/analyzers/video/color.cpp's established treatment of
// `unspecified` as a value rather than a wildcard (D-14 of 06-CONTEXT.md,
// applied here to layout).
const AnalyzerSpec& audio_stream_params_analyzer();

}  // namespace mediadiff
