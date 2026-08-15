#include "cli/commands/compare.h"

#include <cstddef>
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
#include "report/junit.h"
#include "report/markdown.h"
#include "report/model.h"
#include "util/fs.h"

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

// Opens `path` through util/fs.h's fopen_utf8 — the only permitted
// file-open path (ENG-16, this file's own boundary comment), writes
// `content` in full and closes it. Every report destination gets its own
// independent handle: no two writers ever share a buffer.
mediadiff::expected<void, Error> write_report_file(const std::string& path, const std::string& content) {
  FILE* handle = fopen_utf8(path, "wb");
  if (handle == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::usage, "could not open report destination for writing: " + path});
  }
  const std::size_t written = std::fwrite(content.data(), 1, content.size(), handle);
  const bool close_ok = std::fclose(handle) == 0;
  if (written != content.size() || !close_ok) {
    return mediadiff::unexpected(Error{ErrorKind::usage, "failed writing report destination: " + path});
  }
  return {};
}

}  // namespace

void register_compare_command(CLI::App& app) {
  auto* cmp = app.add_subcommand("compare", "Compare two artifacts (or snapshots)");

  auto baseline_path = std::make_shared<std::string>();
  auto candidate_path = std::make_shared<std::string>();
  cmp->add_option("baseline", *baseline_path, "Baseline artifact or *.snap.json")->required();
  cmp->add_option("candidate", *candidate_path, "Candidate artifact or *.snap.json")->required();

  auto strict_flag = std::make_shared<bool>(false);
  auto verbose_flag = std::make_shared<bool>(false);
  cmp->add_flag("--strict", *strict_flag, "A worst-warn finding also fails the run (exit 2)");
  cmp->add_flag("-v,--verbose", *verbose_flag, "Under --json, also render each finding's severity_chain");

  ReportArgs report_args = add_report_flags(*cmp);
  PolicyArgs policy_args = add_policy_flags(*cmp);

  // ENG-16 explicitly reserves exit()/stdout/stderr as "the CLI's
  // prerogative" — this callback is the one place in the compare path
  // permitted to call std::exit() directly, since it lives under src/cli/
  // (outside scripts/lint_eng16.sh's scanned subtrees).
  cmp->callback([baseline_path, candidate_path, strict_flag, verbose_flag, report_args, policy_args]() {
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

    // Report destinations, parsed and cross-checked BEFORE the (possibly
    // expensive) compare itself: a malformed --report argument or a path
    // collision is a usage error the user should see immediately, not
    // after paying for a compare whose report never gets written.
    auto report_destinations = parse_report_destinations(*report_args.report_flags);
    if (!report_destinations) {
      const Error& err = report_destinations.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const bool json_requested = report_args.json_option->count() > 0;
    const bool json_to_file = json_requested && !report_args.json_path->empty();
    if (json_to_file) {
      for (const ReportDestination& dest : *report_destinations) {
        if (dest.path == *report_args.json_path) {
          std::fputs(("mediadiff: --json and --report name the same path '" + dest.path + "'\n").c_str(), stderr);
          std::exit(kExitUsage);
        }
      }
    }

    auto compare_result = compare_fingerprints(*baseline, *candidate, policy, registry);
    if (!compare_result) {
      const Error& err = compare_result.error();
      std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
      std::exit(exit_code_for(err.kind));
    }
    const std::vector<Finding>& findings = *compare_result;

    // One ReportModel, shared by every renderer this run needs (D-08's
    // write-side principle applied to the read side, this plan's own
    // objective) — built once, RenderOptions defaulted to "hide nothing"
    // since none of JSON/Markdown/JUnit filter pass/ignored findings out
    // of their own output (only a later TTY renderer does).
    const RenderOptions render_options{
        /*show_pass=*/true, /*show_ignored=*/true, /*ascii=*/false, /*strict=*/*strict_flag};
    const ReportModel model = build_report_model(candidate->envelope, findings, registry, render_options);

    if (json_requested) {
      const std::string report = render_json(model, registry, policy, *verbose_flag);
      if (json_to_file) {
        auto write_result = write_report_file(*report_args.json_path, report);
        if (!write_result) {
          const Error& err = write_result.error();
          std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
          std::exit(exit_code_for(err.kind));
        }
      } else {
        std::fputs(report.c_str(), stdout);
      }
    }

    for (const ReportDestination& dest : *report_destinations) {
      std::string rendered;
      switch (dest.kind) {
        case ReportDestination::Kind::md:
          rendered = render_markdown(model, registry, *strict_flag);
          break;
        case ReportDestination::Kind::junit:
          rendered = render_junit(model, registry, *strict_flag);
          break;
      }
      auto write_result = write_report_file(dest.path, rendered);
      if (!write_result) {
        const Error& err = write_result.error();
        std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr);
        std::exit(exit_code_for(err.kind));
      }
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
