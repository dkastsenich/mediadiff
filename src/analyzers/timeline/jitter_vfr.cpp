#include "analyzers/timeline/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "analyzers/timeline/unwrap.h"
#include "probe/cadence.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// 05-08-PLAN.md (TIME-05, D-06): the six fixed fractional bucket boundary
// constants, in ASCENDING cascade order (see this file's own
// classify_vfr_bin below for the evaluation order that makes "at a
// boundary -> lower bucket, one tick past -> higher bucket" hold). Fixed,
// never tunable, for the identical reason probe/cadence.h's own detection
// constants are fixed (D-08): a compared bin LABEL must not vary per
// invocation.
inline constexpr std::int64_t kVfrOneTickToleranceTicks = 1;
inline constexpr std::int64_t kVfrOnePercentNum = 1;
inline constexpr std::int64_t kVfrOnePercentDen = 100;
inline constexpr std::int64_t kVfrTwoXMultiplier = 2;
inline constexpr std::int64_t kVfrThreeXMultiplier = 3;

// The six D-06 bin labels, 05-CHECK-ROSTER.md's own approved spelling --
// named constants rather than string literals scattered per call site.
constexpr const char* kBinOnGrid = "on_grid";
constexpr const char* kBinOneTick = "one_tick";
constexpr const char* kBinOnePercent = "one_percent";
constexpr const char* kBinTwoX = "two_x";
constexpr const char* kBinThreeX = "three_x";
constexpr const char* kBinLonger = "longer";

// 04-17 gap closure (WR-03) precedent (src/analyzers/video/gop.cpp,
// src/analyzers/timeline/start_duration.cpp/monotonic.cpp) -- GCC
// 13.3.0 -O3's flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, specifically on the
// `measurement.value = Absent{};` move-construction below. Confirmed
// needed here since this function's body is byte-for-byte the same shape
// that trips the diagnostic in every sibling timeline analyzer file.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// StreamMediaType -> the Scope::Kind a stream is scoped under -- verbatim
// copy of src/analyzers/timeline/monotonic.cpp's own scope_kind_for_stream
// (this project's established per-file-copy convention, never a shared
// export -- 05-PATTERNS.md's "Per-stream Scope derivation" shared
// pattern).
std::optional<Scope::Kind> scope_kind_for_stream(StreamMediaType type) {
  switch (type) {
    case StreamMediaType::video:
      return Scope::Kind::video;
    case StreamMediaType::audio:
      return Scope::Kind::audio;
    case StreamMediaType::subtitle:
      return Scope::Kind::subtitle;
    case StreamMediaType::data:
    case StreamMediaType::other:
      return Scope::Kind::data;
    case StreamMediaType::attachment:
      return std::nullopt;
  }
  return std::nullopt;
}

// Resolves each PacketScanResult::per_stream entry's own Scope BY INDEX --
// per_stream[i] IS AVStream i (packet_scan.h's own documented contract),
// mirroring monotonic.cpp's compute_stream_scopes verbatim (this
// project's per-file-copy convention, not a shared export).
std::vector<std::optional<Scope>> compute_stream_scopes(const DemuxSession& demux, std::size_t stream_count) {
  std::vector<std::optional<Scope>> scopes;
  scopes.reserve(stream_count);

  int video_rank = 0;
  int audio_rank = 0;
  int subtitle_rank = 0;
  int data_rank = 0;

  for (std::size_t i = 0; i < stream_count; ++i) {
    const std::optional<Scope::Kind> kind = scope_kind_for_stream(demux.stream_info(static_cast<int>(i)).media_type);
    if (!kind.has_value()) {
      scopes.push_back(std::nullopt);
      continue;
    }
    int rank = 0;
    switch (*kind) {
      case Scope::Kind::video:
        rank = video_rank++;
        break;
      case Scope::Kind::audio:
        rank = audio_rank++;
        break;
      case Scope::Kind::subtitle:
        rank = subtitle_rank++;
        break;
      case Scope::Kind::data:
        rank = data_rank++;
        break;
      case Scope::Kind::global:
      case Scope::Kind::program:
        // Unreachable: scope_kind_for_stream never returns these two.
        rank = static_cast<int>(i);
        break;
    }
    scopes.push_back(Scope{*kind, rank});
  }
  return scopes;
}

}  // namespace

