#include "probe/cadence.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "core/rational.h"
#include "probe/packet_scan.h"

namespace mediadiff {

namespace {

// The raw timestamp on `axis` for packet index `i`, verbatim (the sentinel
// survives -- see this file's own header comment).
std::int64_t timestamp_on(const PacketRecord& record, CadenceAxis axis) {
  return axis == CadenceAxis::pts ? record.pts : record.dts;
}

// Counts non-sentinel timestamps on `axis`, stopping as soon as `cap` is
// reached -- D-06's axis-selection test only ever needs to know "at least
// two", never the true total, so this never walks the whole array twice at
// full cost when the answer is already decided.
std::size_t count_usable(std::span<const PacketRecord> packets, CadenceAxis axis, std::size_t cap) {
  std::size_t count = 0;
  for (const PacketRecord& record : packets) {
    if (timestamp_on(record, axis) != INT64_MIN) {
      ++count;
      if (count >= cap) {
        break;
      }
    }
  }
  return count;
}

}  // namespace

Cadence derive_cadence(std::span<const PacketRecord> packets, Rational tb) {
  Cadence result;
  result.tb = tb;

  // T-4-30: this derivation's own primitive is StreamPacketScan::packets,
  // already bounded to kMaxPacketsPerStream by PacketScan itself -- checked
  // again here so a future caller that hands this pure function a span not
  // sourced from a real PacketScan inherits the identical bound rather than
  // an unbounded tally below.
  if (packets.size() > static_cast<std::size_t>(kMaxPacketsPerStream)) {
    result.status = CadenceStatus::insufficient_data;
    return result;
  }

  if (tb.num <= 0 || tb.den <= 0) {
    result.status = CadenceStatus::insufficient_data;
    return result;
  }

  // D-06: PTS is the axis whenever it carries at least two usable values
  // anywhere in the array; otherwise DTS; otherwise (this mirrors
  // detail::WindowStatus's own no_timing_data/insufficient_data split,
  // src/analyzers/size/analyzers.h) the derivation reports no_timing_data
  // ONLY when NEITHER axis carries a single real timestamp, and
  // insufficient_data when a real axis exists but has fewer than two.
  const std::size_t pts_usable = count_usable(packets, CadenceAxis::pts, 2);
  CadenceAxis axis = CadenceAxis::pts;
  std::size_t usable_on_axis = pts_usable;
  if (pts_usable < 2) {
    const std::size_t dts_usable = count_usable(packets, CadenceAxis::dts, 2);
    axis = CadenceAxis::dts;
    usable_on_axis = dts_usable;
    if (dts_usable == 0 && pts_usable == 0) {
      result.status = CadenceStatus::no_timing_data;
      return result;
    }
  }
  result.axis = axis;

  if (usable_on_axis < 2) {
    result.status = CadenceStatus::insufficient_data;
    return result;
  }

  // Sort an INDEX VIEW, never `packets` itself (packet_scan.h's own
  // documented contract: read order, NOT guaranteed timestamp-sorted --
  // mirrors detail::compute_peak_window's identical discipline,
  // src/analyzers/size/size.cpp). Proves Test 6 (shuffled read order
  // produces the identical result).
  std::vector<std::size_t> valid;
  valid.reserve(packets.size());
  for (std::size_t i = 0; i < packets.size(); ++i) {
    if (timestamp_on(packets[i], axis) != INT64_MIN) {
      valid.push_back(i);
    }
  }
  if (valid.size() < 2) {
    // Unreachable given usable_on_axis's own count above, kept as a
    // defensive mirror of that same check rather than trusted implicitly.
    result.status = CadenceStatus::insufficient_data;
    return result;
  }
  std::sort(valid.begin(), valid.end(), [&](std::size_t a, std::size_t b) {
    return timestamp_on(packets[a], axis) < timestamp_on(packets[b], axis);
  });

  // Consecutive intervals, every subtraction checked (Test 8: an overflow
  // anywhere in this arithmetic yields insufficient_data, never a wrapped
  // value).
  std::vector<std::int64_t> intervals;
  intervals.reserve(valid.size() - 1);
  for (std::size_t i = 1; i < valid.size(); ++i) {
    std::int64_t interval = 0;
    if (!detail::checked_sub(timestamp_on(packets[valid[i]], axis), timestamp_on(packets[valid[i - 1]], axis),
                              &interval)) {
      result.status = CadenceStatus::insufficient_data;
      return result;
    }
    intervals.push_back(interval);
  }

  // Mode selection: tally each distinct interval value (T-4-30's bound is
  // inherited from the kMaxPacketsPerStream check above -- intervals.size()
  // is always strictly less than packets.size()), take the most frequent,
  // breaking ties by the SMALLER interval so the result is deterministic
  // under any input ordering -- std::map's own ordered iteration is what
  // makes "smallest interval on a tie" a simple first-wins scan rather than
  // a second comparison.
  std::map<std::int64_t, std::int64_t> tally;
  for (std::int64_t interval : intervals) {
    ++tally[interval];
  }

  std::int64_t mode_interval = 0;
  std::int64_t mode_count = -1;
  for (const auto& [interval, count] : tally) {
    if (count > mode_count) {
      mode_count = count;
      mode_interval = interval;
    }
  }

  // D-07: exact integer comparison against the mode, within
  // kCadenceEpsilonTicks (fixed at zero, A1) -- integer-only throughout, no
  // percentage of a computed mean.
  std::int64_t matching = 0;
  for (std::int64_t interval : intervals) {
    std::int64_t delta = 0;
    if (!detail::checked_sub(interval, mode_interval, &delta)) {
      result.status = CadenceStatus::insufficient_data;
      return result;
    }
    if (delta < 0) {
      if (!detail::checked_negate(delta, &delta)) {
        result.status = CadenceStatus::insufficient_data;
        return result;
      }
    }
    if (delta <= kCadenceEpsilonTicks) {
      ++matching;
    }
  }

  result.status = CadenceStatus::ok;
  result.mode_interval_ticks = mode_interval;
  result.matching_intervals = matching;
  result.total_intervals = static_cast<std::int64_t>(intervals.size());

  // CFR iff matching/total >= kCfrMatchingProportionNum/kCfrMatchingProportionDen,
  // cross-multiplied rather than divided:
  // matching * Den >= total * Num. Given kMaxPacketsPerStream's own ceiling
  // (5,000,000), neither product can realistically overflow int64_t, but
  // the checked path is used anyway (this project's own rational-everywhere
  // rule) -- an overflow here (which would require a caller outside any
  // real PacketScan) is treated the same as every other "cannot determine"
  // case in this function: insufficient_data, never a fabricated class.
  std::int64_t lhs = 0;
  std::int64_t rhs = 0;
  if (!detail::checked_mul(matching, kCfrMatchingProportionDen, &lhs) ||
      !detail::checked_mul(result.total_intervals, kCfrMatchingProportionNum, &rhs)) {
    result.status = CadenceStatus::insufficient_data;
    return result;
  }
  result.klass = (lhs >= rhs) ? CadenceClass::cfr : CadenceClass::vfr;

  return result;
}

}  // namespace mediadiff
