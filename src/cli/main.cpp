#include <CLI/CLI.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

#include "cli/commands/compare.h"
#include "cli/commands/dir.h"
#include "cli/commands/explain.h"
#include "cli/commands/inspect.h"
#include "cli/commands/list_checks.h"
#include "cli/commands/snapshot.h"
#include "cli/exit_code.h"
#include "cli/options.h"
#include "util/fs.h"
#include "util/version.h"

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>

// Included explicitly rather than leaned on transitively through util/fs.h.
// MSVC's standard headers pull in less than libstdc++'s, so a translation
// unit that compiles on GCC purely by inheritance can fail on MSVC — and the
// Windows leg is the one that cannot be checked from a POSIX host.
#include <cstdio>       // std::fputs, stderr
#include <string>       // std::string, std::to_string
#include <string_view>  // std::wstring_view
#include <utility>      // std::move
#include <vector>

// _setmode/_fileno/_O_BINARY -- the fd-level binary-mode switch below
// (TRUST-05, G-02-5 lead B). Deliberately NOT in src/util/fs.h: that header
// is under scripts/lint_eng16.sh's SCAN_DIRS (src/util), and the lint's
// PATTERN matches the bare `stdout`/`stderr` tokens -- the same code there
// would red the required `lint (ENG-16 boundary)` merge-gate context. This
// file (src/cli/main.cpp) is outside every ENG-16 scan directory, so it is
// the only place this call is allowed to live.
#include <fcntl.h>
#include <io.h>
#endif

