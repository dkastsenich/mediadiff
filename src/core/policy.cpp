#include "core/policy.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/glob.h"

namespace mediadiff {

namespace {

// Builds one config/CLI layer provenance detail string for a severity
// write, e.g. "mediadiff.toml [severity] entry 2, glob meta.*" or
// "--set meta.*=fail, argv position 4". 1-based positions in the rendered
// text (entry/position "1" for the first one written) -- doc 01's own
// worked example in 02-06-PLAN.md Task 3 uses 1-based "argv position 4",
// and file_order/argv_index are both 0-based counters internally.
std::string config_glob_detail(std::string_view section, std::string_view glob, int file_order) {
  return "mediadiff.toml " + std::string(section) + " entry " + std::to_string(file_order + 1) + ", glob " +
         std::string(glob);
}

std::string config_override_glob_detail(std::string_view path_glob, std::string_view section, std::string_view glob,
                                         int file_order) {
  return "mediadiff.toml [override.\"" + std::string(path_glob) + "\"] " + std::string(section) + " entry " +
         std::to_string(file_order + 1) + ", glob " + std::string(glob);
}

std::string cli_detail(std::string_view flag, std::string_view glob, std::string_view value,
                        std::size_t argv_index) {
  return "--" + std::string(flag) + " " + std::string(glob) + "=" + std::string(value) + ", argv position " +
         std::to_string(argv_index + 1);
}

// Applies one file-ordered GlobRule vector's severity entries onto `policy`,
// each write appending a provenance entry built from `detail_of`.
template <typename DetailFn>
void apply_severity_rules(Policy& policy, const CheckRegistry& registry, const std::vector<GlobRule>& rules,
                           PolicyProvenance::Layer layer, DetailFn detail_of) {
  for (const GlobRule& rule : rules) {
    const std::optional<Severity> severity = severity_from_string(rule.value);
    if (!severity) {
      // toml_load.cpp already rejects this shape at load time (Task 1) --
      // unreachable via a real ConfigFile, kept only so this function never
      // silently no-ops on a value that somehow slipped through.
      continue;
    }
    for (std::uint32_t idx : glob_select(rule.glob, registry)) {
      apply_severity_override(policy, idx, *severity, layer, detail_of(rule));
    }
  }
}

// Same shape as apply_severity_rules, for tolerance -- returns the first
// parse_tolerance rejection encountered (there is no provenance chain to
// build for a tolerance write, see core/policy.h's own top comment).
mediadiff::expected<void, Error> apply_tolerance_rules(Policy& policy, const CheckRegistry& registry,
                                                          const std::vector<GlobRule>& rules) {
  for (const GlobRule& rule : rules) {
    for (std::uint32_t idx : glob_select(rule.glob, registry)) {
      const CheckDef& check = registry.at(idx);
      auto parsed = parse_tolerance(rule.value, check.unit);
      if (!parsed) {
        return mediadiff::unexpected(parsed.error());
      }
      apply_tolerance_override(policy, idx, *parsed);
    }
  }
  return {};
}

// Layer 4 (CLI), applied in span order -- the exact tail resolve_policy
// itself runs, extracted so resolve_policy_for_file (plan 02-11) can apply
// the SAME layer, in the SAME order, without a second copy that could
// drift from resolve_policy's own.
mediadiff::expected<void, Error> apply_cli_overrides(Policy& policy, const CheckRegistry& registry,
                                                        std::span<const CliOverride> cli_overrides) {
  for (const CliOverride& cli_override : cli_overrides) {
    for (std::uint32_t idx : glob_select(cli_override.glob, registry)) {
      if (cli_override.dimension == CliOverride::Dimension::severity) {
        // options.cpp already rejects a severity word outside the closed
        // four-word set before ever constructing a CliOverride -- this is
        // trusted, not re-validated, matching apply_severity_rules' own
        // "already validated upstream" contract for the config layer.
        const std::optional<Severity> severity = severity_from_string(cli_override.value);
        if (severity) {
          apply_severity_override(policy, idx, *severity, PolicyProvenance::Layer::cli,
                                   cli_detail("set", cli_override.glob, cli_override.value, cli_override.argv_index));
        }
      } else {
        const CheckDef& check = registry.at(idx);
        auto parsed = parse_tolerance(cli_override.value, check.unit);
        if (!parsed) {
          return mediadiff::unexpected(parsed.error());
        }
        apply_tolerance_override(policy, idx, *parsed);
      }
    }
  }
  return {};
}

}  // namespace

Severity resolve_severity(const CheckDef& check, const Policy& policy) {
  for (const ResolvedCheck& resolved : policy.per_check) {
    if (resolved.id == check.id) {
      return resolved.severity;
    }
  }
  // No per_check entry for this check -- see this function's header
  // comment: recompute the builtin+profile layers fresh, mirroring
  // resolve_policy's own per-check computation exactly.
  if (check.is_volatile) {
    return Severity::ignore;
  }
  return check.severity_for(policy.profile);
}

mediadiff::expected<Policy, Error> resolve_policy(const CheckRegistry& registry, ProfileId profile,
                                                    const std::optional<ConfigFile>& config,
                                                    std::span<const CliOverride> cli_overrides) {
  Policy policy;
  policy.profile = profile;
  policy.per_check.reserve(registry.size());

  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    const CheckDef& check = registry.at(i);
    ResolvedCheck resolved;
    resolved.id = check.id;

    if (check.is_volatile) {
      // The volatile rule is applied at this builtin layer, unconditionally
      // -- a profile's own [check.profile_severity] entry for a volatile
      // check (if one is ever declared) is deliberately never reached, so
      // "resolves to ignore in every one of the five profiles" holds
      // without an exception. Only a later (config/CLI) layer, appended via
      // apply_severity_override, can promote it (T-2-19).
      resolved.severity = Severity::ignore;
      resolved.chain.push_back(PolicyProvenance{PolicyProvenance::Layer::builtin, "volatile",
                                                 std::string(severity_to_string(resolved.severity))});
    } else {
      resolved.severity = check.default_severity;
      resolved.chain.push_back(PolicyProvenance{PolicyProvenance::Layer::builtin, "checks.def default",
                                                 std::string(severity_to_string(resolved.severity))});

      const Severity profile_severity = check.severity_for(profile);
      if (profile_severity != resolved.severity) {
        resolved.severity = profile_severity;
        resolved.chain.push_back(PolicyProvenance{PolicyProvenance::Layer::profile,
                                                    std::string(profile_to_string(profile)),
                                                    std::string(severity_to_string(resolved.severity))});
      }
    }

    const std::string_view tolerance_text = check.tolerance_for(profile);
    if (!tolerance_text.empty()) {
      auto parsed = parse_tolerance(tolerance_text, check.unit);
      if (!parsed) {
        return mediadiff::unexpected(parsed.error());
      }
      resolved.tolerance = *parsed;
    }

    policy.per_check.push_back(std::move(resolved));
  }

