#include "cli/commands/explain.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "cli/exit_code.h"

namespace mediadiff {

void register_explain_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("explain", "Print compiled-in documentation for a check (ENG-13, DOC-02)");

  auto check_id_text = std::make_shared<std::string>();
  cmd->add_option("check_id", *check_id_text, "The check id to explain, e.g. meta.tool_version")->required();

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `explain` path permitted to call
  // std::exit() directly.
  cmd->callback([check_id_text]() {
    (void)check_id_text;
    // Stub (02-10-PLAN.md Task 1): the real lookup/rendering body is
    // Task 3's job. The subcommand is registered now so `--help` and this
    // plan's CLI-02 surface are complete.
    std::fputs("mediadiff: 'explain' is not implemented yet\n", stderr);
    std::exit(kExitInternal);
  });
}

}  // namespace mediadiff
