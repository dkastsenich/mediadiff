#include "compare/semantics.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "core/frame_pairing.h"
#include "core/rational.h"

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

// 07-03-PLAN.md (CONTENT-02, D-07): the time-aligned FRAME locator for video
// chains (element_stride == 1: one digest per frame, one PTS per frame).
//
// Frames are lined up by presentation time with src/core/frame_pairing.h's one
// D-02 rule (each side's own first frame, strictly less than half the finer
// interval, exact rationals, index fallback when a side has no usable
// timestamps or an unknown interval), so a frame dropped mid-file reports as
// "missing from candidate" and every later frame still lines up -- only
// genuinely changed frames count as differing. The VERDICT is unchanged (the
// caller has already decided the chains differ); this is only the report.
//
// Derived from HashChain::block_digests / element_ticks / element_tb and the
// two sides' `timestamps` / `frame_interval` evidence alone, so a live compare
// and a snapshot compare produce identical evidence (Phase 6 D-03).

// Every evidence list is capped so the report stays bounded and deterministic
// on a pathological alternating-difference stream (T-07-11); the totals are
// always exact and `locator_truncated` says when a cap was hit.
constexpr std::size_t kMaxLocatorRanges = 64;

struct FrameRange {
  std::int64_t first = 0;  // baseline index of the first differing frame
  std::int64_t last = 0;   // baseline index of the last differing frame
  std::int64_t differing = 0;
  std::optional<Rational> start_time;  // seconds from the baseline's first frame, exact
  std::optional<Rational> end_time;
};

struct IndexRange {
  std::int64_t first = 0;
  std::int64_t last = 0;
};

// Contiguous unpaired indices merge into one ascending range; only the first
// kMaxLocatorRanges ranges are kept, while the totals keep counting.
struct IndexRangeList {
  std::vector<IndexRange> kept;
  std::int64_t total_ranges = 0;
  std::int64_t total_frames = 0;
  bool truncated = false;

  void add(std::int64_t index) {
    ++total_frames;
    if (open.has_value() && open->last + 1 == index) {
      open->last = index;
      return;
    }
    flush();
    open = IndexRange{index, index};
  }
  void finish() { flush(); }

 private:
  void flush() {
    if (!open.has_value()) {
      return;
    }
    ++total_ranges;
    if (kept.size() < kMaxLocatorRanges) {
      kept.push_back(*open);
    } else {
      truncated = true;
    }
    open.reset();
  }
  std::optional<IndexRange> open;
};

struct FrameDivergence {
  bool available = false;
  PairingMode mode = PairingMode::time;
  std::string fallback_reason;

  bool has_first = false;
  std::int64_t first_baseline_index = -1;
  std::int64_t first_candidate_index = -1;
  std::optional<std::int64_t> first_pts_value;  // the baseline's own tick
  Rational first_pts_tb{0, 0};                   // ... in the baseline's own timebase
  std::optional<Rational> first_time;           // seconds from the baseline's first frame

  std::vector<FrameRange> ranges;  // capped at kMaxLocatorRanges
  std::int64_t range_count = 0;    // exact
  std::int64_t differing_count = 0;

  IndexRangeList missing_from_candidate;  // baseline indices
  IndexRangeList extra_in_candidate;      // candidate indices

  bool truncated = false;
};

// The side's `frame_interval` evidence ({num, den} seconds); zero, negative or
// missing means unknown (07-01: an MPEG-TS stream reports {0, 0}).
Rational frame_interval_of(const nlohmann::ordered_json& evidence) {
  if (!evidence.is_object()) {
    return Rational{0, 0};
  }
  const auto it = evidence.find("frame_interval");
  if (it == evidence.end() || !it->is_object() || !it->contains("num") || !it->contains("den") ||
      !it->at("num").is_number_integer() || !it->at("den").is_number_integer()) {
    return Rational{0, 0};
  }
  return Rational{it->at("num").get<std::int64_t>(), it->at("den").get<std::int64_t>()};
}

bool timestamps_unusable(const nlohmann::ordered_json& evidence) {
  if (!evidence.is_object()) {
    return false;
  }
  const auto it = evidence.find("timestamps");
  return it != evidence.end() && it->is_string() && it->get_ref<const std::string&>() == "unusable";
}

FrameSeries series_of(const HashChain& chain, const nlohmann::ordered_json& evidence) {
  FrameSeries series;
  if (!timestamps_unusable(evidence)) {
    series.ticks = std::span<const std::int64_t>(chain.element_ticks);
  }
  series.tb = chain.element_tb;
  series.interval = frame_interval_of(evidence);
  series.frame_count = chain.block_digests.size();
  return series;
}