namespace detail {

std::optional<std::vector<std::int64_t>> compute_sorted_axis_intervals(std::span<const PacketRecord> packets,
                                                                          CadenceAxis axis) {
  const Axis local_axis = (axis == CadenceAxis::dts) ? Axis::dts : Axis::pts;
  const AxisView view = build_axis_view(packets, local_axis);
  if (view.samples.size() < 2) {
    // Fewer than two usable timestamps means zero INTERVALS to walk --
    // not an error (mirrors timeline.gaps' own "< 2 reconstructed
    // packets" branch, monotonic.cpp): the caller decides what an empty
    // interval list means for its own statistic.
    return std::vector<std::int64_t>{};
  }

  // A LOCAL SORTED COPY, by (value, packet_index) -- never `view` itself,
  // never the caller's own read-order array (mirrors
  // detail::count_pts_duplicates' identical discipline, monotonic.cpp).
  std::vector<AxisSample> sorted(view.samples.begin(), view.samples.end());
  std::sort(sorted.begin(), sorted.end(), [](const AxisSample& a, const AxisSample& b) {
    if (a.value != b.value) {
      return a.value < b.value;
    }
    return a.packet_index < b.packet_index;
  });

  std::vector<std::int64_t> intervals;
  intervals.reserve(sorted.size() - 1);
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    std::int64_t interval = 0;
    if (!checked_sub(sorted[i].value, sorted[i - 1].value, &interval)) {
      return std::nullopt;
    }
    intervals.push_back(interval);
  }
  return intervals;
}

std::optional<std::string> classify_vfr_bin(std::int64_t interval, std::int64_t ideal_num, std::int64_t ideal_den) {
  // interval_den == interval * ideal_den, the cross-multiplied form of
  // `interval` in the SAME (unreduced) denominator as `ideal_num` --
  // every comparison below is therefore a plain integer comparison, never
  // a division.
  std::int64_t interval_den = 0;
  if (!checked_mul(interval, ideal_den, &interval_den)) {
    return std::nullopt;
  }

  std::int64_t diff = 0;
  if (!checked_sub(interval_den, ideal_num, &diff)) {
    return std::nullopt;
  }
  std::int64_t abs_diff = diff;
  if (diff < 0) {
    if (!checked_negate(diff, &abs_diff)) {
      return std::nullopt;
    }
  }

  // UD-2 (05-19-PLAN.md, WINDOWS #28): a deviation STRICTLY BELOW one
  // tick of the stream's own timebase (`ideal_den`) is representational
  // rounding -- a non-exactly-representable ideal interval (e.g. NTSC's
  // 1001/30000s period at Matroska's 1ms timebase) can never land exactly
  // on its own grid point, and that residual is not real jitter. Strict
  // "<", not "<=": a deviation of EXACTLY one tick is real and lands in
  // one_tick below (TIME-05/adjacency). For an integer-ideal stream
  // (`ideal_num` an exact multiple of `ideal_den`), `abs_diff` is itself
  // always a multiple of `ideal_den`, so this reduces to the
  // pre-quantization `abs_diff == 0` test exactly -- every such stream
  // bins identically to before this rule.
  if (abs_diff < ideal_den) {
    return std::string(kBinOnGrid);
  }

  // "at least one tick, below two ticks": ideal_den <= |diff| <
  // 2*ideal_den -- the cross-multiplied form of `kVfrOneTickToleranceTicks
  // <= |interval - ideal| < 2*kVfrOneTickToleranceTicks`.
  std::int64_t two_tick_bound = 0;
  if (!checked_mul(std::int64_t{2} * kVfrOneTickToleranceTicks, ideal_den, &two_tick_bound)) {
    return std::nullopt;
  }
  if (abs_diff < two_tick_bound) {
    return std::string(kBinOneTick);
  }

  // "within one percent" (relative, both directions): |diff| / ideal_num
  // <= kVfrOnePercentNum / kVfrOnePercentDen -- cross-multiplied to avoid
  // the division: |diff| * kVfrOnePercentDen <= kVfrOnePercentNum *
  // ideal_num.
  std::int64_t pct_lhs = 0;
  if (!checked_mul(abs_diff, kVfrOnePercentDen, &pct_lhs)) {
    return std::nullopt;
  }
  std::int64_t pct_rhs = 0;
  if (!checked_mul(kVfrOnePercentNum, ideal_num, &pct_rhs)) {
    return std::nullopt;
  }
  if (pct_lhs <= pct_rhs) {
    return std::string(kBinOnePercent);
  }

  // two_x/three_x are evaluated ONLY when the interval is genuinely
  // LONGER than the ideal -- a SHORT interval beyond one percent (a
  // duplicate or near-zero interval, the province of
  // timeline.pts_unique/timeline.gaps, not this check) falls straight
  // through to the "longer" catch-all below rather than being
  // misclassified as a lengthened one.
  if (interval_den > ideal_num) {
    std::int64_t two_x_bound = 0;
    if (!checked_mul(kVfrTwoXMultiplier, ideal_num, &two_x_bound)) {
      return std::nullopt;
    }
    if (interval_den <= two_x_bound) {
      return std::string(kBinTwoX);
    }

    std::int64_t three_x_bound = 0;
    if (!checked_mul(kVfrThreeXMultiplier, ideal_num, &three_x_bound)) {
      return std::nullopt;
    }
    if (interval_den <= three_x_bound) {
      return std::string(kBinThreeX);
    }
  }

  return std::string(kBinLonger);
}

