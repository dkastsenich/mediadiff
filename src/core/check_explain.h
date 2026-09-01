#pragma once

// Declares the accessor tools/gen_registry.py's generated check_explain.cpp
// defines (D-02). The generated file carries no header of its own for the
// unprefixed production registry -- only the --symbol-prefix path emits a
// forward declaration (of its own accessor's registry return type), since
// only that path needs one to avoid colliding with the unprefixed
// accessor's own name (see tools/gen_registry.py's render_check_id_h).
// This hand-written header is what lets `mediadiff explain` (ENG-13,
// 02-10-PLAN.md Task 3) call the compiled-in doc body without re-declaring
// the function inline at every call site.

#include <string_view>

#include "core/check_id.h"

namespace mediadiff {

// Returns the compiled-in docs/checks/<id>.md body for `id`, already
// reordered into the three fixed sections -- "## What it measures", "## Why
// it matters", "## Accept / Tune / Silence" (itself carrying its own
// "### Accept"/"### Tune"/"### Silence" sub-headings verbatim) -- in that
// order, regardless of the order they appeared in the source Markdown
// (tools/gen_registry.py's own render_check_explain_cpp). Defined in the
// generated check_explain.cpp; never hand-written.
std::string_view explain_doc(CheckId id);

}  // namespace mediadiff
