#include "support/golden.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "support/fixture_paths.h"
#include "util/fs.h"

namespace mediadiff::test {

namespace {

// Reads a getenv() result honestly: unset AND empty are both "not set" for
// UPDATE_GOLDENS's purposes (D-12) -- `UPDATE_GOLDENS=` on a CI runner that
// merely exports the variable name without a value must not silently
// re-enable the local-developer refresh path.
bool update_goldens_requested() {
  const char* value = std::getenv("UPDATE_GOLDENS");
  return value != nullptr && value[0] != '\0';
}

std::string golden_path(std::string_view case_name) { return golden_dir() + "/" + std::string(case_name) + ".txt"; }

std::string read_whole_file(FILE* handle) {
  std::string content;
  char buf[8192];
  std::size_t read_bytes = 0;
  while ((read_bytes = std::fread(buf, 1, sizeof(buf), handle)) > 0) {
    content.append(buf, read_bytes);
  }
  return content;
}

// The first line (1-based) at which `expected` and `actual` diverge. A
// line present on only one side is reported against an empty string on
// the other. Returns line 0 when the two strings are identical.
struct FirstDiff {
  std::size_t line = 0;
  std::string expected_line;
  std::string actual_line;
};

FirstDiff first_diff(const std::string& expected, const std::string& actual) {
  std::istringstream expected_stream(expected);
  std::istringstream actual_stream(actual);
  std::string expected_line;
  std::string actual_line;
  std::size_t line_number = 0;
  for (;;) {
    const bool has_expected = static_cast<bool>(std::getline(expected_stream, expected_line));
    const bool has_actual = static_cast<bool>(std::getline(actual_stream, actual_line));
    if (!has_expected && !has_actual) {
      return FirstDiff{};
    }
    ++line_number;
    if (!has_expected) {
      return FirstDiff{line_number, "", actual_line};
    }
    if (!has_actual) {
      return FirstDiff{line_number, expected_line, ""};
    }
    if (expected_line != actual_line) {
      return FirstDiff{line_number, expected_line, actual_line};
    }
  }
}

}  // namespace

std::optional<std::string> golden_check_result(std::string_view case_name, std::string_view actual) {
  const std::string path = golden_path(case_name);

  if (update_goldens_requested()) {
    FILE* handle = fopen_utf8(path, "wb");
    if (handle == nullptr) {
      return "UPDATE_GOLDENS: could not open '" + path + "' for writing";
    }
    std::fwrite(actual.data(), 1, actual.size(), handle);
    std::fclose(handle);
    return std::nullopt;
  }

  FILE* handle = fopen_utf8(path, "rb");
  if (handle == nullptr) {
    // A missing golden file is a failure, never an implicit create (D-12)
    // -- otherwise the first run of a broken renderer would mint its own
    // output as the "expected" answer.
    return "golden file missing: '" + path + "' (refresh locally with UPDATE_GOLDENS=1 ctest ...; CI never sets it)";
  }
  const std::string expected = read_whole_file(handle);
  std::fclose(handle);

  const std::string actual_str(actual);
  if (expected == actual_str) {
    return std::nullopt;
  }

  const FirstDiff diff = first_diff(expected, actual_str);
  std::ostringstream message;
  message << "golden mismatch for '" << case_name << "' at line " << diff.line << ":\n"
          << "  expected: " << diff.expected_line << "\n"
          << "  actual:   " << diff.actual_line << "\n"
          << "  (refresh locally with UPDATE_GOLDENS=1 ctest ...; CI never sets UPDATE_GOLDENS)";
  return message.str();
}

void check_golden(std::string_view case_name, std::string_view actual) {
  const std::optional<std::string> result = golden_check_result(case_name, actual);
  if (!result.has_value()) {
    SUCCEED("golden '" << case_name << "' matches (or was refreshed)");
    return;
  }
  FAIL(*result);
}

}  // namespace mediadiff::test
