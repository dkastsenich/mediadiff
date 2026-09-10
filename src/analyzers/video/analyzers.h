#pragma once

// The `video.*` check family's registration declarations (04-01-PLAN.md,
// PROBE-03/VIDEO-05) -- src/probe/orchestrator.cpp assembles
// all_analyzers() from these named accessors, matching
// src/analyzers/{container,size}/analyzers.h's own established convention
// exactly.

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

}  // namespace mediadiff
