#include "probe/video_detectors.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "util/ssim_int.h"

namespace mediadiff {

int black_point_for_range(std::string_view folded_color_range) {
  return folded_color_range == "pc" ? 0 : 16;
}

namespace detail {

void FrozenHysteresis::commit(std::int64_t last, std::int64_t last_tick) {
  if (last - open_.first + 1 >= kFrozenMinFrames) {
    FrameRun run = open_;
    run.last = last;
    run.last_tick = last_tick;
    runs_.push_back(run);
  }
}

void FrozenHysteresis::push(std::int64_t a, std::int64_t ssim_micro, std::int64_t a_tick) {
  if (!in_run_) {
    if (ssim_micro > kFrozenEnterMicro) {
      in_run_ = true;
      open_ = FrameRun{a, a, a_tick, a_tick};
    }
    return;
  }
  if (ssim_micro > kFrozenContinueMicro) {
    return;
  }
  // The pair (a, a+1) broke the run, so frame `a` is its last frame.
  commit(a, a_tick);
  in_run_ = false;
}

void FrozenHysteresis::close_open(std::int64_t last, std::int64_t last_tick) {
  if (!in_run_) {
    return;
  }
  commit(last, last_tick);
  in_run_ = false;
}

}  // namespace detail

void FrozenDetector::feed(const Thumbnail& thumbnail, std::int64_t tick) {
  const std::int64_t index = count_++;
  if (have_previous_) {
    if (previous_.width == thumbnail.width && previous_.height == thumbnail.height) {
      const std::optional<std::int64_t> score = ssim_plane_q24(previous_.pixels.data(), previous_.width,
                                                               thumbnail.pixels.data(), thumbnail.width,
                                                               thumbnail.width, thumbnail.height);
      if (score.has_value()) {
        hysteresis_.push(index - 1, q24_to_micro(*score), previous_tick_);
      } else {
        // A thumbnail shorter than one SSIM window cannot be scored at all.
        measurable_ = false;
        hysteresis_.close_open(index - 1, previous_tick_);
      }
    } else {
      // SSIM is undefined across sizes: a geometry change ends any open run.
      hysteresis_.close_open(index - 1, previous_tick_);
    }
  }
  previous_ = thumbnail;
  previous_tick_ = tick;
  have_previous_ = true;
}

void FrozenDetector::finish() {
  if (have_previous_) {
    hysteresis_.close_open(count_ - 1, previous_tick_);
  }
}

bool thumbnail_is_black(const Thumbnail& thumbnail, int black_point) {
  const std::int64_t n = static_cast<std::int64_t>(thumbnail.width) * thumbnail.height;
  if (n <= 0 || thumbnail.pixels.size() < static_cast<std::size_t>(n)) {
    return false;
  }
  std::int64_t sum = 0;
  std::int64_t sum_sq = 0;
  for (std::int64_t i = 0; i < n; ++i) {
    const std::int64_t v = thumbnail.pixels[static_cast<std::size_t>(i)];
    sum += v;
    sum_sq += v * v;
  }
  const std::int64_t mean_limit = static_cast<std::int64_t>(black_point) + kBlackMeanMargin;
  if (sum > mean_limit * n) {
    return false;
  }
  return n * sum_sq - sum * sum < static_cast<std::int64_t>(kBlackVarianceLimit) * n * n;
}

void BlackDetector::close_open() {
  if (!in_run_) {
    return;
  }
  in_run_ = false;
  if (open_.last - open_.first + 1 >= kBlackMinFrames) {
    runs_.push_back(open_);
  }
}

void BlackDetector::feed(const Thumbnail& thumbnail, std::int64_t tick, int black_point) {
  const std::int64_t index = count_++;
  if (!thumbnail_is_black(thumbnail, black_point)) {
    close_open();
    return;
  }
  if (!in_run_) {
    in_run_ = true;
    open_ = FrameRun{index, index, tick, tick};
  } else {
    open_.last = index;
    open_.last_tick = tick;
  }
}

void BlackDetector::finish() { close_open(); }

}  // namespace mediadiff
