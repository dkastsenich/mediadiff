#include "compare/semantics.h"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "core/rational.h"

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

Ticks to_ticks(const RationalValue& v) { return Ticks{v.num, v.tb}; }

// WR-03: every real-decision comparison in this file (sort order,
// merge-adjacency, overlap) routes through core/rational.h's
// compare_ticks_checked, NOT compare_ticks' own sign-only overflow
// fallback -- that fallback silently treats two different same-signed
// values as equal, which is only safe for compare/tol.cpp's cosmetic
// "+"/"-" delta glyph, never for deciding whether two spans merge or
// overlap. `overflowed` is set (never cleared) the first time any
// comparison below cannot be determined without int64 overflow;
// compare_span checks it once, after every helper here has run, and
// degrades the whole comparison to Status::error instead of trusting a
// result overflow may have silently corrupted.
bool ticks_less(const RationalValue& a, const RationalValue& b, bool& overflowed) {
  const TickOrder result = compare_ticks_checked(to_ticks(a), to_ticks(b));
  if (result.overflowed) {
    overflowed = true;
    return false;
  }
  return result.order < 0;
}

bool ticks_less_equal(const RationalValue& a, const RationalValue& b, bool& overflowed) {
  const TickOrder result = compare_ticks_checked(to_ticks(a), to_ticks(b));
  if (result.overflowed) {
    overflowed = true;
    return false;
  }
  return result.order <= 0;
}

// Merges adjacent spans separated by no more than one tick, sorted by
// start first so the output -- and any later rendering built from it --
// never depends on the input's own order (TRUST-05; this is also the
// property Task 3's shuffled-SpanList test exercises). This generic engine
// layer has no real frame-rate concept of its own (02-CONTEXT.md D-10:
// Phase 2 proves the engine without media) -- "one tick" is the closest
// sound analog to doc 01 section 3's "1 frame" merge gap absent an actual
// frame duration; a later analyzer that knows its own frame rate is
// expected to encode a merge-relevant timebase into the span's own `tb`
// rather than this function inventing one. Assumes a uniform timebase
// across one measurement's own span list (adjacent spans compare `tb` for
// equality before applying the one-tick rule; a `tb` mismatch falls back
// to the always-correct, timebase-agnostic overlap-or-earlier check).
std::vector<Span> merge_spans(std::vector<Span> spans, bool& overflowed) {
  std::sort(spans.begin(), spans.end(),
            [&overflowed](const Span& a, const Span& b) { return ticks_less(a.start, b.start, overflowed); });
  std::vector<Span> merged;
  for (const Span& span : spans) {
    if (!merged.empty()) {
      Span& last = merged.back();
      const bool same_tb = last.end.tb == span.start.tb;
      bool adjacent_or_overlapping = false;
      if (same_tb) {
        // WR-03: `last.end.num + 1` is itself an unchecked int64 add on a
        // value read straight from an untrusted snapshot -- guarded the
        // same way as every other real-decision arithmetic in this fix
        // rather than left as the one bare `+1` in the file.
        std::int64_t threshold = 0;
        if (!detail::checked_add(last.end.num, 1, &threshold)) {
          overflowed = true;
        } else {
          adjacent_or_overlapping = span.start.num <= threshold;
        }
      } else {
        adjacent_or_overlapping = ticks_less_equal(span.start, last.end, overflowed);
      }
      if (adjacent_or_overlapping) {
        if (ticks_less(last.end, span.end, overflowed)) {
          last.end = span.end;
        }
        continue;
      }
    }
    merged.push_back(span);
  }
  return merged;
}

bool overlaps(const Span& a, const Span& b, bool& overflowed) {
  return ticks_less(a.start, b.end, overflowed) && ticks_less(b.start, a.end, overflowed);
}

bool overlaps_any(const Span& s, const std::vector<Span>& others, bool& overflowed) {
  for (const Span& o : others) {
    if (overlaps(s, o, overflowed)) return true;
  }
  return false;
}

}  // namespace

// compare_span: doc 01 section 3's `span` semantic -- interval algebra
// over baseline/candidate span lists. A span present in the candidate that
// does not overlap any (merged) baseline span is *introduced* and gates
// (escalates via the check's severity); a baseline span absent from the
// (merged) candidate is *removed* and is always reported at `info`,
// regardless of severity, and never gates -- doc 01 section 3's own rule.
mediadiff::expected<Finding, Error> compare_span(const CheckDef& check, const Measurement& baseline,
                                                   const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;
  finding.severity = resolve_severity(check, policy);

  static const SpanList kEmptyList;
  const auto* baseline_list_ptr = std::get_if<SpanList>(&baseline.value);
  const auto* candidate_list_ptr = std::get_if<SpanList>(&candidate.value);
  const SpanList& baseline_list = baseline_list_ptr != nullptr ? *baseline_list_ptr : kEmptyList;
  const SpanList& candidate_list = candidate_list_ptr != nullptr ? *candidate_list_ptr : kEmptyList;

  bool overflowed = false;
  const std::vector<Span> baseline_merged = merge_spans(baseline_list.spans, overflowed);
  const std::vector<Span> candidate_merged = merge_spans(candidate_list.spans, overflowed);

  std::vector<Span> introduced;
  for (const Span& c : candidate_merged) {
    if (!overlaps_any(c, baseline_merged, overflowed)) introduced.push_back(c);
  }
  std::vector<Span> removed;
  for (const Span& b : baseline_merged) {
    if (!overlaps_any(b, candidate_merged, overflowed)) removed.push_back(b);
  }

  // WR-03: compare_ticks_checked reported at least one comparison above
  // that could not be determined without int64 overflow -- every
  // introduced/removed/pass verdict computed from baseline_merged/
  // candidate_merged above may be silently wrong (a fabricated tie can
  // merge spans that should not merge, or hide/invent an overlap), so this
  // comparator returns Status::error (D-09's "never a fabricated verdict"
  // contract, mirrored from compare/engine.cpp's value_kind_mismatch and
  // CR-03's compare/tol.cpp & compare/dist.cpp fixes) rather than trusting
  // any of it.
  if (overflowed) {
    finding.status = Status::error;
    finding.message = "span comparator: a tick comparison overflowed int64_t; cannot determine a verdict";
    return finding;
  }

  if (introduced.empty() && removed.empty()) {
    finding.status = Status::pass;
    finding.message = "spans match";
    return finding;
  }

  if (!introduced.empty()) {
    finding.status = escalate(finding.severity);
    finding.message = fmt::format("+{} introduced span(s)", introduced.size());
    return finding;
  }

  // Only removed spans: always info, never gates.
  finding.status = Status::info;
  finding.message = fmt::format("-{} removed span(s)", removed.size());
  return finding;
}

}  // namespace mediadiff
