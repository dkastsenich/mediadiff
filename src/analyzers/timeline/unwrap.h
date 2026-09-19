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

#include "probe/packet_scan.h"

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

// TimelinePacketView (05-16-PLAN.md, TIME-01/TIME-02/TIME-03, the
// assumption-delta `promote` decision): the ONE promoted representation of
// "a stream's timestamps" every timestamp-derived timeline/video/size
// consumer reads through from this plan onward -- never
// `StreamPacketScan::packets` directly. On a non-MPEG-TS input it borrows
// the source stream's own packet vector (zero-copy, `unwrapped()` false):
// the 33-bit rule is MPEG-TS-specific (doc 04 section 1.2), so a non-TS
// input's packets are never touched, copied, or reordered. On MPEG-TS it
// owns a COPY of the packets with both the PTS and DTS axes independently
// unwrapped via the SAME `unwrap_ts_timestamps` this file already
// implements -- never a second unwrap implementation -- via
// `make_timeline_packet_view`'s own per-axis walk (`detail::build_axis_view`
// for sentinel exclusion/`packet_index` mapping, `unwrap_ts_timestamps`
// itself for the wrap arithmetic and its `wrap_events` count, mirroring
// `src/analyzers/timeline/monotonic.cpp`'s own `emit_wrap_events` --
// `detail::unwrap_axis_view` alone discards the wrap count this view's own
// `pts_wrap_events()`/`dts_wrap_events()` need).
//
// `packets()` is computed FRESH from `borrowed_`/`owned_` on EVERY call,
// never cached at construction or across a copy (T-05-72: a copied or
// moved view must never carry a span that can dangle relative to the
// copy's own storage) -- proven by the Task 2 copy-safety test.
//
// `overflowed()` is per-VIEW, not per-axis: it is set the instant EITHER
// axis's own unwrap, or the cross-stream epoch shift below, cannot
// complete without an int64 overflow (T-05-71) -- the axis (or record)
// that overflowed is left at its last good (raw, un-unwrapped) value
// rather than a wrapped or fabricated one; every consumer maps
// `overflowed() == true` to `SkipReason::insufficient_data`.
class TimelinePacketView {
 public:
  std::span<const PacketRecord> packets() const {
    return (borrowed_ != nullptr) ? std::span<const PacketRecord>(*borrowed_) : std::span<const PacketRecord>(owned_);
  }
  bool unwrapped() const { return unwrapped_; }
  bool overflowed() const { return overflowed_; }
  std::int64_t pts_wrap_events() const { return pts_wrap_events_; }
  std::int64_t dts_wrap_events() const { return dts_wrap_events_; }
  std::int64_t epoch_shift() const { return epoch_shift_; }

 private:
  // Never both populated: `borrowed_` non-null means zero-copy (non-TS);
  // `borrowed_` null means `owned_` is this view's own storage (TS). A
  // copy of the view copies `owned_` by value (a genuine deep copy) and
  // `borrowed_` by pointer value -- `packets()` recomputes the span from
  // whichever is populated on the COPY's own members, never the
  // original's.
  const std::vector<PacketRecord>* borrowed_ = nullptr;
  std::vector<PacketRecord> owned_;
  bool unwrapped_ = false;
  bool overflowed_ = false;
  std::int64_t pts_wrap_events_ = 0;
  std::int64_t dts_wrap_events_ = 0;
  std::int64_t epoch_shift_ = 0;

  friend TimelinePacketView make_timeline_packet_view(const StreamPacketScan& stream, bool is_ts);
  friend std::vector<TimelinePacketView> make_timeline_packet_views(const PacketScanResult& scan, bool is_ts);
};

// Builds ONE stream's own `TimelinePacketView`. `is_ts` false: borrows
// `stream.packets` verbatim, zero-copy, `unwrapped()` false -- the 33-bit
// rule never touches a non-MPEG-TS input (doc 04 section 1.2 is
// MPEG-TS-specific). `is_ts` true: copies `stream.packets` into the view's
// own storage and unwraps the PTS axis, then the DTS axis, independently
// (each axis's own wrap events are counted separately -- a stream can wrap
// on one axis without wrapping on the other). `AV_NOPTS_VALUE`-sentinel
// entries are excluded from each axis's own walk (via
// `detail::build_axis_view`) and therefore never written back --
// `owned_`'s sentinel entries are left exactly as copied. An axis whose
// unwrap overflows (`UnwrapResult::overflowed`) marks the WHOLE view
// `overflowed()` and leaves that axis's values at their raw, un-unwrapped
// copies -- never a wrapped or fabricated value (T-05-71).
TimelinePacketView make_timeline_packet_view(const StreamPacketScan& stream, bool is_ts);

// Builds one `TimelinePacketView` per `scan.per_stream` entry (index-
// aligned, mirroring `PacketScanResult::per_stream`'s own "per_stream[i]
// IS AVStream i" contract every timeline consumer already relies on), then
// -- only when `is_ts` is true -- applies doc 04 section 1.2's cross-stream
// epoch rule: takes each stream's own FIRST RAW (pre-unwrap) PTS in read
// order (the first non-`AV_NOPTS_VALUE` value; a stream with no real PTS
// at all contributes no candidate). When the spread between the largest
// and smallest such candidate exceeds `kTsPtsWrapHalfRange` (2^32), every
// stream whose own first raw PTS sits BELOW `kTsPtsWrapHalfRange` is
// placed one `kTsPtsWrapModulus` (2^33) epoch later -- every non-sentinel
// PTS and DTS value in that stream's own (already per-stream-unwrapped)
// view is shifted by `kTsPtsWrapModulus` via `detail::checked_add`, and
// `epoch_shift()` on that view becomes `kTsPtsWrapModulus`. A stream whose
// spread does not exceed the half-range, or whose own first raw PTS is
// already at or above the half-range, is left unshifted
// (`epoch_shift() == 0`). On `is_ts` false the epoch rule is a no-op
// (non-TS views are already zero-copy and untouched). A checked-add
// overflow while applying the shift marks that stream's own view
// `overflowed()` rather than a wrapped or fabricated value.
std::vector<TimelinePacketView> make_timeline_packet_views(const PacketScanResult& scan, bool is_ts);

}  // namespace mediadiff
