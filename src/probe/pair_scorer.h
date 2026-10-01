#pragma once

// 07-08-PLAN.md (CONTENT-04; D-02, D-03): the consumer half of the lockstep --
// online frame pairing and the perceptual score over the paired 128-wide luma
// thumbnails. Libav-free and real-number-free: pairing is 07-03's exact rule
// (core/frame_pairing.h's pair_step, driven one frame at a time), the score is
// 07-05's integer SSIM in Q24 converted to millionths half-up, and every
// statistic here is an int64.
//
// What is reported (D-03): the MINIMUM pair score gates; the floor mean, the
// first pair strictly below kPerceptualThresholdMicro and the ten worst pairs
// ride in evidence. A pair scoring exactly the threshold is NOT below it.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/exact_int.h"
#include "core/frame_pairing.h"
#include "core/rational.h"
#include "probe/lockstep.h"

namespace mediadiff {

// 0.985 in millionths: the pair score below which a frame is "visibly
// different" (doc 06 section 2.2). A reporting constant, never a knob: it only
// decides what is LISTED, never the verdict.
inline constexpr std::int64_t kPerceptualThresholdMicro = 985000;

// The worst-pairs list length (D-03).
inline constexpr std::size_t kPerceptualWorstListSize = 10;

// One scored pair.
struct PairScore {
  // The two frames' decode indices (the same indices frame_hash's divergence
  // locator reports).
  std::int64_t baseline_index = 0;
  std::int64_t candidate_index = 0;
  // The SSIM of the pair in millionths, half-up from Q24 (a negative score is
  // possible for inverted pictures).
  std::int64_t score_micro = 0;
  // The baseline frame's own PTS in its own time base, when it has one.
  bool has_pts = false;
  std::int64_t pts = 0;
  std::int64_t tb_num = 0;
  std::int64_t tb_den = 1;
};

struct PerceptualSummary {
  std::int64_t pairs_scored = 0;
  std::int64_t min_micro = 0;
  // floor(sum / pairs_scored), the sum taken in int64.
  std::int64_t mean_micro = 0;
  // The first scored pair strictly below the threshold, in pair order.
  std::optional<PairScore> first_below;
  // At most kPerceptualWorstListSize pairs, score ascending, ties by baseline
  // index ascending. Equal scores on adjacent frames are both kept.
  std::vector<PairScore> worst;
};

class PerceptualAccumulator {
 public:
  void add(const PairScore& pair);
  std::int64_t count() const { return count_; }
  // Empty when no pair was added -- the caller reports insufficient_data, never
  // a fabricated score of 1.
  std::optional<PerceptualSummary> summary() const;

 private:
  std::int64_t count_ = 0;
  std::int64_t sum_ = 0;
  std::int64_t min_ = 0;
  std::optional<PairScore> first_below_;
  std::vector<PairScore> worst_;
};

class PairScorer {
 public:
  // What the driver does with the two current frames after step().
  enum class Action { advance_both, advance_baseline, advance_candidate };

  // Why the scorer stopped (and the driver closed both slots).
  enum class StopReason {
    none,
    geometry_mismatch,
    thumbnail_unavailable,
    thumbnail_too_small,
    no_partner,
    test_stop,
  };

  // `sample_stride` is `--sample N`: every Nth PAIRED frame is scored (D-08).
  // `stop_after_scored_pairs` is a test hook (0 = never): the scorer latches
  // test_stop after that many scored pairs, the way a consumer that gave up
  // would.
  PairScorer(int sample_stride, std::int64_t stop_after_scored_pairs);

  // Decides what to do with `baseline` and `candidate`, the two current frames
  // (both valid until the driver releases them), and scores them when they
  // pair. The first call decides time vs index pairing from these two first
  // frames (flagged assumption A16).
  Action step(const TappedFrame& baseline, const TappedFrame& candidate);

  // Counts `frames` frames of one side as unpaired (the tail of a side whose
  // partner ended first).
  void count_unpaired(bool baseline_side, std::int64_t frames);

  // Latches a stop (the first reason wins).
  void stop(StopReason reason);
  bool stopped() const { return stop_reason_ != StopReason::none; }
  StopReason stop_reason() const { return stop_reason_; }
  static const char* stop_reason_name(StopReason reason);

  PairingMode pairing() const { return mode_; }
  // Empty unless pairing() is index.
  const std::string& pairing_fallback() const { return fallback_reason_; }
  int sample_stride() const { return sample_stride_; }

  std::int64_t pairs_paired() const { return pair_ordinal_; }
  std::int64_t pairs_scored() const { return accumulator_.count(); }
  std::int64_t unpaired_baseline() const { return unpaired_baseline_; }
  std::int64_t unpaired_candidate() const { return unpaired_candidate_; }
  // Frames of a time-paired stream that lacked a timestamp (counted unpaired).
  std::int64_t unpaired_no_pts() const { return unpaired_no_pts_; }
  std::optional<PerceptualSummary> summary() const { return accumulator_.summary(); }

  // The two thumbnail heights of the pair that latched geometry_mismatch.
  int mismatch_baseline_height() const { return mismatch_baseline_height_; }
  int mismatch_candidate_height() const { return mismatch_candidate_height_; }

 private:
  void decide_mode(const TappedFrame& baseline, const TappedFrame& candidate);
  // Scores the pair just decided; may latch a stop.
  void score_pair(const TappedFrame& baseline, const TappedFrame& candidate);

  int sample_stride_;
  std::int64_t stop_after_scored_pairs_;
  StopReason stop_reason_ = StopReason::none;

  bool decided_ = false;
  PairingMode mode_ = PairingMode::index;
  std::string fallback_reason_;
  std::int64_t first_baseline_pts_ = 0;
  std::int64_t first_candidate_pts_ = 0;
  Rational baseline_tb_{1, 1};
  Rational candidate_tb_{1, 1};
  ExactTime window_{};

  std::int64_t pair_ordinal_ = 0;
  std::int64_t unpaired_baseline_ = 0;
  std::int64_t unpaired_candidate_ = 0;
  std::int64_t unpaired_no_pts_ = 0;
  int mismatch_baseline_height_ = 0;
  int mismatch_candidate_height_ = 0;
  PerceptualAccumulator accumulator_;
};

}  // namespace mediadiff
