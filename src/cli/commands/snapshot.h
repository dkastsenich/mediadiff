#pragma once

#include <string>

#include <CLI/CLI.hpp>

#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// Registers `mediadiff snapshot <file> [--out path] [--force]`.
void register_snapshot_command(CLI::App& app);

// The write path's gate (SNAP-07), factored out of the CLI callback so
// tests/integration/test_snapshot_safe_write.cpp can drive it end to end
// through the real binary without a real media probe (none exists until
// Phase 3) — this is the exact function register_snapshot_command's
// callback itself calls.
//
// Before any byte is written:
//   - If `out_path` exists and `force` is false and `CI=true` is set in
//     the environment, refuses (ErrorKind::usage) regardless of
//     tracked-ness — an automated job overwriting ANY existing baseline
//     without an explicit --force is exactly SNAP-07's failure mode.
//   - Otherwise, if `out_path` exists, is tracked by git (via
//     `git ls-files --error-unmatch`) and `force` is false, refuses
//     (ErrorKind::usage) naming --force. A zero-byte existing file is
//     protected exactly like a non-empty one.
//   - When `out_path` exists but is untracked, or git could not be
//     consulted (unavailable, or the path is outside a repository), the
//     target is treated as untracked and the write proceeds.
// A refusal leaves any existing file at `out_path` byte-identical — the
// gate runs entirely before write_snapshot is ever called.
mediadiff::expected<void, Error> write_snapshot_gated(const Fingerprint& fp, const std::string& out_path, bool force,
                                                       const CheckRegistry& registry);

}  // namespace mediadiff