namespace mediadiff {

int run(int argc, char** argv) {
  CLI::App app{"media-aware regression diff", "mediadiff"};

  // Tolerates "0 subcommands fired" -- the implicit-compare route below.
  app.require_subcommand(0, 1);

  // T-2-36 (CLI-01): prefix matching must be disabled BEFORE any
  // subcommand is added -- a child App copies this flag from its parent
  // at construction (CLI 2.6.2's App_inl.hpp:61), so setting it here and
  // only here is what makes it apply to every subcommand this build
  // registers. The member already defaults to false in 2.6.2; this call
  // makes the requirement explicit rather than accidental, so a future
  // CLI11 bump that flips the default cannot silently re-enable it.
  app.allow_subcommand_prefix_matching(false);

  // T-02-19-01/T-02-19-02 (CLI-06): argv classification must be identical
  // on every platform, and this call must sit ABOVE every subcommand
  // registration for the same reason `allow_subcommand_prefix_matching`
  // does, immediately above -- CLI11 2.6.2's parent-taking `App`
  // constructor copies `allow_windows_style_options_` from the parent in
  // its INHERITABLE block (App.hpp:259-267), so a child registered before
  // this line would keep the platform default instead of inheriting it.
  //
  // CLI11 defaults this member to `true` under `_WIN32` and `false`
  // elsewhere, so leaving it alone means mediadiff parses argv differently
  // on Windows than on POSIX. mediadiff defines no `/`-prefixed option
  // anywhere, so the only observable effect of the default is that a
  // legitimate forward-slash-rooted path argument (e.g.
  // "/data/out.snap.json", a normal absolute path on Windows) is
  // reclassified as an unknown option: the parse throws a
  // `CLI::ParseError`, `app.exit(e)` below maps it to `kExitUsage` (64),
  // and the path is never opened -- instead of `read_snapshot` failing it
  // and this reaching `kExitInput` (65) as CLI-06 requires. This was found
  // by tests/integration/test_exit_codes.cpp's "exit_codes - 65" case
  // failing `64 == 65` on the Windows leg of CI run 31946964023, the first
  // run in the project's history in which the Windows test suite executed.
  app.allow_windows_style_options(false);

  // Lazily computed: only touches the libavutil/libavcodec/libavformat
  // version APIs when --version is actually passed.
  app.set_version_flag("--version", []() { return mediadiff::compose_version_string(); });

  // CLI-01: two optional bare positionals on the ROOT app -- the
  // implicit-compare trick. A token equal to a registered subcommand name
  // is always classified SUBCOMMAND before a bare positional is ever
  // considered (CLI11's own App::_recognize), so these two options can
  // never "steal" a real subcommand invocation; see register_compare_command's
  // own footer text for the one edge case this implies (a file literally
  // named "compare").
  auto implicit_baseline = std::make_shared<std::string>();
  auto implicit_candidate = std::make_shared<std::string>();
  CLI::Option* implicit_baseline_opt =
      app.add_option("baseline", *implicit_baseline, "Baseline artifact or *.snap.json (implicit compare)");
  CLI::Option* implicit_candidate_opt =
      app.add_option("candidate", *implicit_candidate, "Candidate artifact or *.snap.json (implicit compare)");

  register_compare_command(app);
  register_snapshot_command(app);
  register_dir_command(app);
  register_inspect_command(app);
  register_list_checks_command(app);
  register_explain_command(app);

  // CLI11_PARSE's own catch block returns app.exit(e) unmodified, which
  // lands in CLI::ExitCodes' 100-127 range (Success=0, then
  // IncorrectConstruction=100 through ArgumentMismatch=115) — none of
  // which match mediadiff's exit-code contract (doc 00 section 3.1). Every
  // CLI11-level parse failure is definitionally a usage error (kExitUsage),
  // except a clean --help/--version exit, which app.exit(e) already
  // reports as 0.
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e) == 0 ? kExitClean : kExitUsage;
  } catch (const std::exception& e) {
    // CR-01 defense-in-depth backstop: core/config/compare must never throw
    // across the lib boundary (PROJECT.md's error-handling constraint,
    // ENG-16) — every fallible path returns mediadiff::expected<T, Error>
    // instead, and every subcommand callback that reads a snapshot already
    // maps a read failure to a clean exit via exit_code_for(). This catch
    // exists only to turn an exception the lib boundary should have
    // prevented into a diagnosed exit(70) instead of std::terminate() /
    // SIGABRT, for any future call site that reintroduces the pattern
    // CR-01 fixed. It deliberately reports internal (a mediadiff bug), not
    // a user-input problem, since a well-formed lib boundary would never
    // let an exception reach here.
    std::fputs(("mediadiff: internal error (uncaught exception): " + std::string(e.what()) + "\n").c_str(), stderr);
    return kExitInternal;
  }

  // A registered subcommand's own callback already ran (and, for every
  // subcommand this build registers, already called std::exit() itself --
  // src/cli/commands/*.cpp's own ENG-16 rationale) by the time app.parse()
  // returns with no exception. get_subcommands() (parsed_subcommands_)
  // reports which subcommand(s), if any, actually fired.
  const bool subcommand_fired = !app.get_subcommands().empty();

  if (subcommand_fired) {
    // Guard the subcommand-fallthrough leak: subcommand_fallthrough_
    // defaults to true, so a token a subcommand cannot place falls
    // through to the PARENT's own positionals -- which are these two
    // implicit ones. Without this guard, "mediadiff compare a b c" would
    // silently deposit 'c' into implicit_baseline instead of failing; a
    // bare positional is only ever meaningful when no subcommand fired.
    if (implicit_baseline_opt->count() > 0 || implicit_candidate_opt->count() > 0) {
      std::fputs("mediadiff: unexpected extra argument after a subcommand\n", stderr);
      return kExitUsage;
    }
    // Unreachable in practice (every registered subcommand's callback
    // exits directly); kept as a defined return for a future subcommand
    // whose callback might not.
    return kExitClean;
  }

  // No subcommand fired: the implicit-compare route (both positionals
  // set), or bare `mediadiff`/a single operand (fewer than two).
  if (implicit_baseline_opt->count() > 0 && implicit_candidate_opt->count() > 0) {
    // Dispatches through the SAME compare execution the `compare`
    // subcommand's own callback calls -- never a parallel copy (this
    // plan's own Task 1 requirement). run_compare is [[noreturn]]: it
    // always calls std::exit(), carrying none of `compare`'s own optional
    // flags (--profile/--json/--strict/...), matching "mediadiff a b"
    // behaving exactly like "mediadiff compare a b" with none of them
    // given.
    run_compare(*implicit_baseline, *implicit_candidate, /*strict=*/false, /*verbose=*/false, /*quiet=*/false,
                default_report_args(), default_policy_args(), default_color_args());
  }

  // Fewer than two positionals (including bare `mediadiff` with none):
  // CLI-01's own contract is "print help and exit 64", never 0 -- a CI
  // script that invoked the tool with nothing to compare has not
  // succeeded.
  std::fputs(app.help().c_str(), stdout);
  return kExitUsage;
}

}  // namespace mediadiff

#ifdef _WIN32

