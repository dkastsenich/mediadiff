#include "cli/commands/inspect.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "cli/commands/inspect_render.h"
#include "cli/exit_code.h"
#include "cli/options.h"
#include "config/toml_load.h"
#include "core/error.h"
#include "core/model.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"

namespace mediadiff {

void register_inspect_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("inspect", "Render every implemented check family for a single *.snap.json (UC8)");

  // ->type_name("TEXT"): see src/cli/options.cpp's add_policy_flags for the
  // fully worked rationale (D-05).
  CLI::Option* file_path = cmd->add_option("file", "A *.snap.json to inspect")->type_name("TEXT")->required();

  CliOptions options = add_common_options(*cmd);

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `inspect` path permitted to call
  // std::exit() directly. Capturing a raw Option* by value is exactly as
  // safe as the shared_ptr it replaces (D-05): the App owns the Option
  // for the whole program lifetime, and this callback only runs during
  // app.parse().
  cmd->callback([file_path, options]() {
    const CheckRegistry& registry = builtin_registry();

    // Policy resolution, through the SAME resolve_policy sequence
    // compare/list-checks already run (T-2-23) -- never a parallel
    // reimplementation -- so `inspect -v`'s chain can never drift from
    // what those two surfaces would show for the same check. Loaded
    // before fingerprint_input runs so --probe-timeout's own `[probe]
    // timeout_seconds` config fallback can be resolved and applied first.
    const std::string config_path_text = opt_string(options.policy.config_path);
    const std::optional<std::string> explicit_config_path =
        config_path_text.empty() ? std::nullopt : std::make_optional(config_path_text);
    auto config = discover_and_load(explicit_config_path);
    if (!config) {
      const Error& err = config.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto probe_timeout_ms = resolve_probe_timeout_ms(options.probe, *config);
    if (!probe_timeout_ms) {
      const Error& err = probe_timeout_ms.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    if (probe_timeout_ms->has_value()) {
      set_default_wall_clock_budget_ms(**probe_timeout_ms);
    }

    // D-01: `inspect` always resolves to a thread count of 1 (a single
    // file) -- derive_per_file_cap_bytes with threads=1 hands it the
    // whole resolved budget.
    auto probe_budget_mb = resolve_probe_memory_budget_mb(options.probe, *config);
    if (!probe_budget_mb) {
      const Error& err = probe_budget_mb.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    set_default_packet_scan_max_bytes(derive_per_file_cap_bytes(*probe_budget_mb * 1024 * 1024, /*threads=*/1));

    auto fp = fingerprint_input(opt_string(file_path), registry);
    if (!fp) {
      const Error& err = fp.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto cli_overrides = parse_cli_overrides(opt_strings(options.policy.set_flags), opt_strings(options.policy.tol_flags));
    if (!cli_overrides) {
      const Error& err = cli_overrides.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    auto profile = resolve_profile_selection(opt_string(options.policy.profile), *config);
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

    const bool json_requested = opt_flag(options.report.json_option);
    const std::string out = json_requested
                                 ? render_inspect_json(*fp, registry)
                                 : render_inspect_text(*fp, registry, *resolved_policy, opt_flag(options.verbose));
    std::fputs(out.c_str(), stdout);
    std::exit(kExitClean);
  });
}

}  // namespace mediadiff
