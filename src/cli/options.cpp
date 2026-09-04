#include "cli/options.h"

#include <cstddef>
#include <cstdint>
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
#include "core/rational.h"
#include "core/registry.h"
#include "probe/packet_scan.h"
#include "util/fs.h"

namespace mediadiff {

std::string opt_string(const CLI::Option* o) { return (o != nullptr && o->count() > 0) ? o->as<std::string>() : std::string{}; }

bool opt_flag(const CLI::Option* o) { return o != nullptr && o->count() > 0; }

std::vector<std::string> opt_strings(const CLI::Option* o) {
  // Mandatory guard -- see this function's declaration comment in
  // options.h (Landmine 1): an unguarded as<std::vector<std::string>>()
  // on an unset option returns {""} , not {}.
  return (o != nullptr && o->count() > 0) ? o->as<std::vector<std::string>>() : std::vector<std::string>{};
}

PolicyArgs add_policy_flags(CLI::App& cmd) {
  PolicyArgs args;

  // The templated add_option(name, variable, desc) overload every one of
  // these calls used before D-05 ran CLI11's own type inference off the
  // bound variable's type: it set the help-text type name ("TEXT", via
  // detail::type_name<std::string>()) and, for a vector<std::string>
  // binding, ALSO set "one value per occurrence" (type_size(1, 1)) and
  // "unlimited occurrences" (expected(detail::expected_count<vector<
  // std::string>>::value), which resolves to CLI11's
  // expected_max_vector_size). The untargeted add_option(name, desc)
  // overload used here (D-05) runs NEITHER inference -- a freshly
  // constructed Option has no type name to show (confirmed: `--profile`'s
  // help line silently dropped its "TEXT" annotation without this fix) and
  // defaults expected_min_/expected_max_ to 1, i.e. "the flag may be given
  // at most once" (confirmed: a second `--set` occurrence threw
  // ArgumentMismatch::AtMost, "At most 1 required but received 2", instead
  // of accumulating). Both are genuine behavior changes this migration
  // must not introduce, so every string-valued option below restores
  // ->type_name("TEXT") explicitly, and every repeatable one additionally
  // restores ->expected(-1, -1) -- CLI11's own public, documented
  // shorthand for "at least 1 value if given at all, unlimited
  // repetitions" (Option_inl.hpp's expected(min, max): a negative min
  // takes its absolute value, a negative max resolves to
  // expected_max_vector_size) -- deliberately NOT ->take_all(), which
  // forces a SINGLE occurrence to swallow every remaining token, the
  // wrong shape for a flag meant to be repeated once per override
  // (02-RESEARCH.md Pattern 2). This is what restores doc 01 section 6's
  // `--set`/`--tol` repeatable-flag contract, and every option's help
  // text, exactly.
  args.profile = cmd.add_option("--profile", "Select a shipped profile (default: sw-encoder)")->type_name("TEXT");
  args.config_path = cmd.add_option("--config", "Path to mediadiff.toml (default: ./mediadiff.toml if present)")
                          ->type_name("TEXT");
  args.set_flags = cmd.add_option("--set", "Override a check's severity: <glob>=<ignore|info|warn|fail>")
                        ->type_name("TEXT")
                        ->expected(-1, -1);
  args.tol_flags = cmd.add_option("--tol", "Override a check's tolerance: <glob>=<tolerance text>")
                        ->type_name("TEXT")
                        ->expected(-1, -1);

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

  // ->type_name("TEXT") on both, and ->expected(-1, -1) on --report:
  // restores CLI11's own type-inference help text and repeatable-flag
  // accumulation that the untargeted add_option(name, desc) overload does
  // not run on its own -- see add_policy_flags' own comment for the full
  // rationale, confirmed against the pinned CLI11.
  args.json_option = cmd.add_option("--json",
                                     "Render the report as JSON: bare '--json' writes stdout, "
                                     "'--json=PATH' writes PATH")
                          ->type_name("TEXT")
                          ->expected(0, 1);
  args.report_flags =
      cmd.add_option("--report", "Write a file-bound report: '--report md=PATH' or '--report junit=PATH' (repeatable)")
          ->type_name("TEXT")
          ->expected(-1, -1);

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

  args.no_color = cmd.add_flag("--no-color", "Disable ANSI colour output regardless of environment");
  args.ascii =
      cmd.add_flag("--ascii", "Use ASCII status words (OK/WARN/FAIL/INFO) instead of Unicode glyphs");

  return args;
}

ProbeArgs add_probe_flags(CLI::App& cmd) {
  ProbeArgs args;

  // D-05's typed-numeric exception: ->check() rather than a bound
  // variable, so a malformed value is a CLI::ValidationError at parse
  // time (exit 64 via main.cpp's own catch), not a runtime surprise.
  // CLI::NonNegativeNumber accepts 0 -- a 0-second budget is a valid,
  // deliberately-immediate-timeout value (this plan's own acceptance
  // criterion), not a usage error.
  // 03-12-PLAN.md Task 1 (T-3-58/T-3-60, D-05): an upper-bound
  // ->check(CLI::Range(...)) is ADDED here, chained after the existing
  // NonNegativeNumber/PositiveNumber validator rather than replacing it --
  // CLI11 runs every chained ->check() and reports the first one that
  // fails, so a zero/negative value still produces the existing message
  // unchanged, and only a value above the new ceiling gets the new one.
  // This is D-05's own parse-time-validation convention (reject before any
  // resolver runs); resolve_probe_timeout_ms/resolve_probe_memory_budget_bytes
  // below keep their own bound anyway, because the config path (`[probe]
  // timeout_seconds`/`memory_budget_mb`) never passes through CLI11 at all.
  args.timeout_seconds = cmd.add_option("--probe-timeout", "Per-file wall-clock probe budget, in seconds (default: 30)")
                              ->type_name("SECONDS")
                              ->check(CLI::NonNegativeNumber)
                              ->check(CLI::Range(std::int64_t{0}, kMaxProbeTimeoutSeconds));
  // Registered, accepted, and validated here; left unconsumed for plan
  // 03-03 to wire into D-01's global memory-budget model (03-02-PLAN.md
  // Task 2's own instruction).
  args.memory_budget_mb =
      cmd.add_option("--probe-memory-budget-mb",
                      "Global probe memory budget, in MB (accepted now; not yet consumed -- arrives with a "
                      "later plan)")
          ->type_name("MB")
          ->check(CLI::PositiveNumber)
          ->check(CLI::Range(std::int64_t{1}, kMaxProbeMemoryBudgetMb));

  return args;
}

namespace {

// 03-12-PLAN.md Task 1 (T-3-60, D-01): rejects `seconds` above
// kMaxProbeTimeoutSeconds with ErrorKind::usage, naming both the offending
// value and the bound, then converts to milliseconds through
// detail::checked_mul rather than a raw product -- an overflowed product
// here used to make a healthy file look like it had already exceeded its
// wall-clock budget (a false, self-inflicted timeout, T-3-60's own
// description), which is the exact defect this function repairs. Shared
// by both the CLI branch (below) and the config branch, which has no
// CLI11 validator in front of it at all.
mediadiff::expected<std::int64_t, Error> bound_and_convert_timeout_seconds(std::int64_t seconds,
                                                                              std::string_view source) {
  if (seconds > kMaxProbeTimeoutSeconds) {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, std::string(source) + " must not exceed " + std::to_string(kMaxProbeTimeoutSeconds) +
                                     " seconds (twenty-four hours, the maximum probe timeout): '" +
                                     std::to_string(seconds) + "'"});
  }
  std::int64_t ms = 0;
  if (!detail::checked_mul(seconds, 1000, &ms)) {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, std::string(source) + " is too large to convert to milliseconds: '" +
                                     std::to_string(seconds) + "'"});
  }
  return ms;
}

}  // namespace

