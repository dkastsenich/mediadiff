#pragma once

// The `timeline.*` check family's registration declarations (05-01-PLAN.md,
// TIME-01/TIME-03) -- mirrors src/analyzers/{container,size,video}/
// analyzers.h's own established convention exactly: one
// `const AnalyzerSpec&`-returning declaration per analyzer file, each with
// a doc comment naming the owning TIME-NN requirement, the shaping D-NN
// decision, its Scope::Kind usage and its required_passes.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/model.h"
#include "core/rational.h"
#include "core/value.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

// timeline.start / timeline.duration / timeline.duration.coherence
// (05-01-PLAN.md's tracer, extended by 05-04-PLAN.md -- TIME-01/TIME-03,
// D-03): ONE Scope{Kind::global} measurement holding the file's earliest
// presentation time, plus ONE per-stream measurement (Scope::Kind::video/
// audio/subtitle/data) holding that stream's own first presentation PTS
// minus the global origin -- never a per-stream absolute PTS (D-03: a
// whole-file shift is one finding at global scope, not one per stream).
// 05-04-PLAN.md adds, per timestamped stream, the duration TRIPLE
// (container-declared / stream-declared / computed, doc 04 section 1.3)
// as `timeline.duration`'s own compared value plus evidence, and the
// triple's own internal cross-check as `timeline.duration.coherence`'s
// `state`-semantic value (Phase 4 D-10's precedent, mirrors
// video.hdr.coherence). required_passes = {Pass::demux_header,
// Pass::packet_scan} -- declared explicitly here (never left to an
// implication rule), matching video_gop_analyzer()'s own convention. Scope
// ContainerFamily::other -- a timeline check applies to every container.
// This is the one registration point later timeline plans (05-05 onward)
// append sibling analyzers to.
const AnalyzerSpec& timeline_start_duration_analyzer();