// Windows-only wide entry point (D-04, CLI-09). The OS hands the process a
// UTF-16 command line; this is the single site where it is converted to
// UTF-8, exactly once, before mediadiff::run — and therefore the CLI11
// parser — ever sees it. See src/util/fs.h's header comment: this file and
// that header are the only two places permitted to name wide-character
// types.
//
// MSVC's default CRT startup selects wmainCRTStartup automatically when a
// wmain is defined, so no separate linker entry-point flag is needed.
int wmain(int /*argc*/, wchar_t** /*argv*/) {
  // First statement of all, before enable_vt_output() and before any
  // output is produced (including this function's own UTF-16-conversion
  // failure path a few lines below, which writes to stderr).
  //
  // The MSVC CRT opens fd 1 and fd 2 in _O_TEXT by default, translating
  // every '\n' a program writes into "\r\n". mediadiff's four report-file
  // destinations (compare.cpp, dir.cpp x2, snapshot.cpp) already open
  // "wb" -- binary, untranslated -- so without this call, `--json` to
  // stdout and `--json=path` to a file produce DIFFERENT BYTES on
  // Windows for the identical report. That breaks TRUST-05 (byte-
  // identical --json across identical runs), a hard project determinism
  // constraint. Decision recorded 2026-08-18 (02-17-PLAN.md Task 2,
  // option B): both stdout AND stderr go binary, unconditionally --
  // one rule, no exceptions, matching the four "wb" destinations exactly,
  // rather than leaving stderr as the one place newline representation
  // still depends on which stream a byte went to.
  //
  // The asymmetry is closed by raising stdout/stderr TO the file
  // destinations' behaviour, not by lowering the file destinations' --
  // those four "wb" call sites are already correct and are deliberately
  // untouched here.
  //
  // This call cannot live in src/util/fs.h next to enable_vt_output(),
  // its natural-looking home: that header is inside
  // scripts/lint_eng16.sh's SCAN_DIRS (src/util), and the lint's PATTERN
  // matches the bare `stdout`/`stderr` tokens, so the same code there
  // would red the required `lint (ENG-16 boundary)` merge-gate context.
  // src/cli/main.cpp is outside every ENG-16 scan directory, which is
  // why it lives here instead.
  //
  // _setmode/_fileno (not the unprefixed POSIX setmode/fileno): MSVC
  // deprecates the unprefixed spellings (C4996), and /W4 /WX (BUILD-05)
  // promotes that to a hard error -- this project has already lost two
  // rounds to C4996 (getenv, then _wfopen). The (void) cast discards
  // _setmode's previous-mode return value explicitly; the CRT annotates
  // it _Check_return_opt_, and discarding it deliberately costs nothing
  // under /W4.
  (void)_setmode(_fileno(stdout), _O_BINARY);
  (void)_setmode(_fileno(stderr), _O_BINARY);

  // See enable_vt_output's own ordering contract: first statement to
  // touch the console, before the parser is constructed. Emits no bytes
  // of its own, so its position relative to the binary-mode calls above
  // is immaterial to TRUST-05 -- both must simply precede the first
  // actual write, which they do.
  mediadiff::enable_vt_output();

  int argc_w = 0;
  LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
  if (argv_w == nullptr) {
    return 1;
  }

  // Convert every argument to UTF-8 exactly once, here. CLI11 itself is
  // encoding-agnostic — it only needs argv already in UTF-8, which this
  // conversion guarantees.
  //
  // wide_to_utf8 returns an empty string for BOTH an empty input and a
  // conversion failure, and cannot distinguish them on its own. The caller
  // can: an argument is legitimately empty only when the wide string it came
  // from was empty. Checking that here is what keeps the fail-loud contract
  // D-04 requires — ill-formed UTF-16 (an unpaired surrogate, which NTFS
  // permits in a filename) must not be silently rewritten to "". Feeding a
  // substituted empty argument to the parser would shift what every
  // subsequent option means, and the operator would never be told.
  std::vector<std::string> argv_utf8;
  argv_utf8.reserve(static_cast<size_t>(argc_w));
  for (int i = 0; i < argc_w; ++i) {
    const std::wstring_view wide_arg{argv_w[i]};
    std::string utf8_arg = mediadiff::wide_to_utf8(wide_arg);
    if (utf8_arg.empty() && !wide_arg.empty()) {
      LocalFree(argv_w);
      // Reported here rather than from the engine: libmediadiff writes to no
      // standard stream and never exits the process (ENG-16 / D-07).
      std::fputs("mediadiff: argument ", stderr);
      std::fputs(std::to_string(i).c_str(), stderr);
      std::fputs(" is not valid UTF-16 and cannot be converted to UTF-8.\n", stderr);
      return 64;  // usage — the argument is malformed, no input was opened
    }
    argv_utf8.push_back(std::move(utf8_arg));
  }
  LocalFree(argv_w);

  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(argv_utf8.size());
  for (auto& arg : argv_utf8) {
    argv_ptrs.push_back(arg.data());
  }

  return mediadiff::run(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
}

#else  // !_WIN32

int main(int argc, char** argv) {
  // First statement, before any output is produced and before the parser
  // is constructed — see enable_vt_output's own ordering contract. A no-op
  // on this platform, called unconditionally so the ordering rule holds
  // identically on every triplet.
  mediadiff::enable_vt_output();
  return mediadiff::run(argc, argv);
}

#endif  // _WIN32
