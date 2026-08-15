#include "cli/commands/inspect.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "cli/exit_code.h"
#include "cli/options.h"

namespace mediadiff {

void register_inspect_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("inspect", "Render every implemented check family for a single *.snap.json (UC8)");

  auto file_path = std::make_shared<std::string>();
  cmd->add_option("file", *file_path, "A *.snap.json to inspect")->required();

  CliOptions options = add_common_options(*cmd);

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `inspect` path permitted to call
  // std::exit() directly.
  cmd->callback([file_path, options]() {
    (void)file_path;
    (void)options;
    // Stub (02-10-PLAN.md Task 1): the real rendering body is Task 3's
    // job. The subcommand and its full flag set are registered now so
    // `--help` and this plan's CLI-02 surface are complete.
    std::fputs("mediadiff: 'inspect' rendering is not implemented yet\n", stderr);
    std::exit(kExitInternal);
  });
}

}  // namespace mediadiff
