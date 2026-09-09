#pragma once

// The one permitted CLI diagnostic sink (WR-01, T-2-33, plan 03-15).
//
// Every CLI command that needs to tell the user something went wrong
// writes through report_cli_error, not through a direct
// std::fputs(..., stderr) at the call site. This matters because
// Error::message routinely embeds a path built from user- or
// directory-supplied text (src/probe/demux_session.cpp's map_probe_error
// concatenates a path straight into the message), and in `dir` mode that
// filename comes from a directory listing, not from argv -- it is
// genuinely untrusted input on a path the pre-03-15 control-byte lint
// never scanned. This project also enables VT processing on Windows
// (util/fs.h's enable_vt_output), so a raw ANSI escape sequence in that
// text is not merely cosmetic there.
//
// report_cli_error routes `message` through mediadiff::sanitize_for_display
// (src/util/sanitize.h) before writing it, prefixed with "mediadiff: " and
// terminated with a newline, to stderr.
//
// Deliberately printing-only: this function does NOT call std::exit(). The
// call sites already choose their own exit code today (via
// src/cli/exit_code.h's exit_code_for(ErrorKind), or a literal constant for
// a usage error that carries no Error), and folding that decision into a
// printer would make the exit-code contract harder to read, not easier --
// a reader at the call site would have to open this file to learn what
// status the process leaves with. This task changes what is written,
// never whether or with what status the process leaves.
//
// scripts/lint_control_bytes.sh treats a direct standard-error write
// embedding an Error's message anywhere under src/cli/ outside this
// translation unit as a violation, mirroring the same choke-point
// enforcement it already applies to the display render paths.

#include <string_view>

namespace mediadiff {

// Writes "mediadiff: " + sanitize_for_display(message) + "\n" to stderr.
// `message` is typically an Error::message, but any user- or
// directory-derived diagnostic text not backed by an Error (e.g. a
// hand-built usage-error string) is equally safe to pass here.
void report_cli_error(std::string_view message);

}  // namespace mediadiff
