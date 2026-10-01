#include "probe/pair_scorer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "core/frame_pairing.h"
#include "core/rational.h"
#include "probe/video_thumbnail.h"
#include "util/ssim_int.h"

namespace mediadiff {

namespace {

// Floor division for a non-negative divisor, correct for a negative sum.
std::int64_t floor_div(std::int64_t numerator, std::int64_t denominator) {
  std::int64_t quotient = numerator / denominator;
  if (numerator % denominator != 0 && numerator < 0) {
    --quotient;
  }
  return quotient;
}

// Score ascending, then baseline index ascending.
bool worse_than(const PairScore& a, const PairScore& b) {
  if (a.score_micro != b.score_micro) {
    return a.score_micro < b.score_micro;
  }
  return a.baseline_index < b.baseline_index;
}

// The reason suffix for a side that cannot be time paired, or empty when usable.
// Mirrors core/frame_pairing.cpp's series_problem, decided here from the side's
// first frame (A16).
std::string side_problem(const char* side, const TappedFrame& first) {
  if (!first.has_pts) {
    return std::string(side) + "_timestamps_unusable";
  }
  if (first.tb_num <= 0 || first.tb_den <= 0) {
    return std::string(side) + "_timebase_invalid";
  }
  if (first.interval_num <= 0 || first.interval_den <= 0) {
    return std::string(side) + "_interval_unknown";
  }
  return std::string();
}

}  // namespace

void PerceptualAccumulator::add(const PairScore& pair) {
  if (count_ == 0 || pair.score_micro < min_) {
    min_ = pair.score_micro;
  }
  ++count_;
  sum_ += pair.score_micro;
  if (!first_below_.has_value() && pair.score_micro < kPerceptualThresholdMicro) {
    first_below_ = pair;
  }
  // A bounded sorted list: insert in order, drop the best when over the bound.
  const auto position = std::lower_bound(worst_.begin(), worst_.end(), pair, worse_than);
  worst_.insert(position, pair);
  if (worst_.size() > kPerceptualWorstListSize) {
    worst_.pop_back();
  }
}

std::optional<PerceptualSummary> PerceptualAccumulator::summary() const {
  if (count_ == 0) {
    return std::nullopt;
  }
  PerceptualSummary out;
  out.pairs_scored = count_;
  out.min_micro = min_;
  out.mean_micro = floor_div(sum_, count_);
  out.first_below = first_below_;
  out.worst = worst_;
  return out;
}

PairScorer::PairScorer(int sample_stride, std::int64_t stop_after_scored_pairs)
    : sample_stride_(sample_stride > 1 ? sample_stride : 1), stop_after_scored_pairs_(stop_after_scored_pairs) {}

const char* PairScorer::stop_reason_name(StopReason reason) {
  switch (reason) {
    case StopReason::none:
      return "";
    case StopReason::geometry_mismatch:
      return "geometry_mismatch";
    case StopReason::thumbnail_unavailable:
      return "thumbnail_unavailable";
    case StopReason::thumbnail_too_small:
      return "thumbnail_too_small";
    case StopReason::no_partner:
      return "no_partner";
    case StopReason::test_stop:
      return "test_stop";
  }
  return "";
}

void PairScorer::stop(StopReason reason) {
  if (stop_reason_ == StopReason::none) {
    stop_reason_ = reason;
  }
}

void PairScorer::count_unpaired(bool baseline_side, std::int64_t frames) {
  (baseline_side ? unpaired_baseline_ : unpaired_candidate_) += frames;
}

void PairScorer::decide_mode(const TappedFrame& baseline, const TappedFrame& candidate) {
  decided_ = true;
  std::string reason = side_problem("baseline", baseline);
  if (reason.empty()) {
    reason = side_problem("candidate", candidate);
  }
  if (!reason.empty()) {
    mode_ = PairingMode::index;
    fallback_reason_ = std::move(reason);
    return;
  }
  baseline_tb_ = Rational{baseline.tb_num, baseline.tb_den};
  candidate_tb_ = Rational{candidate.tb_num, candidate.tb_den};
  first_baseline_pts_ = baseline.pts;
  first_candidate_pts_ = candidate.pts;
  if (!pairing_window(Rational{baseline.interval_num, baseline.interval_den},
                      Rational{candidate.interval_num, candidate.interval_den}, &window_)) {
    // Unreachable for int64 inputs (frame_pairing.h); recorded, never guessed.
    mode_ = PairingMode::index;
    fallback_reason_ = "exact_arithmetic_overflow";
    return;
  }
  mode_ = PairingMode::time;
}

PairScorer::Action PairScorer::step(const TappedFrame& baseline, const TappedFrame& candidate) {
  if (!decided_) {
    decide_mode(baseline, candidate);
  }

  if (mode_ == PairingMode::time) {
    // A frame of a time-paired stream that lacks a timestamp cannot be placed:
    // it is counted unpaired (A16) and its side advances, baseline first.
    if (!baseline.has_pts) {
      ++unpaired_baseline_;
      ++unpaired_no_pts_;
      return Action::advance_baseline;
    }
    if (!candidate.has_pts) {
      ++unpaired_candidate_;
      ++unpaired_no_pts_;
      return Action::advance_candidate;
    }
    ExactTime baseline_time;
    ExactTime candidate_time;
    PairStep decision = PairStep::overflow;
    if (frame_time(baseline.pts, first_baseline_pts_, baseline_tb_, &baseline_time) &&
        frame_time(candidate.pts, first_candidate_pts_, candidate_tb_, &candidate_time)) {
      decision = pair_step(baseline_time, candidate_time, window_);
    }
    switch (decision) {
      case PairStep::advance_baseline:
        ++unpaired_baseline_;
        return Action::advance_baseline;
      case PairStep::advance_candidate:
        ++unpaired_candidate_;
        return Action::advance_candidate;
      case PairStep::overflow:
        // The rest pairs by position, recorded (frame_pairing.h: unreachable
        // for int64 inputs, reachable only through the ExactTime seam).
        mode_ = PairingMode::index;
        fallback_reason_ = "exact_arithmetic_overflow";
        break;
      case PairStep::pair:
        break;
    }
  }

  score_pair(baseline, candidate);
  return Action::advance_both;
}

void PairScorer::score_pair(const TappedFrame& baseline, const TappedFrame& candidate) {
  const std::int64_t ordinal = pair_ordinal_++;
  if (ordinal % sample_stride_ != 0) {
    return;
  }
  if (baseline.thumbnail == nullptr || candidate.thumbnail == nullptr) {
    stop(StopReason::thumbnail_unavailable);
    return;
  }
  const Thumbnail& a = *baseline.thumbnail;
  const Thumbnail& b = *candidate.thumbnail;
  if (a.width != b.width || a.height != b.height) {
    mismatch_baseline_height_ = a.height;
    mismatch_candidate_height_ = b.height;
    stop(StopReason::geometry_mismatch);
    return;
  }
  const std::size_t expected = static_cast<std::size_t>(a.width) * static_cast<std::size_t>(a.height);
  if (a.pixels.size() != expected || b.pixels.size() != expected) {
    stop(StopReason::thumbnail_unavailable);
    return;
  }
  const std::optional<std::int64_t> q24 =
      ssim_plane_q24(a.pixels.data(), a.width, b.pixels.data(), b.width, a.width, a.height);
  if (!q24.has_value()) {
    // Narrower or shorter than one SSIM window: no score can be formed.
    stop(StopReason::thumbnail_too_small);
    return;
  }

  PairScore pair;
  pair.baseline_index = baseline.decode_index;
  pair.candidate_index = candidate.decode_index;
  pair.score_micro = q24_to_micro(*q24);
  pair.has_pts = baseline.has_pts;
  pair.pts = baseline.pts;
  pair.tb_num = baseline.tb_num;
  pair.tb_den = baseline.tb_den;
  accumulator_.add(pair);

  if (stop_after_scored_pairs_ > 0 && accumulator_.count() >= stop_after_scored_pairs_) {
    stop(StopReason::test_stop);
  }
}

}  // namespace mediadiff
