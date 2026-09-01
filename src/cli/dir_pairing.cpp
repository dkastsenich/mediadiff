#include "cli/dir_pairing.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "util/fs.h"

namespace mediadiff {

namespace {

// T-2-45: a pathological tree (a link cycle that slipped past the
// symlink-skip guard some other way, or simply a corpus far larger than
// any real one this tool targets) fails loudly rather than exhausting
// memory building the pair list.
constexpr std::size_t kMaxWalkEntries = 250000;
constexpr int kMaxWalkDepth = 128;

#ifdef _WIN32
// Converts a UTF-8 argv path to a std::filesystem::path via the wide
// construction util/fs.h's utf8_to_wide already performs -- never through
// std::filesystem::path's own narrow-string constructor, which on Windows
// converts via the ambient ANSI code page rather than UTF-8 (this file's
// own read_first: util/fs.h's UTF-8 path conventions). Neither this
// function nor relative_path_to_utf8 below ever names a wide-character
// type directly (wchar_t/LPWSTR/...) -- both chain straight through fs.h's
// own conversion functions, matching that header's "confine wide types to
// fs.h and main.cpp" contract in spirit even though this file, of
// structural necessity, is the first to construct a std::filesystem::path
// from one.
std::filesystem::path utf8_to_path(const std::string& utf8) { return std::filesystem::path(utf8_to_wide(utf8)); }
// generic_wstring() (forward slashes unconditionally, regardless of
// platform) rather than wstring() (native separators) -- this is the one
// call site DIR-04's "relative paths are forward-slash-separated on every
// platform" contract depends on.
std::string relative_path_to_utf8(const std::filesystem::path& relative) {
  return wide_to_utf8(relative.generic_wstring());
}
#else
// POSIX: std::filesystem::path's narrow constructor takes bytes verbatim
// (no code-page conversion exists to misapply), matching util/fs.h's own
// passthrough contract for this platform.
std::filesystem::path utf8_to_path(const std::string& utf8) { return std::filesystem::path(utf8); }
std::string relative_path_to_utf8(const std::filesystem::path& relative) { return relative.generic_string(); }
#endif

// Walks `root` recursively, invoking `visit(relative_path_utf8)` for every
// REGULAR, non-symlink file found -- `relative_path_utf8` is already
// forward-slash-separated (std::filesystem::path::generic_string(), which
// uses '/' unconditionally regardless of platform) and UTF-8
// (relative_path_to_utf8 above). Returns an error for a root that does not
// exist, is not a directory, cannot be enumerated, or exceeds the walk's
// own entry/depth bounds.
template <typename Visit>
mediadiff::expected<void, Error> walk_tree(const std::string& root_utf8, Visit&& visit) {
  const std::filesystem::path root = utf8_to_path(root_utf8);

  std::error_code status_ec;
  const std::filesystem::file_status root_status = std::filesystem::status(root, status_ec);
  // std::filesystem::status() does NOT set status_ec for a merely
  // nonexistent path (that is reported via file_status's own file_type,
  // not the error_code, by design) -- is_directory(root_status) already
  // covers "does not exist" and "exists but is a file" in one check, and
  // status_ec covers a genuine enumeration failure (e.g. an inaccessible
  // parent component).
  if (status_ec || !std::filesystem::is_directory(root_status)) {
    return mediadiff::unexpected(
        Error{ErrorKind::input_open, "directory '" + root_utf8 + "' does not exist or is not a directory"});
  }

  std::error_code it_ec;
  std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::none, it_ec);
  if (it_ec) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "directory '" + root_utf8 + "' could not be enumerated"});
  }
  const std::filesystem::recursive_directory_iterator end;

  std::size_t entry_count = 0;
  while (it != end) {
    if (it.depth() > kMaxWalkDepth) {
      return mediadiff::unexpected(
          Error{ErrorKind::input_open,
                "directory '" + root_utf8 + "' exceeds the maximum walk depth (" + std::to_string(kMaxWalkDepth) + ")"});
    }
    ++entry_count;
    if (entry_count > kMaxWalkEntries) {
      return mediadiff::unexpected(Error{ErrorKind::input_open, "directory '" + root_utf8 +
                                                                      "' exceeds the maximum walk entry count (" +
                                                                      std::to_string(kMaxWalkEntries) + ")"});
    }

    const std::filesystem::directory_entry& entry = *it;

    // T-2-42: a symbolic link is never followed, as a leaf OR as a
    // directory to recurse into -- is_symlink checks symlink_status
    // (never follows), so this check happens before is_regular_file
    // (which would otherwise follow the link to decide).
    std::error_code symlink_ec;
    const bool is_symlink = entry.is_symlink(symlink_ec);
    if (symlink_ec) {
      return mediadiff::unexpected(Error{ErrorKind::input_open, "directory '" + root_utf8 + "' could not be enumerated"});
    }
    if (is_symlink) {
      // directory_options::none (set above) already means the iterator
      // itself never recurses through a symlinked directory -- this skip
      // is what additionally excludes a symlink from being paired as a
      // leaf file.
      it.increment(it_ec);
      if (it_ec) {
        return mediadiff::unexpected(Error{ErrorKind::input_open, "directory '" + root_utf8 + "' could not be enumerated"});
      }
      continue;
    }

    std::error_code regular_ec;
    const bool is_regular = entry.is_regular_file(regular_ec);
    if (regular_ec) {
      return mediadiff::unexpected(Error{ErrorKind::input_open, "directory '" + root_utf8 + "' could not be enumerated"});
    }
    if (is_regular) {
      const std::filesystem::path relative = std::filesystem::relative(entry.path(), root);
      visit(relative_path_to_utf8(relative));
    }

    it.increment(it_ec);
    if (it_ec) {
      return mediadiff::unexpected(Error{ErrorKind::input_open, "directory '" + root_utf8 + "' could not be enumerated"});
    }
  }

  return {};
}

}  // namespace

mediadiff::expected<std::vector<FilePair>, Error> pair_directories(const std::string& baseline_root,
                                                                       const std::string& candidate_root) {
  // std::map<std::string, ...> orders keys by std::string::operator< --
  // byte-wise, never locale-aware -- so walking this map in iteration
  // order already produces DIR-04's own byte-wise sorted order with no
  // separate explicit sort step.
  std::map<std::string, FilePair> by_relative_path;

  auto baseline_result = walk_tree(baseline_root, [&](const std::string& relative_path) {
    auto it = by_relative_path.try_emplace(relative_path, FilePair{relative_path, false, false}).first;
    it->second.in_baseline = true;
  });
  if (!baseline_result) {
    return mediadiff::unexpected(baseline_result.error());
  }

  auto candidate_result = walk_tree(candidate_root, [&](const std::string& relative_path) {
    auto it = by_relative_path.try_emplace(relative_path, FilePair{relative_path, false, false}).first;
    it->second.in_candidate = true;
  });
  if (!candidate_result) {
    return mediadiff::unexpected(candidate_result.error());
  }

  std::vector<FilePair> pairs;
  pairs.reserve(by_relative_path.size());
  for (auto& [relative_path, pair] : by_relative_path) {
    pairs.push_back(std::move(pair));
  }
  return pairs;
}

}  // namespace mediadiff
