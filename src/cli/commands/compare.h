#pragma once

#include <CLI/CLI.hpp>

namespace mediadiff {

// Registers the `compare` subcommand (two positionals, --json[=path],
// --report kind=path (repeatable), --strict, -v) on `app`. Kept in its own
// translation unit per src/cli/main.cpp's own convention of staying thin —
// main.cpp calls this rather than inlining the subcommand's construction
// and callback.
void register_compare_command(CLI::App& app);

}  // namespace mediadiff
