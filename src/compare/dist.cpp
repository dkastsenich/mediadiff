#include "compare/semantics.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>

#include <fmt/format.h>

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

  static const Histogram kEmptyHistogram;
  const auto* baseline_hist_ptr = std::get_if<Histogram>(&baseline.value);
  const auto* candidate_hist_ptr = std::get_if<Histogram>(&candidate.value);
  const Histogram& baseline_hist = baseline_hist_ptr != nullptr ? *baseline_hist_ptr : kEmptyHistogram;
  const Histogram& candidate_hist = candidate_hist_ptr != nullptr ? *candidate_hist_ptr : kEmptyHistogram;

  std::int64_t baseline_total = 0;
  for (const auto& bin : baseline_hist.bins) {
    baseline_total += bin.second;
  }
  std::int64_t candidate_total = 0;
  for (const auto& bin : candidate_hist.bins) {
    candidate_total += bin.second;
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
    combined[bin.first].first += bin.second;
  }
  for (const auto& bin : candidate_hist.bins) {
    combined[bin.first].second += bin.second;
  }

  std::string worst_bin;
  std::int64_t worst_num = 0;    // |a*B - b*A| for the worst bin so far
  std::int64_t worst_denom = 1;  // A*B for that same bin
  bool any_bin = false;

  for (const auto& [name, side_counts] : combined) {
    const std::int64_t a_count = side_counts.first;
    const std::int64_t b_count = side_counts.second;
    const std::int64_t diff_num = a_count * b_total - b_count * a_total;  // over a_total*b_total
    const std::int64_t abs_diff_num = diff_num < 0 ? -diff_num : diff_num;
    const std::int64_t denom = a_total * b_total;
    // abs_diff_num/denom > worst_num/worst_denom, cross-multiplied.
    if (!any_bin || abs_diff_num * worst_denom > worst_num * denom) {
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
  const bool within_tolerance = worst_num * tolerance->den * 100 <= tolerance->num * worst_denom;

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
