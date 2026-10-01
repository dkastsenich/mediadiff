#include "probe/pair_scorer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include "core/frame_pairing.h"
#include "core/rational.h"
#include "probe/video_thumbnail.h"
#include "util/quality_math.h"
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


// ---------------------------------------------------------------------------
// 07-10-PLAN.md: reading the native planes of a borrowed AVFrame.
// ---------------------------------------------------------------------------

// What the native scorers need to know about one frame's layout.
struct NativeLayout {
  const AVPixFmtDescriptor* desc = nullptr;
  int width = 0;
  int height = 0;
  // Colour components scored: 1 (gray) or 3 (YUV); alpha is never scored.
  int planes = 0;
  int log2_chroma_w = 0;
  int log2_chroma_h = 0;
  int depth = 0;
  std::string label;
};

enum class LayoutStatus { ok, unavailable, unsupported };

// The layout of `frame`, or why it cannot be scored. Decided from the frame's
// own descriptor before any plane is read (T-07-30): a frame that is not planar
// or packed YUV / gray of one uniform depth of 1 to 16 bits is unsupported
// (RGB has no luma plane, a paletted or float or hardware frame has no
// comparable integer samples).
LayoutStatus describe_frame(const AVFrame* frame, NativeLayout* out) {
  if (frame == nullptr || frame->data[0] == nullptr || frame->width <= 0 || frame->height <= 0) {
    return LayoutStatus::unavailable;
  }
  out->width = frame->width;
  out->height = frame->height;
  const AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
  const char* name = av_get_pix_fmt_name(format);
  out->label = std::to_string(frame->width) + "x" + std::to_string(frame->height) + " " +
               (name != nullptr ? name : "unknown");
  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
  if (desc == nullptr) {
    return LayoutStatus::unsupported;
  }
  constexpr int kRejected = AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_PAL |
                            AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_XYZ | AV_PIX_FMT_FLAG_BAYER;
  if ((desc->flags & kRejected) != 0) {
    return LayoutStatus::unsupported;
  }
  const int alpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) != 0 ? 1 : 0;
  const int colour = static_cast<int>(desc->nb_components) - alpha;
  if (colour != 1 && colour != 3) {
    return LayoutStatus::unsupported;
  }
  const int depth = desc->comp[0].depth;
  if (depth < 1 || depth > 16) {
    return LayoutStatus::unsupported;
  }
  for (int c = 0; c < colour; ++c) {
    if (desc->comp[c].depth != depth || frame->data[desc->comp[c].plane] == nullptr) {
      return LayoutStatus::unsupported;
    }
  }
  out->desc = desc;
  out->planes = colour;
  out->log2_chroma_w = colour == 3 ? desc->log2_chroma_w : 0;
  out->log2_chroma_h = colour == 3 ? desc->log2_chroma_h : 0;
  out->depth = depth;
  return LayoutStatus::ok;
}

int plane_dimension(int full, int log2_subsampling) {
  return (full + (1 << log2_subsampling) - 1) >> log2_subsampling;
}

// A component whose samples are plain bytes at their plane's natural stride, so
// the frame's own memory is the plane.
bool directly_readable_8bit(const AVPixFmtDescriptor& desc, int component) {
  const AVComponentDescriptor& c = desc.comp[component];
  return c.depth == 8 && c.step == 1 && c.shift == 0 && c.offset == 0;
}

// Reads one row of component `component` (pixels 0..width of plane row `y`)
// into `dst` as 16-bit values at the component's native depth.
void read_row(const AVFrame& frame, const AVPixFmtDescriptor& desc, int component, int y, int width,
              std::uint16_t* dst) {
  const std::uint8_t* data[4] = {frame.data[0], frame.data[1], frame.data[2], frame.data[3]};
  const int linesize[4] = {frame.linesize[0], frame.linesize[1], frame.linesize[2], frame.linesize[3]};
  av_read_image_line2(dst, data, linesize, &desc, 0, y, component, width, 0, 2);
}

struct Plane8 {
  const std::uint8_t* data = nullptr;
  std::ptrdiff_t stride = 0;
};