mediadiff::expected<std::optional<std::int64_t>, Error> resolve_probe_timeout_ms(const ProbeArgs& args,
                                                                                    const std::optional<ConfigFile>& config) {
  if (args.timeout_seconds != nullptr && args.timeout_seconds->count() > 0) {
    const std::string text = opt_string(args.timeout_seconds);
    try {
      const long long seconds = std::stoll(text);
      auto converted = bound_and_convert_timeout_seconds(static_cast<std::int64_t>(seconds), "--probe-timeout");
      if (!converted) {
        return mediadiff::unexpected(converted.error());
      }
      return *converted;
    } catch (const std::exception&) {
      // Unreachable in practice -- ->check(CLI::NonNegativeNumber) already
      // rejected anything std::stoll could not parse, at CLI11 parse time.
      // Caught here (never crossing the lib boundary) as a defensive
      // backstop only.
      return mediadiff::unexpected(
          Error{ErrorKind::usage, "--probe-timeout must be a non-negative integer number of seconds: '" + text + "'"});
    }
  }
  if (config.has_value() && config->probe.has_value() && config->probe->timeout_seconds.has_value()) {
    auto converted = bound_and_convert_timeout_seconds(static_cast<std::int64_t>(*config->probe->timeout_seconds),
                                                          "'[probe] timeout_seconds'");
    if (!converted) {
      return mediadiff::unexpected(converted.error());
    }
    return *converted;
  }
  return std::nullopt;
}

