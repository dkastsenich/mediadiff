#include "compare/semantics.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace mediadiff {

namespace {

// 06-01-PLAN.md (D-03): best-effort sample-rate recovery from the
// `normalization` evidence key's own "...;rate=<hz>;..." segment
// (src/analyzers/content/sample_hash.cpp is the one producer of that
// string) -- used only to render a human-readable time alongside the
// divergence report below; a missing or unparsable rate degrades to
// reporting the sample range with no time field, never a fabricated
// value.
std::optional<std::int64_t> extract_rate_hz(const nlohmann::ordered_json& evidence) {
  if (!evidence.is_object() || !evidence.contains("normalization") || !evidence.at("normalization").is_string()) {
    return std::nullopt;
  }
  const std::string& normalization = evidence.at("normalization").get_ref<const std::string&>();
  const std::string needle = "rate=";
  const std::size_t pos = normalization.find(needle);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t start = pos + needle.size();
  std::size_t end = start;
  while (end < normalization.size() && normalization[end] >= '0' && normalization[end] <= '9') {
    ++end;
  }
  if (end == start) {
    return std::nullopt;
  }
  try {
    return std::stoll(normalization.substr(start, end - start));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// D-03: the first-divergent-block locator, plus the full divergent-range
// list and total count -- derived identically whether `baseline`/
// `candidate` came from a freshly measured HashChain or one read back
// from a stored snapshot (both are the SAME HashChain::block_digests
// shape), which is what keeps media-vs-media and media-vs-snapshot
// evidence identical. Returns a default-constructed (`available == false`)
// result when either side carries no per-block array at all -- a
// HashChain written before D-04 (or by a producer that never populates
// it), in which case the caller falls back to the element-count-only
// message this comparator already had.
struct DivergenceReport {
  bool available = false;
  std::int64_t first_divergent_block = -1;
  std::int64_t first_divergent_start_sample = 0;
  std::int64_t first_divergent_end_sample = 0;
  std::optional<double> first_divergent_time_ms;
  std::vector<std::pair<std::int64_t, std::int64_t>> divergent_ranges;  // [start_block, end_block)
  std::int64_t divergent_block_count = 0;
};

DivergenceReport compute_divergence(const HashChain& baseline, const HashChain& candidate,
                                     std::optional<std::int64_t> rate_hz) {
  DivergenceReport report;
  if (baseline.block_digests.empty() || candidate.block_digests.empty()) {
    return report;
  }
  const std::int64_t stride =
      baseline.element_stride > 0 ? baseline.element_stride : candidate.element_stride;
  const std::size_t common = std::min(baseline.block_digests.size(), candidate.block_digests.size());

  bool in_run = false;
  std::int64_t run_start = 0;
  auto close_run = [&](std::int64_t end_index) {
    if (in_run) {
      report.divergent_ranges.emplace_back(run_start, end_index);
      report.divergent_block_count += (end_index - run_start);
      in_run = false;
    }
  };

  for (std::size_t i = 0; i < common; ++i) {
    const bool differs = baseline.block_digests[i] != candidate.block_digests[i];
    if (differs) {
      if (!in_run) {
        in_run = true;
        run_start = static_cast<std::int64_t>(i);
      }
      if (report.first_divergent_block < 0) {
        report.first_divergent_block = static_cast<std::int64_t>(i);
        report.first_divergent_start_sample = static_cast<std::int64_t>(i) * stride;
        report.first_divergent_end_sample = report.first_divergent_start_sample + stride;
        if (rate_hz.has_value() && *rate_hz > 0) {
          report.first_divergent_time_ms =
              static_cast<double>(report.first_divergent_start_sample) * 1000.0 / static_cast<double>(*rate_hz);
        }
      }
    } else {
      close_run(static_cast<std::int64_t>(i));
    }
  }
  close_run(static_cast<std::int64_t>(common));

  // A length mismatch beyond the common prefix is itself a divergence --
  // the longer side's tail blocks are, by construction, absent on the
  // other side.
  const std::int64_t total = static_cast<std::int64_t>(std::max(baseline.block_digests.size(), candidate.block_digests.size()));
  if (static_cast<std::int64_t>(common) < total) {
    if (report.first_divergent_block < 0) {
      report.first_divergent_block = static_cast<std::int64_t>(common);
      report.first_divergent_start_sample = static_cast<std::int64_t>(common) * stride;
      report.first_divergent_end_sample = report.first_divergent_start_sample + stride;
      if (rate_hz.has_value() && *rate_hz > 0) {
        report.first_divergent_time_ms =
            static_cast<double>(report.first_divergent_start_sample) * 1000.0 / static_cast<double>(*rate_hz);
      }
    }
    report.divergent_ranges.emplace_back(static_cast<std::int64_t>(common), total);
    report.divergent_block_count += (total - static_cast<std::int64_t>(common));
  }

  report.available = report.first_divergent_block >= 0;
  return report;
}

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

// The hash preconditions doc 01 section 3 names: "same decode-path class,
// same sampling, same normalisation" (doc 01 section 7's decode-
// determinism vocabulary). Carried as evidence keys on the Measurement
// (core/model.h's Measurement::evidence, analyzer-attached metadata)
// rather than a dedicated struct -- no analyzer exists yet to populate
// these (Phase 6+ hashing work), so this exact evidence key shape is this
// plan's own design decision (02-CONTEXT.md's "Claude's Discretion"),
// recorded in 02-04-SUMMARY.md. A future analyzer that needs a
// differently-shaped precondition record can extend this table without
// touching the pass/fail/skipped decision logic below.
constexpr std::array<std::string_view, 3> kPreconditionKeys = {"decode_path_class", "sampling_state",
                                                                 "normalization"};

// 06-14-PLAN.md (WR-02, TRUST-02): a truncated-vs-full pair already
// degrades through the ordinary kPreconditionKeys mismatch above (the two
// sides' `sampling_state` values literally disagree). What that generic
// mismatch rule CANNOT catch is truncated-vs-truncated: two independently
// stopped sweeps whose `sampling_state` values happen to AGREE (both
// kSamplingStateTruncated) and whose chains happen to match over their
// respective (different-length, differently-stopped) prefixes -- a digest
// match there cannot vouch for either side's own unread remainder, so it
// is exactly as incomparable as the mismatched case. Reads the evidence
// VALUE only, never `check.id` -- the same genericity kPreconditionKeys
// itself follows.
bool is_truncated_sampling(const nlohmann::ordered_json& evidence) {
  if (!evidence.is_object()) {
    return false;
  }
  const auto it = evidence.find("sampling_state");
  return it != evidence.end() && it->is_string() && it->get_ref<const std::string&>() == kSamplingStateTruncated;
}

// Returns the name of the first precondition key that disagrees between
// the two evidence objects, or an empty string if every key present on
// either side agrees. A key present on only one side counts as a mismatch
// too -- an undeclared precondition is never assumed to match its absence.
std::string first_precondition_mismatch(const nlohmann::ordered_json& baseline_evidence,
                                         const nlohmann::ordered_json& candidate_evidence) {
  for (std::string_view key : kPreconditionKeys) {
    const bool baseline_has = baseline_evidence.contains(key);
    const bool candidate_has = candidate_evidence.contains(key);
    if (baseline_has != candidate_has) {
      return std::string(key);
    }
    if (baseline_has && candidate_has && baseline_evidence.at(key) != candidate_evidence.at(key)) {
      return std::string(key);
    }
  }
  return "";
}

}  // namespace

// compare_hash: doc 01 section 3's `hash` semantic -- chains equal AND hash
// preconditions match. A precondition mismatch is `skipped:hash_incomparable`
// with a remediation hint, never a fabricated pass or fail (doc 01 section
// 7, T-2-17): the decode-determinism class system exists precisely so a
// class-2 cross-path comparison degrades instead of lying, and this holds
// even when the digests happen to be equal despite the precondition
// mismatch (a coincidental match under different preconditions still
// cannot be trusted).
mediadiff::expected<Finding, Error> compare_hash(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.severity = resolve_severity(check, policy);

  // 06-14-PLAN.md (WR-02, TRUST-02): checked BEFORE the ordinary
  // precondition-mismatch rule below -- a truncated side is incomparable
  // even against another truncated side whose `sampling_state` value
  // happens to agree (see is_truncated_sampling's own doc comment).
  const bool baseline_truncated = is_truncated_sampling(baseline.evidence);
  const bool candidate_truncated = is_truncated_sampling(candidate.evidence);
  if (baseline_truncated || candidate_truncated) {
    finding.status = Status::skipped;
    finding.skip_reason = SkipReason::hash_incomparable;
    const std::string_view which =
        baseline_truncated && candidate_truncated ? "both sides" : (baseline_truncated ? "the baseline" : "the candidate");
    finding.message = fmt::format(
        "hash comparison skipped: 'sampling_state' is 'truncated' on {} -- the decode stopped before the end of "
        "the stream, so a digest match cannot vouch for the unread remainder; see decode_truncation_reason and "
        "meta.decode_errors, and re-run on intact media",
        which);
    return finding;
  }

  const std::string mismatched_key = first_precondition_mismatch(baseline.evidence, candidate.evidence);
  if (!mismatched_key.empty()) {
    finding.status = Status::skipped;
    finding.skip_reason = SkipReason::hash_incomparable;
    finding.message =
        fmt::format("hash comparison skipped: '{}' precondition differs between baseline and candidate -- use a "
                     "perceptual or epsilon comparison instead",
                     mismatched_key);
    return finding;
  }
  finding.skip_reason = SkipReason::none;

  const auto* baseline_chain = std::get_if<HashChain>(&baseline.value);
  const auto* candidate_chain = std::get_if<HashChain>(&candidate.value);
  if (baseline_chain == nullptr || candidate_chain == nullptr) {
    // D-09 already guarantees both sides hold HashChain or Absent; Absent
    // on either side means there is nothing to declare equal.
    finding.status = escalate(finding.severity);
    finding.message = "hash chain present on only one side";
    return finding;
  }

  if (baseline_chain->algorithm == candidate_chain->algorithm && baseline_chain->digest == candidate_chain->digest) {
    finding.status = Status::pass;
    finding.message = "digests match";
    return finding;
  }

  finding.status = escalate(finding.severity);

  // 06-01-PLAN.md (D-03): per-block first-divergence reporting, now that
  // block_digests exists (D-04) -- derived identically whether the
  // baseline came from freshly measured media or a stored snapshot, since
  // both hand this comparator the SAME HashChain::block_digests shape.
  const std::optional<std::int64_t> rate_hz = extract_rate_hz(candidate.evidence);
  const DivergenceReport divergence = compute_divergence(*baseline_chain, *candidate_chain, rate_hz);
  if (divergence.available) {
    nlohmann::ordered_json ranges = nlohmann::ordered_json::array();
    for (const auto& [start, end] : divergence.divergent_ranges) {
      ranges.push_back(nlohmann::ordered_json{{"start_block", start}, {"end_block", end}});
    }
    finding.evidence["first_divergent_block"] = divergence.first_divergent_block;
    finding.evidence["sample_range"] = nlohmann::ordered_json{
        {"start", divergence.first_divergent_start_sample}, {"end", divergence.first_divergent_end_sample}};
    if (divergence.first_divergent_time_ms.has_value()) {
      finding.evidence["first_divergent_time_ms"] = *divergence.first_divergent_time_ms;
    }
    finding.evidence["divergent_ranges"] = ranges;
    finding.evidence["divergent_block_count"] = divergence.divergent_block_count;

    if (divergence.first_divergent_time_ms.has_value()) {
      finding.message = fmt::format(
          "digests differ -- first divergent block {} (samples [{}, {}), ~{:.1f} ms), {} divergent block(s) total",
          divergence.first_divergent_block, divergence.first_divergent_start_sample,
          divergence.first_divergent_end_sample, *divergence.first_divergent_time_ms, divergence.divergent_block_count);
    } else {
      finding.message = fmt::format("digests differ -- first divergent block {} (samples [{}, {})), {} divergent "
                                     "block(s) total",
                                     divergence.first_divergent_block, divergence.first_divergent_start_sample,
                                     divergence.first_divergent_end_sample, divergence.divergent_block_count);
    }
    return finding;
  }

  // Neither side carried a per-block array (a HashChain written before
  // D-04, or by a producer that never populates it) -- the element-count
  // message this comparator has always had.
  finding.message = fmt::format("digests differ ({} baseline element(s), {} candidate element(s))",
                                 baseline_chain->element_count, candidate_chain->element_count);
  return finding;
}

}  // namespace mediadiff
