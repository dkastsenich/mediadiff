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

// D-05's own grid-conformance test: does `actual_delta` (the timestamp's
// own offset from `first_pts`, already computed via checked_sub by the
// caller) sit within `tolerance` ticks of `round_half_even(numerator /
// denominator)` -- doc 04 section 1.1's own report-layer rounding rule,
// reused here for the grid-conformance test itself. This function performs
// ROUND-HALF-TO-EVEN division WITHOUT EVER DIVIDING. `numerator`/
// `denominator` are both non-negative by construction (numerator = n *
// span_ticks with n >= 0 and span_ticks >= 0, since the index view sorts
// ascending; denominator = interval_count > 0), which is what lets the
// tie-break rule below assume a non-negative candidate.
//
// Derivation (cross-multiplication only, matching compare_ticks' own
// discipline in core/rational.h): let x = numerator/denominator. For any
// candidate integer k, round_half_even(x) >= k  iff  x > k - 0.5, OR x is
// EXACTLY k - 0.5 and k is even (half-to-even rounds the tie UP only when
// the upper candidate is even); symmetrically, round_half_even(x) <= k iff
// x < k + 0.5, OR x is EXACTLY k + 0.5 and k is even. Multiplying every
// inequality by 2*denominator clears the /2 and the /denominator at once:
// x > k - 0.5  <=>  2*numerator > denominator*(2k - 1), etc. Testing both
// bounds at k = actual_delta - tolerance and k = actual_delta + tolerance
// is exactly "round_half_even(x) is within tolerance ticks of
// actual_delta" -- the conformance test doc 04's own rounding rule
// requires, with NO division anywhere in this function.
//
// Sets `*ok = false` (return value unspecified) on any checked-arithmetic
// overflow -- the same "insufficient_data, never a wrapped value" contract
// every other helper in this file follows (T-05-09).
bool conforms_to_grid(std::int64_t numerator, std::int64_t denominator, std::int64_t actual_delta,
                       std::int64_t tolerance, bool* ok) {
  *ok = true;

  std::int64_t two_numerator = 0;
  if (!detail::checked_add(numerator, numerator, &two_numerator)) {
    *ok = false;
    return false;
  }

  std::int64_t low_k = 0;
  if (!detail::checked_sub(actual_delta, tolerance, &low_k)) {
    *ok = false;
    return false;
  }
  std::int64_t low_bound = 0;
  {
    std::int64_t two_low_k_minus_one = 0;
    if (!detail::checked_mul(low_k, 2, &two_low_k_minus_one) ||
        !detail::checked_sub(two_low_k_minus_one, 1, &two_low_k_minus_one) ||
        !detail::checked_mul(denominator, two_low_k_minus_one, &low_bound)) {
      *ok = false;
      return false;
    }
  }
  const bool low_ok = (two_numerator > low_bound) || (two_numerator == low_bound && (low_k % 2) == 0);

  std::int64_t high_k = 0;
  if (!detail::checked_add(actual_delta, tolerance, &high_k)) {
    *ok = false;
    return false;
  }
  std::int64_t high_bound = 0;
  {
    std::int64_t two_high_k_plus_one = 0;
    if (!detail::checked_mul(high_k, 2, &two_high_k_plus_one) ||
        !detail::checked_add(two_high_k_plus_one, 1, &two_high_k_plus_one) ||
        !detail::checked_mul(denominator, two_high_k_plus_one, &high_bound)) {
      *ok = false;
      return false;
    }
  }
  const bool high_ok = (two_numerator < high_bound) || (two_numerator == high_bound && (high_k % 2) == 0);

  return low_ok && high_ok;
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
  // D-05: matching_intervals/total_intervals stay populated with D-07's own
  // meaning (Test 9) -- a same-timebase consumer reading them is
  // unaffected -- but neither decides `klass` anymore; the grid-conformance
  // test below does.
  result.matching_intervals = matching;
  result.total_intervals = static_cast<std::int64_t>(intervals.size());

  // D-05: span_ticks is the LAST usable timestamp minus the FIRST (`valid`
  // is sorted ascending, so this is simply the two ends), via checked_sub
  // -- Test 8's overflow discipline applies here too. interval_count is the
  // SAME VALUE as total_intervals above, kept as its own field (see this
  // file's own header comment on why).
  std::int64_t span_ticks = 0;
  if (!detail::checked_sub(timestamp_on(packets[valid.back()], axis), timestamp_on(packets[valid.front()], axis),
                            &span_ticks)) {
    result.status = CadenceStatus::insufficient_data;
    return result;
  }
  result.span_ticks = span_ticks;
  result.interval_count = result.total_intervals;
  // D-05: the EXACT rational ideal interval -- never pre-divided (this
  // file's own header comment; plan 05-08's grid-relative histogram bins
  // read these two fields directly).
  result.ideal_interval_num = span_ticks;
  result.ideal_interval_den = result.interval_count;

  // D-05's own CFR/VFR basis: for every usable, sorted timestamp n (0-based,
  // n=0 is trivially conforming -- its ideal position IS first_pts), test
  // whether it sits within kGridConformanceToleranceTicks of `first_pts +
  // round_half_even(n * ideal_interval_num/ideal_interval_den)`, via
  // conforms_to_grid's own cross-multiplication-only discipline (no
  // division anywhere in this path, per this file's own D-05 comment
  // block).
  const std::int64_t first_timestamp = timestamp_on(packets[valid.front()], axis);
  std::int64_t conforming = 0;
  const std::int64_t considered = static_cast<std::int64_t>(valid.size());
  for (std::int64_t n = 0; n < considered; ++n) {
    std::int64_t actual_delta = 0;
    if (!detail::checked_sub(timestamp_on(packets[valid[static_cast<std::size_t>(n)]], axis), first_timestamp,
                              &actual_delta)) {
      result.status = CadenceStatus::insufficient_data;
      return result;
    }
    std::int64_t numerator = 0;
    if (!detail::checked_mul(n, span_ticks, &numerator)) {
      result.status = CadenceStatus::insufficient_data;
      return result;
    }
    bool ok = false;
    const bool on_grid =
        conforms_to_grid(numerator, result.interval_count, actual_delta, kGridConformanceToleranceTicks, &ok);
    if (!ok) {
      result.status = CadenceStatus::insufficient_data;
      return result;
    }
    if (on_grid) {
      ++conforming;
    }
  }
  result.conforming_timestamps = conforming;
  result.considered_timestamps = considered;

  // CFR iff conforming/considered >= kCfrMatchingProportionNum/
  // kCfrMatchingProportionDen, cross-multiplied rather than divided:
  // conforming * Den >= considered * Num -- D-05 reuses D-07's own
  // 995/1000 proportion constants unchanged (05-CONTEXT.md D-05 explicitly
  // keeps them), applied now to the grid-conformance counts instead of the
  // mode-interval-matching counts. Given kMaxPacketsPerStream's own ceiling
  // (5,000,000), neither product can realistically overflow int64_t, but
  // the checked path is used anyway (this project's own rational-everywhere
  // rule) -- an overflow here (which would require a caller outside any
  // real PacketScan) is treated the same as every other "cannot determine"
  // case in this function: insufficient_data, never a fabricated class.
  std::int64_t lhs = 0;
  std::int64_t rhs = 0;
  if (!detail::checked_mul(conforming, kCfrMatchingProportionDen, &lhs) ||
      !detail::checked_mul(considered, kCfrMatchingProportionNum, &rhs)) {
    result.status = CadenceStatus::insufficient_data;
    return result;
  }
  result.klass = (lhs >= rhs) ? CadenceClass::cfr : CadenceClass::vfr;

  return result;
}

}  // namespace mediadiff