// (ticks[index] - ticks[0]) * tb as a reduced exact Rational of seconds, or
// nullopt when the side has no usable ticks or the value does not fit int64
// (hostile snapshot values only; a missing time is never a fabricated one).
std::optional<Rational> frame_time_of(const FrameSeries& series, std::int64_t index) {
  if (series.ticks.size() != series.frame_count || series.frame_count == 0 || series.tb.num <= 0 ||
      series.tb.den <= 0 || index < 0 || static_cast<std::size_t>(index) >= series.ticks.size()) {
    return std::nullopt;
  }
  std::int64_t relative = 0;
  std::int64_t numerator = 0;
  if (!detail::checked_sub(series.ticks[static_cast<std::size_t>(index)], series.ticks[0], &relative) ||
      !detail::checked_mul(relative, series.tb.num, &numerator) ||
      numerator == std::numeric_limits<std::int64_t>::min()) {
    return std::nullopt;
  }
  if (numerator == 0) {
    return Rational{0, 1};
  }
  const std::int64_t divisor = std::gcd(numerator, series.tb.den);
  return Rational{numerator / divisor, series.tb.den / divisor};
}

FrameDivergence compute_frame_divergence(const HashChain& baseline, const HashChain& candidate,
                                         const nlohmann::ordered_json& baseline_evidence,
                                         const nlohmann::ordered_json& candidate_evidence) {
  FrameDivergence report;
  const FrameSeries baseline_series = series_of(baseline, baseline_evidence);
  const FrameSeries candidate_series = series_of(candidate, candidate_evidence);
  const PairingResult pairing = pair_frames(baseline_series, candidate_series);
  report.mode = pairing.mode;
  report.fallback_reason = pairing.fallback_reason;

  // CONTENT-02 adjacency: differing frames merge into one range over the
  // PAIRED sequence when at most ONE matching pair separates them; two or more
  // matching pairs end the range.
  bool open = false;
  FrameRange current;
  std::int64_t paired_position = 0;
  std::int64_t last_differing_position = 0;
  auto close_range = [&]() {
    if (!open) {
      return;
    }
    ++report.range_count;
    if (report.ranges.size() < kMaxLocatorRanges) {
      report.ranges.push_back(current);
    } else {
      report.truncated = true;
    }
    open = false;
  };

  for (const PairEvent& event : pairing.events) {
    switch (event.kind) {
      case PairEventKind::paired: {
        const auto baseline_index = static_cast<std::size_t>(event.baseline_index);
        const auto candidate_index = static_cast<std::size_t>(event.candidate_index);
        if (baseline.block_digests[baseline_index] != candidate.block_digests[candidate_index]) {
          ++report.differing_count;
          if (!report.has_first) {
            report.has_first = true;
            report.first_baseline_index = event.baseline_index;
            report.first_candidate_index = event.candidate_index;
            report.first_time = frame_time_of(baseline_series, event.baseline_index);
            if (report.first_time.has_value()) {
              report.first_pts_value = baseline_series.ticks[baseline_index];
              report.first_pts_tb = baseline_series.tb;
            }
          }
          if (open && paired_position - last_differing_position - 1 <= 1) {
            current.last = event.baseline_index;
            ++current.differing;
            current.end_time = frame_time_of(baseline_series, event.baseline_index);
          } else {
            close_range();
            open = true;
            current = FrameRange{};
            current.first = event.baseline_index;
            current.last = event.baseline_index;
            current.differing = 1;
            current.start_time = frame_time_of(baseline_series, event.baseline_index);
            current.end_time = current.start_time;
          }
          last_differing_position = paired_position;
        }
        ++paired_position;
        break;
      }
      case PairEventKind::baseline_only:
        report.missing_from_candidate.add(event.baseline_index);
        break;
      case PairEventKind::candidate_only:
        report.extra_in_candidate.add(event.candidate_index);
        break;
    }
  }
  close_range();
  report.missing_from_candidate.finish();
  report.extra_in_candidate.finish();
  report.truncated = report.truncated || report.missing_from_candidate.truncated ||
                     report.extra_in_candidate.truncated;
  report.available = report.differing_count > 0 || report.missing_from_candidate.total_frames > 0 ||
                     report.extra_in_candidate.total_frames > 0;
  return report;
}

nlohmann::ordered_json rational_json(const Rational& value) {
  return nlohmann::ordered_json{{"num", value.num}, {"den", value.den}};
}