namespace detail {

// One stream's own candidate for the file's global origin: its own first
// (in presentation order) non-AV_NOPTS_VALUE PTS, in NATIVE ticks, plus
// the stream's own timebase and Scope. Exposed here (mirrors
// src/analyzers/video/analyzers.h's own detail:: exposure convention) so
// tests/unit/test_timeline_start_duration.cpp can drive
// global_origin_ticks directly over hand-built candidate lists, without a
// real fixture on disk.
struct StreamOriginCandidate {
  Scope scope;
  std::int64_t first_pts_ticks = 0;
  Rational tb{1, 1};
};

// D-03's own scoping rule, step 1: the stream's own first PTS in
// presentation order, in NATIVE ticks -- the minimum value among every
// packet whose `pts` is not the AV_NOPTS_VALUE sentinel (INT64_MIN,
// preserved verbatim by PacketScan -- src/probe/packet_scan.h's own
// documented contract). "First in presentation order" over the set of
// valid-PTS packets is exactly the minimum PTS value among them -- a
// packet's ARRAY position never enters this comparison, matching
// probe/cadence.cpp's own "sort an index view, never trust read order"
// discipline (deliberately implemented here as a linear scan rather than
// a full sort, since only the minimum VALUE is needed, not a full
// ordering -- a strict `<` comparison against the running champion,
// applied in array order, is already a stable, deterministic reduction:
// Test 8's "byte-identical across repeated runs, including when two
// packets share an identical PTS" holds because ties never replace the
// champion). Returns std::nullopt when no packet in `packets` carries a
// real PTS at all (SkipReason::no_timing_data at the call site).
std::optional<std::int64_t> first_presented_pts(std::span<const PacketRecord> packets);

// D-03's own scoping rule, step 2: given every timestamped stream's own
// candidate (native ticks, own timebase), finds the file's single global
// origin -- the candidate with the smallest REAL-TIME value, compared via
// core/rational.h's own checked cross-multiplication
// (compare_ticks_checked), never by first rescaling every candidate to a
// shared timebase via division (core/rational.h's own WR-03 comment: a
// real ordering DECISION, not a cosmetic rendering, must be able to tell
// an overflow apart from a genuine tie). Returns std::nullopt when the
// input is empty, or when the comparison overflows int64_t anywhere
// (T-05-01: a crafted extreme PTS value degrades this check to
// SkipReason::insufficient_data, never a wrapped or fabricated origin).
std::optional<StreamOriginCandidate> global_origin_ticks(std::span<const StreamOriginCandidate> candidates);

// Converts one native-tick PTS value to a millisecond count -- `ms =
// ticks * 1000 * tb.num / tb.den`, via core/rational.h's
// detail::checked_mul then detail::checked_div, mirroring
// src/analyzers/container/ts.cpp's own bytes_to_ms exactly (the
// established project convention for every OTHER `unit = "ms"` check:
// container.ts.pcr_interval/psi_interval both construct their own
// RationalValue as `{ms, 1, Rational{1,1}}` this same way). Deliberately
// NOT an unreduced exact fraction carrying the stream's own raw
// denominator forward: a real fixture's native timebase denominators
// (e.g. tb.den in the hundreds of millions after even one
// cross-multiplication) make an unreduced fraction's num/den grow fast
// enough to overflow the COMPARE layer's own delta cross-multiplication
// (src/compare/tol.cpp) on perfectly ordinary files -- confirmed
// empirically against a real MP4-to-TS remux during this task's own
// execution. `checked_div` truncates toward zero, the same rounding
// every other ms-unit check in this project already accepts. Returns
// std::nullopt on any overflow (never a floating-point type anywhere).
std::optional<RationalValue> ticks_to_ms(std::int64_t ticks, Rational tb);

// The millisecond-scale difference `a - b`, both already expressed via
// ticks_to_ms above -- cross-multiplied via checked_mul/checked_sub,
// mirroring src/compare/tol.cpp's own delta_num/delta_den construction
// exactly (never a float). With both operands carrying `den == 1` (the
// only shape ticks_to_ms ever produces) this degenerates to a plain
// checked subtraction; the general cross-multiplication form is kept so
// this helper stays correct for any RationalValue, not only this
// specific caller's own shape. Returns std::nullopt on any overflow.
std::optional<RationalValue> subtract_ms(const RationalValue& a, const RationalValue& b);

// 05-04-PLAN.md (TIME-01/TIME-03), doc 04 section 1.3: one packet's own
// presentation-order position plus its RECONSTRUCTED duration -- `declared`
// stays false when the packet's own PacketRecord::duration was usable as
// declared (> 0); `declared` (bearing the name `reconstructed` on the
// field, matching the check's own `duration_source` evidence spelling) is
// true when the value was substituted, per doc 04's own rule: the delta to
// the next PTS in presentation order, or -- for the LAST packet in
// presentation order -- the shared cadence's own `mode_interval_ticks`
// (never a second, independent cadence computation).
struct ReconstructedDuration {
  std::int64_t pts_ticks = 0;
  std::int64_t duration_ticks = 0;
  bool reconstructed = false;
};

// Reconstructs every valid-pts packet's own duration in `packets`, in
// PRESENTATION order (sorted by pts ascending -- NOT the input span's own
// `av_read_frame` read order; a LOCAL index view is sorted internally,
// `packets` itself is never reordered, mirroring probe/cadence.cpp's own
// "sort an index view, never trust read order" discipline). Returns
// std::nullopt when `packets` carries no packet with a valid (non-
// AV_NOPTS_VALUE) pts at all, or when any arithmetic step overflows
// (T-05-14: the delta-to-next-pts computation, or deriving the shared
// cadence for the last packet) -- never a wrapped or fabricated duration.
std::optional<std::vector<ReconstructedDuration>> reconstruct_packet_durations(
    std::span<const PacketRecord> packets, Rational tb);

}  // namespace detail

}  // namespace mediadiff
