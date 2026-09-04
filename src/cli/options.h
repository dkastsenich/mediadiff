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

// Shared option storage for the four policy-resolution flags (D-05):
// borrowed `CLI::Option*`s, each defaulted to nullptr. The `App` owns its
// options for the whole program lifetime (`std::vector<Option_p>
// options_`), so a pointer captured into a callback outlives the
// registration function for free -- no heap allocation, no refcount. A
// callback lambda captures this struct by value and reads each member
// through opt_string/opt_strings once CLI11 has parsed argv.
// default_policy_args() leaves every member nullptr (no App ever
// registered an option to point at); opt_string/opt_strings already
// tolerate a null Option*, so a caller never dereferences one directly.
struct PolicyArgs {
  CLI::Option* profile = nullptr;
  CLI::Option* config_path = nullptr;
  CLI::Option* set_flags = nullptr;
  CLI::Option* tol_flags = nullptr;
};

// Registers `--profile`, `--config`, `--set` (repeatable) and `--tol`
// (repeatable) on `cmd`. CLI11 accumulates a repeated `std::vector<std::string>`
// option in encounter order without `take_all()` (that call exists for a
// different accumulation shape, per 02-RESEARCH.md Pattern 2) -- this is
// what makes `set_flags`/`tol_flags` already argv-ordered once parsing
// completes, before parse_cli_overrides ever runs.
PolicyArgs add_policy_flags(CLI::App& cmd);

