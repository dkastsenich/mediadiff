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
#include "probe/cadence.h"
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

// timeline.jitter / timeline.vfr_profile (05-08-PLAN.md, TIME-05): both
// consume `probe/cadence.h`'s shared `derive_cadence` ONCE per stream
// (PROBE-10 -- never a second interval tally or a second CFR/VFR
// classification; `Cadence::klass` alone decides). `timeline.jitter`
// reports sigma (an exact fixed-point `RationalValue`, denominator
// `2^kJitterSigmaFixedShift`, `core/rational.h`) and carries
// `max_abs_deviation_ms` in evidence on the SAME id -- 05-CHECK-ROSTER.md's
// Discretion resolution, never a second check id -- computed via a
// PORTABLE INTEGER square root (`detail::Int128Accum::try_isqrt`), no
// floating point anywhere in the compared value. `SkipReason::vfr` on a
// VFR stream, `Absent{}`, never a sigma computed over a distribution with
// no nominal. `timeline.vfr_profile` bins every interval by its D-06
// deviation from the stream's OWN grid (`Cadence::ideal_interval_num/den`),
// never raw ticks -- the property that makes the same content bin
// identically across MP4 and Matroska despite storing the interval as
// different tick counts. `required_passes = {Pass::demux_header,
// Pass::packet_scan}`, `scope = ContainerFamily::other` (every
// container). Runs on every stream carrying timestamps EXCEPT
// `Scope::Kind::subtitle`, mirroring `timeline_monotonic_analyzer()`'s
// own scope decision. Skip-reason priority: `partial_scan` (Phase 3 D-02,
// ahead of everything -- both ids), then whatever `derive_cadence` itself
// reports (`no_timing_data`/`insufficient_data`) for both ids identically
// (never allowing one id to compute a real value while the other skips
// for a DIFFERENT reason on the SAME stream).
const AnalyzerSpec& timeline_jitter_vfr_analyzer();

