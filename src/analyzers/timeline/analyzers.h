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

// timeline.dts_monotonic / timeline.pts_unique / timeline.gaps /
// timeline.wrap_events (05-05-PLAN.md/05-06-PLAN.md, TIME-01/TIME-02/
// TIME-04): per timestamped stream EXCEPT Scope::Kind::subtitle
// (05-CHECK-ROSTER.md's own scope decision), the count of `dts[i] <=
// dts[i-1]` violations in READ order (doc 04 section 2) and the count of
// duplicate presentation PTS values, both over the SHARED PacketScan array
// with the AV_NOPTS_VALUE sentinel excluded from the axis view rather than
// counted (TIME-01) -- a stream whose axis is entirely absent reports
// `skipped:no_timing_data`, never a fabricated `0`. On a
// ContainerFamily::ts input the raw DTS/PTS sequences are unwrapped via
// unwrap.h's `unwrap_ts_timestamps` FIRST (05-02-PLAN.md, TIME-02) and the
// UNWRAPPED values are what either count analyses -- the `unwrapped`
// evidence flag on each Measurement records which basis produced the
// count.
//
// 05-06-PLAN.md extends this SAME analyzer (never a second AnalyzerSpec)
// with two more ids over the same per-stream loop: `timeline.gaps`
// (`span` semantic -- spans where the interval between two consecutive
// PRESENTATION-order timestamps strictly exceeds `max(2 x nominal,
// declared_duration + 1 tick)`, `nominal` from the ONE shared
// `derive_cadence` mode interval, PROBE-10) and `timeline.wrap_events`
// (`state` semantic -- `ts_33bit_wrap` when a ContainerFamily::ts stream's
// raw PTS sequence exhibits at least one wrap event under
// `unwrap_ts_timestamps`'s own asymmetric rule, `no_wrap` otherwise;
// `skipped:not_applicable_container` on a non-TS input). Both, like the
// two structural-integrity checks above, run over the UNWRAPPED
// presentation timeline on a TS input -- a genuine mid-file 33-bit wrap
// produces zero `timeline.gaps` spans and zero `timeline.dts_monotonic`
// violations attributable to the wrap itself (doc 04 section 5's own
// TS-wrap acceptance criterion).
//
// `required_passes = {Pass::demux_header, Pass::packet_scan}`,
// `scope = ContainerFamily::other` (this check family applies to every
// container; the TS-only unwrap step is a runtime branch on
// `container_family_from_format_name`, not a narrower AnalyzerSpec scope).
// Skip-reason priority: `partial_scan` (Phase 3 D-02, ahead of everything),
// then `no_timing_data` (no real value on the axis at all), then
// `insufficient_data` (an unwrap overflow, or a cadence/reconstruction
// overflow for `timeline.gaps`, T-05-18/T-05-19/T-05-24's own
// degrade-honestly rule) -- `timeline.wrap_events` additionally reports
// `not_applicable_container` on a non-TS input.
const AnalyzerSpec& timeline_monotonic_analyzer();

// timeline.discontinuities / timeline.discontinuities.flagged
// (05-07-PLAN.md, TIME-02/TIME-04, D-08): presentation-time jumps
// strictly exceeding the fixed `kDiscontinuityThresholdMs` (250ms, D-08 --
// never doc 04's own "(config)" knob) that are NOT explained by container
// structure. On a `ContainerFamily::ts` input, a jump is FLAGGED
// (`timeline.discontinuities.flagged`, `info`) when a transport packet
// carrying `discontinuity_indicator=1` (05-07-PLAN.md Task 1's recorded
// offsets, `src/probe/ts_scan.h`) falls within the demuxed packet's own
// `[pos, next_pos)` byte range; every other jump, and every jump on a
// non-TS input, is UNFLAGGED (`timeline.discontinuities`, `fail`, gating).
//
// Two `AnalyzerSpec`s, mirroring `src/analyzers/container/ts.cpp`'s own
// established two-spec convention so the TS scanner never runs on bytes
// it cannot interpret:
//   - `timeline_discontinuities_analyzer()` -- `scope =
//     ContainerFamily::other`, `required_passes = {Pass::demux_header,
//     Pass::packet_scan}`. Owns every NON-TS container (emits both ids,
//     `.flagged` as `skipped:not_applicable_container`); on a TS input it
//     emits NOTHING, deferring entirely to its sibling below.
//   - `timeline_discontinuities_ts_analyzer()` -- `scope =
//     ContainerFamily::ts`, `required_passes = {Pass::demux_header,
//     Pass::packet_scan, Pass::ts_scan}`. Owns TS inputs and performs the
//     flagged/unflagged split.
//
// Runs on every stream carrying timestamps EXCEPT `Scope::Kind::subtitle`
// (same scope as `timeline.gaps`). Skip-reason priority: `partial_scan`
// (Phase 3 D-02, ahead of everything -- TS-scoped: also when
// `TsScanResult::complete` is false, the discontinuity_indicator offset
// list itself is unreliable), `no_timing_data` (no real PTS at all),
// `insufficient_data` (an overflow anywhere in the checked interval/
// threshold arithmetic, the TS unwrap itself, or -- TS only -- when
// `PidStats::discontinuity_offsets_truncated` is set: a classification
// built on a partial flag list could silently demote real breakage to
// `info`, T-05-29, so both ids skip rather than classify), and --
// `.flagged` only, non-TS -- `not_applicable_container`.
const AnalyzerSpec& timeline_discontinuities_analyzer();
const AnalyzerSpec& timeline_discontinuities_ts_analyzer();

