#pragma once

// 07-05-PLAN.md (CONTENT-06, research Q5, claude_docs/06 section 2.3 as
// amended): the frozen-run and black-run detectors. Both are SINKS on the one
// video decode sweep: they consume the deterministic 128-wide 8-bit luma
// thumbnail (probe/video_thumbnail.h) of every decoded frame -- whatever
// `--sample N` is (D-08: a value the stride does not own must not depend on
// it) -- and produce frame-index runs. Converting runs to millisecond spans is
// the analyzer's job (analyzers/content/video_runs.cpp); nothing here knows
// the stream's timebase. All arithmetic is exact integer arithmetic (no
// real-number types anywhere), so a run boundary can never depend on the
// platform or the compiler's floating-point habits (TRUST-05).
//
// FROZEN: SSIM hysteresis on consecutive thumbnails, for every decoder class.
// Enter a run when a consecutive-frame SSIM is above kFrozenEnterMicro, stay in
// it while each one is above kFrozenContinueMicro, and report runs of at least
// kFrozenMinFrames frames. Why two thresholds and not exact hash equality:
// research Q5 measured twelve encodes of one synthetic 50-frame freeze. Exact
// equality fragments at every I-frame and P-frame refinement, vanishes for
// MPEG-2 and for H.264 without a refresh, and moves with GOP length -- a span
// that depends on GOP structure is exactly the false-positive class this tool
// exists to avoid. A single 0.9995 threshold split the run in three of the
// twelve (I-frame refresh inside a frozen region dips the pair score to 0.9984
// at worst). Hysteresis (0.9995 to enter, 0.995 to continue) gave the same
// span for all twelve. The worst in-run dip (0.9984) sits 2.3x further from 1
// than the continue threshold does, and the moving regions scored 0.887-0.940
// (testsrc2) and 0.984-0.997 (a mandelbrot zoom, 1 of 99 pairs above 0.995).
//
// BLACK: range- and depth-normalized. The thumbnail is already 8 bits. A frame
// is black when its mean is at most (black_point + kBlackMeanMargin) and its
// variance is below kBlackVarianceLimit; black_point is 16 for limited or
// unspecified range and 0 for full range, read AFTER detail::fold_pix_fmt_range
// so a yuvj format counts as full. Measured (Q5): a black segment encoded in
// tv and pc range gives thumbnail mean exactly 16.0 and 0.0 with variance 0 in
// both. A range-unaware `mean <= 18` flags pc-range dark grey (Y 16-18); a
// range-unaware `mean <= 2` misses every tv-range black.
//
// A10: every constant below is validated on SYNTHETIC content only. A
// near-static smooth source is indistinguishable from frozen at 128 wide; both
// sides of a compare flag such content identically and `span` gates only
// INTRODUCED runs, so that is not a false-positive source by itself. The
// constants ship fixed (Phase 5 D-08: a detection parameter changes the
// measured value, unlike a tolerance), and a WINDOWS.md `todo` records the
// real-content review.
//
// A11: kBlackMinFrames = 3 is the planner's value. Doc 06 section 2.3 gives a
// minimum only for frozen runs, but a one-frame black flash near a fade can
// cross the threshold in one encode and not the other.

#include <cstdint>
#include <string_view>
#include <vector>

#include "probe/video_thumbnail.h"

namespace mediadiff {

// Consecutive-frame SSIM (millionths) above which a frozen run STARTS (0.9995).
inline constexpr std::int64_t kFrozenEnterMicro = 999500;
// ... and above which it CONTINUES (0.995).
inline constexpr std::int64_t kFrozenContinueMicro = 995000;
// Shortest frozen run reported, in frames (doc 06 section 2.3).
inline constexpr int kFrozenMinFrames = 3;

// A frame is black when mean <= black_point + kBlackMeanMargin ...
inline constexpr int kBlackMeanMargin = 2;
// ... and variance < kBlackVarianceLimit (8-bit sample units squared).
inline constexpr int kBlackVarianceLimit = 4;
// Shortest black run reported, in frames (A11).
inline constexpr int kBlackMinFrames = 3;

// A run of consecutive decoded frames, by index in output order (frame 0 is
// the first frame the detector saw), inclusive at both ends. `first_tick` and
// `last_tick` are those two frames' presentation timestamps in the stream's
// time base (meaningful only while the stream's timestamps are usable).
struct FrameRun {
  std::int64_t first = 0;
  std::int64_t last = 0;
  std::int64_t first_tick = 0;
  std::int64_t last_tick = 0;