namespace detail {

// jitter_vfr.cpp's own axis-sorted interval walk (05-08-PLAN.md Task 2):
// the SAME axis `derive_cadence` itself used (`Cadence::axis`, never
// re-derived), walked via a LOCAL SORTED COPY (mirrors
// `probe/cadence.cpp`'s and this file's own `count_pts_duplicates`'
// "sort a copy, never the caller's own read-order array" discipline) to
// produce the raw tick-domain interval list BOTH `timeline.jitter` and
// `timeline.vfr_profile` consume. This is NOT a second cadence/CFR-VFR
// tally -- that verdict comes from `Cadence::klass` alone, never
// recomputed here -- it is the per-interval VALUE list `Cadence` itself
// never exposes (only aggregate counts), which both checks' own
// statistics (variance, grid-relative bin membership) need, mirroring
// `timeline.gaps`' own `detail::reconstruct_packet_durations`-then-walk
// shape one file over. Returns std::nullopt only when a `checked_sub`
// overflows walking the sorted axis (a crafted timestamp pair whose
// difference does not fit `int64_t`) -- the caller degrades to
// `insufficient_data`, never a wrapped or fabricated interval.
std::optional<std::vector<std::int64_t>> compute_sorted_axis_intervals(std::span<const PacketRecord> packets,
                                                                          CadenceAxis axis);

// D-06's six fixed fractional buckets, in ascending-threshold CASCADE
// order (05-CHECK-ROSTER.md's own approved labels: `on_grid`, `one_tick`,
// `one_percent`, `two_x`, `three_x`, `longer`): `interval`'s deviation
// from the stream's own ideal grid interval (`ideal_num`/`ideal_den`,
// `Cadence::ideal_interval_num/den`) decides EXACTLY one of the six
// labels. An interval shorter than the ideal by more
// than one percent falls through every named tier (`two_x`/`three_x` are
// evaluated only when the interval is genuinely LONGER than the ideal)
// and lands in the `longer` catch-all -- the fixed six-label vocabulary
// 05-CHECK-ROSTER.md approves has no dedicated "shorter" bucket. Returns
// std::nullopt on any checked-arithmetic overflow (a crafted ideal/
// interval pair) -- the caller degrades to `insufficient_data`, never a
// fabricated bucket.
//
// UD-2 quantization rule (05-19-PLAN.md, WINDOWS #28): with
// `Q = interval*ideal_den - ideal_num` (the cross-multiplied deviation,
// in the same unreduced denominator `ideal_den` as `ideal_num`),
// `on_grid` now means `|Q| < ideal_den` (STRICTLY below one tick of the
// stream's own timebase -- representational rounding a non-exactly-
// representable ideal interval cannot avoid, never real jitter) and
// `one_tick` means `ideal_den <= |Q| < 2*ideal_den`. The boundary is
// STRICT BELOW on the on_grid side, INCLUSIVE at exactly one tick on the
// one_tick side (TIME-05/adjacency: a deviation of exactly one tick is
// NOT sub-tick and never counts as on_grid). Every bucket at or past
// `one_percent` is UNCHANGED. For a stream whose ideal interval is an
// EXACT INTEGER number of ticks (`ideal_num` an exact multiple of
// `ideal_den`), `|Q|` is itself always a multiple of `ideal_den`, so
// `on_grid` reduces to the pre-quantization `|Q| == 0` test and
// `one_tick` reduces to the pre-quantization `|Q| == ideal_den` test --
// every such stream bins EXACTLY as it did before this rule (04-17/05-08
// precedent's own "amendment recorded, not silently replacing" style
// applies here too: the old exact-equality rule is superseded for
// non-integer ideals, unchanged for integer ones).
std::optional<std::string> classify_vfr_bin(std::int64_t interval, std::int64_t ideal_num, std::int64_t ideal_den);

// timeline.jitter's own sigma computation (05-08-PLAN.md Task 2, A1;
// re-referenced from the exact ideal interval rather than the mode by
// 05-19-PLAN.md, UD-2, WINDOWS #28): given the stream's own EXACT ideal
// interval (`ideal_num`/`ideal_den`, `Cadence::ideal_interval_num/den`,
// D-05's unreduced span/count rational -- never `mode_interval_ticks`,
// which is timebase-bound and is exactly what produced the NTSC false
// positive) and the raw tick-domain interval list
// `compute_sorted_axis_intervals` above produces, returns the exact
// fixed-point sigma NUMERATOR (denominator is the fixed
// `2^kJitterSigmaFixedShift` scale, `core/rational.h`) and the maximum
// absolute deviation in the SAME fixed-point scale (`max_abs_deviation_fixed`,
// no longer a bare tick count -- a deviation can now be a genuine
// fraction of one tick) -- both computed with ONLY checked-integer and
// `detail::Int128Accum`-wide arithmetic, no floating point, no
// standard-library square root anywhere. Per interval, with
// `Q = interval*ideal_den - ideal_num`: a deviation STRICTLY BELOW one
// tick (`|Q| < ideal_den`) contributes a `d_fixed` term of EXACTLY ZERO
// (UD-2: representational rounding is not jitter) and is tallied in
// `sub_tick_intervals`; every other deviation forms its fixed-point
// magnitude as `(|Q| / ideal_den) * 2^kJitterSigmaFixedShift +
// round_half_even((|Q| % ideal_den) * 2^kJitterSigmaFixedShift /
// ideal_den)` -- an EXACT integer division for the whole-tick part, and a
// round-half-to-even (ties resolve to the even candidate) integer
// division for the sub-tick remainder's own fixed-point fraction, formed
// entirely from `checked_mul`/an integer remainder comparison, never a
// floating-point divide (A1: zeroing applies ONLY strictly below one
// tick -- a deviation of 1.4 ticks contributes its full 1.4 ticks, never
// 0.4). Deviations are scaled to fixed-point BEFORE being squared (never
// a separate wide-times-scalar step): accumulating `d_fixed^2` directly
// is algebraically the scaled-variance term sigma's own fixed-point root
// needs, without ever narrowing the raw (unscaled) sum first. Returns
// std::nullopt when `intervals` is empty (no interval to measure a sigma
// over), on any checked-arithmetic overflow forming a scaled deviation,
// or when the WIDE accumulated sum of squares itself cannot narrow to
// `int64_t` (T-05-34: a crafted interval distribution inflating the sum
// past what any real file could produce) -- the caller degrades to
// `insufficient_data`, never a wrapped or fabricated sigma.
struct JitterSigmaResult {
  // sigma_true_ticks * 2^kJitterSigmaFixedShift, FLOORED (truncated
  // toward zero, never rounded -- A1's own fixed-point contract).
  std::int64_t sigma_fixed_numerator = 0;
  // The largest single interval's own fixed-point deviation magnitude
  // (`d_fixed` above, scale `2^kJitterSigmaFixedShift`) -- replaces the
  // pre-05-19 bare `max_abs_deviation_ticks` tick count, since a
  // deviation can now genuinely be a fraction of one tick.
  std::int64_t max_abs_deviation_fixed = 0;
  std::int64_t considered_intervals = 0;
  // The count of intervals whose deviation was STRICTLY below one tick
  // (`|Q| < ideal_den`) and therefore contributed zero to sigma -- UD-2's
  // own visibility mitigation (T-05-82): a reader can see how much of a
  // reported sigma is "real" jitter versus how many intervals were zeroed
  // as representational rounding.
  std::int64_t sub_tick_intervals = 0;
};
std::optional<JitterSigmaResult> compute_jitter_sigma(std::int64_t ideal_num, std::int64_t ideal_den,
                                                          const std::vector<std::int64_t>& intervals);

}  // namespace detail

