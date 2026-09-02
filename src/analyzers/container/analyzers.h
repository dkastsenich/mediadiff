#pragma once

// The container analyzer family's registration declarations (PROBE-08).
// src/probe/orchestrator.cpp assembles all_analyzers() from these named
// accessors in one explicit, hand-written order -- see that file's own
// comment for why this is deliberately not a self-registering-static
// list.

#include <string>
#include <string_view>
#include <vector>

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

// 03-04-PLAN.md Tasks 2-3 (CONT-03, CONT-04): meta.tags and
// meta.tags.language, both emitted from src/analyzers/container/meta.cpp's
// one shared per-dictionary walk (container + one entry per stream).
// Scoped to ContainerFamily::other -- both checks apply to every container.
const AnalyzerSpec& container_meta_analyzer();

namespace detail {

// Test-only extraction seam (03-04-PLAN.md Task 2, T-3-15): exposes
// src/analyzers/container/meta.cpp's UTF-8 replacement-character
// sanitizer so tests/unit/test_meta_tags.cpp can drive it directly with a
// hand-crafted invalid byte sequence -- no reasonably-constructible
// bitexact media fixture reliably produces one (this project's own
// fixture discipline, D-08), matching src/probe/packet_scan.h's
// detail::make_packet_record precedent for the identical problem shape.
std::string sanitize_utf8_for_test(std::string_view input);

// Test-only extraction seam (03-04-PLAN.md Task 2's own acceptance
// criterion: "a unit test asserts the volatile constant member by member,
// so silently adding a fifth key becomes a test failure rather than a
// quiet policy change"): returns meta.cpp's kVolatileTagKeys as an
// ordinary std::vector so a test can assert on its exact size and exact
// members without meta.cpp exposing the constexpr array itself.
std::vector<std::string> volatile_tag_keys_for_test();

}  // namespace detail

}  // namespace mediadiff
