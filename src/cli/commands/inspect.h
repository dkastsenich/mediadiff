#pragma once

#include <CLI/CLI.hpp>

namespace mediadiff {

// Registers the `inspect` subcommand (REPORT-07, UC8): reads a single
// *.snap.json and renders every implemented check family exactly once, in
// src/report/model.h's fixed Group order, honouring `--json` for a
// machine consumer and `-v` for the resolved severity chain (via the same
// shared src/cli/provenance_render.h renderer `list-checks --effective -v`
// and `compare --json -v` already use). No probe layer exists until Phase
// 3, so a real media file is refused with the same
// ErrorKind::input_unsupported message `snapshot` already gives.
void register_inspect_command(CLI::App& app);

}  // namespace mediadiff
