#include <CLI/CLI.hpp>

#include "cli/commands/compare.h"
#include "cli/commands/list_checks.h"
#include "cli/exit_code.h"
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
#endif

namespace mediadiff {

int run(int argc, char** argv) {
  CLI::App app{"media-aware regression diff", "mediadiff"};

  // Phase 2 adds compare/snapshot/dir/inspect/list-checks/explain subcommands
  // and the implicit-compare positional trick (CLI-01), which tolerates "0
  // subcommands fired" — set this now so Phase 2 doesn't have to touch this
  // line.
  app.require_subcommand(0, 1);

  // Lazily computed: only touches the libavutil/libavcodec/libavformat
  // version APIs when --version is actually passed.
  app.set_version_flag("--version", []() { return mediadiff::compose_version_string(); });

  register_compare_command(app);
  register_list_checks_command(app);

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
  }

  // Reached only when app.parse() completed with no exception: no
  // subcommand fired (e.g. bare `mediadiff` with no args) or a flag like
  // --version already produced its output via the ParseError path above.
  // The `compare` subcommand's own callback determines its exit code and
  // calls std::exit() directly (src/cli/commands/compare.cpp) — it never
  // returns here.
  return kExitClean;
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
  // First statement, before any output is produced and before the parser
  // is constructed — see enable_vt_output's own ordering contract.
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
