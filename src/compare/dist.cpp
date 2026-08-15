#include "compare/semantics.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "core/rational.h"
#include "core/tolerance.h"

namespace mediadiff {

namespace {

Status escalate(Severity severity) {
  switch (severity) {
    case Severity::fail:
      return Status::fail;
    case Severity::warn:
      return Status::warn;
    case Severity::info:
    case Severity::ignore:
      return Status::info;
  }
  return Status::info;
}

}  // namespace

// compare_dist: doc 01 section 3's `dist` semantic -- normalises both
// Histogram bin counts into proportions as exact rationals over their
// respective totals, then compares the maximum absolute difference across
// every bin present on either side against the check's tolerance (a
// percent-of-proportion threshold). Two empty histograms pass (zero bins,
// vacuous maximum). A std::map merge over both bin sets is single-pass
// (T-2-16: no nested rescan), so a large crafted histogram costs
// O(n log n), not O(n^2).
mediadiff::expected<Finding, Error> compare_dist(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;
  finding.severity = resolve_severity(check, policy);

  auto tolerance = parse_tolerance(check.tolerance_for(policy.profile), check.unit);
  if (!tolerance) {
    return mediadiff::unexpected(tolerance.error());
  }

  // CR-03: every accumulation and cross-multiplication below operates on
  // int64 bin counts read directly from an untrusted snapshot
  // (core/serializer.cpp's value_from_json validates TYPE, never
  // MAGNITUDE) -- a crafted histogram with near-INT64_MAX bin counts
  // previously triggered signed integer overflow (UB) in plain
  // `+`/`*`/`-`. Routed through core/rational.h's own
  // detail::checked_add/checked_mul/checked_sub/checked_negate -- the SAME
  // overflow-checked primitives compare/tol.cpp's CR-03 fix uses -- and on
  // overflow this comparator returns a real Finding carrying
  // Status::error, mirroring compare/engine.cpp's value_kind_mismatch
  // "never a coercion, never a fabricated verdict" contract (D-09) rather
  // than computing a UB-tainted result.
  const auto overflow_finding = [&](std::string_view what) {
    finding.status = Status::error;
    finding.skip_reason = SkipReason::none;
    finding.message = fmt::format(
        "dist comparator: {} overflowed int64_t during bin accumulation/cross-multiplication; cannot determine a "
        "verdict",
        what);
    return finding;
  };

  static const Histogram kEmptyHistogram;
  const auto* baseline_hist_ptr = std::get_if<Histogram>(&baseline.value);
  const auto* candidate_hist_ptr = std::get_if<Histogram>(&candidate.value);
  const Histogram& baseline_hist = baseline_hist_ptr != nullptr ? *baseline_hist_ptr : kEmptyHistogram;
  const Histogram& candidate_hist = candidate_hist_ptr != nullptr ? *candidate_hist_ptr : kEmptyHistogram;

  std::int64_t baseline_total = 0;
  for (const auto& bin : baseline_hist.bins) {
    if (!detail::checked_add(baseline_total, bin.second, &baseline_total)) {
      return overflow_finding("baseline_total");
    }
  }
  std::int64_t candidate_total = 0;
  for (const auto& bin : candidate_hist.bins) {
    if (!detail::checked_add(candidate_total, bin.second, &candidate_total)) {
      return overflow_finding("candidate_total");
    }
  }
  // A zero total (no bins, or every bin zero) has no meaningful proportion
  // -- treated as a total of 1 so "0/0" degrades to "always agrees" rather
  // than a division that never happens anyway (this file computes no
  // division at all, only cross-multiplication).
  const std::int64_t a_total = baseline_total > 0 ? baseline_total : 1;
  const std::int64_t b_total = candidate_total > 0 ? candidate_total : 1;

  // Union of both bin-name sets in one merge pass -- a single map keyed by
  // bin name, each side's count accumulated directly (never a nested scan
  // of one side's bins per entry of the other, which is what would make
  // this quadratic).
  std::map<std::string, std::pair<std::int64_t, std::int64_t>> combined;
  for (const auto& bin : baseline_hist.bins) {
    auto& slot = combined[bin.first].first;
    if (!detail::checked_add(slot, bin.second, &slot)) {
      return overflow_finding("combined baseline bin total");
    }
  }
  for (const auto& bin : candidate_hist.bins) {
    auto& slot = combined[bin.first].second;
    if (!detail::checked_add(slot, bin.second, &slot)) {
      return overflow_finding("combined candidate bin total");
    }
  }

  std::string worst_bin;
  std::int64_t worst_num = 0;    // |a*B - b*A| for the worst bin so far
  std::int64_t worst_denom = 1;  // A*B for that same bin
  bool any_bin = false;

  for (const auto& [name, side_counts] : combined) {
    const std::int64_t a_count = side_counts.first;
    const std::int64_t b_count = side_counts.second;
    std::int64_t diff_lhs = 0;
    std::int64_t diff_rhs = 0;
    if (!detail::checked_mul(a_count, b_total, &diff_lhs) || !detail::checked_mul(b_count, a_total, &diff_rhs)) {
      return overflow_finding("diff_num cross-product (bin '" + name + "')");
    }
    std::int64_t diff_num = 0;  // over a_total*b_total
    if (!detail::checked_sub(diff_lhs, diff_rhs, &diff_num)) {
      return overflow_finding("diff_num subtraction (bin '" + name + "')");
    }
    std::int64_t abs_diff_num = 0;
    if (diff_num < 0) {
      if (!detail::checked_negate(diff_num, &abs_diff_num)) {
        return overflow_finding("abs_diff_num (bin '" + name + "')");
      }
    } else {
      abs_diff_num = diff_num;
    }
    std::int64_t denom = 0;
    if (!detail::checked_mul(a_total, b_total, &denom)) {
      return overflow_finding("denom (a_total * b_total, bin '" + name + "')");
    }
    // abs_diff_num/denom > worst_num/worst_denom, cross-multiplied.
    std::int64_t lhs_cmp = 0;
    std::int64_t rhs_cmp = 0;
    if (!detail::checked_mul(abs_diff_num, worst_denom, &lhs_cmp) ||
        !detail::checked_mul(worst_num, denom, &rhs_cmp)) {
      return overflow_finding("worst-bin comparison cross-product (bin '" + name + "')");
    }
    if (!any_bin || lhs_cmp > rhs_cmp) {
      worst_num = abs_diff_num;
      worst_denom = denom;
      worst_bin = name;
      any_bin = true;
    }
  }

  if (!any_bin) {
    finding.status = Status::pass;
    finding.message = "no bins on either side";
    return finding;
  }

  // worst_num/worst_denom <= tolerance.num/(tolerance.den*100) (dist's
  // tolerance is a percent-of-proportion threshold) <=>
  // worst_num*tolerance.den*100 <= tolerance.num*worst_denom.
  std::int64_t tolerance_lhs = 0;
  if (!detail::checked_mul(worst_num, tolerance->den, &tolerance_lhs) ||
      !detail::checked_mul(tolerance_lhs, 100, &tolerance_lhs)) {
    return overflow_finding("tolerance comparison lhs (worst_num * tolerance_den * 100)");
  }
  std::int64_t tolerance_rhs = 0;
  if (!detail::checked_mul(tolerance->num, worst_denom, &tolerance_rhs)) {
    return overflow_finding("tolerance comparison rhs (tolerance_num * worst_denom)");
  }
  const bool within_tolerance = tolerance_lhs <= tolerance_rhs;

  if (within_tolerance) {
    finding.status = Status::pass;
    finding.message = fmt::format("worst bin '{}' within tolerance", worst_bin);
    return finding;
  }

  finding.status = escalate(finding.severity);
  finding.message = fmt::format("worst bin '{}' exceeds tolerance", worst_bin);
  return finding;
}

}  // namespace mediadiff
