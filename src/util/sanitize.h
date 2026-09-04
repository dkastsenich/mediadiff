#pragma once

// mediadiff's single control-byte escaping choke point for the DISPLAY
// render surface (T-2-33, carried from Phase 2's 02-SECURITY.md, closed
// across every output format by 03-11 and 03-15). Every file-derived
// string (a tag value, a filename, a chapter title, or a message/value
// built from one of those) that reaches a display render path -- the set
// of files scripts/lint_control_bytes.sh scans, currently
// src/cli/tty_render.cpp, src/cli/provenance_render.cpp,
// src/report/markdown.cpp and src/cli/commands/inspect_render.h -- must be
// routed through sanitize_for_display before it is formatted into output.
// This list is enforced by the lint's own scan list, not restated here as
// a fixed count: a future render path is added to the lint's scan list,
// not to this comment, so the two can never drift against each other the
// way IN-02 found them to have drifted.
//
// src/report/json.cpp deliberately does NOT call this function: JSON
// already escapes control bytes at the wire level (nlohmann's own string
// escaping, applied by core/serializer.cpp's serialize_document). Calling
// sanitize_for_display there would double-escape and silently change
// every committed JSON golden for no security benefit -- the file carries
// a comment at its own top stating this reasoning explicitly.
//
// src/report/junit.cpp also deliberately does NOT call this function, but
// NOT because "it handles its own context" in some unspecified way: as of
// T-2-33's completion (03-15), junit.cpp's own xml_escape explicitly
// escapes every C0 control byte (other than tab/LF/CR) and DEL as a
// visible "\xHH" sequence in the text itself, in addition to its
// pre-existing four-metacharacter XML escaping -- a raw control byte
// cannot reach an emitted JUnit report unescaped. Routing junit.cpp
// through sanitize_for_display instead would double-escape the four XML
// metacharacters and put the file outside XML's own escaping rules; fixing
// xml_escape in place, in its own context, was the correct shape.
//
// src/cli/diagnostics.cpp (CLI stderr diagnostics) DOES call this
// function -- it is a display render path, just not a terminal renderer
// in src/cli/tty_render.cpp's sense; see its own header comment.
//
// scripts/lint_control_bytes.sh enforces the single-choke-point rule: a
// line in one of the permitted display-render files that references a
// risky file-derived field (a Finding's message/id/baseline/candidate, a
// FileBlock's relative_path, a PolicyProvenance entry's value/detail, or
// an Error's message) without ALSO calling sanitize_for_display on that
// same line is a lint violation, unless the line carries the
// `// control-bytes-allow` marker with a stated reason.

#include <string>
#include <string_view>

namespace mediadiff {

// Escapes every C0 control byte (0x00-0x1F) except horizontal tab (0x09),
// escapes DEL (0x7F), escapes the UTF-8 encoding of the C1 range
// (U+0080-U+009F -- some terminals interpret these as control codes too),
// and replaces any invalid UTF-8 byte sequence with U+FFFD (the Unicode
// replacement character, emitted as valid UTF-8 -- the same replacement
// convention src/analyzers/container/meta.cpp's own sanitize_utf8 already
// uses for the comparison-side value, T-3-15's mitigation). A literal
// backslash byte (0x5C) is ALSO escaped, doubled ("\\"), so that raw text
// which happens to contain the four literal characters `\`, `x`, `1`, `b`
// can never be visually indistinguishable from this function's own escape
// of a real ESC byte (0x1B) -- a reader must always be able to tell "this
// text originally contained a backslash" from "this is my own escape
// marker."
//
// Escape forms emitted by this function:
//   - a doubled backslash for a literal backslash byte: "\\"
//   - "\xHH" (lowercase hex, exactly two digits) for an escaped C0/DEL byte
//   - "\uHHHH" (lowercase hex, exactly four digits) for an escaped C1
//     codepoint
//
// Every caller must sanitize BEFORE any width/elision accounting runs --
// an escaped form can be longer than its raw form, and an elision budget
// computed against the raw (unescaped) length would corrupt every
// downstream truncation decision, exactly the reasoning
// src/cli/tty_render.cpp's own make_glyph plain/rendered split already
// documents for a styled ANSI sequence.
//
// Every other byte (ordinary printable ASCII, or a valid multi-byte UTF-8
// sequence outside the C1 range) passes through unchanged, byte-for-byte
// -- so routing existing, control-byte-free, backslash-free input through
// this function never changes it, and no pre-existing golden is altered
// by adopting it.
std::string sanitize_for_display(std::string_view raw);

}  // namespace mediadiff
