#include "cli/commands/list_checks.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "cli/exit_code.h"
#include "cli/options.h"
#include "cli/provenance_render.h"
#include "config/toml_load.h"
#include "core/error.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"

namespace mediadiff {

namespace {

std::string_view semantic_to_string(Semantic semantic) {
  switch (semantic) {
    case Semantic::exact:
      return "exact";
    case Semantic::tol:
      return "tol";
    case Semantic::set:
      return "set";
    case Semantic::presence:
      return "presence";
    case Semantic::hash:
      return "hash";
    case Semantic::dist:
      return "dist";
    case Semantic::span:
      return "span";
  }
  // Unreachable for any valid Semantic -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "exact";
}

std::string_view value_kind_to_string(ValueKind kind) {
  switch (kind) {
    case ValueKind::int64:
      return "int64";
    case ValueKind::rational:
      return "rational";
    case ValueKind::real:
      return "real";
    case ValueKind::string:
      return "string";
    case ValueKind::string_set:
      return "string_set";
    case ValueKind::histogram:
      return "histogram";
    case ValueKind::span_list:
      return "span_list";
    case ValueKind::hash_chain:
      return "hash_chain";
  }
  return "string";
}

// Tolerance carries no original source text (core/tolerance.h parses it
// into an exact rational, on purpose, so display is free to render however
// is clearest -- there is no round-trip contract to preserve here). Renders
// as an exact fraction plus the check's unit suffix, e.g. "5/1 ms", or
// "<none>" when the check declares no tolerance at all.
std::string render_tolerance(const std::optional<Tolerance>& tolerance) {
  if (!tolerance.has_value()) {
    return "<none>";
  }
  std::string text = fmt::format("{}/{} {}", tolerance->num, tolerance->den, unit_suffix(tolerance->unit));
  if (tolerance->warn_num.has_value()) {
    text += fmt::format(" (warn {}/{})", *tolerance->warn_num, tolerance->den);
  }
  return text;
}

void register_list_checks_flags_and_callback(CLI::App& cmd) {
  auto effective_flag = std::make_shared<bool>(false);
  auto verbose_flag = std::make_shared<bool>(false);
  cmd.add_flag("--effective", *effective_flag,
               "Resolve and print the full policy (profile/config/CLI merged) instead of the bare registry");
  cmd.add_flag("-v,--verbose", *verbose_flag, "Under --effective, also print each check's resolution chain");

  PolicyArgs policy_args = add_policy_flags(cmd);

  cmd.callback([effective_flag, verbose_flag, policy_args]() {
    const CheckRegistry& registry = builtin_registry();

    if (!*effective_flag) {
      std::string out;
      for (std::uint32_t i = 0; i < registry.size(); ++i) {
        const CheckDef& check = registry.at(i);
        out += fmt::format("{}  group={}  semantic={}  unit={}  value_kind={}  severity={}\n", check.id, check.group,
                            semantic_to_string(check.semantic), unit_suffix(check.unit),
                            value_kind_to_string(check.value_kind), severity_to_string(check.default_severity));
      }
      std::fputs(out.c_str(), stdout);
      std::exit(kExitClean);
    }

    // --effective: the SAME resolve_policy sequence compare.cpp's callback
    // runs (T-2-23) -- discover_and_load once, parse_cli_overrides once,
    // resolve_profile_selection, then resolve_policy itself. No parallel
    // reimplementation of any of these steps.
    const std::optional<std::string> explicit_config_path =
        policy_args.config_path->empty() ? std::nullopt : std::make_optional(*policy_args.config_path);
    auto config = discover_and_load(explicit_config_path);
    if (!config) {
      const Error& err = config.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto cli_overrides = parse_cli_overrides(*policy_args.set_flags, *policy_args.tol_flags);
    if (!cli_overrides) {
      const Error& err = cli_overrides.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto profile = resolve_profile_selection(*policy_args.profile, *config);
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

    std::string out;
    for (std::uint32_t i = 0; i < registry.size(); ++i) {
      const CheckDef& check = registry.at(i);
      const ResolvedCheck& resolved = resolved_policy->per_check[i];
      out += fmt::format("{}  severity={}  tolerance={}\n", check.id, severity_to_string(resolved.severity),
                          render_tolerance(resolved.tolerance));
      if (*verbose_flag) {
        out += render_provenance_chain(resolved.chain, 2);
      }
    }
    std::fputs(out.c_str(), stdout);
    std::exit(kExitClean);
  });
}

}  // namespace

void register_list_checks_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("list-checks", "List registered checks, or the resolved effective policy");
  register_list_checks_flags_and_callback(*cmd);
}

}  // namespace mediadiff