// D-09 (05-09-PLAN.md, TIME-06): the priming resolver -- a SHARED
// probe-level primitive whose designed second consumer is Phase 6's
// `audio.priming` (`AUDIO-04`), not a private helper local to
// `av_sync.cpp`. Declared here, in the family header, for exactly that
// reason (05-CHECK-ROSTER.md's own precedent: a primitive another phase is
// planned against lives in the header, not buried in a `.cpp`). Checks the
// packet-level `skip_samples` signal FIRST and falls back to
// `codecpar->initial_padding` only when no packet-level signal was
// captured -- 05-RESEARCH.md's own empirically-verified finding against
// the linked FFmpeg 8.1 is that MP4's `codecpar->initial_padding` is ZERO
// while its first AAC packet's own side data carries the real value
// (`start_skip=1024`); a resolver that checked `initial_padding` FIRST
// would silently report every MP4 as `priming: unknown` while the signal
// is present and free. `Source` is OPEN TO EXTENSION without renaming its
// existing members -- Phase 6 adds the container-mechanism tier (MP4
// `elst` / iTunSMPB / MKV `CodecDelay`) as further fallback arms, never a
// second, independently-written resolver.
struct PrimingResult {
  enum class Source : std::uint8_t {
    skip_samples,
    initial_padding,
    unknown,
  };
  Source source = Source::unknown;
  std::int64_t samples = 0;
};

// `first_packet_skip_samples`/`codecpar_initial_padding` both follow the
// "0 if absent" convention `StreamPacketScan::first_packet_skip_samples`'s
// own caller resolves via `.value_or(0)` before this call -- resolve_priming
// itself stays a pure, allocation-free function over two plain integers so
// it is trivially unit-testable without a `StreamPacketScan` on hand.
PrimingResult resolve_priming(std::int64_t first_packet_skip_samples, std::int64_t codecpar_initial_padding);

