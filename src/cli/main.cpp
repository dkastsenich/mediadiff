#include <CLI/CLI.hpp>

#include "util/fs.h"
#include "util/version.h"

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>

#include <string>
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

  CLI11_PARSE(app, argc, argv);
  return 0;
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
  std::vector<std::string> argv_utf8;
  argv_utf8.reserve(static_cast<size_t>(argc_w));
  for (int i = 0; i < argc_w; ++i) {
    argv_utf8.push_back(mediadiff::wide_to_utf8(argv_w[i]));
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
