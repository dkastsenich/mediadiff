#include "report/model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace mediadiff {

Group group_for(std::string_view check_id) {
  const std::size_t dot = check_id.find('.');
  const std::string_view first = dot == std::string_view::npos ? check_id : check_id.substr(0, dot);
  if (first == "container") return Group::container;
  if (first == "video") return Group::video;
  if (first == "timeline") return Group::timeline;
  if (first == "audio") return Group::audio;
  if (first == "content") return Group::content;
  if (first == "size") return Group::size;
  // Includes a literal "meta" first segment, and every unrecognized one
  // (this plan's own must_have: an unrecognized first segment maps to
  // `meta` rather than being dropped).
  return Group::meta;
}

std::string_view group_to_string(Group group) {
  switch (group) {
    case Group::container:
      return "container";
    case Group::video:
      return "video";
    case Group::timeline:
      return "timeline";
    case Group::audio:
      return "audio";
    case Group::content:
      return "content";
    case Group::size:
      return "size";
    case Group::meta:
      return "meta";
  }
  // Unreachable for any valid Group -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "meta";
}

bool is_gating(Severity severity) { return severity == Severity::warn || severity == Severity::fail; }

std::string scope_to_text(const Scope& scope) {
  if (scope.kind == Scope::Kind::global) {
    return "global";
  }
  std::string_view kind_name;
  switch (scope.kind) {
    case Scope::Kind::global:
      kind_name = "global";
      break;
    case Scope::Kind::video:
      kind_name = "video";
      break;
    case Scope::Kind::audio:
      kind_name = "audio";
      break;
    case Scope::Kind::subtitle:
      kind_name = "subtitle";
      break;
    case Scope::Kind::data:
      kind_name = "data";
      break;
    case Scope::Kind::program:
      kind_name = "program";
      break;
  }
  return std::string(kind_name) + "[" + std::to_string(scope.index) + "]";
}

namespace {

std::size_t group_slot(Group group) {
  for (std::size_t i = 0; i < std::size(kGroupOrder); ++i) {
    if (kGroupOrder[i] == group) {
      return i;
    }
  }
  // Unreachable: kGroupOrder enumerates every Group value.
  return std::size(kGroupOrder) - 1;
}

// Accumulates one finding into a Summary: the Status count, plus
// worst_gating tracked as the maximum Severity seen so far (Severity's own
// declaration order -- ignore < info < warn < fail, core/registry.h -- is
// exactly the ordering "worst" means here).
void accumulate(Summary& summary, const Finding& finding) {
  switch (finding.status) {
    case Status::pass:
      ++summary.pass;
      break;
    case Status::info:
      ++summary.info;
      break;
    case Status::warn:
      ++summary.warn;
      break;
    case Status::fail:
      ++summary.fail;
      break;
    case Status::skipped:
      ++summary.skipped;
      break;
    case Status::error:
      ++summary.error;
      break;
  }
  if (static_cast<int>(finding.severity) > static_cast<int>(summary.worst_gating)) {
    summary.worst_gating = finding.severity;
  }
}

bool is_hidden(const Finding& finding, const RenderOptions& options) {
  if (!options.show_pass && finding.status == Status::pass) {
    return true;
  }
  if (!options.show_ignored && finding.severity == Severity::ignore) {
    return true;
  }
  return false;
}

// One finding's sort key within its own group: registry declaration index
// first (a finding whose id the registry does not recognize -- only ever a
// hand-built unit-test fixture -- sorts after every resolved finding, via
// the maximum representable index), then scope (Scope::Kind then
// Scope::index), matching TRUST-05's own pairing-key ordering in
// compare/engine.cpp.
struct SortKey {
  std::uint32_t registry_index;
  Scope::Kind scope_kind;
  int scope_index;
  std::size_t encounter_index;  // stable tiebreaker, never actually reached
                                 // since no two real findings share
                                 // (check_index, scope) by construction
};

std::vector<std::string> flatten_diagnostics(const nlohmann::ordered_json& diagnostics) {
  std::vector<std::string> lines;
  if (!diagnostics.is_object()) {
    return lines;
  }
  for (auto it = diagnostics.begin(); it != diagnostics.end(); ++it) {
    lines.push_back(it.key() + ": " + it.value().dump());
  }
  return lines;
}

}  // namespace

ReportModel build_report_model(const Envelope& envelope, std::span<const Finding> findings,
                                const CheckRegistry& registry, const RenderOptions& options) {
  ReportModel model;
  model.envelope = envelope;
  model.diagnostics = flatten_diagnostics(envelope.diagnostics);

  model.groups.reserve(std::size(kGroupOrder));
  for (Group group : kGroupOrder) {
    model.groups.push_back(GroupBlock{group, Summary{}, {}});
  }

  // Per-group (finding, sort key) pairs, collected before sorting/filtering
  // so counting (which must see every finding, hidden or not) happens
  // exactly once, up front.
  std::vector<std::vector<std::pair<SortKey, const Finding*>>> per_group(std::size(kGroupOrder));

  for (std::size_t i = 0; i < findings.size(); ++i) {
    const Finding& finding = findings[i];
    accumulate(model.summary, finding);

    const std::size_t slot = group_slot(group_for(finding.id));
    accumulate(model.groups[slot].counts, finding);

    std::uint32_t registry_index = std::numeric_limits<std::uint32_t>::max();
    if (auto idx = registry.find(finding.id)) {
      registry_index = *idx;
    }
    per_group[slot].push_back(
        {SortKey{registry_index, finding.scope.kind, finding.scope.index, i}, &finding});
  }

  for (std::size_t slot = 0; slot < per_group.size(); ++slot) {
    auto& entries = per_group[slot];
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      if (a.first.registry_index != b.first.registry_index) {
        return a.first.registry_index < b.first.registry_index;
      }
      if (a.first.scope_kind != b.first.scope_kind) {
        return a.first.scope_kind < b.first.scope_kind;
      }
      if (a.first.scope_index != b.first.scope_index) {
        return a.first.scope_index < b.first.scope_index;
      }
      return a.first.encounter_index < b.first.encounter_index;
    });
    for (const auto& [key, finding] : entries) {
      (void)key;
      if (!is_hidden(*finding, options)) {
        model.groups[slot].findings.push_back(*finding);
      }
    }
  }

  return model;
}

}  // namespace mediadiff
