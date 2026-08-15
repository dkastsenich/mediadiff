#include "cli/commands/inspect.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "cli/exit_code.h"
#include "cli/options.h"
#include "cli/provenance_render.h"
#include "config/toml_load.h"
#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/serializer.h"
#include "core/snapshot.h"
#include "report/model.h"

namespace mediadiff {

namespace {

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

std::vector<GroupEntry> entries_for_group(const Fingerprint& fp, const CheckRegistry& registry, Group group) {
  std::vector<GroupEntry> entries;
  for (const Measurement& m : fp.measurements) {
    const CheckDef& check = registry.at(m.check_index);
    if (group_for(check.id) == group) {
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
// renderers' own.
std::string value_to_text(const Value& value) { return value_to_json(value).dump(); }

// Renders every Group in kGroupOrder order: a heading, then either an
// explicit no-measurements line or one line per measurement (check id,
// scope, value), and -- under `verbose` -- the resolved severity chain for
// that check via the SAME shared renderer `list-checks --effective -v`
// and `compare --json -v` use (src/cli/provenance_render.h), never a
// second formatter written here. `policy.per_check` is indexed by
// registry declaration index by construction (core/policy.h's own
// resolve_policy comment), so `policy.per_check[check_index]` is a direct
// lookup, no linear scan needed.
std::string render_inspect_text(const Fingerprint& fp, const CheckRegistry& registry, const Policy& policy,
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
      out += fmt::format("  {} {}: {}\n", check.id, scope_to_text(entry.measurement->scope),
                          value_to_text(entry.measurement->value));
      if (verbose && entry.check_index < policy.per_check.size()) {
        out += render_provenance_chain(policy.per_check[entry.check_index].chain, 4);
      }
    }
  }
  return out;
}

nlohmann::ordered_json scope_to_inspect_json(const Scope& scope) { return scope_to_text(scope); }

std::string render_inspect_json(const Fingerprint& fp, const CheckRegistry& registry) {
  nlohmann::ordered_json doc;
  doc["schema_version"] = fp.envelope.schema_version;
  doc["tool_version"] = fp.envelope.tool_version;

  nlohmann::ordered_json groups = nlohmann::ordered_json::object();
  for (Group group : kGroupOrder) {
    nlohmann::ordered_json entries_json = nlohmann::ordered_json::array();
    for (const GroupEntry& entry : entries_for_group(fp, registry, group)) {
      const CheckDef& check = registry.at(entry.check_index);
      entries_json.push_back(nlohmann::ordered_json{
          {"id", std::string(check.id)},
          {"scope", scope_to_inspect_json(entry.measurement->scope)},
          {"value", value_to_json(entry.measurement->value)},
      });
    }
    groups[std::string(group_to_string(group))] = entries_json;
  }
  doc["groups"] = groups;

  return doc.dump(2);
}

}  // namespace

void register_inspect_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("inspect", "Render every implemented check family for a single *.snap.json (UC8)");

  auto file_path = std::make_shared<std::string>();
  cmd->add_option("file", *file_path, "A *.snap.json to inspect")->required();

  CliOptions options = add_common_options(*cmd);

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `inspect` path permitted to call
  // std::exit() directly.
  cmd->callback([file_path, options]() {
    const CheckRegistry& registry = builtin_registry();

    auto fp = read_snapshot(*file_path, registry);
    if (!fp) {
      const Error& err = fp.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    // Policy resolution, through the SAME resolve_policy sequence
    // compare/list-checks already run (T-2-23) -- never a parallel
    // reimplementation -- so `inspect -v`'s chain can never drift from
    // what those two surfaces would show for the same check.
    const std::optional<std::string> explicit_config_path =
        options.policy.config_path->empty() ? std::nullopt : std::make_optional(*options.policy.config_path);
    auto config = discover_and_load(explicit_config_path);
    if (!config) {
      const Error& err = config.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto cli_overrides = parse_cli_overrides(*options.policy.set_flags, *options.policy.tol_flags);
    if (!cli_overrides) {
      const Error& err = cli_overrides.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto profile = resolve_profile_selection(*options.policy.profile, *config);
    if (!profile) {
      const Error& err = profile.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto resolved_policy = resolve_policy(registry, *profile, *config, *cli_overrides);
    if (!resolved_policy) {
      const Error& err = resolved_policy.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    const bool json_requested = options.report.json_option != nullptr && options.report.json_option->count() > 0;
    const std::string out = json_requested ? render_inspect_json(*fp, registry)
                                            : render_inspect_text(*fp, registry, *resolved_policy, *options.verbose);
    std::fputs(out.c_str(), stdout);
    std::exit(kExitClean);
  });
}

}  // namespace mediadiff