// timeline.av_offset (05-09-PLAN.md, TIME-06/TIME-09/TIME-10, D-09/D-10/
// D-11): the signed offset between the first audible sample (audio-side
// priming-adjusted per resolve_priming above) and the first visible frame
// of the primary video stream, positive meaning audio late (doc 04 section
// 2's own sign convention). D-10's stored shape: the RAW offset (computed
// WITHOUT the priming adjustment), the ADJUSTED offset (computed WITH it --
// identical to raw whenever this side's own priming is `unknown`, since
// `resolve_priming` reports zero samples in that case) and a structured
// `priming` evidence object (`state`/`source`/`samples`) both ride in THIS
// measurement's own evidence; `comparison_basis` records THIS SIDE's own
// preference (`"adjusted"` when its priming is known, `"raw"` when it is
// not) -- the generic, evidence-shape-driven override in
// `src/compare/tol.cpp` (Rule 2 addition, not in this plan's own declared
// `files_modified`: D-10 cannot be satisfied without it) reads BOTH sides'
// own `comparison_basis` and only swaps the compared magnitude to
// `adjusted_offset_ms` when EVERY side agrees -- raw-to-raw whenever
// either side's priming is unknown, exactly D-10's own rule, decided once,
// generically, never re-derived per check. D-11: unknown priming is never
// a reason to soften severity or widen tolerance -- the check gates at its
// registered `"5ms,20ms"` severity regardless of `comparison_basis`.
//
// Primary-stream selection (`detail::primary_video_stream`): the first
// video-scoped stream, by array (AVStream) order -- `05-CHECK-ROSTER.md`'s
// own Discretion resolution ("not an attached picture") is declared but
// not yet wired (no `StreamInfo` field exposes the disposition flag within
// this plan's own file scope; no fixture in this plan's corpus carries an
// attached-picture stream, so this is a scoping note, not an observed
// gap). One measurement per audio stream, scoped to that audio stream. No
// audio stream, or no video stream, at all: `skipped:insufficient_data`
// rather than silence -- `skipped != pass` is load-bearing, and a
// completely silent check on an audio-less or video-less input is
// indistinguishable from "the check never ran".
//
// `required_passes = {Pass::demux_header, Pass::packet_scan}`, `scope =
// ContainerFamily::other` (every container). Skip-reason priority:
// `partial_scan` (Phase 3 D-02, ahead of everything), then
// `insufficient_data` for every "nothing to measure" case (no audio, no
// video, or any checked-arithmetic overflow in the offset computation,
// T-05-01-style).
const AnalyzerSpec& timeline_av_sync_analyzer();

namespace detail {

// The first video-scoped stream's own array (AVStream) index, by position
// -- `scopes[i]` is `std::nullopt` for a non-scoped (attachment) stream,
// mirroring `compute_stream_scopes`'s own per-file-copy convention already
// established in `start_duration.cpp`/`monotonic.cpp`. Returns
// `std::nullopt` when no video-scoped stream exists at all.
std::optional<std::size_t> primary_video_stream(std::span<const std::optional<Scope>> scopes);

// 05-14-PLAN.md (Gap 3, TIME-06/TIME-09, 05-VERIFICATION.md's own SC4
// entry): converts a priming SAMPLE count into TICK count in the stream's
// own native timebase `tb`, via its sample rate -- `ticks = samples *
// tb.den / (sample_rate * tb.num)`, rounded to the NEAREST integer with
// ties away from zero (the same rounding `av_rescale_q` applies by
// default, `AV_ROUND_NEAR_INF`). Before this function existed,
// `run_timeline_av_sync` added a priming sample count directly to
// native-timebase ticks, silently correct only when a container's
// demuxed audio timebase happens to equal its sample rate (true for
// MP4-muxed AAC, tb == {1, sample_rate}; false for Matroska's mandated 1
// ms timebase, where 1024 samples at 44100 Hz is 23 ticks, not 1024).
//
// Returns std::nullopt -- never a fabricated or silently-truncated
// adjustment -- when `samples` is negative, when `sample_rate`, `tb.num`
// or `tb.den` is zero or below, or when any product this derivation
// forms overflows `int64_t`; the caller (this file's own
// `run_timeline_av_sync`) treats every std::nullopt the same way:
// `comparison_basis` falls back to `"raw"` for that side, exactly D-10's
// existing "priming unknown" fallback, never a softened severity (D-11).
// 0 samples always returns 0 -- checked arithmetic only, no floating
// point anywhere in this derivation (PROJECT.md's rational-everywhere
// constraint).
std::optional<std::int64_t> priming_samples_to_ticks(std::int64_t samples, std::int64_t sample_rate, Rational tb);

}  // namespace detail

