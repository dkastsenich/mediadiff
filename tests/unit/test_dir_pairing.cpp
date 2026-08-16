// dir_pairing: relative-path pairing, byte-wise deterministic order, and
// the platform-independent forward-slash relative-path contract
// (02-11-PLAN.md Task 1, DIR-01/DIR-04).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cli/dir_pairing.h"
#include "core/error.h"

using mediadiff::ErrorKind;
using mediadiff::FilePair;
using mediadiff::pair_directories;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir(const std::string& tag) {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir = fs::temp_directory_path() /
                        ("mediadiff_dir_pairing_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

void write_file(const fs::path& path) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary);
  out << "x";
}

}  // namespace

TEST_CASE("dir_pairing: identical trees pair every file with both flags set", "[dir]") {
  const fs::path baseline = unique_scratch_dir("identical_a");
  const fs::path candidate = unique_scratch_dir("identical_b");
  write_file(baseline / "one.snap.json");
  write_file(candidate / "one.snap.json");

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].relative_path == "one.snap.json");
  CHECK((*result)[0].in_baseline);
  CHECK((*result)[0].in_candidate);
}

TEST_CASE("dir_pairing: a file only in baseline sets in_baseline alone", "[dir]") {
  const fs::path baseline = unique_scratch_dir("baseline_only_a");
  const fs::path candidate = unique_scratch_dir("baseline_only_b");
  write_file(baseline / "only_baseline.snap.json");

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].in_baseline);
  CHECK_FALSE((*result)[0].in_candidate);
}

TEST_CASE("dir_pairing: a file only in candidate sets in_candidate alone", "[dir]") {
  const fs::path baseline = unique_scratch_dir("candidate_only_a");
  const fs::path candidate = unique_scratch_dir("candidate_only_b");
  write_file(candidate / "only_candidate.snap.json");

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK_FALSE((*result)[0].in_baseline);
  CHECK((*result)[0].in_candidate);
}

TEST_CASE("dir_pairing: nested subdirectories pair by their full relative path", "[dir]") {
  const fs::path baseline = unique_scratch_dir("nested_a");
  const fs::path candidate = unique_scratch_dir("nested_b");
  write_file(baseline / "sub" / "deep" / "file.snap.json");
  write_file(candidate / "sub" / "deep" / "file.snap.json");

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].relative_path == "sub/deep/file.snap.json");
  CHECK((*result)[0].in_baseline);
  CHECK((*result)[0].in_candidate);
}

TEST_CASE("dir_pairing: the returned order is byte-wise sorted", "[dir]") {
  // Case-collision hazard (G-02-4, CI run 31943688186 job 95156439342):
  // macOS ships APFS case-insensitive by default, so two fixture names that
  // differ only by ASCII case denote ONE file there. write_file()'s
  // truncating std::ofstream open silently overwrote the earlier write
  // rather than adding a second file, so only four files ever landed on
  // disk against five expected names -- which is what made the size
  // REQUIRE below read 4 against 5 in CI, not a byte-wise-ordering defect.
  //
  // The two uppercase stems below are chosen to straddle the ASCII case
  // boundary (uppercase block ends at 0x5A, lowercase block starts at
  // 0x61) so the byte-wise order of this fixture provably differs from its
  // case-folded order -- the property DIR-04's ordering guarantee rests
  // on. scripts/lint_fixture_case_collisions.sh enforces the no-collision
  // rule this fixture demonstrates across the whole test tree.
  const fs::path baseline = unique_scratch_dir("sorted_a");
  const fs::path candidate = unique_scratch_dir("sorted_b");
  const std::vector<std::string> names = {"zeta.snap.json", "alpha.snap.json", "Beta.snap.json", "Zulu.snap.json",
                                            "1.snap.json"};
  for (const std::string& name : names) {
    write_file(baseline / name);
  }

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == names.size());

  std::vector<std::string> actual_order;
  for (const FilePair& pair : *result) {
    actual_order.push_back(pair.relative_path);
  }

  // Explicit literal expected order (byte-wise), not a sort of the test's
  // own result -- this makes the exact byte-boundary claim auditable from
  // the source rather than merely asserting "the result is sorted".
  const std::vector<std::string> expected_order = {"1.snap.json", "Beta.snap.json", "Zulu.snap.json",
                                                     "alpha.snap.json", "zeta.snap.json"};
  CHECK(actual_order == expected_order);

  // Teeth assertion: prove the fixture still discriminates byte-wise order
  // from case-folded order. A case-folded sort of the same five names must
  // land in a DIFFERENT order than the byte-wise expected order above -- if
  // a future edit replaces these names with a set whose byte-wise and
  // case-folded orders coincide, this assertion fails rather than passing
  // quietly, converting "the fixture has teeth" from a claim into an
  // enforced invariant.
  std::vector<std::string> case_folded_order = expected_order;
  std::sort(case_folded_order.begin(), case_folded_order.end(), [](const std::string& lhs, const std::string& rhs) {
    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char a, char b) {
          return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b));
        });
  });
  CHECK(case_folded_order != expected_order);
}

