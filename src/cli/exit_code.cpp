#include "cli/exit_code.h"

namespace mediadiff {

int exit_code_for(ErrorKind kind) {
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

int exit_code_for_findings(const Summary& summary, bool strict) {
  switch (summary.worst_gating) {
    case Severity::fail:
      return kExitFail;
    case Severity::warn:
      return strict ? kExitWarnStrict : kExitClean;
    case Severity::info:
    case Severity::ignore:
      return kExitClean;
  }
  // Unreachable for any valid Severity -- see exit_code_for's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return kExitInternal;
}

}  // namespace mediadiff
