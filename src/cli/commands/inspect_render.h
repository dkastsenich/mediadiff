#pragma once

// The container-section rendering `mediadiff inspect` needs -- extracted
// out of src/cli/commands/inspect.cpp's own anonymous namespace into a
// header-only (`inline`) form, matching src/util/fs.h's own established
// header-only convention, so tests/unit/test_inspect_container_section.cpp
// (03-11-PLAN.md Task 2) can call render_inspect_text/render_inspect_json
// directly without linking the whole `inspect` command's CLI11 registration
// machinery (options parsing, config discovery, exit-code mapping) into the
// unit test executable -- src/cli/commands/inspect.cpp's own callback is
// the only caller of that machinery; this header has none of it.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "cli/provenance_render.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/serializer.h"
#include "report/model.h"
#include "util/sanitize.h"

namespace mediadiff {

// One measurement, paired with the registry index it resolved against --
// collected once per Group so both the text and JSON renderers below sort
// and iterate identically (registry declaration order, then scope kind,
// then scope index -- the same ordering src/report/model.cpp's own
// build_report_model uses for findings, applied here to raw measurements
// instead).
struct GroupEntry {
  std::uint32_t check_index;
  const Measurement* measurement;
};

inline std::vector<GroupEntry> entries_for_group(const Fingerprint& fp, const CheckRegistry& registry, Group group) {
  std::vector<GroupEntry> entries;
  for (const Measurement& m : fp.measurements) {
    const CheckDef& check = registry.at(m.check_index);
    // check.id here is a registry-defined check identifier (checks.def,
    // [a-z0-9_.]+ by ENG grammar), never file-derived text -- used only
    // for group membership comparison, not formatted into any render
    // output on this line.
    if (group_for(check.id) == group) {  // control-bytes-allow: registry check id, not rendered
      entries.push_back(GroupEntry{m.check_index, &m});
    }
  }
  std::stable_sort(entries.begin(), entries.end(), [](const GroupEntry& a, const GroupEntry& b) {
    if (a.check_index != b.check_index) {
      return a.check_index < b.check_index;
    }
    if (a.measurement->scope.kind != b.measurement->scope.kind) {
      return a.measurement->scope.kind < b.measurement->scope.kind;
    }
    return a.measurement->scope.index < b.measurement->scope.index;
  });
  return entries;
}

// A Value's canonical text form -- reuses core/serializer.h's value_to_json
// (D-08's "one canonical place a Value becomes text") rather than a second
// stringification this file would have to keep in sync with the report
// renderers' own. CR-02: serialize_value_compact, not nlohmann's own
// .dump() -- routes any embedded double through the same std::to_chars
// writer core/serializer.cpp owns, and stays single-line since this text
// is embedded inline in one "  {id} {scope}: {value}\n" row.
inline std::string value_to_text(const Value& value) { return serialize_value_compact(value_to_json(value)); }

// A Measurement's own `evidence` object rendered as compact, single-line
// text -- e.g. container.track_types' tmcd_streams/caption_streams (CONT-09:
// a lost timecode/caption track named explicitly, not merely inferred from
// a count going down), or container.track_count's per-type histogram. Uses
// nlohmann's own compact dump() rather than core/serializer.h's canonical
// std::to_chars writer: this is TEXT display for a human, not the `--json`
// determinism contract render_inspect_json below still owns, and evidence
// objects in this codebase never carry a raw double (every numeric field
// here is an int64 count or an index), so there is no float-formatting
// canonicity to preserve.
inline std::string evidence_to_text(const nlohmann::ordered_json& evidence) {
  if (evidence.is_null() || (evidence.is_object() && evidence.empty()) || (evidence.is_array() && evidence.empty())) {
    return "";
  }
  return evidence.dump();
}

// Renders every Group in kGroupOrder order: a heading, then either an
// explicit no-measurements line or one line per measurement (check id,
// scope, value, and -- when the analyzer recorded any -- its evidence),
// and -- under `verbose` -- the resolved severity chain for that check via
// the SAME shared renderer `list-checks --effective -v` and
// `compare --json -v` use (src/cli/provenance_render.h), never a second
// formatter written here. `policy.per_check` is indexed by registry
// declaration index by construction (core/policy.h's own resolve_policy
// comment), so `policy.per_check[check_index]` is a direct lookup, no
// linear scan needed.
//
// T-2-33 (03-11-PLAN.md Task 2): the check id, scope text, value text and
// evidence text are all routed through sanitize_for_display before they
// reach this string -- every one can carry (or be built from) file-derived
// bytes (a metadata tag value lives directly in a StringSet's own value
// text; a track's codec name or a chapter title likewise). This is a
// FOURTH sanitize_for_display call site beyond Task 1's three display
// render paths, added deliberately here because `inspect`'s own text
// output is itself a terminal-facing render path this plan's own action
// text names explicitly ("Route every value and message through
// sanitize_for_display") -- render_inspect_json below is NOT sanitized,
// matching src/report/json.cpp's own established reasoning: JSON already
// escapes control bytes at the wire level via core/serializer.cpp's
// serialize_document, and a second pass here would double-escape.
inline std::string render_inspect_text(const Fingerprint& fp, const CheckRegistry& registry, const Policy& policy,
                                        bool verbose) {
  std::string out;
  for (Group group : kGroupOrder) {
    out += fmt::format("{}:\n", group_to_string(group));

    const std::vector<GroupEntry> entries = entries_for_group(fp, registry, group);
    if (entries.empty()) {
      out += "  (no measurements)\n";
      continue;
    }

    for (const GroupEntry& entry : entries) {
      const CheckDef& check = registry.at(entry.check_index);
      const std::string sanitized_id = sanitize_for_display(check.id);
      const std::string sanitized_scope = sanitize_for_display(scope_to_text(entry.measurement->scope));
      // 03-04-PLAN.md Task 1: a measurement the analyzer explicitly marked
      // as not applicable here (Measurement::skip_reason != none, e.g.
      // container.chapters on an MPEG-TS input) renders its skip reason
      // instead of the (Absent -> "null") value text, so `inspect`'s
      // single-file view can distinguish "measured nothing" from "this
      // check does not apply to this file" without going through
      // compare_fingerprints at all.
      if (entry.measurement->skip_reason != SkipReason::none) {
        out += fmt::format("  {} {}: (skipped: {})\n", sanitized_id, sanitized_scope,
                            skip_reason_to_string(entry.measurement->skip_reason));
      } else {
        const std::string sanitized_value = sanitize_for_display(value_to_text(entry.measurement->value));
        out += fmt::format("  {} {}: {}\n", sanitized_id, sanitized_scope, sanitized_value);
      }
      const std::string evidence_text = evidence_to_text(entry.measurement->evidence);
      if (!evidence_text.empty()) {
        out += fmt::format("      evidence: {}\n", sanitize_for_display(evidence_text));
      }
      if (verbose && entry.check_index < policy.per_check.size()) {
        out += render_provenance_chain(policy.per_check[entry.check_index].chain, 4);
      }
    }
  }
  return out;
}

inline nlohmann::ordered_json scope_to_inspect_json(const Scope& scope) { return scope_to_text(scope); }

inline std::string render_inspect_json(const Fingerprint& fp, const CheckRegistry& registry) {
  nlohmann::ordered_json doc;
  doc["schema_version"] = fp.envelope.schema_version;
  doc["tool_version"] = fp.envelope.tool_version;
  // Task 2 (PROBE-01 completion): always present, even when empty, so a
  // caller can rely on the key's presence rather than its absence meaning
  // "no diagnostics API exists" -- mirrors ENG-14's existing
  // skip_reason-always-present discipline for findings.
  doc["diagnostics"] = fp.envelope.diagnostics;

  nlohmann::ordered_json groups = nlohmann::ordered_json::object();
  for (Group group : kGroupOrder) {
    nlohmann::ordered_json entries_json = nlohmann::ordered_json::array();
    for (const GroupEntry& entry : entries_for_group(fp, registry, group)) {
      const CheckDef& check = registry.at(entry.check_index);
      nlohmann::ordered_json entry_json{
          {"id", std::string(check.id)},  // control-bytes-allow: JSON escapes at the wire level (see comment above)
          {"scope", scope_to_inspect_json(entry.measurement->scope)},
          {"value", value_to_json(entry.measurement->value)},
      };
      // 03-04-PLAN.md Task 1: present only when the analyzer explicitly
      // marked this measurement as not applicable (see the text renderer's
      // own comment above for the full rationale) -- every pre-existing
      // `inspect --json` entry (skip_reason always SkipReason::none) stays
      // byte-identical, including tests/golden/inspect_basic.txt.
      if (entry.measurement->skip_reason != SkipReason::none) {
        entry_json["status"] = "skipped";
        entry_json["skip_reason"] = std::string(skip_reason_to_string(entry.measurement->skip_reason));
      }
      entries_json.push_back(std::move(entry_json));
    }
    groups[std::string(group_to_string(group))] = entries_json;
  }
  doc["groups"] = groups;

  // CR-02: top-level document -- routed through the same canonical
  // std::to_chars writer report/json.cpp's render_json now uses, not
  // nlohmann's own doc.dump(2).
  return serialize_document(doc);
}

}  // namespace mediadiff
