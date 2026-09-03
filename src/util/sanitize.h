#pragma once

// mediadiff's single control-byte escaping choke point (T-2-33, carried
// from Phase 2's 02-SECURITY.md and closed by this plan). Every
// file-derived string (a tag value, a filename, a chapter title, or a
// message/value built from one of those) that reaches a DISPLAY render
// path -- src/cli/tty_render.cpp, src/cli/provenance_render.cpp,
// src/report/markdown.cpp, and NOWHERE ELSE -- must be routed through
// sanitize_for_display before it is formatted into output.
//
// src/report/json.cpp and src/report/junit.cpp deliberately do NOT call
// this function: JSON already escapes control bytes at the wire level
// (nlohmann's own string escaping, applied by core/serializer.cpp's
// serialize_document) and JUnit XML escaping (src/report/junit.cpp's own
// xml_escape) handles its own context. Calling sanitize_for_display in
// either file would double-escape and silently change every committed
// JSON/JUnit golden for no security benefit -- both files carry a comment
// at their own top stating this reasoning explicitly.
//
// scripts/lint_control_bytes.sh enforces the single-choke-point rule: a
// line in one of the three permitted display-render files that references
// a risky file-derived field (a Finding's message/id/baseline/candidate, a
// FileBlock's relative_path, a PolicyProvenance entry's value/detail)
// without ALSO calling sanitize_for_display on that same line is a lint
// violation, unless the line carries the `// control-bytes-allow` marker
// with a stated reason.

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
