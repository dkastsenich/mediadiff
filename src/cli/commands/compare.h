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
// `policy_args`/`color_args`/`probe_args` carry the same shared-storage
// shape add_report_flags/add_policy_flags/add_color_flags/add_probe_flags
// produce; a caller with no CLI::App to register them on (main.cpp's
// implicit route) passes src/cli/options.h's default_report_args()/
// default_policy_args()/default_color_args()/default_probe_args() instead.
// Exits the process directly (ENG-16 -- exit()/stdout/stderr are the CLI's
// prerogative) and therefore never returns.
// 06-01-PLAN.md Task 2 (AUDIO-10): `content_enabled` governs whether the
// probe layer's audio decode pass runs for this invocation --
// `compare`'s own default is true (main.cpp's implicit two-positional
// route passes true unconditionally, matching the `compare` subcommand's
// own unflagged default).
// 06-05-PLAN.md (AUDIO-09): `hash_decoder` is the already-resolved
// `--hash-decoder` value ("auto", "default", or a decoder NAME --
// src/cli/options.h's resolve_hash_decoder) -- main.cpp's implicit
// two-positional route passes "auto" unconditionally, matching the
// `compare` subcommand's own unflagged default.
// 07-04-PLAN.md (CONTENT-03, D-08): `sample_stride` is the already-resolved
// `--sample N` (src/cli/options.h's resolve_sample_stride) -- main.cpp's
// implicit two-positional route passes 1 (full) unconditionally, matching the
// `compare` subcommand's own unflagged default.
// 07-10-PLAN.md (CONTENT-08): `quality` is the already-resolved `--psnr` /
// `--ssim` (src/cli/options.h's resolve_quality_request) -- main.cpp's implicit
// two-positional route passes the empty request, matching `compare`'s unflagged
// default (both checks report skipped:not_requested).
[[noreturn]] void run_compare(const std::string& baseline_path, const std::string& candidate_path, bool strict,
                               bool verbose, bool quiet, bool content_enabled, const std::string& hash_decoder,
                               int sample_stride, const QualityRequest& quality, const ReportArgs& report_args,
                               const PolicyArgs& policy_args, const ColorArgs& color_args,
                               const ProbeArgs& probe_args);

}  // namespace mediadiff
