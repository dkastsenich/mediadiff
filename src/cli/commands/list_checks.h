#pragma once

#include <CLI/CLI.hpp>

namespace mediadiff {

// Registers the `list-checks` subcommand (doc 01 sections 4/6, ENG-06,
// ENG-12): without `--effective` it dumps the registry itself, in
// declaration order; with `--effective` it resolves the full policy through
// the SAME resolve_policy sequence `compare` uses -- never a parallel
// reimplementation, so the dump can never drift from what compare actually
// applies (T-2-23) -- and prints each check's resolved severity/tolerance,
// followed under `-v` by its full provenance chain (src/cli/provenance_render.h).
// Registered here because it is the policy dump this phase's `-v` display
// centers on; the remaining subcommands (`snapshot`, `dir`, `inspect`,
// `explain`) are wired in plan 02-10.
void register_list_checks_command(CLI::App& app);

}  // namespace mediadiff