  bool operator==(const FrameRun&) const = default;
};

// The black point for a colour range AFTER the yuvj fold: "pc" is full range
// (0), anything else -- "tv", "unknown", "unspecified" -- is limited (16)
// (A12: unspecified is treated as limited, the YUV convention).
int black_point_for_range(std::string_view folded_color_range);

namespace detail {

// The hysteresis state machine over consecutive-pair scores, separate from the
// SSIM and the pixels so its boundaries are directly unit-testable. A pair is
// (frame a, frame a+1); `push` is told a, the pair's score in millionths and
// frame a's tick.
class FrozenHysteresis {
 public:
  void push(std::int64_t a, std::int64_t ssim_micro, std::int64_t a_tick);

  // Closes an open run at frame `last` (the last frame the caller fed) -- used
  // at the end of the stream and when SSIM becomes undefined (a size change).
  void close_open(std::int64_t last, std::int64_t last_tick);

  const std::vector<FrameRun>& runs() const { return runs_; }

 private:
  void commit(std::int64_t last, std::int64_t last_tick);

  bool in_run_ = false;
  FrameRun open_{};
  std::vector<FrameRun> runs_;
};

}  // namespace detail

// Consumes thumbnails in decode order and reports frozen runs. Holds ONE
// previous thumbnail, never the sequence (bounded memory). A change of
// thumbnail geometry breaks any open run (SSIM is undefined across sizes).
class FrozenDetector {
 public:
  void feed(const Thumbnail& thumbnail, std::int64_t tick);
  // Ends the stream: closes an open run. Call once, after the last frame.
  void finish();

  const std::vector<FrameRun>& runs() const { return hysteresis_.runs(); }
  std::int64_t frames_fed() const { return count_; }
  // False once any consecutive pair could not be scored (a thumbnail shorter
  // than one SSIM window): the run list is then NOT a measurement.
  bool measurable() const { return measurable_; }

 private:
  detail::FrozenHysteresis hysteresis_;
  Thumbnail previous_;
  std::int64_t previous_tick_ = 0;
  std::int64_t count_ = 0;
  bool have_previous_ = false;
  bool measurable_ = true;
};

// True when `thumbnail` is black at `black_point` (mean <= black_point +
// kBlackMeanMargin and variance < kBlackVarianceLimit), by exact integer
// sums: with n pixels, S the sum and Q the sum of squares,
// S <= (black_point + margin) * n and n*Q - S*S < limit * n * n.
// Bounds: n <= 128 * kMaxThumbnailHeight = 2^22, so S <= 2^30, Q <= 2^38,
// n*Q and S*S <= 2^60, limit*n*n <= 2^46 -- all inside int64_t (T-07-16).
bool thumbnail_is_black(const Thumbnail& thumbnail, int black_point);

// Consumes thumbnails in decode order and reports black runs of at least
// kBlackMinFrames frames.
class BlackDetector {
 public:
  void feed(const Thumbnail& thumbnail, std::int64_t tick, int black_point);
  void finish();

  const std::vector<FrameRun>& runs() const { return runs_; }
  std::int64_t frames_fed() const { return count_; }

 private:
  void close_open();

  std::vector<FrameRun> runs_;
  FrameRun open_{};
  std::int64_t count_ = 0;
  bool in_run_ = false;
};

}  // namespace mediadiff
