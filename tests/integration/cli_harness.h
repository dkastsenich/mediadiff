#pragma once

// Reusable CLI integration harness: spawns the built `mediadiff` binary
// (path injected at configure time via the MEDIADIFF_BINARY compile
// definition, resolved from the `mediadiff` target's real on-disk location)
// and captures its exit code, stdout and stderr independently.
//
// The actual process-spawning primitive lives in tests/process_spawn.h —
// extracted there (02-02-PLAN.md Task 3) so tests/unit/test_registry_generator.cpp
// can spawn the Python interpreter directly, without depending on this
// header's MEDIADIFF_BINARY compile definition (the unit test target never
// sets it). This header is now a thin wrapper: CliResult/EnvVars are aliases
// of ProcessResult/ProcessEnvVars, and run_cli() is run_process() pinned to
// MEDIADIFF_BINARY. Nothing about integration-test call sites changes.
//
// Deliberately free of any assertion logic — later phases reuse this for
// every CLI-level test, and mixing spawn logic with assertions makes it
// unreusable (per 01-02-PLAN.md's action block).
//
// Header-only: all functions are `inline` since this header is included
// from multiple integration test translation units over time.

#include <vector>

#include "process_spawn.h"

namespace mediadiff::test {

// Result of spawning the binary once: the real process exit code (not
// collapsed into a boolean), plus stdout and stderr captured separately so
// "nothing on stderr" is an assertable property.
using CliResult = ProcessResult;

// Environment variable overrides for the child process, as (name, value)
// pairs. When passed to run_cli, the child's entire environment is replaced
// with exactly this set rather than inheriting the caller's environment —
// this is what makes the reduced-PATH case possible without mutating the
// test process's own environment.
using EnvVars = ProcessEnvVars;

// Spawns the built mediadiff binary (MEDIADIFF_BINARY, injected by
// tests/integration/CMakeLists.txt from the `mediadiff` target's real
// location) with `args`, capturing its exit code, stdout and stderr. When
// `env_override` is non-null, the child's environment is replaced entirely
// with the given (name, value) pairs instead of inheriting the caller's
// environment.
inline CliResult run_cli(const std::vector<std::string>& args, const EnvVars* env_override = nullptr) {
  return run_process(MEDIADIFF_BINARY, args, env_override);
}

}  // namespace mediadiff::test
