#include "compare/semantics.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <variant>

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

// StringSet is std::set, so `added`/`removed` already iterate in sorted
// order by construction -- the set semantic is order-insensitive on input
// (a StringSet cannot record insertion order at all) and this rendering is
// order-stable on output for exactly that reason (reordering a baseline
// StringSet's construction produces the identical set, hence the identical
// message).
std::string render_diff(const StringSet& added, const StringSet& removed) {
  if (added.empty() && removed.empty()) {
    return "sets match";
  }
  std::string message;
  if (!added.empty()) {
    message += "+";
    bool first = true;
    for (const auto& s : added) {
      if (!first) message += ",";
      message += s;
      first = false;
    }
  }
  if (!removed.empty()) {
    if (!message.empty()) message += " ";
    message += "-";
    bool first = true;
    for (const auto& s : removed) {
      if (!first) message += ",";
      message += s;
      first = false;
    }
  }
  return message;
}

}  // namespace

// compare_set: doc 01 section 3's `set` semantic -- pass iff the symmetric
// difference is empty. Per-check element-level ignore lists (doc 01
// section 3's "volatile metadata keys" note) are a config-layer concern
// (plan 02-05/02-06) not yet wired -- core/registry.h's CheckDef carries no
// such field today, so none is applied here.
mediadiff::expected<Finding, Error> compare_set(const CheckDef& check, const Measurement& baseline,
                                                  const Measurement& candidate, const Policy& policy) {
  Finding finding;
  finding.id = check.id;
  finding.scope = candidate.scope;
  finding.baseline = baseline.value;
  finding.candidate = candidate.value;
  finding.skip_reason = SkipReason::none;
  finding.severity = resolve_severity(check, policy);

  // D-09 already guarantees both sides hold StringSet or Absent; Absent is
  // treated as the empty set ("nothing to measure" is not the same as an
  // element list -- it is the absence of one).
  static const StringSet kEmptySet;
  const auto* baseline_set = std::get_if<StringSet>(&baseline.value);
  const auto* candidate_set = std::get_if<StringSet>(&candidate.value);
  const StringSet& b = baseline_set != nullptr ? *baseline_set : kEmptySet;
  const StringSet& c = candidate_set != nullptr ? *candidate_set : kEmptySet;

  StringSet added;
  StringSet removed;
  std::set_difference(c.begin(), c.end(), b.begin(), b.end(), std::inserter(added, added.begin()));
  std::set_difference(b.begin(), b.end(), c.begin(), c.end(), std::inserter(removed, removed.begin()));

  if (added.empty() && removed.empty()) {
    finding.status = Status::pass;
    finding.message = "sets match";
    return finding;
  }

  finding.status = escalate(finding.severity);
  finding.message = render_diff(added, removed);
  return finding;
}

}  // namespace mediadiff