// Shared option storage for CLI-04's report-destination flags: `--json`
// (optionally `=path`) and repeatable `--report`, both borrowed
// `CLI::Option*`s per D-05. `json_option` alone now answers both questions
// that previously took two members to answer: "was the flag given"
// (`json_option->count() > 0`, i.e. `opt_flag(json_option)`) and "with what
// path" (`opt_string(json_option)`, empty meaning stdout) -- a bare pointer
// to the Option carries both, so the separate `json_path` member this
// struct used to need is gone.
struct ReportArgs {
  CLI::Option* json_option = nullptr;
  CLI::Option* report_flags = nullptr;
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
// PolicyArgs/ReportArgs's own borrowed-`CLI::Option*` shape (D-05) so a
// callback lambda captures this struct by value and reads through
// opt_flag once CLI11 has parsed argv.
struct ColorArgs {
  CLI::Option* no_color = nullptr;
  CLI::Option* ascii = nullptr;
};

// Registers `--no-color` and `--ascii` on `cmd`.
ColorArgs add_color_flags(CLI::App& cmd);

// Shared option storage for the probe layer's two CLI flags (03-02-PLAN.md
// Task 2, PROBE-01 completion): `--probe-timeout` (consumed this plan) and
// `--probe-memory-budget-mb` (registered here, accepted, left unconsumed
// for plan 03-03 to wire up -- D-01's global memory-budget model). Both are
// typed numerics, D-05's stated exception: bound via `->check()` rather
// than the untargeted add_option/opt_string pattern, so a malformed value
// is a parse-time exit-64 usage error rather than a runtime surprise.
struct ProbeArgs {
  CLI::Option* timeout_seconds = nullptr;
  CLI::Option* memory_budget_mb = nullptr;
};

// Registers `--probe-timeout SECONDS` and `--probe-memory-budget-mb MB` on
// `cmd`.
ProbeArgs add_probe_flags(CLI::App& cmd);

// Resolves the wall-clock probe budget, in milliseconds, from
// `--probe-timeout` (when given) else `[probe] timeout_seconds` (when
// `config` declared one) else std::nullopt -- meaning "no override; leave
// src/probe/demux_session.h's own default_wall_clock_budget_ms() in
// force", the same three-way precedence shape src/cli/commands/dir.cpp
// already established for `--threads`/`[dir] threads`. A caller that gets
// an engaged value is expected to call set_default_wall_clock_budget_ms
// with it before the first DemuxSession::open of this invocation; a
// std::nullopt result means the caller should not call the setter at all.
// `--probe-timeout`'s own text is re-parsed here even though CLI11's
// ->check(CLI::NonNegativeNumber) already validated it at parse time,
// mirroring resolve_profile_selection's own "re-validate, don't trust
// blindly" convention.
mediadiff::expected<std::optional<std::int64_t>, Error> resolve_probe_timeout_ms(const ProbeArgs& args,
                                                                                    const std::optional<ConfigFile>& config);

// Resolves the GLOBAL probe-memory budget, in MEGABYTES, from
// `--probe-memory-budget-mb` (when given) else `[probe] memory_budget_mb`
// (when `config` declared one) else src/probe/packet_scan.h's own
// kDefaultProbeMemoryBudgetMb (D-01) -- the same three-way precedence
// shape resolve_probe_timeout_ms already established, except this one
// ALWAYS returns an engaged value (there is no "leave some other default
// in force" case: D-01's derived per-file cap must be set before the
// first run_packet_scan call of every invocation). Every caller converts
// this MB value to bytes, divides by its own resolved thread count via
// src/probe/packet_scan.h's derive_per_file_cap_bytes, and calls
// set_default_packet_scan_max_bytes with the result -- `dir` mode passes
// its resolved `--threads`/`[dir] threads` count; the three single-file
// commands always pass 1 (their own resolved thread count is always 1,
// so a single in-flight file gets the whole budget).
mediadiff::expected<std::int64_t, Error> resolve_probe_memory_budget_mb(const ProbeArgs& args,
                                                                           const std::optional<ConfigFile>& config);

// 03-12-PLAN.md Task 1 (T-3-58, D-01): wraps resolve_probe_memory_budget_mb
// and converts its megabyte result to BYTES in exactly ONE place, instead
// of at four separate command entry points -- each of which used to
// perform its own raw `mb * 1024 * 1024` multiplication with no overflow
// check. Rejects a resolved value above kMaxProbeMemoryBudgetMb (src/config/toml_load.h)
// with ErrorKind::usage naming the offending value and the maximum, then
// converts to bytes via two successive detail::checked_mul (src/core/rational.h)
// steps (MB -> KB -> bytes), returning ErrorKind::usage on either overflow.
// resolve_probe_memory_budget_mb itself stays public -- this function wraps
// it rather than replacing it, so the plain megabyte value stays available
// wherever a caller wants it for diagnostics.
mediadiff::expected<std::int64_t, Error> resolve_probe_memory_budget_bytes(const ProbeArgs& args,
                                                                               const std::optional<ConfigFile>& config);

// All-null ProbeArgs, matching default_policy_args()/default_report_args()/
// default_color_args()'s own contract -- used by main.cpp's implicit
// two-positional dispatch, which carries none of `compare`'s own optional
// flags.
ProbeArgs default_probe_args();

// All-null PolicyArgs/ReportArgs/ColorArgs, with no CLI11 flags registered
// on any App -- used by main.cpp's implicit two-positional dispatch
// (CLI-01), which intentionally carries none of `compare`'s own optional
// flags: "mediadiff a b" behaves exactly like "mediadiff compare a b" with
// none of them given, dispatched through the SAME run_compare
// (src/cli/commands/compare.h) rather than a parallel code path. Every
// member of every struct returned here is nullptr (there is no App and
// therefore no CLI::Option to point at); opt_string/opt_flag/opt_strings
// all tolerate a null Option*, which is what makes `return {};` a correct,
// complete implementation for all three factories.
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

// D-05's three borrowed-Option* read accessors. Each tolerates a null
// `o` (the all-null default_*_args() case, see below) as well as an `o`
// that is non-null but was never given on the command line
// (`o->count() == 0`) -- the untargeted `add_option`/`add_flag` overloads
// this migration adopts do not bind a variable CLI11 can default-populate
// for us, so every reader must ask the Option itself.
//
// opt_strings' `count() == 0` early return is NOT defensive padding -- it
// is mandatory. CLI11 2.6.2's Option::results(T&) (Option.hpp:735-741)
// does `res.emplace_back()` when `results_` is empty and no default string
// is set, so an unguarded `o->as<std::vector<std::string>>()` on an unset
// option returns a ONE-ELEMENT vector holding a single empty string, not
// an empty vector. For a vector-valued option like `--set`/`--tol`/
// `--report`, that `{""}` would reach parse_cli_overrides/
// parse_report_destinations as a malformed `<glob>=<value>` argument with
// no `=`, which append_overrides (options.cpp) rejects as ErrorKind::usage
// -- i.e. every unset `--set`/`--tol`/`--report` would turn into a usage
// error on every invocation. Guarding on count() first is what keeps
// "the flag was never given" mapping to the empty vector every existing
// caller already expects.
std::string opt_string(const CLI::Option* o);
bool opt_flag(const CLI::Option* o);
std::vector<std::string> opt_strings(const CLI::Option* o);

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
  ProbeArgs probe;
  CLI::Option* strict = nullptr;
  CLI::Option* quiet = nullptr;
  CLI::Option* verbose = nullptr;
};

// Registers every flag CliOptions bundles onto `cmd`.
CliOptions add_common_options(CLI::App& cmd);

}  // namespace mediadiff
