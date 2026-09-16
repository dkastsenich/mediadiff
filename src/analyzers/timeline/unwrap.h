#pragma once

// unwrap_ts_timestamps: doc 04 section 1.2's 33-bit MPEG-TS PTS/DTS
// unwrap rule, quoted verbatim (05-02-PLAN.md Task 2, TIME-02):
//
// "Per elementary stream, before any other analysis: given consecutive
// raw PTS/DTS in 90 kHz, if delta < -2^32 (half range), add 2^33 to the
// running unwrap offset; symmetric guard for backward jumps > half range
// (treated as genuine discontinuity, not wrap). Unwrapped values feed
// everything downstream; raw values are preserved in evidence. Wrap
// events themselves are recorded (info) -- a candidate that wraps where
// baseline didn't usually means a start-offset change upstream."
// (claude_docs/04-timeline-analysis.md section 1.2)
//
// Same three contracts src/probe/cadence.h states about itself (PROBE-10):
// pure, deterministic, integer-only; operates on a caller-owned span and
// never mutates or re-sorts it (`raw_in_read_order` is a span of const
// values -- the caller keeps the raw span for evidence, TIME-02's "raw
// values are preserved in evidence" is the CALLER's job, this function
// makes it possible by never destroying them); no second av_read_frame
// sweep and nothing pre-computed during any scan -- this is a pure
// function over whatever int64 ticks the caller already has in memory.

#include <cstdint>
#include <span>
#include <vector>

namespace mediadiff {

// doc 04 section 1.2's own constants, transcribed as named values (never
// a bare decimal literal at a use site) -- the 90 kHz MPEG-TS PTS/DTS
// domain's 33-bit wrap modulus and its half-range wrap-detection
// threshold.
inline constexpr std::int64_t kTsPtsWrapModulus = std::int64_t{1} << 33;    // 2^33
inline constexpr std::int64_t kTsPtsWrapHalfRange = std::int64_t{1} << 32;  // 2^32

// unwrap_ts_timestamps' own result: `unwrapped` is the SAME LENGTH as the
// input and is the ONLY value downstream timeline analysis consumes --
// the caller keeps its own raw input span for evidence, this function
// never destroys it. `wrap_events` counts every delta-strictly-below-
// half-range step detected. `overflowed` is set once a checked arithmetic
// step anywhere in the walk fails (T-05-05: a crafted stream cannot grow
// the running offset without bound) -- the caller maps `overflowed ==
// true` to SkipReason::insufficient_data, the same precedent
// src/probe/cadence.cpp's own comment states for every other checked-
// arithmetic failure in this project.
struct UnwrapResult {
  std::vector<std::int64_t> unwrapped;
  std::int64_t wrap_events = 0;
  bool overflowed = false;
};

// Walks `raw_in_read_order` applying doc 04 section 1.2's asymmetric
// unwrap rule to consecutive pairs. Every arithmetic step -- the delta,
// the running-offset update, and the value application -- goes through
// detail::checked_sub/checked_add (core/rational.h); an overflow anywhere
// sets `overflowed = true` and the walk stops ADJUSTING the offset
// further at that step (it never stops producing output), matching
// src/probe/cadence.cpp's own "an overflow anywhere in this arithmetic
// yields insufficient_data, never a wrapped value" precedent.
//
// The forward guard (a delta ABOVE +kTsPtsWrapHalfRange) is deliberately
// asymmetric: it does NOT adjust the running offset. doc 04 makes this
// asymmetry normative -- a symmetric rule would erase a genuine backward
// discontinuity, which is precisely what TIME-02 asks this function to
// distinguish from a wrap (T-05-07).
UnwrapResult unwrap_ts_timestamps(std::span<const std::int64_t> raw_in_read_order);

namespace detail {

// One wrap-detection decision, in isolation from any surrounding walk:
// given the running offset and an already-computed delta (raw[i] -
// raw[i-1], via checked_sub at the call site), applies doc 04 section
// 1.2's asymmetric rule. Exposed here (mirrors src/probe/ts_scan.h's own
// step_continuity/PidContinuityState shape, per this plan's own read_first
// list) so a unit test can seed an offset already near INT64_MAX and
// prove T-05-05's overflow-stops-adjusting behavior directly, threading
// several calls to demonstrate REPEATED growth followed by a refusal --
// without replaying the ~2^30 real wrap events unwrap_ts_timestamps' own
// top-level loop would need to reach that magnitude through ordinary
// input. Building (or even iterating) an input span that large is not
// something a unit test can afford, in either time or memory.
struct WrapStepResult {
  std::int64_t next_offset = 0;
  bool wrapped = false;
  bool overflowed = false;
};

// `offset` is the running offset BEFORE this step; `delta` is the raw
// value's already-computed, already-overflow-checked delta from its
// predecessor. Returns the (possibly unchanged) next offset, whether this
// step registered a wrap, and whether the offset-update's own checked_add
// failed (in which case `next_offset == offset`, unchanged -- never a
// silently wrapped value).
WrapStepResult apply_wrap_step(std::int64_t offset, std::int64_t delta);

}  // namespace detail

}  // namespace mediadiff
