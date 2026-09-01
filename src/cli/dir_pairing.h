#pragma once

// Directory pairing for `dir` mode (doc 01 section 10, DIR-01/DIR-04). Lives
// in src/cli/ because doc 01 section 10 places all `dir`-mode process
// control there -- the per-file analysis this feeds (compare_fingerprints,
// resolve_policy) is unchanged library code under core/ and compare/.
//
// Pairing is computed IN FULL, in a byte-wise sorted, platform-independent
// order, before any worker starts (src/cli/worker_pool.h's bounded pool
// consumes exactly this vector, read-only, by index). This is what makes
// `--threads 1` and `--threads 8` produce byte-identical output: the
// pairing itself never depends on filesystem enumeration order, which
// std::filesystem does not guarantee across platforms or even across two
// runs on the same platform.

#include <string>
#include <vector>

#include "core/error.h"
#include "util/expected.h"

namespace mediadiff {

// One relative path's presence on either side of a `dir` comparison.
// `relative_path` is always forward-slash-separated (T-2-... / DIR-04),
// regardless of platform, so the same corpus pairs identically on Windows
// and POSIX and a report diffs cleanly across machines.
struct FilePair {
  std::string relative_path;
  bool in_baseline;
  bool in_candidate;

  bool operator==(const FilePair&) const = default;
};

// Walks `baseline_root` and `candidate_root` recursively, pairing every
// regular file found in either tree by its path relative to that tree's
// own root, and returns the union in byte-wise sorted order (an explicit
// std::string comparison -- never a locale-aware collation, and never
// std::filesystem::path's own comparison, both of which can differ across
// platforms and locales). The returned vector is the FULL pairing; no
// caller ever partially consumes it, and it is treated as const from the
// moment this function returns.
//
// A symbolic link is never followed -- neither during the recursive walk
// (so a link to a directory cannot pull an unrelated subtree into the
// comparison or produce an unbounded walk through a link cycle, T-2-42)
// nor as a leaf entry (a symlink to a file is skipped, not paired).
//
// A root that does not exist, is not a directory, or cannot be enumerated
// is ErrorKind::input_open. Two empty (or nonexistent-of-neither-relevance
// -- both must exist as directories) roots are NOT an error: the result is
// an empty vector, and the caller reports zero files, zero findings, exit
// 0. The walk is bounded by a maximum entry count and a maximum depth
// (T-2-45); exceeding either is also ErrorKind::input_open, naming which
// bound was hit, rather than silently truncating the corpus or exhausting
// memory on a pathological tree.
mediadiff::expected<std::vector<FilePair>, Error> pair_directories(const std::string& baseline_root,
                                                                       const std::string& candidate_root);

}  // namespace mediadiff