std::optional<JitterSigmaResult> compute_jitter_sigma(std::int64_t ideal_num, std::int64_t ideal_den,
                                                          const std::vector<std::int64_t>& intervals) {
  if (intervals.empty()) {
    return std::nullopt;
  }

  constexpr std::int64_t kScale = std::int64_t{1} << kJitterSigmaFixedShift;

  Int128Accum sum_sq;
  std::int64_t max_abs_deviation_fixed = 0;
  std::int64_t sub_tick_intervals = 0;

  for (const std::int64_t interval : intervals) {
    // Q = interval*ideal_den - ideal_num, the SAME cross-multiplied
    // deviation classify_vfr_bin above computes -- both this function and
    // that one measure deviation against the identical unreduced ideal
    // (05-19-PLAN.md's own key_links).
    std::int64_t interval_den = 0;
    if (!checked_mul(interval, ideal_den, &interval_den)) {
      return std::nullopt;
    }
    std::int64_t diff = 0;
    if (!checked_sub(interval_den, ideal_num, &diff)) {
      return std::nullopt;
    }
    std::int64_t abs_diff = diff;
    if (diff < 0) {
      if (!checked_negate(diff, &abs_diff)) {
        return std::nullopt;
      }
    }

    std::int64_t d_fixed = 0;
    if (abs_diff < ideal_den) {
      // UD-2/A1: sub-tick deviation contributes EXACTLY ZERO to sigma --
      // representational rounding, never real jitter.
      ++sub_tick_intervals;
    } else {
      // d_fixed = (|Q| / ideal_den) * scale + round_half_even((|Q| %
      // ideal_den) * scale / ideal_den) -- an EXACT integer division for
      // the whole-tick part, and a round-half-to-even integer division
      // for the sub-tick remainder's own fixed-point fraction. A1: the
      // deviation's FULL magnitude enters here (never zeroed or
      // truncated once at or past one tick).
      const std::int64_t whole_ticks = abs_diff / ideal_den;
      const std::int64_t remainder = abs_diff % ideal_den;

      std::int64_t whole_scaled = 0;
      if (!checked_mul(whole_ticks, kScale, &whole_scaled)) {
        return std::nullopt;
      }

      std::int64_t remainder_scaled = 0;
      if (!checked_mul(remainder, kScale, &remainder_scaled)) {
        return std::nullopt;
      }
      const std::int64_t frac_floor = remainder_scaled / ideal_den;
      const std::int64_t frac_remainder = remainder_scaled % ideal_den;
      std::int64_t twice_frac_remainder = 0;
      if (!checked_mul(std::int64_t{2}, frac_remainder, &twice_frac_remainder)) {
        return std::nullopt;
      }
      // Round-half-to-even: strictly past the halfway point rounds up;
      // exactly at the halfway point rounds to the EVEN candidate (never
      // always-up, which would bias sigma high across many boundary-
      // sitting deviations).
      std::int64_t frac_rounded = frac_floor;
      if (twice_frac_remainder > ideal_den ||
          (twice_frac_remainder == ideal_den && (frac_floor % 2) != 0)) {
        if (!checked_add(frac_floor, 1, &frac_rounded)) {
          return std::nullopt;
        }
      }

      if (!checked_add(whole_scaled, frac_rounded, &d_fixed)) {
        return std::nullopt;
      }
    }

    if (d_fixed > max_abs_deviation_fixed) {
      max_abs_deviation_fixed = d_fixed;
    }

    // (deviation * scale)^2 accumulated directly -- see this function's
    // own declaration comment in analyzers.h for why scaling happens
    // before squaring.
    sum_sq.add_product(d_fixed, d_fixed);
  }

  std::int64_t sum_sq_i64 = 0;
  if (!sum_sq.try_narrow(&sum_sq_i64)) {
    // T-05-34: a crafted interval distribution overflowing the sum of
    // squared deviations -- the narrowing is range-checked and a refusal
    // yields insufficient_data at the caller, never a wrapped sigma.
    return std::nullopt;
  }

  const std::int64_t n = static_cast<std::int64_t>(intervals.size());
  std::int64_t mean_scaled_sq = 0;
  if (!checked_div(sum_sq_i64, n, &mean_scaled_sq)) {
    return std::nullopt;
  }

  std::int64_t sigma_fixed = 0;
  if (!isqrt_i64(mean_scaled_sq, &sigma_fixed)) {
    return std::nullopt;
  }

  return JitterSigmaResult{sigma_fixed, max_abs_deviation_fixed, n, sub_tick_intervals};
}

}  // namespace detail