// Plane `component` as bytes at depth 8 (promoted by `shift` bits first). The
// frame's own memory when it is directly readable and unshifted, else a copy in
// `scratch`.
Plane8 acquire_plane8(const AVFrame& frame, const NativeLayout& layout, int component, int width, int height,
                      int shift, std::vector<std::uint8_t>* scratch, std::vector<std::uint16_t>* row) {
  if (shift == 0 && directly_readable_8bit(*layout.desc, component)) {
    return Plane8{frame.data[layout.desc->comp[component].plane],
                  static_cast<std::ptrdiff_t>(frame.linesize[layout.desc->comp[component].plane])};
  }
  scratch->resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  row->resize(static_cast<std::size_t>(width));
  for (int y = 0; y < height; ++y) {
    read_row(frame, *layout.desc, component, y, width, row->data());
    std::uint8_t* out = scratch->data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      out[x] = static_cast<std::uint8_t>((*row)[static_cast<std::size_t>(x)] << shift);
    }
  }
  return Plane8{scratch->data(), static_cast<std::ptrdiff_t>(width)};
}

// Plane `component` as 16-bit values promoted by `shift` bits, always a copy in
// `scratch` at stride `width` (one reader for every endianness and packing).
const std::uint16_t* acquire_plane16(const AVFrame& frame, const NativeLayout& layout, int component, int width,
                                     int height, int shift, std::vector<std::uint16_t>* scratch) {
  scratch->resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    std::uint16_t* out = scratch->data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    read_row(frame, *layout.desc, component, y, width, out);
    if (shift != 0) {
      for (int x = 0; x < width; ++x) {
        out[x] = static_cast<std::uint16_t>(out[x] << shift);
      }
    }
  }
  return scratch->data();
}

