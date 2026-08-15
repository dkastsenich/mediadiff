#include "core/glob.h"

#include <string>

namespace mediadiff {

namespace {

// Splits `s` on '.' into segments, preserving empty segments (a leading,
// trailing, or doubled '.' produces one) so callers can detect and reject
// them explicitly rather than have them silently absorbed by the split.
std::vector<std::string_view> split_segments(std::string_view s) {
  std::vector<std::string_view> segments;
  std::size_t start = 0;
  while (true) {
    const std::size_t dot = s.find('.', start);
    if (dot == std::string_view::npos) {
      segments.push_back(s.substr(start));
      break;
    }
    segments.push_back(s.substr(start, dot - start));
    start = dot + 1;
  }
  return segments;
}

// Shared malformed-pattern diagnosis, used by both validate_glob (which
// reports the reason) and the matching functions (which just need a
// bool). Returns an empty message when `pattern` is well-formed.
std::string diagnose(std::string_view pattern) {
  if (pattern.empty()) {
    return "glob pattern is empty; an empty pattern matches zero check ids, never all of them";
  }
  const std::vector<std::string_view> segments = split_segments(pattern);
  for (std::string_view segment : segments) {
    if (segment.empty()) {
      return "glob pattern '" + std::string(pattern) +
             "' has an empty segment (a leading, trailing, or doubled '.')";
    }
  }
  for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
    if (segments[i] == "**") {
      return "glob pattern '" + std::string(pattern) + "' uses '**' outside the final segment";
    }
  }
  return "";
}

}  // namespace

mediadiff::expected<void, Error> validate_glob(std::string_view pattern) {
  const std::string reason = diagnose(pattern);
  if (!reason.empty()) {
    return mediadiff::unexpected(Error{ErrorKind::usage, reason});
  }
  return {};
}

bool glob_matches(std::string_view pattern, std::string_view check_id) {
  if (!diagnose(pattern).empty()) {
    return false;
  }

  const std::vector<std::string_view> pattern_segments = split_segments(pattern);
  const std::vector<std::string_view> id_segments = split_segments(check_id);

  const bool trailing_globstar = pattern_segments.back() == "**";
  const std::size_t fixed_count = trailing_globstar ? pattern_segments.size() - 1 : pattern_segments.size();

  if (trailing_globstar) {
    // "**" must consume at least one trailing segment — it does not match
    // zero, so "container.ts.**" must not match "container.ts".
    if (id_segments.size() <= fixed_count) {
      return false;
    }
  } else if (id_segments.size() != fixed_count) {
    return false;
  }

  for (std::size_t i = 0; i < fixed_count; ++i) {
    if (pattern_segments[i] == "*") {
      continue;  // matches exactly one whole segment, any content
    }
    if (pattern_segments[i] != id_segments[i]) {
      return false;
    }
  }
  return true;
}

std::vector<std::uint32_t> glob_select(std::string_view pattern, const CheckRegistry& registry) {
  std::vector<std::uint32_t> matches;
  std::uint32_t index = 0;
  for (const CheckDef& def : registry) {
    if (glob_matches(pattern, def.id)) {
      matches.push_back(index);
    }
    ++index;
  }
  return matches;
}

}  // namespace mediadiff
