#include <CLI/CLI.hpp>

#include "util/version.h"

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

int main(int argc, char** argv) {
  return mediadiff::run(argc, argv);
}
