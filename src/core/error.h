#pragma once

// mediadiff's fallible-operation error type (ENG-15). Every fallible
// function in core/, config/ and compare/ returns
// mediadiff::expected<T, Error> (src/util/expected.h) rather than throwing —
// no exceptions cross the lib boundary (PROJECT.md's Error-handling
// constraint). src/cli/exit_code.h owns the ONE place `Error.kind` is
// translated into a process exit code; nothing under core/, config/ or
// compare/ ever calls exit() itself (ENG-16).

#include <string>

namespace mediadiff {

// Doc 01 section 11's error taxonomy, verbatim. Each enumerator maps to
// exactly one exit code in src/cli/exit_code.h; that mapping is written as
// a switch with no `default:` arm specifically so adding an enumerator here
// without extending the mapping is a compile error, not a silent gap.
enum class ErrorKind {
  usage,                // malformed argv or config; no input was opened
  input_open,           // a named input file did not open
  input_unsupported,    // input opened but could not be understood (bad JSON,
                         // schema-version-major skew, an unregistered check ID)
  decode,                // decode failure mid-analysis; partial results may exist
  internal,              // a mediadiff bug, not a problem with the user's input
};

// `message` is required now, not optional, because CLI-10's tolerance-grammar
// diagnostic ("wrong unit for this check") must name the expected unit —
// an Error with no message would have nowhere to put it.
struct Error {
  ErrorKind kind;
  std::string message;
};

}  // namespace mediadiff
