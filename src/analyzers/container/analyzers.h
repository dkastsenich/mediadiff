#pragma once

// The container analyzer family's registration declarations (PROBE-08).
// src/probe/orchestrator.cpp assembles all_analyzers() from these named
// accessors in one explicit, hand-written order -- see that file's own
// comment for why this is deliberately not a self-registering-static
// list.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/rational.h"
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

// 03-08-PLAN.md Tasks 1-3 (PROBE-06/07 consumer, CONT-07, CONT-08): the six
// container.ts.* checks, emitted from ProbeResults::ts (src/probe/ts_scan.h).
// Scoped to ContainerFamily::ts -- required_passes includes Pass::ts_scan,
// so this analyzer (and therefore the scanner) is never even considered for
// an MP4/MKV input, mirroring container_mp4_analyzer()/container_mkv_analyzer()'s
// own reasoning exactly. Three of the six (pcr_interval, psi_interval,
// pmt_version_churn) emit one measurement per program at
// Scope{Kind::program, program_number} -- the PSI value, never an
// AVFormatContext::programs[] array position (CONT-08); the other three
// (cc_errors, cc_discontinuities, null_ratio) stay at global scope, since
// they are whole-transport-stream properties.
const AnalyzerSpec& container_ts_analyzer();

// The family-agnostic sibling of the analyzer above -- same structural
// reason container_mp4_not_applicable_analyzer()/container_mkv_not_applicable_analyzer()
// exist (see either accessor's own comment): scoped to ContainerFamily::other,
// a no-op when the file IS actually MPEG-TS, and an explicit
// skipped:not_applicable_container Measurement for all six container.ts.*
// checks on every OTHER family (at global scope even for the three
// normally program-scoped checks, mirroring the incomplete-walk skip's own
// reasoning: there is no program list to skip at when the file is not TS).
const AnalyzerSpec& container_ts_not_applicable_analyzer();

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

// Test-only extraction point (03-13-PLAN.md Task 1, CR-01/CR-02): the
// pure, checked, totally-ordered median-duration computation
// container.mp4.fragment_duration's emit_fragment_duration (mp4.cpp)
// wraps, exposed here so tests/unit/test_mp4_fragment_duration.cpp can
// drive it directly against hand-built extreme-DTS arrays -- mirroring
// src/analyzers/size/analyzers.h's own detail::compute_peak_window
// precedent for the identical problem shape: an overflow-triggering DTS
// value, or a tick/timebase combination extreme enough to overflow a
// cross-multiplied comparator, is not practically reachable from a real
// muxed fixture under this project's bitexact-only fixture discipline
// (D-08), so the seam is what makes the input reachable at all.
enum class MedianDurationStatus {
  ok,
  // Fewer than two DTS values, a non-positive timebase numerator or
  // denominator, or an overflow anywhere in the adjacent-delta
  // computation -- every one of these is "a real median cannot be
  // computed", collapsed to the one SkipReason the registered check
  // itself expresses (SkipReason::insufficient_data).
  cannot_determine,
};

struct MedianDurationResult {
  MedianDurationStatus status = MedianDurationStatus::cannot_determine;
  // The lower-median inter-keyframe duration, valid only when status ==
  // MedianDurationStatus::ok.
  Ticks median{};
};

// Computes the lower median of the adjacent deltas between `keyframe_dts`
// (in `tb`-timebase ticks). Every delta is built through
// detail::checked_sub (core/rational.h) -- CR-01: a crafted file's
// adjacent keyframe DTS values are file-controlled and unbounded, unlike
// the byte-offset deltas elsewhere in this analyzer family, so this is
// the one DTS-delta site that must never perform a raw signed
// subtraction. A single overflowing delta refuses the WHOLE computation
// rather than skipping the offending pair and continuing: a median
// computed from a filtered subset is a fabricated answer, which this
// project treats as worse than refusing.
//
// The result is ordered by raw std::int64_t tick value, NEVER through
// compare_ticks_checked -- CR-02: every duration in this computation
// shares the SAME timebase `tb` by construction, and `tb`'s numerator and
// denominator are proven strictly positive before any delta is built, so
// ordering by tick value is exactly equivalent to ordering by real
// duration AND is a total order on std::int64_t that cannot overflow.
// compare_ticks_checked exists for the general two-timebase case and
// folds an overflowing comparison into "equivalent" (core/rational.h's
// own WR-03 comment) -- calling std::stable_sort with a comparator whose
// result can depend on an overflow condition is not a strict weak order,
// which is undefined behavior during the sort call itself. Do not
// "restore" compare_ticks_checked here.
//
// `keyframe_dts` need not be sorted; the caller's span is never reordered
// (a local copy is sorted internally instead), mirroring
// detail::compute_peak_window's own `packets` contract.
MedianDurationResult compute_median_fragment_duration(std::span<const std::int64_t> keyframe_dts, Rational tb);

}  // namespace detail

}  // namespace mediadiff
