#pragma once

// The CLI surface's exit-code contract (doc 00 section 3.1): "<3" means a
// regression finding, ">=64" means the run could not produce a verdict at
// all — a CI script can branch on that split alone without knowing
// anything else about mediadiff.

#include "core/error.h"

namespace mediadiff {

constexpr int kExitClean = 0;         // no fail, no warn-under-strict
constexpr int kExitFail = 1;          // at least one `fail`-status finding
constexpr int kExitWarnStrict = 2;    // worst finding is `warn` and --strict was given
constexpr int kExitUsage = 64;        // malformed argv or config; no input was opened
constexpr int kExitInput = 65;        // an input did not open, or did not parse
constexpr int kExitDecode = 66;       // decode failure mid-analysis; partial JSON may exist
constexpr int kExitInternal = 70;     // a mediadiff bug, not a problem with the user's input

// The ONE place ErrorKind -> exit code translation happens (doc 01 section
// 11). Written as a switch with NO default: arm so that adding a new
// ErrorKind enumerator without extending this mapping is a -Werror
// (-Wswitch) compile failure, not a silent fallthrough to some arbitrary
// code.
inline int exit_code_for(ErrorKind kind) {
  switch (kind) {
    case ErrorKind::usage:
      return kExitUsage;
    case ErrorKind::input_open:
      return kExitInput;
    case ErrorKind::input_unsupported:
      return kExitInput;
    case ErrorKind::decode:
      return kExitDecode;
    case ErrorKind::internal:
      return kExitInternal;
  }
  // Unreachable for any valid ErrorKind: every enumerator is handled above,
  // and -Wswitch (part of -Wall, promoted to -Werror project-wide) already
  // turns a future enumerator added without a matching case into a build
  // failure at the switch itself — deliberately with NO default: arm, so
  // that failure happens there rather than silently falling through to
  // this line. This return exists only to satisfy -Wreturn-type.
  return kExitInternal;
}

}  // namespace mediadiff
