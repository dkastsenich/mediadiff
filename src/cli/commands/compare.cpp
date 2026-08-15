#include "cli/commands/compare.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "cli/exit_code.h"
#include "compare/engine.h"
#include "core/error.h"
#include "core/model.h"
#include "core/policy.h"
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

  // ENG-16 explicitly reserves exit()/stdout/stderr as "the CLI's
  // prerogative" — this callback is the one place in the compare path
  // permitted to call std::exit() directly, since it lives under src/cli/
  // (outside scripts/lint_eng16.sh's scanned subtrees).
  cmp->callback([baseline_path, candidate_path, json_flag, strict_flag]() {
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

    // Tracer scope (02-01-PLAN.md): a single fixed profile, no --profile
    // flag, no config file, no --set/--tol overrides. Plan 02-05 wires the
    // full precedence merge through this same Policy type.
    const Policy policy{ProfileId::sw_encoder};

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
