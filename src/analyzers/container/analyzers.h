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

// 03-05-PLAN.md Tasks 1-2 (PROBE-04, CONT-05): the six container.mp4.*
// checks, emitted from ProbeResults::bmff (src/probe/bmff_scan.h). Scoped
// to ContainerFamily::mp4 -- required_passes includes Pass::bmff_scan, so
// this analyzer (and therefore the scanner) is never even considered for
// an MKV/TS input (the orchestrator's own family-scoped union computation,
// PROBE-08, filters it out before Pass::bmff_scan ever enters the union).
const AnalyzerSpec& container_mp4_analyzer();

// The family-agnostic sibling of the analyzer above: scoped to
// ContainerFamily::other (runs for EVERY container, including MP4), but
// its own run() is a no-op whenever the file IS actually MP4 (the analyzer
// above already produced real measurements for that case). For every
// OTHER family it emits all six container.mp4.* checks as an explicit
// skipped:not_applicable_container Measurement (core/model.h's
// Measurement::skip_reason -- the same mechanism 03-04-PLAN.md's
// container.chapters established). This split exists because
// `inspect`/`compare` only ever render a check that has a real Measurement
// (src/cli/commands/inspect.cpp iterates Fingerprint::measurements, never
// the full CheckRegistry) -- a single analyzer scoped only to
// ContainerFamily::mp4 would leave container.mp4.* entirely ABSENT from a
// non-MP4 file's report rather than explicitly skipped, and declaring
// Pass::bmff_scan on a family-agnostic analyzer would run the scanner on
// every container, violating this plan's own prohibition. Two narrowly-
// scoped AnalyzerSpecs is what lets both requirements hold at once.
const AnalyzerSpec& container_mp4_not_applicable_analyzer();

// 03-06-PLAN.md Tasks 1-2 (PROBE-05, CONT-06): the four container.mkv.*
// checks, emitted from ProbeResults::ebml (src/probe/ebml_scan.h). Scoped
// to ContainerFamily::mkv -- required_passes includes Pass::ebml_scan, so
// this analyzer (and therefore the scanner) is never even considered for
// an MP4/TS input, mirroring container_mp4_analyzer()'s own reasoning
// exactly.
const AnalyzerSpec& container_mkv_analyzer();

// The family-agnostic sibling of the analyzer above -- same structural
// reason container_mp4_not_applicable_analyzer() exists (see that
// accessor's own comment): scoped to ContainerFamily::other, a no-op when
// the file IS actually MKV, and an explicit
// skipped:not_applicable_container Measurement for all four
// container.mkv.* checks on every OTHER family.
const AnalyzerSpec& container_mkv_not_applicable_analyzer();

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