  // Layer 3: config. Absent entirely when no mediadiff.toml was loaded
  // (config == std::nullopt) -- doc 01 section 6's "defaults only" path.
  if (config.has_value()) {
    apply_severity_rules(policy, registry, config->severity, PolicyProvenance::Layer::config,
                          [](const GlobRule& rule) { return config_glob_detail("[severity]", rule.glob, rule.file_order); });
    auto tol_result = apply_tolerance_rules(policy, registry, config->tolerance);
    if (!tol_result) {
      return mediadiff::unexpected(tol_result.error());
    }

    // [override.*] blocks, in ascending OverrideBlock::file_order -- the
    // vector itself is already in that order (config/toml_load.cpp builds
    // it by walking the document's override table in source-position
    // order), but the sort is made explicit here rather than assumed, since
    // resolve_policy's own correctness is what doc 01 section 6's file-order
    // rule is actually about.
    std::vector<const OverrideBlock*> overrides_in_order;
    overrides_in_order.reserve(config->overrides.size());
    for (const OverrideBlock& block : config->overrides) {
      overrides_in_order.push_back(&block);
    }
    std::sort(overrides_in_order.begin(), overrides_in_order.end(),
              [](const OverrideBlock* a, const OverrideBlock* b) { return a->file_order < b->file_order; });

    for (const OverrideBlock* block : overrides_in_order) {
      apply_severity_rules(policy, registry, block->severity, PolicyProvenance::Layer::config,
                            [block](const GlobRule& rule) {
                              return config_override_glob_detail(block->path_glob, "[severity]", rule.glob, rule.file_order);
                            });
      auto override_tol_result = apply_tolerance_rules(policy, registry, block->tolerance);
      if (!override_tol_result) {
        return mediadiff::unexpected(override_tol_result.error());
      }
    }
  }

  // Layer 4: CLI, applied in span order (argv order, by CliOverride
  // construction in src/cli/options.cpp).
  auto cli_result = apply_cli_overrides(policy, registry, cli_overrides);
  if (!cli_result) {
    return mediadiff::unexpected(cli_result.error());
  }

  return policy;
}

mediadiff::expected<Policy, Error> resolve_policy_for_file(const Policy& base, const CheckRegistry& registry,
                                                              const std::optional<ConfigFile>& config,
                                                              std::string_view relative_path,
                                                              std::span<const CliOverride> cli_overrides) {
  Policy policy = base;

  if (config.has_value()) {
    std::vector<const OverrideBlock*> matching;
    for (const OverrideBlock& block : config->overrides) {
      if (glob_matches_path(block.path_glob, relative_path)) {
        matching.push_back(&block);
      }
    }
    std::sort(matching.begin(), matching.end(),
              [](const OverrideBlock* a, const OverrideBlock* b) { return a->file_order < b->file_order; });

    for (const OverrideBlock* block : matching) {
      apply_severity_rules(policy, registry, block->severity, PolicyProvenance::Layer::config,
                            [block](const GlobRule& rule) {
                              return config_override_glob_detail(block->path_glob, "[severity]", rule.glob, rule.file_order);
                            });
      auto tol_result = apply_tolerance_rules(policy, registry, block->tolerance);
      if (!tol_result) {
        return mediadiff::unexpected(tol_result.error());
      }
    }
  }

  auto cli_result = apply_cli_overrides(policy, registry, cli_overrides);
  if (!cli_result) {
    return mediadiff::unexpected(cli_result.error());
  }

  return policy;
}

void apply_severity_override(Policy& policy, std::uint32_t check_index, Severity severity,
                              PolicyProvenance::Layer layer, std::string detail) {
  ResolvedCheck& resolved = policy.per_check[check_index];
  resolved.severity = severity;
  resolved.chain.push_back(PolicyProvenance{layer, std::move(detail), std::string(severity_to_string(severity))});
}

void apply_tolerance_override(Policy& policy, std::uint32_t check_index, Tolerance tolerance) {
  policy.per_check[check_index].tolerance = std::move(tolerance);
}

}  // namespace mediadiff
