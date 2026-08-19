#pragma once

#include <CLI/CLI.hpp>

namespace mediadiff {

// Registers the `dir` subcommand (DIR-01..05, doc 01 section 10): two
// directory positionals, `--threads`, `--content`/`--no-content`, plus the
// full common-option set (src/cli/options.h's add_common_options). The
// callback itself is a stub -- dir-mode orchestration (deterministic file
// pairing, bounded parallelism, aggregate reporting) is plan 02-11's job.
// Registering the subcommand and its flags now is what makes CLI-02's full
// six-subcommand surface observable in this plan, and gives plan 02-11 a
// callback body to fill rather than a file/flag set to invent.
void register_dir_command(CLI::App& app);

}  // namespace mediadiff
