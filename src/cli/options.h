#pragma once

// The argv-capture half of doc 01 section 6's configuration precedence
// chain: the `--profile`/`--config`/`--set`/`--tol` flags shared by every
// subcommand that resolves a Policy (`compare`, `list-checks`), and the
// pure text-parsing step that turns CLI11's own accumulated `--set`/`--tol`
// vectors into the CliOverride list core/policy.h's resolve_policy consumes
// as its layer-four argument. This file owns argv capture and syntax-only
// validation and nothing else -- discover_and_load (mediadiff.toml itself)
// and resolve_policy (the merge, including a --tol value's unit-grammar
// check against a specific check) are each subcommand's own command entry
// point's job, per core/policy.h's resolve_policy doc comment.

#include <CLI/CLI.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cli/color_policy.h"
#include "config/toml_load.h"
#include "core/error.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "util/expected.h"

namespace mediadiff {

// Shared option storage for the four policy-resolution flags, populated by
// CLI11 once `cmd->callback` fires -- the same shared_ptr-per-flag shape
// src/cli/commands/compare.cpp's pre-02-06 --json/--strict flags already
// use, so a callback lambda captures this struct by value and reads through
// each pointer once CLI11 has parsed argv.
struct PolicyArgs {
  std::shared_ptr<std::string> profile;
  std::shared_ptr<std::string> config_path;
  std::shared_ptr<std::vector<std::string>> set_flags;
  std::shared_ptr<std::vector<std::string>> tol_flags;
};

// Registers `--profile`, `--config`, `--set` (repeatable) and `--tol`
// (repeatable) on `cmd`. CLI11 accumulates a repeated `std::vector<std::string>`
// option in encounter order without `take_all()` (that call exists for a
// different accumulation shape, per 02-RESEARCH.md Pattern 2) -- this is
// what makes `set_flags`/`tol_flags` already argv-ordered once parsing
// completes, before parse_cli_overrides ever runs.
PolicyArgs add_policy_flags(CLI::App& cmd);

// Shared option storage for CLI-04's report-destination flags: `--json`
// (optionally `=path`) and repeatable `--report`. `json_option` is the raw
// CLI11 Option*, kept alongside the bound string so a caller can
// distinguish "the flag was never given" (json_option->count() == 0) from
// "the flag was given with no path, meaning stdout" (count() > 0,
// *json_path empty) -- a shared_ptr<std::string> alone cannot make that
// distinction, since both cases leave the string empty.
struct ReportArgs {
  std::shared_ptr<std::string> json_path;
  CLI::Option* json_option = nullptr;
  std::shared_ptr<std::vector<std::string>> report_flags;
};

// Registers `--json` (an optionally-valued flag: `--json` alone means
// stdout, `--json=path` writes to that file, `->expected(0, 1)` is CLI11's
// own idiom for a flag that MAY take one value) and `--report` (repeatable,
// `<kind>=<path>`) on `cmd`.
ReportArgs add_report_flags(CLI::App& cmd);

// Converts every accumulated `--set`/`--tol` argument into one argv-ordered
// CliOverride list -- severity and tolerance counted as independent
// `argv_index` sequences (CliOverride::Dimension), so interleaving `--set`
// and `--tol` in argv cannot change either dimension's own relative order.
// Each argument is split on its FIRST `=`; malformed text -- no `=`, an
// empty glob, an empty value, a `--set` severity word outside
// ignore/info/warn/fail, or a glob `validate_glob` rejects -- is
// `ErrorKind::usage` naming the offending argument text verbatim. A `--tol`
// value's own unit grammar is deliberately NOT checked here: doing so
// requires knowing which check(s) the glob resolves to and each one's
// declared Unit, which only resolve_policy (holding the registry) can
// determine -- this function's contract ends at "is this argument
// well-formed text", not "is this argument valid for some check".
mediadiff::expected<std::vector<CliOverride>, Error> parse_cli_overrides(const std::vector<std::string>& set_flags,
                                                                            const std::vector<std::string>& tol_flags);

// Doc 01 section 5's profile-selection precedence: `--profile` (when
// non-empty) wins over `mediadiff.toml`'s `profile=` (when `config` holds a
// value and declared one) wins over `kDefaultProfile`. `profile_flag`'s
// text is re-validated here even though CLI11 already ran; `config`'s
// `profile` was already validated by discover_and_load at load time, so
// that branch is trusted (`profile_from_string` is only re-called there to
// convert text back to the enum, not to re-reject it).
mediadiff::expected<ProfileId, Error> resolve_profile_selection(const std::string& profile_flag,
                                                                    const std::optional<ConfigFile>& config);

// One `--report kind=path` destination (CLI-04): `kind` is one of the two
// file-bound report formats this plan wires (`md`, `junit` -- TTY has no
// `--report` spelling since it is compare's own stdout default, and JSON's
// own `--json[=path]` flag is handled separately, not through this type),
// `path` is the text after the first `=`.
struct ReportDestination {
  enum class Kind { md, junit };
  Kind kind;
  std::string path;
};

// Parses every accumulated `--report` argument into an argv-ordered
// ReportDestination list. Each argument is split on its FIRST `=`;
// malformed text (no `=`, an empty kind, an empty path), a kind outside
// {md, junit}, or two `--report` destinations naming the same path is
// `ErrorKind::usage` naming the offending argument text verbatim -- the
// same rejection shape parse_cli_overrides already uses for `--set`/`--tol`.
// Does NOT know about `--json`'s own path (src/cli/commands/compare.cpp
// checks that collision itself, since only it holds both `--json`'s path
// and this function's return value at once).
mediadiff::expected<std::vector<ReportDestination>, Error> parse_report_destinations(
    const std::vector<std::string>& report_flags);

// Shared option storage for CLI-08's two colour-affecting flags. Mirrors
// PolicyArgs/ReportArgs's own shared_ptr-per-flag shape so a callback
// lambda captures this struct by value and reads through each pointer once
// CLI11 has parsed argv.
struct ColorArgs {
  std::shared_ptr<bool> no_color;
  std::shared_ptr<bool> ascii;
};

// Registers `--no-color` and `--ascii` on `cmd`.
ColorArgs add_color_flags(CLI::App& cmd);

// Default-valued PolicyArgs/ReportArgs/ColorArgs, with no CLI11 flags
// registered on any App -- used by main.cpp's implicit two-positional
// dispatch (CLI-01), which intentionally carries none of `compare`'s own
// optional flags: "mediadiff a b" behaves exactly like
// "mediadiff compare a b" with none of them given, dispatched through the
// SAME run_compare (src/cli/commands/compare.h) rather than a parallel
// code path. `ReportArgs::json_option` stays nullptr here (there is no
// CLI::Option to point at); a caller reading it must check for null before
// dereferencing, which src/cli/commands/compare.cpp's run_compare does.
PolicyArgs default_policy_args();
ReportArgs default_report_args();
ColorArgs default_color_args();

// Reads every signal `decide_color` (src/cli/color_policy.h) needs into a
// ColorInputs value: `args`' two CLI11-parsed flags, plus `NO_COLOR`/`CI`/
// `GITHUB_ACTIONS` from the process environment (via `mediadiff::getenv_utf8`,
// src/util/fs.h) and whether stdout is a TTY. This function IS the only
// reader of `NO_COLOR` and `GITHUB_ACTIONS` in the repository; `CI` is ALSO
// read independently by src/cli/commands/snapshot.cpp's write gate
// (`ci_env_is_true()`, SNAP-07's CI-safe overwrite refusal) for an
// unrelated purpose. The TTY test performed here is likewise not unique to
// this function -- src/cli/commands/compare.cpp and src/cli/commands/dir.cpp
// each perform their own, independent TTY check. What still holds:
// `decide_color` itself never touches `getenv_utf8`/isatty (src/cli/color_policy.h's
// own header comment), which is what keeps it table-testable. Called
// exactly once per compare invocation, after CLI11 has finished parsing.
ColorInputs read_color_inputs(const ColorArgs& args);

// The shared bundle of every flag more than one subcommand needs
// (`--profile`, `--config`, `--set`, `--tol`, `--json`, `--report`,
// `--strict`, `-q`, `-v`, `--no-color`, `--ascii` -- 02-10-PLAN.md Task 1)
// so the spellings cannot drift between commands. `-q` suppresses
// non-error output; `-v` sets both the show-pass and show-chain render
// options. Used by `dir` and `inspect` (this plan); `compare` predates
// this bundle and keeps its own equivalent flags (02-08/02-09), and
// `list-checks`/`snapshot`/`explain` need a different or narrower set of
// their own.
struct CliOptions {
  PolicyArgs policy;
  ReportArgs report;
  ColorArgs color;
  std::shared_ptr<bool> strict;
  std::shared_ptr<bool> quiet;
  std::shared_ptr<bool> verbose;
};

// Registers every flag CliOptions bundles onto `cmd`.
CliOptions add_common_options(CLI::App& cmd);

}  // namespace mediadiff
