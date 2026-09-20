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
#include <map>
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

// Renders ONE measurement row (check id, scope, value or skip reason, its
// evidence line when present, and -- under `verbose` -- its resolved
// severity chain) at the given left-hand indent. Extracted out of
// render_inspect_text's own per-group loop (06-11-PLAN.md, Task 1) so the
// generic per-group renderer below and render_audio_group_text's own
// per-stream blocks share EXACTLY one sanitization/formatting path -- a
// second, independently-written row formatter is exactly how one of the
// two paths could silently stop sanitizing a value (T-2-33's own point).
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
inline std::string render_group_entry_text(const CheckRegistry& registry, const GroupEntry& entry,
                                            const Policy& policy, bool verbose, int indent) {
  std::string out;
  const std::string pad(static_cast<std::size_t>(indent), ' ');
  const CheckDef& check = registry.at(entry.check_index);
  const std::string sanitized_id = sanitize_for_display(check.id);
  const std::string sanitized_scope = sanitize_for_display(scope_to_text(entry.measurement->scope));
  // 03-04-PLAN.md Task 1: a measurement the analyzer explicitly marked
  // as not applicable here (Measurement::skip_reason != none, e.g.
  // container.chapters on an MPEG-TS input) renders its skip reason
  // instead of the (Absent -> "null") value text, so `inspect`'s
  // single-file view can distinguish "measured nothing" from "this
  // check does not apply to this file" without going through
  // compare_fingerprints at all. This is also 06-11-PLAN.md's own
  // "never blank, always an explicit reason" contract for a
  // decode-dependent row under `--no-content` (SkipReason::requires_decode
  // renders identically to every other skip reason here -- a decode row
  // that was never attempted is exactly as informative as any other
  // "measured nothing, and here is why" row this renderer already prints).
  if (entry.measurement->skip_reason != SkipReason::none) {
    out += fmt::format("{}{} {}: (skipped: {})\n", pad, sanitized_id, sanitized_scope,
                        skip_reason_to_string(entry.measurement->skip_reason));
  } else {
    const std::string sanitized_value = sanitize_for_display(value_to_text(entry.measurement->value));
    out += fmt::format("{}{} {}: {}\n", pad, sanitized_id, sanitized_scope, sanitized_value);
  }
  const std::string evidence_text = evidence_to_text(entry.measurement->evidence);
  if (!evidence_text.empty()) {
    out += fmt::format("{}    evidence: {}\n", pad, sanitize_for_display(evidence_text));
  }
  if (verbose && entry.check_index < policy.per_check.size()) {
    out += render_provenance_chain(policy.per_check[entry.check_index].chain, indent + 2);
  }
  return out;
}

// The audio group's own bespoke TEXT rendering (06-11-PLAN.md, Task 1,
// ROADMAP SC1): one block per audio STREAM in ascending stream-index
// order -- every check id at that scope index, in registry order -- rather
// than the generic per-group loop's check-major ordering (every group's id
// across every stream first, then the next id). SC1 is written in terms of
// what a user SEES on one audio track at a time, and the sanitize/skip
// mechanics are identical to the generic loop (render_group_entry_text is
// the SAME function both paths call) -- only the GROUPING differs here.
//
// content.audio.sample_hash deliberately stays OUT of this block: it
// belongs to Group::content by group_for's own first-dot-segment rule
// (`content.audio.sample_hash`, not `audio.*`), renders in its own
// `content:` section exactly as it always has, and Test 6's own
// registry-enumerated coverage assertion only requires it to appear
// SOMEWHERE in the rendered output -- which it already does, unchanged.
//
// The "no audio stream at all" case (Test 5) is detected from the
// MEASUREMENTS THEMSELVES, never from a hand-maintained fixture/id list:
// every audio.* analyzer's own `!any_audio` branch (stream_params.cpp,
// priming.cpp, loudness.cpp, silence.cpp) emits exactly one
// SkipReason::insufficient_data measurement per id at Scope{audio, 0} when
// no real audio stream exists -- a genuinely different skip reason from
// SkipReason::partial_scan (a truncated scan, which stays visible as
// per-id skip lines rather than being folded into this single line, since
// "the scan didn't finish" is a different fact from "there is no audio
// here"). If every entry in the group carries that ONE sentinel skip
// reason, this renders a single explicit line instead of one skip line per
// id -- and a FUTURE audio id that forgets its own !any_audio sentinel (or
// uses a different skip reason for it) simply falls through to the normal
// per-stream block rendering instead of silently joining this line, which
// is the fail-visible direction.
inline std::string render_audio_group_text(const Fingerprint& fp, const CheckRegistry& registry, const Policy& policy,
                                            bool verbose) {
  std::string out;
  const std::vector<GroupEntry> entries = entries_for_group(fp, registry, Group::audio);
  if (entries.empty()) {
    // Matches every other group's own "(no measurements)" convention
    // (see render_inspect_text below) for the case this group has no
    // registered ids at all reporting anything -- distinct from the
    // "no audio streams" sentinel below, which requires every id to be
    // PRESENT and uniformly skipped, not simply absent.
    out += "  (no measurements)\n";
    return out;
  }

  bool all_no_audio_sentinel = true;
  for (const GroupEntry& entry : entries) {
    if (entry.measurement->skip_reason != SkipReason::insufficient_data) {
      all_no_audio_sentinel = false;
      break;
    }
  }
  if (all_no_audio_sentinel) {
    out += "  (no audio streams)\n";
    return out;
  }

  // Group by stream index (Scope::index), preserving `entries`' own
  // check-index-ascending order within each bucket -- entries_for_group
  // already sorts (check_index, scope.kind, scope.index), so pushing into
  // per-index buckets IN THAT ITERATION ORDER yields check-index-ascending
  // rows inside each bucket for free, with no secondary sort needed.
  // std::map keeps the buckets themselves in ascending stream-index order.
  std::map<int, std::vector<GroupEntry>> by_index;
  for (const GroupEntry& entry : entries) {
    by_index[entry.measurement->scope.index].push_back(entry);
  }

  for (const auto& [index, stream_entries] : by_index) {
    out += fmt::format("  audio[{}]:\n", index);
    for (const GroupEntry& entry : stream_entries) {
      out += render_group_entry_text(registry, entry, policy, verbose, /*indent=*/4);
    }
  }
  return out;
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
// Group::audio is the one exception (06-11-PLAN.md, ROADMAP SC1): it
// renders through render_audio_group_text's own per-stream blocks instead
// of this loop's check-major ordering -- see that function's own comment.
// Every other group's rendering, sanitization and skip-reason handling is
// completely unchanged.
inline std::string render_inspect_text(const Fingerprint& fp, const CheckRegistry& registry, const Policy& policy,
                                        bool verbose) {
  std::string out;
  for (Group group : kGroupOrder) {
    out += fmt::format("{}:\n", group_to_string(group));

    if (group == Group::audio) {
      out += render_audio_group_text(fp, registry, policy, verbose);
      continue;
    }

    const std::vector<GroupEntry> entries = entries_for_group(fp, registry, group);
    if (entries.empty()) {
      out += "  (no measurements)\n";
      continue;
    }

    for (const GroupEntry& entry : entries) {
      out += render_group_entry_text(registry, entry, policy, verbose, /*indent=*/2);
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
