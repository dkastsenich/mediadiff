#include "cli/commands/explain.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/diagnostics.h"
#include "cli/exit_code.h"
#include "cli/options.h"
#include "core/check_explain.h"
#include "core/registry.h"

namespace mediadiff {

namespace {

std::string first_dot_segment(std::string_view id) {
  const std::size_t dot = id.find('.');
  return std::string(dot == std::string_view::npos ? id : id.substr(0, dot));
}

}  // namespace

void register_explain_command(CLI::App& app) {
  auto* cmd = app.add_subcommand("explain", "Print compiled-in documentation for a check (ENG-13, DOC-02)");

  // ->type_name("TEXT"): see src/cli/options.cpp's add_policy_flags for the
  // fully worked rationale (D-05).
  CLI::Option* check_id_text = cmd->add_option("check_id", "The check id to explain, e.g. meta.tool_version")
                                    ->type_name("TEXT")
                                    ->required();

  // ENG-16: exit()/stdout/stderr are the CLI's prerogative -- this
  // callback is the one place in the `explain` path permitted to call
  // std::exit() directly. Capturing a raw Option* by value is exactly as
  // safe as the shared_ptr it replaces (D-05): the App owns the Option
  // for the whole program lifetime, and this callback only runs during
  // app.parse().
  cmd->callback([check_id_text]() {
    const CheckRegistry& registry = builtin_registry();

    const std::string check_id = opt_string(check_id_text);
    bool was_aliased = false;
    const std::optional<std::uint32_t> index = registry.resolve_alias(check_id, &was_aliased);
    if (!index.has_value()) {
      const std::string group = first_dot_segment(check_id);
      std::vector<std::string_view> group_members;
      for (std::uint32_t i = 0; i < registry.size(); ++i) {
        if (registry.at(i).group == group) {
          group_members.push_back(registry.at(i).id);
        }
      }
      std::string message = "unknown check id '" + check_id + "'";
      if (!group_members.empty()) {
        message += " -- checks in group '" + group + "': ";
        for (std::size_t i = 0; i < group_members.size(); ++i) {
          if (i > 0) {
            message += ", ";
          }
          message += std::string(group_members[i]);
        }
      }
      report_cli_error(message);
      std::exit(kExitUsage);
    }

    std::string out;
    if (was_aliased) {
      const CheckDef& resolved_check = registry.at(*index);
      out += "mediadiff: '" + check_id + "' is a deprecated alias for '" + std::string(resolved_check.id) + "'\n\n";
    }
    out += std::string(explain_doc(static_cast<CheckId>(*index)));
    out += "\n";
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::exit(kExitClean);
  });
}

}  // namespace mediadiff
