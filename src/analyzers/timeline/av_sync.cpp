#include "analyzers/timeline/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

// timeline.av_offset (05-09-PLAN.md, TIME-06/TIME-09/TIME-10, D-09/D-10/
// D-11): the signed offset between the first audible sample and the first
// visible frame of the primary video stream -- see this analyzer's own
// declaration in analyzers.h for the full rationale. This file owns:
// resolve_priming (the shared priming resolver Phase 6 extends),
// detail::primary_video_stream, and the analyzer itself.
namespace mediadiff {

namespace {

// StreamMediaType -> Scope::Kind -- verbatim copy of
// start_duration.cpp/monotonic.cpp's own scope_kind_for_stream (this
// project's per-file-copy convention, never a shared export).
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void push_skip(CheckId id, Scope scope, SkipReason reason, const nlohmann::ordered_json& reason_evidence,
               Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = reason_evidence;
  fp.measurements.push_back(std::move(measurement));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// resolve_priming's own Source enumerator -> its canonical lowercase
// string spelling -- the SAME three spellings `priming.source` publishes
// in evidence and `comparison_basis`'s own "adjusted"/"raw" pair reads.
std::string_view priming_source_to_string(PrimingResult::Source source) {
  switch (source) {
    case PrimingResult::Source::skip_samples:
      return "skip_samples";
    case PrimingResult::Source::initial_padding:
      return "initial_padding";
    case PrimingResult::Source::unknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace

PrimingResult resolve_priming(std::int64_t first_packet_skip_samples, std::int64_t codecpar_initial_padding) {
  // Checked FIRST, per 05-RESEARCH.md's own empirically-verified finding:
  // MP4's codecpar->initial_padding is ZERO while its first AAC packet's
  // own side data carries the real value -- an initial_padding-first
  // resolver would silently report every MP4 as priming: unknown.
  if (first_packet_skip_samples > 0) {
    return PrimingResult{PrimingResult::Source::skip_samples, first_packet_skip_samples};
  }
  if (codecpar_initial_padding > 0) {
    return PrimingResult{PrimingResult::Source::initial_padding, codecpar_initial_padding};
  }
  return PrimingResult{PrimingResult::Source::unknown, 0};
}

namespace detail {

std::optional<std::size_t> primary_video_stream(std::span<const std::optional<Scope>> scopes) {
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (scopes[i].has_value() && scopes[i]->kind == Scope::Kind::video) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace detail

namespace {

// timeline_av_sync_analyzer's own run(): D-09's priming resolution, D-10's
// dual raw/adjusted storage and D-11's unsoftened severity, all over the
// shared PacketScan array -- never a second sweep, never a decode. See
// this check's own declaration in analyzers.h for the full skip-reason
// priority and evidence-shape rationale.
void run_timeline_av_sync(const ProbeResults& results, Fingerprint& fp) {
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
  const Scope global_scope{Scope::Kind::global, 0};

  std::vector<std::size_t> audio_indices;
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (scopes[i].has_value() && scopes[i]->kind == Scope::Kind::audio) {
      audio_indices.push_back(i);
    }
  }

  if (packet_scan.partial) {
    // D-02 (Phase 3): a value derived from a truncated sweep is a
    // confidently wrong number -- ahead of every other reason.
    const nlohmann::ordered_json reason_evidence{{"reason", "partial_scan"}};
    if (audio_indices.empty()) {
      push_skip(CheckId::timeline_av_offset, global_scope, SkipReason::partial_scan, reason_evidence, fp);
    } else {
      for (std::size_t idx : audio_indices) {
        push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::partial_scan, reason_evidence, fp);
      }
    }
    return;
  }

  if (audio_indices.empty()) {
    // skipped != pass is load-bearing -- an audio-less input still reports
    // this check ran and explicitly found nothing to measure, never
    // silence.
    push_skip(CheckId::timeline_av_offset, global_scope, SkipReason::insufficient_data,
               nlohmann::ordered_json{{"reason", "no_audio_stream"}}, fp);
    return;
  }

  const std::optional<std::size_t> primary_video = detail::primary_video_stream(scopes);
  if (!primary_video.has_value()) {
    for (std::size_t idx : audio_indices) {
      push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_video_stream"}}, fp);
    }
    return;
  }

  const StreamPacketScan& video_stream = packet_scan.per_stream[*primary_video];
  const std::optional<std::int64_t> video_first_pts_ticks =
      detail::first_presented_pts(std::span<const PacketRecord>(video_stream.packets));
  const std::optional<RationalValue> video_first_pts_ms =
      video_first_pts_ticks.has_value() ? detail::ticks_to_ms(*video_first_pts_ticks, video_stream.tb) : std::nullopt;

  if (!video_first_pts_ms.has_value()) {
    for (std::size_t idx : audio_indices) {
      push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_video_timing_data"}}, fp);
    }
    return;
  }

  for (std::size_t audio_idx : audio_indices) {
    const Scope scope = *scopes[audio_idx];
    const StreamPacketScan& audio_stream = packet_scan.per_stream[audio_idx];

    const std::optional<std::int64_t> audio_first_pts_ticks =
        detail::first_presented_pts(std::span<const PacketRecord>(audio_stream.packets));
    if (!audio_first_pts_ticks.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_audio_timing_data"}}, fp);
      continue;
    }

    // D-09: packet-level skip_samples first, codecpar->initial_padding as
    // fallback -- see resolve_priming's own doc comment above.
    const PrimingResult priming =
        resolve_priming(audio_stream.first_packet_skip_samples.value_or(0), audio_stream.initial_padding);

    // The first audible sample: the first packet's PRESENTATION time plus
    // its resolved priming sample count, in native ticks -- composes
    // correctly with libav's own edit-list application rather than
    // double-subtracting it (05-RESEARCH.md Pitfall 5). When priming is
    // `unknown`, `priming.samples == 0`, so the adjusted tick value is
    // IDENTICAL to the raw one -- no special-casing needed for D-10's own
    // "adjusted_offset_ms equal to raw_offset_ms" requirement.
    std::int64_t adjusted_audio_ticks = 0;
    if (!detail::checked_add(*audio_first_pts_ticks, priming.samples, &adjusted_audio_ticks)) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "priming_overflow"}}, fp);
      continue;
    }

    const std::optional<RationalValue> audio_raw_ms = detail::ticks_to_ms(*audio_first_pts_ticks, audio_stream.tb);
    const std::optional<RationalValue> audio_adjusted_ms = detail::ticks_to_ms(adjusted_audio_ticks, audio_stream.tb);
    if (!audio_raw_ms.has_value() || !audio_adjusted_ms.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "rescale_overflow"}}, fp);
      continue;
    }

