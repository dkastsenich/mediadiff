#pragma once

// Resolves fixture/golden/snapshot directories from compile definitions
// injected by CMake (MEDIADIFF_FIXTURE_DIR, MEDIADIFF_GOLDEN_DIR) rather
// than from the process's current working directory, so `ctest` invoked
// from any directory finds the same files -- Catch.cmake's own
// WORKING_DIRECTORY default is the build tree, not the source tree.
//
// Header-only: both functions are `inline` since this header is included
// from multiple test translation units over time.

#include <string>

namespace mediadiff::test {

// tests/fixtures/ -- MEDIADIFF_FIXTURE_DIR, injected by
// tests/unit/CMakeLists.txt and tests/integration/CMakeLists.txt.
inline std::string fixture_dir() { return std::string(MEDIADIFF_FIXTURE_DIR); }

// tests/golden/ -- MEDIADIFF_GOLDEN_DIR, injected the same way. Read-only
// unless UPDATE_GOLDENS is set (D-12) -- see tests/support/golden.h.
inline std::string golden_dir() { return std::string(MEDIADIFF_GOLDEN_DIR); }

// tests/fixtures/snapshots/ -- the hand-authored *.snap.json tree (D-10).
inline std::string snapshot_dir() { return fixture_dir() + "/snapshots"; }

}  // namespace mediadiff::test
