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

}  // namespace mediadiff