    const std::optional<RationalValue> raw_offset_ms = detail::subtract_ms(*audio_raw_ms, *video_first_pts_ms);
    const std::optional<RationalValue> adjusted_offset_ms = detail::subtract_ms(*audio_adjusted_ms, *video_first_pts_ms);
    if (!raw_offset_ms.has_value() || !adjusted_offset_ms.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "delta_overflow"}}, fp);
      continue;
    }

    // D-10: THIS SIDE's own basis preference -- "adjusted" only when this
    // side's own priming is known. The generic, evidence-shape-driven
    // override in src/compare/tol.cpp only swaps to the adjusted
    // magnitude when BOTH sides agree; a single side reporting "raw" here
    // is what makes a mixed pair fall back to raw-to-raw (D-10's own
    // TRUST-08 cross-release reasoning).
    const bool priming_known = priming.source != PrimingResult::Source::unknown;
    const std::string_view comparison_basis = priming_known ? "adjusted" : "raw";
    const std::string_view priming_state = priming_known ? "known" : "unknown";

    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_av_offset);
    measurement.scope = scope;
    // D-10: the RAW offset is ALWAYS the stored/compared Measurement::value
    // -- a well-defined magnitude computable from this single side alone,
    // with no cross-file knowledge required. The compare-time override (see
    // src/compare/tol.cpp) is what actually applies D-10's "adjusted when
    // both sides know it" rule.
    measurement.value = *raw_offset_ms;
    measurement.evidence = nlohmann::ordered_json{
        {"raw_offset_ms", raw_offset_ms->num},
        {"adjusted_offset_ms", adjusted_offset_ms->num},
        {"comparison_basis", std::string(comparison_basis)},
        {"priming",
         nlohmann::ordered_json{
             {"state", std::string(priming_state)},
             {"source", std::string(priming_source_to_string(priming.source))},
             {"samples", priming.samples},
         }},
        {"primary_video_stream_index", static_cast<std::int64_t>(*primary_video)},
    };
    fp.measurements.push_back(std::move(measurement));
  }
}

}  // namespace

