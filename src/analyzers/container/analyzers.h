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
const AnalyzerSpec& container_topology_analyzer();

}  // namespace mediadiff
