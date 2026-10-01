#pragma once

// The ONE frame-pairing rule (07-CONTEXT.md D-02 and D-07).
//
// Two video files are lined up by PRESENTATION TIME, not by decode index: a
// frame dropped in the middle of one file leaves exactly one frame unpaired
// instead of shifting every later pair. The rule, in full:
//
//   * each side's time is measured from its OWN first frame,
//       t = (ticks[i] - ticks[0]) * tb
//     so a container that starts its clock elsewhere (MPEG-TS's 1.4 s shift,
//     an MP4 edit list's negative first tick) cannot misalign the sides;
//   * two frames pair when their times differ by STRICTLY less than half the
//     frame interval of the FINER side (the smaller of the two intervals): a
//     difference of exactly half does not pair;
//   * otherwise the frame whose time is earlier is emitted unpaired and that
//     side advances (equal times always pair, because the window is positive;
//     if both were ever outside the window at equal time the baseline would
//     advance first);
//   * when either side has no usable timestamps, or an unknown frame interval,
//     the whole result falls back to pairing by decode index and records why.
//
// EVERY decision is exact rational cross-multiplication in detail::ExactInt
// (core/exact_int.h, 256-bit): nothing inexact decides a pair, and
// core/rational.h's tick comparison helper is deliberately not used (its
// overflow fallback is sign-only, WR-03). A product that would not fit 256 bits
// switches the WHOLE result to index pairing with
// fallback_reason == "exact_arithmetic_overflow" -- never a guessed order
// (T-07-10). That is structurally unreachable for int64 ticks, timebases and
// intervals (the widest product below is four int64 magnitudes and a shift,
// 2^255), so the branch is exercised through pair_step's ExactTime seam.
//
// `pair_step` is the single decision primitive. `pair_frames` (batch, used
// by src/compare/hash.cpp's locator) and any online consumer (07-08's
// lockstep scorer, which sees one decoded frame at a time) both drive it, so
// the rule exists once.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/exact_int.h"
#include "core/rational.h"

namespace mediadiff {

// One side's per-frame presentation timestamps.
struct FrameSeries {
  // One tick per frame, in `tb`-second units. Empty when the side carries no
  // usable timestamps (a raw elementary stream); index mode then applies.
  std::span<const std::int64_t> ticks;
  // The side's timebase, seconds per tick ({num, den}, both positive).
  Rational tb{0, 0};
  // The side's nominal frame interval in seconds ({num, den}); zero or
  // negative means unknown.
  Rational interval{0, 0};
  // The number of frames. Index mode needs it when `ticks` is empty.
  std::size_t frame_count = 0;
};

enum class PairingMode { time, index };

enum class PairEventKind { paired, baseline_only, candidate_only };

// `baseline_index` is meaningful for paired and baseline_only events;
// `candidate_index` for paired and candidate_only events. The other field is
// -1.
struct PairEvent {
  PairEventKind kind = PairEventKind::paired;
  std::int64_t baseline_index = -1;
  std::int64_t candidate_index = -1;

  bool operator==(const PairEvent&) const = default;
};

struct PairingResult {
  PairingMode mode = PairingMode::time;
  // In consumption order: ascending in both indices.
  std::vector<PairEvent> events;
  // Non-empty only when mode == index. One of:
  //   baseline_timestamps_unusable / candidate_timestamps_unusable
  //   baseline_timebase_invalid   / candidate_timebase_invalid
  //   baseline_interval_unknown   / candidate_interval_unknown
  //   exact_arithmetic_overflow
  std::string fallback_reason;
};

// A time in exact seconds: num / den with den > 0. Wide on purpose: a frame
// time is (rel ticks * tb.num) / tb.den, which can exceed int64.
struct ExactTime {
  detail::ExactInt num;
  detail::ExactInt den;
};

// What pair_step decided for the two current frames.
enum class PairStep {
  pair,               // the two frames are one pair; advance both
  advance_baseline,   // the baseline frame is unpaired; advance the baseline
  advance_candidate,  // the candidate frame is unpaired; advance the candidate
  overflow            // an exact product exceeded 256 bits; the caller must fall back to index
};

// (ticks - first_ticks) * tb as an exact time. False only on 256-bit overflow
// (unreachable for int64 inputs). `tb` must have num > 0 and den > 0.
bool frame_time(std::int64_t ticks, std::int64_t first_ticks, const Rational& tb, ExactTime* out);

// The pairing window: half of the SMALLER of the two intervals, exact. Both
// intervals must have num > 0 and den > 0.
bool pairing_window(const Rational& baseline_interval, const Rational& candidate_interval,
                    ExactTime* out);

// The single decision primitive. `window` is pairing_window's result.
PairStep pair_step(const ExactTime& baseline, const ExactTime& candidate, const ExactTime& window);

// The batch form: walks both series with pair_step (time mode) or by
// position (index mode) and returns every event.
PairingResult pair_frames(const FrameSeries& baseline, const FrameSeries& candidate);

}  // namespace mediadiff
