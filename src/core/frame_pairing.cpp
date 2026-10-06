#include "core/frame_pairing.h"

#include <limits>
#include <string_view>
#include <utility>

namespace mediadiff {

namespace {

using detail::ExactInt;

constexpr std::string_view kOverflowReason = "exact_arithmetic_overflow";

// A side with frames is usable for time pairing only with one tick per frame,
// a positive timebase and a positive frame interval. Returns the reason
// suffix, or an empty view when usable. An empty side has nothing to pair and
// is vacuously usable.
std::string_view series_problem(const FrameSeries& side) {
  if (side.frame_count == 0) {
    return {};
  }
  if (side.ticks.size() != side.frame_count) {
    return "timestamps_unusable";
  }
  if (side.tb.num <= 0 || side.tb.den <= 0) {
    return "timebase_invalid";
  }
  if (side.interval.num <= 0 || side.interval.den <= 0) {
    return "interval_unknown";
  }
  return {};
}

PairingResult index_pairing(std::size_t baseline_count, std::size_t candidate_count,
                            std::string reason) {
  PairingResult result;
  result.mode = PairingMode::index;
  result.fallback_reason = std::move(reason);
  const std::size_t common = baseline_count < candidate_count ? baseline_count : candidate_count;
  result.events.reserve(baseline_count > candidate_count ? baseline_count : candidate_count);
  for (std::size_t i = 0; i < common; ++i) {
    result.events.push_back(PairEvent{PairEventKind::paired, static_cast<std::int64_t>(i),
                                      static_cast<std::int64_t>(i)});
  }
  for (std::size_t i = common; i < baseline_count; ++i) {
    result.events.push_back(PairEvent{PairEventKind::baseline_only, static_cast<std::int64_t>(i), -1});
  }
  for (std::size_t j = common; j < candidate_count; ++j) {
    result.events.push_back(PairEvent{PairEventKind::candidate_only, -1, static_cast<std::int64_t>(j)});
  }
  return result;
}

}  // namespace

bool frame_time(std::int64_t ticks, std::int64_t first_ticks, const Rational& tb, ExactTime* out) {
  ExactInt relative;
  if (!ExactInt::try_sub(ExactInt::from_i64(ticks), ExactInt::from_i64(first_ticks), &relative)) {
    return false;
  }
  ExactInt numerator;
  if (!ExactInt::try_mul(relative, ExactInt::from_i64(tb.num), &numerator)) {
    return false;
  }
  out->num = numerator;
  out->den = ExactInt::from_i64(tb.den);
  return true;
}

bool pairing_window(const Rational& baseline_interval, const Rational& candidate_interval,
                    ExactTime* out) {
  // The smaller interval: a.num/a.den < b.num/b.den  <=>  a.num*b.den < b.num*a.den (dens > 0).
  ExactInt left;
  ExactInt right;
  if (!ExactInt::try_mul(ExactInt::from_i64(baseline_interval.num),
                         ExactInt::from_i64(candidate_interval.den), &left) ||
      !ExactInt::try_mul(ExactInt::from_i64(candidate_interval.num),
                         ExactInt::from_i64(baseline_interval.den), &right)) {
    return false;
  }
  const Rational& finer = ExactInt::compare(left, right) <= 0 ? baseline_interval : candidate_interval;
  // Half of it: num / (2 * den).
  ExactInt twice_den;
  if (!ExactInt::try_add(ExactInt::from_i64(finer.den), ExactInt::from_i64(finer.den), &twice_den)) {
    return false;
  }
  out->num = ExactInt::from_i64(finer.num);
  out->den = twice_den;
  return true;
}

PairStep pair_step(const ExactTime& baseline, const ExactTime& candidate, const ExactTime& window) {
  // With dens > 0: tA - tB = (A.num*B.den - B.num*A.den) / (A.den*B.den).
  ExactInt a_scaled;
  ExactInt b_scaled;
  if (!ExactInt::try_mul(baseline.num, candidate.den, &a_scaled) ||
      !ExactInt::try_mul(candidate.num, baseline.den, &b_scaled)) {
    return PairStep::overflow;
  }
  ExactInt difference;
  if (!ExactInt::try_sub(a_scaled, b_scaled, &difference)) {
    return PairStep::overflow;
  }
  // |tA - tB| < w  <=>  |difference| * w.den < w.num * A.den * B.den.
  ExactInt lhs;
  ExactInt den_product;
  ExactInt rhs;
  if (!ExactInt::try_mul(difference.abs(), window.den, &lhs) ||
      !ExactInt::try_mul(baseline.den, candidate.den, &den_product) ||
      !ExactInt::try_mul(window.num, den_product, &rhs)) {
    return PairStep::overflow;
  }
  if (ExactInt::compare(lhs, rhs) < 0) {
    return PairStep::pair;
  }
  // Outside the window: the earlier frame is unpaired. `difference` is the
  // sign of tA - tB, so a non-positive value means the baseline is not later.
  return ExactInt::compare(difference, ExactInt::from_i64(0)) <= 0 ? PairStep::advance_baseline
                                                                   : PairStep::advance_candidate;
}

PairingResult pair_frames(const FrameSeries& baseline, const FrameSeries& candidate) {
  const std::string_view baseline_problem = series_problem(baseline);
  if (!baseline_problem.empty()) {
    return index_pairing(baseline.frame_count, candidate.frame_count,
                         std::string("baseline_") + std::string(baseline_problem));
  }
  const std::string_view candidate_problem = series_problem(candidate);
  if (!candidate_problem.empty()) {
    return index_pairing(baseline.frame_count, candidate.frame_count,
                         std::string("candidate_") + std::string(candidate_problem));
  }

  PairingResult result;
  result.mode = PairingMode::time;
  result.events.reserve(baseline.frame_count > candidate.frame_count ? baseline.frame_count
                                                                     : candidate.frame_count);

  std::size_t i = 0;
  std::size_t j = 0;
  if (baseline.frame_count > 0 && candidate.frame_count > 0) {
    ExactTime window;
    if (!pairing_window(baseline.interval, candidate.interval, &window)) {
      return index_pairing(baseline.frame_count, candidate.frame_count, std::string(kOverflowReason));
    }
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::size_t time_a_for = kNone;
    std::size_t time_b_for = kNone;
    ExactTime time_a;
    ExactTime time_b;
    while (i < baseline.frame_count && j < candidate.frame_count) {
      if (time_a_for != i) {
        if (!frame_time(baseline.ticks[i], baseline.ticks[0], baseline.tb, &time_a)) {
          return index_pairing(baseline.frame_count, candidate.frame_count,
                               std::string(kOverflowReason));
        }
        time_a_for = i;
      }
      if (time_b_for != j) {
        if (!frame_time(candidate.ticks[j], candidate.ticks[0], candidate.tb, &time_b)) {
          return index_pairing(baseline.frame_count, candidate.frame_count,
                               std::string(kOverflowReason));
        }
        time_b_for = j;
      }
      switch (pair_step(time_a, time_b, window)) {
        case PairStep::pair:
          result.events.push_back(PairEvent{PairEventKind::paired, static_cast<std::int64_t>(i),
                                            static_cast<std::int64_t>(j)});
          ++i;
          ++j;
          break;
        case PairStep::advance_baseline:
          result.events.push_back(
              PairEvent{PairEventKind::baseline_only, static_cast<std::int64_t>(i), -1});
          ++i;
          break;
        case PairStep::advance_candidate:
          result.events.push_back(
              PairEvent{PairEventKind::candidate_only, -1, static_cast<std::int64_t>(j)});
          ++j;
          break;
        case PairStep::overflow:
          return index_pairing(baseline.frame_count, candidate.frame_count,
                               std::string(kOverflowReason));
      }
    }
  }
  for (; i < baseline.frame_count; ++i) {
    result.events.push_back(PairEvent{PairEventKind::baseline_only, static_cast<std::int64_t>(i), -1});
  }
  for (; j < candidate.frame_count; ++j) {
    result.events.push_back(PairEvent{PairEventKind::candidate_only, -1, static_cast<std::int64_t>(j)});
  }
  return result;
}

}  // namespace mediadiff
