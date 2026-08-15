// 02-09-PLAN.md Task 1: the one colour decision (CLI-08). `decide_color` is
// a pure function over explicit ColorInputs -- every row here asserts BOTH
// fields of the returned ColorDecision, so a change that accidentally
// couples color_enabled to ascii_glyphs (or vice versa) is caught.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "cli/color_policy.h"

namespace {

using mediadiff::ColorDecision;
using mediadiff::ColorInputs;
using mediadiff::decide_color;

}  // namespace

TEST_CASE("color - stdout is a tty with nothing else set enables colour", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  const ColorDecision decision = decide_color(inputs);
  CHECK(decision.color_enabled);
  CHECK_FALSE(decision.ascii_glyphs);
}

TEST_CASE("color - non-tty stdout disables colour", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = false;
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
  CHECK_FALSE(decision.ascii_glyphs);
}

TEST_CASE("color - NO_COLOR set to the empty string disables colour", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.no_color = std::string("");
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
}

TEST_CASE("color - NO_COLOR set to a non-empty value disables colour", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.no_color = std::string("1");
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
}

TEST_CASE("color - CI=true disables colour", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.ci = std::string("true");
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
}

TEST_CASE("color - CI=true together with GITHUB_ACTIONS=true keeps colour enabled", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = false;  // GitHub Actions redirects stdout to a pipe
  inputs.ci = std::string("true");
  inputs.github_actions = std::string("true");
  const ColorDecision decision = decide_color(inputs);
  CHECK(decision.color_enabled);
}

TEST_CASE("color - CI=false does not disable colour", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.ci = std::string("false");
  const ColorDecision decision = decide_color(inputs);
  CHECK(decision.color_enabled);
}

TEST_CASE("color - --no-color beats GITHUB_ACTIONS=true", "[color]") {
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.github_actions = std::string("true");
  inputs.flag_no_color = true;
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
}

TEST_CASE("color - WR-02: NO_COLOR (environment) beats GITHUB_ACTIONS=true", "[color]") {
  // A GitHub Actions runner sets GITHUB_ACTIONS=true automatically,
  // regardless of operator intent; NO_COLOR is always a deliberate,
  // explicit request and must win even though github_actions is also
  // "true" in the same run.
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.github_actions = std::string("true");
  inputs.no_color = std::string("1");
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
}

TEST_CASE("color - WR-02: NO_COLOR set to the empty string still beats GITHUB_ACTIONS=true", "[color]") {
  // NO_COLOR's own convention: presence is the signal, not the value --
  // exercised here specifically against the github_actions precedence
  // rule, not just in isolation (the "NO_COLOR set to the empty string
  // disables colour" case above doesn't set github_actions at all).
  ColorInputs inputs;
  inputs.stdout_is_tty = true;
  inputs.github_actions = std::string("true");
  inputs.no_color = std::string("");
  const ColorDecision decision = decide_color(inputs);
  CHECK_FALSE(decision.color_enabled);
}

TEST_CASE("color - --ascii sets ascii_glyphs in both colour-enabled and colour-disabled rows", "[color]") {
  ColorInputs enabled_inputs;
  enabled_inputs.stdout_is_tty = true;
  enabled_inputs.flag_ascii = true;
  const ColorDecision enabled_decision = decide_color(enabled_inputs);
  CHECK(enabled_decision.color_enabled);
  CHECK(enabled_decision.ascii_glyphs);

  ColorInputs disabled_inputs;
  disabled_inputs.stdout_is_tty = false;
  disabled_inputs.flag_ascii = true;
  const ColorDecision disabled_decision = decide_color(disabled_inputs);
  CHECK_FALSE(disabled_decision.color_enabled);
  CHECK(disabled_decision.ascii_glyphs);
}
