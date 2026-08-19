#pragma once

// The fourth report format (REPORT-02, REPORT-03) -- the one a human
// actually reads. Renders the SAME ReportModel the JSON/Markdown/JUnit
// renderers consume (src/report/model.h), so grouping, ordering and
// summarisation are never re-derived here; this file owns layout, colour
// gating and the accept/tune/silence triple only. `render_tty` is a pure
// function of its four arguments -- no stream, no getenv, no isatty --
// matching src/util/version.cpp's compose_version_string pattern: it
// returns a fully composed string, and src/cli/commands/compare.cpp is
// what actually writes it to stdout.

#include <string>

#include "cli/color_policy.h"
#include "core/registry.h"
#include "report/model.h"

namespace mediadiff {

// Renders `model` as TTY text: a one-line summary, then each of
// ReportModel's seven groups in its own fixed order, each finding on its
// own row, and -- under every gating finding (src/report/model.h's own
// is_gating), individually, never once per group -- the accept/tune/
// silence triple sourced from `registry`'s own CheckDef::explain_accept/
// explain_tune/explain_silence fields (tools/gen_registry.py, this plan's
// own extension). A group with no findings to show (every finding in it
// was filtered out of `model`, or the group was never populated) is
// omitted entirely, matching src/report/markdown.cpp's own "skip an empty
// group" convention.
//
// `color` decides whether ANSI escapes are emitted at all (never when
// `color.color_enabled` is false) and whether status glyphs render as
// ASCII words (`color.ascii_glyphs`) -- both resolved once, by the caller,
// via src/cli/color_policy.h's decide_color, and passed in as data rather
// than re-derived here.
//
// `terminal_width` bounds the FINDING ROW's own value column only: a
// value too wide to fit is elided with a trailing marker rather than
// wrapped across lines, so a diff stays readable at exactly the width it
// was asked to render at. The accept/tune/silence hint text is prose, not
// a value column, and IS word-wrapped to the same width -- wrapping prose
// does not reintroduce the "wrapped value column" problem REPORT-02
// prohibits, since a hint line was never meant to be read as tabular data.
// No terminal is queried here; the caller (src/cli/commands/compare.cpp)
// queries it once, defaulting to 100 columns when it cannot be
// interrogated, which is what keeps this function callable at any width a
// unit test chooses.
std::string render_tty(const ReportModel& model, const CheckRegistry& registry, const ColorDecision& color,
                        int terminal_width);

// Renders `model` as `dir` mode's own TTY report (doc 01 section 10,
// DIR-03): a corpus summary line (the SAME shape render_summary_line
// already produces, over `model.totals`), then a worst-N table listing up
// to `terminal_height` files (defaulting to 10, doc 01's own "worst-N
// table" language), worst `worst_gating` severity first, ties broken by
// relative path -- a TOTAL order, so the table's own row order is
// deterministic regardless of worker completion order, matching every
// other corpus-facing guarantee this plan makes. `terminal_height <= 0`
// falls back to the default of 10 rather than rendering zero -- an
// unqueryable terminal height (the common case: stdout redirected to a
// file or a pipe) must not silently make the worst-N table vanish.
std::string render_tty(const CorpusModel& model, const CheckRegistry& registry, const ColorDecision& color,
                        int terminal_width, int terminal_height);

}  // namespace mediadiff
