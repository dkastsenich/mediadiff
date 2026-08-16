#pragma once

// CLI-08: the single place the colour question is answered, so no renderer
// (src/cli/tty_render.cpp) re-derives it. `decide_color` is a pure function
// over an explicit `ColorInputs` value rather than a function that reads
// the environment/isatty itself -- that separation is what makes the whole
// precedence matrix table-testable (tests/unit/test_color_policy.cpp)
// without spawning a process or mutating the test runner's own environment.
// Every environment read in the repository goes through
// `mediadiff::getenv_utf8` (src/util/fs.h), the single site of the
// platform primitive; `read_color_inputs` (src/cli/options.h/.cpp) is the
// only caller that reads the COLOUR variables (`NO_COLOR`/`CI`/
// `GITHUB_ACTIONS`), but src/cli/commands/snapshot.cpp and
// src/cli/commands/dir.cpp each read their own, different variables
// through that same shim. The TTY test likewise has three call sites:
// `read_color_inputs`, src/cli/commands/compare.cpp and
// src/cli/commands/dir.cpp.

#include <optional>
#include <string>

namespace mediadiff {

// Every signal the colour decision depends on, captured as plain data.
// `no_color`/`ci`/`github_actions` are `std::optional<std::string>` rather
// than bare strings so "unset" (nullopt) is distinguishable from "set to
// the empty string" (a value holding "") -- load-bearing for NO_COLOR,
// whose own convention is that PRESENCE is the signal, not the value
// (`NO_COLOR=` with no value still disables colour).
struct ColorInputs {
  bool stdout_is_tty = false;
  std::optional<std::string> no_color;
  std::optional<std::string> ci;
  std::optional<std::string> github_actions;
  bool flag_no_color = false;
  bool flag_ascii = false;
};

// The resolved answer: whether ANSI escapes should be emitted at all, and
// whether status glyphs should render as ASCII words instead of Unicode
// symbols. The two fields are independent (`--ascii` applies in both the
// colour-enabled and colour-disabled cases) -- kept as two separate bools
// rather than a combined enum specifically so a caller (or a test) cannot
// accidentally couple them.
struct ColorDecision {
  bool color_enabled = false;
  bool ascii_glyphs = false;

  bool operator==(const ColorDecision&) const = default;
};

// Resolves `inputs` into a ColorDecision, in this precedence order (doc 00
// section 3.2, CLI-08; corrected by WR-02 -- flag_no_color/no_color now
// beat github_actions, not the reverse):
//
//   1. `flag_no_color` (the `--no-color` CLI flag) OR `no_color.has_value()`
//      (NO_COLOR present in the environment, ANY value including the empty
//      string -- the NO_COLOR convention's own rule is that presence is the
//      signal, not the value) disables colour unconditionally -- an
//      explicit "no colour" request beats every OTHER signal below,
//      including GITHUB_ACTIONS=true. GITHUB_ACTIONS=true is set
//      automatically by every GitHub Actions runner regardless of operator
//      intent, so it must never override a deliberate NO_COLOR/--no-color
//      request; the reverse ordering was WR-02's bug.
//   2. Otherwise, `github_actions == "true"` keeps colour ENABLED even
//      though `ci` may also be "true" at the same time -- GitHub's own log
//      viewer renders ANSI, so the ordinary CI=true suppression below would
//      otherwise wrongly strip colour from exactly the CI system that can
//      display it.
//   3. Otherwise, `ci == "true"` disables colour.
//   4. Otherwise, colour follows `stdout_is_tty`.
//
// `ascii_glyphs` is simply `inputs.flag_ascii`, independent of every rule
// above: a terminal that cannot render the glyphs is a different problem
// from one that cannot render colour, so `--ascii` swaps the symbol set in
// every colour state.
ColorDecision decide_color(const ColorInputs& inputs);

}  // namespace mediadiff
