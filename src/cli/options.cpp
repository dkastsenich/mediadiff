#include "cli/options.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

ReportArgs add_report_flags(CLI::App& cmd) {
  ReportArgs args;
  args.json_path = std::make_shared<std::string>();
  args.report_flags = std::make_shared<std::vector<std::string>>();

  args.json_option = cmd.add_option("--json", *args.json_path,
                                     "Render the report as JSON: bare '--json' writes stdout, "
                                     "'--json=PATH' writes PATH")
                          ->expected(0, 1);
  cmd.add_option("--report", *args.report_flags,
                  "Write a file-bound report: '--report md=PATH' or '--report junit=PATH' (repeatable)");

  return args;
}

namespace {

// Same first-`=` split shape as options.cpp's own split_override, applied
// to `--report`'s `<kind>=<path>` grammar instead of `--set`/`--tol`'s
// `<glob>=<value>`.
std::optional<std::pair<std::string, std::string>> split_report_flag(std::string_view raw) {
  const std::size_t eq = raw.find('=');
  if (eq == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view kind = raw.substr(0, eq);
  const std::string_view path = raw.substr(eq + 1);
  if (kind.empty() || path.empty()) {
    return std::nullopt;
  }
  return std::make_pair(std::string(kind), std::string(path));
}

}  // namespace

mediadiff::expected<std::vector<ReportDestination>, Error> parse_report_destinations(
    const std::vector<std::string>& report_flags) {
  std::vector<ReportDestination> destinations;
  destinations.reserve(report_flags.size());

  for (const std::string& raw : report_flags) {
    auto split = split_report_flag(raw);
    if (!split) {
      return mediadiff::unexpected(Error{
          ErrorKind::usage,
          "malformed --report argument '" + raw + "' (expected <kind>=<path>, with both sides non-empty)"});
    }
    auto& [kind_text, path] = *split;

    ReportDestination::Kind kind;
    if (kind_text == "md") {
      kind = ReportDestination::Kind::md;
    } else if (kind_text == "junit") {
      kind = ReportDestination::Kind::junit;
    } else {
      return mediadiff::unexpected(Error{ErrorKind::usage, "--report '" + raw + "': unrecognized report kind '" +
                                                                 kind_text + "' (expected md or junit)"});
    }
    destinations.push_back(ReportDestination{kind, std::move(path)});
  }

  for (std::size_t i = 0; i < destinations.size(); ++i) {
    for (std::size_t j = i + 1; j < destinations.size(); ++j) {
      if (destinations[i].path == destinations[j].path) {
        return mediadiff::unexpected(
            Error{ErrorKind::usage, "two --report flags name the same path '" + destinations[i].path + "'"});
      }
    }
  }

  return destinations;
}

ColorArgs add_color_flags(CLI::App& cmd) {
  ColorArgs args;
  args.no_color = std::make_shared<bool>(false);
  args.ascii = std::make_shared<bool>(false);

  cmd.add_flag("--no-color", *args.no_color, "Disable ANSI colour output regardless of environment");
  cmd.add_flag("--ascii", *args.ascii,
               "Use ASCII status words (OK/WARN/FAIL/INFO) instead of Unicode glyphs");

  return args;
}

PolicyArgs default_policy_args() {
  PolicyArgs args;
  args.profile = std::make_shared<std::string>();
  args.config_path = std::make_shared<std::string>();
  args.set_flags = std::make_shared<std::vector<std::string>>();
  args.tol_flags = std::make_shared<std::vector<std::string>>();
  return args;
}

ReportArgs default_report_args() {
  ReportArgs args;
  args.json_path = std::make_shared<std::string>();
  args.json_option = nullptr;
  args.report_flags = std::make_shared<std::vector<std::string>>();
  return args;
}

ColorArgs default_color_args() {
  ColorArgs args;
  args.no_color = std::make_shared<bool>(false);
  args.ascii = std::make_shared<bool>(false);
  return args;
}

CliOptions add_common_options(CLI::App& cmd) {
  CliOptions options;
  options.policy = add_policy_flags(cmd);
  options.report = add_report_flags(cmd);
  options.color = add_color_flags(cmd);
  options.strict = std::make_shared<bool>(false);
  options.quiet = std::make_shared<bool>(false);
  options.verbose = std::make_shared<bool>(false);

  cmd.add_flag("--strict", *options.strict, "A worst-warn finding also fails the run (exit 2)");
  cmd.add_flag("-q,--quiet", *options.quiet, "Suppress non-error output");
  cmd.add_flag("-v,--verbose", *options.verbose,
               "Also render each finding's severity_chain / resolution chain");

  return options;
}

namespace {

// Reads one environment variable, distinguishing "unset" (nullopt) from
// "set to the empty string" (a value holding "") -- std::getenv already
// makes that distinction (nullptr vs a pointer to ""), this just carries
// it into ColorInputs's own optional-string shape.
std::optional<std::string> read_env(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

// The one place mediadiff calls isatty (POSIX) / _isatty (MSVC) -- neither
// name is permitted outside this function, matching src/util/fs.h's own
// "confine platform-specific I/O primitives to one file" convention for a
// different primitive.
bool stdout_is_tty() {
#ifdef _WIN32
  return _isatty(_fileno(stdout)) != 0;
#else
  return isatty(fileno(stdout)) != 0;
#endif
}

}  // namespace

ColorInputs read_color_inputs(const ColorArgs& args) {
  ColorInputs inputs;
  inputs.stdout_is_tty = stdout_is_tty();
  inputs.no_color = read_env("NO_COLOR");
  inputs.ci = read_env("CI");
  inputs.github_actions = read_env("GITHUB_ACTIONS");
  inputs.flag_no_color = *args.no_color;
  inputs.flag_ascii = *args.ascii;
  return inputs;
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
