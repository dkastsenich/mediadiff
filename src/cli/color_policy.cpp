#include "cli/color_policy.h"

namespace mediadiff {

ColorDecision decide_color(const ColorInputs& inputs) {
  bool color_enabled;
  // WR-02: flag_no_color and no_color.has_value() are checked TOGETHER,
  // before github_actions -- an explicit NO_COLOR request (the CLI flag or
  // the environment variable's own presence-is-the-signal convention) must
  // beat GITHUB_ACTIONS=true, which every GitHub Actions runner sets
  // automatically regardless of what the operator asked for. The previous
  // ordering checked github_actions first, so a job that set BOTH
  // GITHUB_ACTIONS=true (automatic) and NO_COLOR=1 (deliberate) got colour
  // anyway -- see this file's own header doc comment for the full,
  // corrected precedence.
  if (inputs.flag_no_color || inputs.no_color.has_value()) {
    color_enabled = false;
  } else if (inputs.github_actions.has_value() && *inputs.github_actions == "true") {
    color_enabled = true;
  } else if (inputs.ci.has_value() && *inputs.ci == "true") {
    color_enabled = false;
  } else {
    color_enabled = inputs.stdout_is_tty;
  }
  return ColorDecision{color_enabled, inputs.flag_ascii};
}

}  // namespace mediadiff