nlohmann::ordered_json index_ranges_json(const std::vector<IndexRange>& ranges) {
  nlohmann::ordered_json out = nlohmann::ordered_json::array();
  for (const IndexRange& range : ranges) {
    out.push_back(nlohmann::ordered_json{{"first", range.first}, {"last", range.last}});
  }
  return out;
}

// The ONLY place milliseconds appear: rendered from the exact time, never
// stored in evidence.
double to_milliseconds(const Rational& seconds) {
  return static_cast<double>(seconds.num) * 1000.0 / static_cast<double>(seconds.den);
}

void write_frame_divergence(const FrameDivergence& divergence, Finding& finding) {
  nlohmann::ordered_json& evidence = finding.evidence;
  evidence["pairing"] = divergence.mode == PairingMode::time ? "time" : "index";
  if (divergence.mode == PairingMode::index) {
    evidence["pairing_fallback"] = divergence.fallback_reason;
  }
  if (divergence.has_first) {
    nlohmann::ordered_json first{{"baseline_index", divergence.first_baseline_index},
                                 {"candidate_index", divergence.first_candidate_index}};
    if (divergence.first_pts_value.has_value() && divergence.first_time.has_value()) {
      // The PTS is in the baseline's own timebase; `time` is the exact seconds
      // from its first frame.
      first["pts"] = nlohmann::ordered_json{{"value", *divergence.first_pts_value}, {"tb", rational_json(divergence.first_pts_tb)}};
      first["time"] = rational_json(*divergence.first_time);
    }
    evidence["first_divergent_frame"] = std::move(first);
  }
  nlohmann::ordered_json ranges = nlohmann::ordered_json::array();
  for (const FrameRange& range : divergence.ranges) {
    nlohmann::ordered_json entry{{"first", range.first}, {"last", range.last}, {"differing", range.differing}};
    if (range.start_time.has_value() && range.end_time.has_value()) {
      entry["start_time"] = rational_json(*range.start_time);
      entry["end_time"] = rational_json(*range.end_time);
    }
    ranges.push_back(std::move(entry));
  }
  evidence["divergent_ranges"] = std::move(ranges);
  evidence["divergent_range_count"] = divergence.range_count;
  evidence["differing_frame_count"] = divergence.differing_count;
  evidence["missing_from_candidate"] = index_ranges_json(divergence.missing_from_candidate.kept);
  evidence["missing_from_candidate_count"] = divergence.missing_from_candidate.total_frames;
  evidence["extra_in_candidate"] = index_ranges_json(divergence.extra_in_candidate.kept);
  evidence["extra_in_candidate_count"] = divergence.extra_in_candidate.total_frames;
  evidence["locator_truncated"] = divergence.truncated;
}

std::string frame_divergence_message(const FrameDivergence& divergence) {
  std::vector<std::string> parts;
  if (divergence.differing_count > 0) {
    std::string part;
    if (divergence.differing_count == 1) {
      part = fmt::format("frame {} differs", divergence.first_baseline_index);
      if (divergence.first_time.has_value()) {
        part += fmt::format(" ({:.1f} ms)", to_milliseconds(*divergence.first_time));
      }
    } else {
      if (divergence.range_count == 1) {
        const FrameRange& range = divergence.ranges.front();
        part = fmt::format("frames {}-{} differ ({} frames)", range.first, range.last, range.differing);
      } else {
        part = fmt::format("{} frames differ across {} ranges", divergence.differing_count, divergence.range_count);
      }
      part += fmt::format(", first at frame {}", divergence.first_baseline_index);
      if (divergence.first_time.has_value()) {
        part += fmt::format(" ({:.1f} ms)", to_milliseconds(*divergence.first_time));
      }
    }
    parts.push_back(std::move(part));
  }
  auto unpaired_part = [](const IndexRangeList& list, std::string_view side) {
    if (list.total_frames == 1) {
      return fmt::format("frame {} {}", list.kept.front().first, side);
    }
    return fmt::format("{} frames {}, first at frame {}", list.total_frames, side, list.kept.front().first);
  };
  if (divergence.missing_from_candidate.total_frames > 0) {
    parts.push_back(unpaired_part(divergence.missing_from_candidate, "missing from candidate"));
  }
  if (divergence.extra_in_candidate.total_frames > 0) {
    parts.push_back(unpaired_part(divergence.extra_in_candidate, "extra in candidate"));
  }
  if (divergence.mode == PairingMode::index) {
    parts.push_back(fmt::format("frames paired by decode order ({})", divergence.fallback_reason));
  }
  if (divergence.truncated) {
    parts.push_back(fmt::format("lists truncated at {} ranges", kMaxLocatorRanges));
  }
  std::string message = "hash mismatch -- ";
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      message += "; ";
    }
    message += parts[i];
  }
  return message;
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