// One stream's own sorted, valid-pts view (for the video-side binary
// search in av_sync.cpp) PLUS its own COVERED duration -- from the first
// packet's presentation START to the LAST packet's presentation END
// (start + duration), never "start to start". See av_sync.cpp's own
// `sorted_pts_with_span` doc comment for the full worked finding this
// span-measurement convention is built on. Declared here (05-14-PLAN.md,
// Gap 6, CR-01/WR-01) so `tests/unit/test_av_sync.cpp` can drive
// `detail::sorted_pts_with_span` directly against hand-built
// `PacketRecord` vectors, the same `detail::`-exposure convention every
// sibling pure helper in this phase already follows (WR-01's own
// observation: this was previously the one exception, anonymous-
// namespace-only, which is almost certainly why the CR-01 single-packet
// underflow was never caught by a unit test).
struct PtsSpan {
  std::vector<std::int64_t> pts;        // ascending, valid (non-sentinel) pts, presentation order.
  std::vector<std::int64_t> durations;  // parallel to `pts` -- each entry's own EFFECTIVE duration
                                         // (declared `PacketRecord::duration` when > 0, else the
                                         // neighbouring interval -- see sorted_pts_with_span's own
                                         // doc comment in av_sync.cpp for the full neighbour rule).
  std::int64_t span_ticks = 0;          // (last pts + last's own effective duration) - first pts.
  bool has_span = false;                // pts.size() >= 2 AND span_ticks computed overflow-free and > 0.
  std::int64_t nominal_duration_ticks = 0;  // MEDIAN of `durations` -- see av_sync.cpp's own doc
                                             // comment for the full containment-cap rationale.
};

namespace detail {

// CR-01/WR-01 (05-REVIEW.md, 05-14-PLAN.md Gap 6): builds `PtsSpan` from a
// stream's own raw packets, sorted by presentation time. On a single-entry
// stream (or any entry with no reachable neighbour) with a non-positive
// declared duration, the effective duration is 0 -- never an out-of-bounds
// neighbour read. See av_sync.cpp's own definition for the full worked
// derivation (including the containment-cap `nominal_duration_ticks`
// computation and the sort/median cost bounds).
PtsSpan sorted_pts_with_span(std::span<const PacketRecord> packets);

}  // namespace detail

// --- 05-10-PLAN.md (TIME-07/TIME-08), doc 04 section 3: the flagship A/V
// drift algorithm, as a pure, unit-testable function. -----------------------

// K = 32, doc 04 section 3's own fixed checkpoint count (D-08: a detection
// constant, not a knob, in v1). Named so no bare literal appears at any use
// site.
inline constexpr int kDriftCheckpointCount = 32;

// The 2 ms epsilon doc 04 section 3.4's own classification rule and D-07's
// dual gate both cite -- the SAME constant, never two independently-tuned
// numbers.
inline constexpr std::int64_t kDriftEpsilonMs = 2;

// "any single residual step > 3x epsilon" -- doc 04 section 3.4, verbatim.
inline constexpr std::int64_t kDriftStepResidualMultiple = 3;

// The fixed rate threshold doc 04 section 3.4's own "|slope| below
// tolerance" clause tests against, expressed as an exact rational (0.2 =
// 1/5) rather than a decimal -- the SAME magnitude 05-CHECK-ROSTER.md
// registers as `timeline.av_drift`'s own compare-time tolerance
// (`"0.2ms/min"`), reused here for fit_drift's OWN single-file
// constant-offset/linear-drift classification rather than a second,
// independently-tuned number (D-08).
inline constexpr std::int64_t kDriftRateEpsilonNumMsPerMin = 1;
inline constexpr std::int64_t kDriftRateEpsilonDenMsPerMin = 5;

