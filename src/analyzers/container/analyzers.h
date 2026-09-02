#pragma once

// The container analyzer family's registration declarations (PROBE-08).
// src/probe/orchestrator.cpp assembles all_analyzers() from these named
// accessors in one explicit, hand-written order -- see that file's own
// comment for why this is deliberately not a self-registering-static
// list.

#include "probe/pass.h"

namespace mediadiff {

// container.format (doc 02 section 2): the container family's own name,
// derived from DemuxSession::format_name(), as one exact-semantic
// `Measurement`. Scoped to ContainerFamily::other ("every container") --
// this is the phase's tracer check, deliberately family-agnostic.
//
// 03-04-PLAN.md Task 1 also expands this same analyzer with
// container.track_count/track_types/track_order/chapters -- one AnalyzerSpec,
// still ContainerFamily::other, matching topology.cpp's own doc comment on
// why these five checks share one analyzer rather than five.
const AnalyzerSpec& container_topology_analyzer();

}  // namespace mediadiff