QualityPoint make_point(const TappedFrame& baseline, const TappedFrame& candidate, std::int64_t value) {
  QualityPoint point;
  point.baseline_index = baseline.decode_index;
  point.candidate_index = candidate.decode_index;
  point.has_pts = baseline.has_pts;
  point.pts = baseline.pts;
  point.tb_num = baseline.tb_num;
  point.tb_den = baseline.tb_den;
  point.value = value;
  return point;
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

void PsnrAccumulator::add(const PsnrFrame& frame) {
  if (count_ == 0 || frame.point.value < min_.value) {
    min_ = frame.point;
  }
  if (count_ == 0) {
    plane_count_ = frame.plane_count;
  }
  ++count_;
  sum_ += frame.point.value;
  for (std::size_t i = 0; i < plane_sum_.size(); ++i) {
    plane_sum_[i] += frame.plane[i];
  }
  if (frame.identical) {
    ++identical_;
  }
}

std::optional<PsnrSummary> PsnrAccumulator::summary() const {
  if (count_ == 0) {
    return std::nullopt;
  }
  PsnrSummary out;
  out.pairs_scored = count_;
  out.mean_milli_db = floor_div(sum_, count_);
  out.min = min_;
  out.plane_count = plane_count_;
  for (std::size_t i = 0; i < plane_sum_.size(); ++i) {
    out.plane_mean_milli_db[i] = floor_div(plane_sum_[i], count_);
  }
  out.identical_frames = identical_;
  return out;
}

void NativeSsimAccumulator::add(const QualityPoint& point, bool identical) {
  if (count_ == 0 || point.value < min_.value) {
    min_ = point;
  }
  ++count_;
  sum_ += point.value;
  if (identical) {
    ++identical_;
  }
}

std::optional<NativeSsimSummary> NativeSsimAccumulator::summary() const {
  if (count_ == 0) {
    return std::nullopt;
  }
  NativeSsimSummary out;
  out.pairs_scored = count_;
  out.mean_micro = floor_div(sum_, count_);
  out.min = min_;
  out.identical_frames = identical_;
  return out;
}

PairScorer::PairScorer(int sample_stride, std::int64_t stop_after_scored_pairs, QualityRequest quality)
    : sample_stride_(sample_stride > 1 ? sample_stride : 1),
      stop_after_scored_pairs_(stop_after_scored_pairs),
      quality_(quality) {}

const char* PairScorer::quality_stop_name(QualityStop reason) {
  switch (reason) {
    case QualityStop::none:
      return "";
    case QualityStop::geometry_mismatch:
      return "geometry_mismatch";
    case QualityStop::frame_unavailable:
      return "frame_unavailable";
    case QualityStop::unsupported_format:
      return "unsupported_pixel_format";
  }
  return "";
}

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
  // The native scorers read the frames, not the thumbnails, and latch their own
  // stops: perceptual below runs whether or not they did.
  if ((quality_.psnr || quality_.ssim) && quality_stop_ == QualityStop::none) {
    score_quality(baseline, candidate);
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

void PairScorer::score_quality(const TappedFrame& baseline, const TappedFrame& candidate) {
  NativeLayout a;
  NativeLayout b;
  const LayoutStatus status_a = describe_frame(baseline.frame, &a);
  const LayoutStatus status_b = describe_frame(candidate.frame, &b);
  const auto latch = [&](QualityStop reason) {
    quality_stop_ = reason;
    quality_baseline_label_ = a.label;
    quality_candidate_label_ = b.label;
  };
  if (status_a == LayoutStatus::unavailable || status_b == LayoutStatus::unavailable) {
    latch(QualityStop::frame_unavailable);
    return;
  }
  if (status_a == LayoutStatus::unsupported || status_b == LayoutStatus::unsupported) {
    latch(QualityStop::unsupported_format);
    return;
  }
  // Equal display dimensions and plane layout, checked before any plane is read
  // (T-07-30). A different bit depth is NOT a mismatch: the lower is promoted by
  // an exact left shift below.
  if (a.width != b.width || a.height != b.height || a.planes != b.planes || a.log2_chroma_w != b.log2_chroma_w ||
      a.log2_chroma_h != b.log2_chroma_h) {
    latch(QualityStop::geometry_mismatch);
    return;
  }
  const int bpc = std::max(a.depth, b.depth);
  if (quality_bpc_ == 0) {
    quality_bpc_ = bpc;
    quality_planes_ = a.planes;
  } else if (bpc != quality_bpc_ || a.planes != quality_planes_) {
    // A cap or a plane set that changes mid-stream would make the mean
    // meaningless: stop rather than blend.
    latch(QualityStop::geometry_mismatch);
    return;
  }
  const int shift_a = bpc - a.depth;
  const int shift_b = bpc - b.depth;
  const bool want_ssim = quality_.ssim && !ssim_too_small_ && a.width >= kSsimWindow && a.height >= kSsimWindow;
  if (quality_.ssim && !want_ssim) {
    ssim_too_small_ = true;
  }
  const int planes = quality_.psnr ? a.planes : 1;

  std::uint64_t total_sse = 0;
  std::uint64_t total_samples = 0;
  PsnrFrame psnr_frame;
  psnr_frame.plane_count = a.planes;
  std::optional<std::int64_t> ssim_q24;

  for (int c = 0; c < planes; ++c) {
    const int pw = c == 0 ? a.width : plane_dimension(a.width, a.log2_chroma_w);
    const int ph = c == 0 ? a.height : plane_dimension(a.height, a.log2_chroma_h);
    std::uint64_t sse = 0;
    if (bpc <= 8) {
      const Plane8 pa = acquire_plane8(*baseline.frame, a, c, pw, ph, shift_a, &scratch8_[0], &row_);
      const Plane8 pb = acquire_plane8(*candidate.frame, b, c, pw, ph, shift_b, &scratch8_[1], &row_);
      if (quality_.psnr) {
        sse = plane_sse<std::uint8_t>(pa.data, pa.stride, pb.data, pb.stride, pw, ph);
      }
      if (c == 0 && want_ssim) {
        ssim_q24 = ssim_plane_q24(pa.data, pa.stride, pb.data, pb.stride, pw, ph);
      }
    } else {
      const std::uint16_t* pa = acquire_plane16(*baseline.frame, a, c, pw, ph, shift_a, &scratch16_[0]);
      const std::uint16_t* pb = acquire_plane16(*candidate.frame, b, c, pw, ph, shift_b, &scratch16_[1]);
      if (quality_.psnr) {
        sse = plane_sse<std::uint16_t>(pa, pw, pb, pw, pw, ph);
      }
      if (c == 0 && want_ssim) {
        ssim_q24 = ssim_plane_q24_wide(pa, pw, pb, pw, pw, ph, bpc);
      }
    }
    if (quality_.psnr) {
      const std::uint64_t samples = static_cast<std::uint64_t>(pw) * static_cast<std::uint64_t>(ph);
      psnr_frame.plane[static_cast<std::size_t>(c)] = psnr_milli_db(sse, samples, bpc);
      total_sse += sse;
      total_samples += samples;
    }
  }

  if (quality_.psnr) {
    psnr_frame.point = make_point(baseline, candidate, psnr_milli_db(total_sse, total_samples, bpc));
    psnr_frame.identical = total_sse == 0;
    psnr_.add(psnr_frame);
  }
  if (want_ssim) {
    if (ssim_q24.has_value()) {
      ssim_.add(make_point(baseline, candidate, q24_to_micro(*ssim_q24)), *ssim_q24 == kSsimQ24One);
    } else {
      ssim_too_small_ = true;
    }
  }
}

}  // namespace mediadiff