// A safety bound on the least-squares fit's own REDUCED denominator
// (05-RESEARCH.md's own worked overflow analysis, extended by this task):
// after `detail::Int128Accum::try_reduce_ratio` narrows the slope to its
// lowest terms, every SUBSEQUENT residual/classification computation
// multiplies that denominator by small, fixed factors (K, 1000*tb.num,
// tb.den) via `detail::checked_mul` -- bounding the reduced denominator to
// one billion keeps every one of those chained multiplications safely
// inside int64_t for any REAL media file (frame durations and sample
// counts are overwhelmingly composite, so the GCD reduction empirically
// brings realistic denominators far below this bound) while still
// degrading honestly (`insufficient_data`, never a wrapped value) on the
// vanishingly unlikely adversarial input that does not reduce enough.
inline constexpr std::int64_t kMaxDriftDenominator = 1'000'000'000;

// One checkpoint's own inputs to fit_drift (doc 04 section 3 steps 1-2):
// `t_v_ticks` (the nearest video frame start) and `offset_ticks`
// (`t_a_aligned - t_v`), BOTH already expressed as exact ticks of ONE
// common, caller-chosen timebase -- fit_drift itself never rescales
// between the video and audio axes; finding the nearest video frame /
// audio sample boundary and rescaling the audio side onto the video's own
// axis is entirely the CALLER's job (05-10-PLAN.md Task 2). This struct is
// fit_drift's pure, timebase-agnostic input shape, which is what makes it
// unit-testable against hand-built trajectories with no real fixture on
// disk.
struct DriftCheckpoint {
  std::int64_t t_v_ticks = 0;
  std::int64_t offset_ticks = 0;
};

// The classified pattern (doc 04 section 3.4), exactly the four spellings
// 05-CHECK-ROSTER.md registers for `timeline.av_drift.pattern`.
enum class DriftPattern : std::uint8_t {
  constant_offset,
  linear_drift,
  step,
  irregular,
};

// fit_drift's own output. `rate_ms_per_min_num/den` is the least-squares
// slope, converted to ms/min, as an EXACT rational (A2: never a
// pre-divided value) -- `rate_ms_per_min_den` is always strictly positive.
// `end_delta_ms`/`residual_max_ms` are already converted to milliseconds
// (the SAME `detail::ticks_to_ms`-style checked_mul/checked_div truncation
// every other ms-unit check in this project already uses); `step_time_ms`
// is populated only when `pattern == step` (the checkpoint's own `t_v`, in
// ms, at which the step was detected).
struct DriftFit {
  std::int64_t rate_ms_per_min_num = 0;
  std::int64_t rate_ms_per_min_den = 1;
  std::int64_t end_delta_ms = 0;
  std::int64_t residual_max_ms = 0;
  DriftPattern pattern = DriftPattern::constant_offset;
  std::optional<std::int64_t> step_time_ms;
};

// fit_drift (05-10-PLAN.md Task 1): doc 04 section 3's algorithm, steps 3-4
// (the checkpoint CONSTRUCTION, steps 1-2, is Task 2's job in
// av_sync.cpp's own run() function). Least-squares line over
// `checkpoints`' own `(t_v_ticks, offset_ticks)` pairs, zero-based at the
// first checkpoint (mathematically free -- the slope is invariant to a
// constant shift in x -- and REQUIRED: 05-RESEARCH.md's own worked
// overflow analysis shows a two-hour 90kHz file's raw ticks are large
// enough to overflow int64_t otherwise), accumulated exclusively via
// `detail::Int128Accum` (never plain int64_t -- see that class's own
// comment for the worked magnitude bound: `K*Sum(x^2)` alone reaches
// roughly 46x INT64_MAX for this ordinary case), and narrowed to an EXACT
// rational slope via `Int128Accum::try_reduce_ratio`'s GCD-based
// reduction. Classification exactly per doc 04 section 3.4, with "stable
// plateaus" made concrete per this plan's own A3 (see av_sync.cpp's own
// implementation comment for the transcription).
//
// Requires at least 2 checkpoints (a line needs two distinct points);
// fewer is an explicit failure, never a degenerate fit. Returns
// std::nullopt when `checkpoints.size() < 2`, when every checkpoint shares
// the identical `t_v_ticks` (no x-variance to fit against), or when ANY
// arithmetic step overflows or fails to narrow anywhere in the fit
// (including the `kMaxDriftDenominator` safety bound above) -- the caller
// maps this to `SkipReason::insufficient_data`, never a wrapped or
// truncated slope (05-RESEARCH.md Pitfall 2, `cadence.cpp`'s own identical
// promise).
std::optional<DriftFit> fit_drift(std::span<const DriftCheckpoint> checkpoints, Rational tb);