const AnalyzerSpec& timeline_av_sync_analyzer() {
  static const AnalyzerSpec spec{"timeline_av_sync", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_av_sync};
  return spec;
}

// --- 05-10-PLAN.md Task 1 (TIME-07/TIME-08): fit_drift, doc 04 section 3's
// algorithm as a pure, overflow-safe, float-free function. ------------------
//
// Doc 04 section 3, transcribed verbatim as this function's own normative
// source:
//
//   "Inputs: presentation timelines of the primary video stream and each
//   audio stream (audio priming-adjusted). For K = 32 checkpoints at video
//   timeline fractions k/K:
//   1. t_v(k) = presentation time of the nearest video frame start; t_a(k)
//      = presentation time of the nearest audio sample boundary (packet
//      start + sample-accurate offset within the packet at the stream
//      rate).
//   2. offset(k) = t_a_aligned(k) - t_v(k) where alignment picks the audio
//      time covering the same media position (nearest-sample; audio
//      granularity << 1 ms makes interpolation unnecessary).
//   3. Least-squares line over (t_v(k), offset(k)) -> slope = rate
//      (ms/min), intercept ~= timeline.av_offset.
//   4. Pattern classification: residual max < epsilon (2 ms) ->
//      constant-offset if |slope| below tolerance else linear-drift; any
//      single residual step > 3x epsilon with stable plateaus on both
//      sides -> step (report the step time). Otherwise irregular
//      (rendered with the residual max).
//   5. Cross-file comparison gates on |rate_candidate - rate_baseline| and
//      end-delta difference; the trajectory (K offsets) is stored in the
//      fingerprint so compare against a snapshot retains full fidelity.
//
//   Determinism: integer/rational inputs, fixed K, fixed epsilon =>
//   byte-identical results across platforms -- this check must never
//   itself jitter (idempotence guarantee)."
//
// This function implements steps 3-4 (steps 1-2, the checkpoint
// CONSTRUCTION, are Task 2's job in run_timeline_av_sync's own extension).

