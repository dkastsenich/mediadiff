#include "cli/diagnostics.h"

#include <cstdio>
#include <string>

#include "util/sanitize.h"

namespace mediadiff {

void report_cli_error(std::string_view message) {
  const std::string sanitized = sanitize_for_display(message);
  const std::string line = "mediadiff: " + sanitized + "\n";
  std::fputs(line.c_str(), stderr);
}

}  // namespace mediadiff