namespace detail {

// Which packet field the axis view below reads -- doc 04 section 2 defines
// `dts_monotonic` over DTS and `pts_unique` over PTS; both share the exact
// same sentinel-exclusion/read-order-preserving construction, so ONE
// parameterized view type serves both checks rather than two near-copies.
enum class Axis { dts, pts };

// One packet's own SURVIVING entry in an axis view: its ORIGINAL array
// position (`packet_index`, for evidence -- T-05-19's "excluded count"
// requirement needs the distinction between "this packet's own array
// position" and "this packet's own rank among real values", so the two are
// never conflated), its value on the axis under test (native ticks, or
// TS-unwrapped ticks once unwrap_axis_view below has run), and
// `PacketRecord::pos` (the byte offset evidence cites).
struct AxisSample {
  std::size_t packet_index = 0;
  std::int64_t value = 0;
  std::int64_t pos = 0;
};

// The axis view itself: READ-ORDER-preserving (array position order,
// mirroring `packets`' own `av_read_frame` order -- monotonicity is
// defined over READ order per doc 04 section 2, never a re-sort here),
// with every AV_NOPTS_VALUE-sentinel packet on this axis EXCLUDED rather
// than treated as a value (TIME-01) -- `excluded_count` is what evidence
// cites so a reader can tell "no violations" apart from "no usable data".
struct AxisView {
  std::vector<AxisSample> samples;
  std::int64_t excluded_count = 0;
};

// Builds `packets`' own axis view for `axis`: excludes every packet whose
// value on that axis is `INT64_MIN` (the AV_NOPTS_VALUE sentinel,
// preserved verbatim by PacketScan), preserving every surviving packet's
// own array position in `packet_index`. A pure, read-only function over a
// caller-owned span -- `packets` is never mutated or reordered.
AxisView build_axis_view(std::span<const PacketRecord> packets, Axis axis);

// Applies unwrap.h's `unwrap_ts_timestamps` to `view`'s own values (in
// read order, exactly as they already sit in `view.samples`) and returns a
// NEW AxisView whose samples carry the UNWRAPPED values with
// `packet_index`/`pos` preserved verbatim -- `view` itself is never
// mutated. Returns std::nullopt when `UnwrapResult::overflowed` is set
// (T-05-18: a crafted stream cannot grow the running unwrap offset without
// bound); the caller maps that to `SkipReason::insufficient_data`, never a
// wrapped or fabricated value. Exposed here (not folded into the
// analyzer's own run()) so a unit test can prove Test 6 (a wrapping TS
// sequence reports zero monotonicity violations from the wrap itself)
// directly against hand-built raw ticks, without a real fixture on disk.
std::optional<AxisView> unwrap_axis_view(const AxisView& view);

// timeline.dts_monotonic's own count: consecutive pairs in READ order
// where `dts[i] <= dts[i-1]` (doc 04 section 2 -- a TIE counts as a
// violation, not only a strict decrease; D-04's zero-magnitude tolerance
// on the registered check treats this count identically to `exact`
// equality against `0` while keeping `--tol timeline.dts_monotonic=2`
// meaningful). `first_violation_index`/`first_violation_pos` name the
// FIRST violating pair's SECOND member -- the packet whose own value broke
// the sequence, not the one before it.
struct MonotonicResult {
  std::int64_t violation_count = 0;
  std::optional<std::size_t> first_violation_index;
  std::optional<std::int64_t> first_violation_pos;
};

MonotonicResult count_dts_violations(const AxisView& view);

// timeline.pts_unique's own count: duplicate presentation PTS values,
// found by sorting a LOCAL COPY of `view.samples` by (value, packet_index)
// -- never `view` itself, never the caller's own read-order array -- and
// counting adjacent equal values. `first_duplicate_value`/
// `first_duplicate_index_a`/`first_duplicate_index_b` name the smallest
// duplicated value encountered walking the sorted order (deterministic:
// the `packet_index` tie-break makes the sort itself, and therefore which
// pair is "first", reproducible across runs even when three or more
// packets share one value).
struct DuplicateResult {
  std::int64_t duplicate_count = 0;
  std::optional<std::int64_t> first_duplicate_value;
  std::optional<std::size_t> first_duplicate_index_a;
  std::optional<std::size_t> first_duplicate_index_b;
};

DuplicateResult count_pts_duplicates(const AxisView& view);

}  // namespace detail

}  // namespace mediadiff
