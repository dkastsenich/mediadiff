#include "cli/color_policy.h"

namespace mediadiff {

ColorDecision decide_color(const ColorInputs& inputs) {
  bool color_enabled;
  if (inputs.flag_no_color) {
    color_enabled = false;
  } else if (inputs.github_actions.has_value() && *inputs.github_actions == "true") {
    color_enabled = true;
  } else if (inputs.no_color.has_value()) {
    color_enabled = false;
  } else if (inputs.ci.has_value() && *inputs.ci == "true") {
    color_enabled = false;
  } else {
    color_enabled = inputs.stdout_is_tty;
  }
  return ColorDecision{color_enabled, inputs.flag_ascii};
}

}  // namespace mediadiff