// The evidence's `sampling_state` text when it is a string, else nullopt (the
// key absent, or not a string). Reads the VALUE only, never `check.id`.
std::optional<std::string_view> sampling_state_text(const nlohmann::ordered_json& evidence) {
  if (!evidence.is_object()) {
    return std::nullopt;
  }
  const auto it = evidence.find("sampling_state");
  if (it == evidence.end() || !it->is_string()) {
    return std::nullopt;
  }
  return std::string_view(it->get_ref<const std::string&>());
}

// How one side's `sampling_state` reads in the sampling_mismatch message:
// `full` is stride 1, a canonical `sampled:N` is stride N, and anything else
// (a hand-edited or hostile stored string) is quoted verbatim -- never guessed.
std::string describe_sampling(std::string_view state) {
  if (state == kSamplingStateFull) {
    return "'full' (stride 1)";
  }
  if (const std::optional<int> stride = parse_sampled_stride(state); stride.has_value()) {
    return fmt::format("'{}' (stride {})", state, *stride);
  }
  return fmt::format("'{}'", state);
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

  // 07-04-PLAN.md (CONTENT-03, D-08, research Pitfall 6): a `--sample N` chain
  // holds every Nth frame, so it is comparable only with a chain taken at the
  // same N. When the two `sampling_state` strings differ and at least one is a
  // canonical `sampled:N`, that is skipped:sampling_mismatch -- the precise
  // reason -- and NOT the generic hash_incomparable the precondition rule
  // below would report. Runs after the truncated rule (truncation wins, the
  // 06-14 ordering) and before first_precondition_mismatch. A state that does
  // not parse is never treated as sampled, so a malformed stored string falls
  // through to the generic precondition mismatch and can never become a pass
  // (T-07-14). A side with no `sampling_state` at all also falls through.
  const std::optional<std::string_view> baseline_state = sampling_state_text(baseline.evidence);
  const std::optional<std::string_view> candidate_state = sampling_state_text(candidate.evidence);
  if (baseline_state.has_value() && candidate_state.has_value() && *baseline_state != *candidate_state &&
      (parse_sampled_stride(*baseline_state).has_value() || parse_sampled_stride(*candidate_state).has_value())) {
    finding.status = Status::skipped;
    finding.skip_reason = SkipReason::sampling_mismatch;
    finding.message = fmt::format(
        "hash comparison skipped: 'sampling_state' differs -- baseline is {}, candidate is {}; a chain holding every "
        "Nth frame cannot be compared with one holding a different selection, so re-run both sides with the same "
        "--sample N (or without --sample)",
        describe_sampling(*baseline_state), describe_sampling(*candidate_state));
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
  // 07-03-PLAN.md (CONTENT-02, D-07): a video chain (one digest per frame,
  // element_stride == 1 on both sides) gets the time-aligned frame locator
  // instead; audio's stride is its block length, so the branch below is
  // untouched for it.
  if (baseline_chain->element_stride == 1 && candidate_chain->element_stride == 1 &&
      !baseline_chain->block_digests.empty() && !candidate_chain->block_digests.empty()) {
    const FrameDivergence frame_divergence =
        compute_frame_divergence(*baseline_chain, *candidate_chain, baseline.evidence, candidate.evidence);
    if (frame_divergence.available) {
      write_frame_divergence(frame_divergence, finding);
      finding.message = frame_divergence_message(frame_divergence);
      // 07-04-PLAN.md (D-08): both sides are at the same stride by now (an
      // unequal pair was skipped above), and every frame number in the report
      // is a STORED-frame index -- stored frame k is decode frame k * stride.
      // Said in the evidence and the message so "frame 20" is never misread as
      // the 20th decoded frame. Absent for a full chain, so no existing
      // report changes.
      const std::optional<int> stride =
          baseline_state.has_value() ? parse_sampled_stride(*baseline_state) : std::nullopt;
      if (stride.has_value()) {
        finding.evidence["sample_stride"] = *stride;
        finding.message += fmt::format("; frame numbers index the stored frames under --sample {} (decode frame = {} x number)",
                                        *stride, *stride);
      }
      return finding;
    }
  }

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
