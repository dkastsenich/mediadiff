#pragma once

// The CLI surface's exit-code contract (doc 00 section 3.1): "<3" means a
// regression finding, ">=64" means the run could not produce a verdict at
// all — a CI script can branch on that split alone without knowing
// anything else about mediadiff.
//
// Split into this header plus exit_code.cpp (02-10-PLAN.md Task 2) so a
// second function (exit_code_for_findings, below) can share the same
// translation unit without becoming a second `inline` definition repeated
// into every TU that includes this header.

#include "core/error.h"
#include "report/model.h"

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
int exit_code_for(ErrorKind kind);

// The second half of the exit-code contract (doc 00 section 3.1, CLI-06):
// derives the process exit code from a compare run's worst resolved
// GATING severity (Summary::worst_gating -- an axis independent of any
// individual finding's Status, src/report/model.h's own comment on why)
// and whether --strict was given. `kExitFail` when the worst gating
// severity is `Severity::fail` (unconditional -- --strict never changes
// whether `fail` fails); `kExitWarnStrict` when the worst is
// `Severity::warn` and `strict` is true; `kExitClean` otherwise (including
// a worst of `Severity::warn` without --strict, and `Severity::info`/
// `Severity::ignore`). Also a switch with no default: arm, same rationale
// as exit_code_for above.
int exit_code_for_findings(const Summary& summary, bool strict);

}  // namespace mediadiff
