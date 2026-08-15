#pragma once

#include <cstddef>
#include <string>

#include "core/registry.h"
#include "report/model.h"

namespace mediadiff {

// The Markdown body's byte budget (doc 01 section 9, REPORT-04/REPORT-05).
// Declared here (extern) so callers and tests can see it without
// duplicating the constant; the literal value and its full "why 60000, why
// bytes not characters" rationale are defined in src/report/markdown.cpp,
// next to the renderer that actually enforces it.
extern const std::size_t kMarkdownBudgetBytes;

// Renders `model` as the `--report md=` body (doc 01 section 9): a summary
// table (one row per Status, explicit counts, even when every count is
// zero) followed by one `<details>` block per non-empty group in the
// model's group order, each containing that group's findings as a table.
// A run with zero findings still emits the summary table (with explicit
// zeros) and NO `<details>` blocks at all -- an empty document would read
// as "the tool did not run" rather than "nothing changed"; a group with at
// least one finding always gets its own `<details>` block, even when that
// group has exactly one finding.
//
// When the rendered body would exceed kMarkdownBudgetBytes, the fold
// engages: whole findings are dropped from the END of the lowest-severity
// class first (Severity::ignore and Severity::info always; Severity::warn
// too, UNLESS `strict` is true, in which case a warn finding is protected
// until every non-gating finding is already gone) -- a `Severity::fail`
// finding is NEVER dropped, matching this plan's own prohibition (a
// gating finding is never dropped without stating so) at its strictest:
// every fail finding survives folding, full stop. Exactly one fold line
// is appended at the very end of the document, naming the REAL number of
// findings withheld and pointing at the JSON artifact; the line is
// entirely absent when nothing was withheld, never rendered with a count
// of zero. If even dropping every droppable finding still leaves the
// document over budget (only reachable when the summary table plus every
// protected finding alone already exceeds the budget -- a pathological
// group/fail count), the remaining text is truncated on a UTF-8 CHARACTER
// boundary, never mid-code-point, and the fold line is still appended.
std::string render_markdown(const ReportModel& model, const CheckRegistry& registry, bool strict);

// Renders `model` as `dir` mode's own `--report md=` corpus body (doc 01
// section 10, DIR-03): the same summary-table-plus-budget shape as the
// single-file renderer above, with a per-file summary table (one row per
// FileBlock, in `model.files`' own order: relative path, then that file's
// own pass/info/warn/fail/skipped/error/worst_gating counts) inserted
// ABOVE the per-group `<details>` blocks -- which here roll up findings
// from every file, in file order then group order, into the SAME
// group-keyed detail sections the single-file renderer already produces
// (a corpus's own `<details>` blocks are not one per file; DIR-03's "per
// file summary line" contract is satisfied by the per-file table, and the
// existing group-based fold/budget machinery stays the single mechanism
// that ever drops a finding, matching the "renderer never re-derives a
// second grouping/folding pass" requirement this plan is built on).
std::string render_markdown(const CorpusModel& model, const CheckRegistry& registry, bool strict);

}  // namespace mediadiff
