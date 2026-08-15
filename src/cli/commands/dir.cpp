#include "cli/commands/dir.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "cli/exit_code.h"
#include "cli/options.h"

namespace mediadiff {

void register_dir_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("dir", "Compare two directories of media artifacts (corpus diff, DIR-01..05)");

  auto baseline_dir = std::make_shared<std::string>();
  auto candidate_dir = std::make_shared<std::string>();
  cmd->add_option("baseline_dir", *baseline_dir, "Baseline directory")->required();
  cmd->add_option("candidate_dir", *candidate_dir, "Candidate directory")->required();

  auto threads = std::make_shared<int>(0);
  auto content_flag = std::make_shared<bool>(false);
  auto no_content_flag = std::make_shared<bool>(false);
  cmd->add_option("--threads", *threads, "Bounded worker-pool size (default: hardware concurrency)");
  cmd->add_flag("--content", *content_flag, "Enable the decode-pass content checks (opt-in for dir mode)");
  cmd->add_flag("--no-content", *no_content_flag,
                "Explicitly disable the decode-pass content checks (dir mode's own default)");

  CliOptions options = add_common_options(*cmd);

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `dir` path permitted to call
  // std::exit() directly.
  cmd->callback([baseline_dir, candidate_dir, threads, content_flag, no_content_flag, options]() {
    (void)baseline_dir;
    (void)candidate_dir;
    (void)threads;
    (void)content_flag;
    (void)no_content_flag;
    (void)options;
    // Stub (02-10-PLAN.md Task 1): `dir`-mode orchestration is plan
    // 02-11's job. Every flag above is registered now so `--help` and
    // this plan's CLI-02 surface are complete; the callback itself has
    // nothing to run yet. Reports ErrorKind::internal (exit 70) rather
    // than silently no-op'ing and exiting 0, which would be
    // indistinguishable from "compared nothing and found no
    // differences" -- also this plan's own reachable path for CLI-06's
    // internal-error exit code, until plan 02-11 replaces this body with
    // a real one and re-points that assertion at a genuine internal-error
    // path (this plan's own Flagged Assumption on CLI-06).
    std::fputs("mediadiff: 'dir' orchestration is not implemented yet (arrives in a later plan)\n", stderr);
    std::exit(kExitInternal);
  });
}

}  // namespace mediadiff