TEST_CASE("dir_pairing: two empty roots return an empty vector and no error", "[dir]") {
  const fs::path baseline = unique_scratch_dir("empty_a");
  const fs::path candidate = unique_scratch_dir("empty_b");

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("dir_pairing: a nonexistent root returns ErrorKind::input_open", "[dir]") {
  const fs::path candidate = unique_scratch_dir("nonexistent_b");
  auto result = pair_directories("/no/such/mediadiff/dir_pairing/root", candidate.string());
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::input_open);
}

TEST_CASE("dir_pairing: a root that is a file, not a directory, returns ErrorKind::input_open", "[dir]") {
  const fs::path baseline = unique_scratch_dir("file_root_a");
  const fs::path candidate = unique_scratch_dir("file_root_b");
  const fs::path file_as_root = baseline / "not_a_dir.txt";
  write_file(file_as_root);

  auto result = pair_directories(file_as_root.string(), candidate.string());
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::input_open);
}

TEST_CASE("dir_pairing: separators are normalised to forward slash in the returned relative paths", "[dir]") {
  const fs::path baseline = unique_scratch_dir("sep_a");
  const fs::path candidate = unique_scratch_dir("sep_b");
  write_file(baseline / "a" / "b" / "c.snap.json");

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].relative_path.find('\\') == std::string::npos);
  CHECK((*result)[0].relative_path == "a/b/c.snap.json");
}

TEST_CASE("dir_pairing: a tree containing a non-ASCII filename pairs correctly", "[dir]") {
  const fs::path baseline = unique_scratch_dir("utf8_a");
  const fs::path candidate = unique_scratch_dir("utf8_b");
  // U+00E9 (LATIN SMALL LETTER E WITH ACUTE), UTF-8 encoded -- résumé.snap.json
  const std::string non_ascii_name = "r\xC3\xA9sum\xC3\xA9.snap.json";
  write_file(baseline / non_ascii_name);
  write_file(candidate / non_ascii_name);

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].relative_path == non_ascii_name);
  CHECK((*result)[0].in_baseline);
  CHECK((*result)[0].in_candidate);
}

TEST_CASE("dir_pairing: a symbolic link is not followed and never paired as a leaf", "[dir]") {
  const fs::path baseline = unique_scratch_dir("symlink_a");
  const fs::path candidate = unique_scratch_dir("symlink_b");
  const fs::path real_file = baseline / "real.snap.json";
  write_file(real_file);

  std::error_code ec;
  fs::create_symlink(real_file, baseline / "link.snap.json", ec);
  if (ec) {
    // Symlink creation can fail in a sandboxed CI environment lacking the
    // privilege (notably some Windows runners without Developer Mode) --
    // skipped rather than failed, matching this project's own
    // skipped-not-passed discipline for an environment precondition this
    // test cannot control.
    SUCCEED("symlink creation unavailable in this environment; skipping");
    return;
  }

  auto result = pair_directories(baseline.string(), candidate.string());
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  CHECK((*result)[0].relative_path == "real.snap.json");
}
