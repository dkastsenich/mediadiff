#include "core/policy.h"

#include <string>
#include <string_view>
#include <utility>

namespace mediadiff {

namespace {

std::string_view severity_name(Severity severity) {
  switch (severity) {
    case Severity::ignore:
      return "ignore";
    case Severity::info:
      return "info";
    case Severity::warn:
      return "warn";
    case Severity::fail:
      return "fail";
  }
  // Unreachable for any valid Severity -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "ignore";
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

mediadiff::expected<Policy, Error> resolve_policy(const CheckRegistry& registry, ProfileId profile) {
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
                                                 std::string(severity_name(resolved.severity))});
    } else {
      resolved.severity = check.default_severity;
      resolved.chain.push_back(PolicyProvenance{PolicyProvenance::Layer::builtin, "checks.def default",
                                                 std::string(severity_name(resolved.severity))});

      const Severity profile_severity = check.severity_for(profile);
      if (profile_severity != resolved.severity) {
        resolved.severity = profile_severity;
        resolved.chain.push_back(PolicyProvenance{PolicyProvenance::Layer::profile,
                                                    std::string(profile_to_string(profile)),
                                                    std::string(severity_name(resolved.severity))});
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

  return policy;
}

void apply_severity_override(Policy& policy, std::uint32_t check_index, Severity severity,
                              PolicyProvenance::Layer layer, std::string detail) {
  ResolvedCheck& resolved = policy.per_check[check_index];
  resolved.severity = severity;
  resolved.chain.push_back(PolicyProvenance{layer, std::move(detail), std::string(severity_name(severity))});
}

}  // namespace mediadiff
