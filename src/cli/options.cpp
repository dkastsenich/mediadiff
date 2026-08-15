#include "cli/options.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/glob.h"
#include "core/registry.h"

namespace mediadiff {

PolicyArgs add_policy_flags(CLI::App& cmd) {
  PolicyArgs args;
  args.profile = std::make_shared<std::string>();
  args.config_path = std::make_shared<std::string>();
  args.set_flags = std::make_shared<std::vector<std::string>>();
  args.tol_flags = std::make_shared<std::vector<std::string>>();

  cmd.add_option("--profile", *args.profile, "Select a shipped profile (default: sw-encoder)");
  cmd.add_option("--config", *args.config_path, "Path to mediadiff.toml (default: ./mediadiff.toml if present)");
  // Deliberately no ->take_all(): that call forces a SINGLE occurrence to
  // swallow every remaining token, which is the wrong shape for a flag
  // meant to be repeated once per override. CLI11's own default behavior
  // for a std::vector<std::string>-bound option already accumulates one
  // value per occurrence, in encounter order, across as many repetitions
  // as argv contains (02-RESEARCH.md Pattern 2) -- exactly doc 01 section
  // 6's `--set`/`--tol` repeatable-flag contract, with no further
  // configuration needed.
  cmd.add_option("--set", *args.set_flags, "Override a check's severity: <glob>=<ignore|info|warn|fail>");
  cmd.add_option("--tol", *args.tol_flags, "Override a check's tolerance: <glob>=<tolerance text>");

  return args;
}

namespace {

// Splits `raw` (one `--set`/`--tol` argument's text) on its first `=` into
// (glob, value). Returns nullopt when there is no `=` at all, or when
// either side would be empty -- both are "no '='"/"empty glob"/"empty
// value" per this file's own header comment, folded into one check since
// all three produce the same diagnostic shape.
std::optional<std::pair<std::string, std::string>> split_override(std::string_view raw) {
  const std::size_t eq = raw.find('=');
  if (eq == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view glob = raw.substr(0, eq);
  const std::string_view value = raw.substr(eq + 1);
  if (glob.empty() || value.empty()) {
    return std::nullopt;
  }
  return std::make_pair(std::string(glob), std::string(value));
}

mediadiff::expected<void, Error> append_overrides(const std::vector<std::string>& flags, std::string_view flag_name,
                                                     CliOverride::Dimension dimension,
                                                     std::vector<CliOverride>& out) {
  std::size_t argv_index = 0;
  for (const std::string& raw : flags) {
    auto split = split_override(raw);
    if (!split) {
      return mediadiff::unexpected(
          Error{ErrorKind::usage, "malformed --" + std::string(flag_name) + " argument '" + raw +
                                       "' (expected <glob>=<value>, with both sides non-empty)"});
    }
    auto& [glob, value] = *split;

    auto glob_check = validate_glob(glob);
    if (!glob_check) {
      return mediadiff::unexpected(
          Error{ErrorKind::usage, "malformed --" + std::string(flag_name) + " argument '" + raw +
                                       "': " + glob_check.error().message});
    }

    if (dimension == CliOverride::Dimension::severity && !severity_from_string(value)) {
      return mediadiff::unexpected(Error{
          ErrorKind::usage, "malformed --" + std::string(flag_name) + " argument '" + raw + "': '" + value +
                                 "' is not a recognized severity (expected one of ignore, info, warn, fail)"});
    }

    out.push_back(CliOverride{dimension, std::move(glob), std::move(value), argv_index});
    ++argv_index;
  }
  return {};
}

}  // namespace

mediadiff::expected<std::vector<CliOverride>, Error> parse_cli_overrides(const std::vector<std::string>& set_flags,
                                                                            const std::vector<std::string>& tol_flags) {
  std::vector<CliOverride> overrides;
  overrides.reserve(set_flags.size() + tol_flags.size());

  auto sev_result = append_overrides(set_flags, "set", CliOverride::Dimension::severity, overrides);
  if (!sev_result) {
    return mediadiff::unexpected(sev_result.error());
  }
  auto tol_result = append_overrides(tol_flags, "tol", CliOverride::Dimension::tolerance, overrides);
  if (!tol_result) {
    return mediadiff::unexpected(tol_result.error());
  }

  return overrides;
}

mediadiff::expected<ProfileId, Error> resolve_profile_selection(const std::string& profile_flag,
                                                                    const std::optional<ConfigFile>& config) {
  if (!profile_flag.empty()) {
    auto profile = profile_from_string(profile_flag);
    if (!profile) {
      return mediadiff::unexpected(
          Error{ErrorKind::usage, "'--profile " + profile_flag + "' is not one of the five recognized profile names"});
    }
    return *profile;
  }
  if (config.has_value() && config->profile.has_value()) {
    // Already validated by discover_and_load (src/config/toml_load.cpp) at
    // load time -- trusted here, not re-rejected.
    auto profile = profile_from_string(*config->profile);
    if (profile) {
      return *profile;
    }
  }
  return kDefaultProfile;
}

}  // namespace mediadiff
