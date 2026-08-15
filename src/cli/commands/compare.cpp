#include "cli/commands/compare.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cli/exit_code.h"
#include "cli/options.h"
#include "compare/engine.h"
#include "config/toml_load.h"
#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "report/json.h"

namespace mediadiff {

namespace {

// The single worst Status across all findings, used to pick the exit code
// (doc 00 section 3.1's "<3 vs >=64" contract). `fail` outranks everything
// else this plan can produce — the tracer's `exact` semantic only ever
// emits pass/info/warn/fail, but the ordering already accounts for
// info/skipped/error so a later plan adding a semantic that can emit them
// does not have to touch this function.
Status worst_status(const std::vector<Finding>& findings) {
  Status worst = Status::pass;
  for (const Finding& f : findings) {
    if (f.status == Status::fail) {
      return Status::fail;  // nothing outranks fail; short-circuit
    }
    if (f.status == Status::warn) {
      worst = Status::warn;
    }
  }
  return worst;
}

}  // namespace

void register_compare_command(CLI::App& app) {
  auto* cmp = app.add_subcommand("compare", "Compare two artifacts (or snapshots)");

  auto baseline_path = std::make_shared<std::string>();
  auto candidate_path = std::make_shared<std::string>();
  cmp->add_option("baseline", *baseline_path, "Baseline artifact or *.snap.json")->required();
  cmp->add_option("candidate", *candidate_path, "Candidate artifact or *.snap.json")->required();

  auto json_flag = std::make_shared<bool>(false);
  auto strict_flag = std::make_shared<bool>(false);
  cmp->add_flag("--json", *json_flag, "Render the report as JSON on stdout");
  cmp->add_flag("--strict", *strict_flag, "A worst-warn finding also fails the run (exit 2)");

  PolicyArgs policy_args = add_policy_flags(*cmp);

  // ENG-16 explicitly reserves exit()/stdout/stderr as "the CLI's
  // prerogative" — this callback is the one place in the compare path
  // permitted to call std::exit() directly, since it lives under src/cli/
  // (outside scripts/lint_eng16.sh's scanned subtrees).
  cmp->callback([baseline_path, candidate_path, json_flag, strict_flag, policy_args]() {
    const CheckRegistry& registry = builtin_registry();

    auto baseline = read_snapshot(*baseline_path, registry);
    if (!baseline) {
      const Error& err = baseline.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    auto candidate = read_snapshot(*candidate_path, registry);
    if (!candidate) {
      const Error& err = candidate.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }

    // Doc 01 section 6: mediadiff.toml is read exactly once here, before
    // any worker starts (a `dir` run's later plan reuses this same
    // resolved Policy per file rather than re-reading the config).
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
    // The `[transform]` block's declared expectation is read here, once,
    // rather than inside resolve_policy itself -- compare/exact.cpp reads
    // Policy::transform_expectation at comparison time exactly as it
    // already does for a hand-built one (plan 02-05).
    if (config->has_value() && (*config)->transform.has_value()) {
      resolved_policy->transform_expectation.resolution = (*config)->transform->resolution;
    }
    const Policy& policy = *resolved_policy;

    auto compare_result = compare_fingerprints(*baseline, *candidate, policy, registry);
    if (!compare_result) {
      const Error& err = compare_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const std::vector<Finding>& findings = *compare_result;

    if (*json_flag) {
      const std::string report = render_json(findings, candidate->envelope, registry);
      std::fputs(report.c_str(), stdout);
    }

    const Status worst = worst_status(findings);
    if (worst == Status::fail) {
      std::exit(kExitFail);
    }
    if (worst == Status::warn && *strict_flag) {
      std::exit(kExitWarnStrict);
    }
    std::exit(kExitClean);
  });
}

}  // namespace mediadiff