namespace {

// A2's own decision (05-10-PLAN.md's flagged_assumptions): zero-base `x`
// at the first checkpoint, accumulate Sum(x)/Sum(y)/Sum(x^2)/Sum(xy) in
// the 128-bit accumulator, and produce the slope as an exact rational
// rather than a pre-divided value.
//
// N and D are computed via ALL K*(K-1)/2 pairwise differences rather than
// the textbook Sum(x)/Sum(x^2) closed form directly -- an algebraic
// identity (verified by this task's own derivation, recorded in
// 05-10-SUMMARY.md): for any K points,
//   K*Sum(xy) - Sum(x)*Sum(y) == Sum_{i<j} (x_i-x_j)*(y_i-y_j)
//   K*Sum(x^2) - (Sum(x))^2   == Sum_{i<j} (x_i-x_j)^2
// Both sides are the SAME closed-form numerator/denominator; the pairwise
// form lets every individual term reach detail::Int128Accum::add_product
// as a single, ALWAYS-safe int64*int64 product (a difference of two
// zero-based ticks is bounded by the file's own span, comfortably within
// int64_t, even when the intermediate closed-form terms Sum(x^2) and
// K*Sum(x^2) individually would not be) -- never a SECOND, independently
// written accumulation of the textbook formula that could drift from this
// one.
struct DriftLine {
  std::int64_t slope_num = 0;
  std::int64_t slope_den = 1;
};

std::optional<DriftLine> fit_line(std::span<const std::int64_t> x, std::span<const std::int64_t> y) {
  const std::size_t count = x.size();
  detail::Int128Accum n_wide;
  detail::Int128Accum d_wide;
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      std::int64_t dx = 0;
      std::int64_t dy = 0;
      if (!detail::checked_sub(x[j], x[i], &dx) || !detail::checked_sub(y[j], y[i], &dy)) {
        return std::nullopt;
      }
      d_wide.add_product(dx, dx);
      n_wide.add_product(dx, dy);
    }
  }
  std::int64_t raw_num = 0;
  std::int64_t raw_den = 0;
  if (!n_wide.try_reduce_ratio(d_wide, &raw_num, &raw_den)) {
    // Covers BOTH a genuine reduction failure and D == 0 (every x_i
    // identical -- no x-variance to fit a line against): try_reduce_ratio
    // itself refuses a zero denominator.
    return std::nullopt;
  }
  if (raw_den > kMaxDriftDenominator) {
    // The safety bound analyzers.h's own comment documents -- refused
    // here rather than risking a downstream checked_mul overflow in the
    // residual/classification arithmetic below.
    return std::nullopt;
  }
  return DriftLine{raw_num, raw_den};
}

// The residual, in MILLISECONDS, of checkpoint `k` from the least-squares
// line: `y_k - (intercept + slope*x_k)`, where `intercept = (Sum_y -
// slope*Sum_x)/K`. Derived (05-10-SUMMARY.md's own worked algebra) as an
// exact fraction with numerator
// `raw_den*(K*y_k - Sum_y) + raw_num*(Sum_x - K*x_k)` scaled by
// `1000*tb.num` and denominator `K*raw_den*tb.den` -- both sides computed
// via detail::Int128Accum::add_product (never a "wide times narrow"
// primitive this class does not have), then reduced and narrowed via
// try_reduce_ratio exactly like the slope itself, and finally truncated to
// a plain int64_t millisecond count via detail::checked_div -- the SAME
// truncating convention `detail::ticks_to_ms` already establishes
// throughout this project. Returns std::nullopt on ANY overflow or
// narrowing failure anywhere in this derivation.
std::optional<std::int64_t> residual_ms(std::int64_t count, std::int64_t sum_x, std::int64_t sum_y,
                                          std::int64_t x_k, std::int64_t y_k, std::int64_t raw_num,
                                          std::int64_t raw_den, std::int64_t scale_tb, Rational tb) {
  std::int64_t k_y_k = 0;
  std::int64_t a_k = 0;
  if (!detail::checked_mul(count, y_k, &k_y_k) || !detail::checked_sub(k_y_k, sum_y, &a_k)) {
    return std::nullopt;
  }
  std::int64_t k_x_k = 0;
  std::int64_t b_k = 0;
  if (!detail::checked_mul(count, x_k, &k_x_k) || !detail::checked_sub(sum_x, k_x_k, &b_k)) {
    return std::nullopt;
  }
  std::int64_t a_k_scaled = 0;
  std::int64_t b_k_scaled = 0;
  if (!detail::checked_mul(a_k, scale_tb, &a_k_scaled) || !detail::checked_mul(b_k, scale_tb, &b_k_scaled)) {
    return std::nullopt;
  }

  detail::Int128Accum wide_num;
  wide_num.add_product(raw_den, a_k_scaled);
  wide_num.add_product(raw_num, b_k_scaled);

  std::int64_t k_tb_den = 0;
  if (!detail::checked_mul(count, tb.den, &k_tb_den)) {
    return std::nullopt;
  }
  detail::Int128Accum wide_den;
  wide_den.add_product(k_tb_den, raw_den);

  std::int64_t reduced_num = 0;
  std::int64_t reduced_den = 0;
  if (!wide_num.try_reduce_ratio(wide_den, &reduced_num, &reduced_den)) {
    return std::nullopt;
  }
  std::int64_t ms = 0;
  if (!detail::checked_div(reduced_num, reduced_den, &ms)) {
    return std::nullopt;
  }
  return ms;
}