// timeline.timecode / timeline.timecode.value (05-11-PLAN.md, TIME-11):
// SMPTE timecode presence and start value from a QuickTime `tmcd` track,
// reachable from `Pass::demux_header` ALONE -- no scan of any kind needed,
// matching `video_color_analyzer()`'s own shape exactly (05-RESEARCH.md
// Pattern 4, empirically re-verified this task: the MOV/MP4 demuxer
// resolves a `tmcd` track's starting timecode during
// `avformat_find_stream_info` itself, publishing it as a plain string
// under `AVStream::metadata["timecode"]` -- `DemuxSession::stream_info`'s
// own `StreamInfo::timecode_metadata` field, never a raw libav read from
// this file). `timeline.timecode` reports the `presence` semantic
// (`"present"` / `Absent{}`); `timeline.timecode.value` reports the exact
// SMPTE string under `exact` -- compared BYTE FOR BYTE, never parsed into
// fields, so the drop-frame punctuation (a semicolon before the frame
// field, confirmed reachable on the no-decode path this task, A1) is part
// of the compared value. Source stream: the first stream (by array order)
// whose `StreamInfo::is_timecode` is true (the `tmcd` codec-tag marker
// DemuxSession already resolves for CONT-09) -- deterministic, mirrors
// `detail::find_tmcd_stream`'s own doc comment in timecode.cpp. A file
// with no such stream reports BOTH ids `Absent{}` (TIME-11's own empty
// edge, must_haves item 5) -- an explicit, comparable absence, never an
// empty string, with `SkipReason::none` (an ordinary, real, permanent
// absence, never `skipped`). Evidence on every emitted measurement
// (present or absent) carries `unreachable_sources` -- 05-RESEARCH.md
// Pattern 4 / Pitfall 6's own source-tree-grep finding that
// `AV_PKT_DATA_S12M_TIMECODE` has no file-demuxer producer in this build
// (only `libavdevice/decklink_dec.cpp`, which `vcpkg.json`'s ffmpeg feature
// list does not link) and MPEG-2 GOP timecode
// (`AV_FRAME_DATA_GOP_TIMECODE`) is populated only on a decoded `AVFrame`
// -- named honestly, in evidence AND in `docs/checks/timeline.timecode.md`,
// never built out as a real code path (05-CHECK-ROSTER.md: no
// separately-triggerable S12M id, since DOC-03 requires a real trigger
// fixture and none can exist for it). `required_passes =
// {Pass::demux_header}` only, `scope = ContainerFamily::other` (every
// container). Scoped `Scope::Kind::global` (one file-level SMPTE origin,
// not a per-stream property -- mirrors `timeline.start`'s own D-03 global
// measurement, `start_duration.cpp`).
const AnalyzerSpec& timeline_timecode_analyzer();

namespace detail {

// Whether `timecode_string` carries the drop-frame punctuation -- a
// semicolon immediately before the frame field (`HH:MM:SS;FF`) rather than
// a colon (`HH:MM:SS:FF`), confirmed empirically this task (05-11-SUMMARY.md,
// Task 1) against the pinned FFmpeg 9.0.1 generator: a drop-frame-rate
// `-timecode` input renders with the semicolon, a non-drop-frame one with
// all colons. A bare substring search is deliberately sufficient -- not a
// full SMPTE grammar parse -- because this flag is EVIDENCE ONLY; the
// COMPARED value (`timeline.timecode.value`) is always the raw byte
// sequence, so a pathological string that happens to carry a stray `;`
// elsewhere cannot corrupt the comparison itself, only this one derived
// evidence field.
bool derive_drop_frame(const std::string& timecode_string);

}  // namespace detail

}  // namespace mediadiff
