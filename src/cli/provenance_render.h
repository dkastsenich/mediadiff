#pragma once

// The one severity-provenance-chain text renderer every `-v` surface shares
// (ENG-06, doc 01 sections 4/6): `list-checks --effective -v` (this plan),
// `inspect -v` (plan 02-10), and `compare --json -v` (plan 02-08, which
// renders the same three PolicyProvenance fields as a JSON array instead of
// this text form). Rendering lives here, in src/cli/ -- PolicyProvenance
// (core/policy.h) carries data and formats nothing (ENG-16).

#include <span>
#include <string>

#include "core/policy.h"

namespace mediadiff {

// Renders `chain` as one line per entry, in chain order (resolution order --
// every layer appends, none rewrites, so a value a later layer replaced is
// still visible). Each line is `indent_spaces` spaces, then the layer name
// (`builtin`/`profile`/`config`/`cli`) left-padded to width 7 (the longest
// spelling, "profile", is 7 characters) followed by two literal spaces, then
// the value that layer wrote left-padded to width 8 followed immediately by
// the source detail in parentheses -- no sorting, no reversal, no
// deduplication, no filtering, matching this plan's worked example exactly.
// The trailing entry is the resolved value by construction (last writer
// wins), so no winner marker glyph is emitted -- this is what keeps the
// text identical under `--ascii`/`--no-color`/a non-TTY stdout. Every
// rendered line ends with a single `\n`; the returned string has no leading
// or trailing blank line beyond that.
std::string render_provenance_chain(std::span<const PolicyProvenance> chain, int indent_spaces);

}  // namespace mediadiff
