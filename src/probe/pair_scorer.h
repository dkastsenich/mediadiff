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
//
// 07-10-PLAN.md (CONTENT-08; D-01, D-03): the same paired frames also feed the
// opt-in NATIVE-resolution scorers, quality.psnr and quality.ssim, when the
// caller's QualityRequest asks for them. Those read the borrowed AVFrames'
// planes (the pair's only libav contact, kept inside pair_scorer.cpp), at the
// pair's own stride and pairing, and gate on the floor MEAN of the per-frame
// scores (D-03); the minimum (with its frame indices and PTS) rides in
// evidence. Their latched failures (a geometry the two sides cannot pair, a
// format that cannot be read) stop the QUALITY checks only -- perceptual keeps
// running.
//
// 07-11-PLAN.md (CONTENT-09): `--vmaf` rides the same paired frames. A build
// configured with MEDIADIFF_WITH_VMAF feeds each scored pair to a
// VmafAccumulator (probe/vmaf_scorer.h) and pools once at the end; any other
// build never constructs one. `--sample N` (N >= 2) refuses VMAF outright --
// its temporal features need consecutive frames -- so the accumulator exists
// only at a stride of 1.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "core/exact_int.h"
#include "core/frame_pairing.h"
#include "core/rational.h"
#include "probe/lockstep.h"
#include "probe/vmaf_scorer.h"

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

// One scored pair of a native-resolution quality check: where it was (the same
// indices and baseline PTS PairScore carries) and its value in the check's own
// integer unit (milli-dB for PSNR, millionths for SSIM).
struct QualityPoint {
  std::int64_t baseline_index = 0;
  std::int64_t candidate_index = 0;
  bool has_pts = false;
  std::int64_t pts = 0;
  std::int64_t tb_num = 0;
  std::int64_t tb_den = 1;
  std::int64_t value = 0;
};

// One frame pair's PSNR: the combined (sample-count-weighted luma plus chroma)
// value rides in `point.value`; `plane` holds the per-plane values (Y, U, V --
// only the first `plane_count` are meaningful; a gray frame has one).
struct PsnrFrame {
  QualityPoint point;
  int plane_count = 0;
  std::array<std::int64_t, 3> plane{};
  // Every plane's SSE was zero.
  bool identical = false;
};

struct PsnrSummary {
  std::int64_t pairs_scored = 0;
  // floor(sum / pairs_scored) of the per-frame combined milli-dB: the gated value.
  std::int64_t mean_milli_db = 0;
  // The lowest combined value (the first one on a tie), with its frame indices.
  QualityPoint min;
  int plane_count = 0;
  std::array<std::int64_t, 3> plane_mean_milli_db{};
  std::int64_t identical_frames = 0;
};

class PsnrAccumulator {
 public:
  void add(const PsnrFrame& frame);
  std::int64_t count() const { return count_; }
  // Empty when no pair was added (the caller reports insufficient_data).
  std::optional<PsnrSummary> summary() const;

 private:
  std::int64_t count_ = 0;
  std::int64_t sum_ = 0;
  std::array<std::int64_t, 3> plane_sum_{};
  int plane_count_ = 0;
  std::int64_t identical_ = 0;
  QualityPoint min_;
};

struct NativeSsimSummary {
  std::int64_t pairs_scored = 0;
  // floor(sum / pairs_scored) of the per-frame micro-SSIM: the gated value.
  std::int64_t mean_micro = 0;
  QualityPoint min;
  // Frames whose luma windows all scored exactly one (identical luma).
  std::int64_t identical_frames = 0;
};

class NativeSsimAccumulator {
 public:
  void add(const QualityPoint& point, bool identical);
  std::int64_t count() const { return count_; }
  std::optional<NativeSsimSummary> summary() const;

 private:
  std::int64_t count_ = 0;
  std::int64_t sum_ = 0;
  std::int64_t identical_ = 0;
  QualityPoint min_;
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

  // Why the native quality checks stopped scoring (their own latch, separate
  // from StopReason: perceptual keeps running past it).
  enum class QualityStop {
    none,
    // The two sides' display dimensions, plane layout or bit depth differ.
    geometry_mismatch,
    // A frame was not available to read (a synthetic or non-decoded pair).
    frame_unavailable,
    // A pixel format the scorers cannot read (RGB, paletted, float, hardware,
    // or a layout other than gray / three-plane YUV).
    unsupported_format,
  };