mediadiff::expected<std::int64_t, Error> resolve_probe_memory_budget_mb(const ProbeArgs& args,
                                                                           const std::optional<ConfigFile>& config) {
  if (args.memory_budget_mb != nullptr && args.memory_budget_mb->count() > 0) {
    const std::string text = opt_string(args.memory_budget_mb);
    try {
      const long long mb = std::stoll(text);
      return static_cast<std::int64_t>(mb);
    } catch (const std::exception&) {
      // Unreachable in practice -- ->check(CLI::PositiveNumber) already
      // rejected anything std::stoll could not parse, at CLI11 parse
      // time. Caught here (never crossing the lib boundary) as a
      // defensive backstop only, matching resolve_probe_timeout_ms's own
      // shape.
      return mediadiff::unexpected(Error{
          ErrorKind::usage, "--probe-memory-budget-mb must be a positive integer number of megabytes: '" + text + "'"});
    }
  }
  if (config.has_value() && config->probe.has_value() && config->probe->memory_budget_mb.has_value()) {
    return static_cast<std::int64_t>(*config->probe->memory_budget_mb);
  }
  return static_cast<std::int64_t>(kDefaultProbeMemoryBudgetMb);
}

// 03-12-PLAN.md Task 1 (T-3-58, D-01): wraps resolve_probe_memory_budget_mb
// so the megabytes-to-bytes conversion happens in ONE place instead of at
// four command entry points, each of which used to perform its own raw
// `mb * 1024 * 1024` product with no overflow check. Both call sites that
// can feed this function (--probe-memory-budget-mb, already bounded by
// this plan's ->check(CLI::Range(...)) at parse time; `[probe]
// memory_budget_mb`, already bounded by src/config/toml_load.cpp's own
// loader-time ceiling) are pre-validated by the time they reach here --
// the bound check below is a deliberate backstop, not the primary
// enforcement point, since the config path never passes through CLI11 at
// all and a future caller of resolve_probe_memory_budget_mb should not be
// able to bypass this bound by skipping the loader.
mediadiff::expected<std::int64_t, Error> resolve_probe_memory_budget_bytes(const ProbeArgs& args,
                                                                               const std::optional<ConfigFile>& config) {
  auto mb = resolve_probe_memory_budget_mb(args, config);
  if (!mb) {
    return mediadiff::unexpected(mb.error());
  }
  if (*mb > kMaxProbeMemoryBudgetMb) {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, "probe memory budget must not exceed " + std::to_string(kMaxProbeMemoryBudgetMb) +
                                     " MB (one tebibyte, the maximum probe memory budget): '" + std::to_string(*mb) +
                                     "'"});
  }
  // Two successive checked_mul steps (MB -> KB -> bytes) rather than one
  // `*mb * 1024 * 1024` product: each step is its own overflow-checked
  // multiplication, so no intermediate value is ever produced by raw,
  // unchecked arithmetic.
  std::int64_t kb = 0;
  if (!detail::checked_mul(*mb, 1024, &kb)) {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, "probe memory budget is too large to convert to bytes: '" + std::to_string(*mb) + "'"});
  }
  std::int64_t bytes = 0;
  if (!detail::checked_mul(kb, 1024, &bytes)) {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, "probe memory budget is too large to convert to bytes: '" + std::to_string(*mb) + "'"});
  }
  return bytes;
}

PolicyArgs default_policy_args() { return {}; }

ReportArgs default_report_args() { return {}; }

ColorArgs default_color_args() { return {}; }

ProbeArgs default_probe_args() { return {}; }

CliOptions add_common_options(CLI::App& cmd) {
  CliOptions options;
  options.policy = add_policy_flags(cmd);
  options.report = add_report_flags(cmd);
  options.color = add_color_flags(cmd);
  options.probe = add_probe_flags(cmd);

  options.strict = cmd.add_flag("--strict", "A worst-warn finding also fails the run (exit 2)");
  options.quiet = cmd.add_flag("-q,--quiet", "Suppress non-error output");
  options.verbose =
      cmd.add_flag("-v,--verbose", "Also render each finding's severity_chain / resolution chain");

  return options;
}

namespace {

// Three sites in the repository call isatty (POSIX) / _isatty (MSVC): this
// one, src/cli/commands/compare.cpp:59 and src/cli/commands/dir.cpp:74 --
// this function does NOT currently satisfy src/util/fs.h's own "confine
// platform-specific I/O primitives to one file" convention for this
// primitive, unlike the environment reads this file's read_color_inputs
// now performs exclusively through mediadiff::getenv_utf8. The causal
// asymmetry is the single most useful thing to learn here: the TTY-test
// drift is compile-INVISIBLE on MSVC, because all three call sites sit
// inside non-Windows preprocessor branches, whereas the environment-read
// drift this file used to have was compile-VISIBLE, because getenv was
// called unconditionally on every platform. Same broken convention --
// only the compile-visible half ever broke the Windows build, which is
// exactly why the TTY-test drift went unnoticed for as long as the
// getenv drift did (.planning/debug/windows-getenv-c4996.md).
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
  inputs.no_color = getenv_utf8("NO_COLOR");
  inputs.ci = getenv_utf8("CI");
  inputs.github_actions = getenv_utf8("GITHUB_ACTIONS");
  inputs.flag_no_color = opt_flag(args.no_color);
  inputs.flag_ascii = opt_flag(args.ascii);
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
