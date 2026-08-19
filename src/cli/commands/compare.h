#pragma once

#include <string>

#include <CLI/CLI.hpp>

#include "cli/options.h"

namespace mediadiff {

// Registers the `compare` subcommand (two positionals, --json[=path],
// --report kind=path (repeatable), --strict, -q, -v, --no-color, --ascii)
// on `app`. Kept in its own translation unit per src/cli/main.cpp's own
// convention of staying thin — main.cpp calls this rather than inlining
// the subcommand's construction and callback.
void register_compare_command(CLI::App& app);

// The compare execution itself -- shared by the `compare` subcommand's own
// callback and main.cpp's implicit two-positional dispatch (02-10-PLAN.md
// Task 1, CLI-01), so "mediadiff a b" and "mediadiff compare a b" run
// through the exact same code, never a parallel copy. `report_args`/
// `policy_args`/`color_args` carry the same shared-storage shape
// add_report_flags/add_policy_flags/add_color_flags produce; a caller with
// no CLI::App to register them on (main.cpp's implicit route) passes
// src/cli/options.h's default_report_args()/default_policy_args()/
// default_color_args() instead. Exits the process directly (ENG-16 --
// exit()/stdout/stderr are the CLI's prerogative) and therefore never
// returns.
[[noreturn]] void run_compare(const std::string& baseline_path, const std::string& candidate_path, bool strict,
                               bool verbose, bool quiet, const ReportArgs& report_args, const PolicyArgs& policy_args,
                               const ColorArgs& color_args);

}  // namespace mediadiff