  // `sample_stride` is `--sample N`: every Nth PAIRED frame is scored (D-08).
  // `stop_after_scored_pairs` is a test hook (0 = never): the scorer latches
  // test_stop after that many scored pairs, the way a consumer that gave up
  // would. `quality` is 07-10's opt-in native scorers (default: none).
  PairScorer(int sample_stride, std::int64_t stop_after_scored_pairs, QualityRequest quality = {});

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

  // --- 07-10: the native-resolution quality scorers ---
  const QualityRequest& quality_request() const { return quality_; }
  QualityStop quality_stop() const { return quality_stop_; }
  static const char* quality_stop_name(QualityStop reason);
  // Empty unless the matching check was requested and a pair was scored.
  std::optional<PsnrSummary> psnr_summary() const { return psnr_.summary(); }
  std::optional<NativeSsimSummary> ssim_summary() const { return ssim_.summary(); }
  // The pair bit depth (the larger of the two sides') every scored pair used; 0
  // before the first scored pair.
  int quality_bpc() const { return quality_bpc_; }
  // True when a frame was too small for one SSIM window (PSNR is unaffected).
  bool ssim_frame_too_small() const { return ssim_too_small_; }
  // "WxH pix_fmt" of each side's frame at the pair that latched a quality stop
  // (empty otherwise): the evidence a geometry_mismatch carries.
  const std::string& quality_baseline_label() const { return quality_baseline_label_; }
  const std::string& quality_candidate_label() const { return quality_candidate_label_; }

  // --- 07-11: quality.vmaf (CONTENT-09) ---
  // Flushes and pools the VMAF contexts, once, after the last pair. A no-op
  // unless a VmafAccumulator exists (VMAF requested, a build that links libvmaf,
  // stride 1) and the pairing and the native quality latch both ran to the end.
  void finish_vmaf();
  // A libvmaf failure (construction, a pair, or pooling): the compare cannot
  // report a score and fails with this internal error rather than guess.
  const std::optional<Error>& vmaf_error() const { return vmaf_error_; }
  // Empty until finish_vmaf() ran and pooled.
  const std::optional<VmafSummary>& vmaf_summary() const { return vmaf_summary_; }
  // The pair's chroma layout is none libvmaf accepts (4:2:0, 4:2:2, 4:4:4, gray):
  // reported as geometry_mismatch, never converted. PSNR and SSIM are unaffected.
  bool vmaf_layout_unsupported() const { return vmaf_layout_unsupported_; }
  // The frames are smaller than libvmaf can score (kVmafMinDimension): reported as
  // insufficient_data, and libvmaf is never handed such a picture.
  bool vmaf_frame_too_small() const { return vmaf_frame_too_small_; }

 private:
  void decide_mode(const TappedFrame& baseline, const TappedFrame& candidate);
  // Scores the pair just decided; may latch a stop.
  void score_pair(const TappedFrame& baseline, const TappedFrame& candidate);
  // 07-10: scores the pair at native resolution; may latch a quality stop.
  void score_quality(const TappedFrame& baseline, const TappedFrame& candidate);

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

  QualityRequest quality_;
  QualityStop quality_stop_ = QualityStop::none;
  int quality_bpc_ = 0;
  int quality_planes_ = 0;
  bool ssim_too_small_ = false;
  std::string quality_baseline_label_;
  std::string quality_candidate_label_;
  PsnrAccumulator psnr_;
  NativeSsimAccumulator ssim_;
  // shared_ptr, not unique_ptr: a build without libvmaf never defines
  // VmafAccumulator's destructor, and a shared_ptr (whose deleter is captured at
  // construction) does not need it to be.
  std::shared_ptr<VmafAccumulator> vmaf_;
  std::optional<Error> vmaf_error_;
  std::optional<VmafSummary> vmaf_summary_;
  bool vmaf_layout_unsupported_ = false;
  bool vmaf_frame_too_small_ = false;
  // Read only by finish_vmaf()'s MEDIADIFF_WITH_VMAF branch. The define is
  // PRIVATE to libmediadiff, so guarding the member would change this class's
  // layout between the library and any TU that includes the header; keep it
  // unconditional and silence -Wunused-private-field on non-VMAF builds.
  [[maybe_unused]] bool vmaf_finished_ = false;
  // Reused plane buffers, one pair per side: a plane is read into them (when it
  // is not directly usable), scored, and overwritten by the next plane.
  std::array<std::vector<std::uint8_t>, 2> scratch8_;
  std::array<std::vector<std::uint16_t>, 2> scratch16_;
  std::vector<std::uint16_t> row_;
};

}  // namespace mediadiff