std::int64_t abs_i64(std::int64_t v) { return v < 0 ? -v : v; }

}  // namespace

// Task 2's own checkpoint construction (this file's `run_timeline_av_sync`)
// always passes exactly `kDriftCheckpointCount` checkpoints -- doc 04
// section 3's own fixed K, D-08's "detection constant, not a knob" -- but
// this function itself imposes no such requirement: steps 3-4 (the fit
// itself) are independent of step 1-2's construction, so any count >= 2
// produces a defined fit, which is exactly what makes it unit-testable
// against small, hand-built trajectories that are nowhere near K=32.
std::optional<DriftFit> fit_drift(std::span<const DriftCheckpoint> checkpoints, Rational tb) {
  const std::size_t count = checkpoints.size();
  if (count < 2 || tb.num <= 0 || tb.den <= 0) {
    return std::nullopt;
  }

  // Zero-base x at the first checkpoint (A2, required and mathematically
  // free -- see this file's own comment above fit_line).
  std::vector<std::int64_t> x(count);
  std::vector<std::int64_t> y(count);
  const std::int64_t x0 = checkpoints[0].t_v_ticks;
  for (std::size_t i = 0; i < count; ++i) {
    if (!detail::checked_sub(checkpoints[i].t_v_ticks, x0, &x[i])) {
      return std::nullopt;
    }
    y[i] = checkpoints[i].offset_ticks;
  }

  const std::optional<DriftLine> line = fit_line(x, y);
  if (!line.has_value()) {
    return std::nullopt;
  }

  DriftFit fit;
  if (!detail::checked_mul(line->slope_num, 60000, &fit.rate_ms_per_min_num)) {
    return std::nullopt;
  }
  fit.rate_ms_per_min_den = line->slope_den;

  const std::int64_t count_i64 = static_cast<std::int64_t>(count);
  std::int64_t sum_x = 0;
  std::int64_t sum_y = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (!detail::checked_add(sum_x, x[i], &sum_x) || !detail::checked_add(sum_y, y[i], &sum_y)) {
      return std::nullopt;
    }
  }

  std::int64_t scale_tb = 0;
  if (!detail::checked_mul(1000, tb.num, &scale_tb)) {
    return std::nullopt;
  }

  std::vector<std::int64_t> residuals(count);
  std::int64_t residual_max = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const std::optional<std::int64_t> r =
        residual_ms(count_i64, sum_x, sum_y, x[i], y[i], line->slope_num, line->slope_den, scale_tb, tb);
    if (!r.has_value()) {
      return std::nullopt;
    }
    residuals[i] = *r;
    residual_max = std::max(residual_max, abs_i64(*r));
  }
  fit.residual_max_ms = residual_max;

  // "End delta means drift accumulated from start to end" (05-CONTEXT.md
  // D-07): the LAST checkpoint's offset minus the FIRST's, in ticks,
  // converted to ms via the same detail::ticks_to_ms every other ms-unit
  // check in this project already uses.
  std::int64_t end_delta_ticks = 0;
  if (!detail::checked_sub(checkpoints.back().offset_ticks, checkpoints.front().offset_ticks, &end_delta_ticks)) {
    return std::nullopt;
  }
  const std::optional<RationalValue> end_delta_ms_value = detail::ticks_to_ms(end_delta_ticks, tb);
  if (!end_delta_ms_value.has_value()) {
    return std::nullopt;
  }
  fit.end_delta_ms = end_delta_ms_value->num;

  // Doc 04 section 3.4, first branch: residual max < epsilon -> constant
  // vs linear, decided by the SAME fixed 0.2 ms/min threshold
  // 05-CHECK-ROSTER.md registers as this check's own compare-time
  // tolerance (kDriftRateEpsilon{Num,Den}MsPerMin, analyzers.h).
  if (residual_max < kDriftEpsilonMs) {
    std::int64_t lhs = 0;
    std::int64_t rhs = 0;
    const std::int64_t abs_rate_num = abs_i64(fit.rate_ms_per_min_num);
    if (!detail::checked_mul(abs_rate_num, kDriftRateEpsilonDenMsPerMin, &lhs) ||
        !detail::checked_mul(kDriftRateEpsilonNumMsPerMin, fit.rate_ms_per_min_den, &rhs)) {
      return std::nullopt;
    }
    fit.pattern = (lhs < rhs) ? DriftPattern::constant_offset : DriftPattern::linear_drift;
    return fit;
  }

  // Second branch (residual_max >= kDriftEpsilonMs, having failed the first
  // branch's own `<` test above): A3's own concretisation of "any single
  // residual step > kDriftStepResidualMultiple epsilons ... with stable
  // plateaus". Deliberately evaluated over the RAW offset
  // trajectory (each checkpoint's own offset, in ms), never the
  // least-squares FIT's own residuals: a genuine mid-file step, fit by ONE
  // straight line through both plateaus, pulls that line's own slope away
  // from zero (this task's own worked derivation, recorded in
  // 05-10-SUMMARY.md -- a symmetric 3-checkpoints-flat / jump /
  // 3-checkpoints-flat trajectory produces FIT residuals of
  // [+14.3,-11.4,-37.1,+37.1,+11.4,-14.3] ms, NOT two flat groups), so
  // "stable plateaus" tested against FIT residuals would reject every
  // genuine step. The raw offset trajectory itself is what actually forms
  // two flat groups either side of a real step, and is what a reader means
  // by "plateau" in the first place.
  std::vector<std::int64_t> offset_ms(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::optional<RationalValue> value = detail::ticks_to_ms(checkpoints[i].offset_ticks, tb);
    if (!value.has_value()) {
      return std::nullopt;
    }
    offset_ms[i] = value->num;
  }

  std::size_t step_index = 0;
  std::int64_t largest_jump = -1;
  for (std::size_t i = 1; i < count; ++i) {
    const std::int64_t jump = abs_i64(offset_ms[i] - offset_ms[i - 1]);
    if (jump > largest_jump) {
      largest_jump = jump;
      step_index = i;
    }
  }

  const std::int64_t step_threshold = kDriftStepResidualMultiple * kDriftEpsilonMs;
  bool is_step = largest_jump > step_threshold;
  if (is_step) {
    std::int64_t sum_before = 0;
    for (std::size_t i = 0; i < step_index; ++i) {
      if (!detail::checked_add(sum_before, offset_ms[i], &sum_before)) {
        return std::nullopt;
      }
    }
    std::int64_t sum_after = 0;
    for (std::size_t i = step_index; i < count; ++i) {
      if (!detail::checked_add(sum_after, offset_ms[i], &sum_after)) {
        return std::nullopt;
      }
    }
    std::int64_t mean_before = 0;
    std::int64_t mean_after = 0;
    if (!detail::checked_div(sum_before, static_cast<std::int64_t>(step_index), &mean_before) ||
        !detail::checked_div(sum_after, static_cast<std::int64_t>(count - step_index), &mean_after)) {
      return std::nullopt;
    }
    for (std::size_t i = 0; i < step_index && is_step; ++i) {
      if (abs_i64(offset_ms[i] - mean_before) > kDriftEpsilonMs) {
        is_step = false;
      }
    }
    for (std::size_t i = step_index; i < count && is_step; ++i) {
      if (abs_i64(offset_ms[i] - mean_after) > kDriftEpsilonMs) {
        is_step = false;
      }
    }
  }

  if (is_step) {
    fit.pattern = DriftPattern::step;
    const std::optional<RationalValue> step_time_ms_value =
        detail::ticks_to_ms(checkpoints[step_index].t_v_ticks, tb);
    if (!step_time_ms_value.has_value()) {
      return std::nullopt;
    }
    fit.step_time_ms = step_time_ms_value->num;
  } else {
    fit.pattern = DriftPattern::irregular;
  }
  return fit;
}

}  // namespace mediadiff