namespace {

// Converts one native-tick magnitude to an exact millisecond RationalValue
// WITHOUT truncating it to a whole ms (unlike
// src/analyzers/timeline/start_duration.cpp's own detail::ticks_to_ms,
// which deliberately truncates via checked_div for a raw PTS/duration
// value) -- sigma's own sub-tick fixed-point precision (denominator
// `2^kJitterSigmaFixedShift`) would be destroyed by that truncation
// before ever reaching the compared Value. `ticks_num`/`ticks_den`
// together express the tick-domain magnitude as an exact fraction (sigma
// itself is `ticks_num/ticks_den` ticks); the result is GCD-reduced so
// the same true value always renders as the identical canonical num/den
// pair (byte-identical --json across runs). Returns std::nullopt on any
// checked-arithmetic overflow.
std::optional<RationalValue> exact_ticks_to_ms(std::int64_t ticks_num, std::int64_t ticks_den, Rational tb) {
  std::int64_t num = 0;
  if (!detail::checked_mul(ticks_num, tb.num, &num) || !detail::checked_mul(num, 1000, &num)) {
    return std::nullopt;
  }
  std::int64_t den = 0;
  if (!detail::checked_mul(ticks_den, tb.den, &den)) {
    return std::nullopt;
  }
  if (den <= 0) {
    return std::nullopt;
  }
  const std::int64_t divisor = std::gcd(num < 0 ? -num : num, den);
  if (divisor > 1) {
    num /= divisor;
    den /= divisor;
  }
  return RationalValue{num, den, Rational{1, 1}};
}

// timeline.jitter for ONE stream, CFR-only (VFR skips before this is ever
// called). 05-19-PLAN.md (UD-2, WINDOWS #28): the deviation reference is
// now `cadence`'s own EXACT ideal interval
// (`ideal_interval_num`/`ideal_interval_den`, D-05's unreduced span/count
// rational) rather than the timebase-bound `mode_interval_ticks` -- the
// same reference `timeline.vfr_profile` below already binned against, so
// both checks now agree on what "on grid" means. `intervals` is this
// file's own axis-sorted interval list (compute_sorted_axis_intervals
// above), the SAME list timeline.vfr_profile below also consumes -- ONE
// walk, two statistics, never two tallies.
void emit_jitter(const Cadence& cadence, const std::vector<std::int64_t>& intervals, Rational tb, Scope scope,
                  Fingerprint& fp) {
  const std::optional<detail::JitterSigmaResult> sigma_result =
      detail::compute_jitter_sigma(cadence.ideal_interval_num, cadence.ideal_interval_den, intervals);
  if (!sigma_result.has_value()) {
    push_skip(CheckId::timeline_jitter, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const std::int64_t scale = std::int64_t{1} << kJitterSigmaFixedShift;
  const std::optional<RationalValue> sigma_ms = exact_ticks_to_ms(sigma_result->sigma_fixed_numerator, scale, tb);
  if (!sigma_ms.has_value()) {
    push_skip(CheckId::timeline_jitter, scope, SkipReason::insufficient_data, fp);
    return;
  }
  // max_abs_deviation_ms: evidence-only, but `max_abs_deviation_fixed` is
  // now a FIXED-POINT magnitude (scale `2^kJitterSigmaFixedShift`), not a
  // bare tick count -- exact_ticks_to_ms (the SAME helper sigma_ms uses
  // above), never start_duration.cpp's whole-tick-truncating
  // detail::ticks_to_ms, or this sub-tick precision would be destroyed.
  const std::optional<RationalValue> max_abs_deviation_ms =
      exact_ticks_to_ms(sigma_result->max_abs_deviation_fixed, scale, tb);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_jitter);
  measurement.scope = scope;
  measurement.value = *sigma_ms;
  nlohmann::ordered_json evidence{
      {"nominal_interval_ticks", cadence.mode_interval_ticks},
      {"cadence_class", "cfr"},
      {"considered_intervals", sigma_result->considered_intervals},
      {"sigma_scale", scale},
      {"deviation_reference", "ideal"},
      {"ideal_interval_num", cadence.ideal_interval_num},
      {"ideal_interval_den", cadence.ideal_interval_den},
      {"sub_tick_intervals", sigma_result->sub_tick_intervals},
  };
  if (max_abs_deviation_ms.has_value()) {
    evidence["max_abs_deviation_ms"] = nlohmann::ordered_json{{"num", max_abs_deviation_ms->num},
                                                                 {"den", max_abs_deviation_ms->den}};
  }
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// timeline.vfr_profile for ONE stream -- runs REGARDLESS of CFR/VFR
// (unlike timeline.jitter, which skips entirely on VFR): the histogram
// itself is exactly what tells a reader the stream is not constant-rate,
// mirroring video.frame_rate.measured's own "a VFR classification does
// not skip this check" precedent (docs/checks/video.frame_rate.measured.md).
void emit_vfr_profile(const Cadence& cadence, const std::vector<std::int64_t>& intervals, Scope scope,
                       Fingerprint& fp) {
  std::map<std::string, std::int64_t> counts{
      {kBinOnGrid, 0}, {kBinOneTick, 0}, {kBinOnePercent, 0}, {kBinTwoX, 0}, {kBinThreeX, 0}, {kBinLonger, 0},
  };
  for (const std::int64_t interval : intervals) {
    const std::optional<std::string> bin =
        detail::classify_vfr_bin(interval, cadence.ideal_interval_num, cadence.ideal_interval_den);
    if (!bin.has_value()) {
      push_skip(CheckId::timeline_vfr_profile, scope, SkipReason::insufficient_data, fp);
      return;
    }
    ++counts[*bin];
  }

  // Deterministic, fixed iteration order -- a std::map is already sorted
  // by key (TRUST-05), mirroring src/analyzers/video/frame_types.cpp's
  // own histogram_from_counts convention exactly (this project's
  // per-file-copy convention for this shape, not a shared export).
  Histogram histogram;
  histogram.bins.reserve(counts.size());
  for (const auto& [name, count] : counts) {
    histogram.bins.emplace_back(name, count);
  }

  // sub_tick_intervals (05-19-PLAN.md, UD-2): the on_grid bucket's own
  // count -- on_grid now MEANS "deviation strictly below one tick"
  // (classify_vfr_bin's own new rule), so this is a duplicate read of
  // that same count, surfaced as its own named key for visibility
  // (T-05-82) rather than requiring a reader to infer it from the
  // histogram's on_grid bin.
  const std::int64_t sub_tick_intervals = counts.at(kBinOnGrid);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_vfr_profile);
  measurement.scope = scope;
  measurement.value = std::move(histogram);
  measurement.evidence = nlohmann::ordered_json{
      {"nominal_interval_ticks", cadence.mode_interval_ticks},
      {"ideal_interval_num", cadence.ideal_interval_num},
      {"ideal_interval_den", cadence.ideal_interval_den},
      {"cadence_class", cadence.klass == CadenceClass::cfr ? "cfr" : "vfr"},
      {"considered_intervals", static_cast<std::int64_t>(intervals.size())},
      {"sub_tick_intervals", sub_tick_intervals},
  };
  fp.measurements.push_back(std::move(measurement));
}

// timeline_jitter_vfr_analyzer's own run(): every timestamped stream
// EXCEPT Scope::Kind::subtitle (mirrors timeline_monotonic_analyzer's own
// scope decision) gets exactly one Measurement per id. derive_cadence
// (probe/cadence.h, D-05/D-06's shared primitive) is called ONCE per
// stream and its result -- never a second cadence or CFR/VFR
// classification -- decides both `timeline.jitter`'s skip-on-VFR branch
// and `timeline.vfr_profile`'s grid-relative bin reference.
void run_timeline_jitter_vfr(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  // 05-18-PLAN.md (Gap 2, TIME-02): every timestamp read below goes through
  // the ONE promoted `TimelinePacketView` per stream (05-16's
  // assumption-delta `promote` decision) -- never `StreamPacketScan::
  // packets` directly. Per-stream builder is sufficient here (unlike
  // av_sync.cpp's cross-stream comparison): jitter/vfr_profile are
  // computed independently per stream, with no cross-stream epoch
  // alignment needed. `is_ts` matches every sibling timeline analyzer's
  // own established pattern exactly.
  const bool is_ts = container_family_from_format_name(demux.format_name()) == ContainerFamily::ts;
  const std::vector<TimelinePacketView> views = make_timeline_packet_views(packet_scan, is_ts);

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind == Scope::Kind::subtitle) {
      continue;
    }
    if (i >= packet_scan.per_stream.size()) {
      // Defensive only -- packet_scan.per_stream is sized from
      // AVFormatContext::nb_streams, matching `scopes`' own construction.
      continue;
    }
    const StreamPacketScan& stream_scan = packet_scan.per_stream[i];
    const Scope scope = *scopes[i];

    if (packet_scan.partial || stream_scan.partial) {
      // D-02: a sigma/histogram computed from a truncated sweep is a
      // confidently wrong number -- ahead of every other skip reason,
      // matching every sibling timeline analyzer's own priority order.
      push_skip(CheckId::timeline_jitter, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::timeline_vfr_profile, scope, SkipReason::partial_scan, fp);
      continue;
    }

    if (i >= views.size() || views[i].overflowed()) {
      // T-05-71: this stream's own unwrap could not complete without an
      // int64 overflow -- both ids skip for the SAME reason, following
      // this file's own established "both ids skip together" rule.
      push_skip(CheckId::timeline_jitter, scope, SkipReason::insufficient_data, fp);
      push_skip(CheckId::timeline_vfr_profile, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    const std::span<const PacketRecord> stream_packets = views[i].packets();
    const Cadence cadence = derive_cadence(stream_packets, stream_scan.tb);

    if (cadence.status == CadenceStatus::no_timing_data) {
      push_skip(CheckId::timeline_jitter, scope, SkipReason::no_timing_data, fp);
      push_skip(CheckId::timeline_vfr_profile, scope, SkipReason::no_timing_data, fp);
      continue;
    }
    if (cadence.status == CadenceStatus::insufficient_data) {
      push_skip(CheckId::timeline_jitter, scope, SkipReason::insufficient_data, fp);
      push_skip(CheckId::timeline_vfr_profile, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    const std::optional<std::vector<std::int64_t>> intervals =
        detail::compute_sorted_axis_intervals(stream_packets, cadence.axis);
    if (!intervals.has_value()) {
      push_skip(CheckId::timeline_jitter, scope, SkipReason::insufficient_data, fp);
      push_skip(CheckId::timeline_vfr_profile, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    if (cadence.klass == CadenceClass::vfr) {
      // ROADMAP SC3: a VFR stream skips timeline.jitter (never a sigma
      // computed over a distribution with no nominal) but STILL reports
      // a real timeline.vfr_profile histogram.
      push_skip(CheckId::timeline_jitter, scope, SkipReason::vfr, fp);
    } else {
      emit_jitter(cadence, *intervals, stream_scan.tb, scope, fp);
    }
    emit_vfr_profile(cadence, *intervals, scope, fp);
  }
}

}  // namespace

const AnalyzerSpec& timeline_jitter_vfr_analyzer() {
  static const AnalyzerSpec spec{"timeline_jitter_vfr", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_jitter_vfr};
  return spec;
}

}  // namespace mediadiff
